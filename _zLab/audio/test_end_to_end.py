#!/usr/bin/env python3
"""Bounded opt-in real Session and fake local audio duplex integration.

A test-only native driver loops scheduled PCM into dated capture; it is not a
physical audio device or RunPod test. Set LAB_RUN_LIVE_E2E=1 and provide
LAB_AUDIO_WORKER and LAB_FAKE_AUDIO paths to exercise the CPU receiver.
"""
import json
import os
import re
from pathlib import Path
import select
import struct
import subprocess
import sys
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
RELAY = ROOT / '_zLab/worker_relay.py'
TRANSPORT = ROOT / '_zLab/audio/transport.py'
MAX_FRAME = 8 * 1024 * 1024


def frame(magic, kind, payload=b''):
    return struct.pack('<4sII', magic, kind, len(payload)) + payload


def text(value):
    value = value.encode('utf-8')
    return struct.pack('<I', len(value)) + value


def complete_observation(descriptions, received_rows, drained):
    """Require this fixture's exact ordered bits, physical quiet and drain."""
    if not drained or not any('decoded' in row and 'e' in row for row in received_rows):
        return False
    data = [index for index, row in enumerate(descriptions) if '· DATA ' in row]
    if len(data) < 3:
        return False
    start, last = data[-3], data[-1]
    selected = descriptions[start:last + 3]
    if len(selected) != 5 or any('· incomplete evidence' in row for row in selected):
        return False
    if not all(token in row for row, token in zip(selected,
               ('· DATA 0 ·', '· DATA 0 ·', '· DATA 1 ·', '· QUIET ·', '· QUIET ·'))):
        return False
    slots = [re.search(r'Slot (\d+) ·', row) for row in selected]
    return (all(slots) and [int(match.group(1)) for match in slots] ==
            list(range(int(slots[0].group(1)), int(slots[0].group(1)) + 5)))


class Driver:
    def __init__(self, worker, local, timeout=480, extra_env=None):
        remote = [sys.executable, str(RELAY), '--live', '--file-channel', worker]
        # Enough finite capacity for this short, predeclared test only; actual
        # receipt of audio is bounded independently by the native stop plan.
        local = [local, '--device', 'fake', '--capture-bytes', str(64 * 1024 * 1024),
                 '--playback-bytes', str(4 * 1024 * 1024), '--capture-frames', '3600000',
                 '--playback-prebuffer-frames', '4096']
        argv = [sys.executable, str(TRANSPORT), '--max-clock-offset', '0.005',
                '--local-utc-uncertainty', '0.0001', '--remote-clock-drift-ppm', '10',
                '--remote-json', json.dumps(remote), '--local-json', json.dumps(local)]
        if os.environ.get('LAB_LIVE_DEBUG') == '1':
            argv.extend(('--debug-child-stderr', '--debug-audio-summary'))
        cpus = sorted(os.sched_getaffinity(0))
        self.proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, bufsize=0,
                                     env={**os.environ, **(extra_env or {})}, preexec_fn=lambda: os.sched_setaffinity(0, cpus[:4]))
        self.deadline = time.monotonic() + timeout
        self.snapshot = None
        self.last = []
        self.saw_transmission_drain = False

    def read_exact(self, count):
        out = bytearray()
        while len(out) < count:
            remaining = self.deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.proc.stdout], [], [], remaining)[0]:
                raise AssertionError('live integration deadline; last UI: ' + repr(self.last[-8:]))
            data = os.read(self.proc.stdout.fileno(), count - len(out))
            if not data:
                error = self.proc.stderr.read(8192).decode('utf-8', 'replace')
                raise AssertionError('live transport closed: ' + error + '; last UI: ' + repr(self.last[-8:]))
            out.extend(data)
        return bytes(out)

    def update(self):
        magic, kind, size = struct.unpack('<4sII', self.read_exact(12))
        if magic != b'DPW1' or size > MAX_FRAME:
            raise AssertionError('invalid live output frame')
        payload = self.read_exact(size)
        if kind == 101:
            self.snapshot = json.loads(payload)
            # Keep only a short bounded diagnostic summary; no credentials.
            labels = [item.get('text', '') for item in self.snapshot.get('controls', [])
                      if item.get('kind') == 'label' and item.get('text')]
            self.last = (self.last + labels)[-16:]
            self.saw_transmission_drain |= 'Robust transmission sent' in labels
            if os.environ.get('LAB_LIVE_TRACE') == '1':
                important = [v for v in labels if any(k in v for k in ('Robust ', 'TX:', 'quiet', 'error', 'failed'))]
                cancel = next((v for v in self.snapshot['controls'] if v.get('label') == 'Cancel TX'), {})
                transition = (tuple(important), cancel.get('enabled'))
                if important and transition != getattr(self, '_previous_labels', None):
                    print('live fixture:', ' | '.join(important), 'Cancel TX:', cancel.get('enabled'), file=sys.stderr, flush=True)
                    if (os.environ.get('LAB_LIVE_TRACE_ALL') == '1' and
                            getattr(self, '_previous_cancel', False) and not cancel.get('enabled')):
                        print('live fixture labels on stop:', repr(labels), file=sys.stderr, flush=True)
                    self._previous_labels = transition
                    self._previous_cancel = cancel.get('enabled')
        elif kind == 103:
            raise AssertionError('native operation error: ' + payload[4:].decode('utf-8', 'replace'))
        elif kind == 104:
            raise AssertionError('native worker closed before completion')

    def wait(self, predicate):
        while not predicate():
            self.update()

    def control(self, label):
        return next((item for item in self.snapshot['controls']
                     if item.get('label', '').split(' · Budget target ', 1)[0] == label), None)

    def event(self, kind, target, value=''):
        sequence = int(self.snapshot['ack']) + 1
        payload = struct.pack('<IQQQIII', 1, int(self.snapshot['generation']), sequence,
                              int(target), kind, 0, 0) + text(value) + text('')
        self.proc.stdin.write(frame(b'DPL1', 0, frame(b'DPW1', 2, payload)))
        self.wait(lambda: int(self.snapshot['ack']) >= sequence)

    def page(self, name):
        tab = next(item for item in self.snapshot['tabs'] if item['label'] == name)
        self.event(11, tab['id'])
        self.wait(lambda: any(item['label'] == name and item['selected'] for item in self.snapshot['tabs']))

    def edit(self, label, value, kind=1):
        control = self.control(label)
        if control is None:
            raise AssertionError('missing control: ' + label)
        self.event(kind, control['id'], value)

    def close(self):
        self.proc.stdin.close()
        try:
            self.proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait()
        self.proc.stdout.close()
        if os.environ.get('LAB_LIVE_DEBUG') == '1':
            diagnostics = self.proc.stderr.read(16384).decode('utf-8', 'replace')
            if diagnostics:
                print('fake-device diagnostics:', diagnostics, file=sys.stderr, flush=True)
        self.proc.stderr.close()


class DecisionPredicateTests(unittest.TestCase):
    def test_requires_ordered_complete_bits_quiet_and_drain(self):
        decisions = ('DATA 0', 'DATA 0', 'DATA 1', 'QUIET', 'QUIET')
        rows = [f'Slot {i + 7} · epoch 1 / ordinal 2 · {decision} · evidence 1'
                for i, decision in enumerate(decisions)]
        received = [['decoded', 'e']]
        self.assertTrue(complete_observation(rows, received, True))
        self.assertFalse(complete_observation(rows, received, False))
        self.assertFalse(complete_observation(rows, [], True))
        for index, replacement in ((2, 'DATA 0'), (3, 'AMBIGUOUS')):
            altered = rows.copy()
            altered[index] = altered[index].replace(decisions[index], replacement)
            self.assertFalse(complete_observation(altered, received, True))
        incomplete = rows.copy(); incomplete[4] += ' · incomplete evidence'
        self.assertFalse(complete_observation(incomplete, received, True))
        gap = rows.copy(); gap[4] = gap[4].replace('Slot 11', 'Slot 12')
        self.assertFalse(complete_observation(gap, received, True))
        interrupted = rows[:4] + ['Slot 11 · AMBIGUOUS · evidence'] + [rows[4].replace('Slot 11', 'Slot 12')]
        self.assertFalse(complete_observation(interrupted, received, True))


class LiveEndToEnd(unittest.TestCase):
    @unittest.skipUnless(os.environ.get('LAB_RUN_LIVE_E2E') == '1', 'explicit opt-in for bounded live DSP')
    def test_fake_physical_message_through_native_worker(self):
        worker = os.environ.get('LAB_AUDIO_WORKER', '')
        fake = os.environ.get('LAB_FAKE_AUDIO', '')
        self.assertTrue(Path(worker).is_file(), 'LAB_AUDIO_WORKER must name the current lab worker')
        self.assertTrue(Path(fake).is_file(), 'LAB_FAKE_AUDIO must name the fake device companion')
        self.assertNotIn('LAB_AUDIO_FIXTURE_PREHISTORY_SECONDS', os.environ, 'full holdout must use real-time capture')
        self.assertNotIn('LAB_ANCHOR_AGE', os.environ, 'full holdout must use its declared calendar')
        driver = Driver(worker, fake)
        try:
            driver.wait(lambda: driver.snapshot is not None)
            driver.wait(lambda: driver.control('Rate') is not None)
            # Settings are the normal production GUI controls, with a strong,
            # short signal and one-sided normalized PCM noise density.
            for label, value in (('Clock sync', '1ms'), ('Region', '1ms'), ('Offset', '0ms'),
                                 ('Audio error', '50ms'), ('Margin', '1x'), ('Rate', '120 Hz'),
                                 ('Target SNR (dB-Hz)', '80')):
                driver.edit(label, value)
            for label in ('Baseband Osc', 'Shift Osc'):
                driver.edit(label, 'gpsdo-ocxo', 2)
            driver.page('Robust setup')
            # Anchor the public calendar in the recent past so normal receiver
            # rearm is required; never infer completion from a local TX length.
            driver.edit('Calendar anchor (UTC seconds)', str(int(time.time()) - 10))
            driver.edit('Initial waveform ordinal', '7000037')
            driver.edit('Live input noise (dBFS/Hz)', '-100')
            driver.page('Console')
            message = next(item for item in driver.snapshot['controls']
                           if item.get('kind') == 'text' and item.get('byteLimit') == 4 * 1024 * 1024)
            driver.event(1, message['id'], 'e')
            driver.wait(lambda: bool(driver.control('Transmit') and driver.control('Transmit').get('enabled')))
            driver.event(4, driver.control('Transmit')['id'])
            def completed():
                signals = (driver.control('Signals') or {}).get('records', [])
                rows = [[cell.get('text', '') for cell in row['cells']] for row in signals]
                steps = (driver.control('Pattern steps') or {}).get('records', [])
                descriptions = [' '.join(cell.get('text', '') for cell in row['cells']) for row in steps]
                return complete_observation(descriptions, rows, driver.saw_transmission_drain)
            while not completed():
                driver.update()
                cancel = driver.control('Cancel TX')
                if cancel and not cancel.get('enabled'):
                    labels = [item.get('text', '') for item in driver.snapshot.get('controls', []) if item.get('text')]
                    errors = [value for value in labels if any(term in value.lower() for term in
                              ('timed out', 'not verified', 'failed', 'discontinuity', 'underrun', 'refused'))]
                    if errors:
                        raise AssertionError('finite transmission failed before completion: ' + repr(errors))
            steps = (driver.control('Pattern steps') or {}).get('records', [])
            descriptions = [' '.join(cell.get('text', '') for cell in row['cells']) for row in steps]
            signals = (driver.control('Signals') or {}).get('records', [])
            rows = [[cell.get('text', '') for cell in row['cells']] for row in signals]
            self.assertTrue(complete_observation(descriptions, rows, driver.saw_transmission_drain), descriptions)
            # Finite capture end is a normal drain only after native physical
            # completion, and must not surface as an input-EOF receiver error.
            driver.wait(lambda: any(item.get('text') == 'Finite Robust capture drained after physical completion'
                                    for item in driver.snapshot.get('controls', [])))
        finally:
            driver.close()


    @unittest.skipUnless(os.environ.get('LAB_RUN_LIVE_CANCEL') == '1', 'explicit opt-in for bounded live cancellation fixture')
    def test_cancel_during_quiet_rearm_is_incomplete(self):
        worker = os.environ.get('LAB_AUDIO_WORKER', '')
        fake = os.environ.get('LAB_FAKE_AUDIO', '')
        self.assertTrue(Path(worker).is_file(), 'LAB_AUDIO_WORKER must name the current lab worker')
        self.assertTrue(Path(fake).is_file(), 'LAB_FAKE_AUDIO must name the fake device companion')
        driver = Driver(worker, fake, timeout=40)
        try:
            driver.wait(lambda: driver.snapshot is not None)
            driver.page('Robust setup')
            driver.edit('Calendar anchor (UTC seconds)', str(int(time.time()) - 10))
            driver.edit('Initial waveform ordinal', '7000037')
            driver.edit('Live input noise (dBFS/Hz)', '-100')
            driver.page('Console')
            message = next(item for item in driver.snapshot['controls']
                           if item.get('kind') == 'text' and item.get('byteLimit') == 4 * 1024 * 1024)
            driver.event(1, message['id'], 'e')
            driver.wait(lambda: bool(driver.control('Transmit') and driver.control('Transmit').get('enabled')))
            driver.event(4, driver.control('Transmit')['id'])
            driver.wait(lambda: bool(driver.control('Cancel TX') and driver.control('Cancel TX').get('enabled')))
            driver.event(4, driver.control('Cancel TX')['id'])
            driver.wait(lambda: bool(driver.control('Cancel TX') and not driver.control('Cancel TX').get('enabled')))
            signals = (driver.control('Signals') or {}).get('records', [])
            rows = [[cell.get('text', '') for cell in row['cells']] for row in signals]
            self.assertFalse(any('decoded' in row for row in rows), rows)
        finally:
            driver.close()

    @unittest.skipUnless(os.environ.get('LAB_RUN_LIVE_EOF') == '1', 'explicit opt-in for bounded live EOF fixture')
    def test_early_physical_capture_eof_is_incomplete(self):
        worker = os.environ.get('LAB_AUDIO_WORKER', '')
        fake = os.environ.get('LAB_FAKE_AUDIO', '')
        self.assertTrue(Path(worker).is_file(), 'LAB_AUDIO_WORKER must name the current lab worker')
        self.assertTrue(Path(fake).is_file(), 'LAB_FAKE_AUDIO must name the fake device companion')
        driver = Driver(worker, fake, timeout=40, extra_env={'LAB_AUDIO_FIXTURE_EOF_FRAMES': '3000'})
        try:
            driver.wait(lambda: driver.snapshot is not None)
            driver.page('Robust setup')
            driver.edit('Calendar anchor (UTC seconds)', str(int(time.time()) - 10))
            driver.edit('Initial waveform ordinal', '7000037')
            driver.edit('Live input noise (dBFS/Hz)', '-100')
            driver.page('Console')
            driver.wait(lambda: any('physical capture ended before its planned cutoff' in item.get('text', '').lower()
                                    for item in driver.snapshot['controls']))
            signals = (driver.control('Signals') or {}).get('records', [])
            cells = [[cell.get('text', '') for cell in row['cells']] for row in signals]
            self.assertFalse(any('decoded' in row for row in cells), cells)
        finally:
            driver.close()


if __name__ == '__main__':
    unittest.main()

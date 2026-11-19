#!/usr/bin/env python3
import json
import os
from pathlib import Path
import signal
import struct
import sys
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.request
from unittest import mock
from urllib.parse import urlsplit

import browser_bridge


def frame(kind, payload=b''):
    return b'DPW1' + struct.pack('<II', kind, len(payload)) + payload


PEER = r'''
import struct, sys

def frame(t,p=b''):
    return b'DPW1'+struct.pack('<II',t,len(p))+p
sys.stdout.buffer.write(frame(101,b'{"generation":"1","ack":"0"}'))
sys.stdout.buffer.flush()
while True:
    h=sys.stdin.buffer.read(12)
    if not h: break
    if len(h)!=12: sys.exit(2)
    magic,t,n=struct.unpack('<4sII',h)
    data=sys.stdin.buffer.read(n)
    if len(data)!=n: sys.exit(2)
    sys.stdout.buffer.write(frame(103,struct.pack('<I',0)))
    sys.stdout.buffer.flush()
'''

READY_PEER = PEER.replace(
    '{"generation":"1","ack":"0"}',
    '{"version":1,"generation":"1","ack":"0","controls":'
    '[{"kind":"label","text":"Robust simulation ready"}]}',
)


LIVE_PEER = r'''
import json,struct,sys

def frame(t,p=b''):
    return b'DPW1'+struct.pack('<II',t,len(p))+p
resource={'capture_cap_bytes':1000,'playback_cap_bytes':2000,
          'capture_workspace_bytes':None,'playback_workspace_bytes':None,
          'companion_shared_control_limit_bytes':524288,'transport_buffer_allowance_bytes':134217728,
          'browser_host_allowance_bytes':536870912,'companion_worker_stack_count':3,
          'companion_worker_stack_bytes':None,'workspace_includes_buffer_cap':True,
          'companion_control_included_in_browser_allowance':False,
          'workspace_receipts_are_conservative':True,'receipt_is_aggregate':False,
          'process_baseline_included':False}
sys.stdout.buffer.write(frame(190,json.dumps(resource).encode())+frame(101,b'{"generation":"1","ack":"0"}'))
sys.stdout.buffer.flush()
while True:
    h=sys.stdin.buffer.read(12)
    if not h: break
    magic,channel,n=struct.unpack('<4sII',h)
    packet=sys.stdin.buffer.read(n)
    if magic!=b'DPL1' or channel!=0 or packet[:4]!=b'DPW1': sys.exit(4)
    kind=struct.unpack_from('<I',packet,4)[0]
    sys.stdout.buffer.write(frame(108 if kind==15 else 103,b'0000'))
    sys.stdout.buffer.flush()
'''


class LiveTransportTests(unittest.TestCase):
    def test_live_channel_receipts_and_restrictions(self):
        worker = browser_bridge.SimulationWorker([sys.executable, '-u', '-c', LIVE_PEER], live=True)
        try:
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.snapshot is not None or worker.output.error, timeout=5))
                self.assertIsNone(worker.output.error)
                self.assertEqual(worker.resources['transport_buffer_allowance_bytes'], 134217728)
            self.assertEqual(struct.unpack_from('<I', worker.output.get(), 4)[0], 101)
            worker.input(frame(15, struct.pack('<Qd', 7, 0.0)))
            self.assertEqual(struct.unpack_from('<I', worker.output.get(), 4)[0], 108)
            with self.assertRaises(browser_bridge._preview.PreviewError):
                worker.input(frame(3))
            with self.assertRaises(browser_bridge._preview.PreviewError):
                worker.write(frame(10), trusted=True)
        finally:
            worker.close()

    def test_receipt_from_actual_transport_is_accepted(self):
        transport = Path(__file__).resolve().parent / 'audio' / 'transport.py'
        child = json.dumps([sys.executable, '-c', 'import time; time.sleep(30)'])
        command = [sys.executable, str(transport), '--max-clock-offset', '0.1',
                   '--local-utc-uncertainty', '0.1', '--remote-clock-drift-ppm', '1',
                   '--remote-json', child, '--local-json', child, '--report-resources']
        worker = browser_bridge.SimulationWorker(command, live=True)
        try:
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.resources is not None or worker.output.error, timeout=8))
                self.assertIsNone(worker.output.error)
                self.assertEqual(worker.resources['browser_host_allowance_bytes'], 536870912)
                self.assertEqual(worker.resources['companion_worker_stack_count'], 3)
                self.assertIsNone(worker.resources['companion_worker_stack_bytes'])
                self.assertTrue(worker.resources['workspace_includes_buffer_cap'])
                self.assertFalse(worker.resources['receipt_is_aggregate'])
        finally:
            worker.close()

    def test_invalid_resource_receipt_is_not_forwarded(self):
        script = ("import struct,sys,time; p=b'{\"danger\":\"<script>\"}'; "
                  "sys.stdout.buffer.write(b'DPW1'+struct.pack('<II',190,len(p))+p); "
                  "sys.stdout.buffer.flush(); time.sleep(1)")
        worker = browser_bridge.SimulationWorker([sys.executable, '-u', '-c', script], live=True)
        try:
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.output.error is not None, timeout=5))
                self.assertIn('Invalid local resource receipt', worker.output.error)
                self.assertIsNone(worker.resources)
            self.assertEqual(worker.output.size, 0)
        finally:
            worker.close()


class StartupCleanupTests(unittest.TestCase):
    def test_child_is_reaped_if_pipe_setup_fails(self):
        real_popen = browser_bridge.subprocess.Popen
        children = []

        def capture(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            children.append(child)
            return child

        with mock.patch.object(browser_bridge.subprocess, 'Popen', side_effect=capture), \
             mock.patch.object(browser_bridge.os, 'set_blocking', side_effect=OSError('test setup failure')):
            with self.assertRaisesRegex(OSError, 'test setup failure'):
                browser_bridge.SimulationWorker([sys.executable, '-u', '-c', PEER])
        self.assertEqual(len(children), 1)
        self.assertIsNotNone(children[0].poll())
        self.assertTrue(children[0].stdin.closed)
        self.assertTrue(children[0].stdout.closed)

    def test_setup_failure_stops_child_process_group(self):
        script = r'''
import os,signal,struct,sys,time
pid=os.fork()
if pid==0:
    signal.signal(signal.SIGTERM,signal.SIG_IGN)
    while True: time.sleep(1)
sys.stdout.buffer.write(struct.pack('<I',pid)); sys.stdout.buffer.flush()
while True: time.sleep(1)
'''
        real_popen = browser_bridge.subprocess.Popen
        children, descendants = [], []

        def capture(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            children.append(child)
            return child

        def fail_after_fork(*_args):
            ready = browser_bridge.select.select([children[0].stdout], [], [], 5)[0]
            if not ready:
                raise OSError('fork fixture did not start')
            descendants.append(struct.unpack('<I', os.read(children[0].stdout.fileno(), 4))[0])
            raise OSError('injected setup failure')

        try:
            with mock.patch.object(browser_bridge.subprocess, 'Popen', side_effect=capture), \
                 mock.patch.object(browser_bridge.os, 'set_blocking', side_effect=fail_after_fork):
                with self.assertRaisesRegex(OSError, 'injected setup failure'):
                    browser_bridge.SimulationWorker([sys.executable, '-u', '-c', script])
            self.assertIsNotNone(children[0].returncode)
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                try:
                    state = Path(f'/proc/{descendants[0]}/stat').read_text().split(') ', 1)[1][0]
                except (FileNotFoundError, ProcessLookupError):
                    break
                if state == 'Z':
                    break
                time.sleep(.02)
            else:
                self.fail('descendant survived failed setup')
        finally:
            for pid in descendants:
                try:
                    os.kill(pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass

    def test_setup_term_failure_still_kills_and_reaps_child(self):
        real_popen = browser_bridge.subprocess.Popen
        real_killpg = os.killpg
        children, signals = [], []

        def capture(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            children.append(child)
            return child

        def reject_term(pid, sig):
            signals.append(sig)
            if sig == signal.SIGTERM:
                raise OSError('injected TERM failure')
            return real_killpg(pid, sig)

        with mock.patch.object(browser_bridge.subprocess, 'Popen', side_effect=capture), \
             mock.patch.object(browser_bridge.os, 'set_blocking', side_effect=OSError('setup failed')), \
             mock.patch.object(browser_bridge.os, 'killpg', side_effect=reject_term):
            with self.assertRaisesRegex(OSError, 'setup failed'):
                browser_bridge.SimulationWorker([sys.executable, '-u', '-c', PEER])
        self.assertIn(signal.SIGTERM, signals)
        self.assertIn(signal.SIGKILL, signals)
        self.assertIsNotNone(children[0].returncode)

    def test_setup_group_kill_failure_reports_incomplete_cleanup(self):
        real_popen = browser_bridge.subprocess.Popen
        real_killpg = os.killpg
        children = []

        def capture(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            children.append(child)
            return child

        def reject_kill(pid, sig):
            if sig == signal.SIGKILL:
                raise OSError('injected KILL failure')
            return real_killpg(pid, sig)

        try:
            with mock.patch.object(browser_bridge.subprocess, 'Popen', side_effect=capture), \
                 mock.patch.object(browser_bridge.os, 'set_blocking', side_effect=OSError('setup failed')), \
                 mock.patch.object(browser_bridge.os, 'killpg', side_effect=reject_kill):
                with self.assertRaisesRegex(RuntimeError, 'cleanup was incomplete') as failure:
                    browser_bridge.SimulationWorker([sys.executable, '-u', '-c', PEER])
            self.assertIsInstance(failure.exception.__cause__, OSError)
            self.assertIsNone(children[0].returncode)  # Group identity is retained.
        finally:
            if children:
                try:
                    real_killpg(children[0].pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                children[0].wait(timeout=2)

    def test_setup_does_not_signal_reaped_group_after_wait_race(self):
        real_popen, real_killpg = browser_bridge.subprocess.Popen, os.killpg
        children, signals = [], []

        def capture(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            children.append(child)
            return child

        def record_signal(pid, sig):
            signals.append(sig)
            return real_killpg(pid, sig)

        try:
            with mock.patch.object(browser_bridge.subprocess, 'Popen', side_effect=capture), \
                 mock.patch.object(browser_bridge.os, 'set_blocking', side_effect=OSError('setup failed')), \
                 mock.patch.object(browser_bridge.os, 'waitid', side_effect=[None, ChildProcessError('reaped')]), \
                 mock.patch.object(browser_bridge.os, 'killpg', side_effect=record_signal):
                with self.assertRaisesRegex(RuntimeError, 'cleanup was incomplete'):
                    browser_bridge.SimulationWorker([sys.executable, '-u', '-c', PEER])
            self.assertEqual(signals, [signal.SIGTERM])
        finally:
            if children:
                try:
                    real_killpg(children[0].pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                children[0].wait(timeout=2)


class FileImportTests(unittest.TestCase):
    def test_real_worker_import_is_scoped_and_authorized(self):
        root = Path(__file__).resolve().parent.parent
        binary = root / 'build/dev-cli-web/datapump-worker'
        if not binary.is_file():
            self.skipTest('native worker not built')
        with tempfile.TemporaryDirectory() as directory:
            home = Path(directory)
            (home / 'keyfile').write_text('invalid fixture keyfile')
            (home / 'link').symlink_to('/etc/passwd')
            worker = browser_bridge.SimulationWorker(
                [sys.executable, str(root / '_zLab/worker_relay.py'),
                 '--file-channel', str(binary)], files=home)
            try:
                with worker.state:
                    self.assertTrue(worker.state.wait_for(
                        lambda: worker.snapshot is not None or worker.output.error, timeout=10))
                    self.assertIsNone(worker.output.error)
                    snapshot = worker.snapshot
                    menu = next(c for c in snapshot['controls'] if c.get('label') == 'Keyfile')
                    target = next(o['id'] for o in menu['options'] if o['label'] == 'Open keyfile')
                    seq, generation = int(snapshot['ack']) + 1, int(snapshot['generation'])
                payload = (struct.pack('<IQQQIII', 1, generation, seq, int(target), 4, 0, 0)
                           + browser_bridge._preview.string('') + browser_bridge._preview.string(''))
                worker.input(frame(2, payload))
                with worker.state:
                    self.assertTrue(worker.state.wait_for(
                        lambda: (worker.snapshot or {}).get('service') is not None or worker.output.error,
                        timeout=10))
                    self.assertIsNone(worker.output.error)
                    snapshot = worker.snapshot
                    service = snapshot['service']
                    event = {'version': 1, 'kind': 13, 'generation': snapshot['generation'],
                             'sequence': str(int(snapshot['ack']) + 1), 'target': service['id']}
                request = {'kind': 'open_file', 'event': event, 'path': '../outside'}
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.service(request)
                request['path'] = 'link'
                with self.assertRaises(OSError):
                    worker.service(request)
                # Ordinary browser messages cannot submit trusted file frames,
                # even when the local user enabled a file root.
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.input(frame(10, b''))
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.input(b'DPL1' + struct.pack('<II', 1, 12) + frame(12))
                request['path'] = 'keyfile'
                worker.service(request)
                with worker.state:
                    self.assertGreaterEqual(int(worker.snapshot['ack']), int(event['sequence']))
                with self.assertRaises(browser_bridge._preview.PreviewError):
                    worker.service(request)
            finally:
                worker.close()



class BrowserBridgeTests(unittest.TestCase):
    def setUp(self):
        self.bridge = browser_bridge.start([sys.executable, '-u', '-c', PEER])
        parts = urlsplit(self.bridge.url)
        self.base = f'{parts.scheme}://{parts.netloc}'
        self.token = parts.fragment
        self.session = ''

    def tearDown(self):
        self.bridge.close()
        self.bridge.close()

    def request(self, path, body=None, token=None, **headers):
        headers['X-Preview-Token'] = self.token if token is None else token
        headers['X-Preview-Session'] = self.session
        req = urllib.request.Request(self.base + path, data=body, headers=headers)
        with urllib.request.urlopen(req, timeout=5) as response:
            return response.read()

    def start_peer(self):
        self.session = json.loads(self.request('/start', b''))['session']
        return self.bridge.server.worker

    def test_loopback_ui_and_simulation_peer(self):
        self.assertTrue(self.base.startswith('http://127.0.0.1:'))
        self.assertIn(b'RunPod Robust simulation', self.request('/'))
        script = self.request('/tools/preview.mjs')
        self.assertIn(b'audio.disabled=true;audio.title=', script)
        worker = self.start_peer()
        snapshot = self.request('/output')
        self.assertEqual(snapshot[:4], b'DPW1')
        self.assertEqual(struct.unpack_from('<I', snapshot, 4)[0], 101)
        self.request('/input', frame(1, struct.pack('<II', 800, 600)))
        self.assertEqual(struct.unpack_from('<I', self.request('/output'), 4)[0], 103)
        self.bridge.close()
        self.assertIsNotNone(worker.process.poll())

    def test_lab_shell_and_shared_renderer_are_forced_dark(self):
        # The browser host supplies this stylesheet to its sandboxed renderer.
        # Only the lab response is changed; the maintained asset remains intact.
        original = (Path(__file__).resolve().parent.parent / 'web/style.css').read_bytes()
        shell = self.request('/')
        stylesheet = self.request('/web/style.css')
        self.assertIn(b':root { color-scheme: dark; }', shell)
        self.assertEqual(stylesheet, original + b'\n:root { color-scheme: dark; }\n')
        self.assertIn(b'color-scheme:light dark', original)
        with self.assertRaises(urllib.error.HTTPError) as forbidden:
            self.request('/web/style.css', Origin='https://evil.example')
        self.assertEqual(forbidden.exception.code, 403)

    def test_close_retries_failed_worker_cleanup(self):
        worker = self.start_peer()
        self.request('/output')
        original_wait = worker.process.wait
        calls = 0

        def fail_once(*args, **kwargs):
            nonlocal calls
            calls += 1
            if calls == 1:
                raise OSError('injected wait failure')
            return original_wait(*args, **kwargs)

        with mock.patch.object(worker.process, 'wait', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'injected wait failure'):
                self.bridge.close()
        # Even after an error, cleanup attempted to reap the actual child; the
        # owner remains registered until a successful retry finishes cleanup.
        self.assertTrue(worker._wait_without_reaping(2))
        self.assertIs(self.bridge.server.worker, worker)
        self.assertFalse(self.bridge._closed)
        self.bridge.close()
        self.assertIsNone(self.bridge.server.worker)
        self.assertTrue(worker._close_complete)
        self.assertTrue(self.bridge._closed)

    def test_close_stops_descendant_when_leader_exited(self):
        # The descendant inherits stdout and ignores TERM. The exited leader
        # must remain unreaped until its process group has been stopped.
        script = r'''
import json,os,signal,struct,sys,time
pid=os.fork()
if pid==0:
    signal.signal(signal.SIGTERM,signal.SIG_IGN)
    while True: time.sleep(1)
p=json.dumps({'generation':'1','ack':'0','descendant':pid}).encode()
sys.stdout.buffer.write(b'DPW1'+struct.pack('<II',101,len(p))+p)
sys.stdout.buffer.flush()
os._exit(0)
'''
        self.bridge.close()
        self.bridge = browser_bridge.start([sys.executable, '-u', '-c', script])
        parts = urlsplit(self.bridge.url)
        self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
        worker = self.start_peer()
        data = self.request('/output')
        descendant = json.loads(data[12:])['descendant']
        try:
            self.assertTrue(worker._wait_without_reaping(3))
            self.bridge.close()
            self.assertIsNotNone(worker.process.returncode)
            self.assertFalse(worker.reader.is_alive())
            # An orphan can briefly remain a zombie until the host reaps it.
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                try:
                    state = Path(f'/proc/{descendant}/stat').read_text().split(') ', 1)[1][0]
                except (FileNotFoundError, ProcessLookupError):
                    break
                if state == 'Z':
                    break
                time.sleep(.02)
            else:
                self.fail('descendant remained alive after bridge close')
        finally:
            try:
                os.kill(descendant, signal.SIGKILL)
            except ProcessLookupError:
                pass

    def test_failed_group_signal_preserves_leader_until_retry(self):
        worker = self.start_peer()
        self.request('/output')
        killpg = os.killpg
        failed = False

        def fail_once(pid, sig):
            nonlocal failed
            if sig == signal.SIGKILL and not failed:
                failed = True
                raise OSError('injected group signal failure')
            return killpg(pid, sig)

        with mock.patch.object(browser_bridge.os, 'killpg', side_effect=fail_once):
            with self.assertRaisesRegex(OSError, 'injected group signal failure'):
                self.bridge.close()
        self.assertTrue(worker._wait_without_reaping(2))
        self.assertIsNone(worker.process.returncode)
        self.assertFalse(worker._group_stopped)
        self.assertIs(self.bridge.server.worker, worker)
        self.bridge.close()
        self.assertTrue(worker._group_stopped)
        self.assertIsNotNone(worker.process.returncode)

    def test_close_does_not_signal_group_after_external_reap_race(self):
        worker = self.start_peer()
        self.request('/output')
        real_killpg = os.killpg
        signals = []

        def record_signal(pid, sig):
            signals.append(sig)
            return real_killpg(pid, sig)

        with mock.patch.object(worker, '_wait_without_reaping',
                               side_effect=[True, ChildProcessError('reaped')]), \
             mock.patch.object(browser_bridge.os, 'killpg', side_effect=record_signal):
            with self.assertRaises(ChildProcessError):
                self.bridge.close()
        self.assertEqual(signals, [signal.SIGTERM])
        self.assertIs(self.bridge.server.worker, worker)
        # The injected race did not actually reap the child; retry can safely
        # finish its group cleanup after the real waitid check.
        self.bridge.close()
        self.assertIsNotNone(worker.process.returncode)

    def test_close_retries_shutdown_error_after_stopping_worker(self):
        worker = self.start_peer()
        self.request('/output')
        shutdown = self.bridge.server.shutdown

        def shutdown_then_fail():
            shutdown()
            raise OSError('injected shutdown failure')

        with mock.patch.object(self.bridge.server, 'shutdown', side_effect=shutdown_then_fail):
            with self.assertRaisesRegex(OSError, 'injected shutdown failure'):
                self.bridge.close()
        self.assertIsNotNone(worker.process.poll())
        self.assertFalse(self.bridge._closed)
        self.bridge.close()
        self.assertTrue(self.bridge._closed)

    def test_concurrent_close_calls_are_serialized(self):
        worker = self.start_peer()
        self.request('/output')
        barrier = threading.Barrier(3)
        failures = []

        def close_bridge():
            barrier.wait()
            try:
                self.bridge.close()
            except Exception as error:
                failures.append(error)

        callers = [threading.Thread(target=close_bridge) for _ in range(2)]
        for caller in callers:
            caller.start()
        barrier.wait()
        for caller in callers:
            caller.join(timeout=10)
        self.assertFalse(any(caller.is_alive() for caller in callers))
        self.assertEqual(failures, [])
        self.assertIsNotNone(worker.process.poll())
        self.assertTrue(self.bridge._closed)

    def test_maintenance_retries_close_error(self):
        worker = self.start_peer()
        self.request('/output')
        original_close = worker.close
        calls = []

        def fail_once():
            calls.append(1)
            if len(calls) == 1:
                raise OSError('injected cleanup failure')
            return original_close()

        with mock.patch.object(worker, 'close', side_effect=fail_once):
            worker.output.close('injected worker failure')
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                with self.bridge.server.owner_lock:
                    if self.bridge.server.worker is None:
                        break
                time.sleep(.05)
            self.assertGreaterEqual(len(calls), 2)
            self.assertIsNone(self.bridge.server.worker)
            self.assertIsNotNone(worker.process.poll())

    def test_authorization_and_disabled_audio(self):
        with self.assertRaises(urllib.error.HTTPError) as unauthorized:
            self.request('/start', b'', token='wrong')
        self.assertEqual(unauthorized.exception.code, 403)
        self.start_peer()
        with self.assertRaises(urllib.error.HTTPError) as audio:
            self.request('/input', frame(3))
        self.assertEqual(audio.exception.code, 400)
        with self.assertRaises(urllib.error.HTTPError) as mixed:
            self.request('/input', frame(1, b'12345678') + frame(4))
        self.assertEqual(mixed.exception.code, 400)
        with self.assertRaises(urllib.error.HTTPError) as service:
            self.request('/service', b'{}')
        self.assertEqual(service.exception.code, 400)


    def test_existing_native_worker_snapshot(self):
        binary = Path(__file__).resolve().parent.parent / 'build/dev-cli-web/datapump-worker'
        if not binary.is_file():
            self.skipTest('native worker not built')
        self.bridge.close()
        self.bridge = browser_bridge.start([str(binary), '--simulation'])
        parts = urlsplit(self.bridge.url)
        self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
        self.start_peer()
        self.request('/input', frame(1, struct.pack('<II', 800, 600)))
        data = self.request('/output')
        self.assertEqual(data[:4], b'DPW1')
        self.assertEqual(struct.unpack_from('<I', data, 4)[0], 101)
        snapshot = json.loads(data[12:])
        self.assertIn('generation', snapshot)

    def test_browser_file_choice_import_and_stale_cancellation(self):
        binary = Path(__file__).resolve().parent.parent / 'build/dev-cli-web/datapump-worker'
        if not binary.is_file():
            self.skipTest('native worker not built')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'keyfile').write_text('invalid fixture keyfile')
            self.bridge.close()
            relay = Path(__file__).resolve().parent / 'worker_relay.py'
            self.bridge = browser_bridge.start(
                [sys.executable, str(relay), '--file-channel', str(binary)], files=path)
            parts = urlsplit(self.bridge.url)
            self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
            reply = json.loads(self.request('/start', b''))
            self.session = reply['session']
            self.assertEqual(reply['files'], str(path))
            script = self.request('/tools/preview.mjs')
            self.assertIn(b"if(!files)throw", script)
            self.assertIn(b"path=await choose(service,signal)", script)
            snapshot = None
            def wait(predicate):
                nonlocal snapshot
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    for kind, packet in browser_bridge._preview.frames(self.request('/output')):
                        if kind == 101:
                            snapshot = json.loads(packet[12:])
                    if snapshot and predicate(snapshot):
                        return snapshot
                self.fail('native GUI snapshot deadline')
            def event(kind, target, flags=0):
                seq = int(snapshot['ack']) + 1
                payload = (struct.pack('<IQQQIII', 1, int(snapshot['generation']), seq,
                                       int(target), kind, flags, 0)
                           + browser_bridge._preview.string('') + browser_bridge._preview.string(''))
                self.request('/input', frame(2, payload))
                wait(lambda s: int(s['ack']) >= seq)
            wait(lambda s: any(c.get('label') == 'Keyfile' for c in s['controls']))
            def open_choice():
                menu = next(c for c in snapshot['controls'] if c.get('label') == 'Keyfile')
                target = next(o['id'] for o in menu['options'] if o['label'] == 'Open keyfile')
                event(4, target)
                wait(lambda s: bool(s.get('service')))
                service = snapshot['service']
                return {'kind': 'open_file', 'path': 'keyfile', 'event': {
                    'version': 1, 'kind': 13, 'generation': snapshot['generation'],
                    'sequence': str(int(snapshot['ack']) + 1), 'target': service['id']}}
            # Cancel one chooser; its old capability cannot authorize an import.
            cancelled = open_choice()
            event(13, cancelled['event']['target'], flags=2)
            with self.assertRaises(urllib.error.HTTPError) as denied:
                self.request('/service', json.dumps(cancelled).encode())
            self.assertEqual(denied.exception.code, 400)
            request = open_choice()
            encoded = json.dumps(request).encode()
            self.request('/service', encoded)
            with self.assertRaises(urllib.error.HTTPError) as stale:
                self.request('/service', encoded)
            self.assertEqual(stale.exception.code, 400)
            wait(lambda s: int(s['ack']) >= int(request['event']['sequence']))

    def test_live_receipt_endpoint_is_authenticated(self):
        self.bridge.close()
        self.bridge = browser_bridge.start([sys.executable, '-u', '-c', LIVE_PEER], live=True)
        parts = urlsplit(self.bridge.url)
        self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
        self.assertIn(b'local companion', self.request('/'))
        script = self.request('/tools/preview.mjs')
        self.assertIn(b'/lab-resources', script)
        self.assertIn(b'if(!stopped)report(error)', script)
        self.start_peer()
        self.request('/output')
        result = json.loads(self.request('/lab-resources'))
        self.assertEqual(result['capture_cap_bytes'], 1000)
        self.assertFalse(result['process_baseline_included'])
        with self.assertRaises(urllib.error.HTTPError) as forbidden:
            self.request('/lab-resources', token='wrong')
        self.assertEqual(forbidden.exception.code, 403)
        self.request('/input', frame(1, struct.pack('<II', 800, 600)))
        self.assertEqual(struct.unpack_from('<I', self.request('/output'), 4)[0], 103)

    def test_live_fatal_status_survives_maintenance_until_seen(self):
        # The child ignores EOF; the failed session must reap it even when a
        # browser keeps sending heartbeat requests and never polls output.
        script = ("import struct,sys,time; f=lambda t,p:b'DPW1'+struct.pack('<II',t,len(p))+p; "
                  "sys.stdout.buffer.write(f(101,b'{\"generation\":\"1\",\"ack\":\"0\"}')); "
                  "sys.stdout.buffer.flush(); time.sleep(.1); "
                  "sys.stdout.buffer.write(f(191,b'{\"code\":\"clock\"}')); sys.stdout.buffer.flush(); time.sleep(30)")
        self.bridge.close()
        with mock.patch.object(browser_bridge._preview, 'LEASE', 1.5):
            self.bridge = browser_bridge.start([sys.executable, '-u', '-c', script], live=True)
            parts = urlsplit(self.bridge.url)
            self.base, self.token = f'{parts.scheme}://{parts.netloc}', parts.fragment
            worker = self.start_peer()
            self.request('/output')
            with worker.state:
                self.assertTrue(worker.state.wait_for(lambda: worker.output.error is not None, timeout=5))
            time.sleep(.7)  # At least one maintenance sweep has occurred.
            self.assertIsNotNone(worker.process.poll())
            self.request('/heartbeat', b'')
            with self.assertRaises(urllib.error.HTTPError) as error:
                self.request('/output')
            self.assertEqual(error.exception.code, 410)
            self.assertIn(b'clock qualification failed', error.exception.read())
            for _ in range(4):
                time.sleep(.3)
                try:
                    self.request('/heartbeat', b'')
                except urllib.error.HTTPError:
                    break
            time.sleep(.5)
            self.assertIsNone(self.bridge.server.worker)

    def test_reject_untrusted_host(self):
        with self.assertRaises(urllib.error.HTTPError) as forbidden:
            self.request('/', Host='evil.example')
        self.assertEqual(forbidden.exception.code, 403)

    def test_arguments_rejected(self):
        with self.assertRaises(ValueError):
            browser_bridge.start('ssh example')
        with self.assertRaises(ValueError):
            browser_bridge.start([])


class ManagedBrowserTests(unittest.TestCase):
    client = 'a' * 64

    def start_bridge(self, script=READY_PEER, **kwargs):
        bridge = browser_bridge.start([sys.executable, '-u', '-c', script],
                                      managed=True, **kwargs)
        self.addCleanup(bridge.close)
        self.bridge = bridge
        self.url = urlsplit(bridge.url)

    def request(self, route, data=b'', session='', client=None, origin=None):
        request = urllib.request.Request(
            f'{self.url.scheme}://{self.url.netloc}{route}', data=data,
            headers={'X-Preview-Token': self.url.fragment, 'X-Preview-Session': session,
                     'X-Preview-Client': client or self.client,
                     'Origin': origin or f'{self.url.scheme}://{self.url.netloc}'})
        with urllib.request.urlopen(request, timeout=5) as reply:
            return reply.read()

    def attach(self):
        return json.loads(self.request('/start'))['session']

    def wait_status(self, expected):
        deadline = time.monotonic() + 5
        while self.bridge.status()[0] != expected and time.monotonic() < deadline:
            time.sleep(.02)
        self.assertEqual(self.bridge.status()[0], expected)

    def test_early_exit_and_startup_timeout(self):
        for script, message in (('raise SystemExit(255)', 'Native worker exited'),
                                (PEER, 'Timed out waiting')):
            with self.subTest(message=message), self.assertRaisesRegex(browser_bridge._preview.PreviewError, message):
                self.start_bridge(script, ready_timeout=.5)

    def test_same_tab_reload_restores_defaults_and_rejects_stale_lease(self):
        script = READY_PEER.replace(
            "sys.stdout.buffer.write(frame(103,struct.pack('<I',0)))",
            "sys.stdout.buffer.write(frame(101,b'{\"version\":1,\"generation\":\"1\","
            "\"ack\":\"1\",\"controls\":[{\"kind\":\"text\",\"text\":\"changed value\"}]}'))",
        )
        self.start_bridge(script)
        worker = self.bridge.server.worker
        self.assertEqual(self.bridge.status(), ('waiting', ''))
        original = self.attach()
        first_snapshot = self.request('/output', None, original)
        self.assertIn(b'Robust simulation ready', first_snapshot)
        self.request('/input', frame(1, b'12345678'), original)
        self.assertIn(b'changed value', self.request('/output', None, original))
        with self.assertRaises(urllib.error.HTTPError) as sibling:
            self.request('/start', client='b' * 64)
        self.assertEqual(sibling.exception.code, 409)
        self.request('/detach', session=original)
        refreshed = self.attach()
        self.assertNotEqual(original, refreshed)
        self.assertIsNot(worker, self.bridge.server.worker)
        self.assertIsNotNone(worker.process.returncode)
        self.assertEqual(first_snapshot, self.request('/output', None, refreshed))
        # A late pagehide from the old page must not detach its replacement.
        self.request('/detach', session=original)
        self.request('/browser-error', session=original)
        self.request('/heartbeat', session=refreshed)
        with self.assertRaises(urllib.error.HTTPError) as stale:
            self.request('/input', frame(1, b'12345678'), original)
        self.assertEqual(stale.exception.code, 410)
        self.assertEqual(self.bridge.status(), ('active', ''))
        self.request('/stop', session=refreshed)
        self.assertEqual(self.bridge.status()[0], 'ended')

    def test_reload_starts_a_fresh_worker_without_a_detach_signal(self):
        self.start_bridge()
        original = self.attach()
        worker = self.bridge.server.worker
        refreshed = self.attach()
        self.assertIsNot(worker, self.bridge.server.worker)
        self.assertNotEqual(original, refreshed)
        self.assertIn(b'Robust simulation ready', self.request('/output', None, refreshed))

    def test_worker_reset_preserves_the_pod_session_past_the_close_grace(self):
        self.start_bridge()
        original = self.attach()
        worker = self.bridge.server.worker
        close = worker.close
        closing = threading.Event()
        release = threading.Event()
        responses = []

        def wait_to_close():
            closing.set()
            if not release.wait(3):
                raise RuntimeError('Reset test did not release worker shutdown')
            close()

        def reload():
            try:
                responses.append(self.attach())
            except Exception as error:
                responses.append(error)

        with mock.patch.object(worker, 'close', side_effect=wait_to_close), \
             mock.patch.object(browser_bridge, '_RECONNECT_GRACE', .1):
            self.request('/detach', session=original)
            replacement = threading.Thread(target=reload, daemon=True)
            replacement.start()
            try:
                self.assertTrue(closing.wait(2))
                # Maintenance has a .25 s interval, so let it see the expired
                # close grace while the old worker is still shutting down.
                time.sleep(.4)
                with self.assertRaises(urllib.error.HTTPError) as sibling:
                    self.request('/start', client='b' * 64)
                self.assertEqual(sibling.exception.code, 409)
                self.assertEqual(self.bridge.status()[0], 'waiting')
            finally:
                release.set()
                replacement.join(timeout=5)
        self.assertFalse(replacement.is_alive())
        self.assertEqual(len(responses), 1)
        self.assertIsInstance(responses[0], str)
        self.assertNotEqual(responses[0], original)
        self.assertEqual(self.bridge.status()[0], 'active')

    def test_close_grace_and_network_loss(self):
        with mock.patch.object(browser_bridge, '_RECONNECT_GRACE', .1):
            self.start_bridge()
            session = self.attach()
            with self.assertRaises(urllib.error.HTTPError) as forbidden:
                self.request('/detach', session=session, origin='null')
            self.assertEqual(forbidden.exception.code, 403)
            self.request('/detach', session=session)
            self.wait_status('ended')
        with mock.patch.object(browser_bridge, '_BROWSER_LEASE', .1):
            self.start_bridge()
            self.attach()
            self.wait_status('ended')

    def test_worker_failure_is_distinct_from_browser_close(self):
        self.start_bridge(READY_PEER.replace('while True:', 'raise SystemExit(255)\nwhile True:')
                          .replace('import struct, sys', 'import struct, sys, time')
                          .replace('raise SystemExit(255)', 'time.sleep(.4); raise SystemExit(255)'))
        self.attach()
        self.wait_status('failed')
        self.assertIn('worker disconnected', self.bridge.status()[1])


if __name__ == '__main__':
    unittest.main()

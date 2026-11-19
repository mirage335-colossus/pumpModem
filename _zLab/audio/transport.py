#!/usr/bin/env python3
"""Route a lab live worker, local device companion and trusted GUI host.

The GUI input is DPL1-multiplexed (channel 0 UI, channel 1 authorized file
service); output is DPW1. The remote relay must run with --live --file-channel.
The local child receives and emits only the lab audio protocol (DPW1 types
200--208 and 220--229). Neither child receives a socket from this process.

The two clock limits are explicit assumptions, in seconds. The local UTC bound
must be established independently of this program. A four-timestamp exchange
only checks the relative local/pod clock envelope; it cannot establish UTC.
EOF, cancellation or a transport failure never represents receiver completion.
"""
from __future__ import annotations

import argparse
from collections import deque
import json
import math
import os
import secrets
import select
import selectors
import signal
import struct
import subprocess
import sys
import threading
import time

HEADER = 12
MAX_PAYLOAD = 8 * 1024 * 1024
MAX_AUDIO = 64 * 1024
QUEUE_LIMIT = 2 * (MAX_PAYLOAD + 2 * HEADER)
QUEUE_ITEMS = 2048
PROBE_TIMEOUT = 10.0
PROBE_PERIOD = 20.0
STALL_TIMEOUT = 120.0
QUANTIZATION = 2e-6
MAX_CLOCK_STEP = .001


class TransportError(Exception):
    pass


def frame(kind, payload=b''):
    return struct.pack('<4sII', b'DPW1', kind, len(payload)) + payload


def envelope(channel, block):
    return struct.pack('<4sII', b'DPL1', channel, len(block)) + block


def valid_bound(value, name):
    if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or not 0 < value <= 3600:
        raise ValueError(name + ' must be finite, positive and at most 3600 seconds')
    return float(value)


def frame_info(block, magic=b'DPW1'):
    if len(block) < HEADER:
        raise TransportError('truncated frame')
    actual, kind, size = struct.unpack_from('<4sII', block)
    maximum = MAX_PAYLOAD + HEADER if magic == b'DPL1' else MAX_PAYLOAD
    if actual != magic or size > maximum or len(block) != HEADER + size:
        raise TransportError('invalid frame')
    return kind, block[HEADER:]


class Reader:
    """Read at most one complete, bounded frame per source."""
    def __init__(self, fd, magic, limit=MAX_PAYLOAD):
        self.fd, self.magic, self.limit = fd, magic, limit
        self.data = bytearray()
        self.total = None

    def read(self):
        expected = self.total if self.total is not None else HEADER
        try:
            data = os.read(self.fd, min(65536, expected - len(self.data)))
        except BlockingIOError:
            return None
        if not data:
            if self.data:
                raise TransportError('truncated input at EOF')
            return False
        self.data.extend(data)
        if self.total is None and len(self.data) == HEADER:
            magic, _, size = struct.unpack('<4sII', self.data)
            if magic != self.magic or size > self.limit:
                raise TransportError('invalid input header')
            self.total = HEADER + size
        if self.total is not None and len(self.data) == self.total:
            result = bytes(self.data)
            self.data.clear()
            self.total = None
            return result
        return None


class Output:
    def __init__(self, fd, on_complete=None):
        self.fd = fd
        self.items = deque()
        self.labels = deque()
        self.on_complete = on_complete
        self.size = 0
        self.last_progress = time.monotonic()
        self.partial = False

    def put(self, data, label=None):
        if self.size + len(data) > QUEUE_LIMIT or len(self.items) >= QUEUE_ITEMS:
            raise TransportError('transport queue exceeded its bound')
        if not self.items:
            self.last_progress = time.monotonic()
        self.items.append(memoryview(data))
        self.labels.append(label)
        self.size += len(data)

    def write(self):
        if not self.items:
            return
        try:
            count = os.write(self.fd, self.items[0][:65536])
        except BlockingIOError:
            return
        if count <= 0:
            raise TransportError('transport output closed')
        self.size -= count
        self.last_progress = time.monotonic()
        self.items[0] = self.items[0][count:]
        self.partial = bool(self.items[0])
        if not self.items[0]:
            self.items.popleft()
            label = self.labels.popleft()
            if self.on_complete and label is not None:
                self.on_complete(label)


class ClockGate:
    """Measure a conservative interval for pod clock minus local clock."""
    def __init__(self, maximum, utc_uncertainty, remote_drift_ppm):
        self.maximum = valid_bound(maximum, 'maximum clock offset')
        self.utc_uncertainty = valid_bound(utc_uncertainty, 'local UTC uncertainty')
        self.remote_drift = valid_bound(remote_drift_ppm, 'remote clock drift in ppm') * 1e-6
        self.pending = None
        self.last_ok = None
        self.bound = None
        self.anchor_wall = self.anchor_mono = self.anchor_error = None

    @staticmethod
    def sample():
        before = time.monotonic()
        wall = time.time()
        after = time.monotonic()
        return wall, (before + after) / 2, (after - before) / 2

    def request(self, wall=None, mono=None):
        if self.pending is not None:
            return None
        if wall is None and mono is None:
            wall, mono, sample_error = self.sample()
        elif wall is not None and mono is not None:
            sample_error = 0
        else:
            raise ValueError('wall and monotonic samples must be supplied together')
        self.check_continuity(wall, mono, sample_error)
        nonce = secrets.randbits(63) | (1 << 63)
        self.pending = nonce, wall, mono, sample_error
        return frame(15, struct.pack('<Qd', nonce, wall))

    def response(self, payload, wall=None, mono=None):
        if len(payload) != 32:
            raise TransportError('invalid clock response')
        nonce, echo, remote_receive, remote_send = struct.unpack('<Qddd', payload)
        if self.pending is None or nonce != self.pending[0]:
            return False
        _, sent_wall, sent_mono, sent_error = self.pending
        if wall is None and mono is None:
            wall, mono, receive_error = self.sample()
        elif wall is not None and mono is not None:
            receive_error = 0
        else:
            raise ValueError('wall and monotonic samples must be supplied together')
        self.check_continuity(wall, mono, receive_error)
        elapsed = mono - sent_mono
        sample_error = sent_error + receive_error + QUANTIZATION
        if (not all(math.isfinite(x) for x in (echo, remote_receive, remote_send, wall, mono))
                or echo != sent_wall or elapsed < 0 or elapsed > PROBE_TIMEOUT
                or abs((wall - sent_wall) - elapsed) > MAX_CLOCK_STEP + sample_error
                or remote_send < remote_receive):
            raise TransportError('clock exchange failed or local clock stepped')
        # Causality gives remote-minus-local offset in this interval; its
        # largest absolute endpoint includes asymmetry and both network legs.
        # The pod clock can move relative to the local clock during the
        # exchange itself; charge its declared drift over the whole RTT.
        error = MAX_CLOCK_STEP + sample_error + elapsed * self.remote_drift
        low = remote_send - wall - error
        high = remote_receive - sent_wall + error
        if low > high:
            raise TransportError('inconsistent clock exchange')
        bound = max(abs(low), abs(high))
        if bound > self.maximum:
            raise TransportError('pod/local clock difference exceeds the declared bound')
        self.pending = None
        self.bound = bound
        self.last_ok = mono
        self.anchor_wall, self.anchor_mono, self.anchor_error = wall, mono, receive_error
        return True

    def check_pending(self, now):
        if self.pending is not None and now - self.pending[2] > PROBE_TIMEOUT:
            raise TransportError('clock exchange timed out')

    def check_continuity(self, wall=None, mono=None, _sample_error=None):
        if self.last_ok is None:
            return
        if wall is None and mono is None:
            wall, mono, sample_error = self.sample()
        elif wall is not None and mono is not None:
            sample_error = 0 if _sample_error is None else _sample_error
        else:
            raise ValueError('wall and monotonic samples must be supplied together')
        elapsed = mono - self.anchor_mono
        if (not math.isfinite(wall) or not math.isfinite(mono) or elapsed < 0
                or abs((wall - self.anchor_wall) - elapsed)
                > MAX_CLOCK_STEP + self.anchor_error + sample_error + QUANTIZATION):
            raise TransportError('local clock stepped between clock exchanges')
        # The configured offset is a bound throughout the interval, not just
        # at sample instants. The remote drift assumption and local clock
        # continuity margin consume it until the next qualified exchange.
        if self.bound + elapsed * self.remote_drift + MAX_CLOCK_STEP + sample_error > self.maximum:
            raise TransportError('clock drift exhausts the declared offset bound')

    def playback_open(self, block):
        # u64 stream, u32 rate/channel/exclusive, f64 gain, max UTC error ...
        kind, payload = frame_info(block)
        if kind != 202 or len(payload) < 36:
            raise TransportError('invalid playback open')
        remaining = struct.unpack_from('<d', payload, 28)[0] - self.maximum - self.utc_uncertainty
        if not math.isfinite(remaining) or remaining <= 0:
            raise TransportError('clock assumptions exceed playback UTC error allowance')
        data = bytearray(block)
        struct.pack_into('<d', data, HEADER + 28, remaining)
        return bytes(data)

    def capture_pcm(self, block):
        # u64 stream/first-frame/UTC-seconds, f64 fraction/uncertainty ...
        kind, payload = frame_info(block)
        if kind != 221 or len(payload) < 40:
            raise TransportError('invalid capture timestamp')
        uncertainty = struct.unpack_from('<d', payload, 32)[0]
        if not math.isfinite(uncertainty) or uncertainty < 0:
            raise TransportError('invalid capture uncertainty')
        charged = math.nextafter(uncertainty + self.maximum + self.utc_uncertainty, math.inf)
        if not math.isfinite(charged):
            raise TransportError('capture uncertainty overflow')
        data = bytearray(block)
        struct.pack_into('<d', data, HEADER + 32, charged)
        return bytes(data)


def _command(value):
    if (not isinstance(value, list) or not value
            or any(not isinstance(item, str) or not item or '\0' in item for item in value)):
        raise ValueError('child command must be a nonempty JSON string array')
    return value


def _declared_bytes(command, option):
    """Read a companion's local cap without exposing command/identity data."""
    positions = [index for index, value in enumerate(command) if value == option]
    if len(positions) != 1 or positions[0] + 1 >= len(command):
        return None
    try:
        result = int(command[positions[0] + 1])
    except ValueError:
        return None
    return result if 0 < result <= (1 << 63) - 1 else None


def _fatal_code(error):
    if isinstance(error, (ValueError, json.JSONDecodeError)):
        return 'configuration'
    if not isinstance(error, TransportError):
        return 'transport'
    reason = str(error).lower()
    if 'clock' in reason or 'utc' in reason:
        return 'clock'
    if reason.startswith('local ') or 'local audio' in reason:
        return 'local_audio'
    if reason.startswith('remote '):
        return 'remote'
    return 'transport'


def _fatal_status(error, fd=1):
    # Only a fixed category is exposed to the GUI. A failed/non-reading GUI
    # must not keep children or the paid job alive indefinitely.
    payload = json.dumps({'code': _fatal_code(error)}, separators=(',', ':')).encode('ascii')
    pending = memoryview(frame(191, payload))
    deadline = time.monotonic() + .5
    try:
        os.set_blocking(fd, False)
        while pending and time.monotonic() < deadline:
            if not select.select([], [fd], [], max(0, deadline - time.monotonic()))[1]:
                break
            try:
                written = os.write(fd, pending)
            except BlockingIOError:
                continue
            if written <= 0:
                break
            pending = pending[written:]
    except (OSError, ValueError):
        pass


def _stop(child):
    if child is None:
        return
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            if os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT):
                break
            time.sleep(.02)
    finally:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.wait()
        for pipe in (child.stdin, child.stdout):
            if pipe:
                pipe.close()


class DiagnosticTail:
    """Drain opt-in diagnostics while retaining at most 4 KiB per child."""
    def __init__(self, pipe):
        self.pipe = pipe
        self.tail = bytearray()
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        while True:
            try:
                block = os.read(self.pipe.fileno(), 4096)
            except OSError:
                return
            if not block:
                return
            self.tail.extend(block)
            if len(self.tail) > 4096:
                del self.tail[:-4096]

    def finish(self, name):
        self.thread.join(timeout=2)
        self.pipe.close()
        if self.tail:
            value = self.tail.decode('utf-8', 'replace')
            value = ''.join(c if c in '\n\t' or (32 <= ord(c) < 127) else '?' for c in value)
            print(f'lab audio {name} diagnostic (may contain sensitive data): {value}', file=sys.stderr)


def run(remote_command, local_command, maximum, utc_uncertainty, remote_drift_ppm, *, input_fd=0, output_fd=1,
        stop_requested=lambda: False, stall_timeout=STALL_TIMEOUT, report_resources=False,
        debug_child_stderr=False, debug_audio_summary=False):
    """Run the relay. Raises on incomplete transport; closing the GUI cancels."""
    remote = local = None
    outputs = None
    remote_diagnostic = local_diagnostic = None
    counts = {}
    started_at = time.monotonic()

    def count(name, kind):
        if debug_audio_summary:
            key = f'{name}_{kind}'
            seconds = round(time.monotonic() - started_at, 3)
            metric = counts.setdefault(key, {'count': 0, 'first_s': seconds, 'last_s': seconds})
            metric['count'] += 1
            metric['last_s'] = seconds
    try:
        remote_command, local_command = _command(remote_command), _command(local_command)
        gate = ClockGate(maximum, utc_uncertainty, remote_drift_ppm)
        if not math.isfinite(stall_timeout) or stall_timeout <= 0:
            raise ValueError('stall timeout must be finite and positive')
        diagnostic = subprocess.PIPE if debug_child_stderr else subprocess.DEVNULL
        remote = subprocess.Popen(remote_command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=diagnostic, bufsize=0, start_new_session=True)
        if debug_child_stderr:
            remote_diagnostic = DiagnosticTail(remote.stderr)
        local = subprocess.Popen(local_command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=diagnostic, bufsize=0, start_new_session=True)
        if debug_child_stderr:
            local_diagnostic = DiagnosticTail(local.stderr)
        readers = {'gui': Reader(input_fd, b'DPL1', MAX_PAYLOAD + HEADER),
                   'remote': Reader(remote.stdout.fileno(), b'DPW1'),
                   'local': Reader(local.stdout.fileno(), b'DPW1', MAX_AUDIO)}
        outputs = {'gui': Output(output_fd),
                   'remote': Output(remote.stdin.fileno(), lambda kind: count('to_remote', kind)),
                   'local': Output(local.stdin.fileno(), lambda kind: count('to_local', kind))}
        for reader in readers.values():
            os.set_blocking(reader.fd, False)
        for output in outputs.values():
            os.set_blocking(output.fd, False)
        deferred = deque()
        deferred_size = 0
        gating = False
        # Retain capture ownership through both CAP_END and CAP_STOP, which can
        # arrive in either order on separate pipes. The remote may also have
        # issued a credit before it learned that capture ended.
        captures, playbacks = {}, set()
        max_streams = 256
        resources = {
            'capture_cap_bytes': _declared_bytes(local_command, '--capture-bytes'),
            'playback_cap_bytes': _declared_bytes(local_command, '--playback-bytes'),
            'capture_workspace_bytes': None,
            'playback_workspace_bytes': None,
            'companion_shared_control_limit_bytes': 512 * 1024,
            # Covers four bounded queues, input frame buffers, object metadata,
            # in-flight frame copies and partially written retained byte arrays.
            # Python/interpreter and child process baselines remain separate.
            'transport_buffer_allowance_bytes': 128 * 1024 * 1024,
            'browser_host_allowance_bytes': 512 * 1024 * 1024,
            'companion_worker_stack_count': 3,
            # The native companion queries actual pthread defaults; Python
            # cannot safely infer that byte count from its own stack limit.
            'companion_worker_stack_bytes': None,
            'workspace_includes_buffer_cap': True,
            'companion_control_included_in_browser_allowance': False,
            'workspace_receipts_are_conservative': True,
            'receipt_is_aggregate': False,
            'process_baseline_included': False,
        }

        def publish_resources():
            if report_resources:
                payload = json.dumps(resources, separators=(',', ':'), allow_nan=False).encode('ascii')
                outputs['gui'].put(frame(190, payload))

        publish_resources()

        def stream_id(payload):
            if len(payload) < 8:
                raise TransportError('truncated audio stream ID')
            stream = struct.unpack_from('<Q', payload)[0]
            if not stream:
                raise TransportError('invalid audio stream ID')
            return stream

        def deliver_audio(block):
            kind, payload = frame_info(block)
            stream = stream_id(payload)
            if kind == 200:
                if stream in captures or len(captures) >= max_streams:
                    raise TransportError('duplicate or excessive capture streams')
                captures[stream] = {'ended': False, 'stopped': False, 'cutoff': False,
                                    'cutoff_ack': False, 'cutoff_accepted': False}
            elif kind in (201, 207, 208):
                if stream not in captures:
                    raise TransportError('unknown capture stream')
                if kind == 208:
                    if (len(payload) != 16 or struct.unpack_from('<Q', payload, 8)[0] == 0
                            or captures[stream]['cutoff'] or captures[stream]['ended']
                            or captures[stream]['stopped']):
                        raise TransportError('invalid capture cutoff')
                    captures[stream]['cutoff'] = True
                if kind == 201:
                    captures[stream]['stopped'] = True
            elif kind == 202:
                if stream in playbacks or len(playbacks) >= max_streams:
                    raise TransportError('duplicate or excessive playback streams')
                playbacks.add(stream)
                block = gate.playback_open(block)
            elif stream not in playbacks:
                raise TransportError('unknown playback stream')
            elif kind == 204 and not any(c['cutoff_accepted'] and not c['ended'] and not c['stopped']
                                         for c in captures.values()):
                raise TransportError('playback PCM before an accepted live capture cutoff')
            outputs['local'].put(block, kind)
            if kind == 201 and captures[stream]['ended']:
                del captures[stream]

        def probe():
            request = gate.request()
            if request:
                outputs['remote'].put(envelope(0, request))

        def audio_out(block, *, from_deferred=False):
            nonlocal gating, deferred_size
            kind, payload = frame_info(block)
            if kind not in range(200, 209) or len(payload) > MAX_AUDIO:
                raise TransportError('unexpected remote audio frame')
            if gating or (deferred and not from_deferred):
                if deferred_size + len(block) > QUEUE_LIMIT or len(deferred) >= QUEUE_ITEMS:
                    raise TransportError('pending audio exceeded its bound')
                deferred.append(block)
                deferred_size += len(block)
            elif kind in (200, 203):
                gating = True
                if from_deferred:
                    deferred.appendleft(block)
                else:
                    deferred.append(block)
                deferred_size += len(block)
                probe()
            else:
                deliver_audio(block)

        def release():
            nonlocal gating, deferred_size
            gating = False
            # The first item is the control just qualified. Other controls
            # independently require a fresh measurement before proceeding.
            first = deferred.popleft()
            deferred_size -= len(first)
            deliver_audio(first)

        while True:
            if stop_requested():
                return
            now = time.monotonic()
            gate.check_pending(now)
            gate.check_continuity()
            if gate.last_ok is not None and now - gate.last_ok > PROBE_PERIOD and gate.pending is None:
                probe()
            for output in outputs.values():
                if output.items and now - output.last_progress > stall_timeout:
                    raise TransportError('transport output stalled')
            # Drain deferred audio in order as the local endpoint has room;
            # never turn temporary backpressure into a bulk queue overflow.
            if (deferred and not gating and outputs['local'].size <= QUEUE_LIMIT - MAX_AUDIO - HEADER
                    and len(outputs['local'].items) < QUEUE_ITEMS):
                block = deferred.popleft()
                deferred_size -= len(block)
                audio_out(block, from_deferred=True)
            with selectors.PollSelector() as selector:
                # Limit each source whenever its destination could overflow;
                # one full 8 MiB frame is always admissible when below half.
                # Both GUI and local can be readable in the same poll cycle;
                # reserve one maximum frame for each plus a generated probe.
                remote_reserve = MAX_PAYLOAD + MAX_AUDIO + 4 * HEADER + 32
                if (outputs['remote'].size <= QUEUE_LIMIT - remote_reserve
                        and len(outputs['remote'].items) <= QUEUE_ITEMS - 3):
                    selector.register(readers['gui'].fd, selectors.EVENT_READ, ('gui', 'read'))
                    selector.register(readers['local'].fd, selectors.EVENT_READ, ('local', 'read'))
                if (outputs['gui'].size <= QUEUE_LIMIT - MAX_PAYLOAD - HEADER
                        and len(outputs['gui'].items) < QUEUE_ITEMS
                        and outputs['local'].size <= QUEUE_LIMIT - MAX_AUDIO - HEADER
                        and len(outputs['local'].items) < QUEUE_ITEMS
                        and deferred_size <= QUEUE_LIMIT - MAX_AUDIO - HEADER
                        and len(deferred) < QUEUE_ITEMS):
                    selector.register(readers['remote'].fd, selectors.EVENT_READ, ('remote', 'read'))
                for name, output in outputs.items():
                    if output.items:
                        selector.register(output.fd, selectors.EVENT_WRITE, (name, 'write'))
                for key, _ in selector.select(.1):
                    name, op = key.data
                    if op == 'write':
                        outputs[name].write()
                        continue
                    try:
                        block = readers[name].read()
                    except TransportError as error:
                        raise TransportError(name + ' input: ' + str(error)) from error
                    if block is None:
                        continue
                    if block is False:
                        if name == 'gui':
                            return
                        raise TransportError(name + ' child ended before receiver completion')
                    if name == 'gui':
                        channel, inner = frame_info(block, b'DPL1')
                        kind, _ = frame_info(inner)
                        if channel not in (0, 1) or kind not in ((1, 2, 15) if channel == 0 else (2, 10, 11, 12)):
                            raise TransportError('invalid GUI authority channel')
                        outputs['remote'].put(block)
                    elif name == 'local':
                        kind, payload = frame_info(block)
                        if kind not in range(220, 230):
                            raise TransportError('unexpected local audio frame')
                        count('local', kind)
                        stream = stream_id(payload)
                        if kind in (220, 221, 222) and stream not in captures:
                            raise TransportError('unknown local capture stream')
                        if kind in (220, 221, 222) and captures[stream]['ended']:
                            raise TransportError('audio arrived after capture end')
                        if kind in (223, 224, 225, 226, 227) and stream not in playbacks:
                            raise TransportError('unknown local playback stream')
                        if kind == 228 and stream not in captures and stream not in playbacks:
                            raise TransportError('unknown local audio error stream')
                        if kind == 229:
                            if (stream not in captures or not captures[stream]['cutoff']
                                    or captures[stream]['cutoff_ack'] or len(payload) < 16):
                                raise TransportError('unexpected capture cutoff acknowledgment')
                            status, reason_size = struct.unpack_from('<II', payload, 8)
                            if status not in (0, 1) or reason_size > 512 or len(payload) != 16 + reason_size:
                                raise TransportError('invalid capture cutoff acknowledgment')
                            captures[stream]['cutoff_ack'] = True
                            captures[stream]['cutoff_accepted'] = bool(status)
                        if kind == 221:
                            block = gate.capture_pcm(block)
                        if kind in (220, 223):
                            if len(payload) < 32:
                                raise TransportError('truncated local audio format receipt')
                            workspace = struct.unpack_from('<Q', payload, 24)[0]
                            key = 'capture_workspace_bytes' if kind == 220 else 'playback_workspace_bytes'
                            resources[key] = workspace
                            publish_resources()
                        if kind == 222:
                            captures[stream]['ended'] = True
                            if captures[stream]['stopped']:
                                del captures[stream]
                        if kind in (226, 227):
                            playbacks.remove(stream)
                        outputs['remote'].put(envelope(0, block), kind)
                    else:
                        kind, payload = frame_info(block)
                        if kind in range(200, 209):
                            count('remote', kind)
                            audio_out(block)
                        elif kind == 108 and gate.response(payload):
                            if gating:
                                release()
                        elif kind in (101, 103, 104, 105, 106, 107, 108):
                            outputs['gui'].put(block)
                        else:
                            raise TransportError('unexpected remote worker frame')
    except (TransportError, ValueError, OSError) as error:
        # A partly sent UI frame cannot be followed by another frame without
        # corrupting the byte stream. In that case EOF is the only safe signal.
        if report_resources and (outputs is None or not outputs['gui'].partial):
            _fatal_status(error, output_fd)
        raise
    finally:
        if debug_audio_summary:
            queued = {name: output.size for name, output in outputs.items()} if outputs else {}
            print('lab audio frame counts: ' + json.dumps({'counts': counts, 'queued_bytes': queued},
                  separators=(',', ':')), file=sys.stderr)
        _stop(local)
        _stop(remote)
        if local_diagnostic:
            local_diagnostic.finish('local')
        if remote_diagnostic:
            remote_diagnostic.finish('remote')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--max-clock-offset', type=float, required=True)
    parser.add_argument('--local-utc-uncertainty', type=float, required=True)
    parser.add_argument('--remote-clock-drift-ppm', type=float, required=True)
    parser.add_argument('--remote-json', required=True, help='JSON argument array for the remote relay')
    parser.add_argument('--local-json', required=True, help='JSON argument array for the local audio companion')
    parser.add_argument('--report-resources', action='store_true',
                        help='send lab-only type 190 local resource receipts to the controlling GUI')
    parser.add_argument('--debug-child-stderr', action='store_true',
                        help='expose a bounded child diagnostic tail locally; may contain sensitive job data')
    parser.add_argument('--debug-audio-summary', action='store_true',
                        help='print numeric protocol frame counts on exit (no sample contents)')
    args = parser.parse_args(argv)
    stopped = False

    def stop(_number, _frame):
        nonlocal stopped
        stopped = True

    signal.signal(signal.SIGINT, stop)
    signal.signal(signal.SIGTERM, stop)
    entered = False
    try:
        remote_command, local_command = json.loads(args.remote_json), json.loads(args.local_json)
        entered = True
        run(remote_command, local_command, args.max_clock_offset,
            args.local_utc_uncertainty, args.remote_clock_drift_ppm, stop_requested=lambda: stopped,
            report_resources=args.report_resources, debug_child_stderr=args.debug_child_stderr,
            debug_audio_summary=args.debug_audio_summary)
    except (TransportError, ValueError, OSError, json.JSONDecodeError) as error:
        if args.report_resources and not entered:
            _fatal_status(error)
        print('lab audio transport: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

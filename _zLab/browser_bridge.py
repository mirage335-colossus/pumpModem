#!/usr/bin/env python3
"""Expose the shared Robust UI over a private loopback bridge.

The command must speak DPW1 on stdin/stdout; for SSH it must invoke a pod-side
relay which starts the native worker with anonymous pipes. This module never
constructs a shell command. The caller owns the command, pod and credentials.
Optional file imports are confined to an explicitly selected local directory
and use a separate trusted worker pipe. Live mode uses a separate local audio
transport and companion; browser audio and file exports remain disabled.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
from pathlib import Path
import secrets
import struct
import subprocess
import threading
import time
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parent.parent
_spec = importlib.util.spec_from_file_location('datapump_lab_preview', ROOT / 'tools' / 'preview.py')
_preview = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_preview)


class ErrorRetainingQueue(_preview.OutputQueue):
    def close(self, error):
        with self.condition:
            if self.error is not None:
                return
            self.error = error
            self.items.clear()
            self.size = 0
            self.condition.notify_all()


class SimulationWorker(_preview.Worker):
    def __init__(self, command, files=None, live=False):
        self.id = secrets.token_urlsafe(24)
        self.touched = time.monotonic()
        self.output = ErrorRetainingQueue() if live else _preview.OutputQueue()
        self.write_lock, self.event_lock = threading.Lock(), threading.Lock()
        self.state = threading.Condition()
        self.snapshot, self.export, self.active_service = None, None, None
        self.resources = None
        self.live = live
        self.failed_at = None
        self.close_lock = threading.Lock()
        self.closed, self.cancelled = threading.Event(), threading.Event()
        self.file_scope = _preview.HostFiles(files) if files else None
        self.file_fd = None
        self.file_channel = self.file_scope is not None
        # The remote program's stderr is discarded: it can contain modem or
        # transport data. Consequently SSH authentication and host-key failures
        # surface as a generic worker exit; diagnose SSH separately if needed.
        try:
            self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                            stderr=subprocess.DEVNULL, start_new_session=True, bufsize=0)
        except BaseException:
            if self.file_scope:
                self.file_scope.close()
            raise
        try:
            os.set_blocking(self.process.stdin.fileno(), False)
            self.reader = threading.Thread(target=self.read_output, daemon=True)
            self.reader.start()
        except BaseException:
            # Starting a child transfers ownership immediately, even if later
            # setup or thread creation fails. Never leave an SSH relay behind.
            self.closed.set()
            try:
                self.process.stdin.close()
            finally:
                try:
                    self.process.terminate()
                except ProcessLookupError:
                    pass
                try:
                    self.process.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
                self.process.stdout.close()
                if self.file_scope:
                    self.file_scope.close()
            raise

    def read_output(self):
        try:
            while True:
                header = self.read_exact(12)
                magic, kind, size = struct.unpack('<4sII', header)
                if magic != b'DPW1' or size > _preview.MAX_FRAME:
                    raise _preview.PreviewError('Invalid worker frame')
                if kind == 191 and self.live:
                    if size > 4096:
                        raise _preview.PreviewError('Invalid local transport error receipt')
                    receipt = json.loads(self.read_exact(size))
                    messages = {
                        'clock': 'Live audio clock qualification failed; check clock offset, UTC uncertainty and drift limits.',
                        'local_audio': 'Local audio companion stopped or failed; check the local audio device.',
                        'remote': 'Remote worker or connection failed; check the pod and SSH.',
                        'transport': 'Live audio transport failed or stalled; receiver completion was not established.',
                        'configuration': 'Live audio transport configuration is invalid.',
                    }
                    if (not isinstance(receipt, dict) or set(receipt) != {'code'}
                            or not isinstance(receipt['code'], str) or receipt['code'] not in messages):
                        raise _preview.PreviewError('Invalid local transport error receipt')
                    raise _preview.PreviewError(messages[receipt['code']])
                if kind == 190 and self.live:
                    if size > 4096:
                        raise _preview.PreviewError('Invalid local resource receipt')
                    payload = self.read_exact(size)
                    values = json.loads(payload)
                    numeric_keys = {'capture_cap_bytes', 'playback_cap_bytes', 'capture_workspace_bytes',
                                    'playback_workspace_bytes', 'companion_shared_control_limit_bytes',
                                    'transport_buffer_allowance_bytes', 'browser_host_allowance_bytes',
                                    'companion_worker_stack_count', 'companion_worker_stack_bytes'}
                    flags = {'process_baseline_included': False,
                             'workspace_includes_buffer_cap': True,
                             'companion_control_included_in_browser_allowance': False,
                             'workspace_receipts_are_conservative': True,
                             'receipt_is_aggregate': False}
                    if (not isinstance(values, dict) or set(values) != numeric_keys | flags.keys()
                            or any(values[key] is not expected for key, expected in flags.items())
                            or any(values[key] is not None and
                                   (type(values[key]) is not int or not 0 <= values[key] < 2**63)
                                   for key in numeric_keys)):
                        raise _preview.PreviewError('Invalid local resource receipt')
                    with self.state:
                        self.resources = values
                        self.state.notify_all()
                    continue  # Lab-only receipt is never sent to the renderer.
                if kind not in ((101, 103, 104, 108) if self.live else (101, 103, 104)):
                    raise _preview.PreviewError('Unexpected worker output; browser audio and file output are unavailable')
                payload = self.read_exact(size)
                if self.closed.is_set():
                    continue
                if kind == 101:
                    with self.state:
                        self.snapshot = json.loads(payload)
                        self.state.notify_all()
                self.output.put(header + payload)
        except (OSError, ValueError, _preview.PreviewError) as error:
            self.output.close(str(error))
            with self.state:
                self.state.notify_all()
            with self.write_lock:
                if not self.process.stdin.closed:
                    self.process.stdin.close()

    def input(self, data):
        blocks = _preview.frames(data)
        # Physical audio travels only via the local companion, while trusted
        # file input is created by service(), never by a browser frame.
        if any(kind not in ((1, 2, 15) if self.live else (1, 2)) for kind, _ in blocks):
            raise _preview.PreviewError('Only UI and supported clock input are admitted')
        with self.event_lock:
            for _, block in blocks:
                self.write(block)

    def write(self, data, trusted=False):
        if trusted and not self.file_channel:
            raise _preview.PreviewError('Host file imports are unavailable')
        if self.file_channel or self.live:
            # The browser never supplies the envelope. Only the checked service
            # operation below can send channel 1; input() admits DPW1 UI only.
            data = struct.pack('<4sII', b'DPL1', 1 if trusted else 0, len(data)) + data
        super().write(data, trusted=False)

    def service(self, request):
        if request.get('kind') != 'open_file':
            raise _preview.PreviewError('Only authorized file imports are available')
        if not self.file_scope:
            raise _preview.PreviewError('No local file root was selected')
        return super().service(request)

    def close(self):
        # Give the live transport time to terminate and reap its own two child
        # process groups before force-killing it. Retain the first live error.
        with self.close_lock:
            if self.closed.is_set():
                return
            self.closed.set()
            self.cancelled.set()
            self.output.close('Worker stopped')
            with self.state:
                if self.export:
                    self.export.finish('Worker stopped')
                self.state.notify_all()
            with self.write_lock:
                if not self.process.stdin.closed:
                    self.process.stdin.close()
            try:
                self.process.wait(timeout=6 if self.live else 3)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                try:
                    self.process.wait(timeout=6 if self.live else 2)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            self.reader.join(timeout=2)
            self.process.stdout.close()
            with self.event_lock:
                if self.file_scope:
                    self.file_scope.close()
                    self.file_scope = None


class Handler(_preview.Handler):
    def do_GET(self):
        # Use the existing, maintained UI, with the unavailable microphone
        # control visibly disabled rather than requesting device permission.
        if self.path == '/lab-resources':
            try:
                if not self.authenticated():
                    raise _preview.PreviewError('Unauthorized', 403)
                worker = self.server.owned(self.headers.get('X-Preview-Session', ''))
                if not worker.live:
                    raise _preview.PreviewError('Not found', 404)
                with worker.state:
                    data = json.dumps(worker.resources)
                return self.reply(data, 'application/json')
            except _preview.PreviewError as error:
                return self.reply(str(error), status=error.status)
        if self.path in ('/', '/tools/preview.mjs'):
            try:
                if not self.local():
                    raise _preview.PreviewError('Invalid origin or host', 403)
                if self.path == '/':
                    data = (ROOT / 'tools' / 'preview.html').read_text()
                    data = data.replace('Native Data Pump worker', 'RunPod Robust live/duplex' if self.server.live else 'RunPod Robust simulation')
                    data = data.replace('Enable microphone and audio', 'Audio handled by local companion' if self.server.live else 'Live audio unavailable')
                    if self.server.live:
                        data = data.replace('<p id="status"', '<p id="lab-resources">Local audio resources: awaiting receipt</p><p id="status"')
                    return self.reply(data, 'text/html; charset=utf-8')
                data = (ROOT / 'tools' / 'preview.mjs').read_text()
                old = 'audio.disabled=false;audio.onclick=()=>view.enableAudio().catch(report);'
                if old not in data:
                    raise _preview.PreviewError('Unsupported preview UI version', 500)
                data = data.replace(old, "audio.disabled=true;audio.title='Audio is handled outside the browser.';")
                if self.server.live:
                    anchor = 'session=result.session;files=result.files;startResolve();'
                    if anchor not in data:
                        raise _preview.PreviewError('Unsupported preview UI version', 500)
                    update = """session=result.session;files=result.files;startResolve();
                    const showResources=async()=>{try {const r=await (await request('/lab-resources')).json();
                        if(r)document.querySelector('#lab-resources').textContent='Local audio resources (bytes unless stated; workspace includes its buffer cap; process baseline excluded; this is not a total): '+JSON.stringify(r);
                    }catch(error){if(!stopped)report(error);}};
                    showResources();resourcesTimer=setInterval(showResources,2000);"""
                    data = data.replace(anchor, update)
                    data = data.replace("let session='',view,stopped=false,heartbeat,files=null,snapshot=null;",
                                        "let session='',view,stopped=false,heartbeat,resourcesTimer,files=null,snapshot=null;")
                    data = data.replace('clearInterval(heartbeat);inputs.close();',
                                        'clearInterval(heartbeat);clearInterval(resourcesTimer);inputs.close();')
                return self.reply(data, 'text/javascript')
            except (OSError, _preview.PreviewError) as error:
                return self.reply(str(error), status=getattr(error, 'status', 500))
        return super().do_GET()


class Server(_preview.PreviewServer):
    def __init__(self, command, port, files=None, live=False):
        self.command = command
        self.files = files
        self.live = live
        options = SimpleNamespace(mode='worker', simulation=not live, files=files)
        # PreviewServer fixes the bind address to IPv4 loopback and implements
        # request limits, origin/token checks and session lease enforcement.
        super().__init__(options, port)
        self.RequestHandlerClass = Handler

    def start_worker(self):
        with self.owner_lock:
            if self.stopping.is_set():
                raise _preview.PreviewError('Bridge is stopping', 410)
            if self.worker is not None:
                raise _preview.PreviewError('Another tab owns this worker', 409)
            self.worker = SimulationWorker(self.command, self.files, self.live)
            return self.worker

    def maintain(self):
        if not self.live:
            return super().maintain()
        # Reap failed transport promptly, but retain only its sanitized error
        # for a fixed lease that heartbeats and status requests cannot extend.
        while not self.stopping.wait(.5):
            with self.owner_lock:
                worker = self.worker
                if not worker:
                    continue
                now = time.monotonic()
                if worker.output.error and worker.failed_at is None:
                    worker.failed_at = now
                    try:
                        worker.process.terminate()
                    except ProcessLookupError:
                        pass
                    worker.close()
                deadline_start = worker.failed_at if worker.failed_at is not None else worker.touched
                if time.monotonic() - deadline_start > _preview.LEASE:
                    worker.close()
                    self.worker = None


class Bridge:
    def __init__(self, command, port=0, files=None, live=False):
        if not isinstance(command, (list, tuple)) or not command or any(not isinstance(x, str) or not x for x in command):
            raise ValueError('command must be a nonempty argument list')
        if not isinstance(port, int) or not 0 <= port <= 65535:
            raise ValueError('invalid port')
        self.server = Server(list(command), port, files, live)
        self.url = self.server.origin + '/#' + self.server.token
        self._closed = False
        self._server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self._maintenance = threading.Thread(target=self.server.maintain, daemon=True)
        self._server_thread.start()
        self._maintenance.start()

    def close(self):
        if self._closed:
            return
        self._closed = True
        self.server.stopping.set()
        self.server.shutdown()
        with self.server.owner_lock:
            worker = self.server.worker
        if worker:
            self.server.stop_worker(worker.id)
        self.server.server_close()
        self._maintenance.join(timeout=3)
        self._server_thread.join(timeout=3)

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def start(command, port=0, files=None, live=False):
    return Bridge(command, port, files, live)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=0)
    parser.add_argument('--files', help='explicit local import root; relay must use --file-channel')
    parser.add_argument('--live', action='store_true', help='child is a local DPL1 live audio transport')
    parser.add_argument('command', nargs=argparse.REMAINDER, help='DPW1 relay executable and arguments')
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('supply a relay command after --')
    with start(command, args.port, files=args.files, live=args.live) as bridge:
        print('Open ' + bridge.url, flush=True)
        try:
            threading.Event().wait()
        except KeyboardInterrupt:
            pass


if __name__ == '__main__':
    main()

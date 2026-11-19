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
import select
import signal
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
        self._close_complete = False
        self._group_stopped = False
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
        except BaseException as setup_error:
            # Starting a child transfers ownership immediately, even if later
            # setup or thread creation fails. Never leave an SSH relay behind.
            self.closed.set()
            cleanup_errors = []
            try:
                self.process.stdin.close()
            except Exception as error:
                cleanup_errors.append(error)
            can_signal_group = True
            try:
                # A reaped PID is no longer safe to use as a process-group ID.
                self._wait_without_reaping(0)
            except ChildProcessError as error:
                cleanup_errors.append(error)
                can_signal_group = False
            if can_signal_group:
                try:
                    os.killpg(self.process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                except Exception:
                    # Still attempt the unconditional group kill below.
                    pass
                try:
                    self._wait_without_reaping(2)
                except ChildProcessError as error:
                    cleanup_errors.append(error)
                    can_signal_group = False
                except Exception as error:
                    cleanup_errors.append(error)
            group_stopped = False
            for _ in range(2 if can_signal_group else 0):
                try:
                    # Retain the leader until all processes still in its group
                    # have been signaled, even if it has already exited.
                    os.killpg(self.process.pid, signal.SIGKILL)
                    group_stopped = True
                    break
                except ProcessLookupError:
                    group_stopped = True
                    break
                except OSError as error:
                    cleanup_error = error
            if group_stopped:
                try:
                    self.process.wait(timeout=2)
                except Exception as error:
                    cleanup_errors.append(error)
            elif can_signal_group:
                cleanup_errors.append(cleanup_error)
            try:
                self.process.stdout.close()
            except Exception as error:
                cleanup_errors.append(error)
            if self.file_scope:
                try:
                    self.file_scope.close()
                except Exception as error:
                    cleanup_errors.append(error)
            if cleanup_errors:
                raise RuntimeError('Bridge startup cleanup was incomplete; the worker may need manual termination.') from setup_error
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
        with self.write_lock:
            if self.closed.is_set() or self.process.stdin.closed:
                raise _preview.PreviewError('Worker stopped', 410)
            # Do not use Popen.poll(): reaping the leader here could release
            # the process-group ID while a descendant still holds the pipes.
            fd = self.process.stdin.fileno()
            view, deadline = memoryview(data), time.monotonic() + 3
            while view:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([], [fd], [], remaining)[1]:
                    raise _preview.PreviewError('Worker input pipe stalled')
                try:
                    view = view[os.write(fd, view):]
                except BlockingIOError:
                    pass

    def _wait_without_reaping(self, timeout):
        """Reserve the leader's PID until the owned process group is stopped."""
        if self.process.returncode is not None:
            return True
        deadline = time.monotonic() + timeout
        while True:
            try:
                if os.waitid(os.P_PID, self.process.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT):
                    return True
            except InterruptedError:
                continue
            if time.monotonic() >= deadline:
                return False
            time.sleep(.02)

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
            if self._close_complete:
                return
            # `closed` rejects new work as soon as shutdown starts. It is not
            # proof that the child and its resources have been reaped: a
            # later close must be able to finish an interrupted cleanup.
            self.closed.set()
            self.cancelled.set()
            errors = []

            def attempt(action):
                try:
                    action()
                except Exception as error:
                    errors.append(error)

            attempt(lambda: self.output.close('Worker stopped'))

            def notify_state():
                with self.state:
                    if self.export:
                        self.export.finish('Worker stopped')
                    self.state.notify_all()
            attempt(notify_state)

            def close_input():
                with self.write_lock:
                    if not self.process.stdin.closed:
                        self.process.stdin.close()
            attempt(close_input)

            # Wait for ordinary EOF cleanup without reaping the group leader.
            # Its PID reserves the process-group identity, including when it
            # exited before a descendant that still holds the output pipe.
            can_signal_group = not self._group_stopped and self.process.returncode is None
            try:
                self._wait_without_reaping(6 if self.live else 3)
            except ChildProcessError as error:
                # Never signal a possibly reused PID if somebody else reaped it.
                can_signal_group = False
                errors.append(error)
            except Exception as error:
                errors.append(error)

            if can_signal_group:
                try:
                    os.killpg(self.process.pid, signal.SIGTERM)
                except ProcessLookupError:
                    pass
                except Exception as error:
                    errors.append(error)
                try:
                    self._wait_without_reaping(2)
                except ChildProcessError as error:
                    errors.append(error)
                    can_signal_group = False
                except Exception as error:
                    errors.append(error)
                if can_signal_group:
                    try:
                        # Also stop descendants if the group leader exited first.
                        os.killpg(self.process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        self._group_stopped = True
                    except Exception as error:
                        errors.append(error)
                    else:
                        self._group_stopped = True
            # Never reap the group leader after a failed group signal. Its
            # retained PID prevents a retry from signaling a reused group ID.
            if self._group_stopped:
                attempt(lambda: self.process.wait(timeout=2))
            elif self.process.returncode is not None:
                errors.append(RuntimeError('Worker leader was reaped before its process group stopped'))

            attempt(lambda: self.reader.join(timeout=2))
            if self.reader.is_alive():
                errors.append(RuntimeError('Worker output reader did not stop'))
            else:
                attempt(self.process.stdout.close)

            def close_files():
                with self.event_lock:
                    if self.file_scope:
                        self.file_scope.close()
                        self.file_scope = None
            attempt(close_files)
            if self.process.returncode is None:
                errors.append(RuntimeError('Worker process did not stop'))
            if errors:
                raise errors[0]
            self._close_complete = True


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
        if self.path in ('/', '/tools/preview.mjs', '/web/style.css'):
            try:
                if not self.local():
                    raise _preview.PreviewError('Invalid origin or host', 403)
                if self.path == '/':
                    data = (ROOT / 'tools' / 'preview.html').read_text()
                    # Keep the lab shell's native controls and dialogs dark,
                    # independently of the user's browser/OS preference.
                    data = data.replace('</style>', ':root { color-scheme: dark; }\n</style>', 1)
                    data = data.replace('Native Data Pump worker', 'RunPod Robust live/duplex' if self.server.live else 'RunPod Robust simulation')
                    data = data.replace('Enable microphone and audio', 'Audio handled by local companion' if self.server.live else 'Live audio unavailable')
                    if self.server.live:
                        data = data.replace('<p id="status"', '<p id="lab-resources">Local audio resources: awaiting receipt</p><p id="status"')
                    return self.reply(data, 'text/html; charset=utf-8')
                if self.path == '/web/style.css':
                    # The shared renderer runs in a sandboxed iframe and is
                    # passed this text. Override its light/dark preference for
                    # the lab only; the shared Data Pump asset stays unchanged.
                    data = (ROOT / 'web' / 'style.css').read_text()
                    return self.reply(data + '\n:root { color-scheme: dark; }\n', 'text/css')
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
        # Reap failed workers promptly. A failed live transport retains only
        # its sanitized error for a fixed lease that heartbeats cannot extend.
        # A transient close error keeps the same owner for the next sweep.
        while not self.stopping.wait(.5):
            with self.owner_lock:
                worker = self.worker
                if not worker:
                    continue
                now = time.monotonic()
                if self.live and worker.output.error and worker.failed_at is None:
                    worker.failed_at = now
                    try:
                        if worker.process.returncode is None:
                            os.killpg(worker.process.pid, signal.SIGTERM)
                    except ProcessLookupError:
                        pass
                    except OSError:
                        # close() still tries the other cleanup stages.
                        pass
                if self.live and worker.failed_at is not None:
                    try:
                        worker.close()
                    except Exception:
                        continue
                deadline_start = worker.failed_at if self.live and worker.failed_at is not None else worker.touched
                if (not self.live and worker.output.error) or time.monotonic() - deadline_start > _preview.LEASE:
                    try:
                        worker.close()
                    except Exception:
                        continue
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
        self._close_lock = threading.Lock()
        self._server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self._maintenance = threading.Thread(target=self.server.maintain, daemon=True)
        self._server_thread.start()
        self._maintenance.start()

    def close(self):
        with self._close_lock:
            if self._closed:
                return
            self.server.stopping.set()
            errors = []

            def attempt(action):
                try:
                    action()
                except Exception as error:
                    errors.append(error)

            # Each stage is independently attempted, even if an earlier one
            # fails. The worker stays registered until its close succeeds, so
            # a caller can retry rather than orphaning an active process.
            attempt(self.server.shutdown)
            with self.server.owner_lock:
                worker = self.server.worker
            if worker:
                attempt(lambda: self.server.stop_worker(worker.id))
            attempt(self.server.server_close)
            attempt(lambda: self._maintenance.join(timeout=3))
            attempt(lambda: self._server_thread.join(timeout=3))
            if self._maintenance.is_alive() or self._server_thread.is_alive():
                errors.append(RuntimeError('Bridge threads did not stop'))
            if errors:
                raise errors[0]
            self._closed = True

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

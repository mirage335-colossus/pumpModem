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
from contextlib import contextmanager
import importlib.util
import json
import os
from pathlib import Path
import re
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

_BROWSER_START_TIMEOUT = 60
_RECONNECT_GRACE = 8
_BROWSER_LEASE = 90
_CLIENT_ID = re.compile(r'[a-f0-9]{64}')


def _gpu_unavailable_toggle(controls):
    """Whether an unavailable preferred GPU still has an enabled CPU opt-out."""
    return any(
        isinstance(candidate, dict) and candidate.get('kind') == 'toggle'
        and candidate.get('enabled') is True
        and str(candidate.get('label', '')).strip().lower() == 'gpu'
        for candidate in controls)


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
    def __init__(self, command, files=None, live=False, managed=False):
        self.id = secrets.token_urlsafe(24)
        self.touched = time.monotonic()
        self.output = ErrorRetainingQueue()
        self.write_lock, self.event_lock = threading.Lock(), threading.Lock()
        self.state = threading.Condition()
        self.snapshot, self.export, self.active_service = None, None, None
        self.snapshot_frame = None
        self.startup_error = None
        self.ready = False
        self.browser_attached = not managed
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
                with self.state:
                    if kind == 101:
                        self.snapshot = json.loads(payload)
                        self.snapshot_frame = header + payload
                    elif kind in (103, 104) and not self.ready:
                        self.startup_error = 'The native worker could not initialize.'
                    if self.browser_attached:
                        self.output.put(header + payload)
                    self.state.notify_all()
        except (OSError, ValueError, _preview.PreviewError) as error:
            self.output.close(str(error))
            with self.state:
                self.state.notify_all()
            with self.write_lock:
                if not self.process.stdin.closed:
                    self.process.stdin.close()

    def wait_ready(self, timeout):
        def is_ready():
            if self.output.error or self.closed.is_set() or self.startup_error:
                return True
            snapshot = self.snapshot
            if (not isinstance(snapshot, dict) or snapshot.get('version') != 1
                    or not isinstance(snapshot.get('controls'), list)):
                return False
            # Live reception also needs browser settings (e.g., noise density).
            # Its worker and companion initialization are acknowledged here;
            # this is not a physical audio qualification.
            if self.live:
                return self.resources is not None
            for control in snapshot['controls']:
                if not isinstance(control, dict) or control.get('kind') != 'label':
                    continue
                value = control.get('text')
                if isinstance(value, str) and value.startswith(('GPU reception unavailable', 'GPU reception is unavailable')):
                    # A preferred GPU backend can fail to initialize while the
                    # shared GUI still offers an enabled GPU toggle. That is a
                    # usable runtime CPU choice; a strict worker still exits
                    # through the normal startup failure path.
                    if _gpu_unavailable_toggle(snapshot['controls']):
                        return True
                    self.startup_error = 'GPU reception is unavailable on this pod.'
                    return True
                if value == 'Robust simulation ready':
                    return True
            return False

        with self.state:
            if not self.state.wait_for(is_ready, timeout=timeout):
                raise _preview.PreviewError('Timed out waiting for the native worker to become ready.')
            if self.startup_error or self.output.error or self.closed.is_set():
                raise _preview.PreviewError(self.startup_error or self.output.error or 'The native worker stopped.')
            self.ready = True

    def attach_browser(self, attached):
        """Start a presentation with the worker's complete current snapshot."""
        with self.state, self.output.condition:
            self.browser_attached = attached
            self.output.items.clear()
            self.output.size = 0
            if attached and self.snapshot_frame and not self.output.error:
                self.output.items.append(self.snapshot_frame)
                self.output.size = len(self.snapshot_frame)
            self.output.condition.notify_all()

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
    def dispatch(self, method):
        if not self.server.managed or (method == 'GET' and self.path.startswith('/web/')
                                        and self.path[5:] in _preview.WEB_ASSETS):
            return super().dispatch(method)
        try:
            if not self.authenticated():
                raise _preview.PreviewError('Unauthorized', 403)
            session = self.headers.get('X-Preview-Session', '')
            client = self.headers.get('X-Preview-Client', '')
            if method == 'POST' and self.path == '/start':
                self.body(0)
                session = self.server.attach(client)
                files = self.server.options.files
                return self.reply(json.dumps({'session': session, 'files': str(files) if files else None,
                                              'simulation': not self.server.live}), 'application/json')
            if method == 'POST' and self.path == '/browser-error':
                self.body(0)
                self.server.browser_error(client, session)
                return self.reply()
            if method == 'POST' and self.path == '/stop':
                self.body(0)
                self.server.end_browser(session)
                return self.reply()
            if method == 'POST' and self.path == '/detach':
                self.body(0)
                self.server.detach(session)
                return self.reply()
            with self.server.active_request(session):
                return super().dispatch(method)
        except (OSError, ValueError, TypeError, _preview.PreviewError) as error:
            self.close_connection = True
            try:
                message = str(error) if isinstance(error, _preview.PreviewError) else 'Invalid browser request.'
                return self.reply(message, status=getattr(error, 'status', 400))
            except OSError:
                pass

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
        if self.path == '/favicon.ico' and self.local():
            return self.reply(status=204)
        if self.path in ('/', '/tools/preview.mjs', '/web/style.css'):
            try:
                if not self.local():
                    raise _preview.PreviewError('Invalid origin or host', 403)
                if self.path == '/':
                    data = (ROOT / 'tools' / 'preview.html').read_text()
                    if self.server.managed:
                        data = data.replace('<html lang="en">', '<html lang="en" data-lab-managed>')
                        data = data.replace('<button id="audio" disabled>', '<button id="audio" hidden disabled>')
                        label = 'End Live/Duplex' if self.server.live else 'End simulation'
                        data = data.replace('Stop worker</button>', label + '</button>')
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
    def __init__(self, command, port, files=None, live=False, managed=False):
        self.command = command
        self.files = files
        self.live = live
        self.managed = managed
        self.prepared_at = None
        self.ready_timeout = None
        self.resetting = False
        self.browser_client = self.browser_session = self.detached_at = None
        self.active_requests = 0
        self.outcome = None
        options = SimpleNamespace(mode='worker', simulation=not live, files=files)
        # PreviewServer fixes the bind address to IPv4 loopback and implements
        # request limits, origin/token checks and session lease enforcement.
        super().__init__(options, port)
        self.RequestHandlerClass = Handler
        self.request_idle = threading.Condition(self.owner_lock)

    def start_worker(self):
        with self.owner_lock:
            if self.stopping.is_set():
                raise _preview.PreviewError('Bridge is stopping', 410)
            if self.worker is not None:
                raise _preview.PreviewError('Another tab owns this worker', 409)
            self.worker = SimulationWorker(self.command, self.files, self.live, self.managed)
            return self.worker

    def prepare(self, timeout):
        self.start_worker().wait_ready(timeout)
        with self.owner_lock:
            self.ready_timeout = timeout
            self.prepared_at = time.monotonic()

    def _check_running(self):
        if self.outcome or self.stopping.is_set():
            raise _preview.PreviewError(self.outcome[1] if self.outcome else 'Simulation ended.', 410)

    def _attach_worker(self, client):
        self.browser_client = client
        self.browser_session = secrets.token_urlsafe(24)
        self.detached_at = None
        self.worker.touched = time.monotonic()
        self.worker.attach_browser(True)
        return self.browser_session

    def attach(self, client):
        if not _CLIENT_ID.fullmatch(client):
            raise _preview.PreviewError('Invalid browser session identifier.', 400)
        with self.request_idle:
            def check_available():
                self._check_running()
                if self.browser_client is not None and not secrets.compare_digest(self.browser_client, client):
                    raise _preview.PreviewError('The simulation is open in another browser tab.', 409)
                if self.prepared_at is None or self.resetting:
                    raise _preview.PreviewError('The worker is still starting.', 425)

            check_available()
            if self.browser_client is None or self.live:
                # Live reattachment shares the existing worker's output queue
                # with the previous page's last outstanding request.
                if not self.request_idle.wait_for(lambda: not self.active_requests, timeout=2):
                    raise _preview.PreviewError('Waiting for the previous page to disconnect.', 425)
                check_available()
                return self._attach_worker(client)

            # Fence the old page before releasing the lock. Already admitted
            # requests still reference the old worker, including long polls
            # and file imports which its close will wake or cancel.
            self.resetting = True
            self.browser_session = None
            previous = self.worker
        try:
            previous.close()
            with self.owner_lock:
                self._check_running()
                self.worker = SimulationWorker(self.command, self.files, self.live, self.managed)
                worker = self.worker
            worker.wait_ready(self.ready_timeout)
            with self.owner_lock:
                self._check_running()
                return self._attach_worker(client)
        except (OSError, RuntimeError, _preview.PreviewError) as error:
            with self.owner_lock:
                self._check_running()
                self.outcome = ('failed', 'The simulation could not reset to defaults.')
            raise _preview.PreviewError(self.outcome[1], 500) from error
        finally:
            with self.owner_lock:
                self.resetting = False

    def _matches_browser(self, session):
        return (self.browser_session is not None and isinstance(session, str)
                and secrets.compare_digest(self.browser_session, session))

    def detach(self, session):
        with self.owner_lock:
            if self._matches_browser(session) and not self.outcome and self.detached_at is None:
                self.detached_at = time.monotonic()
                self.worker.attach_browser(False)

    def _browser_worker(self, session):
        if self.outcome or self.worker is None:
            raise _preview.PreviewError(self.outcome[1] if self.outcome else 'Simulation ended.', 410)
        if not self._matches_browser(session) or self.detached_at is not None:
            raise _preview.PreviewError('This page is disconnected from the simulation.', 410)
        self.worker.touched = time.monotonic()
        return self.worker

    def owned(self, session):
        if not self.managed:
            return super().owned(session)
        with self.owner_lock:
            return self._browser_worker(session)

    @contextmanager
    def active_request(self, session):
        with self.owner_lock:
            self._browser_worker(session)
            self.active_requests += 1
        try:
            yield
        finally:
            with self.request_idle:
                self.active_requests -= 1
                if not self.active_requests:
                    self.request_idle.notify_all()

    def end_browser(self, session):
        with self.owner_lock:
            self._browser_worker(session)
            self.outcome = ('ended', 'Simulation ended.')

    def browser_error(self, client, session):
        with self.owner_lock:
            if (not self.outcome and _CLIENT_ID.fullmatch(client)
                    and (self.browser_client is None or self._matches_browser(session))):
                self.outcome = ('failed', 'The browser could not load the simulation.')

    def status(self):
        with self.owner_lock:
            if self.outcome:
                return self.outcome
            if self.browser_session:
                return 'active', ''
            return 'waiting', ''

    def maintain_browser(self):
        while not self.stopping.wait(.25):
            with self.owner_lock:
                worker = self.worker
                if not worker or self.prepared_at is None or self.resetting:
                    continue
                now = time.monotonic()
                if not self.outcome:
                    if isinstance(worker.snapshot, dict) and worker.snapshot.get('closing') is True:
                        self.outcome = ('ended', 'Simulation ended.')
                    elif worker.output.error:
                        error = worker.output.error if self.live else 'The native simulation worker disconnected.'
                        self.outcome = ('failed', error)
                    elif self.browser_session is None and now - self.prepared_at >= _BROWSER_START_TIMEOUT:
                        self.outcome = ('failed', 'The browser did not open the simulation.')
                    elif (self.detached_at is not None and now - self.detached_at >= _RECONNECT_GRACE
                          or self.browser_session and now - worker.touched >= _BROWSER_LEASE):
                        self.outcome = ('ended', 'The simulation page closed.')
                reap = bool(self.outcome)
            if reap:
                try:
                    worker.close()
                except (OSError, RuntimeError):
                    continue
                with self.owner_lock:
                    if self.worker is worker:
                        self.worker = None

    def maintain(self):
        if self.managed:
            return self.maintain_browser()
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
    def __init__(self, command, port=0, files=None, live=False, *, managed=False, ready_timeout=30):
        if not isinstance(command, (list, tuple)) or not command or any(not isinstance(x, str) or not x for x in command):
            raise ValueError('command must be a nonempty argument list')
        if not isinstance(port, int) or not 0 <= port <= 65535:
            raise ValueError('invalid port')
        self.server = Server(list(command), port, files, live, managed)
        self.url = self.server.origin + '/#' + self.server.token
        self._closed = False
        self._close_lock = threading.Lock()
        self._server_thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self._maintenance = threading.Thread(target=self.server.maintain, daemon=True)
        self._server_thread.start()
        self._maintenance.start()
        if managed:
            try:
                self.server.prepare(ready_timeout)
            except BaseException:
                self.close()
                raise

    def status(self):
        return self.server.status()

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
                if worker and self.server.managed and not self.server.outcome:
                    self.server.outcome = ('ended', 'Simulation ended.')
                    worker.output.close(self.server.outcome[1])
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


def start(command, port=0, files=None, live=False, *, managed=False, ready_timeout=30):
    return Bridge(command, port, files, live, managed=managed, ready_timeout=ready_timeout)


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

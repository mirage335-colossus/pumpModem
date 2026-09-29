#!/usr/bin/env python3
"""Local development host for Data Pump's worker and WebAssembly builds.

Uses only the Python standard library. Build first with build.sh; this helper
never builds, downloads, installs or exposes a listener beyond IPv4 loopback.
It is not installed in application packages. See COMPILE-web.
"""
import argparse
from collections import deque
import json
import os
from pathlib import Path
import secrets
import select
import signal
import socket
import stat
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parent.parent
MAX_FRAME = 8 * 1024 * 1024
MAX_QUEUE = 16 * 1024 * 1024
MAX_FILE = 256 * 1024 * 1024
LEASE = 15.0
WEB_ASSETS = {'hosted.mjs', 'protocol.mjs', 'renderer.mjs', 'browser_audio.mjs',
              'audio_worklet.js', 'style.css'}


class PreviewError(Exception):
    def __init__(self, message, status=400):
        super().__init__(message)
        self.status = status


def frame(kind, payload=b''):
    return b'DPW1' + struct.pack('<II', kind, len(payload)) + payload


def frames(data):
    """Validate the entire bounded request before writing any of its frames."""
    result, at = [], 0
    while at < len(data):
        if len(data) - at < 12:
            raise PreviewError('Truncated frame header')
        magic, kind, size = struct.unpack_from('<4sII', data, at)
        if magic != b'DPW1' or size > MAX_FRAME or size > len(data) - at - 12:
            raise PreviewError('Invalid frame length or magic')
        result.append((kind, data[at:at + 12 + size]))
        at += 12 + size
    return result


def string(value):
    data = value.encode('utf-8')
    return struct.pack('<I', len(data)) + data


def event_bytes(event, value, cancelled=False):
    return struct.pack('<IQQQIII', 1, int(event['generation']), int(event['sequence']),
                       int(event['target']), 13, 2 if cancelled else 0, 0) + string(value) + string('')


class OutputQueue:
    def __init__(self, limit=MAX_QUEUE):
        self.condition = threading.Condition()
        self.items, self.size, self.error, self.limit = deque(), 0, None, limit

    def put(self, data):
        with self.condition:
            if self.error:
                return
            if self.size + len(data) > self.limit or len(self.items) >= 2048:
                raise PreviewError('Browser output queue is full')
            self.items.append(data)
            self.size += len(data)
            self.condition.notify_all()

    def get(self):
        with self.condition:
            self.condition.wait_for(lambda: self.items or self.error, timeout=1)
            if self.error:
                raise PreviewError(self.error, 410)
            parts, count = [], 0
            while self.items and (not parts or count + len(self.items[0]) <= 256 * 1024):
                part = self.items.popleft()
                parts.append(part)
                count += len(part)
            self.size -= count
            return b''.join(parts)

    def close(self, error):
        with self.condition:
            self.error = error
            self.items.clear()
            self.size = 0
            self.condition.notify_all()


class HostFiles:
    """Descriptor-relative file choices; never give the worker a host path."""
    def __init__(self, root):
        self.root = root
        self.fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)

    def parent(self, value):
        path = Path(value)
        if path.is_absolute():
            try:
                path = path.relative_to(self.root)
            except ValueError as error:
                raise PreviewError('Choose a path within --files') from error
        if not path.parts or any(p in ('..', '.') for p in path.parts):
            raise PreviewError('Choose a relative filename within --files')
        descriptor = os.dup(self.fd)
        try:
            for part in path.parts[:-1]:
                child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW,
                                dir_fd=descriptor)
                os.close(descriptor)
                descriptor = child
            return descriptor, path.name
        except BaseException:
            os.close(descriptor)
            raise

    def read(self, value):
        parent, name = self.parent(value)
        try:
            fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
        finally:
            os.close(parent)
        try:
            info = os.fstat(fd)
            if not stat.S_ISREG(info.st_mode) or info.st_size > MAX_FILE:
                raise PreviewError('Select a regular file of at most 256 MiB')
            return os.fdopen(fd, 'rb'), name, info
        except BaseException:
            os.close(fd)
            raise

    def save(self, value, target):
        parent, name = self.parent(value)
        temporary = '.datapump-preview-' + secrets.token_hex(16) + '.partial'
        try:
            try:
                os.stat(name, dir_fd=parent, follow_symlinks=False)
            except FileNotFoundError:
                pass
            else:
                raise PreviewError('Choose a new filename; overwrites are disabled')
            fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW,
                         0o600, dir_fd=parent)
            return Export(parent, name, temporary, fd, target)
        except BaseException:
            os.close(parent)
            raise

    def close(self):
        os.close(self.fd)


class Export:
    def __init__(self, parent, name, temporary, fd, target):
        self.parent, self.name, self.temporary = parent, name, temporary
        self.file, self.target = os.fdopen(fd, 'wb'), int(target)
        info = os.fstat(fd)
        self.identity = (info.st_dev, info.st_ino)
        self.total, self.position, self.error = None, 0, None
        self.done = threading.Event()
        self.lock = threading.Lock()

    def finish(self, error=None):
        with self.lock:
            if self.done.is_set():
                return
            try:
                if error:
                    raise PreviewError(error)
                if self.total is None or self.position != self.total:
                    raise PreviewError('Incomplete worker file export')
                self.file.flush()
                os.fsync(self.file.fileno())
                # Linux linkat follows this descriptor, not the replaceable
                # temporary pathname. An existing destination still fails.
                os.link('/proc/self/fd/' + str(self.file.fileno()), self.name,
                        dst_dir_fd=self.parent, follow_symlinks=True)
            except (OSError, PreviewError) as failure:
                self.error = str(failure)
            finally:
                try:
                    self.file.close()
                except OSError as failure:
                    self.error = self.error or str(failure)
                finally:
                    try:
                        info = os.stat(self.temporary, dir_fd=self.parent, follow_symlinks=False)
                        if (info.st_dev, info.st_ino) == self.identity:
                            os.unlink(self.temporary, dir_fd=self.parent)
                    except FileNotFoundError:
                        pass
                    except OSError as failure:
                        self.error = self.error or str(failure)
                    finally:
                        os.close(self.parent)
                        self.done.set()

    def accept(self, kind, payload):
        if len(payload) < 8 or struct.unpack_from('<Q', payload)[0] != self.target:
            raise PreviewError('Unexpected file export target')
        if kind == 107:
            if len(payload) < 12:
                raise PreviewError('Truncated export result')
            size = struct.unpack_from('<I', payload, 8)[0]
            if len(payload) != 12 + size:
                raise PreviewError('Invalid export result')
            self.finish(payload[12:].decode('utf-8') or None)
            return
        with self.lock:
            if self.done.is_set():
                return  # A cancelled transfer may still have bounded in-flight bytes.
            if kind == 105:
                if len(payload) < 20:
                    raise PreviewError('Truncated export header')
                size = struct.unpack_from('<I', payload, 8)[0]
                if self.total is not None or len(payload) != 20 + size:
                    raise PreviewError('Invalid export header')
                self.total = struct.unpack_from('<Q', payload, 12 + size)[0]
                if self.total > MAX_FILE:
                    raise PreviewError('Export exceeds 256 MiB')
            elif kind == 106:
                if len(payload) < 20 or self.total is None:
                    raise PreviewError('Export data before header')
                position, size = struct.unpack_from('<QI', payload, 8)
                if (position != self.position or size > 65536 or len(payload) != 20 + size
                        or size > self.total - self.position):
                    raise PreviewError('Invalid export position or size')
                self.file.write(payload[20:])
                self.position += size


class Worker:
    def __init__(self, binary, simulation=False, files=None):
        self.id = secrets.token_urlsafe(24)
        self.touched = time.monotonic()
        self.output = OutputQueue()
        self.write_lock, self.event_lock = threading.Lock(), threading.Lock()
        self.state = threading.Condition()
        self.snapshot, self.export, self.active_service = None, None, None
        self.closed, self.cancelled = threading.Event(), threading.Event()
        self.file_scope = HostFiles(files) if files else None
        read_fd, self.file_fd = os.pipe()
        try:
            self.process = subprocess.Popen(
                [str(binary), '--file-events-fd', str(read_fd)] + (['--simulation'] if simulation else []),
                stdin=subprocess.PIPE, stdout=subprocess.PIPE, pass_fds=(read_fd,),
                start_new_session=True, bufsize=0)
        except BaseException:
            os.close(self.file_fd)
            if self.file_scope:
                self.file_scope.close()
            raise
        finally:
            os.close(read_fd)
        os.set_blocking(self.process.stdin.fileno(), False)
        os.set_blocking(self.file_fd, False)
        self.reader = threading.Thread(target=self.read_output, daemon=True)
        self.reader.start()

    def read_exact(self, size):
        parts = bytearray()
        while len(parts) < size:
            block = self.process.stdout.read(size - len(parts))
            if not block:
                raise PreviewError('Native worker exited', 410)
            parts.extend(block)
        return bytes(parts)

    def read_output(self):
        try:
            while True:
                header = self.read_exact(12)
                magic, kind, size = struct.unpack('<4sII', header)
                if magic != b'DPW1' or size > MAX_FRAME:
                    raise PreviewError('Invalid worker frame')
                payload = self.read_exact(size)
                if self.closed.is_set():
                    continue
                if kind == 101:
                    with self.state:
                        self.snapshot = json.loads(payload)
                        self.state.notify_all()
                if kind in (105, 106, 107):
                    with self.state:
                        export = self.export
                    if export is None:
                        raise PreviewError('File output has no authorized host destination')
                    export.accept(kind, payload)
                    if kind == 107:
                        with self.state:
                            if self.export is export:
                                self.export = None
                            self.state.notify_all()
                else:
                    self.output.put(header + payload)
        except (OSError, ValueError, PreviewError) as error:
            self.output.close(str(error))
            with self.state:
                if self.export:
                    self.export.finish(str(error))
                self.state.notify_all()
            # Closing stdin lets C++ remove its private workspace normally.
            with self.write_lock:
                if not self.process.stdin.closed:
                    self.process.stdin.close()

    def write(self, data, trusted=False):
        with self.write_lock:
            if self.closed.is_set() or self.process.poll() is not None or self.process.stdin.closed:
                raise PreviewError('Worker stopped', 410)
            fd = self.file_fd if trusted else self.process.stdin.fileno()
            view, deadline = memoryview(data), time.monotonic() + 3
            while view:
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([], [fd], [], remaining)[1]:
                    raise PreviewError('Worker input pipe stalled')
                try:
                    view = view[os.write(fd, view):]
                except BlockingIOError:
                    pass

    def input(self, data):
        blocks = frames(data)
        if any(kind not in (1, 2, 3, 4, 5, 6, 7, 8, 15) for kind, _ in blocks):
            raise PreviewError('Frame exceeds presentation/audio authority')
        for kind, block in blocks:
            if kind == 2:
                with self.event_lock:
                    self.write(block)
            else:
                self.write(block)

    def check_service(self, event, kind):
        with self.state:
            snapshot = self.snapshot or {}
            service = snapshot.get('service') or {}
            if (event.get('version') != 1 or event.get('kind') != 13
                    or str(event.get('target')) != str(service.get('id'))
                    or str(event.get('generation')) != str(snapshot.get('generation'))
                    or kind != service.get('kind')
                    or int(event['sequence']) <= int(snapshot.get('ack', 0))):
                raise PreviewError('Host service is stale or unavailable')
            return service

    def wait_ack(self, sequence):
        with self.state:
            ok = self.state.wait_for(lambda: self.closed.is_set() or self.cancelled.is_set()
                                    or self.output.error
                                    or int((self.snapshot or {}).get('ack', 0)) >= sequence, timeout=5)
            if not ok or self.closed.is_set() or self.output.error:
                raise PreviewError('Worker did not acknowledge the service')
            if self.cancelled.is_set():
                raise PreviewError('File choice cancelled')

    def cancel_service(self, target):
        with self.state:
            if self.active_service == str(target):
                self.cancelled.set()
                if self.export:
                    self.export.finish('File choice cancelled')
                self.state.notify_all()

    def service(self, request):
        event, kind = request['event'], request['kind']
        with self.event_lock:
            self.check_service(event, kind)
            if kind not in ('clipboard', 'open_file', 'save_file'):
                raise PreviewError('This preview supports individual files, not folder access')
            if kind != 'clipboard' and not self.file_scope:
                raise PreviewError('Restart the preview with --files DIR to enable host file choices')
            with self.state:
                if self.export is not None:
                    raise PreviewError('Previous file export is still finishing')
                self.active_service = str(event['target'])
                self.cancelled.clear()
            importing = False
            try:
                if kind == 'clipboard':
                    self.write(frame(2, event_bytes(event, '')), trusted=True)
                elif kind == 'open_file':
                    source, name, before = self.file_scope.read(request['path'])
                    with source:
                        self.write(frame(10, event_bytes(event, name) + struct.pack('<Q', before.st_size)), True)
                        importing = True
                        position = 0
                        while position < before.st_size:
                            if self.cancelled.is_set() or self.closed.is_set():
                                raise PreviewError('File import cancelled')
                            block = source.read(min(65536, before.st_size - position))
                            if not block:
                                raise PreviewError('Selected file was truncated')
                            self.write(frame(11, struct.pack('<QQI', int(event['target']), position,
                                                            len(block)) + block), True)
                            position += len(block)
                        after = os.fstat(source.fileno())
                        if (before.st_size, before.st_mtime_ns, before.st_ctime_ns) != (after.st_size, after.st_mtime_ns, after.st_ctime_ns):
                            raise PreviewError('Selected file changed during import')
                        if self.cancelled.is_set():
                            raise PreviewError('File import cancelled')
                        self.write(frame(12, struct.pack('<Q', int(event['target']))), True)
                        importing = False
                else:
                    export = self.file_scope.save(request['path'], event['target'])
                    with self.state:
                        self.export = export
                    try:
                        self.write(frame(13, event_bytes(event, export.name)), True)
                        deadline = time.monotonic() + 60
                        while not export.done.wait(.1):
                            if self.closed.is_set() or self.cancelled.is_set() or time.monotonic() > deadline:
                                raise PreviewError('File save cancelled or timed out')
                        if export.error:
                            raise PreviewError(export.error)
                    except BaseException:
                        export.finish('File save did not complete')
                        raise
                self.wait_ack(int(event['sequence']))
            except BaseException:
                if importing and not self.closed.is_set():
                    # A renderer error alone does not clear the native upload.
                    self.write(frame(2, event_bytes(event, '', cancelled=True)), True)
                raise
            finally:
                with self.state:
                    self.active_service = None

    def close(self):
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
            if self.file_fd is not None:
                os.close(self.file_fd)
                self.file_fd = None
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.reader.join(timeout=2)
        self.process.stdout.close()
        with self.event_lock:
            if self.file_scope:
                self.file_scope.close()
                self.file_scope = None


class PreviewServer(ThreadingHTTPServer):
    daemon_threads = False
    allow_reuse_address = False

    def __init__(self, options, port=0):
        self.options, self.worker = options, None
        self.owner_lock = threading.Lock()
        self.stopping = threading.Event()
        self.slots = threading.BoundedSemaphore(16)
        self.token = secrets.token_urlsafe(32)
        super().__init__(('127.0.0.1', port), Handler)
        self.host = f'127.0.0.1:{self.server_port}'
        self.origin = 'http://' + self.host

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, address)
        except BaseException:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()

    def start_worker(self):
        with self.owner_lock:
            if self.stopping.is_set():
                raise PreviewError('Preview is stopping', 410)
            if self.worker is not None:
                raise PreviewError('Another tab owns this worker. Stop it or close that tab first.', 409)
            worker = Worker(self.options.worker, self.options.simulation, self.options.files)
            self.worker = worker
            return worker

    def owned(self, session):
        with self.owner_lock:
            if self.worker is None or not secrets.compare_digest(self.worker.id, session):
                raise PreviewError('Worker session ended. Reload to start again.', 410)
            self.worker.touched = time.monotonic()
            return self.worker

    def stop_worker(self, session):
        with self.owner_lock:
            if self.worker is None or not secrets.compare_digest(self.worker.id, session):
                return
            self.worker.close()
            self.worker = None

    def maintain(self):
        while not self.stopping.wait(.5):
            with self.owner_lock:
                worker = self.worker
                expired = worker and (time.monotonic() - worker.touched > LEASE or worker.output.error)
                if expired:
                    worker.close()
                    self.worker = None


class Handler(BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def setup(self):
        super().setup()
        self.connection.settimeout(5)
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def log_message(self, *args):
        pass  # Do not log access-token URLs or selected filenames.

    def reply(self, value=b'', mime='text/plain; charset=utf-8', status=200):
        if isinstance(value, str):
            value = value.encode('utf-8')
        self.send_response(status)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(value)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Referrer-Policy', 'no-referrer')
        self.send_header('X-Frame-Options', 'DENY')
        self.end_headers()
        self.wfile.write(value)

    def local(self):
        return (self.headers.get('Host') == self.server.host
                and self.headers.get('Origin', self.server.origin) == self.server.origin
                and self.headers.get('Sec-Fetch-Site') != 'cross-site')

    def authenticated(self, token=None):
        return self.local() and secrets.compare_digest(
            self.server.token, token if isinstance(token, str) else self.headers.get('X-Preview-Token', ''))

    def body(self, maximum):
        if self.headers.get('Transfer-Encoding'):
            raise PreviewError('Transfer encoding is not accepted')
        lengths = self.headers.get_all('Content-Length', [])
        if len(lengths) != 1:
            raise PreviewError('One content length is required')
        size = int(lengths[0])
        if not 0 <= size <= maximum:
            raise PreviewError('Request exceeds its bound', 413)
        data = self.rfile.read(size)
        if len(data) != size:
            raise PreviewError('Truncated request')
        return data

    def do_GET(self):
        self.dispatch('GET')

    def do_POST(self):
        self.dispatch('POST')

    def dispatch(self, method):
        try:
            if not self.local():
                raise PreviewError('Invalid origin or host', 403)
            options = self.server.options
            if method == 'GET':
                if options.mode == 'wasm' and self.path in ('/', '/datapump-wasm.html'):
                    return self.reply(options.page.read_bytes(), 'text/html; charset=utf-8')
                if options.mode == 'worker':
                    if self.path == '/':
                        return self.reply((ROOT/'tools/preview.html').read_bytes(), 'text/html; charset=utf-8')
                    if self.path == '/tools/preview.mjs':
                        return self.reply((ROOT/'tools/preview.mjs').read_bytes(), 'text/javascript')
                    if self.path.startswith('/web/') and self.path[5:] in WEB_ASSETS:
                        path = ROOT/'web'/self.path[5:]
                        return self.reply(path.read_bytes(), 'text/css' if path.suffix == '.css' else 'text/javascript')
            if options.mode != 'worker':
                raise PreviewError('Not found', 404)
            if method == 'POST' and self.path == '/close':
                data = json.loads(self.body(2048))
                if not self.authenticated(data.get('token')):
                    raise PreviewError('Unauthorized', 403)
                self.server.stop_worker(str(data.get('session', '')))
                return self.reply()
            if not self.authenticated():
                raise PreviewError('Unauthorized', 403)
            if method == 'POST' and self.path == '/start':
                if self.body(0):
                    raise PreviewError('Unexpected start body')
                worker = self.server.start_worker()
                return self.reply(json.dumps({'session': worker.id, 'files': str(options.files) if options.files else None,
                                               'simulation': options.simulation}), 'application/json')
            worker = self.server.owned(self.headers.get('X-Preview-Session', ''))
            if method == 'GET' and self.path == '/output':
                return self.reply(worker.output.get(), 'application/octet-stream')
            if method != 'POST':
                raise PreviewError('Not found', 404)
            if self.path == '/input':
                worker.input(self.body(MAX_FRAME + 12))
            elif self.path == '/service':
                worker.service(json.loads(self.body(16384)))
            elif self.path == '/cancel':
                worker.cancel_service(json.loads(self.body(2048))['target'])
            elif self.path in ('/stop', '/heartbeat'):
                self.body(0)
                if self.path == '/stop':
                    self.server.stop_worker(worker.id)
            else:
                raise PreviewError('Not found', 404)
            self.reply()
        except (OSError, ValueError, KeyError, TypeError, OverflowError, struct.error, PreviewError) as error:
            self.close_connection = True
            try:
                self.reply(str(error), status=error.status if isinstance(error, PreviewError) else 400)
            except OSError:
                pass


def arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='mode', required=True)
    for mode in ('worker', 'wasm'):
        command = sub.add_parser(mode, help='preview the ' + mode + ' build')
        command.add_argument('--build-dir', type=Path, help='matching build.sh output directory')
        command.add_argument('--port', type=int, default=0, help='loopback port (default: automatically selected)')
        if mode == 'worker':
            command.add_argument('--simulation', action='store_true', help='start without live audio')
            command.add_argument('--files', type=Path, help='enable explicit host file choices beneath DIR; no overwrites')
    options = parser.parse_args(argv)
    if not 0 <= options.port <= 65535:
        parser.error('--port must be between 0 and 65535')
    directory = (options.build_dir or ROOT/('build/dev-cli-web' if options.mode == 'worker' else 'build/wasm')).resolve()
    options.build_dir = directory
    if options.mode == 'worker':
        if not sys.platform.startswith('linux'):
            parser.error('The native pipe worker is currently Linux-only')
        options.worker = directory/'datapump-worker'
        if not options.worker.is_file() or not os.access(options.worker, os.X_OK):
            parser.error(f'Worker missing: {options.worker}. Build with ./build.sh --cli --web-worker (and matching --build-dir).')
        options.files = options.files.resolve(strict=True) if options.files else None
        if options.files and not options.files.is_dir():
            parser.error('--files must name a directory')
    else:
        options.page = directory/'web/datapump-wasm.html'
        if not options.page.is_file():
            parser.error(f'Wasm page missing: {options.page}. Build with ./build.sh --wasm-sdk PATH (and matching --build-dir).')
    return options


def main(argv=None):
    options = arguments(argv)
    with PreviewServer(options, options.port) as server:
        # handle_request keeps signal handlers free of shutdown's same-thread wait.
        server.timeout = .25
        old_handlers = {}
        for signum in (signal.SIGINT, signal.SIGTERM):
            old_handlers[signum] = signal.signal(signum, lambda *_: server.stopping.set())
        maintenance = threading.Thread(target=server.maintain, daemon=True)
        maintenance.start()
        url = server.origin + ('/#' + server.token if options.mode == 'worker' else '/datapump-wasm.html')
        print('Open ' + url, flush=True)
        print('Ctrl-C stops this local preview.' + (' Host files require --files DIR.' if options.mode == 'worker' and not options.files else ''), flush=True)
        try:
            while not server.stopping.is_set():
                server.handle_request()
        finally:
            server.stopping.set()
            with server.owner_lock:
                worker = server.worker
            if worker:
                server.stop_worker(worker.id)
            maintenance.join()
            for signum, handler in old_handlers.items():
                signal.signal(signum, handler)


if __name__ == '__main__':
    try:
        main()
    except (OSError, PreviewError) as error:
        raise SystemExit(str(error))

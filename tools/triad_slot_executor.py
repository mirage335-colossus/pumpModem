"""Bounded independent public-slot owners; signal decisions stay in the parent.

The serial and process paths call the same preparation/scoring functions. Each
spawned owner has a separate native pipe and public basis address. This module
imports numerical libraries only after the child numerical-thread limit is set.
"""
from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass
import json
import math
import multiprocessing
from multiprocessing.connection import wait as wait_connections
import os
from pathlib import Path
import signal
import time

_RESULT_BYTES = 4 * 1024**2
_HEADER_BYTES = 1024**2
_COMPACT_RECEIPT_BYTES = 16 * 1024
_SPAWN_AUXILIARY_ALLOWANCE_BYTES = 32 * 1024**2
_SLOT_KEYS = ('schema', 'status', 'reason', 'slot_classification', 'decision_bit',
    'candidate_bit', 'bit_scores', 'operational', 'sample_rate_hz',
    'represented_baseband_seconds', 'ratio_processing_seconds_per_baseband_second',
    'procedure_id', 'coverage', 'stages', 'workspace', 'quiet')
_WORKSPACE_FAILURES = frozenset((
    'complete_helper_transient_memory_limit',
    'complete_input_filter_working_memory_limit',
    'complete_tiled_frequency_memory_limit',
    'observation_fft_memory_limit',
    'correlation_working_memory_limit',
))


def _positive(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(name + ' must be a positive integer')
    return value


def _shared_input_extent(samples):
    import sys
    samples = _positive(samples, 'slot samples')
    if samples > sys.maxsize // 17:
        raise ValueError('slot input exceeds the local address space')
    return 17 * samples


def _shared_input_layout(samples):
    size = _shared_input_extent(samples)
    page = os.sysconf('SC_PAGE_SIZE')
    pages = (size+page-1)//page
    # The file backing is charged once. Keep explicit bounds for mapping/FD
    # metadata and both fill/reader page tables, including assembly overlap.
    tables = ((8*pages+page-1)//page)*page
    return size, pages*page, 64*1024+2*tables


def shared_input_storage_bytes(samples):
    _, backing, overhead = _shared_input_layout(samples)
    return backing+overhead


def shared_input_reservation_bytes(samples):
    return shared_input_storage_bytes(samples)+_RESULT_BYTES


def _shared_seals():
    import fcntl
    return (fcntl, fcntl.F_SEAL_WRITE | fcntl.F_SEAL_GROW |
            fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_SEAL)


def clear_slot_exception_frames(error):
    """Release input exports retained by completed exception traceback frames."""
    import traceback
    pending, seen = [error], set()
    while pending:
        retained = pending.pop()
        if id(retained) in seen:
            continue
        seen.add(id(retained))
        pending.extend(item for item in (retained.__cause__, retained.__context__)
                       if item is not None)
        traceback.clear_frames(retained.__traceback__)
        retained.__traceback__ = retained.__cause__ = retained.__context__ = None


def _receive_shared_fd(connection):
    """Receive exactly stdlib send_handle's one Linux SCM_RIGHTS descriptor."""
    import array
    import socket
    received = []
    try:
        integers = array.array('i')
        with socket.fromfd(connection.fileno(), socket.AF_UNIX, socket.SOCK_STREAM) as channel:
            data, ancillary, flags, _ = channel.recvmsg(
                1, socket.CMSG_SPACE(integers.itemsize), socket.MSG_CMSG_CLOEXEC)
            valid_ancillary = len(ancillary) == 1
            for level, kind, content in ancillary:
                if level == socket.SOL_SOCKET and kind == socket.SCM_RIGHTS:
                    # Own every complete descriptor before validating the token
                    # or truncation, or closing the duplicate socket. Padding
                    # in CMSG_SPACE can fit more than one int.
                    count = len(content)//integers.itemsize
                    descriptors = array.array('i')
                    descriptors.frombytes(content[:count*integers.itemsize])
                    received.extend(descriptors)
                    valid_ancillary &= len(content) == integers.itemsize
                else:
                    valid_ancillary = False
        if (data != b'\x01' or not valid_ancillary or len(received) != 1 or
                flags & (socket.MSG_CTRUNC | socket.MSG_TRUNC)):
            # Linux closes descriptors omitted by ancillary truncation; every
            # descriptor actually delivered above remains our responsibility.
            raise ValueError('shared slot handoff must contain exactly one descriptor')
        fd = received.pop()
        return fd
    finally:
        for fd in received:
            os.close(fd)


def select_slot_input_transport():
    """Choose before admission; a later transfer failure never changes mode."""
    fd = received = None
    mapping = None
    connections = ()
    try:
        import mmap
        from multiprocessing.reduction import send_handle
        fcntl, seals = _shared_seals()
        fd = os.memfd_create('triad-slot-capability', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
        os.ftruncate(fd, 1)
        mapping = mmap.mmap(fd, 1, access=mmap.ACCESS_WRITE)
        mapping[0] = 0
        mapping.close()
        mapping = None
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, seals)
        connections = multiprocessing.Pipe()
        send_handle(connections[0], fd, os.getpid())
        received = _receive_shared_fd(connections[1])
        mapping = mmap.mmap(received, 1, access=mmap.ACCESS_READ)
        if mapping[0] != 0 or fcntl.fcntl(received, fcntl.F_GET_SEALS) & seals != seals:
            raise OSError('sealed input capability did not preserve its descriptor')
        return dict(method='sealed_memfd', reason=None)
    except (ImportError, AttributeError, OSError, ValueError, NotImplementedError) as error:
        return dict(method='pipe_copy', reason='sealed_memfd_unavailable:'+type(error).__name__)
    finally:
        if mapping is not None:
            mapping.close()
        for connection in connections:
            connection.close()
        for handle in (received, fd):
            if handle is not None:
                os.close(handle)


class _SharedSlotView:
    """One read-only mapping; close must precede the release acknowledgement."""
    def __init__(self, fd, descriptor, samples, *, own_fd):
        self._fd, self._own_fd, self._mapping = fd, own_fd, None
        self.values = self.valid = None
        try:
            import mmap
            import numpy as np
            size = _shared_input_extent(samples)
            if (not isinstance(descriptor, dict) or descriptor.get('samples') != samples or
                    type(descriptor.get('samples')) is not int or
                    type(descriptor.get('bytes')) is not int or descriptor['bytes'] != size or
                    descriptor.get('dtype') != 'complex128+uint8' or
                    any(type(descriptor.get(key)) is not int or descriptor[key] < 0
                        for key in ('device', 'inode'))):
                raise ValueError('shared slot descriptor differs from its bounded public input')
            stat = os.fstat(fd)
            fcntl, seals = _shared_seals()
            if (stat.st_size != size or stat.st_dev != descriptor['device'] or
                    stat.st_ino != descriptor['inode'] or
                    fcntl.fcntl(fd, fcntl.F_GET_SEALS) & seals != seals):
                raise ValueError('shared slot extent, identity or immutable seals differ')
            self._mapping = mmap.mmap(fd, size, access=mmap.ACCESS_READ)
            self.values = np.frombuffer(self._mapping, np.complex128, count=samples)
            self.valid = np.frombuffer(self._mapping, np.uint8, count=samples, offset=16*samples)
        except BaseException:
            self.close()
            raise

    def close(self):
        self.values = self.valid = None
        if self._mapping is not None:
            # BufferError retains the mapping/fd. Callers must fail closed,
            # never acknowledge release while any exported view remains.
            self._mapping.close()
            self._mapping = None
        if self._own_fd and self._fd is not None:
            os.close(self._fd)
            self._fd = None


class SharedSlotInput:
    """Parent-owned immutable backing retained through completion or retry."""
    def __init__(self, fd, samples):
        self.fd, self.samples = fd, samples
        self.input_bytes = _shared_input_extent(samples)
        self.storage_bytes = shared_input_storage_bytes(samples)
        stat = os.fstat(fd)
        self.descriptor = dict(samples=samples, bytes=self.input_bytes,
            dtype='complex128+uint8', device=stat.st_dev, inode=stat.st_ino)

    @classmethod
    def from_arrays(cls, values, valid):
        import mmap
        import numpy as np
        values, valid = np.asarray(values), np.asarray(valid)
        if (values.ndim != 1 or valid.shape != values.shape or not values.size or
                values.dtype != np.complex128 or valid.dtype != np.uint8):
            raise ValueError('shared slot input requires matching complex128/uint8 vectors')
        samples = int(values.size)
        size = _shared_input_extent(samples)
        # Establish a finalizable owner before creating the descriptor, so an
        # interrupted fill or caller handoff cannot orphan a raw integer fd.
        owned = cls.__new__(cls)
        owned.fd = None
        mapping = target_values = target_valid = None
        try:
            owned.fd = os.memfd_create('triad-slot-input', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
            cls.__init__(owned, owned.fd, samples)
            os.ftruncate(owned.fd, size)
            mapping = mmap.mmap(owned.fd, size, access=mmap.ACCESS_WRITE)
            target_values = np.frombuffer(mapping, np.complex128, count=samples)
            target_valid = np.frombuffer(mapping, np.uint8, count=samples, offset=16*samples)
            target_values[:] = values
            target_valid[:] = valid
            target_values = target_valid = None
            mapping.close()
            mapping = None
            fcntl, seals = _shared_seals()
            fcntl.fcntl(owned.fd, fcntl.F_ADD_SEALS, seals)
            return owned
        except BaseException:
            target_values = target_valid = None
            if mapping is not None:
                mapping.close()
            owned.close()
            raise

    def open_readonly(self):
        if self.fd is None:
            raise RuntimeError('shared slot input has already been released')
        return _SharedSlotView(self.fd, self.descriptor, self.samples, own_fd=False)

    def close(self):
        fd = getattr(self, 'fd', None)
        if fd is not None:
            self.fd = None
            os.close(fd)

    def __del__(self):
        try:
            self.close()
        except BaseException:
            # Finalization is a last-resort fd guard, never a release receipt.
            pass


def _receive_shared_input(connection, descriptor, samples):
    fd = _receive_shared_fd(connection)
    return _SharedSlotView(fd, descriptor, samples, own_fd=True)


def _rss(pid):
    try:
        for line in Path('/proc', str(pid), 'status').read_text().splitlines():
            if line.startswith('VmRSS:'):
                return int(line.split()[1]) * 1024
    except (OSError, ValueError):
        pass
    return None



def _bootstrap_rss(pid):
    """Linux post-exec current/peak RSS from one process-mm snapshot.

    VmHWM covers this spawned interpreter's imports, unlike a parent heap
    estimate or process-accounting peaks that may cross the exec boundary.
    The backend already requires Linux /proc RSS admission.
    """
    try:
        fields = {line.split(':', 1)[0]: line.split(':', 1)[1].split()
                  for line in Path('/proc', str(pid), 'status').read_text().splitlines()
                  if line.startswith(('VmRSS:', 'VmHWM:'))}
        if any(fields[key][1] != 'kB' for key in ('VmRSS', 'VmHWM')):
            return None
        resident, peak = (int(fields[key][0])*1024 for key in ('VmRSS', 'VmHWM'))
        if resident < 0 or peak < resident:
            return None
        return resident, peak
    except (OSError, KeyError, ValueError, IndexError):
        return None


def spawn_auxiliary_reservation_bytes():
    """Reserve the shared spawn tracker without starting or stopping it.

    The parent counts this once before its first spawn and retains the largest
    reservation through backend shutdown and serial fallback. CPython's small
    resource tracker can outlive either slot owner, so it is not part of an
    individual owner's releasable workspace.
    """
    from multiprocessing import resource_tracker
    tracker = getattr(resource_tracker, '_resource_tracker', None)
    pid = getattr(tracker, '_pid', None)
    resident = _rss(pid) if type(pid) is int and pid > 0 else None
    return max(_SPAWN_AUXILIARY_ALLOWANCE_BYTES, resident or 0)


def _process_identity(pid):
    try:
        fields = Path('/proc', str(pid), 'stat').read_text().rsplit(') ', 1)[1].split()
        return int(fields[19]), fields[0]
    except (OSError, ValueError, IndexError):
        return None


def _native_alive(owner):
    identity = _process_identity(owner.get('native_pid'))
    return (identity is not None and identity[0] == owner.get('native_start_ticks')
        and identity[1] not in ('Z', 'X'))


def _source_spec(command):
    if (not isinstance(command, (tuple, list)) or len(command) < 4 or
            any(not isinstance(value, str) or '\0' in value for value in command) or
            list(command[-3:-1]) != ['--stream-server', '--stream-kind'] or
            command[-1] not in ('channel', 'emitted')):
        raise ValueError('slot owners require a trusted NativeStream command')
    return str(command[0]), tuple(command[1:-3]), str(command[-1])


def _stop_native(source, timeout=1.):
    """Do not wait for NativeStream.close's potentially blocking header read."""
    process = getattr(source, 'process', None)
    if process is None:
        return
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=timeout)
        except Exception:
            process.kill()
            process.wait(timeout=timeout)
    for stream in (process.stdin, process.stdout, process.stderr):
        if stream is not None:
            stream.close()
    source.process = None


@contextmanager
def initialized_slot_source(source_spec, *, workers):
    """Create the canonical independent native owner with one numerical thread."""
    from triad_cpu import configure_numerical_threads, resolve_cpu_budget
    configure_numerical_threads(resolve_cpu_budget(jobs=1,
        workers=_positive(workers, 'slot workers'), threads=1))
    # Load the canonical receiver closure before the bootstrap handshake.
    # These imports create no templates or observations; first-job imports must
    # not sit outside the admitted interpreter bootstrap envelope.
    import triad_case_replay  # noqa: F401
    import triad_receiver  # noqa: F401
    from triad_continuous import NativeStream
    executable, arguments, kind = _source_spec(source_spec)
    source = NativeStream(executable, arguments, kind=kind)
    try:
        yield source
    finally:
        _stop_native(source)


@dataclass
class PreparedSlot:
    source: object
    receiver: object
    bank: dict
    preparation_seconds: float
    preparation_peak_bytes: int
    service_start_monotonic: float
    workspace_receipts_complete: bool


@contextmanager
def prepared_public_slot(source, manifest, *, public_address, source_identity,
                         values, valid, workers, memory_limit_bytes,
                         bank_builder=None, receiver_factory=None,
                         receiver_policy=None):
    """Own the single canonical bank/receiver preparation and its lifetime."""
    import numpy as np
    from triad_case_replay import build_public_screen
    from triad_receiver import CompleteBitReceiver
    workers = _positive(workers, 'slot workers')
    limit = _positive(memory_limit_bytes, 'slot memory limit')
    started = time.perf_counter()
    values, valid = np.asarray(values), np.asarray(valid)
    count = manifest.get('calendar', {}).get('capture_samples')
    if (type(count) is not int or count < 1 or values.shape != (count,) or
            valid.shape != (count,) or values.dtype != np.complex128 or
            valid.dtype != np.uint8):
        raise ValueError('slot input must match its complete public complex128/uint8 window')
    input_bytes = values.nbytes + valid.nbytes
    if input_bytes >= limit:
        raise MemoryError('public slot input leaves no bank workspace')
    bank_builder = build_public_screen if bank_builder is None else bank_builder
    receiver_factory = CompleteBitReceiver if receiver_factory is None else receiver_factory
    bank = bank_builder(source, manifest, public_address=public_address,
        source_identity=source_identity, memory_limit_bytes=limit-input_bytes,
        threads=workers)
    peak = bank.get('work_receipt', {}).get('preparation_peak_estimate_bytes')
    workspace_complete = (not isinstance(peak, bool) and isinstance(peak, (int, float))
        and math.isfinite(peak) and peak >= 0)
    peak = input_bytes + (math.ceil(peak) if workspace_complete else 0)
    if peak > limit:
        raise MemoryError('public bank preparation exceeds its slot workspace')
    receiver = None
    try:
        receiver = receiver_factory(bank['manifest'], bank['screen'], source,
            workers=workers, memory_limit_bytes=limit, policy=receiver_policy)
        yield PreparedSlot(source, receiver, bank, time.perf_counter()-started,
            peak, started, workspace_complete)
    finally:
        if receiver is not None:
            receiver.close()


def score_public_slot(source, manifest, *, public_address, source_identity,
                      values, valid, workers, memory_limit_bytes,
                      bank_builder=None, receiver_factory=None,
                      receiver_policy=None):
    """Score all work for one public window; never commit message state here."""
    with prepared_public_slot(source, manifest, public_address=public_address,
            source_identity=source_identity, values=values, valid=valid,
            workers=workers, memory_limit_bytes=memory_limit_bytes,
            bank_builder=bank_builder, receiver_factory=receiver_factory,
            receiver_policy=receiver_policy) as prepared:
        row = prepared.receiver.receive(values, valid)
        if row.get('status') == 'unresolved' and row.get('reason') in _WORKSPACE_FAILURES:
            raise MemoryError('explicit receiver workspace admission: ' + row['reason'])
        exposure = len(values) / manifest['sample_rate_hz']
        row.setdefault('stages', {})['recurring_bank_preparation'] = dict(
            included_in_service=True, represented_baseband_seconds=exposure,
            ratio_processing_seconds_per_baseband_second=prepared.preparation_seconds/exposure,
            public_address=dict(public_address), template_identity=prepared.bank.get('template_identity'))
        workspace = row.get('workspace', {})
        row_peak = workspace.get('estimated_peak_bytes')
        declared_limit = workspace.get('memory_limit_bytes')
        valid_peak = (not isinstance(row_peak, bool) and isinstance(row_peak, (int, float))
            and math.isfinite(row_peak) and row_peak >= 0)
        valid_limit = type(declared_limit) is int and declared_limit > 0
        workspace_complete = prepared.workspace_receipts_complete and valid_peak and valid_limit
        if (valid_peak and row_peak > memory_limit_bytes or
                valid_limit and declared_limit > memory_limit_bytes or
                valid_peak and valid_limit and row_peak > declared_limit):
            raise MemoryError('complete slot exceeded its declared workspace')
        peak = max(prepared.preparation_peak_bytes, math.ceil(row_peak) if valid_peak else 0)
        if peak > memory_limit_bytes:
            raise MemoryError('complete slot exceeds its admitted workspace')
        result = dict(row=row, template_identity=prepared.bank.get('template_identity'),
            bank_work_receipt=prepared.bank.get('work_receipt', {}),
            preparation_seconds=prepared.preparation_seconds, workspace_peak_bytes=peak,
            workspace_receipts_complete=workspace_complete,
            service_start_monotonic=prepared.service_start_monotonic)
    # Receiver teardown is recurring service in both serial and child paths.
    stopped = time.perf_counter()
    result.update(service_seconds=stopped-result['service_start_monotonic'],
        service_stop_monotonic=stopped)
    row['ratio_processing_seconds_per_baseband_second'] = result['service_seconds']/exposure
    return result


def _affinity(cpus, native_pid):
    if not cpus or not hasattr(os, 'sched_setaffinity'):
        return
    for pid in (os.getpid(), native_pid):
        tasks = Path('/proc', str(pid), 'task')
        for task in tasks.iterdir():
            try:
                os.sched_setaffinity(int(task.name), set(cpus))
            except ProcessLookupError:
                pass


def _send(connection, payload):
    data = json.dumps(payload, allow_nan=False, separators=(',', ':')).encode()
    if len(data) > _RESULT_BYTES:
        raise ValueError('slot result exceeds its bounded IPC receipt')
    connection.send_bytes(data)


def _send_completed(connection, job_id, started, payload):
    before = time.perf_counter()
    body = json.dumps(payload, allow_nan=False, separators=(',', ':')).encode()
    if len(body) > _RESULT_BYTES:
        raise ValueError('slot result exceeds its bounded IPC receipt')
    stopped = time.perf_counter()
    _send(connection, dict(kind='completed', job_id=job_id,
        service_start_monotonic=started, service_stop_monotonic=stopped,
        result_encode_seconds=stopped-before, payload_bytes=len(body)))
    connection.send_bytes(body)


def _transport_result(result):
    """Bound decoded result ownership without carrying diagnostic state arrays."""
    row = {key: result['row'][key] for key in _SLOT_KEYS if key in result['row']}
    for receipt in (row, result['bank_work_receipt']):
        if len(json.dumps(receipt, allow_nan=False).encode()) > _COMPACT_RECEIPT_BYTES:
            raise ValueError('slot diagnostic exceeds its canonical bounded receipt')
    return dict(result, row=row)


def _slot_process(connection, source_spec, total_workers):
    """Single pipe owner, one immutable job at a time; no shared source address."""
    source = None
    try:
        if hasattr(os, 'setsid'):
            os.setsid()
        with initialized_slot_source(source_spec, workers=total_workers) as source:
            descriptor = source.descriptor
            process_bootstrap = _bootstrap_rss(os.getpid())
            native_bootstrap = _bootstrap_rss(source.process.pid)
            _send(connection, dict(kind='ready', pid=os.getpid(), native_pid=source.process.pid,
                process_rss_bytes=process_bootstrap[0] if process_bootstrap else None,
                native_rss_bytes=native_bootstrap[0] if native_bootstrap else None,
                process_bootstrap_peak_rss_bytes=process_bootstrap[1] if process_bootstrap else None,
                native_bootstrap_peak_rss_bytes=native_bootstrap[1] if native_bootstrap else None,
                native_workspace_bytes=max(int(descriptor.get('fixed_workspace_bytes', 0)),
                    int(descriptor.get('workspace_budget_bytes', 0)))))
            while True:
                header = json.loads(connection.recv_bytes(_HEADER_BYTES))
                if header.get('kind') == 'close':
                    return
                if header.get('kind') != 'slot':
                    raise ValueError('unknown slot process command')
                job_id = header['job_id']
                started = time.perf_counter()
                import numpy as np
                count = _positive(header['samples'], 'slot samples')
                limit = _positive(header['memory_limit_bytes'], 'slot memory limit')
                declared_samples = header['manifest'].get('calendar', {}).get('capture_samples')
                if (type(declared_samples) is not int or declared_samples != count or
                        _shared_input_extent(count) >= limit):
                    raise ValueError('slot transport changed its admitted public extent')
                transport = header.get('input_transport', 'pipe_copy')
                shared = raw = mask = result = None
                # Framing/descriptor failure ends this owner. Continuing could
                # interpret a pending SCM_RIGHTS token as the next JSON header.
                if transport == 'sealed_memfd':
                    shared = _receive_shared_input(connection, header.get('shared_input'), count)
                elif transport == 'pipe_copy':
                    raw = connection.recv_bytes(16*count)
                    mask = connection.recv_bytes(count)
                    if len(raw) != 16*count or len(mask) != count:
                        raise ValueError('slot IPC input changed its public window extent')
                else:
                    raise ValueError('unknown slot input transport')
                try:
                    _affinity(header['cpu_ids'], source.process.pid)
                    result = score_public_slot(source, header['manifest'],
                        public_address=header['public_address'], source_identity=header['source_identity'],
                        values=shared.values if shared is not None else np.frombuffer(raw, np.complex128),
                        valid=shared.valid if shared is not None else np.frombuffer(mask, np.uint8),
                        workers=header['workers'], memory_limit_bytes=header['memory_limit_bytes'],
                        receiver_policy=header['receiver_policy'])
                    result = _transport_result(result)
                    payload = dict(result=result)
                except Exception as error:
                    payload = dict(
                        error_type=type(error).__name__, error_phase='slot_preparation_or_scoring',
                        error_message=str(error)[:1024])
                    # Preparation/scoring traceback frames can retain ndarray
                    # exports even after their scopes have returned.
                    clear_slot_exception_frames(error)
                finally:
                    result = raw = mask = None
                    if shared is not None:
                        shared.close()
                        shared = None
                # A completion is also an input-release acknowledgement.
                # Cleanup failures leave ownership with the parent until the
                # failed process has been terminated/reaped.
                _send_completed(connection, job_id, started, payload)
                del payload
    except (EOFError, BrokenPipeError):
        pass
    except Exception as error:
        try:
            _send(connection, dict(kind='startup_error', error_type=type(error).__name__,
                error_phase='slot_owner_startup', error_message=str(error)[:1024]))
        except (OSError, ValueError):
            pass
    finally:
        connection.close()


class SlotProcessBackend:
    """Bounded independent owners sharing one immutable software-worker allocation.

    There is no hidden job queue. A lone all-worker job keeps every permit
    until its result is collected; a later job cannot oversubscribe it.
    Input and result ownership transfers are bounded; the parent keeps ordering.
    """
    supports_shared_input = True

    def __init__(self, source_spec, *, max_processes=None, total_workers=None,
                 initial_processes=1, startup_timeout=10., close_timeout=3., input_transport=None):
        from triad_cpu import resolve_receiver_workers, detect_cpu_topology
        _source_spec(source_spec)
        self.total_workers = resolve_receiver_workers(total_workers)
        self.max_processes = _positive(min(resolve_receiver_workers(), self.total_workers)
            if max_processes is None else max_processes, 'slot owner count')
        if self.max_processes > self.total_workers:
            raise ValueError('slot owners exceed the aggregate software worker allocation')
        initial_processes = _positive(initial_processes, 'initial slot owners')
        if initial_processes > self.max_processes:
            raise ValueError('initial slot owners exceed the owner cap')
        if not math.isfinite(startup_timeout) or startup_timeout <= 0 or not math.isfinite(close_timeout) or close_timeout <= 0:
            raise ValueError('slot process timeouts must be finite and positive')
        if not hasattr(os, 'killpg') or not Path('/proc/self/status').is_file():
            raise MemoryError('bounded slot process/RSS admission is unavailable on this platform')
        if input_transport is not None and input_transport not in ('pipe_copy', 'sealed_memfd'):
            raise ValueError('unknown slot input transport')
        selected = (select_slot_input_transport() if input_transport is None else
                    dict(method=input_transport, reason='explicit_transport'))
        self.input_transport, self.input_transport_reason = selected['method'], selected['reason']
        self.close_timeout = float(close_timeout)
        self._owners, self._active, self._closed, self._failed = [], {}, False, False
        self._shutdown_complete = False
        self._bootstrap_process_peak_bytes = self._bootstrap_native_peak_bytes = 0
        topology = detect_cpu_topology()
        if topology.get('status') == 'complete':
            cores = [core['logical_cpu_ids'] for core in topology['physical_cores']]
            self._cpu_ids = [core[i] for i in range(max(map(len, cores)))
                for core in cores if i < len(core)][:self.total_workers]
            if len(self._cpu_ids) < self.total_workers:
                # Explicit caller allocations remain exact, including deliberate
                # overcommit. Exclusive masks cannot represent that allocation.
                self._cpu_ids = []
        else:
            self._cpu_ids = []
        self._source_spec = tuple(source_spec)
        self.startup_timeout = float(startup_timeout)
        self.startup_seconds = 0.
        self.ensure_processes(initial_processes)

    @property
    def owner_count(self):
        return len(self._owners)

    def ensure_processes(self, count):
        count = _positive(count, 'required slot owners')
        if self._closed or self._failed:
            raise RuntimeError('slot process backend is closed or failed')
        if count > self.max_processes:
            raise ValueError('required slot owners exceed the owner cap')
        if count <= len(self._owners):
            return
        started = time.perf_counter()
        context = multiprocessing.get_context('spawn')
        pending = []
        try:
            for _ in range(count-len(self._owners)):
                parent, child = context.Pipe()
                try:
                    process = context.Process(target=_slot_process,
                        args=(child, self._source_spec, self.total_workers), daemon=False)
                except BaseException:
                    parent.close()
                    child.close()
                    raise
                self._owners.append(dict(process=process, connection=parent, ready=None,
                    resident_snapshot=0, native_pid=None))
                pending.append(self._owners[-1])
                try:
                    process.start()
                finally:
                    child.close()
            while pending:
                left = self.startup_timeout-(time.perf_counter()-started)
                if left <= 0:
                    raise TimeoutError('slot owners did not finish their bounded startup')
                ready = wait_connections([owner['connection'] for owner in pending], timeout=left)
                if not ready:
                    raise TimeoutError('slot owners did not finish their bounded startup')
                for owner in list(pending):
                    if owner['connection'] not in ready:
                        continue
                    reply = json.loads(owner['connection'].recv_bytes(_RESULT_BYTES))
                    if reply.get('kind') != 'ready':
                        raise RuntimeError('slot owner startup failed: '+reply.get('error_type', 'protocol'))
                    if reply.get('process_rss_bytes') is None or reply.get('native_rss_bytes') is None:
                        raise MemoryError('slot runtime RSS admission is unavailable on this platform')
                    if (reply.get('pid') != owner['process'].pid or
                            type(reply.get('native_pid')) is not int or
                            any(type(reply.get(key)) is not int or reply[key] < 0
                                for key in ('process_rss_bytes', 'native_rss_bytes', 'native_workspace_bytes',
                                    'process_bootstrap_peak_rss_bytes', 'native_bootstrap_peak_rss_bytes')) or
                            reply.get('process_bootstrap_peak_rss_bytes', -1) < reply['process_rss_bytes'] or
                            reply.get('native_bootstrap_peak_rss_bytes', -1) < reply['native_rss_bytes']):
                        raise RuntimeError('slot owner omitted bounded runtime admission')
                    owner.update(ready=reply, native_pid=reply['native_pid'],
                        resident_snapshot=reply['process_rss_bytes']+max(reply['native_rss_bytes'], reply['native_workspace_bytes']))
                    self._bootstrap_process_peak_bytes = max(self._bootstrap_process_peak_bytes,
                        reply['process_bootstrap_peak_rss_bytes'])
                    self._bootstrap_native_peak_bytes = max(self._bootstrap_native_peak_bytes,
                        reply['native_bootstrap_peak_rss_bytes'], reply['native_workspace_bytes'])
                    native_identity = _process_identity(reply['native_pid'])
                    if native_identity is None:
                        raise RuntimeError('native slot owner exited during startup')
                    owner['native_start_ticks'] = native_identity[0]
                    pending.remove(owner)
        except BaseException:
            self.close()
            raise
        finally:
            self.startup_seconds += time.perf_counter()-started

    def retire_idle(self, count):
        count = _positive(count, 'retained slot owners')
        if self._closed or self._failed or self._active:
            raise RuntimeError('slot owners can retire only at an idle healthy boundary')
        if count >= len(self._owners):
            return
        retiring = self._owners[count:]
        self._release_owners(retiring)
        del self._owners[count:]

    def prospective_owner_bytes(self, python_bytes, native_bytes):
        # Spawned interpreters do not inherit the parent's live input heap.
        # Keep immutable pre-DSP bootstrap peaks even after an owner retires.
        # Current parent and existing-owner RSS remain in their own ledger.
        if self._bootstrap_process_peak_bytes:
            return self._bootstrap_process_peak_bytes+max(native_bytes, self._bootstrap_native_peak_bytes)
        return python_bytes+native_bytes

    @staticmethod
    def input_reservation_bytes(samples):
        return 4*17*_positive(samples, 'slot samples') + _RESULT_BYTES

    @property
    def resident_bytes(self):
        if self._closed:
            return sum((_rss(owner['process'].pid) or 0) + max(_rss(owner['native_pid']) or 0,
                (owner.get('ready') or {}).get('native_workspace_bytes', 0))
                for owner in self._owners if owner['process'].pid is not None and not owner.get('released', False))
        total = 0
        active_owners = {entry['owner'] for entry in self._active.values()}
        for i, owner in enumerate(self._owners):
            if i not in active_owners:
                sizes = (_rss(owner['process'].pid), _rss(owner['native_pid']))
                if all(value is not None for value in sizes):
                    owner['resident_snapshot'] = sizes[0]+max(sizes[1], owner['ready']['native_workspace_bytes'])
            total += owner['resident_snapshot']
        return total

    def resident_bytes_after_retirement(self, count):
        if self._active or not 0 <= count <= len(self._owners):
            raise RuntimeError('retirement estimate requires an idle owner prefix')
        self.resident_bytes
        return sum(owner['resident_snapshot'] for owner in self._owners[:count])

    @property
    def native_workspace_bytes(self):
        return sum(owner['ready']['native_workspace_bytes'] for owner in self._owners
            if owner['ready'] is not None and not owner.get('released', False))

    @property
    def active_workers(self):
        return sum(job['workers'] for job in self._active.values())

    def submit(self, job_id, *, manifest, public_address, source_identity, values=None, valid=None,
               shared_input=None, workers, memory_limit_bytes, receiver_policy=None):
        import numpy as np
        if self._closed or self._failed:
            raise RuntimeError('slot process backend is closed or failed')
        if type(job_id) is not int or job_id < 0 or job_id in self._active:
            raise ValueError('slot job id must be a distinct nonnegative integer')
        workers = _positive(workers, 'slot workers')
        _positive(memory_limit_bytes, 'slot memory limit')
        used = {job['owner'] for job in self._active.values()}
        idle = [i for i in range(len(self._owners)) if i not in used]
        if not idle or self.active_workers+workers > self.total_workers:
            raise RuntimeError('slot job would exceed the shared owner/CPU allocation')
        samples = manifest.get('calendar', {}).get('capture_samples')
        _shared_input_extent(samples)
        if self.input_transport == 'sealed_memfd':
            if (not isinstance(shared_input, SharedSlotInput) or shared_input.fd is None or
                    shared_input.samples != samples or values is not None or valid is not None):
                raise ValueError('sealed slot transport requires one matching owned input')
        else:
            if shared_input is not None:
                raise ValueError('copied slot transport cannot borrow shared input')
            values, valid = np.asarray(values), np.asarray(valid)
            if (values.shape != (samples,) or valid.shape != (samples,) or
                    values.dtype != np.complex128 or valid.dtype != np.uint8):
                raise ValueError('slot IPC input must match its complete public window')
        if samples*17 >= memory_limit_bytes:
            raise MemoryError('slot input exhausts its workspace allocation')
        reserved = {cpu for job in self._active.values() for cpu in job['cpu_ids']}
        cpus = [cpu for cpu in self._cpu_ids if cpu not in reserved][:workers]
        i = idle[0]
        owner = self._owners[i]
        self.resident_bytes
        header = dict(kind='slot', job_id=job_id, manifest=manifest,
            public_address=public_address, source_identity=source_identity, samples=samples,
            workers=workers, cpu_ids=cpus, memory_limit_bytes=memory_limit_bytes,
            receiver_policy=receiver_policy, input_transport=self.input_transport)
        if shared_input is not None:
            header['shared_input'] = shared_input.descriptor
        encoded = json.dumps(header, allow_nan=False, separators=(',', ':')).encode()
        if len(encoded) > _HEADER_BYTES:
            raise ValueError('slot descriptor exceeds bounded IPC header')
        self._active[job_id] = dict(owner=i, workers=workers, cpu_ids=cpus,
            submit_start_monotonic=time.perf_counter(), memory_limit_bytes=memory_limit_bytes,
            input_reservation_bytes=(shared_input_reservation_bytes(samples) if shared_input is not None
                                     else self.input_reservation_bytes(samples)))
        try:
            owner['connection'].send_bytes(encoded)
            if shared_input is not None:
                from multiprocessing.reduction import send_handle
                send_handle(owner['connection'], shared_input.fd, owner['process'].pid)
            else:
                owner['connection'].send_bytes(values.tobytes(order='C'))
                owner['connection'].send_bytes(valid.tobytes(order='C'))
        except BaseException:
            self.close()
            raise
        return job_id

    def poll(self, wait=False):
        if type(wait) is not bool:
            raise ValueError('slot polling wait must be a boolean')
        if self._closed or not self._active:
            return []
        by_connection = {self._owners[job['owner']]['connection']: job_id
            for job_id, job in self._active.items()}
        ready = wait_connections(list(by_connection), timeout=None if wait else 0.)
        results = []
        for connection in ready:
            job_id = by_connection[connection]
            active = self._active[job_id]
            receive_started = time.perf_counter()
            try:
                result = json.loads(connection.recv_bytes(_RESULT_BYTES))
                if result.get('kind') != 'completed' or result.get('job_id') != job_id:
                    raise ValueError('slot owner returned a different job')
                payload_bytes = result.get('payload_bytes')
                if type(payload_bytes) is not int or not 0 < payload_bytes <= _RESULT_BYTES:
                    raise ValueError('slot owner returned an unbounded result')
                body = connection.recv_bytes(_RESULT_BYTES)
                if len(body) != payload_bytes:
                    raise ValueError('slot owner changed its result extent')
                payload = json.loads(body)
                if (not isinstance(payload, dict) or
                        any(key not in ('result', 'error_type', 'error_phase', 'error_message') for key in payload) or
                        ('result' in payload) == ('error_type' in payload)):
                    raise ValueError('slot owner returned an invalid result envelope')
                result.update(payload)
                start, stop = result.get('service_start_monotonic'), result.get('service_stop_monotonic')
                if (not isinstance(start, (int, float)) or not isinstance(stop, (int, float)) or
                        not math.isfinite(start) or not math.isfinite(stop) or stop < start):
                    raise ValueError('slot owner returned invalid service chronology')
            except (EOFError, OSError, ValueError) as error:
                result = dict(job_id=job_id, error_type=type(error).__name__,
                    error_phase='slot_owner_transport', error_message=str(error)[:1024],
                    service_start_monotonic=active['submit_start_monotonic'],
                    service_stop_monotonic=time.perf_counter())
            if 'error_type' in result:
                self._failed = True
            result.update(parent_result_start_monotonic=receive_started,
                submit_start_monotonic=active['submit_start_monotonic'],
                collected_stop_monotonic=time.perf_counter(), workers=active['workers'],
                cpu_ids=active['cpu_ids'], memory_limit_bytes=active['memory_limit_bytes'],
                input_reservation_bytes=active['input_reservation_bytes'])
            results.append(result)
            del self._active[job_id]
        return results

    def close(self):
        if self._closed and getattr(self, '_shutdown_complete', False):
            return
        self._closed = True
        self._release_owners(self._owners)
        self._active.clear()
        self._shutdown_complete = True

    def _release_owners(self, owners):
        end = time.perf_counter()+self.close_timeout
        for owner in owners:
            if owner.get('released', False):
                continue
            # Idle graceful shutdown is optional; never block on a busy pipe.
            if not any(job['owner'] == self._owners.index(owner) for job in self._active.values()):
                try:
                    owner['connection'].send_bytes(b'{"kind":"close"}')
                except (OSError, EOFError):
                    pass
        # Clean every owned group, including a native service whose Python
        # leader already exited. Do this before reaping the leader's PID.
        for owner in owners:
            self._terminate(owner, signal.SIGTERM)
        # Share the grace period and leave time to kill/reap every owner even
        # with a short caller-supplied shutdown deadline.
        term_end = min(end, time.perf_counter()+min(.5, self.close_timeout/3))
        for owner in owners:
            process = owner['process']
            if process.pid is not None:
                process.join(timeout=max(0., term_end-time.perf_counter()))
        for owner in owners:
            self._terminate(owner, signal.SIGKILL)
        for owner in owners:
            process = owner['process']
            if process.pid is not None:
                process.join(timeout=max(0., end-time.perf_counter()))
        for owner in owners:
            owner['connection'].close()
        # SIGKILL delivery is asynchronous even after a dead leader was reaped.
        # Use only the already reserved close deadline for native exit delivery.
        while time.perf_counter() < end and any(_native_alive(owner) for owner in owners):
            time.sleep(min(.01, max(0., end-time.perf_counter())))
        for owner in owners:
            owner['released'] = ((owner['process'].pid is None or not owner['process'].is_alive())
                and not _native_alive(owner))
        if any(not owner['released'] for owner in owners):
            self._failed = True
            raise RuntimeError('slot owner shutdown could not release its reserved resources')

    @staticmethod
    def _terminate(owner, sig):
        if owner.get('released', False):
            return
        process = owner['process']
        if process.pid is None:
            return
        if hasattr(os, 'killpg'):
            try:
                os.killpg(process.pid, sig)
                return
            except ProcessLookupError:
                pass
            # Native owners inherit this group. Once it is gone, do not signal
            # a stale numeric native PID that may have been reused elsewhere.
            if process.is_alive():
                process.kill() if sig == signal.SIGKILL else process.terminate()
            return
        if process.is_alive():
            process.kill() if sig == signal.SIGKILL else process.terminate()
        pid = owner.get('native_pid')
        if pid is not None:
            try:
                os.kill(pid, sig)
            except ProcessLookupError:
                pass

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()

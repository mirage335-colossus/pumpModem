"""Fail-closed identity for a future local CPU receiver-service calibration.

Matching identities do not establish predictive accuracy, memory headroom or
current performance.  A caller must still check workload/measurement geometry,
workspace availability and independent holdouts, and must not substitute GPU
elapsed time for local CPU service.  Old receipts without this identity cannot
be promoted to a current calibration.
"""
from __future__ import annotations

from contextlib import ExitStack
import hashlib
import importlib
import json
import os
import platform
from pathlib import Path
import re
import stat
import subprocess
import sys
import time

SCHEMA = 'triad-local-cpu-calibration-identity-v2'
_HEX = re.compile(r'^[0-9a-f]{64}$')
# Vetted static transitive import closure for the complete manual path.  Keep
# this explicit: a glob by itself cannot detect a file removed before capture.
_PYTHON_MODULES = (
    'build_capacity', 'triad_abbreviated', 'triad_acquisition',
    'triad_case_capacity', 'triad_case_metrics', 'triad_case_noise',
    'triad_case_receiver', 'triad_case_replay', 'triad_case_suite', 'triad_cases',
    'triad_continuous', 'triad_cpu', 'triad_duration_model',
    'triad_dynamic_frequency', 'triad_dynamic_joint', 'triad_dynamic_scan',
    'triad_dynamic_variants', 'triad_gaussian_prepared', 'triad_gaussian_state',
    'triad_incremental_receiver', 'triad_manual_cases', 'triad_message_gate',
    'triad_message_receiver', 'triad_operational_admission', 'triad_orchestration_cost',
    'triad_quiet_diagnostic', 'triad_realtime_fft', 'triad_receiver',
    'triad_regression_receiver', 'triad_stream_receiver', 'triad_timing_forward',
    'triad_tracking_basis', 'triad_tracking_budget', 'triad_tracking_receiver',
    'triad_weak_helper',
)
_DSP_SOURCES = frozenset(
    ['tools/' + name + '.py' for name in _PYTHON_MODULES] + [
    'tools/triad-helper-export.cpp', 'src/triad_basis.cpp', 'src/triad_random_process.hpp',
    'src/triad_shaping.cpp', 'src/triad_gaussian_fading.hpp', 'src/triad_modem_v3.cpp',
    'src/triad_simulation_stream.cpp', 'src/compression_short.cpp', 'src/tuning_presets.cpp',
    'src/helper_carrier.cpp', 'include/datapump/triad_transmission_keys.hpp',
    'include/datapump/triad_pulse.hpp', 'include/datapump/triad_helper_burst.hpp',
    'include/datapump/triad_shaping.hpp', 'include/datapump/triad_modem_v3.hpp',
    'include/datapump/triad_simulation.hpp'])
_THREAD_VARIABLES = ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS',
    'BLIS_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS', 'NUMEXPR_NUM_THREADS')
_OPTIONAL_THREAD_VARIABLES = ('OMP_PROC_BIND', 'OMP_PLACES', 'OMP_DYNAMIC', 'OMP_SCHEDULE',
    'OMP_MAX_ACTIVE_LEVELS', 'GOMP_CPU_AFFINITY', 'GOMP_SPINCOUNT', 'KMP_AFFINITY',
    'KMP_BLOCKTIME', 'KMP_SETTINGS', 'OPENBLAS_CORETYPE', 'OPENBLAS_NUM_THREADS64_',
    'OPENBLAS_MAIN_FREE', 'MKL_CBWR', 'MKL_DYNAMIC', 'MKL_THREADING_LAYER',
    'MKL_ENABLE_INSTRUCTIONS')
_THREAD_PREFIXES = ('OMP_', 'GOMP_', 'KMP_', 'OPENBLAS_', 'MKL_', 'BLIS_', 'VECLIB_', 'NUMEXPR_')
_COMPILED_ROLES = frozenset(('numpy_core', 'numpy_fft', 'scipy_fft', 'scipy_linalg',
    'scipy_special', 'torch_extension', 'torch_cpu'))
_CONDITIONAL_SCOPE = (
    'Conditional path and inode identity in a trusted shared mount namespace. '
    'The mapped device differs from file stat and the mapped file handle is not proved. '
    'Use fresh controlled processes without concurrent file or mount replacement, '
    'in-place binary mutation or unobserved transient executable loads. '
    'Pair captures around each actual workload and validate timing independently.'
)


def _hash_file(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
        allow_nan=False).encode()).hexdigest()


def _positive(value):
    return type(value) is int and value > 0


def _integer_pair(value):
    return isinstance(value, list) and len(value) == 2 and all(type(n) is int and n >= 0 for n in value)


def _valid_hash(value):
    return isinstance(value, str) and bool(_HEX.fullmatch(value))


def _known(value):
    return (isinstance(value, str) and bool(value.strip()) and value == value.strip() and
            value.lower() not in ('unknown', 'unavailable', 'none', 'n/a', 'na', '?', '-') and
            not value.lower().startswith(('unknown ', 'unavailable ')))


def _expected_source_paths():
    from triad_cases import ROOT
    found = set()
    for pattern in ('tools/triad*.*', 'src/triad*.*', 'include/datapump/triad*.*'):
        found.update(str(path.relative_to(ROOT)) for path in ROOT.glob(pattern) if path.is_file())
    found.update(('src/compression_short.cpp', 'src/tuning_presets.cpp',
                  'src/helper_carrier.cpp', 'tools/build_capacity.py'))
    return found


def _thread_environment():
    keys = set(_THREAD_VARIABLES) | set(_OPTIONAL_THREAD_VARIABLES)
    keys.update(key for key in os.environ if key.startswith(_THREAD_PREFIXES))
    return {key:os.environ.get(key) for key in sorted(keys)}


def _executable_maps(process):
    rows = (process / 'maps').read_text().splitlines()
    if not rows or len(rows) > 16384:
        raise ValueError('process maps unavailable or exceed the bounded scan')
    files, regions = {}, []
    for row in rows:
        parts = row.split(maxsplit=5)
        if len(parts) < 5:
            raise ValueError('malformed process mapping')
        if 'x' not in parts[1]:
            continue
        if 'w' in parts[1]:
            raise ValueError('writable executable mapping is not identified')
        inode = int(parts[4])
        path = parts[5] if len(parts) == 6 else ''
        if inode == 0:
            if path in ('[vdso]', '[vsyscall]'):
                continue
            raise ValueError('anonymous executable mapping is not identified')
        if not path.startswith('/') or path.endswith(' (deleted)'):
            raise ValueError('executable mapping path is unavailable')
        major, minor = (int(value, 16) for value in parts[3].split(':'))
        resolved = str(Path(path).resolve(strict=True))
        entry = (major, minor, inode)
        if resolved in files and files[resolved] != entry:
            raise ValueError('a mapped file changed identity')
        files[resolved] = entry
        first, last = (int(bound, 16) for bound in parts[0].split('-'))
        if first >= last:
            raise ValueError('invalid executable mapping extent')
        regions.append((f'{first:x}-{last:x}', resolved, parts[1], parts[2]))
    if not files:
        raise ValueError('no executable mappings found')
    return files, regions


def _mount_namespace(process):
    metadata = (process / 'ns' / 'mnt').stat()
    return metadata.st_dev, metadata.st_ino


def _verify_map_links(process, regions):
    for region, path, _, _ in regions:
        target = os.readlink(process / 'map_files' / region)
        if (not target.startswith('/') or target.endswith(' (deleted)') or
                str(Path(target).resolve(strict=True)) != path):
            raise ValueError('mapped executable path cannot be verified')


def _file_identity(metadata):
    return (metadata.st_dev, metadata.st_ino, metadata.st_mode, metadata.st_size,
            metadata.st_mtime_ns, metadata.st_ctime_ns)


def _mapped_executables(pid, *, allow_conditional=False):
    """Hash stable executable backing files and record any device discrepancy."""
    process = Path('/proc') / str(pid)
    own_process = Path('/proc/self')
    namespace = _mount_namespace(process)
    if namespace != _mount_namespace(own_process):
        raise ValueError('process mount namespace differs from the controller')
    files, regions = _executable_maps(process)
    result = {}
    conditional = {}
    with ExitStack() as stack:
        opened = {}
        for path, (major, minor, inode) in sorted(files.items()):
            fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            stream = stack.enter_context(os.fdopen(fd, 'rb'))
            before = os.fstat(stream.fileno())
            if not stat.S_ISREG(before.st_mode) or before.st_ino != inode:
                raise ValueError('mapped executable no longer matches its file')
            file_device = [os.major(before.st_dev), os.minor(before.st_dev)]
            mapped_device = [major, minor]
            if file_device != mapped_device:
                if not allow_conditional:
                    raise ValueError('mapped executable no longer matches its file')
                conditional[path] = dict(mapped_device=mapped_device, file_device=file_device, inode=inode)
            opened[path] = stream, _file_identity(before)
        if conditional:
            _verify_map_links(process, regions)
        for path, (stream, _) in opened.items():
            digest = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(block)
            result[path] = digest.hexdigest()
        if (_mount_namespace(process) != namespace or _mount_namespace(own_process) != namespace or
                _executable_maps(process) != (files, regions)):
            raise ValueError('process executable mappings changed during hashing')
        if conditional:
            _verify_map_links(process, regions)
        for path, (stream, expected) in opened.items():
            if (_file_identity(os.fstat(stream.fileno())) != expected or
                    _file_identity(os.stat(path, follow_symlinks=False)) != expected):
                raise ValueError('executable file changed during hashing')
    return result, conditional, _digest((files, regions))


def _process_pair(native_pid):
    def process_marker(pid):
        fields = (Path('/proc') / str(pid) / 'stat').read_text().rpartition(')')[2].split()
        if len(fields) < 20 or not fields[19].isdigit():
            raise ValueError('process start time unavailable')
        return [pid, int(fields[19])]

    namespace = _mount_namespace(Path('/proc/self'))
    if _mount_namespace(Path('/proc') / str(native_pid)) != namespace:
        raise ValueError('native mount namespace differs from the controller')
    return dict(controller=process_marker(os.getpid()), native=process_marker(native_pid),
        mount_namespace=list(namespace), boot_id_sha256=_hash_file('/proc/sys/kernel/random/boot_id'))


def _compiled_roles(host_files, numpy, scipy, torch):
    """Require the CPU runtime components exercised by the canonical path."""
    import numpy._core._multiarray_umath as numpy_core
    import numpy.fft._pocketfft_umath as numpy_fft
    import scipy.linalg._fblas as scipy_linalg
    import scipy.special._ufuncs as scipy_special
    fft_candidates = sorted(str(Path(module.__file__).resolve()) for name, module in tuple(sys.modules.items())
        if name.startswith('scipy.fft.') and getattr(module, '__file__', None) and
        str(module.__file__).endswith('.so'))
    torch_cpu = sorted(path for path in host_files if Path(path).name.startswith('libtorch_cpu.so'))
    if not fft_candidates or not torch_cpu:
        raise ValueError('required compiled FFT or Torch CPU backend not observed')
    roles = dict(numpy_core=str(Path(numpy_core.__file__).resolve()),
        numpy_fft=str(Path(numpy_fft.__file__).resolve()), scipy_fft=fft_candidates[0],
        scipy_linalg=str(Path(scipy_linalg.__file__).resolve()),
        scipy_special=str(Path(scipy_special.__file__).resolve()),
        torch_extension=str(Path(torch._C.__file__).resolve()), torch_cpu=torch_cpu[0])
    if any(path not in host_files for path in roles.values()):
        raise ValueError('required compiled backend is not mapped')
    return roles


def _pool_descriptors(threads):
    import threadpoolctl

    result = []
    for item in threadpoolctl.threadpool_info():
        path = item.get('filepath')
        if (not isinstance(path, str) or not Path(path).is_absolute() or
                not Path(path).is_file() or not _positive(item.get('num_threads')) or
                item['num_threads'] != threads or
                any(not _known(item.get(key))
                    for key in ('user_api', 'internal_api', 'prefix'))):
            raise ValueError('unknown or uncontrolled numerical thread pool')
        version = item.get('version')
        if version is not None and not _known(version):
            raise ValueError('invalid numerical-library version')
        architecture = item.get('architecture')
        if architecture is not None and not _known(architecture):
            raise ValueError('invalid numerical-library architecture')
        result.append(dict(user_api=item['user_api'], internal_api=item['internal_api'],
            prefix=item['prefix'], filepath=str(Path(path).resolve()),
            version=version, architecture=architecture, num_threads=threads,
            binary_sha256=_hash_file(path)))
    if not result:
        raise ValueError('numerical thread-pool providers are unavailable')
    return sorted(result, key=lambda row: (row['filepath'], row['user_api'],
        row['internal_api'], row['prefix']))


def _valid_identity(value):
    """Validate complete mandatory fields before comparing identities."""
    if not isinstance(value, dict) or set(value) != {
            'configuration_hash', 'manual_list_sha256', 'precision', 'workers', 'fft_threads',
            'numerical_threads', 'cpu', 'runtime', 'sources', 'native_binary_sha256'}:
        return False
    if not (_valid_hash(value['configuration_hash']) and _valid_hash(value['manual_list_sha256']) and
            _valid_hash(value['native_binary_sha256']) and
            value['precision'] == 'fp64' and all(_positive(value[k]) for k in
                ('workers', 'fft_threads', 'numerical_threads'))):
        return False
    sources = value['sources']
    if (not isinstance(sources, dict) or not _DSP_SOURCES.issubset(sources) or
            set(sources) != _expected_source_paths()):
        return False
    if any(not isinstance(k, str) or not k or not _valid_hash(v) for k, v in sources.items()):
        return False
    cpu = value['cpu']
    if not isinstance(cpu, dict) or set(cpu) != {'model', 'machine', 'machine_id_sha256', 'budget'}:
        return False
    budget = cpu['budget']
    if (not all(_known(cpu[k]) for k in ('model', 'machine')) or
            not _valid_hash(cpu['machine_id_sha256']) or not isinstance(budget, dict)):
        return False
    topology = budget.get('topology')
    if (budget.get('quota_status') != 'visible' or budget.get('warnings') != [] or
            not _positive(budget.get('available_cpus')) or
            not _positive(budget.get('os_available_cpus')) or
            budget['available_cpus'] > budget['os_available_cpus'] or
            not isinstance(budget.get('quota_limits'), list) or
            not isinstance(budget.get('receiver_worker_candidates'), list) or
            any(not _positive(n) for n in budget['receiver_worker_candidates']) or
            not isinstance(topology, dict) or topology.get('status') != 'complete' or
            not _positive(topology.get('physical_core_count')) or
            not isinstance(topology.get('logical_cpu_ids'), list) or
            not topology['logical_cpu_ids'] or any(type(n) is not int or n < 0 for n in topology['logical_cpu_ids']) or
            topology['logical_cpu_ids'] != sorted(set(topology['logical_cpu_ids'])) or
            topology.get('logical_cpu_count') != len(topology['logical_cpu_ids']) or
            not isinstance(topology.get('physical_cores'), list) or
            len(topology['physical_cores']) != topology['physical_core_count'] or
            value['workers'] * max(value['fft_threads'], value['numerical_threads']) > budget['available_cpus']):
        return False
    for limit in budget['quota_limits']:
        if (not isinstance(limit, dict) or not isinstance(limit.get('source'), str) or
                not limit['source'] or any(not _positive(limit.get(key)) for key in ('quota', 'period', 'cpus')) or
                limit['cpus'] != max(1, limit['quota'] // limit['period'])):
            return False
    if budget['available_cpus'] != min([budget['os_available_cpus'],
                                       *(limit['cpus'] for limit in budget['quota_limits'])]):
        return False
    if (budget['receiver_worker_candidates'] != sorted(set(budget['receiver_worker_candidates'])) or
            1 not in budget['receiver_worker_candidates'] or
            budget['available_cpus'] not in budget['receiver_worker_candidates']):
        return False
    covered, core_ids = [], set()
    for core in topology['physical_cores']:
        if (not isinstance(core, dict) or any(type(core.get(key)) is not int or core[key] < 0
                for key in ('package_id', 'core_id')) or
                not isinstance(core.get('logical_cpu_ids'), list) or not core['logical_cpu_ids'] or
                any(type(n) is not int or n < 0 for n in core['logical_cpu_ids'])):
            return False
        covered.extend(core['logical_cpu_ids'])
        core_ids.add((core['package_id'], core['core_id']))
    if sorted(covered) != topology['logical_cpu_ids'] or len(core_ids) != len(topology['physical_cores']):
        return False
    runtime = value['runtime']
    if not isinstance(runtime, dict) or set(runtime) != {
            'implementation', 'python_version', 'python_binary_sha256', 'numpy_version',
            'scipy_version', 'torch_version', 'threadpoolctl_version', 'torch_build_sha256',
            'kernel_release', 'libc', 'torch_intraop_threads', 'torch_interop_threads',
            'thread_environment', 'thread_pools', 'host_executables', 'native_executables',
            'compiled_roles', 'conditional_mapping_devices'}:
        return False
    if any(not _known(runtime[k]) for k in
           ('implementation', 'python_version', 'numpy_version', 'scipy_version', 'torch_version',
            'threadpoolctl_version', 'kernel_release')):
        return False
    if (not _valid_hash(runtime['python_binary_sha256']) or not _valid_hash(runtime['torch_build_sha256']) or
            not isinstance(runtime['libc'], list) or len(runtime['libc']) != 2 or
            any(not _known(part) for part in runtime['libc']) or
            not _positive(runtime['torch_intraop_threads']) or not _positive(runtime['torch_interop_threads']) or
            runtime['torch_intraop_threads'] != value['numerical_threads'] or runtime['torch_interop_threads'] != 1):
        return False
    env = runtime['thread_environment']
    if (not isinstance(env, dict) or not (set(_THREAD_VARIABLES) | set(_OPTIONAL_THREAD_VARIABLES)).issubset(env) or
            any(not isinstance(k, str) or not k.startswith(_THREAD_PREFIXES) or
                (v is not None and not _known(v)) for k, v in env.items()) or
            any(env[k] != str(value['numerical_threads']) for k in _THREAD_VARIABLES)):
        return False
    pools = runtime['thread_pools']
    if not isinstance(pools, list) or not pools:
        return False
    for pool in pools:
        if not isinstance(pool, dict) or set(pool) != {'user_api', 'internal_api', 'prefix',
                'filepath', 'version', 'architecture', 'num_threads', 'binary_sha256'}:
            return False
        if (any(not _known(pool[k]) for k in
                ('user_api', 'internal_api', 'prefix', 'filepath')) or
                not Path(pool['filepath']).is_absolute() or
                not _valid_hash(pool['binary_sha256']) or
                not _positive(pool['num_threads']) or pool['num_threads'] != value['numerical_threads'] or
                (pool['version'] is not None and not _known(pool['version'])) or
                (pool['architecture'] is not None and not _known(pool['architecture']))):
            return False
    for field in ('host_executables', 'native_executables'):
        files = runtime[field]
        if (not isinstance(files, dict) or not files or len(files) > 2048 or
                any(not _known(path) or not Path(path).is_absolute() or not _valid_hash(digest)
                    for path, digest in files.items())):
            return False
    devices = runtime['conditional_mapping_devices']
    if not isinstance(devices, dict) or set(devices) != {'host', 'native'}:
        return False
    for side, paths in devices.items():
        if not isinstance(paths, dict) or not set(paths).issubset(runtime[side + '_executables']):
            return False
        for metadata in paths.values():
            if (not isinstance(metadata, dict) or set(metadata) != {'mapped_device', 'file_device', 'inode'} or
                    not _integer_pair(metadata['mapped_device']) or not _integer_pair(metadata['file_device']) or
                    metadata['mapped_device'] == metadata['file_device'] or not _positive(metadata['inode'])):
                return False
    if value['native_binary_sha256'] not in runtime['native_executables'].values():
        return False
    roles = runtime['compiled_roles']
    if (not isinstance(roles, dict) or set(roles) != _COMPILED_ROLES or
            any(not _known(path) or path not in runtime['host_executables'] for path in roles.values())):
        return False
    if any(pool['filepath'] not in runtime['host_executables'] or
           runtime['host_executables'][pool['filepath']] != pool['binary_sha256'] for pool in pools):
        return False
    return True


def capture_calibration_identity(*, exporter, configuration_hash, workers, native_service=None,
                                 case_list=None, numerical_threads=1, fft_threads=1, precision='fp64',
                                 allow_conditional=False):
    """Capture local CPU provenance after numerical threads are configured.

    The caller supplies a canonical case's configuration hash. The current
    manual list is always reparsed and must contain it; its exact file hash is
    bound into the identity. The caller supplies its live, owned canonical
    NativeStream; executable mappings from that actual service are bound rather
    than guessed from library names. Failure to identify a runtime, source or
    CPU is unresolved. Capture again after actual work: a match before/after
    still does not prove worker thread settings, workload coverage, workspace,
    a stable thermal state, held-out prediction accuracy or any timing claim.
    A device mismatch fails by default. The explicit conditional mode checks
    kernel mapping links and held file descriptors, and records the discrepancy.
    All comparisons require both observations around each workload.
    The returned identity is local and must not be uploaded to a GPU or another
    host; it contains a hashed machine ID and machine-specific paths.
    """
    try:
        started_ns = time.monotonic_ns()
        if (not _valid_hash(configuration_hash) or not _positive(workers) or
                not _positive(numerical_threads) or not _positive(fft_threads) or precision != 'fp64' or
                type(allow_conditional) is not bool):
            raise ValueError('invalid declared calibration domain')
        from triad_case_metrics import machine_resources
        from triad_cases import DEFAULT_CASES, ROOT, load_cases
        from triad_case_suite import identity
        from triad_continuous import NativeStream
        import numpy
        import scipy
        import scipy.fft
        import scipy.linalg
        import scipy.special
        import torch
        import threadpoolctl

        case_list = DEFAULT_CASES if case_list is None else case_list
        cases = load_cases(case_list)
        if configuration_hash not in {case['configuration_hash'] for case in cases['cases']}:
            raise ValueError('configuration is absent from the current manual list')
        if type(native_service) is not NativeStream:
            raise ValueError('a live owned canonical native service is required')
        process = native_service.process
        if process is None or process.poll() is not None:
            raise ValueError('a live owned canonical native service is required')
        pid = process.pid
        if not _positive(pid):
            raise ValueError('native service PID is invalid')
        process_executable = os.stat('/proc/' + str(pid) + '/exe')
        expected_executable = os.stat(exporter)
        if (process_executable.st_dev, process_executable.st_ino) != (
                expected_executable.st_dev, expected_executable.st_ino):
            raise ValueError('native service executable differs from the exporter')
        process_status = (Path('/proc') / str(pid) / 'status').read_text().splitlines()
        parents = [line.split(':', 1)[1].strip() for line in process_status if line.startswith('PPid:')]
        if parents != [str(os.getpid())]:
            raise ValueError('native service is not owned by this controller')
        process_pair = _process_pair(pid)
        initial = identity(exporter)
        initial['sources']['tools/build_capacity.py'] = _hash_file(ROOT / 'tools/build_capacity.py')
        machine = machine_resources()
        for name in _PYTHON_MODULES:
            importlib.import_module(name)
        # Load the actual compiled FFT, projection, likelihood and BLAS paths.
        numpy.fft.rfft(numpy.zeros(8, dtype=numpy.float64))
        scipy.fft.rfft(numpy.zeros(8, dtype=numpy.float64))
        scipy.linalg.solve(numpy.eye(2), numpy.ones(2))
        scipy.special.ndtr(1.)
        torch.linalg.cholesky(torch.eye(2, dtype=torch.float64))
        torch_build = torch.__config__.show()
        if not isinstance(torch_build, str) or not torch_build:
            raise ValueError('PyTorch build identity is unavailable')
        host_files, host_devices, host_maps = _mapped_executables(
            os.getpid(), allow_conditional=allow_conditional)
        native_files, native_devices, native_maps = _mapped_executables(
            pid, allow_conditional=allow_conditional)
        compiled_roles = _compiled_roles(host_files, numpy, scipy, torch)
        thread_environment = _thread_environment()
        runtime = dict(implementation=platform.python_implementation(),
            python_version=platform.python_version(), python_binary_sha256=_hash_file(sys.executable),
            numpy_version=numpy.__version__, scipy_version=scipy.__version__, torch_version=str(torch.__version__),
            threadpoolctl_version=threadpoolctl.__version__,
            torch_build_sha256=hashlib.sha256(torch_build.encode()).hexdigest(),
            kernel_release=platform.release(), libc=list(platform.libc_ver()),
            torch_intraop_threads=torch.get_num_threads(), torch_interop_threads=torch.get_num_interop_threads(),
            thread_environment=thread_environment, thread_pools=_pool_descriptors(numerical_threads),
            host_executables=host_files, native_executables=native_files, compiled_roles=compiled_roles,
            conditional_mapping_devices=dict(host=host_devices, native=native_devices))
        final = identity(exporter)
        final['sources']['tools/build_capacity.py'] = _hash_file(ROOT / 'tools/build_capacity.py')
        final_cases = load_cases(case_list)
        final_machine = machine_resources()
        if initial['sources'] != final['sources'] or initial['native_binary_sha256'] != final['native_binary_sha256']:
            raise ValueError('receiver source or native binary changed during identity capture')
        if machine['cpu_model'] != final_machine['cpu_model'] or machine['cpu'] != final_machine['cpu']:
            raise ValueError('CPU topology or quota changed during identity capture')
        if cases['source_sha256'] != final_cases['source_sha256']:
            raise ValueError('manual list changed during identity capture')
        if (thread_environment != _thread_environment() or native_service.process is not process or
                process.poll() is not None or _process_pair(pid) != process_pair):
            raise ValueError('thread controls or native service changed during identity capture')
        value = dict(configuration_hash=configuration_hash, manual_list_sha256=cases['source_sha256'],
            precision=precision, workers=workers,
            fft_threads=fft_threads, numerical_threads=numerical_threads,
            cpu=dict(model=machine['cpu_model'], machine=platform.machine(),
                machine_id_sha256=_hash_file('/etc/machine-id'), budget=machine['cpu']),
            runtime=runtime, sources=final['sources'], native_binary_sha256=final['native_binary_sha256'])
        if not _valid_identity(value):
            raise ValueError('required calibration identity is unknown or invalid')
        conditional = bool(host_devices or native_devices)
        return dict(schema=SCHEMA, status='conditional' if conditional else 'available',
            identity=value, identity_sha256=_digest(value),
            observation=dict(process_pair=process_pair, host_maps_sha256=host_maps,
                native_maps_sha256=native_maps, started_ns=started_ns, ended_ns=time.monotonic_ns()),
            scope=_CONDITIONAL_SCOPE if conditional else
                'Local identity only; FFT and worker-thread controls are declarations. Does not establish actual worker settings, CPU timing, workspace, GPU parity or predictive accuracy.')
    except (OSError, ValueError, TypeError, RuntimeError, ImportError, KeyError,
            subprocess.SubprocessError) as error:
        return dict(schema=SCHEMA, status='unresolved', reason=type(error).__name__,
            scope='Unavailable identity cannot authorize calibration reuse.')


def _valid_observation(observation):
    if not isinstance(observation, dict) or set(observation) != {
            'process_pair', 'host_maps_sha256', 'native_maps_sha256', 'started_ns', 'ended_ns'}:
        return False
    pair = observation['process_pair']
    return (isinstance(pair, dict) and set(pair) == {'controller', 'native', 'mount_namespace', 'boot_id_sha256'} and
            all(_integer_pair(pair[key]) and pair[key][0] > 0 for key in ('controller', 'native')) and
            pair['controller'][0] != pair['native'][0] and _integer_pair(pair['mount_namespace']) and
            _valid_hash(pair['boot_id_sha256']) and
            all(_valid_hash(observation[key]) for key in ('host_maps_sha256', 'native_maps_sha256')) and
            _positive(observation['started_ns']) and _positive(observation['ended_ns']) and
            observation['started_ns'] < observation['ended_ns'])


def _receipt_problem(receipt, *, allow_conditional):
    try:
        if (not isinstance(receipt, dict) or receipt.get('schema') != SCHEMA or
                not _valid_identity(receipt.get('identity'))):
            return 'identity_unavailable_or_invalid'
        devices = receipt['identity']['runtime']['conditional_mapping_devices']
        conditional = bool(devices['host'] or devices['native'])
        if receipt.get('status') != ('conditional' if conditional else 'available'):
            return 'identity_mode_mismatch'
        if conditional and not allow_conditional:
            return 'conditional_identity_not_admitted'
        if not _valid_observation(receipt.get('observation')):
            return 'identity_observation_unavailable_or_invalid'
        if receipt.get('identity_sha256') != _digest(receipt['identity']):
            return 'identity_digest_mismatch'
    except (OSError, ValueError, TypeError, ImportError):
        return 'identity_unavailable_or_invalid'
    return None


def compare_calibration_identities(recorded, current, *, allow_conditional=False,
                                   recorded_after=None, current_after=None):
    """Compare unchanged paired observations for two workloads.

    ``recorded`` and ``current`` are the before observations. Each after
    observation must come from the same controlled processes as its before
    observation. The caller binds the actual work and measured start/end to the
    interval between captures and validates work and timing independently.
    """
    if type(allow_conditional) is not bool:
        return dict(status='unresolved', reason='invalid_conditional_control')
    for name, receipt in (('recorded', recorded), ('current', current)):
        problem = _receipt_problem(receipt, allow_conditional=allow_conditional)
        if problem:
            return dict(status='unresolved', reason=name + '_' + problem)
    if recorded['identity_sha256'] != current['identity_sha256'] or recorded['identity'] != current['identity']:
        differences = [key for key in sorted(recorded['identity'])
                       if recorded['identity'][key] != current['identity'][key]]
        return dict(status='unresolved', reason='calibration_identity_changed', changed_fields=differences)
    for name, before, after in (('recorded', recorded, recorded_after), ('current', current, current_after)):
        problem = _receipt_problem(after, allow_conditional=allow_conditional)
        if problem:
            return dict(status='unresolved', reason=name + '_after_' + problem)
        if (before['identity'] != after['identity'] or
                any(before['observation'][key] != after['observation'][key]
                    for key in ('process_pair', 'host_maps_sha256', 'native_maps_sha256')) or
                before['observation']['ended_ns'] >= after['observation']['started_ns']):
            return dict(status='unresolved', reason=name + '_work_identity_changed')
    if recorded['status'] == 'conditional':
        return dict(status='conditionally_matched', scope=_CONDITIONAL_SCOPE)
    return dict(status='matched', scope='Exact identity match only; no performance or coverage qualification.')

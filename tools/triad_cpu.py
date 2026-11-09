"""One CPU allocation for isolated receiver workers and numerical libraries.

Detection reuses the project's affinity and cgroup traversal, without its
compiler-specific CPU reserve or memory heuristic. Numerical libraries must
be configured by the caller before workers start; this module imports none.
"""
from __future__ import annotations

from pathlib import Path
import os
import sys
import threading

from build_capacity import cgroup_directories, positive_int, read_text, usable_cpus

_NUMERICAL_LIMITER = None
_NUMERICAL_CONTROLLER = None
_CONFIGURED_LINEAR_THREADS = None
_NUMERICAL_CONFIGURATION = None
_WORKER_NUMERICAL = threading.local()


def _positive_integer(value, name):
    if isinstance(value, bool) or not isinstance(value, int) or value < 1:
        raise ValueError(name + ' must be a positive integer')
    return value


def detect_cpu_topology(*, logical_cpu_ids=None,
                        sysfs_root=Path('/sys/devices/system/cpu'),
                        linux=None):
    """Identify physical cores represented by the process affinity, if known.

    Linux sysfs reports physical package/core pairs for each logical CPU. The
    count is complete only when that data is readable for every CPU in the
    effective affinity mask. A partial read remains unknown: counting only the
    readable nodes could silently omit a physical core. Other platforms report
    topology as unavailable instead of estimating cores from SMT ratios.

    ``logical_cpu_ids`` and ``sysfs_root`` are injection points for deterministic
    tests; normal callers should omit them.
    """
    if linux is None:
        linux = sys.platform.startswith('linux')
    if not linux:
        return dict(status='unavailable', logical_cpu_ids=None,
                    logical_cpu_count=None, physical_core_count=None,
                    physical_cores=None,
                    scope='Physical topology is not available from this platform implementation.')

    if logical_cpu_ids is None:
        get_affinity = getattr(os, 'sched_getaffinity', None)
        if get_affinity is None:
            return dict(status='unavailable', logical_cpu_ids=None,
                        logical_cpu_count=None, physical_core_count=None,
                        physical_cores=None,
                        scope='Linux effective process affinity is unavailable; physical topology is not guessed.')
        try:
            logical_cpu_ids = get_affinity(0)
        except OSError as error:
            return dict(status='unavailable', logical_cpu_ids=None,
                        logical_cpu_count=None, physical_core_count=None,
                        physical_cores=None,
                        reason='Cannot read effective CPU affinity: ' + str(error),
                        scope='Physical topology is not guessed when effective CPU affinity is unknown.')
    try:
        parsed_cpu_ids = []
        for cpu in logical_cpu_ids:
            if isinstance(cpu, bool) or not isinstance(cpu, int) or cpu < 0:
                raise ValueError('logical CPU ids must be nonnegative integers')
            parsed_cpu_ids.append(cpu)
        logical_cpu_ids = sorted(set(parsed_cpu_ids))
    except (TypeError, ValueError, OverflowError):
        return dict(status='unavailable', logical_cpu_ids=None,
                    logical_cpu_count=None, physical_core_count=None,
                    physical_cores=None,
                    reason='Effective CPU affinity contains invalid CPU identifiers.',
                    scope='Physical topology is not guessed when effective CPU affinity is invalid.')
    if not logical_cpu_ids:
        return dict(status='unavailable', logical_cpu_ids=[],
                    logical_cpu_count=0, physical_core_count=None,
                    physical_cores=None, reason='Effective CPU affinity is empty.',
                    scope='Physical topology requires at least one allowed logical CPU.')

    cores = {}
    errors = []
    for cpu in logical_cpu_ids:
        directory = Path(sysfs_root) / ('cpu' + str(cpu)) / 'topology'
        try:
            package = int((directory / 'physical_package_id').read_text().strip())
            core = int((directory / 'core_id').read_text().strip())
            if package < 0 or core < 0:
                raise ValueError('negative package/core id')
        except (OSError, ValueError, TypeError) as error:
            errors.append(dict(logical_cpu=cpu, reason=str(error)))
            continue
        cores.setdefault((package, core), []).append(cpu)

    if errors:
        return dict(status='partial', logical_cpu_ids=logical_cpu_ids,
                    logical_cpu_count=len(logical_cpu_ids), physical_core_count=None,
                    physical_cores=None, incomplete_logical_cpus=errors,
                    scope='Incomplete Linux sysfs topology; physical-core count is unknown, never inferred from the readable subset.')
    groups = [dict(package_id=package, core_id=core,
                   logical_cpu_ids=sorted(ids))
              for (package, core), ids in sorted(cores.items())]
    return dict(status='complete', logical_cpu_ids=logical_cpu_ids,
                logical_cpu_count=len(logical_cpu_ids),
                physical_core_count=len(groups), physical_cores=groups,
                source='Linux sysfs physical_package_id/core_id for every allowed logical CPU.',
                scope='Topology counts only cores represented in the current effective process affinity. It does not estimate memory, thermal, or task-scheduler capacity.')


def receiver_worker_candidates(capacity):
    """Return bounded worker counts to measure on one shared receive stream.

    The candidate list is an experiment plan, not a predicted winner. It
    includes serial operation, modest scaling points, known physical cores when
    available, and the entire logical CPU budget so hyperthreads are tested
    rather than assumed useful. It never multiplies independent receptions.
    """
    if isinstance(capacity, int) and not isinstance(capacity, bool):
        receipt = dict(available_cpus=capacity)
    elif isinstance(capacity, dict):
        receipt = capacity
    else:
        raise ValueError('capacity must be a CPU detection record or positive integer')
    budget = _positive_integer(receipt.get('available_cpus'), 'available CPUs')
    candidates = {1, min(2, budget), min(4, budget), budget}
    topology = receipt.get('topology')
    if isinstance(topology, dict) and topology.get('status') == 'complete':
        physical = topology.get('physical_core_count')
        if physical is not None:
            physical = _positive_integer(physical, 'physical cores')
            candidates.add(min(physical, budget))
    return sorted(candidates)


def detect_cpu_budget(proc=Path('/proc')):
    """Report the current process allocation and visible Linux ancestor quotas.

    Fractional CPU quotas are rounded down, with one thread as the minimum.
    Inaccessible or malformed quota information is reported rather than treated
    as proof of unlimited CPUs. Known limits remain effective if another node
    is unreadable. Ancestors hidden by a container namespace are unknowable.
    """
    available = _positive_integer(usable_cpus(), 'OS CPU allocation')
    limits, warnings = [], []
    status = 'not_applicable'
    if sys.platform.startswith('linux'):
        status = 'visible'
        try:
            for controllers, directories in cgroup_directories(proc):
                if '' not in controllers and 'cpu' not in controllers:
                    continue
                for directory in directories:
                    try:
                        if '' in controllers:
                            parts = read_text(directory / 'cpu.max').split()
                            source = str(directory / 'cpu.max')
                        else:
                            parts = [read_text(directory / 'cpu.cfs_quota_us').strip(),
                                     read_text(directory / 'cpu.cfs_period_us').strip()]
                            source = str(directory / 'cpu.cfs_quota_us')
                        if not any(parts):
                            continue  # CPU controller may not be enabled here.
                        if len(parts) != 2:
                            raise ValueError('unrecognized CPU quota')
                        amount, period = map(positive_int, parts)
                        if period is None or (amount is None and parts[0] not in ('max', '-1')):
                            raise ValueError('unrecognized CPU quota')
                        if amount is not None:
                            limits.append(dict(source=source, quota=amount, period=period,
                                               cpus=max(1, amount // period)))
                    except (OSError, ValueError, TypeError) as error:
                        status = 'partial'
                        warnings.append(str(directory) + ': ' + str(error))
        except (OSError, ValueError, TypeError) as error:
            status = 'unknown'
            warnings.append('cgroup CPU visibility: ' + str(error))
    budget = min([available, *(limit['cpus'] for limit in limits)])
    topology = detect_cpu_topology()
    return dict(available_cpus=budget, os_available_cpus=available,
                quota_status=status, quota_limits=limits, topology=topology,
                receiver_worker_candidates=receiver_worker_candidates(
                    dict(available_cpus=budget, topology=topology)),
                warnings=warnings,
                scope='Process-visible logical CPUs and readable ancestor quotas; no CPU reserve. Hidden limits and future affinity changes are not inferred.')


def resolve_cpu_budget(*, jobs=1, workers=None, threads=None, capacity=None,
                       linear_algebra_limit=None):
    """Resolve one total concurrency budget, preserving explicit overrides.

    Automatic mode uses at most two private receiver workspaces. An even CPU
    budget is divided equally when multiple jobs exist; otherwise one worker
    receives the whole budget. Callers with measured small-matrix overhead may
    supply a separate automatic linear-algebra limit; other receiver families
    retain the full numerical budget. Explicit counts remain exact, including an
    intentional overcommit, which is reported. This preserves frozen protocols
    when replayed on a smaller machine without silently changing their counts.
    """
    jobs = _positive_integer(jobs, 'jobs')
    for value, name in ((workers, 'workers'), (threads, 'threads'),
                        (linear_algebra_limit, 'linear algebra limit')):
        if value is not None:
            _positive_integer(value, name)
    requested = dict(workers=workers, threads=threads,
                     linear_algebra_limit=linear_algebra_limit)
    if capacity is None:
        capacity = detect_cpu_budget()
    elif isinstance(capacity, int) and not isinstance(capacity, bool):
        capacity = dict(available_cpus=capacity, scope='Caller-supplied CPU budget')
    if not isinstance(capacity, dict):
        raise ValueError('capacity must be a CPU detection record or positive integer')
    budget = _positive_integer(capacity.get('available_cpus'), 'available CPUs')
    if workers is None and threads is None:
        workers = 2 if jobs >= 2 and budget >= 2 and budget % 2 == 0 else 1
        threads = budget // workers
    elif workers is None:
        workers = max(1, min(2, jobs, budget // threads))
    elif threads is None:
        threads = max(1, budget // workers)
    total = workers * threads
    return dict(schema='triad-cpu-budget-v1', available_cpus=budget,
                workers=workers, threads=threads, total_thread_budget=total,
                linear_algebra_threads=(min(linear_algebra_limit, threads)
                                        if requested['threads'] is None and linear_algebra_limit is not None
                                        else threads),
                unused_cpu_budget=max(0, budget-total),
                explicit_overcommit=total > budget, jobs=jobs,
                requested=requested, capacity=dict(capacity),
                policy='One aggregate worker × numerical-thread budget; automatic workspace fan-out is at most two. A caller-supplied linear-algebra limit applies only to automatic threads in its measured receiver family. Explicit thread counts are preserved for both stages.')


def configure_numerical_threads(allocation):
    """Apply the resolved budget once from the controller, before workers start.

    Environment variables also govern subsequently loaded libraries; threadpoolctl
    applies the same limit to BLAS/OpenMP libraries already loaded by imports.
    Global numerical settings must never be changed by a receiver job.
    """
    if threading.current_thread() is not threading.main_thread():
        raise RuntimeError('configure numerical threads from the main controller before starting workers')
    fft_threads = _positive_integer(allocation.get('threads'), 'FFT threads')
    threads = _positive_integer(allocation.get('linear_algebra_threads', fft_threads),
                                'linear algebra threads')
    for name in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS',
                 'BLIS_NUM_THREADS', 'VECLIB_MAXIMUM_THREADS', 'NUMEXPR_NUM_THREADS'):
        os.environ[name] = str(threads)
    try:
        from threadpoolctl import ThreadpoolController
    except ImportError as error:
        raise RuntimeError('automatic receiver CPU configuration requires threadpoolctl') from error
    # Load both BLAS builds before configuring; NumPy and SciPy may use different
    # providers. These imports remain lazy so resource planning stays inexpensive.
    import numpy  # noqa: F401
    import scipy.linalg  # noqa: F401
    import torch
    torch.set_num_threads(threads)
    warnings = []
    if torch.get_num_interop_threads() != 1:
        try:
            torch.set_num_interop_threads(1)
        except RuntimeError as error:
            warnings.append('PyTorch interop pool was already initialized: ' + str(error))
    global _NUMERICAL_LIMITER, _NUMERICAL_CONTROLLER
    global _CONFIGURED_LINEAR_THREADS, _NUMERICAL_CONFIGURATION
    _NUMERICAL_CONTROLLER = ThreadpoolController()
    _NUMERICAL_LIMITER = _NUMERICAL_CONTROLLER.limit(limits=threads)
    _CONFIGURED_LINEAR_THREADS = threads
    _NUMERICAL_CONFIGURATION = object()
    return dict(threads=threads, linear_algebra_threads=threads,
                fft_thread_budget=fft_threads, torch_intraop_threads=torch.get_num_threads(),
                torch_interop_threads=torch.get_num_interop_threads(),
                pools=_NUMERICAL_CONTROLLER.info(), warnings=warnings,
                scope='Main-controller linear-algebra limits applied before receiver workers. Worker initialization must apply the same fixed limits for thread-local OpenMP implementations. FFT calls use their separate explicit thread budget.')


def initialize_numerical_worker(allocation):
    """Apply the fixed controller policy once in each receiver worker thread.

    Some OpenMP runtimes keep their limits thread-local. Reuse the controller
    discovered after all numerical libraries loaded, without rediscovery,
    restoration, per-job retuning, environment changes or torch setters.
    """
    threads = _positive_integer(allocation.get('linear_algebra_threads', allocation.get('threads')),
                                'linear algebra threads')
    if _NUMERICAL_CONTROLLER is None or _NUMERICAL_CONFIGURATION is None:
        raise RuntimeError('configure numerical threads before initializing receiver workers')
    if threads != _CONFIGURED_LINEAR_THREADS:
        raise ValueError('worker linear-algebra limit differs from the fixed controller allocation')
    if getattr(_WORKER_NUMERICAL, 'configuration', None) is not _NUMERICAL_CONFIGURATION:
        # Retain the non-restoring limiter for the lifetime of this worker.
        _WORKER_NUMERICAL.limiter = _NUMERICAL_CONTROLLER.limit(limits=threads)
        _WORKER_NUMERICAL.receipt = dict(
            linear_algebra_threads=threads, pools=_NUMERICAL_CONTROLLER.info(),
            scope='Same fixed controller limit initialized once in this worker; no per-job global retuning.')
        _WORKER_NUMERICAL.configuration = _NUMERICAL_CONFIGURATION
    return dict(_WORKER_NUMERICAL.receipt)

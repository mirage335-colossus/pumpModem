"""Optional physical-core topology for native receiver benchmark reports.

The compiled receiver owns effective hardware, software-worker and memory
allocation. This helper only identifies physical cores in the process affinity
so the manual harness can request a bounded physical-core comparison.
"""
from __future__ import annotations

from pathlib import Path
import os
import sys


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

#!/usr/bin/env python3
"""Offline fixtures for advisory build resource detection and safe fallbacks."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('capacity', ROOT / 'tools/build_capacity.py')
capacity = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capacity)
MIB = capacity.MIB


class BuildCapacity(unittest.TestCase):
    def test_cpu_reserve_and_small_hosts(self):
        for cpus, expected in [(1, 1), (2, 1), (8, 7), (64, 63)]:
            self.assertEqual(capacity.select_jobs(cpus, 128 * 1024 * MIB), expected)

    def test_memory_budget_and_unknown_are_distinct(self):
        for memory, expected in [(0, 1), (512 * MIB, 1), (2048 * MIB, 2),
                                 (4096 * MIB, 4), (None, 2)]:
            self.assertEqual(capacity.select_jobs(64, memory), expected)

    def test_affinity_limits_visible_host(self):
        with patch.object(capacity.sys, 'platform', 'linux'), \
             patch.object(capacity.os, 'cpu_count', return_value=64), \
             patch.object(capacity.os, 'sched_getaffinity', return_value={2, 3, 4, 5}, create=True):
            self.assertEqual(capacity.usable_cpus(), 4)

    def test_unavailable_affinity_and_cpu_count(self):
        with patch.object(capacity.os, 'cpu_count', return_value=None), \
             patch.object(capacity.os, 'sched_getaffinity', side_effect=OSError, create=True):
            self.assertEqual(capacity.usable_cpus(), 1)

    def test_probe_failure_never_blocks_build(self):
        for error in [OSError('denied'), ValueError('unknown format'), AttributeError('unsupported')]:
            with patch.object(capacity, 'available_memory', side_effect=error):
                self.assertEqual(capacity.default_jobs(), 1)

    def fixture(self, kind='v2', mount_root='/tenant'):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        proc, mount = root / 'proc', root / 'cgroup space'
        (proc / 'self').mkdir(parents=True)
        (mount / 'job').mkdir(parents=True)
        escaped = str(mount).replace(' ', r'\040')
        if kind == 'v2':
            member, tail = '0::/tenant/job', 'cgroup2 cgroup rw'
        else:
            member, tail = '2:cpu,memory:/tenant/job', 'cgroup cgroup rw,cpu,memory'
        (proc / 'self/cgroup').write_text(member + '\n')
        (proc / 'self/mountinfo').write_text(f'1 0 0:1 {mount_root} {escaped} rw - {tail}\n')
        return proc, mount

    def test_v2_parent_limits_and_fractional_quota(self):
        proc, mount = self.fixture()
        (mount / 'cpu.max').write_text('350000 100000')
        (mount / 'job/cpu.max').write_text('max 100000')
        (mount / 'memory.max').write_text(str(4 * 1024 * MIB))
        (mount / 'memory.high').write_text(str(3 * 1024 * MIB))
        (mount / 'memory.current').write_text(str(1024 * MIB))
        (mount / 'job/memory.max').write_text('max')
        (mount / 'job/memory.current').write_text('0')
        cpus, memory = capacity.linux_limits(proc)
        self.assertEqual(cpus, [3])
        self.assertEqual(min(memory), 2 * 1024 * MIB)
        with patch.object(capacity.sys, 'platform', 'linux'), \
             patch.object(capacity, 'usable_cpus', return_value=64), \
             patch.object(capacity, 'available_memory', return_value=16 * 1024 * MIB), \
             patch.object(capacity, 'linux_limits', return_value=(cpus, memory)):
            self.assertEqual(capacity.default_jobs(), 2)

    def test_v1_exhausted_parent_and_unlimited_child(self):
        proc, mount = self.fixture('v1')
        (mount / 'cpu.cfs_quota_us').write_text('800000')
        (mount / 'cpu.cfs_period_us').write_text('100000')
        (mount / 'memory.limit_in_bytes').write_text('1024')
        (mount / 'memory.usage_in_bytes').write_text('2048')
        (mount / 'job/memory.limit_in_bytes').write_text(str((1 << 63) - 4096))
        (mount / 'job/memory.usage_in_bytes').write_text('10')
        self.assertEqual(capacity.linux_limits(proc), ([8], [0]))

    def test_malformed_and_inaccessible_cgroups(self):
        proc, mount = self.fixture()
        (mount / 'cpu.max').write_text('unknown 0')
        (mount / 'memory.current').write_text('unknown')
        with self.assertRaises(ValueError):
            capacity.linux_limits(proc)
        (mount / 'cpu.max').write_text('max 100000')
        (mount / 'memory.max').write_text('unrecognized')
        with self.assertRaises(ValueError):
            capacity.linux_limits(proc)
        with self.assertRaises(ValueError):
            capacity.linux_limits(proc / 'absent')
        (proc / 'self/cgroup').write_text('0::/outside\n')
        with self.assertRaises(ValueError):
            list(capacity.cgroup_directories(proc))

    def test_unknown_cgroup_limits_do_not_trust_host_capacity(self):
        proc, mount = self.fixture()
        (mount / 'memory.max').write_text(str(2 * 1024 * MIB))
        with self.assertRaises(ValueError):
            capacity.linux_limits(proc)
        with patch.object(capacity.sys, 'platform', 'linux'), \
             patch.object(capacity, 'usable_cpus', return_value=64), \
             patch.object(capacity, 'available_memory', return_value=128 * 1024 * MIB), \
             patch.object(capacity, 'linux_limits', side_effect=ValueError('unreadable')):
            self.assertEqual(capacity.default_jobs(), 1)

    def test_memavailable_not_free_or_total(self):
        with patch.object(capacity.sys, 'platform', 'linux'), \
             patch.object(capacity, 'read_text', return_value='MemTotal: 999 kB\nMemFree: 1 kB\nMemAvailable: 123 kB\n'):
            self.assertEqual(capacity.available_memory(), 123 * 1024)

    def test_macos_page_sizes_and_no_double_counting(self):
        for size in [4096, 16384]:
            output = (f'Mach Virtual Memory Statistics: (page size of {size} bytes)\n'
                      'Pages free: 10.\nPages inactive: 20.\nPages speculative: 3.\nPages purgeable: 9.\n')
            with patch.object(capacity.subprocess, 'check_output', return_value=output):
                self.assertEqual(capacity.macos_memory(), 33 * size)
        with patch.object(capacity.subprocess, 'check_output', side_effect=subprocess.TimeoutExpired('vm_stat', 2)):
            self.assertIsNone(capacity.macos_memory())

    def test_windows_memory_api_and_failure(self):
        def report(pointer):
            status = pointer._obj
            self.assertEqual(status.length, 64)
            status.avail_phys, status.avail_page = 4096 * MIB, 2048 * MIB
            return 1
        with patch.object(capacity.ctypes, 'windll', create=True) as api:
            api.kernel32.GlobalMemoryStatusEx.side_effect = report
            self.assertEqual(capacity.windows_memory(), 2048 * MIB)
            api.kernel32.GlobalMemoryStatusEx.side_effect = OSError
            self.assertIsNone(capacity.windows_memory())

    def test_windows_affinity_and_multiple_processor_groups(self):
        def affinity(handle, process, system):
            process._obj.value, system._obj.value = 0b1100, 0b1111
            return 1
        with patch.object(capacity.sys, 'platform', 'win32'), \
             patch.object(capacity.os, 'sched_getaffinity', side_effect=OSError, create=True), \
             patch.object(capacity.ctypes, 'windll', create=True) as api:
            api.kernel32.GetProcessAffinityMask.side_effect = affinity
            with patch.object(capacity.os, 'cpu_count', return_value=4):
                self.assertEqual(capacity.usable_cpus(), 2)
            api.reset_mock()
            with patch.object(capacity.os, 'cpu_count', return_value=128):
                self.assertEqual(capacity.usable_cpus(), 128)
            api.kernel32.GetProcessAffinityMask.assert_not_called()


if __name__ == '__main__':
    unittest.main()

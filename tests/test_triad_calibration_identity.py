"""Calibration provenance cannot turn stale or incomplete data into a match."""
from copy import deepcopy
from pathlib import Path
import os
import sys
from tempfile import NamedTemporaryFile
from unittest.mock import patch
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import triad_calibration_identity as calibration


def valid_identity():
    threads = 1
    topology = dict(status='complete', logical_cpu_ids=[0, 1], logical_cpu_count=2,
        physical_core_count=1, physical_cores=[dict(package_id=0, core_id=0, logical_cpu_ids=[0, 1])])
    budget = dict(available_cpus=2, os_available_cpus=2, quota_status='visible', quota_limits=[],
        warnings=[], topology=topology, receiver_worker_candidates=[1, 2])
    roles = {role:'/test/'+role+'.so' for role in calibration._COMPILED_ROLES}
    host_files = {path:'7'*64 for path in roles.values()}
    host_files['/test/libblas.so'] = 'e'*64
    return dict(configuration_hash='a'*64, manual_list_sha256='9'*64, precision='fp64', workers=2, fft_threads=threads,
        numerical_threads=threads, cpu=dict(model='test CPU', machine='x86_64',
            machine_id_sha256='b'*64, budget=budget),
        runtime=dict(implementation='CPython', python_version='3.13.1', python_binary_sha256='c'*64,
            numpy_version='2.0', scipy_version='1.0', torch_version='2.0', threadpoolctl_version='3.0',
            torch_build_sha256='d'*64, kernel_release='test', libc=['glibc', '2.0'],
            torch_intraop_threads=threads, torch_interop_threads=1,
            thread_environment={**{name:'1' for name in calibration._THREAD_VARIABLES},
                                **{name:None for name in calibration._OPTIONAL_THREAD_VARIABLES}},
            host_executables=host_files, native_executables={'/test/exporter':'0'*64,
                '/test/libnative.so':'8'*64}, compiled_roles=roles,
            conditional_mapping_devices=dict(host={}, native={}),
            thread_pools=[dict(user_api='blas', internal_api='openblas', prefix='libblas',
                filepath='/test/libblas.so', version=None, architecture='x86_64', num_threads=threads,
                binary_sha256='e'*64)]),
        sources={name:'f'*64 for name in calibration._expected_source_paths()}, native_binary_sha256='0'*64)


def receipt(value, pid=9, started_ns=10):
    return dict(schema=calibration.SCHEMA, status='available', identity=value,
                identity_sha256=calibration._digest(value), observation=dict(
                    process_pair=dict(controller=[pid, 400], native=[pid+1, 500], mount_namespace=[4, 6],
                        boot_id_sha256='a'*64), host_maps_sha256='b'*64, native_maps_sha256='c'*64,
                    started_ns=started_ns, ended_ns=started_ns+2))


def conditional_receipt(pid, started_ns):
    value = valid_identity()
    value['runtime']['conditional_mapping_devices']['host']['/test/libblas.so'] = dict(
        mapped_device=[0, 29], file_device=[0, 31], inode=201)
    return dict(receipt(value, pid, started_ns), status='conditional')


class CalibrationIdentityTests(unittest.TestCase):
    def test_exact_match_is_identity_only(self):
        value = valid_identity()
        compare = calibration.compare_calibration_identities
        recorded, recorded_after = receipt(value, 9, 10), receipt(value, 9, 30)
        current, current_after = receipt(value, 19, 50), receipt(value, 19, 70)
        result = compare(recorded, current, recorded_after=recorded_after, current_after=current_after)
        self.assertEqual(result['status'], 'matched')
        self.assertIn('no performance', result['scope'])
        self.assertEqual(compare(recorded, current)['status'], 'unresolved')
        changed_maps = deepcopy(current_after)
        changed_maps['observation']['native_maps_sha256'] = 'd'*64
        for after in (current, receipt(value, 20, 70), changed_maps):
            self.assertEqual(compare(recorded, current, recorded_after=recorded_after,
                                     current_after=after)['status'], 'unresolved')

    def test_changed_components_never_reuse(self):
        base = valid_identity()
        mutations = [
            lambda x:x.update(configuration_hash='1'*64),
            lambda x:x.update(manual_list_sha256='6'*64),
            lambda x:x.update(workers=1),
            lambda x:x.update(precision='fp32'),
            lambda x:x['cpu'].update(model='different CPU'),
            lambda x:x['cpu'].update(machine_id_sha256='2'*64),
            lambda x:x['runtime'].update(numpy_version='2.1'),
            lambda x:x['runtime']['thread_pools'][0].update(binary_sha256='3'*64),
            lambda x:x['runtime']['host_executables'].update({'/test/torch_cpu.so':'8'*64}),
            lambda x:x['runtime']['native_executables'].update({'/test/libnative.so':'6'*64}),
            lambda x:x['runtime']['thread_environment'].update(OMP_DYNAMIC='TRUE'),
            lambda x:x['sources'].update({'tools/triad_receiver.py':'4'*64}),
            lambda x:x.update(native_binary_sha256='5'*64),
        ]
        for mutation in mutations:
            changed = deepcopy(base)
            mutation(changed)
            with self.subTest(changed=changed):
                result = calibration.compare_calibration_identities(receipt(base), receipt(changed, 19, 50),
                    recorded_after=receipt(base, 9, 30), current_after=receipt(changed, 19, 70))
                self.assertEqual(result['status'], 'unresolved')

    def test_unknown_incomplete_or_malformed_cannot_match_itself(self):
        base = valid_identity()
        variants = []
        for mutation in (
            lambda x:x['sources'].pop('tools/triad_receiver.py'),
            lambda x:x['sources'].pop('tools/triad_weak_helper.py'),
            lambda x:x['cpu']['budget'].update(quota_status='unknown'),
            lambda x:x['cpu'].update(model='unknown'),
            lambda x:x['cpu']['budget']['topology'].update(status='partial'),
            lambda x:x['cpu']['budget']['topology'].update(physical_cores=[]),
            lambda x:x['runtime'].update(thread_pools=[]),
            lambda x:x['runtime'].update(torch_interop_threads=2),
            lambda x:x['runtime'].update(torch_intraop_threads=True),
            lambda x:x['runtime'].update(torch_version='unknown'),
            lambda x:x['runtime'].update(native_executables={}),
            lambda x:x['runtime']['host_executables'].pop(x['runtime']['compiled_roles']['scipy_fft']),
            lambda x:x['runtime']['thread_environment'].update(OMP_NUM_THREADS=None),
            lambda x:x['runtime']['thread_environment'].pop('OMP_PROC_BIND'),
            lambda x:x['runtime']['thread_pools'][0].update(binary_sha256='unknown'),
            lambda x:x['runtime']['thread_pools'][0].update(num_threads=True),
            lambda x:x.update(workers=True),
        ):
            value = deepcopy(base)
            mutation(value)
            variants.append(receipt(value))
        variants += [{}, dict(status='unresolved'), dict(schema=calibration.SCHEMA, status='available',
            identity=base, identity_sha256='0'*64)]
        for invalid in variants:
            with self.subTest(invalid=invalid):
                after = dict(invalid, observation=receipt(base, 9, 30)['observation'])
                result = calibration.compare_calibration_identities(invalid, invalid,
                    recorded_after=after, current_after=after)
                self.assertEqual(result['status'], 'unresolved')

    def test_capture_requires_a_live_canonical_native_service(self):
        from triad_cases import load_cases
        current = load_cases('_tests-RobustModem.md')['cases'][0]['configuration_hash']
        result = calibration.capture_calibration_identity(exporter='/does/not/exist',
            configuration_hash=current, workers=1)
        self.assertEqual(result['status'], 'unresolved')

    def test_configuration_not_in_fresh_list_cannot_be_fingerprinted(self):
        with patch('triad_cases.load_cases', return_value=dict(cases=[dict(configuration_hash='b'*64)])):
            result = calibration.capture_calibration_identity(exporter='unused',
                configuration_hash='a'*64, workers=1)
        self.assertEqual(result['status'], 'unresolved')

    def test_unproved_mapping_device_never_matches_even_with_same_inode(self):
        with NamedTemporaryFile() as stream:
            stat = os.fstat(stream.fileno())
            device_major, device_minor = os.major(stat.st_dev), os.minor(stat.st_dev)
            row = (f'1000-2000 r-xp 00000000 {device_major+1:02x}:{device_minor:02x} '
                   f'{stat.st_ino} {stream.name}\n')
            with patch.object(calibration.Path, 'read_text', return_value=row):
                with self.assertRaisesRegex(ValueError, 'no longer matches'):
                    calibration._mapped_executables(os.getpid())

    def test_conditional_mapping_requires_live_path_links_and_stable_inventory(self):
        with NamedTemporaryFile() as stream:
            metadata = os.fstat(stream.fileno())
            mapped_major = os.major(metadata.st_dev)+1
            minor = os.minor(metadata.st_dev)
            row = f'001000-002000 r-xp 00000000 {mapped_major:02x}:{minor:02x} {metadata.st_ino} {stream.name}\n'
            with patch.object(calibration.Path, 'read_text', return_value=row), \
                    patch.object(calibration.os, 'readlink', return_value=stream.name) as readlink:
                files, devices, _ = calibration._mapped_executables(os.getpid(), allow_conditional=True)
            self.assertEqual(set(files), {stream.name})
            self.assertEqual(devices[stream.name]['mapped_device'], [mapped_major, minor])
            self.assertEqual([call.args[0].name for call in readlink.call_args_list], ['1000-2000'] * 2)
            with patch.object(calibration.Path, 'read_text', return_value=row), \
                    patch.object(calibration.os, 'readlink', return_value=stream.name+' (deleted)'):
                with self.assertRaisesRegex(ValueError, 'path cannot be verified'):
                    calibration._mapped_executables(os.getpid(), allow_conditional=True)
            with patch.object(calibration.Path, 'read_text', side_effect=[row, row.replace('002000', '003000')]), \
                    patch.object(calibration.os, 'readlink', return_value=stream.name):
                with self.assertRaisesRegex(ValueError, 'mappings changed'):
                    calibration._mapped_executables(os.getpid(), allow_conditional=True)

    def test_conditional_match_requires_both_same_process_observation_pairs(self):
        recorded, recorded_after = conditional_receipt(9, 10), conditional_receipt(9, 30)
        current, current_after = conditional_receipt(19, 50), conditional_receipt(19, 70)
        compare = calibration.compare_calibration_identities
        self.assertEqual(compare(recorded, current, allow_conditional=True)['status'], 'unresolved')
        self.assertEqual(compare(recorded, current, recorded_after=recorded_after,
                                 current_after=current_after)['status'], 'unresolved')
        result = compare(recorded, current, allow_conditional=True,
            recorded_after=recorded_after, current_after=current_after)
        self.assertEqual(result['status'], 'conditionally_matched')
        self.assertIn('trusted shared mount namespace', result['scope'])
        changed_maps = deepcopy(current_after)
        changed_maps['observation']['host_maps_sha256'] = 'd'*64
        for after in (current, conditional_receipt(20, 70), changed_maps):
            self.assertEqual(compare(recorded, current, allow_conditional=True,
                recorded_after=recorded_after, current_after=after)['status'], 'unresolved')
        falsely_exact = dict(recorded, status='available')
        self.assertEqual(compare(falsely_exact, falsely_exact)['status'], 'unresolved')


if __name__ == '__main__':
    unittest.main()

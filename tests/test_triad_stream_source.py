#!/usr/bin/env python3
"""Focused native pull-source checks; no multi-day waveform files are made."""
import json
import math
from pathlib import Path
import subprocess
import sys
import struct
import tempfile
import unittest


def complex_values(data):
    return [complex(real, imag) for real, imag in struct.iter_unpack('<dd', data)]

BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None
KEY_FIXTURE = (Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith('-')
               else BINARY.with_name('test_triad_keyfile_fixture') if BINARY else None)
COMMON = ['--case', 'hf', '--sample-rate', '6000', '--rate', '100', '--added-symbols', '1',
          '--epoch', '1793850000', '--key-seed', '72001', '--seed', '7', '--seconds', '8',
          '--control', 'flat', '--oscillator-effects', 'off', '--arrival-seconds', '0',
          '--fhss', 'off', '--helper', 'off', '--cn0', '80']


def exact(stream, count):
    parts = []
    while count:
        part = stream.read(count)
        if not part:
            raise RuntimeError('truncated native stream')
        parts.append(part)
        count -= len(part)
    return b''.join(parts)


class Source:
    def __init__(self, args=()):
        self.process = subprocess.Popen([str(BINARY), *COMMON, '--stream-server', *args],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            self.plan = self.line()
            self.descriptor = self.line()
        except Exception:
            self.process.kill()
            _, stderr = self.process.communicate()
            raise RuntimeError('stream startup failed: '+stderr.decode(errors='replace'))

    def line(self):
        value = self.process.stdout.readline()
        if not value:
            raise RuntimeError('missing native response')
        return json.loads(value)

    def command(self, value):
        self.process.stdin.write((value+'\n').encode())
        self.process.stdin.flush()
        return self.line()

    def next(self, count):
        header = self.command('next '+str(count))
        n = header['count']
        return header, exact(self.process.stdout, 16*n), exact(self.process.stdout, n)

    def basis(self, first, count, candidates):
        rows = '\n'.join(' '.join(map(str, row)) for row in candidates)
        header = self.command(f'basis {first} {count} {len(candidates)}\n'+rows)
        data = exact(self.process.stdout, 16*count*len(candidates))
        values = complex_values(data)
        return header, [values[i*count:(i+1)*count] for i in range(len(candidates))]

    def basis_map(self, first, offsets, candidates, audio_center=0):
        count = len(offsets)
        rows = '\n'.join(' '.join(map(str, row)) for row in candidates)
        self.process.stdin.write((f'basis-map {first} {count} {len(candidates)} {audio_center}\n'+rows+'\n').encode())
        self.process.stdin.write(struct.pack('<'+'d'*count, *offsets))
        self.process.stdin.flush()
        header = self.line()
        values = complex_values(exact(self.process.stdout, 16*count*len(candidates)))
        return header, [values[i*count:(i+1)*count] for i in range(len(candidates))]

    def close(self):
        if self.process.poll() is not None:
            return None
        result = self.command('quit')
        self.process.stdin.close()
        self.process.wait(timeout=10)
        error = self.process.stderr.read().decode(errors='replace')
        self.process.stdout.close()
        self.process.stderr.close()
        if self.process.returncode:
            raise RuntimeError(error)
        return result

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        if kind:
            self.process.kill()
            self.process.communicate()
        else:
            self.close()


@unittest.skipIf(BINARY is None, 'native binary path required')
class PullSourceTests(unittest.TestCase):

    def test_plan_publishes_format_and_physics_controls(self):
        def plan(*extra):
            result = subprocess.run([str(BINARY), *COMMON, '--plan-only', *extra],
                                    capture_output=True, check=True, timeout=90)
            return json.loads(result.stdout)

        defaults = plan()
        self.assertTrue(defaults['format']['encrypt'])
        self.assertTrue(defaults['format']['added_interleave'])
        self.assertEqual(defaults['physics']['path_loss_db'], 180)
        self.assertEqual(defaults['physics']['noise_floor_dbm_hz'], -164)
        configured = plan('--encrypt', 'off', '--added-interleave', 'off', '--path-loss-db', '123.5')
        self.assertFalse(configured['format']['encrypt'])
        self.assertFalse(configured['format']['added_interleave'])
        self.assertEqual(configured['physics']['path_loss_db'], 123.5)
        self.assertEqual(configured['physics']['physical_data_cn0_db_hz'],
                         defaults['physics']['physical_data_cn0_db_hz'] + 56.5)

    def test_short_message_plan_metadata_uses_short_prefix_codec(self):
        message = 'fox'
        result = subprocess.run([str(BINARY), *COMMON, '--plan-only', '--short-message', message],
                                capture_output=True, check=True, timeout=90)
        plan = json.loads(result.stdout)
        self.assertEqual(plan['short_message'], {
            'text': message, 'source_bytes': 3, 'encoded_bits': 22, 'codec': 'short-prefix'})
        self.assertEqual(plan['transmit_calendar']['bits'], 22)
        calendar = plan['transmit_calendar']
        self.assertAlmostEqual(calendar['single_bit_seconds'],
                               calendar['first_helper_end_seconds']-calendar['first_helper_start_seconds'])
        self.assertGreater(plan['transmit_calendar']['short_message_seconds'],
                           plan['transmit_calendar']['single_bit_seconds'])
        helper_plan = subprocess.run([str(BINARY), *COMMON, '--plan-only', '--helper', 'on',
                                      '--short-message', message], capture_output=True,
                                     check=True, timeout=90)
        helper_calendar = json.loads(helper_plan.stdout)['transmit_calendar']
        self.assertAlmostEqual(helper_calendar['single_bit_seconds'],
                               helper_calendar['first_helper_end_seconds']-helper_calendar['first_helper_start_seconds'])
        self.assertGreater(helper_calendar['first_support_start_seconds'],
                           helper_calendar['first_helper_start_seconds'])
        too_long = subprocess.run([str(BINARY), *COMMON, '--plan-only', '--short-message', 'x'*257],
                                  capture_output=True, timeout=10)
        self.assertNotEqual(too_long.returncode, 0)
        self.assertIn(b'at most256 source bytes', too_long.stderr)

    def test_zero_helper_power_is_truthful_when_helper_is_disabled(self):
        args = ['--plan-only', '--helper', 'off', '--helper-power-w', '0']
        result = subprocess.run([str(BINARY), *COMMON, *args], capture_output=True,
                                check=True, timeout=90)
        plan = json.loads(result.stdout)
        self.assertEqual(plan['physics']['effective_helper_w'], 0)
        self.assertIsNone(plan['physics']['effective_helper_cn0_db_hz'])
        self.assertFalse(plan['helper_placement']['enabled'])
        self.assertTrue(plan['helper_placement']['feasible_at_requested_if'])
        self.assertEqual(plan['export_constraints']['fixed_helper_lead_seconds'], 0)
        with Source(['--helper-power-w', '0']) as source:
            self.assertFalse(source.plan['helper_placement']['enabled'])
            self.assertEqual(source.plan['physics']['effective_helper_w'], 0)
            self.assertIsNone(source.plan['physics']['effective_helper_cn0_db_hz'])
            self.assertEqual(source.descriptor['helper_high_hz'], None)
            self.assertLess(source.descriptor['data_offset_samples'], 6000)
        failed = subprocess.run([str(BINARY), *COMMON, '--helper', 'on', '--plan-only', '--helper-power-w', '0'],
                                capture_output=True, timeout=10)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn(b'zero requires --helper off', failed.stderr)

    def test_thermal_noise_switch_preserves_nominal_link_and_basis(self):
        candidate = [[0, 0, 0, 0, 0, 0, 0]]
        with Source() as noisy:
            self.assertTrue(noisy.plan['physics']['thermal_noise_enabled'])
            self.assertTrue(noisy.descriptor['thermal_noise_enabled'])
            self.assertGreater(noisy.descriptor['noise_variance'], 0)
            self.assertEqual(noisy.descriptor['noise_variance'], noisy.descriptor['nominal_noise_variance'])
            noisy_physics = dict(noisy.plan['physics'])
            _, noisy_basis = noisy.basis(0, 4, candidate)
        with Source(['--thermal-noise', 'off']) as clean:
            self.assertFalse(clean.plan['physics']['thermal_noise_enabled'])
            self.assertFalse(clean.descriptor['thermal_noise_enabled'])
            self.assertEqual(clean.descriptor['noise_variance'], 0)
            self.assertGreater(clean.descriptor['nominal_noise_variance'], 0)
            clean_physics = dict(clean.plan['physics'])
            _, clean_basis = clean.basis(0, 4, candidate)
        noisy_physics.pop('thermal_noise_enabled')
        clean_physics.pop('thermal_noise_enabled')
        self.assertEqual(noisy_physics, clean_physics)
        self.assertEqual(noisy_basis, clean_basis)

    def test_shared_raw_basis_bank_matches_scalar_mapped_reference_and_ordinals(self):
        rows = [(bit, epoch, 0, 0, 0, 0, 0) for epoch in (-6, 0, 6) for bit in (0, 1)]
        with Source(['--stream-kind', 'emitted', '--symbol-ordinal', '91']) as source:
            self.assertEqual(source.descriptor['symbol_ordinal'], 91)
            self.assertEqual(source.plan['format']['symbol_ordinal'], 91)
            first, count = source.descriptor['source_samples']//3, 513
            header, bank = source.basis(first, count, rows)
            self.assertTrue(header['shared_geometry_bank'])
            reference_header, reference = source.basis_map(first, list(range(count)), rows)
            self.assertFalse(reference_header['shared_geometry_bank'])
            self.assertEqual(bank, reference)
            _, repeated = source.basis(first, count, rows)
            self.assertEqual(bank, repeated)
        with Source(['--stream-kind', 'emitted', '--symbol-ordinal', '92']) as source:
            _, different = source.basis(first, count, rows)
            self.assertNotEqual(bank, different)

    def test_hf_fhss_public_basis_bank_batches_within_workspace(self):
        # Regression for the real HF FHSS suite row: all 26 public bit/epoch
        # alternatives must fit the bounded workspace without dropping epochs.
        rows = [(bit, epoch, 0, 0, 0, 0, 0)
                for epoch in range(-6, 7) for bit in (0, 1)]
        with Source(['--rate', '36', '--added-symbols', '10', '--fhss', 'on',
                     '--helper-power-w', '0']) as source:
            first = source.descriptor['data_offset_samples'] + 32
            header, bank = source.basis(first, 4, rows)
            self.assertTrue(header['shared_geometry_bank'])
            self.assertLessEqual(header['workspace_bytes'], source.descriptor['workspace_budget_bytes'])
            self.assertGreater(header['template_cache_evictions'], 0)
            _, repeated = source.basis(first, 4, rows)
            self.assertEqual(bank, repeated)
            selected = [rows[i] for i in (0, 12, 25)]
            _, scalar = source.basis_map(first, [0., 1., 2., 3.], selected)
            self.assertEqual([bank[i] for i in (0, 12, 25)], scalar)

    def test_mapped_basis_protocol_preserves_uniform_and_fractional_calendar(self):
        with Source(['--stream-kind', 'emitted']) as source:
            d = source.descriptor
            self.assertTrue(d['mapped_basis_supported'])
            self.assertGreater(d['nominal_triplet_bandwidth_hz'], 0)
            first = d['data_offset_samples'] + 12000
            rows = [[b, 0, d['data_offset_samples']/6000, 0, 0, 0, 0] for b in (0, 1)]
            _, regular = source.basis(first, 19, rows)
            _, mapped = source.basis_map(first, list(range(19)), rows)
            self.assertEqual(regular, mapped)
            offsets = [i*1.0002+.125 for i in range(19)]
            _, whole = source.basis_map(first, offsets, rows, 1725)
            _, left = source.basis_map(first, offsets[:7], rows, 1725)
            _, right = source.basis_map(first+7, [x-7 for x in offsets[7:]], rows, 1725)
            self.assertEqual(whole, [a+b for a, b in zip(left, right)])





    def test_emitted_basis_and_random_access_continuity(self):
        with Source(['--stream-kind', 'emitted']) as source:
            d = source.descriptor
            self.assertFalse(d['simulation_channel_constructed'])
            self.assertFalse(d['conditional_gaussian_null_supported'])
            offset = d['data_offset_samples']
            position = 0
            wanted = offset+4096
            emitted = []
            while position < wanted:
                header, data, _ = source.next(min(16384, wanted-position))
                values = complex_values(data)
                if position+len(values) > offset:
                    emitted.extend(values[max(0, offset-position):])
                position += len(values)
            candidate = [0, 0, offset/d['sample_rate_hz'], 0, 0, 0, 0]
            _, basis = source.basis(offset, 4096, [candidate])
            self.assertEqual(len(basis[0]), len(emitted))
            self.assertTrue(all(abs(a-b) <= 1e-10*(1+abs(b)) for a,b in zip(basis[0],emitted)))
            _, whole = source.basis(offset+33, 1003, [candidate])
            _, first = source.basis(offset+33, 401, [candidate])
            _, last = source.basis(offset+434, 602, [candidate])
            self.assertEqual(whole, [a+b for a,b in zip(first,last)])

    def test_multiday_prefix_has_bounded_preparation(self):
        with Source(['--seconds', str(38*86400), '--stream-kind', 'emitted']) as source:
            d = source.descriptor
            self.assertGreater(d['total_samples'], 38*86400*6000)
            self.assertLessEqual(d['power']['data_reference_sampled_tiles'], 16)
            self.assertLessEqual(d['power']['data_reference_nodes'], 16*2048)
            self.assertLess(d['fixed_workspace_bytes'], d['workspace_budget_bytes'])
            header, data, valid = source.next(23)
            self.assertEqual((header['first_sample'], header['count']), (0, 23))
            self.assertFalse(header['complete'])
            self.assertEqual(len(data), 23*16)
            self.assertEqual(valid, bytes([1])*23)
            first = int(30*86400*d['sample_rate_hz'])
            _, whole = source.basis(first, 71, [[1, 0, d['data_offset_samples']/6000, .001, .01, .001, .002]])
            self.assertTrue(all(math.isfinite(v.real) and math.isfinite(v.imag) for row in whole for v in row))
            self.assertGreater(max(abs(v) for row in whole for v in row), 0)

    def test_oversized_command_is_rejected(self):
        with Source() as source:
            _, error = source.process.communicate(b'x'*1025+b'\n', timeout=10)
            self.assertNotEqual(source.process.returncode, 0)
            self.assertIn(b'exceeds1024bytes', error)

    def test_key_and_command_guards(self):
        # Fresh diagnostic source noise is not the declared thermal Gaussian
        # null, even when no Helper signal is present.
        with Source(['--diagnostic-iq-noise-bits', '10']) as source:
            self.assertFalse(source.descriptor['conditional_gaussian_null_supported'])
        # Operational streams must not silently use the fixture key.
        args = [value for value in COMMON]
        i = args.index('--key-seed'); del args[i:i+2]
        failed = subprocess.run([str(BINARY), *args, '--stream-server'], input=b'', capture_output=True, timeout=10)
        self.assertNotEqual(failed.returncode, 0)
        self.assertIn(b'requires --key-file', failed.stderr)
        with tempfile.TemporaryDirectory() as directory:
            key = Path(directory)/'key'; key.write_bytes(bytes(range(32)))
            old = subprocess.run([str(BINARY), *args, '--stream-server', '--key-file', str(key)],
                                 input=b'', capture_output=True, timeout=10)
            self.assertNotEqual(old.returncode, 0, 'single-lane raw key must be rejected')
            self.assertIn(b'invalid keyfile length', old.stderr)
            key.write_bytes(bytes(range(128)))
            raw = subprocess.run([str(BINARY), *args, '--stream-server', '--key-file', str(key)],
                                 input=b'', capture_output=True, timeout=10)
            self.assertNotEqual(raw.returncode, 0, 'raw dual-lane key must be rejected')
            self.assertIn(b'unsupported keyfile format', raw.stderr)
        # The generator is linked only into this test target. Its deterministic
        # synthetic audio and keyfiles are public fixtures, not operational keys.
        self.assertIsNotNone(KEY_FIXTURE, 'test-only keyfile generator is required')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            single, multi = root/'single.key', root/'multi.key'
            subprocess.run([str(KEY_FIXTURE), str(single), 'single'], check=True, timeout=90)
            def accepted(key, *extra, data_offset=None, sample_rate=None):
                # The bounded basis command can inspect the DATA coordinate
                # directly, without rendering the 38-second lead-in.
                command = (f'basis {data_offset} 4096 1\n'
                           f'0 0 {data_offset/sample_rate} 0 0 0 0\nquit\n').encode() if data_offset is not None else b'quit\n'
                proc = subprocess.run([str(BINARY), *args, '--stream-server', '--key-file', str(key), *extra],
                                      input=command, capture_output=True, timeout=180)
                self.assertEqual(proc.returncode, 0, proc.stderr.decode(errors='replace'))
                lines = proc.stdout.split(b'\n', 3)
                self.assertEqual(len(lines), 4, 'keyfile startup omitted its descriptor or block')
                descriptor = json.loads(lines[1])
                self.assertEqual(descriptor['key_source'], 'keyfile')
                if data_offset is None:
                    self.assertTrue(json.loads(lines[2])['stopped'])
                    return descriptor
                self.assertEqual(descriptor['data_offset_samples'], data_offset)
                self.assertEqual(descriptor['sample_rate_hz'], sample_rate)
                header = json.loads(lines[2])
                self.assertEqual((header['count'], header['columns'], header['first_sample']), (4096, 1, data_offset))
                self.assertGreaterEqual(len(lines[3]), 16*4096, 'truncated DATA basis block')
                data = lines[3][:16*4096]
                self.assertTrue(any(value != 0 for value in complex_values(data)), 'DATA basis was silent')
                return data
            descriptor = accepted(single)
            subprocess.run([str(KEY_FIXTURE), str(multi), 'multi'], check=True, timeout=90)
            for name_args, diagnostic in (((), b'--key-name is required'),
                                          (('--key-name', 'Missing'), b'named key is not present')):
                failed = subprocess.run([str(BINARY), *args, '--stream-server', '--key-file', str(multi), *name_args],
                                        input=b'', capture_output=True, timeout=90)
                self.assertNotEqual(failed.returncode, 0)
                self.assertIn(diagnostic, failed.stderr)
            coordinate = dict(data_offset=descriptor['data_offset_samples'], sample_rate=descriptor['sample_rate_hz'])
            first = accepted(multi, '--key-name', 'First', **coordinate)
            first_again = accepted(multi, '--key-name', 'First', **coordinate)
            second = accepted(multi, '--key-name', 'Second', **coordinate)
            self.assertEqual(first, first_again, 'named selection was not reproducible')
            self.assertNotEqual(first, second, 'named selection reused another entry\'s waveform')
            with multi.open('r+b') as handle:
                handle.seek(-1, 2); original = handle.read(1)
                handle.seek(-1, 2); handle.write(bytes([original[0] ^ 1]))
            damaged = subprocess.run([str(BINARY), *args, '--stream-server', '--key-file', str(multi), '--key-name', 'Second'],
                                     input=b'', capture_output=True, timeout=90)
            self.assertNotEqual(damaged.returncode, 0)
            self.assertIn(b'authentication failed', damaged.stderr)
        with Source() as source:
            _, error = source.process.communicate(b'next 16385\n', timeout=10)
            self.assertNotEqual(source.process.returncode, 0)
            self.assertIn(b'COUNT', error)
        with Source() as source:
            source.process.stdin.write(b'basis 0 4 1\n0 7 0 0 0 0 0\n')
            source.process.stdin.flush()
            source.process.wait(timeout=10)
            self.assertNotEqual(source.process.returncode, 0)
            self.assertIn(b'public alternatives', source.process.stderr.read())
            source.process.stdin.close(); source.process.stdout.close(); source.process.stderr.close()


if __name__ == '__main__':
    unittest.main()

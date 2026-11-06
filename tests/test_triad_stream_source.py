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
    def test_clock_follow_is_explicit_bounded_and_retains_nominal_basis(self):
        args = ['--stream-kind', 'emitted', '--rate', '.0036', '--seconds', '1000000',
                '--clock-follow-bounded']
        with Source(args) as source:
            d = source.descriptor
            self.assertTrue(d['clock_follow_bounded'])
            self.assertEqual(d['clock_follow_endpoint_padding_seconds'], 2)
            epoch = d['epoch']
            r = source.command('clock 2')
            self.assertTrue(r['accepted'])
            self.assertEqual(r['offset_seconds'], 0)
            source.next(4096)
            r = source.command('clock -1')
            self.assertTrue(r['accepted'])
            self.assertGreaterEqual(r['rate_scale'], 1)
            self.assertFalse(source.command('clock 2.1')['accepted'])
            self.assertEqual(d['epoch'], epoch)
            # An independently started RX gets the public calendar and nominal
            # templates, without being told the private TX correction trace.
            sample = d['data_offset_samples'] + 100000 * 6000
            candidates = [(0, 0, d['data_offset_samples']/6000, 0, 0, 0, 0)]
            _, rows = source.basis(sample, 8, candidates)
            self.assertEqual(len(rows[0]), 8)
            with Source(args) as receiver:
                for field in ('epoch', 'total_samples', 'data_offset_samples', 'source_samples'):
                    self.assertEqual(receiver.descriptor[field], d[field])
                _, independent = receiver.basis(sample, 8, candidates)
                self.assertEqual(rows, independent)
        with Source(['--stream-kind', 'emitted']) as source:
            self.assertFalse(source.command('clock 1')['accepted'])

    def test_clock_follow_rejects_unqualified_regimes(self):
        for extra in ([], ['--helper','on'], ['--fhss','on'], ['--burst-wait-tiles','3']):
            args = [] if not extra else ['--rate','.0036','--seconds','1000000',*extra]
            p = subprocess.run([str(BINARY), *COMMON, '--stream-server', '--stream-kind','emitted',
                '--clock-follow-bounded', *args], input=b'quit\n', capture_output=True, timeout=10)
            self.assertNotEqual(p.returncode, 0)
            self.assertTrue(b'bounded clock following requires' in p.stderr or
                            b'center-aligned FHSS requires' in p.stderr)

    def test_pattern_identity_propagates_and_unknown_choices_fail(self):
        actual = []
        for revision, depth in (('independent-apsk-v2', .25), ('legacy-v1', .4)):
            args = ['--stream-kind', 'emitted', '--added-symbols', '10']
            if revision == 'legacy-v1':
                args += ['--pattern-revision', revision, '--added-amplitude-depth', str(depth)]
            with Source(args) as source:
                d = source.descriptor
                for info in (source.plan['format'], d, d['power']):
                    self.assertEqual(info['pattern_revision'], revision)
                    self.assertEqual(info['added_amplitude_depth'], depth)
                first = d['data_offset_samples'] + d['sample_rate_hz']
                _, basis = source.basis(first, 71, [[0, 0, d['data_offset_samples']/d['sample_rate_hz'], 0, 0, 0, 0]])
                actual.append(basis)
        self.assertNotEqual(actual[0], actual[1])
        for flags in (['--pattern-revision', 'unknown'], ['--added-amplitude-depth', '0'],
                      ['--added-amplitude-depth', '1'], ['--added-amplitude-depth', 'nan']):
            failed = subprocess.run([str(BINARY), *COMMON, '--stream-server', *flags],
                                    input=b'', capture_output=True, timeout=10)
            self.assertNotEqual(failed.returncode, 0)

    def test_channel_matches_finite_export_and_chunks(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/'reference'
            subprocess.run([str(BINARY), *COMMON, '--power-reference', 'whole-active', '--output', str(output)],
                           check=True, capture_output=True, timeout=60)
            metadata = json.loads((output/'manifest.json').read_text())
            self.assertEqual(metadata['format']['pattern_revision'], 'independent-apsk-v2')
            self.assertEqual(metadata['format']['added_amplitude_depth'], .25)
            expected = (output/'observation.c128').read_bytes()
            expected_validity = (output/'validity.u8').read_bytes()
            with Source(['--power-reference', 'whole-active']) as source:
                self.assertTrue(source.descriptor['conditional_gaussian_null_supported'])
                raw, validity, position = [], [], 0
                for iteration in range(10000):
                    header, data, valid = source.next((17, 4096, 16384)[iteration % 3])
                    self.assertEqual(header['first_sample'], position)
                    position += header['count']
                    raw.append(data); validity.append(valid)
                    if header['complete']:
                        break
                else:
                    self.fail('short source did not finish')
                self.assertEqual(position, source.descriptor['total_samples'])
                self.assertEqual(b''.join(raw), expected)
                self.assertEqual(b''.join(validity), expected_validity)
                empty, data, valid = source.next(1)
                self.assertEqual((empty['count'], data, valid), (0, b'', b''))

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
            proc = subprocess.Popen([str(BINARY), *args, '--stream-server', '--key-file', str(key)],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            json.loads(proc.stdout.readline()); descriptor=json.loads(proc.stdout.readline())
            self.assertEqual(descriptor['key_source'], 'raw-key-file')
            out, error = proc.communicate(b'next 16385\n', timeout=10)
            self.assertNotEqual(proc.returncode, 0)
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

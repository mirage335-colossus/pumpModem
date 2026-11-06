#!/usr/bin/env python3
"""Cheap source-probe contract checks; no sensitivity or long campaign."""
import cmath
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None


def read_complex(path):
    return [complex(a, b) for a, b in struct.iter_unpack('<dd', path.read_bytes())]


@unittest.skipIf(BINARY is None, 'native probe executable required')
class PatternProbeTests(unittest.TestCase):
    def probe(self, path, *args):
        command = [str(BINARY), '--output', str(path), '--shaping', 'legacy', *map(str, args)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads((path/'manifest.json').read_text())

    def test_shapes_exact_composite_and_coefficient_power(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'probe'
            m = self.probe(path, '--window-seconds', '.8')
            n = m['rows']; count = m['coefficient_count']
            self.assertEqual(m['format']['pattern_revision'], 'independent-apsk-v2')
            self.assertEqual(m['dtype'], '<c16')
            self.assertEqual(m['composite_shape'], [2, n])
            self.assertEqual(m['carrier_shape'], [2, 3, n])
            self.assertEqual(m['coefficients_shape'], [2, count, 3])
            self.assertFalse(m['channel_applied'])
            self.assertEqual(m['envelope_shape'], [n])
            gate = [x[0] for x in struct.iter_unpack('<d', (path/m['envelope_file']).read_bytes())]
            self.assertEqual(gate, [1.]*n)
            x = read_complex(path/m['composite_file'])
            c = read_complex(path/m['carrier_file'])
            coeff = read_complex(path/m['coefficients_file'])
            self.assertEqual(len(x), 2*n)
            self.assertEqual(len(c), 6*n)
            self.assertEqual(len(coeff), 6*count)
            for b in range(2):
                for j in range(n):
                    t = (m['first_sample']+j-m['calendar_origin_sample'])/m['sample_rate_hz']
                    expected = sum(c[(3*b+k)*n+j] * cmath.exp(2j*math.pi*(k-1)*m['carrier_spacing_hz']*t)
                                   for k in range(3))
                    self.assertLess(abs(x[b*n+j]-expected), 1e-9)
                for row in range(count):
                    value = coeff[(b*count+row)*3:(b*count+row+1)*3]
                    self.assertAlmostEqual(sum(abs(z)**2 for z in value), 3., places=11)
                    self.assertGreater(min(abs(z) for z in value), 0)
            self.assertNotEqual(x[:n], x[n:])

    def test_window_replay_is_independent_of_export_chunk_and_revision_is_explicit(self):
        with tempfile.TemporaryDirectory() as tmp:
            parent = Path(tmp)
            full = self.probe(parent/'full', '--start-seconds', '20', '--window-seconds', '2')
            part = self.probe(parent/'part', '--start-seconds', '20.5', '--window-seconds', '.5')
            old = self.probe(parent/'old', '--start-seconds', '20', '--window-seconds', '2',
                             '--pattern-revision', 'legacy-v1')
            n, small = full['rows'], part['rows']
            offset = part['first_sample']-full['first_sample']
            a = read_complex(parent/'full'/full['composite_file'])
            b = read_complex(parent/'part'/part['composite_file'])
            for bit in range(2):
                self.assertEqual(a[bit*n+offset:bit*n+offset+small], b[bit*small:(bit+1)*small])
            self.assertEqual(old['format']['pattern_revision'], 'legacy-v1')
            self.assertNotEqual((parent/'full'/full['composite_file']).read_bytes(),
                                (parent/'old'/old['composite_file']).read_bytes())

    def test_multiday_low_rate_window_uses_actual_sample_grid(self):
        with tempfile.TemporaryDirectory() as tmp:
            m = self.probe(Path(tmp)/'low', '--rate', '.0036', '--added', '1',
                           '--sample-rate', '1', '--sample-rate-denominator', '64',
                           '--seconds', '3283200', '--window-seconds', '64000')
            self.assertEqual(m['rows'], 1000)
            self.assertEqual(m['sample_rate_hz'], 1/64)
            self.assertGreater(m['source_samples']/m['sample_rate_hz'], 38*86400)
            self.assertLess(m['artifact_bytes'], 1024*1024)

    def test_invalid_work_or_identity_is_rejected_before_output_creation(self):
        cases = [('--window-seconds', '1000'), ('--window-seconds', '-1'),
                 ('--window-seconds', 'nan'), ('--added-amplitude-depth', '0'),
                 ('--added-amplitude-depth', '1'), ('--pattern-revision', 'unknown')]
        with tempfile.TemporaryDirectory() as tmp:
            for i, args in enumerate(cases):
                path = Path(tmp)/str(i)
                result = subprocess.run([str(BINARY), '--output', str(path), '--shaping', 'legacy', *args],
                                        capture_output=True, text=True, timeout=10)
                self.assertNotEqual(result.returncode, 0, args)
                self.assertFalse(path.exists(), args)

    def test_existing_output_is_not_overwritten(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'existing'; path.mkdir()
            marker = path/'marker'; marker.write_text('preserve')
            result = subprocess.run([str(BINARY), '--output', str(path), '--shaping', 'legacy'],
                                    capture_output=True, text=True, timeout=10)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(marker.read_text(), 'preserve')
            self.assertEqual(list(path.iterdir()), [marker])


if __name__ == '__main__':
    unittest.main()

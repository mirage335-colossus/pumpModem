#!/usr/bin/env python3
"""Focused native pull-source checks; no multi-day waveform files are made."""
import json
import hashlib
from fractions import Fraction
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

    def test_standalone_short_codec_preserves_bits_and_filters_received_text(self):
        def codec(operation, value, *, check=True):
            return subprocess.run([str(BINARY), operation, value], capture_output=True,
                                  check=check, timeout=10)
        fox = json.loads(codec('--short-encode', 'fox').stdout)
        self.assertEqual(fox['encoded_bits'], 22)
        self.assertEqual(json.loads(codec('--short-decode', fox['bits']).stdout)['text'], 'fox')
        self.assertEqual(json.loads(codec('--short-encode', 'e').stdout)['bits'], '001')
        self.assertEqual(json.loads(codec('--short-decode', '001').stdout)['text'], 'e')
        # The exact canonical literal for NUL is valid source data, but must
        # never escape the shared receive-presentation filter as raw text.
        filtered = json.loads(codec('--short-decode', '1111100000000').stdout)
        self.assertEqual((filtered['decoded_bytes'], filtered['text']), (1, '_'))
        self.assertEqual(json.loads(codec('--short-decode', '').stdout)['text'], '')
        for bad in ('0010', '001x', '1' * 209, '000' * 17):
            self.assertNotEqual(codec('--short-decode', bad, check=False).returncode, 0)
        self.assertNotEqual(codec('--short-encode', 'x' * 17, check=False).returncode, 0)
        mixed = subprocess.run([str(BINARY), '--short-encode', 'e', '--plan-only'],
                               capture_output=True, timeout=10)
        self.assertNotEqual(mixed.returncode, 0)

    def test_sequence_bits_and_public_template_addresses_match_each_slot(self):
        bits = '001'
        with Source(['--stream-kind', 'emitted', '--data-bits', bits,
                     '--thermal-noise', 'off']) as source:
            d = source.descriptor
            fs = d['sample_rate_hz']
            self.assertTrue(d['basis_address_supported'])
            self.assertNotIn('data_bits', d)
            self.assertNotIn('bit_count', d)
            # This public test geometry has support shorter than the native
            # no-Helper minimum sixty-second spacing. No RX uses this oracle.
            self.assertLess(d['source_samples'], 60 * fs)
            initial_epoch = d['epoch']
            initial_ordinal = d['symbol_ordinal']
            cursor = 0
            for index, bit in enumerate(bits):
                slot_offset = d['data_offset_samples'] + index * 60 * fs
                first = slot_offset + d['source_samples'] // 2
                while cursor < first:
                    header, _, _ = source.next(min(16384, first - cursor))
                    cursor = header['first_sample'] + header['count']
                address = source.command(f'basis-address {initial_epoch + index * 60} {initial_ordinal + index}')
                self.assertEqual(address['generation'], index + 1)
                self.assertTrue(address['template_cache_cleared'])
                rows = [[b, 0, slot_offset / fs, 0, 0, 0, 0] for b in (0, 1)]
                _, alternatives = source.basis(first, 257, rows)
                header, data, valid = source.next(257)
                cursor = header['first_sample'] + header['count']
                self.assertEqual(header['first_sample'], first)
                self.assertEqual(valid, bytes([1]) * 257)
                values = complex_values(data)
                expected, wrong = alternatives[int(bit)], alternatives[1 - int(bit)]
                self.assertTrue(all(abs(a-b) <= 1e-10 * (1+abs(b)) for a,b in zip(values, expected)))
                self.assertGreater(max(abs(a-b) for a,b in zip(values, wrong)), 1e-3)
            # Addressing a template beyond the emitted message is valid public
            # hypothesis work. It must not require the actual message length.
            address = source.command(f'basis-address {initial_epoch + 600} {initial_ordinal + 10}')
            self.assertEqual(address['basis_symbol_ordinal'], initial_ordinal + 10)
            _, beyond = source.basis(d['source_samples'] // 2, 17, [[0, 0, 0, 0, 0, 0, 0]])
            self.assertGreater(max(abs(v) for v in beyond[0]), 0)

    def test_public_basis_address_native_guards(self):
        for command in ('basis-address 5 0', 'basis-address 4294967290 0',
                        'basis-address 1793850000 -1', 'basis-address 1793850000 18446744073709551616',
                        'basis-address 1793850000 0 extra', 'basis-address 1793850000'):
            with self.subTest(command=command), Source(['--stream-kind', 'emitted']) as source:
                source.process.communicate((command + '\n').encode('ascii'), timeout=10)
                self.assertNotEqual(source.process.returncode, 0)

    def test_public_slot_plans_do_not_depend_on_payload_channel_realization_or_basis_address(self):
        for helper in ('off', 'on'):
            def plans(bits, seed, arrival):
                with Source(['--data-bits', bits, '--observation-slots', '4', '--helper', helper,
                             '--seed', str(seed), '--arrival-seconds', str(arrival)]) as source:
                    d = source.descriptor
                    self.assertTrue(d['slot_plan_supported'])
                    rows = [source.command(f'slot-plan {index}') for index in (0, 1, 2, 3, 10)]
                    self.assertEqual(rows[0]['source_begin_samples'], d['data_offset_samples'])
                    self.assertEqual(rows[0]['pattern_epoch'], d['epoch'])
                    self.assertEqual(rows[0]['symbol_ordinal'], d['symbol_ordinal'])
                    self.assertEqual(rows[0]['source_samples'], d['source_samples'])
                    for current, successor in zip(rows[:3], rows[1:4]):
                        self.assertEqual(current['next_source_begin_samples'], successor['source_begin_samples'])
                        self.assertEqual(successor['symbol_ordinal'], current['symbol_ordinal'] + 1)
                    for row in rows:
                        self.assertEqual(row['pattern_begin_samples'] - row['source_begin_samples'],
                                         row['source_calendar_origin_sample'])
                        self.assertEqual(row['source_end_samples'] - row['source_begin_samples'],
                                         row['source_samples'])
                        self.assertEqual(row['receiver_window_physical_origin_sample'],
                                         row['receiver_window_first_sample'])
                        self.assertEqual(row['receiver_window_samples'],
                                         row['receiver_window_stop_sample'] - row['receiver_window_first_sample'])
                        self.assertAlmostEqual(row['window_capture_lead_seconds'],
                            row['capture_lead_seconds'] - row['receiver_window_first_sample']/row['sample_rate_hz'])
                    source.command(f'basis-address {d["epoch"] + 600} 91')
                    self.assertEqual(source.command('slot-plan 1'), rows[1])
                    self.assertEqual(source.next(3)[0]['first_sample'], 0)
                    return rows
            self.assertEqual(plans('0', 7, 0), plans('001', 19, .01))

    def test_public_slot_plan_native_guards(self):
        for command in ('slot-plan -1', 'slot-plan 1024', 'slot-plan', 'slot-plan 0 extra'):
            with self.subTest(command=command), Source(['--data-bits', '0']) as source:
                source.process.communicate((command + '\n').encode('ascii'), timeout=10)
                self.assertNotEqual(source.process.returncode, 0)
        with Source(['--stream-kind', 'emitted']) as source:
            source.process.communicate(b'slot-plan 0\n', timeout=10)
            self.assertNotEqual(source.process.returncode, 0)

    def test_quiet_aperture_excludes_public_neighbor_tails_at_future_clock_limits(self):
        args = ['--data-bits', '0', '--helper', 'on', '--rate', '1', '--added-symbols', '1',
                '--added-interleave', 'off', '--seconds', '.001',
                '--control', 'selected', '--oscillator-effects', 'on', '--base-oscillator', 'crystal',
                '--shift-oscillator', 'ic-7100', '--rf-hz', '500000', '--spread-preset', 'hf-best',
                '--margin', '1', '--selective-perturbation', 'each-1']
        with Source(args) as source:
            d = source.descriptor
            fs = d['sample_rate_hz']
            physics = source.plan['physics']
            plans = {index: source.command(f'slot-plan {index}') for index in (0, 1, 2, 20, 21, 22, 23)}
            half_ppm = physics['base_relative_pair_ppm_bound'] / 2
            scales = [Fraction.from_float(1 + sign * half_ppm * 1e-6) for sign in (-1, 1)]
            arrivals = [Fraction.from_float(x) for x in physics['arrival_prior_seconds']]
            lead = Fraction.from_float(float(plans[0]['capture_lead_seconds']))
            # Enumerate the endpoint clocks/arrivals independently. Eight
            # interpolation rows plus the exact selected finite-filter tail
            # remain inside the native envelope's larger32-row guard.
            first_guard = Fraction(8, fs) / min(scales)
            selective_tail = Fraction(2, 3) / Fraction.from_float(d['carrier_spacing_hz'])
            tail = Fraction.from_float(physics['spread_delay_ms'] * .001) + selective_tail + Fraction(2, fs) / min(scales)
            def first_edge(samples):
                seconds = Fraction(samples, fs)
                return min(fs * rx * (lead + seconds / tx + arrival - first_guard)
                           for rx in scales for tx in scales for arrival in arrivals)
            def last_edge(samples):
                seconds = Fraction(samples, fs)
                return max(fs * rx * (lead + seconds / tx + arrival + tail)
                           for rx in scales for tx in scales for arrival in arrivals)
            for index in (0, 1, 21, 22):
                row = plans[index]
                self.assertLessEqual(row['receiver_data_first_sample'], max(0, math.floor(first_edge(row['source_begin_samples']))))
                self.assertGreaterEqual(row['receiver_data_stop_sample'], math.ceil(last_edge(row['source_end_samples'])))
                if index:
                    previous = plans[index - 1]
                    self.assertGreaterEqual(row['receiver_previous_signal_stop_sample'], math.ceil(last_edge(previous['helper_end_samples'])))
                else:
                    self.assertEqual(row['receiver_previous_signal_stop_sample'], 0)
                successor = plans[index + 1]
                self.assertLessEqual(row['receiver_next_signal_first_sample'], max(0, math.floor(first_edge(successor['helper_begin_samples']))))
                first = min(row['receiver_window_stop_sample'], max(row['receiver_window_first_sample'],
                    row['receiver_data_first_sample'], row['receiver_previous_signal_stop_sample']))
                stop = max(first, min(row['receiver_window_stop_sample'], row['receiver_data_stop_sample'],
                    row['receiver_next_signal_first_sample']))
                self.assertEqual((row['quiet_aperture_first_sample'], row['quiet_aperture_stop_sample']), (first, stop))
                self.assertEqual(row['quiet_aperture_available'], first < stop)
            # The old full-window statistic included a possible predecessor
            # tail, whereas current DATA starts much later in this geometry.
            self.assertLess(plans[22]['receiver_window_first_sample'], plans[21]['receiver_data_stop_sample'])
            self.assertGreater(plans[22]['quiet_aperture_first_sample'], plans[22]['receiver_window_first_sample'])
            selected_stop = plans[0]['receiver_data_stop_sample']
        with Source([*args, '--control', 'flat']) as flat:
            off = flat.command('slot-plan 0')
            self.assertGreaterEqual(selected_stop - off['receiver_data_stop_sample'],
                                    math.floor(float(selective_tail) * fs) - 2)

    def test_sequence_channel_is_chunk_invariant_and_basis_control_does_not_reset_it(self):
        args = ['--data-bits', '001', '--helper', 'on', '--control', 'selected', '--oscillator-effects', 'on',
                '--spread-preset', 'hf-best', '--selective-perturbation', 'each-1',
                '--channel-evolution', 'stochastic', '--random-doppler-width-hz', '.1',
                '--delay-excursion-ms', '.001', '--delay-knot-seconds', '1']
        # Hash samples and validity separately so caller chunk boundaries do
        # not themselves change the digest input.
        def sample_hash(chunks, change_basis):
            with Source(args) as source:
                d = source.descriptor
                samples, validity = hashlib.sha256(), hashlib.sha256()
                cursor = turn = 0
                while cursor < d['total_samples']:
                    if change_basis and turn in (1, 4):
                        source.command(f'basis-address {d["epoch"] + 600 + turn} {91 + turn}')
                        source.basis(2048, 3, [[0, 0, 0, 0, 0, 0, 0]])
                    header, data, valid = source.next(chunks[turn % len(chunks)])
                    self.assertEqual(header['first_sample'], cursor)
                    self.assertGreater(header['count'], 0)
                    samples.update(data)
                    validity.update(valid)
                    cursor += header['count']
                    turn += 1
                self.assertTrue(header['complete'])
                self.assertGreater(d['capture_samples'], d['emitted_samples'])
                receipt = source.close()
                self.assertTrue(receipt['complete'])
                self.assertEqual(receipt['source_generated_samples'], d['emitted_samples'])
                return samples.hexdigest(), validity.hexdigest(), cursor
        self.assertEqual(sample_hash((16384,), False), sample_hash((123, 4093, 8191), True))

    def test_sequence_workspace_and_input_bounds(self):
        with Source(['--stream-kind', 'emitted', '--data-bits', '0']) as short:
            short_bytes = short.descriptor['fixed_workspace_bytes']
        with Source(['--stream-kind', 'emitted', '--data-bits', '01' * 128]) as long:
            d = long.descriptor
            self.assertLess(d['fixed_workspace_bytes'] - short_bytes, 65536)
            self.assertLess(d['fixed_workspace_bytes'], d['workspace_budget_bytes'])
            self.assertEqual(long.next(3)[0]['count'], 3)
        for extra in (['--data-bits', ''], ['--data-bits', '0x1'], ['--data-bits', '0' * 257],
                      ['--data-bits', '001', '--whole-bit-count', '2'],
                      ['--data-bits', '001', '--bit', '1']):
            failed = subprocess.run([str(BINARY), *COMMON, '--stream-server', *extra],
                                    input=b'', capture_output=True, timeout=10)
            self.assertNotEqual(failed.returncode, 0)

    def test_public_observation_horizon_contains_real_quiet_slots_independent_of_payload_length(self):
        def quiet_window(bits):
            with Source(['--data-bits', bits, '--observation-slots', '4']) as source:
                d = source.descriptor
                self.assertEqual(d['public_observation_slots'], 4)
                self.assertNotIn('data_bits', d)
                self.assertNotIn('bit_count', d)
                fs = d['sample_rate_hz']
                first = round(d['capture_lead_seconds'] * fs) + 180 * fs + d['source_samples'] // 2
                cursor = 0
                while cursor < first:
                    header, _, _ = source.next(min(16384, first - cursor))
                    cursor += header['count']
                header, data, valid = source.next(257)
                self.assertEqual(header['first_sample'], first)
                self.assertEqual(valid, bytes([1]) * 257)
                self.assertGreater(max(abs(v) for v in complex_values(data)), 0)
                # Transport exhaustion is not message completion. Stopping
                # before this fixed public observation budget stays incomplete.
                receipt = source.close()
                self.assertFalse(receipt['complete'])
                return d['total_samples'], d['emitted_samples'], data
        one, three = quiet_window('0'), quiet_window('001')
        self.assertEqual(one[0], three[0])
        self.assertLess(one[1], three[1])
        self.assertEqual(one[2], three[2])
        for args in (['--data-bits', '001', '--observation-slots', '2'],
                     ['--data-bits', '0', '--observation-slots', '0'],
                     ['--data-bits', '0', '--observation-slots', '1025'],
                     ['--observation-slots', '4']):
            failed = subprocess.run([str(BINARY), *COMMON, '--stream-server', *args],
                                    input=b'', capture_output=True, timeout=10)
            self.assertNotEqual(failed.returncode, 0)

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
            # Nonzero center forces the scalar mapped renderer. With zero
            # warp its center phase is exactly zero.
            reference_header, reference = source.basis_map(first, list(range(count)), rows, audio_center=1)
            self.assertFalse(reference_header['shared_geometry_bank'])
            self.assertEqual(bank, reference)
            offsets = [i*1.0002+.125 for i in range(count)]
            mapped_header, mapped = source.basis_map(first, offsets, rows)
            self.assertTrue(mapped_header['shared_geometry_bank'])
            _, mapped_reference = source.basis_map(first, offsets, rows, audio_center=1)
            self.assertEqual(mapped, mapped_reference)
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

    def test_shared_mapped_bank_accepts_large_origin_with_distinct_offsets(self):
        with Source(['--stream-kind', 'emitted']) as source:
            rows = [(bit, 0, 0, 0, 0, 0, 0) for bit in (0, 1)]
            # The offsets remain distinct even when adding either to this
            # origin gives the same rounded native sample coordinate.
            header, values = source.basis_map(1 << 63, [0., .125], rows)
            self.assertTrue(header['shared_geometry_bank'])
            self.assertEqual(header['first_sample'], 1 << 63)
            self.assertEqual(values, [[0j, 0j], [0j, 0j]])





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

"""Fast contract tests for fresh per-transmission background-noise plumbing."""
import argparse
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

TOOLS = Path(__file__).resolve().parents[1] / 'tools'
sys.path.insert(0, str(TOOLS))
import triad_background_noise as m


class BackgroundNoiseAdapterTests(unittest.TestCase):
    def parser(self):
        p = argparse.ArgumentParser()
        m.add_arguments(p)
        return p

    def test_default_is_exactly_off_and_does_not_launch_mixer(self):
        args = self.parser().parse_args([])
        self.assertFalse(m.enabled(args))
        with tempfile.TemporaryDirectory() as d:
            with mock.patch.object(m.subprocess, 'run') as run:
                result = m.apply(args, input_path=Path(d) / 'clean',
                    output_path=Path(d) / 'out', metadata_path=Path(d) / 'meta.json',
                    sample_rate=48000, input_format='s16', output_format='s16', channels=2)
                self.assertIsNone(result)
                run.assert_not_called()

    def test_level_forms_and_fixed_watt_reference(self):
        cases = [(['--background-noise-bits', '10'], ['--bits', '10']),
                 (['--background-noise-obscured-bits', '2'], ['--obscured-bits', '2']),
                 (['--background-noise-db', '90'], ['--db', '90.0']),
                 (['--background-noise-watts', '0.001', '--background-noise-reference-watts', '40',
                   '--background-noise-reference-rms', '0.25'],
                  ['--noise-watts', '0.001', '--reference-watts', '40.0', '--reference-rms', '0.25'])]
        for supplied, expected in cases:
            with self.subTest(supplied=supplied):
                args = self.parser().parse_args(supplied)
                self.assertTrue(m.enabled(args))
                argv = m.command_args(args, sample_rate=48000,
                    input_path='clean.s16', output_path='fresh.s16', metadata_path='noise.json',
                    input_format='s16', output_format='s16', channels=2)
                self.assertEqual(argv[argv.index('--input') + 1], 'clean.s16')
                self.assertEqual(argv[argv.index('--output') + 1], 'fresh.s16')
                self.assertEqual(argv[argv.index('--sample-rate') + 1], '48000')
                for i in range(0, len(expected), 2):
                    self.assertEqual(argv[argv.index(expected[i]) + 1], expected[i + 1])

    def test_relative_watts_require_declared_reference_and_levels_are_bounded(self):
        cases = [(['--background-noise-watts', '.001'], 'both fixed reference'),
                 (['--background-noise-bits', '17'], '0..16'),
                 (['--background-noise-obscured-bits', '-1'], '0..16'),
                 (['--background-noise-db', '-2'], 'nonnegative')]
        for supplied, error in cases:
            with self.subTest(supplied=supplied):
                args = self.parser().parse_args(supplied)
                with self.assertRaisesRegex(ValueError, error):
                    m.validate(args)

    def test_each_transmission_runs_a_new_native_mixer_process(self):
        args = self.parser().parse_args(['--background-noise-db', '90'])
        with tempfile.TemporaryDirectory() as d:
            def fake_run(argv, **kwargs):
                Path(argv[argv.index('--output') + 1]).write_bytes(b'noisy')
                Path(argv[argv.index('--metadata') + 1]).write_text('{"variance":0.0001}\n')
            with mock.patch.object(m.subprocess, 'run', side_effect=fake_run) as run:
                for ordinal in (1, 2):
                    clean = Path(d) / f'clean-{ordinal}.s16'
                    clean.write_bytes(b'clean')
                    output = Path(d) / f'noisy-{ordinal}.s16'
                    meta = Path(d) / f'noise-{ordinal}.json'
                    receipt = m.apply(args, input_path=clean, output_path=output,
                        metadata_path=meta, sample_rate=48000, input_format='s16',
                        output_format='s16', channels=2)
                    self.assertIn(str(output), receipt['argv'])
                self.assertEqual(run.call_count, 2)
                self.assertTrue(run.call_args_list[0].kwargs['check'])
                self.assertNotEqual(run.call_args_list[0].args[0], run.call_args_list[1].args[0])

    def test_disabled_audio_and_recorded_adc_replay_are_not_noise_transmitters(self):
        spec = importlib.util.spec_from_file_location('triad_live_alsa_noise_test', TOOLS / 'triad_live_alsa.py')
        alsa = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(alsa)
        parsed = alsa.parser().parse_args(['--live', '--exclusive', '--input', 'in.f32', '--output', 'out.f32',
            '--metadata', 'meta.json', '--rate', '48000', '--capture-device', 'hw:1,0',
            '--playback-device', 'hw:1,0'])
        self.assertFalse(m.enabled(parsed))
        self.assertEqual(alsa.parser().parse_args(['--live', '--exclusive', '--input', 'in.f32', '--output', 'out.f32',
            '--metadata', 'meta.json', '--rate', '48000', '--capture-device', 'hw:1,0',
            '--playback-device', 'hw:1,0', '--background-noise-bits', '16']).background_noise_bits, 16)

    def test_continuous_audio_adapter_rejects_noise_on_recorded_adc_pcm(self):
        spec = importlib.util.spec_from_file_location('triad_stream_run_noise_test', TOOLS / 'triad_stream_run.py')
        stream = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(stream)
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            out = root / 'new-output'
            with self.assertRaises(SystemExit) as error:
                argv = ['triad_stream_run.py', '--source-dir', str(root), '--output', str(out),
                        '--replay-pcm', str(root / 'capture.pcm'), '--playback-offset-frames', '0',
                        '--background-noise-bits', '10']
                with mock.patch.object(sys, 'argv', argv):
                    # The validation runs before loading slots or opening any audio endpoint.
                    stream.main()
            self.assertEqual(error.exception.code, 2)
            self.assertFalse(out.exists())

    def test_duplex_noise_options_reach_live_preflight_for_both_backends(self):
        spec = importlib.util.spec_from_file_location('triad_live_duplex_noise_test', TOOLS / 'triad_live_duplex.py')
        duplex = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(duplex)
        with tempfile.TemporaryDirectory() as d:
            for backend in ('native', 'alsa-tools'):
                with self.subTest(backend=backend), self.assertRaisesRegex(ValueError, 'run/analyze requires --prepared'):
                    duplex.main(['run', '--output', str(Path(d) / f'out-{backend}'),
                                 '--capture-backend', backend, '--background-noise-bits', '10'])
            with self.assertRaisesRegex(ValueError, 'transmit-time option'):
                duplex.main(['prepare', '--output', str(Path(d) / 'out2'), '--background-noise-bits', '10'])


if __name__ == '__main__':
    unittest.main()

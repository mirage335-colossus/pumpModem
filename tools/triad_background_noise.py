"""Shared CLI plumbing for opt-in cryptographic triad TX noise."""
from __future__ import annotations

from pathlib import Path
import hashlib
import json
import math
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def add_arguments(parser):
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--background-noise-bits', type=int, metavar='BITS',
                       help='ideal DAC-equivalent resolution, 0..16 (off by default)')
    group.add_argument('--background-noise-obscured-bits', type=int, metavar='BITS',
                       help='number of the 16 ideal DAC bits obscured, 0..16')
    group.add_argument('--background-noise-db', type=float, metavar='DB',
                       help='noise suppression below a full-scale sine, in dB')
    group.add_argument('--background-noise-watts', type=float, metavar='WATTS',
                       help='noise watts relative to a fixed active-signal reference')
    parser.add_argument('--background-noise-reference-watts', type=float,
                        help='strongest active Helper/DATA transmitter power for relative watts')
    parser.add_argument('--background-noise-reference-rms', type=float,
                        help='fixed RMS reference of the stronger complete active Helper or DATA signal')
    parser.add_argument('--background-noise-tool', type=Path,
                        default=ROOT / 'build/dev-cli/background_noise',
                        help='native OpenSSL AES background-noise mixer')


def enabled(args):
    return any(getattr(args, name, None) is not None for name in (
        'background_noise_bits', 'background_noise_obscured_bits',
        'background_noise_db', 'background_noise_watts'))


def forwarded_arguments(args):
    """Return only the level and fixed references for another TX frontend."""
    validate(args)
    if not enabled(args):
        return []
    if args.background_noise_bits is not None:
        result = ['--background-noise-bits', str(args.background_noise_bits)]
    elif args.background_noise_obscured_bits is not None:
        result = ['--background-noise-obscured-bits', str(args.background_noise_obscured_bits)]
    elif args.background_noise_db is not None:
        result = ['--background-noise-db', str(args.background_noise_db)]
    else:
        result = ['--background-noise-watts', str(args.background_noise_watts),
                  '--background-noise-reference-watts', str(args.background_noise_reference_watts),
                  '--background-noise-reference-rms', str(args.background_noise_reference_rms)]
    result += ['--background-noise-tool', str(args.background_noise_tool)]
    return result


def validate(args):
    active = enabled(args)
    watts = getattr(args, 'background_noise_watts', None)
    ref_w = getattr(args, 'background_noise_reference_watts', None)
    ref_rms = getattr(args, 'background_noise_reference_rms', None)
    if not active:
        if ref_w is not None or ref_rms is not None:
            raise ValueError('background-noise references require an enabled noise level')
        return
    if watts is not None:
        if ref_w is None or ref_rms is None:
            raise ValueError('relative noise watts require both fixed reference watts and reference RMS')
        if not math.isfinite(ref_w) or ref_w <= 0 or not math.isfinite(ref_rms) or ref_rms <= 0:
            raise ValueError('background-noise references must be finite and positive')
    elif ref_w is not None or ref_rms is not None:
        raise ValueError('background-noise references are only valid with --background-noise-watts')
    if getattr(args, 'background_noise_bits', None) is not None and not 0 <= args.background_noise_bits <= 16:
        raise ValueError('background-noise bits must be 0..16')
    if getattr(args, 'background_noise_obscured_bits', None) is not None and not 0 <= args.background_noise_obscured_bits <= 16:
        raise ValueError('background-noise obscured bits must be 0..16')
    if getattr(args, 'background_noise_db', None) is not None and (
            not math.isfinite(args.background_noise_db) or args.background_noise_db < 0):
        raise ValueError('background-noise suppression must be finite and nonnegative')
    if watts is not None and (not math.isfinite(watts) or watts <= 0):
        raise ValueError('background-noise watts must be finite and positive')


def command_args(args, *, sample_rate, input_path, output_path, metadata_path,
                 input_format, output_format, channels=1, signal_gain=1.0):
    """Build the native mixer command; does not start it or hide its errors."""
    validate(args)
    if not enabled(args):
        return None
    result = [str(args.background_noise_tool), '--input', str(input_path),
              '--output', str(output_path), '--format', input_format,
              '--output-format', output_format, '--channels', str(channels),
              '--sample-rate', str(int(sample_rate)), '--signal-gain', str(signal_gain),
              '--metadata', str(metadata_path)]
    if args.background_noise_bits is not None:
        result += ['--bits', str(args.background_noise_bits)]
    elif args.background_noise_obscured_bits is not None:
        result += ['--obscured-bits', str(args.background_noise_obscured_bits)]
    elif args.background_noise_db is not None:
        result += ['--db', str(args.background_noise_db)]
    else:
        result += ['--noise-watts', str(args.background_noise_watts),
                   '--reference-watts', str(args.background_noise_reference_watts),
                   '--reference-rms', str(args.background_noise_reference_rms)]
    return result


def apply(args, *, input_path, output_path, metadata_path, sample_rate,
          input_format, output_format, channels=1, signal_gain=1.0, timeout=60.):
    argv = command_args(args, sample_rate=sample_rate, input_path=input_path,
                        output_path=output_path, metadata_path=metadata_path,
                        input_format=input_format, output_format=output_format,
                        channels=channels, signal_gain=signal_gain)
    if argv is None:
        return None
    # A fresh native process creates one fresh ephemeral key for this transmission.
    subprocess.run(argv, check=True, capture_output=True, text=True, timeout=timeout)
    def digest(path):
        value = hashlib.sha256()
        with Path(path).open('rb') as stream:
            for block in iter(lambda: stream.read(1 << 20), b''):
                value.update(block)
        return value.hexdigest()
    return {'file': str(output_path), 'sha256': digest(output_path),
            'input_sha256': digest(input_path), 'metadata': str(metadata_path),
            'metadata_sha256': digest(metadata_path),
            'measurement': json.loads(Path(metadata_path).read_text()), 'argv': argv}

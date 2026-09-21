#!/usr/bin/env python3
"""Summarize completed live probes; safe to rerun while other probes are active.

Usage: python3 summarize.py [directory] [--plot]
No audio is opened. --plot uses matplotlib only if it is already installed.
"""
import argparse
import csv
import hashlib
import json
import math
from datetime import datetime, timezone
from pathlib import Path


def finite(value):
    return value if isinstance(value, (int, float)) and math.isfinite(value) else None


def read_json(path):
    try:
        raw = path.read_bytes()
        return json.loads(raw), raw, None
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        return None, None, f"{type(error).__name__}: {error}"


def db(value):
    return 20 * math.log10(value) if value is not None and value > 0 else None


def ratio(numerator, denominator):
    return numerator / denominator if numerator is not None and denominator else None


def probe_rows(directory):
    rows, omitted, analyses = [], [], []
    paths = {p.with_name(p.name.removesuffix('.command.json') + '.json')
             for p in directory.glob('*.command.json')}
    paths.update(p for p in directory.glob('*.json')
                 if not p.name.endswith(('-known.json', '.command.json'))
                 and p.name not in {'summary.json', 'tones-plan.json'})
    for path in sorted(paths):
        probe, raw, error = read_json(path)
        if error:
            omitted.append({'file': path.name, 'reason': error})
            continue
        if not isinstance(probe, dict) or probe.get('mode') not in {'raw', 'codec'}:
            continue
        if not all(key in probe for key in ('profile', 'tx_level', 'fifo_overflow', 'tx_intervals')):
            omitted.append({'file': path.name, 'reason': 'Incomplete probe summary'})
            continue
        known_path = path.with_name(path.stem + '-known.json')
        known, _, error = read_json(known_path)
        status = 'available'
        if error:
            status = 'missing' if not known_path.exists() else 'incomplete'
            analyses.append({'file': known_path.name, 'reason': error})
            known = {}
        elif not isinstance(known, dict):
            status, known = 'invalid', {}
        elif known.get('inputs', {}).get('probe', {}).get('sha256') not in (
                None, hashlib.sha256(raw).hexdigest()):
            status, known = 'stale', {}
            analyses.append({'file': known_path.name, 'reason': 'Probe SHA256 differs from analyzed input'})
        elif not known.get('qualified', False):
            status = 'unqualified'
        summary = known.get('summary', {}) if status == 'available' else {}
        mode = probe['mode']
        tx = probe.get('tx_level', {})
        amplitude, baud, rolloff = (finite(probe.get(k)) for k in ('amplitude', 'symbol_rate', 'rolloff'))
        peak, rms = finite(tx.get('peak')), finite(tx.get('rms'))
        raw_compared = probe.get('raw_compared_bits') if mode == 'raw' else None
        raw_wrong = probe.get('raw_wrong_bits') if mode == 'raw' else None
        raw_ber = ratio(raw_wrong, raw_compared)
        known_ber = finite(summary.get('known_payload_ber'))
        ber_basis = 'known-source demapped payload bits' if known_ber is not None else None
        if known_ber is None and mode == 'raw' and status == 'available':
            known_ber, ber_basis = raw_ber, 'production raw bit comparison'
        codec_pass = None
        if mode == 'codec':
            codec_pass = bool(probe.get('decoded_complete') and probe.get('physical_end') and probe.get('exact'))
        band = path.stem.split('-', 1)[0]
        if probe['profile'] == 'wire' and baud is not None:
            bandwidth = baud * (1 + rolloff) if rolloff is not None else None
            band = 'wide' if bandwidth and bandwidth > 5000 else 'mid' if bandwidth and bandwidth > 500 else 'narrow'
        else:
            bandwidth = baud * (1 + rolloff) if baud is not None and rolloff is not None else None
        rows.append({
            'trial': path.stem, 'profile': probe['profile'], 'band': band, 'mode': mode,
            'amplitude': amplitude, 'constellation': probe.get('apsk'), 'symbol_rate': baud,
            'rolloff': rolloff, 'bandwidth_hz': bandwidth, 'carrier_hz': probe.get('carrier_hz'),
            'tx_rms': rms, 'tx_peak': peak, 'tx_crest_db': db(ratio(peak, rms)),
            'digital_overload': peak > 1 if peak is not None else None,
            'near_full_only': .999 <= peak <= 1 if peak is not None else None,
            'tx_near_full_samples': tx.get('clipped'),
            'known_status': status, 'known_evm': finite(summary.get('known_evm')),
            'known_signal_to_error_db': finite(summary.get('known_signal_to_residual_db')),
            'known_ber': known_ber, 'known_ber_basis': ber_basis,
            'raw_compared_bits': raw_compared, 'raw_wrong_bits': raw_wrong, 'raw_ber': raw_ber,
            'raw_erased_bits': probe.get('raw_erased_bits') if mode == 'raw' else None,
            'raw_missing_bits': probe.get('raw_missing_bits') if mode == 'raw' else None,
            'source_bytes': probe.get('source_bytes') if mode == 'codec' else None,
            'codec_pass': codec_pass, 'physical_end': bool(probe.get('physical_end')),
            'fifo_overflow': bool(probe.get('fifo_overflow')),
            'fifo_maximum_seconds': probe.get('fifo_maximum_seconds'),
            'dsp_max_chunk_seconds': probe.get('dsp_max_chunk_seconds'),
            'capture_error': probe.get('capture_error'), 'playback_error': probe.get('playback_error'),
        })
    rows.sort(key=lambda row: (row['profile'], -(row['bandwidth_hz'] or 0), row['mode'], row['amplitude'] or 0, row['trial']))
    return rows, omitted, analyses


def tone_rows(directory):
    path = directory / 'tones/analysis-production-mono.json'
    data, _, error = read_json(path)
    if error:
        return [], {}, error
    rows = []
    for measurement in data.get('measurements', []):
        tx = measurement.get('tx_segment', {})
        if (measurement.get('analysis') != 'stationary_single_tone' or
                measurement.get('window') != 'steady' or tx.get('frequency_hz') != 997 or
                not measurement.get('qualified')):
            continue
        amplitude = finite(tx.get('tx_peak_requested'))
        power = finite(measurement.get('fitted_signal_power_fs2'))
        fitted_rms = math.sqrt(power) if power is not None and power >= 0 else None
        gain = ratio(fitted_rms, amplitude / math.sqrt(2) if amplitude else None)
        row = {'tx_peak': amplitude, 'fitted_rx_rms': fitted_rms, 'fitted_gain': gain,
               'fitted_gain_db': db(gain), 'frequency_hz': measurement.get('frequency_hz')}
        for band in measurement.get('bands', []):
            key = f"{band['low_hz']:g}_{band['high_hz']:g}hz"
            row['sinad_db_' + key] = finite(band.get('sinad_db'))
        rows.append(row)
    rows.sort(key=lambda row: row['tx_peak'])
    gains = [row['fitted_gain_db'] for row in rows if row['fitted_gain_db'] is not None]
    spread = {'points': len(gains), 'gain_min_db': min(gains), 'gain_max_db': max(gains),
              'gain_spread_db': max(gains) - min(gains)} if gains else {'points': 0}
    return rows, spread, None


def fmt(value, precision=3):
    return '—' if value is None else f'{value:.{precision}g}'


def markdown(rows, tones, spread, omitted, analyses):
    lines = [
        '# Cable output-level measurements', '',
        'Known-symbol signal/error includes noise, distortion and receiver estimation errors; it is not calibrated AWGN SNR. '
        'Raw trials test uncoded bit decisions and have no file pass/fail outcome. Codec pass requires an exact, physically completed file. '
        'BER is a pre-FEC demapped payload-bit error fraction, not a percent.', '',
        'Every radio-labeled trial used the same headphone-to-microphone cable; no IC-7100 was connected. '
        'The manually selected mid-band cable trials exceed the production one-second FIFO allowance and do not qualify real-time reception.', '',
        '| Trial | Mode | Bandwidth Hz | Amplitude | TX RMS / peak | Known signal/error dB | BER | Codec | FIFO max s | TX range |',
        '|---|---|---:|---:|---:|---:|---:|---|---:|---|',
    ]
    for row in rows:
        codec = '—' if row['codec_pass'] is None else 'pass' if row['codec_pass'] else 'incomplete / not exact'
        level = ('unavailable' if row['tx_peak'] is None else 'OVERLOAD >1' if row['digital_overload']
                 else 'near full only' if row['near_full_only'] else 'within range')
        fifo = fmt(row['fifo_maximum_seconds']) + (' OVERFLOW' if row['fifo_overflow'] else '')
        lines.append(f"| {row['trial']} | {row['mode']} | {fmt(row['bandwidth_hz'],5)} | {fmt(row['amplitude'])} | "
                     f"{fmt(row['tx_rms'],4)} / {fmt(row['tx_peak'],4)} | {fmt(row['known_signal_to_error_db'],4)} | "
                     f"{fmt(row['known_ber'])} | {codec} | {fifo} | {level} |")
    lines += ['', 'Generated peak >1 proves digital overload. A peak from .999 through 1 is only near full scale. '
              'The probe’s legacy `clipped` count uses the .999 threshold; it is exported as `tx_near_full_samples`, not an overload count.', '',
              '## Separate 997 Hz tone measurements', '',
              'Steady-window fitted fundamental gain is captured PCM RMS divided by generated sine PCM RMS. '
              'It includes the production capture routing and gain, is not a separate analog-voltage calibration, '
              'and is separate from the normalized modem constellation gain.', '',
              '| TX sine peak | Fitted chain gain dB | SINAD 300–18300 Hz dB | SINAD 20–20000 Hz dB |',
              '|---:|---:|---:|---:|']
    for row in tones:
        lines.append(f"| {fmt(row['tx_peak'])} | {fmt(row['fitted_gain_db'],6)} | "
                     f"{fmt(row.get('sinad_db_300_18300hz'),5)} | {fmt(row.get('sinad_db_20_20000hz'),5)} |")
    lines += ['', f"Fitted gain spread over {spread.get('points',0)} steady levels: {fmt(spread.get('gain_spread_db'),5)} dB.", '',
              f"Omitted incomplete/unreadable probe summaries: {len(omitted)}. Missing/incomplete/stale known-symbol analyses: {len(analyses)}."]
    for item in omitted + analyses:
        lines.append(f"- `{item['file']}`: {item['reason'].splitlines()[0]}")
    return '\n'.join(lines) + '\n'


def plot_if_available(directory, rows):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        return 'omitted: matplotlib is not installed; no dependency was installed'
    fig, ax = plt.subplots(figsize=(8.5, 5.2), layout='constrained')
    colors = {'wide': '#1261a0', 'mid': '#bd571c', 'narrow': '#347447'}
    for band, color in colors.items():
        for mode, marker in [('raw', 'o'), ('codec', 's')]:
            points = [r for r in rows if r['profile'] == 'wire' and r['band'] == band and r['mode'] == mode
                      and r['known_signal_to_error_db'] is not None]
            if not points:
                continue
            points.sort(key=lambda r: r['amplitude'])
            ax.plot([r['amplitude'] for r in points], [r['known_signal_to_error_db'] for r in points],
                    color=color, marker=marker, linewidth=1, label=f'{band} · {mode}')
            for row in points:
                if row['digital_overload']:
                    ax.scatter(row['amplitude'], row['known_signal_to_error_db'], marker='x', color='red', s=100, zorder=4)
                elif row['near_full_only']:
                    ax.scatter(row['amplitude'], row['known_signal_to_error_db'], marker='D', facecolors='none', edgecolors='black', s=100, zorder=4)
    ax.set(xlabel='Configured waveform amplitude', ylabel='Known-symbol signal / error power (dB)',
           title='Cable modem residual versus output level')
    ax.grid(alpha=.25);ax.legend(fontsize=9)
    fig.suptitle('× generated peak >1   ◇ near full (.999–1); raw results are not file outcomes', fontsize=9)
    fig.savefig(directory / 'known-error-vs-amplitude.png', dpi=180)
    plt.close(fig)
    return 'known-error-vs-amplitude.png'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', nargs='?', type=Path, default=Path(__file__).resolve().parent)
    parser.add_argument('--plot', action='store_true')
    args = parser.parse_args()
    rows, omitted, analyses = probe_rows(args.directory)
    tones, spread, tone_error = tone_rows(args.directory)
    plot = plot_if_available(args.directory, rows) if args.plot else 'not requested'
    report = {'generated_utc': datetime.now(timezone.utc).isoformat(), 'probes': rows,
              'tone_997hz': tones, 'tone_linearity': spread, 'tone_error': tone_error,
              'omitted_probes': omitted, 'unavailable_known_analyses': analyses, 'plot': plot,
              'interpretation': {'known_residual': 'Includes receiver errors and distortion; not calibrated AWGN SNR',
                                 'raw_mode': 'No codec/file outcome is inferred',
                                 'overload': 'Generated PCM peak strictly >1; near-full threshold .999 is separate'}}
    (args.directory / 'summary.json').write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    with (args.directory / 'summary.csv').open('w', newline='') as output:
        if rows:
            writer = csv.DictWriter(output, fieldnames=list(rows[0]))
            writer.writeheader();writer.writerows(rows)
    (args.directory / 'summary.md').write_text(markdown(rows, tones, spread, omitted, analyses))
    print(f"{len(rows)} complete probe summaries; {len(omitted)} omitted probes; {len(analyses)} unavailable known analyses; {len(tones)} steady tone levels")
    print('Plot:', plot)


if __name__ == '__main__':
    main()

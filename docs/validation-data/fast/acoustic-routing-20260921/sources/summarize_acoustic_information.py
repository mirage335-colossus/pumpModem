import csv
import json
import math
import sys
from fractions import Fraction
from pathlib import Path

prefix = Path(sys.argv[1])
qam = int(sys.argv[2])
fft_size = int(sys.argv[3]) if len(sys.argv) > 3 else 32768
sample_rate = 48000
bps = qam.bit_length() - 1
code_rate = float(Fraction(sys.argv[4])) if len(sys.argv) > 4 else .75

def rows(suffix):
    with open(str(prefix) + suffix) as f:
        return [{k: float(v) for k, v in row.items()} for row in csv.DictReader(f)]

frames = rows('-frames.csv')
cycles = rows('-cycles.csv')
planes = rows('-planes.csv')
bins = rows('-bins.csv')
bands = {}
for row in bins:
    frequency = row['index'] * sample_rate / fft_size
    low = 500 + 250 * math.floor((frequency - 500) / 250)
    band = bands.setdefault(low, dict(symbols=0, bits=0, errors=0, information=0, channel_power=0))
    band['symbols'] += row['symbols']
    band['bits'] += row['compared_bits']
    band['errors'] += row['errors']
    band['information'] += row['symbols'] * row['gmi_per_symbol']
    band['channel_power'] += row['symbols'] * row['mean_channel_power']
band_rows = []
for low, band in sorted(bands.items()):
    band_rows.append(dict(low_hz=low, high_hz=low+250,
        compared_bits=band['bits'], ber=band['errors']/band['bits'],
        gmi_per_bit=band['information']/band['bits'],
        gmi_per_tone=band['information']/band['symbols'],
        mean_channel_power_db=10*math.log10(max(band['channel_power']/band['symbols'], 1e-30))))
with open(str(prefix) + '-bands.csv', 'w') as f:
    writer = csv.DictWriter(f, fieldnames=band_rows[0].keys() if band_rows else ['low_hz'])
    writer.writeheader()
    writer.writerows(band_rows)

count = sum(r['compared_bits'] for r in frames)
summary = dict(qam=qam, bits_per_tone=bps, code_rate=code_rate,
    code_information_load_per_tone=bps*code_rate,
    compared_coded_bits=int(count), frame_count=len(frames),
    fully_observed_frames=sum(r['compared_bits'] == 64800 for r in frames),
    incomplete_frame_indices=[int(r['index']) for r in frames if r['compared_bits'] != 64800],
    hard_ber=sum(r['errors'] for r in frames)/count if count else None,
    erased_bits=int(sum(r['erased_bits'] for r in frames)),
    confident_wrong_bits=int(sum(r['confident_wrong_bits'] for r in frames)),
    gmi_per_coded_bit=sum(r['gmi_per_bit']*r['compared_bits'] for r in frames)/count if count else None,
    minimum_frame_gmi_per_bit=min((r['gmi_per_bit'] for r in frames),default=None),
    maximum_frame_gmi_per_bit=max((r['gmi_per_bit'] for r in frames),default=None),
    cycles=cycles, planes=planes,
    diagnostic_limits='Empirical GMI of decoder metrics, not a strict channel-capacity upper bound. The selected code rate is a margin reference, not a finite-code success guarantee. Oracle scale is chosen after capture using known bits. Bands include fixed alignment bits; frame/cycle/plane metrics exclude them.')
Path(str(prefix) + '-information.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({k:v for k,v in summary.items() if k not in ['cycles','planes','diagnostic_limits']},indent=2))

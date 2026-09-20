"""Recheck the lossless selected capture archive; this script never opens audio."""
from pathlib import Path
import hashlib,json,sys
import numpy as np
ROOT=Path(__file__).resolve().parents[4]
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools'))
from cable_snr_analysis import analyze_tone,analyze_silence,analyze_multitone
count=0
for folder in sorted((HERE/'runs').iterdir()):
    analysis=json.loads((folder/'analysis-mono.json').read_text())
    with np.load(folder/'capture-windows.npz',allow_pickle=False) as arrays:
        for row in json.loads((folder/'capture-windows.json').read_text()):
            raw=arrays[row['key']]
            assert hashlib.sha256(raw.tobytes()).hexdigest()==row['sha256']
            original=analysis['measurements'][row['analysis_measurement_index']]
            x=raw.astype(float).mean(axis=1)
            if original['tx_segment']['kind']=='tone':
                r=analyze_tone(x,48000,original['frequency_hint_hz'])
                fields=('snr_db','sinad_db')
            elif original['tx_segment']['kind']=='multitone':
                r=analyze_multitone(x,48000,analysis['metadata']['frequencies'])
                fields=('linear_multitone_residual_ratio_db',)
            else:
                r=analyze_silence(x,48000)
                fields=('noise_rms_dbfs',)
            for expected,actual in zip(original['bands'],r['bands']):
                for field in fields:
                    assert abs(expected[field]-actual[field])<.001,(folder.name,row['key'],field,expected[field],actual[field])
            count+=1
    if (folder/'production-windows.npz').exists():
        analysis=json.loads((folder/'analysis-production-mono.json').read_text())
        with np.load(folder/'production-windows.npz',allow_pickle=False) as arrays:
            for row in json.loads((folder/'production-windows.json').read_text()):
                raw=arrays[row['key']]
                assert hashlib.sha256(raw.tobytes()).hexdigest()==row['sha256']
                original=analysis['measurements'][row['analysis_measurement_index']]
                r=analyze_tone(raw,48000,original['frequency_hint_hz'])
                for expected,actual in zip(original['bands'],r['bands']):
                    for field in ('snr_db','sinad_db'):
                        assert abs(expected[field]-actual[field])<.001,(folder.name,row['key'],field)
                count+=1
print(f'PASS: {count} original PCM windows retain their SHA-256 and reproduce reported metrics within 0.001 dB.')

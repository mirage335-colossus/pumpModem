"""Read the archived cable capture and extract spectrograms; never access hardware."""
from pathlib import Path
import gzip, hashlib, json
import numpy as np

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
capture=ROOT/'local-evidence/helper-live-cable/capture-40s.f32.gz'
raw=gzip.open(capture,'rb').read()
expected='cd15730de6c0b67b2ce69f2e08e9d6c33fdef7724f5ec2a7ed074f819a56a405'
assert hashlib.sha256(raw).hexdigest()==expected
x=np.frombuffer(raw,dtype='<f4').astype(np.float64)
fs=48000

def stft(begin,end,n,hop):
    start=np.arange(round(begin*fs),min(round(end*fs),len(x))-n+1,hop,dtype=np.int64)
    window=np.hanning(n)
    f=np.fft.rfftfreq(n,1/fs); keep=f<=3000
    power=[]
    for k in range(0,len(start),128):
        frames=x[start[k:k+128,None]+np.arange(n)[None,:]]*window
        z=np.fft.rfft(frames,axis=1)
        # One-sided PSD estimate; absolute normalization cancels in the displayed reference.
        q=abs(z[:,keep])**2/(fs*np.sum(window**2));q[:,1:]*=2
        power.append(q)
    power=np.concatenate(power)
    return (start+(n-1)/2)/fs, f[keep],power

t,f,p=stft(0,len(x)/fs,4096,960)
tz,fz,pz=stft(10.8,12.4,1024,96)
ref=float(np.max(p))
np.savez_compressed(HERE/'spectrogram-data.npz',time=t,frequency=f,power_db=(10*np.log10(np.maximum(p/ref,1e-16))).astype('f4'),
                    zoom_time=tz,zoom_frequency=fz,zoom_power_db=(10*np.log10(np.maximum(pz/ref,1e-16))).astype('f4'))
meta={'description':'Archived non-FHSS analog cable recording; no new hardware acquisition or RF path-loss simulation.',
      'capture_sha256':expected,'compressed_sha256':hashlib.sha256(capture.read_bytes()).hexdigest(),
      'sample_rate_hz':fs,'samples':len(x),'duration_seconds':len(x)/fs,
      'overview':{'window':'symmetric Hann','samples':4096,'hop_samples':960,'window_ms':4096/fs*1000,'fft_spacing_hz':fs/4096},
      'zoom':{'window':'symmetric Hann','samples':1024,'hop_samples':96,'window_ms':1024/fs*1000,'fft_spacing_hz':fs/1024,'capture_seconds':[10.8,12.4]},
      'color_scale':'-75 to 0 dB relative to maximum overview one-sided PSD; same reference in both panels.',
      'time_axis':'Original capture-file seconds; no onset, phase, frequency or timing correction applied.',
      'limits':['Window duration limits time resolution; FFT spacing is not a calibrated timing resolution.',
                'A magnitude spectrogram cannot distinguish the sign of a steady auxiliary tone.',
                'Recorded historical waveform; not a new test of the current optional no-ramp mode.',
                'No RF hopping, 180 dB path loss, or comparison with new receiver corrections is represented.']}
(HERE/'visual-data-provenance.json').write_text(json.dumps(meta,indent=2)+'\n')
print('Extracted archived-capture spectrograms:',p.shape,pz.shape,'duration',len(x)/fs)

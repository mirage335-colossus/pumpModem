import json, math, sys
from pathlib import Path
import numpy as np

ROOT=Path('/home/user/___quick/p/_cur/dataPump/pumpModem')
PREFIX=Path('/tmp')
NAMES=('levels-g0','levels-g6','levels-g12','frequency-g0','repeat-g0','maximum-g0')
BANDS=((300.,18300.),(20.,20000.))

def fft_mask_measure(x,fs,hint,width):
    # Independent spectral-bin estimator: no sinusoidal or analyzer fitting.
    x=np.asarray(x,dtype=np.float64)
    n=x.size
    w=np.hanning(n)
    z=np.fft.rfft((x-x.mean())*w)
    power=np.abs(z)**2/(n*np.dot(w,w))
    power[1:-1]*=2
    freq=np.fft.rfftfreq(n,1/fs)
    search=(freq>=hint-2)&(freq<=hint+2)
    indexes=np.flatnonzero(search)
    peak=int(indexes[np.argmax(power[indexes])])
    found=peak*fs/n
    fund=np.abs(np.arange(freq.size)-peak)<=width
    dc=np.arange(freq.size)<=width
    harmonic=np.zeros(freq.size,dtype=bool)
    for order in range(2,7):
        folded=abs((order*found+fs/2)%fs-fs/2)
        center=int(round(folded*n/fs))
        harmonic|=np.abs(np.arange(freq.size)-center)<=width
    signal=float(power[fund].sum())
    out=[]
    for low,high in BANDS:
        inside=(freq>=low)&(freq<=high)
        noise=float(power[inside&~fund&~harmonic&~dc].sum())
        nd=float(power[inside&~fund&~dc].sum())
        out.append(dict(low_hz=low,high_hz=high,signal_power_fs2=signal,
                        noise_power_fs2=noise,noise_and_distortion_power_fs2=nd,
                        snr_db=10*math.log10(signal/noise),sinad_db=10*math.log10(signal/nd)))
    return dict(window_samples=n,fft_bin_width_hz=fs/n,fundamental_fft_hz=found,
                excluded_half_width_native_bins=width,bands=out)

def load(name):
    folder=PREFIX/('pump-snr-20260920-'+name)
    doc=json.loads((folder/'analysis-mono.json').read_text())
    pcm=np.memmap(folder/'capture-stereo.f32',dtype='<f4',mode='r').reshape(-1,2)
    return doc,pcm

def steady(doc, strong_only=False):
    return [m for m in doc['measurements'] if m['analysis']=='stationary_single_tone'
            and m['window']=='steady' and (not strong_only or m['tx_segment']['tx_peak_requested']>=.9)]

def pooled(measures):
    out=[]
    for index,(low,high) in enumerate(BANDS):
        bs=[m['bands'][index] for m in measures]
        durations=[m['duration_seconds'] for m in measures]
        signal=sum(b['signal_power_fs2']*t for b,t in zip(bs,durations))
        noise=sum(b['noise_power_fs2']*t for b,t in zip(bs,durations))
        nd=sum(b['noise_and_distortion_power_fs2']*t for b,t in zip(bs,durations))
        out.append(dict(low_hz=low,high_hz=high,
            pooled_snr_db=10*math.log10(signal/noise),
            pooled_sinad_db=10*math.log10(signal/nd),
            snr_min_db=min(b['snr_db'] for b in bs),snr_max_db=max(b['snr_db'] for b in bs),
            sinad_min_db=min(b['sinad_db'] for b in bs),sinad_max_db=max(b['sinad_db'] for b in bs),
            mean_signal_power_fs2=signal/sum(durations),
            mean_noise_power_fs2=noise/sum(durations),
            mean_noise_and_distortion_power_fs2=nd/sum(durations)))
    return out

result={'method':'Independent one-sided Hann FFT power integration at the exact archived steady capture windows. Exclude DC, fundamental, and folded harmonics 2..6 with +/-2 and +/-3 native-bin masks. No analyzer core is called for this independent check. Masking omits some adjacent noise and slow sidebands, so slight positive SNR bias is expected.',
        'sampling_caveat':'The ADC/DAC may share a clock; these near-coherent stationary tones do not prove performance for independent clocks or nonstationary signals.',
        'independent_fft_rows':[],'pooled_plateaus':{},'maximum_channel_comparison':{},'frequency_response':{}}
for name in NAMES:
    doc,pcm=load(name)
    for m in steady(doc):
        if m['tx_segment']['frequency_hz']!=997 or m['tx_segment']['tx_peak_requested']<.35:continue
        start,end=m['capture_start_sample'],m['capture_end_sample']
        x=np.asarray(pcm[start:end],dtype=np.float64).mean(axis=1)
        checks=[]
        for width in (2,3):
            check=fft_mask_measure(x,doc['sample_rate'],997,width)
            for b,reference in zip(check['bands'],m['bands']):
                b['reported_snr_db']=reference['snr_db'];b['reported_sinad_db']=reference['sinad_db']
                b['snr_difference_db']=b['snr_db']-reference['snr_db']
                b['sinad_difference_db']=b['sinad_db']-reference['sinad_db']
            checks.append(check)
        result['independent_fft_rows'].append(dict(dataset=name,segment_index=m['segment_index'],
            tx_peak=m['tx_segment']['tx_peak_requested'],capture_start_sample=start,capture_end_sample=end,
            independent_checks=checks))
    if name in ('repeat-g0','maximum-g0'):
        measurements=steady(doc,True)
        result['pooled_plateaus'][name]=dict(count=len(measurements),
            tx_peak=measurements[0]['tx_segment']['tx_peak_requested'],
            output_volume_percent=95 if name=='repeat-g0' else 100,
            input_gain_db=0,window_seconds=measurements[0]['duration_seconds'],
            aggregation='Sum signal/noise/distortion powers weighted by window duration; no overlapping first/second-second windows pooled.',
            bands=pooled(measurements))
    if name=='frequency-g0':
        response=[]
        for m in steady(doc):
            signal=m['fitted_signal_power_fs2']
            tx_power=m['tx_segment']['tx_peak_requested']**2/2
            response.append(dict(frequency_hz=m['frequency_hint_hz'],signal_rms_dbfs=10*math.log10(signal),
                                 gain_db=10*math.log10(signal/tx_power)))
        reference=next(r['gain_db'] for r in response if r['frequency_hz']==997)
        for r in response:r['gain_relative_to_997_db']=r['gain_db']-reference
        result['frequency_response']=dict(points=response,min_gain_db=min(r['gain_db'] for r in response),
            max_gain_db=max(r['gain_db'] for r in response),
            peak_to_peak_gain_db=max(r['gain_db'] for r in response)-min(r['gain_db'] for r in response),
            note='Same commanded0.95peak and fixed output95% control at8frequencies313..18203Hz; gain includes fixed output/control path and mono averaging. No dense passband flatness inferred.')

# Explicitly separate use of the existing analyzer for left/right comparison.
sys.path.insert(0,str(ROOT/'tools'))
from cable_snr_analysis import analyze_tone
doc,pcm=load('maximum-g0')
maximum=steady(doc,True)
for channel,index in (('left',0),('right',1)):
    measurements=[];independent=[]
    for m in maximum:
        x=np.asarray(pcm[m['capture_start_sample']:m['capture_end_sample'],index],dtype=np.float64)
        fit=analyze_tone(x,doc['sample_rate'],997.)
        measurements.append(dict(duration_seconds=fit['duration_seconds'],bands=fit['bands']))
        independent.append(fft_mask_measure(x,doc['sample_rate'],997,3))
    result['maximum_channel_comparison'][channel]=dict(count=len(measurements),
        method='Existing analyzer applied separately to the same three center windows; also independent FFT mask values retained.',
        bands=pooled(measurements),independent_fft=independent)
result['maximum_channel_comparison']['mono']=result['pooled_plateaus']['maximum-g0']
all_deltas=[abs(b['snr_difference_db']) for r in result['independent_fft_rows'] for c in r['independent_checks'] for b in c['bands']]
all_sinad=[abs(b['sinad_difference_db']) for r in result['independent_fft_rows'] for c in r['independent_checks'] for b in c['bands']]
result['independent_summary']=dict(strong997_center_windows=len(result['independent_fft_rows']),
    max_abs_snr_difference_db=max(all_deltas),max_abs_sinad_difference_db=max(all_sinad),
    all_within_0_1_db=max(all_deltas+all_sinad)<=.1)
Path('/tmp/pump-snr-independent-check.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k in ('independent_summary','pooled_plateaus','frequency_response')},indent=2))
for name,channel in result['maximum_channel_comparison'].items():
 print(name,[(b['pooled_snr_db'],b['pooled_sinad_db']) for b in channel['bands']])

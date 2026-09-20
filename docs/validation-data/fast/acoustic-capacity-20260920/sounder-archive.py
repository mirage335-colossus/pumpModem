import csv, hashlib, json, shutil
from pathlib import Path
import numpy as np

destination = Path('docs/validation-data/fast/acoustic-capacity-20260920')
names = ('a08','a20','a40','a20-right')
manifest, rows, spectra = [], [], {}
for name in names:
    source = Path('/tmp/fast-acoustic-sounder-'+name)
    report = json.loads((source/'analysis.json').read_text())
    metadata = json.loads((source/'metadata.json').read_text())
    capture = json.loads((source/'capture-metadata.json').read_text())
    shutil.copyfile(source/'analysis.json', destination/(name+'-analysis.json'))
    shutil.copyfile(source/'metadata.json', destination/(name+'-stimulus.json'))
    raw = source/'capture-stereo.f32'
    manifest.append(dict(name=name, source_directory=str(source), capture_path=str(raw),
                         capture_bytes=raw.stat().st_size,
                         capture_sha256=hashlib.sha256(raw.read_bytes()).hexdigest(),
                         transmit_mono_sha256=metadata['transmit_mono_sha256'],
                         transmit_stereo_sha256=hashlib.sha256((source/'transmit-stereo.f32').read_bytes()).hexdigest(),
                         playback_routing='right_only' if name.endswith('right') else 'identical_both_channels',
                         source_volume=capture.get('source_volume_after'), sink_volume=capture.get('sink_volume_after'),
                         capture_complete=capture.get('complete'),
                         raw_spectral_files=[str(p) for p in source.glob('spectral-analysis-*.npz')]))
    for channel, measurement in report['measurements'].items():
        arrays = dict(np.load(source/('spectral-analysis-'+channel+'.npz')))
        f = arrays['frequency_hz']
        columns = ['frequency_hz','linear_response_gain_power','coherence','source_psd','heldout_residual_psd',
                   'repeat_residual_psd','silence_psd','capacity_gain_power','capacity_noise_psd']
        averaged = []
        for left in np.arange(0,24000,50):
            mask = (f >= left) & (f < left+50)
            averaged.append([left+25,float(np.mean(arrays['transfer_real'][mask]**2+arrays['transfer_imag'][mask]**2))]
                            +[float(np.mean(arrays[key][mask])) for key in columns[2:]])
        averaged = np.asarray(averaged)
        np.savetxt(destination/(name+'-'+channel+'-spectrum-50hz.csv'),averaged,delimiter=',',header=','.join(columns),comments='',fmt='%.10g')
        if channel=='arithmetic_mean':spectra[name]=averaged
        band = next(b for b in measurement['bands'] if b['low_hz']==300 and b['high_hz']==18000)
        rows.append(dict(name=name,channel=channel,peak_source=metadata['amplitude'],
                         source_rms=metadata['active_period_levels']['rms'],
                         capture_active_rms=measurement['active_capture_levels']['rms'],
                         capture_peak=measurement['capture_levels']['peak'],
                         clock_ppm=measurement['clock_error_ppm'],
                         uniform_conditional_bps=band['uniform_power_bps'],water_filled_conditional_bps=band['water_filling_bps'],
                         median_coherence=band['median_coherence'],
                         impulse_90_percent_ms=measurement['impulse']['energy_90_percent_span_seconds']*1000,
                         impulse_99_percent_ms=measurement['impulse']['energy_99_percent_span_seconds']*1000,
                         qualification_reasons=';'.join(measurement['qualification_reasons'])))
with (destination/'summary.csv').open('w') as output:
    writer=csv.DictWriter(output,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
(destination/'sounder-manifest.json').write_text(json.dumps(dict(runs=manifest,analysis_source_sha256=hashlib.sha256(Path('tools/acoustic_channel_analysis.py').read_bytes()).hexdigest(),
                                                       tests_source_sha256=hashlib.sha256(Path('tests/test_acoustic_channel_analysis.py').read_bytes()).hexdigest()),indent=2)+'\n')

colors={'a08':'#2768bd','a20':'#18855f','a40':'#cc6030','a20-right':'#8b55a7'}
labels={'a08':'Both speakers, peak 0.08','a20':'Both speakers, peak 0.20','a40':'Both speakers, peak 0.40','a20-right':'Right speaker, peak 0.20'}
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1180" height="850" viewBox="0 0 1180 850">',
     '<rect width="1180" height="850" fill="white"/>',
     '<g font-family="DejaVu Sans, sans-serif" fill="#172638">',
     '<text x="60" y="36" font-size="23" font-weight="bold">Measured nearby-speaker / microphone acoustic channel</text>',
     '<text x="60" y="61" font-size="14">48 kHz float capture · fixed OS levels · independent random-phase held-out response · microphone arithmetic mean</text>']
for i,name in enumerate(names):
    x=60+i*280
    svg += [f'<line x1="{x}" y1="85" x2="{x+27}" y2="85" stroke="{colors[name]}" stroke-width="3"/>',
            f'<text x="{x+35}" y="90" font-size="13">{labels[name]}</text>']

def panel(x,y,w,h,title,ymin,ymax,yticks,series):
    svg.append(f'<text x="{x}" y="{y-15}" font-size="17" font-weight="bold">{title}</text>')
    for value in yticks:
        py=y+h-(value-ymin)/(ymax-ymin)*h
        svg.extend([f'<line x1="{x}" y1="{py:.2f}" x2="{x+w}" y2="{py:.2f}" stroke="#dce3e9"/>',
                    f'<text x="{x-8}" y="{py+4:.2f}" font-size="11" text-anchor="end">{value}</text>'])
    for value in (0,3,6,9,12,15,18):
        px=x+value/18*w
        svg.extend([f'<line x1="{px:.2f}" y1="{y}" x2="{px:.2f}" y2="{y+h}" stroke="#edf0f3"/>',
                    f'<text x="{px:.2f}" y="{y+h+17}" font-size="11" text-anchor="middle">{value}</text>'])
    svg.append(f'<text x="{x+w/2}" y="{y+h+35}" font-size="12" text-anchor="middle">Frequency (kHz)</text>')
    for name,values in series.items():
        f=spectra[name][:,0];mask=(f>=300)&(f<=18000)
        points=' '.join(f'{x+ff/18000*w:.2f},{y+h-(min(ymax,max(ymin,v))-ymin)/(ymax-ymin)*h:.2f}' for ff,v in zip(f[mask],values[mask]))
        svg.append(f'<polyline points="{points}" fill="none" stroke="{colors[name]}" stroke-width="1.3"/>')

panel(60,145,500,250,'Linear response gain (dB)',-35,10,(-30,-20,-10,0,10),
      {n:10*np.log10(np.maximum(a[:,1],1e-30)) for n,a in spectra.items()})
panel(655,145,460,250,'Signal / held-out residual (dB)',-10,40,(-10,0,10,20,30,40),
      {n:10*np.log10(np.maximum(a[:,3]*a[:,7]/np.maximum(a[:,8],1e-30),1e-30)) for n,a in spectra.items()})
panel(60,490,500,245,'Held-out residual density (dB FS²/Hz)',-125,-65,(-120,-110,-100,-90,-80,-70),
      {n:10*np.log10(np.maximum(a[:,4],1e-30)) for n,a in spectra.items()})
panel(655,490,460,245,'Independent phase-block coherence',0,1,(0,.25,.5,.75,1),
      {n:a[:,2] for n,a in spectra.items()})
svg += ['<text x="60" y="802" font-size="13">Higher level improves low bands; residual distortion/time variation limits gains above 8 kHz. Stereo wins at this placement.</text>',
        '<text x="60" y="824" font-size="12">50 Hz power averages. Residual includes noise, nonlinearities, channel variation and estimation error; curves are not modem capacity or reliability.</text>',
        '</g></svg>']
(destination/'sounder-comparison.svg').write_text('\n'.join(svg)+'\n')
print(destination)

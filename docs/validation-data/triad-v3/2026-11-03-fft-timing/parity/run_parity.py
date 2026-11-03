#!/usr/bin/env python3
"""Bounded native-sample parity fixtures for the isolated FFT timing path."""
import hashlib, json, os, pathlib, subprocess, time

ROOT=pathlib.Path(__file__).resolve().parents[3]
OUT=pathlib.Path(__file__).resolve().parent
BIN=ROOT/'build/dev-cli/triad_long'
PY=pathlib.Path('/tmp/triad-weak-env/bin/python')
EXPECTED_BIN='9cdf82f396d9be9a7d11b66b45b254e5f18b3857f27b08aed58430dce6d8ea47'
PRIOR=(-4/1024,4/1024)
GRID_STEP=1/1024

def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while chunk:=f.read(1<<20): h.update(chunk)
    return h.hexdigest()

def run(name,argv,timeout=60):
    log=OUT/(name+'.log'); before=time.monotonic()
    try:
        with log.open('xb') as output:
            p=subprocess.run(argv,cwd=ROOT,stdout=output,stderr=subprocess.STDOUT,
                timeout=timeout,check=False,env={**os.environ,'OMP_NUM_THREADS':'1',
                'OPENBLAS_NUM_THREADS':'1','MKL_NUM_THREADS':'1','NUMEXPR_NUM_THREADS':'1'})
    except subprocess.TimeoutExpired:
        receipt={'name':name,'status':'timeout','timeout_seconds':timeout,
                 'elapsed_seconds':time.monotonic()-before,'argv':list(map(str,argv)),
                 'log_sha256':sha(log)}
        (OUT/(name+'.status.json')).write_text(json.dumps(receipt,indent=2)+'\n')
        raise
    receipt={'name':name,'status':'completed' if p.returncode==0 else 'failed',
        'exit_code':p.returncode,'elapsed_seconds':time.monotonic()-before,
        'argv':list(map(str,argv)),'log_sha256':sha(log)}
    (OUT/(name+'.status.json')).write_text(json.dumps(receipt,indent=2)+'\n')
    if p.returncode:
        raise RuntimeError(f'{name} exited {p.returncode}; inspect {log}')
    return receipt

def common(seed,stress):
    lo,hi=PRIOR
    values=['--rate','640' if stress else '36','--added','1','--sample-rate','1024','--seconds','8',
        '--cn0','12','--rf-hz','0','--nominal-if-hz','1725',
        '--base-oscillator','gpsdo-ocxo','--shift-oscillator','gpsdo-ocxo',
        '--clock-sync','GPS_10ms-1ms_region-0ms_offset','--audio-error-ms','0',
        '--margin','1','--clock-ppm','0','--epoch-offsets','0',
        '--frequency','0','0','1','--frequency-drift','0',
        '--phase-diffusion',str(0.5*3.141592653589793/180),
        '--basis','two-ray','--tap-delay-ms','.5','--tap-doppler-hz','.1',
        '--arrival-prior-seconds',str(lo),str(hi),
        '--seed',str(seed),'--key-seed',str(seed+101),'--channel-seed',str(seed+201),
        '--oscillator-seed',str(seed+301),'--noise-seed',str(seed+401),
        '--frontend-seed',str(seed+501),'--path-seed',str(seed+601),
        '--signal-bit','1','--control','signal']
    if stress:
        values += ['--channel','none','--spread-preset','hf-best','--margin','1',
            '--selective-perturbation','each-65','--selective-model','dispersive-dft',
            '--selective-modulation-rate-hz','.1','--fhss','--burst-wait-tiles','3']
    else:
        values += ['--channel','none','--spread-preset','none','--selective-perturbation','off']
    return values

def main():
    if any(p.name != pathlib.Path(__file__).name and p.name != '__pycache__' for p in OUT.iterdir()):
        raise SystemExit('output directory must be fresh')
    if sha(BIN)!=EXPECTED_BIN: raise SystemExit('native binary identity differs from the accepted source build')
    plan={'schema':'triad-fft-native-parity-v1','binary_sha256':sha(BIN),
        'fixed_public_arrival_prior_seconds':list(PRIOR),
        'same_capture_contract':'Each base export and nine-point direct-reference export reuse all seven seed roots, waveform/channel/oscillator/noise/frontend settings and explicit arrival prior. Only --start template bank changes. Observation and validity bytes must be identical.',
        'cases':[{'name':'flat-fractional-ray','seed':3201,'rate_hz':36,'control':'none/Off, true two-ray .5ms/.1Hz','missed_dwells':[],'burst_wait_tiles':0},
            {'name':'hf-Each65-FHSS-burst','seed':4201,'rate_hz':640,'rate_exception_reason':'rate36/72/360 FHSS have no protected dwell cells; rate640 was an exploratory compatible geometry. Final accepted parity uses rate1200 in the user-listed set.','control':'HF-best, Each65 at .1Hz, FHSS envelope, burst-wait tiles, missed dwells selected from declared exported calendar','missed_dwells':'selected after configure-only calendar receipt','burst_wait_tiles':3,'status':'exploratory exports retained; not part of accepted parity result'}],
        'lag_grid_samples':list(range(9)),'sample_rate_hz':1024.,'cn0_db_hz':12.,'target':['exact observation/mask bytes','FFT full four-column Gram/correlations versus raw native direct products at every lag','complete-lattice actual-noisy score/threshold parity with grouped direct reference'],
        'limits':['two independent simulation conditions, not reliability trials','no clock/epoch/CFO acquisition or receiver integration qualification','each child timeout 60s, one CPU thread; no automatic retries']}
    (OUT/'experiment.json').write_text(json.dumps(plan,indent=2)+'\n')
    for entry in plan['cases']:
        name=entry['name'];seed=entry['seed']; base=common(seed,name!='flat-fractional-ray')
        configure=[str(BIN),'--mode','configure',*base]
        run(name+'-configure',configure)
        # Configure is deterministic and contains only public calendar facts.
        config_log=(OUT/(name+'-configure.log')).read_text()
        (OUT/(name+'-configure.json')).write_text(config_log)
        cfg=json.loads(config_log)['configuration']
        if name=='hf-Each65-FHSS-burst':
            # Keep three erasures separated across the observed source. Derive
            # only from the public configured calendar, never realized truth.
            total=cfg.get('receiver_dwell_count')
            if total is None:
                # Exact declared schedule uses 0.4-second dwells; pad/tail are
                # reported by configure. Round down to a valid final dwell.
                seconds=cfg.get('duration',{}).get('elapsed_source_calendar_seconds')
                if seconds is None: raise RuntimeError('configure receipt lacks public duration for missed-dwell placement')
                total=max(1,int(float(seconds)/.4))
            missed=sorted(set((0,int(total)//2,max(0,int(total)-1))))
            values=base+['--missed-dwells',','.join(map(str,missed))]
            entry['missed_dwells']=missed;entry['configured_receiver_dwell_count']=int(total)
            plan['cases']=list(plan['cases'])
        else: values=base
        low=PRIOR[0]; high=PRIOR[1]
        common_export=['--memory-mib','512','--chunk','4096','--threads','1','--export-max-mib','512']
        sampled=OUT/name/'sampled-base'; direct=OUT/name/'sampled-direct'
        direct_stats=OUT/name/'direct-grouped'
        for label,start in [('base',(low,low,1.)),('direct',(low,high,GRID_STEP))]:
            directory=sampled if label=='base' else direct
            argv=[str(BIN),'--mode','export-sampled',*values,'--start',*map(str,start),
                  *common_export,'--output',str(directory)]
            run(name+'-sampled-'+label,argv)
        # Same physical sampled observation, all public timing groups retained
        # in the direct receiver's native grouped sufficient statistics.
        argv=[str(BIN),'--mode','export-group-stats','--stats-noisy',*values,
              '--start',str(low),str(high),str(GRID_STEP),'--memory-mib','512',
              '--chunk','4096','--block','65536','--output',str(direct_stats)]
        run(name+'-grouped-direct',argv)
        with (OUT/(name+'-configure.json')).open() as f: cfg=json.load(f)
        cfg=cfg['configuration']
        for directory in (sampled,direct,direct_stats):
            manifest=json.loads((directory/'manifest.json').read_text())
            manifest['_files_sha256']={p.name:sha(p) for p in directory.iterdir() if p.is_file()}
            (OUT/(name+'-'+directory.name+'-receipt.json')).write_text(json.dumps(manifest,indent=2)+'\n')
        # Exact raw sample identity is mandatory before comparing receivers.
        for filename in ('observation.c128','validity.u8'):
            if sha(sampled/filename)!=sha(direct/filename):
                raise RuntimeError(f'{name}: direct FFT-reference export changed actual {filename}')
    (OUT/'experiment.json').write_text(json.dumps(plan,indent=2)+'\n')
    print(json.dumps({'status':'exports-complete','cases':len(plan['cases']),
        'binary_sha256':sha(BIN)}))

if __name__=='__main__': main()

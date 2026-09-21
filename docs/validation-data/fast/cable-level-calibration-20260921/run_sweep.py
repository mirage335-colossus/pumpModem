import argparse, json, subprocess, time
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('group',choices=['wide','mid','narrow','radio','radio-narrow'])
p.add_argument('--levels',type=float,nargs='+',default=[.1,.2,.3,.4])
p.add_argument('--bytes',type=int,default=100000)
a=p.parse_args()
root=Path('/tmp/cable-level-20260921')
base=['build/fast_cable_probe','--profile','wire' if a.group in ['wide','mid','narrow'] else 'ssb',
      '--capacity','--stereo','--tail','7']
if a.group=='wide': base+=['--mode','codec','--bytes',str(a.bytes)]
if a.group=='mid': base+=['--mode','raw','--intervals','64','--symbol-rate','1764.7058823529412']
if a.group=='narrow': base+=['--mode','raw','--intervals','16','--symbol-rate','176.47058823529412','--tail','9']
if a.group=='radio': base+=['--mode','raw','--intervals','64']
if a.group=='radio-narrow': base+=['--mode','raw','--intervals','8','--symbol-rate','218.1818181818182',
                                  '--qam','16','--depth','1','--tail','9']
for level in a.levels:
    name=f'{a.group}-a{round(level*100):03d}'
    if a.group=='wide' and a.bytes!=100000: name+=f'-b{a.bytes}'
    prefix=root/name
    if prefix.with_suffix('.json').exists(): raise RuntimeError(f'Existing run: {name}')
    cmd=base+['--amplitude',str(level),'--capture-save',str(prefix)+'.f32',
              '--tx-bits-save',str(prefix)+'.bits','--rx-symbols-save',str(prefix)+'.iq']
    prefix.with_suffix('.command.json').write_text(json.dumps(cmd,indent=2)+'\n')
    print('START',name,flush=True)
    start=time.monotonic()
    with prefix.with_suffix('.json').open('w') as out,prefix.with_suffix('.log').open('w') as err:
        result=subprocess.run(cmd,stdout=out,stderr=err,timeout=120)
    if result.returncode: raise RuntimeError(f'{name}: diagnostic exited {result.returncode}')
    d=json.loads(prefix.with_suffix('.json').read_text())
    print(json.dumps(dict(name=name,elapsed=time.monotonic()-start,
                         **{k:d[k] for k in ['exact','rx_intervals','tx_intervals','ldpc_failed_frames',
                            'ldpc_changed_bits','raw_wrong_bits','raw_erased_bits','fifo_overflow','tx_level','signal_level']})),flush=True)
    if d['fifo_overflow'] or d['capture_error'] or d['playback_error']: raise RuntimeError('Audio transport failure')

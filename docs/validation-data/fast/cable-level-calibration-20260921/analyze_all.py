"""Reproduce known-source residual metrics from retained completed probes."""
import argparse, csv, hashlib, json, sys
from pathlib import Path
import numpy as np

p=argparse.ArgumentParser()
p.add_argument('directory',type=Path)
p.add_argument('--repo',type=Path,default=Path.cwd())
a=p.parse_args()
sys.path.insert(0,str(a.repo/'tools'))
import fast_known_evm

count=0
for path in sorted(a.directory.glob('*-a*.json')):
    if any(word in path.stem for word in ['known','command']): continue
    report=json.loads(path.read_text())
    if not isinstance(report,dict) or report.get('mode') not in ['raw','codec']: continue
    bits=path.with_suffix('.bits');iq=path.with_suffix('.iq')
    result,rows=fast_known_evm.analyze(report,np.fromfile(bits,'u1'),np.fromfile(iq,'<c8'))
    if report['mode']=='raw':
        assert result['summary']['known_payload_bit_errors']==report['raw_wrong_bits'],path
    result['inputs']={name:{'path':str(item),'sha256':hashlib.file_digest(open(item,'rb'),'sha256').hexdigest()}
                      for name,item in [('probe',path),('tx_bits',bits),('rx_symbols',iq)]}
    path.with_name(path.stem+'-known.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    count+=1
    print(path.stem,round(result['summary']['known_signal_to_residual_db'],3),
          round(result['summary']['known_payload_ber']*100,5),'% errors',flush=True)
print('Complete:',count,'probes; independent raw BER agrees with probe in every raw case')

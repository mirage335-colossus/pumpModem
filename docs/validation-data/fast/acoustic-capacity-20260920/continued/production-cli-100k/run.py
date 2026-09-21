from pathlib import Path
import subprocess,time,json,random,hashlib
p=Path('/tmp/acoustic-production-100k')
source=random.Random(547).randbytes(100000);(p/'source.bin').write_bytes(source)
pump=str(Path('build/pump').resolve())
rxcmd=[pump,'fast-listen','--profile','acoustic','--device','default','--save',str(p/'received.bin'),'--seconds','60','--json']
txcmd=[pump,'fast-tx','--profile','acoustic','--device','default','--input',str(p/'source.bin'),'--json']
start=time.monotonic();rx=None;tx=None
try:
    with (p/'rx.json').open('w') as ro,(p/'rx.log').open('w') as re,(p/'tx.json').open('w') as to,(p/'tx.log').open('w') as te:
        rx=subprocess.Popen(rxcmd,stdout=ro,stderr=re)
        time.sleep(1)
        tx=subprocess.Popen(txcmd,stdout=to,stderr=te)
        tx.wait(timeout=max(1,63-(time.monotonic()-start)))
        rx.wait(timeout=max(1,63-(time.monotonic()-start)))
finally:
    for child in [tx,rx]:
        if child is not None and child.poll() is None:child.terminate()
    for child in [tx,rx]:
        if child is not None:
            try:child.wait(timeout=2)
            except subprocess.TimeoutExpired:child.kill();child.wait()
    received=(p/'received.bin').read_bytes() if (p/'received.bin').exists() else b''
    result={'wall_seconds':time.monotonic()-start,'rx_command':rxcmd,'tx_command':txcmd,'rx_exit':rx.returncode if rx else None,'tx_exit':tx.returncode if tx else None,'source_bytes':len(source),'received_bytes':len(received),'source_sha256':hashlib.sha256(source).hexdigest(),'received_sha256':hashlib.sha256(received).hexdigest(),'exact':source==received}
    (p/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

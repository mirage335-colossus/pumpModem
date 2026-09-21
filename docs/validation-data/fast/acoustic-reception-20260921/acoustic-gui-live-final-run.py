from pathlib import Path
import subprocess,time,json
p=Path('/tmp/acoustic-gui-live-final');p.mkdir(exist_ok=True)
children=[];start=time.monotonic()
try:
 with (p/'rx.log').open('w') as rxlog,(p/'tx.log').open('w') as txlog:
  children.append(subprocess.Popen(['/tmp/acoustic-gui-live-debug-final','rx',str(p/'received.txt')],stdout=rxlog,stderr=subprocess.STDOUT))
  time.sleep(2)
  children.append(subprocess.Popen(['/tmp/acoustic-gui-live-debug-final','tx',str(p/'unused.txt')],stdout=txlog,stderr=subprocess.STDOUT))
  for child in reversed(children):child.wait(timeout=max(1,65-(time.monotonic()-start)))
finally:
 for child in children:
  if child.poll() is None:child.terminate()
 for child in children:
  try:child.wait(timeout=2)
  except subprocess.TimeoutExpired:child.kill();child.wait()
 result={'wall_seconds':time.monotonic()-start,'exit_codes':[x.returncode for x in children]}
 (p/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

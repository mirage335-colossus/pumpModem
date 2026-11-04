from pathlib import Path
import hashlib,json,shutil,subprocess,tarfile
base=Path('local-evidence/triad-performance-completion')
out=Path('docs/validation-data/triad-v3/2026-11-04-performance-completion')
out.mkdir(parents=True,exist_ok=True)
def archive(name,paths):
    with tarfile.open(out/name,'w:gz',compresslevel=9) as tf:
        for p,arc in paths:
            p=Path(p)
            for f in sorted(p.rglob('*')) if p.is_dir() else [p]:
                if not f.is_file() or '__pycache__' in f.parts or 'compiled-cache' in f.parts or f.suffix in ('.pyc','.c128','.f64','.u64') or f.name=='.coverage':continue
                tf.add(f,arcname=str(Path(arc)/(f.relative_to(p) if p.is_dir() else '')),recursive=False)
archive('cpu-routines.tar.gz',[(base/x,x) for x in ['cpu-guard-001','cpu-guard-002','cpu-family-holds-001']])
archive('cpu-lifecycle.tar.gz',[(base/'cpu-lifecycle-001-parent','cpu-lifecycle-001-parent')])
shutil.copyfile('/tmp/triad-completion-cloud/triad-final-evidence.tar.gz',out/'gpu-evidence.tar.gz')
archive('decision-controls.tar.gz',[(base/'controls','controls'),(base/'native-controls-parent','native-controls-parent'),('local-evidence/triad-guard-actual-controls/run_controls.py','native-control-driver.py')])
archive('method-assessment.tar.gz',[(base/x,x) for x in ['method-assessment','policy-futility-fix','lifecycle-regime-repair']]+[(base/'gpu-final-download'/x,x) for x in ['gpu-methods-001-failed.json','gpu-methods-002.json','gpu-methods-002.log']])
archive('unit-and-coverage.tar.gz',[(base/'unit','unit')])
sources=sorted(set([*Path('tools').glob('triad*.py'),*Path('tools').glob('triad*json'),*Path('tests').glob('test_triad*.py'),Path('.github/workflows/triad-performance.yml')]))
archive('suite-sources.tar.gz',[(p,p) for p in sources])
identity={'git_head':subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),'worktree_scope':'Modified isolated performance harness/tests/docs; native DSP implementation unchanged in this work','source_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},'native_binary_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [Path('build/dev-cli/triad_long'),Path('build/dev-cli/triad_planner_perf'),Path('local-evidence/triad-performance-trace/reference-triad_long-gcc142'),Path('build/dev-cli-242-reference-source/triad_planner_perf')]},'artifact_scope':'Technical evidence only; private provider operations/account/billing excluded','exclusions':['regenerable sampled binary arrays','compiled caches','interpreter bytecode','coverage execution database; exported coverage retained']}
(out/'source-identities.json').write_text(json.dumps(identity,indent=2)+'\n')
shutil.copyfile(base/'package-evidence.py',out/'package-evidence.py')
print(json.dumps({p.name:p.stat().st_size for p in sorted(out.iterdir()) if p.is_file()},indent=2))

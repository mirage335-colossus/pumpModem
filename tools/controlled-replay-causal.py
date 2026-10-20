#!/usr/bin/env python3
"""Fixed-source, fixed-key diagnostic. Never qualification or a production fix."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import zipfile

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('sdk_capture_base', HERE/'sdk-rev-smoke-capture.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
BASE = '42d7a0cb82c027f906555c39adccb7ec6fa91f21'
EPOCH = '1792499067.0'
KEYS = {
 'key1': {'artifact':12264400813, 'zip':'7c36d731da54eab4ceeffa2b398b27340a86b91a1311b443ec2bf03a7909b675',
          'key':'7e045654ceb6c12e37badb33573d7d90b6e3a21fe5c2bed7bb045dc9865311a9',
          'mac':'0deb442a705d6f8abbb2003e1cdb25d399f63cda6d7f7668670c347e25b2e229'},
 'key2': {'artifact':12264609208, 'zip':'e4454feb9c97ebd21aa2ccbdbaed50eed3473f07279e802eaf98cc0c596b04e8',
          'key':'ef959c6d0facfd50ee8a13e14aa04d734833c64462c507f9808aaf2a592670ce',
          'mac':'9f8987e2fbed314e07f192719c69eb8b099b9b2c248075eadd65e212916ece11'},
}

def objects(path):
    assert Path(path).stat().st_size <= base.STDOUT_LIMIT, 'typed diagnostic stream exceeds capture bound'
    return [base.unique_json(line) for line in Path(path).read_text().splitlines() if line.startswith('{')]

def held_settings(boundaries):
    pre = next(r for r in boundaries if r['boundary'] == 'phase20_before_binary_transmit')
    # The generated destination filename contains the run's wall clock. The
    # trusted file's bytes and loaded key MAC, rather than its label, identify it.
    return {k:v for k,v in pre['fixture_settings'].items() if k != 'key_path_label'}

def prepare(args):
    repo = Path(args.base).resolve()
    assert subprocess.check_output(['git','-c','safe.directory='+str(repo),'-C',str(repo),'rev-parse','HEAD'],text=True).strip() == BASE
    overlay = HERE/'controlled-replay-causal'
    manifest = json.loads((overlay/'overlay-manifest.json').read_text())
    assert manifest['base_commit'] == BASE and manifest['diagnostic_only'] is True
    assert base.digest(overlay/'overlay.patch') == manifest['patch_sha256']
    for item in manifest['files']:
        path = repo/item['path']
        assert (base.digest(path) == item['preimage_sha256']) if item['preimage_sha256'] else not path.exists()
    command = ['git','-c','safe.directory='+str(repo),'-C',str(repo),'apply']
    subprocess.run(command+['--check',str(overlay/'overlay.patch')],check=True)
    subprocess.run(command+[str(overlay/'overlay.patch')],check=True)
    for item in manifest['files']:
        assert base.digest(repo/item['path']) == item['postimage_sha256']
    sdk = repo/'build/sdk/share/datapump-sdk/manifest.json'
    assert base.digest(sdk) == '4fdcc4b8d57e0a67eeb2ce7fac70667b75f40f01215105de95b771c77c5303c3'
    base.atomic_json(args.record, {'base_commit':BASE,'tool_commit':os.environ['GITHUB_SHA'],
        'overlay':manifest,'sdk_manifest':base.file_record(sdk),'epoch':EPOCH,
        'purpose':'first terminal loss plus matched full/minimal trace on retained synthetic keys; not qualification',
        'default_clock_branch':'no Session EpochClock supplied; private epoch_now override only',
        'settings':{'runner':'ubuntu-24.04','container':'ubuntu:24.04','backend':'Rev','LP_NUM_THREADS':1,
                    'key_generation':'production128MiB creation and existing-file rejection retained; trusted fixture copied only after new exclusive creation'}})

def fixture(args):
    pin = KEYS[args.key]
    output = Path(args.output).resolve()
    assert not output.exists()
    with tempfile.TemporaryDirectory(dir=os.environ['RUNNER_TEMP']) as temporary:
        archive = Path(temporary)/'retained.zip'
        with archive.open('wb') as stream:
            subprocess.run(['gh','api','repos/'+os.environ['GITHUB_REPOSITORY']+'/actions/artifacts/'+str(pin['artifact'])+'/zip'],stdout=stream,check=True)
        assert base.digest(archive) == pin['zip']
        with zipfile.ZipFile(archive) as files:
            names = [n for n in files.namelist() if '/smoke-dir/' in n and n.endswith('.key')]
            assert len(names) == 1
            with files.open(names[0]) as source, output.open('xb') as destination:
                shutil.copyfileobj(source,destination)
        assert base.digest(output) == pin['key']
    base.atomic_json(args.record,dict(pin, fixture=base.file_record(output), key_choice=args.key))

def unit(args):
    output = Path(args.output).resolve()
    output.mkdir(parents=True,exist_ok=False)
    with tarfile.open(args.archive,'r:gz') as files:
        names = [m for m in files.getmembers() if m.isfile() and m.name.endswith('/bin/datapump-gui')]
        assert len(names) == 1 and not any(m.issym() or m.islnk() or m.isdev() for m in files.getmembers())
        files.extractall(output/'isolated package with spaces',filter='data')
        gui = output/'isolated package with spaces'/names[0].name
    command = ['xvfb-run','-a','-s','-screen 0 2400x1800x24 -dpi 96','timeout','--kill-after=5','30',
               'env','PATH=','PYTHONHOME=/nonexistent','PYTHONPATH=/nonexistent','LD_LIBRARY_PATH=',str(gui),
               '--smoke-test','--smoke-timeout','600','--smoke-dir',str(output/'unit-fixtures')]
    env = dict(os.environ, DATAPUMP_REPLAY_STAGING_CHECK='1', DATAPUMP_CAUSAL_EPOCH=EPOCH)
    with (output/'stdout.log').open('wb') as out, (output/'stderr.log').open('wb') as err:
        result = subprocess.run(command,env=env,stdout=out,stderr=err)
    records = objects(output/'stdout.log')+objects(output/'stderr.log')
    assignments = [r for r in records if r.get('event') == 'replay_terminal_assignment']
    checks = [r for r in records if r.get('event') == 'replay_terminal_staging_check']
    assert result.returncode == 1 and len(assignments) == 2 and len(checks) == 1
    assert assignments[0]['signal_id'] == 81 and assignments[0]['new_received_bits'] == 32
    assert assignments[1]['signal_id'] == 82 and assignments[1]['old_signal_id'] == 81
    assert assignments[1]['independent_old_terminal'] is True
    assert checks[0]['expected_complete_ids'] == 2 and checks[0]['retained_complete_ids'] == 1
    base.atomic_json(output/'result.json',{'result':'CONFIRMED baseline staging invariant failure; not a receiver reproduction or qualification',
        'application_exit':result.returncode,'command':command,'records':records,'archive':base.file_record(args.archive)})
    shutil.rmtree(output/'isolated package with spaces')

def pcm_report(directory, records):
    seals = [r for r in records if r.get('event') == 'datapump_causal_input_seal']
    assert len(seals) == 1 and seals[0]['complete'] is True
    seal = seals[0]
    assert seal['io_errors'] == 0 and all(seal[k] for k in ('pcm_flush_ok','pcm_close_ok','metadata_flush_ok','metadata_close_ok'))
    path = directory/'source-input.f32'
    metadata = json.loads(path.with_suffix('.f32.json').read_text())
    assert Path(seal['pcm_path']).resolve() == path.resolve()
    assert Path(seal['metadata_path']).resolve() == path.with_suffix('.f32.json').resolve()
    assert metadata['pcm_bytes'] == path.stat().st_size == metadata['captured_samples']*4
    assert not metadata['partition_limited'] and not metadata['pcm_limited']
    assert len(metadata['partitions']) == metadata['total_partitions']
    assert seal['captured_samples'] == metadata['captured_samples'] == metadata['attempted_samples']
    assert seal['partitions'] == metadata['total_partitions']
    assert not metadata['observer_clock_failed'] and metadata['observer_process_cpu_ticks_per_second'] > 0
    parts = metadata['partitions']
    targets = [i for i,p in enumerate(parts) if p['tx_serial'] == 16]
    assert targets, 'binary TX16 input absent'
    last = targets[-1]
    owner, revision = parts[last]['owner_cookie'],parts[last]['revision']
    same = [p for p in parts[:last+1] if p['owner_cookie'] == owner and p['revision'] == revision]
    # The last reset of that owner/revision defines this bank's actual prelude.
    starts = [i for i,p in enumerate(same) if p['frontier_before'] == 0]
    assert starts
    selected = same[starts[-1]:]
    digest = hashlib.sha256()
    frontier = 0
    with path.open('rb') as stream:
        for p in selected:
            assert p['frontier_before'] == frontier and not p['exception']
            assert p['captured_count'] == p['input_count']
            frontier = p['frontier_after']
            assert frontier == p['frontier_before']+p['input_count']
            stream.seek(p['file_sample_offset']*4)
            data = stream.read(p['captured_count']*4)
            assert len(data) == p['captured_count']*4
            digest.update(data)
    lengths = [p['input_count'] for p in selected]
    return {'scope':'bank prelude through last binary TX16 feed; equality is not per-receiver scoring proof',
            'samples':frontier,'pcm_sha256':digest.hexdigest(),'submission_lengths_sha256':hashlib.sha256(json.dumps(lengths).encode()).hexdigest(),
            'partitions':len(selected),'whole_pcm':base.file_record(path),'metadata':base.file_record(path.with_suffix('.f32.json')),'seal':seal}

def one(args, mode, attempt):
    directory = Path(args.evidence).resolve()/'smokes'/mode
    saved = os.environ.copy()
    os.environ.update(DATAPUMP_SMOKE_KEY_FIXTURE=str(Path(args.fixture).resolve()),DATAPUMP_CAUSAL_EPOCH=EPOCH,
                      DATAPUMP_REPLAY_TERMINAL_CENSUS='1',DATAPUMP_CAUSAL_PCM=str(directory/'source-input.f32'))
    original = base.subprocess.run
    def launch(command, **kwargs):
        if mode == 'minimal':
            i = command.index('--verbose')
            del command[i:i+3]  # --verbose --verbose-file PATH; preserve actual command in run.json.
        return original(command, **kwargs)
    reason = None
    try:
        base.subprocess.run = launch
        values = argparse.Namespace(archive=args.archive,evidence=str(directory),artifact_root=args.evidence,attempt=attempt,
                                    base_commit=BASE,tool_commit=os.environ['GITHUB_SHA'],source_manifest=args.source_manifest,settings=args.source_manifest)
        try: base.capture(values)
        except SystemExit as error: reason = str(error)
    finally:
        base.subprocess.run = original
        os.environ.clear();os.environ.update(saved)
    if not (directory/'run.json').exists():
        directory.mkdir(parents=True,exist_ok=True)
        result = {'mode':mode,'application_exit':None,'measurement_valid':False,
                  'measurement_error':'setup stopped before a complete application run record',
                  'base_capture_notice':reason,'epoch':EPOCH,'fixture':base.file_record(args.fixture)}
        base.atomic_json(directory/'causal-result.json',result)
        return result
    run = json.loads((directory/'run.json').read_text())
    result = {'mode':mode,'application_exit':run['exit_code'],'base_capture_notice':reason,
              'full_trace_required':mode == 'full','base_run':base.file_record(directory/'run.json'),
              'fixture':base.file_record(args.fixture),'epoch':EPOCH,'measurement_valid':False,
              'elapsed_ns':run['monotonic_elapsed_ns']}
    try:
        assert run['boundary_closure']['valid'] and run['capture_error'] is None
        records = objects(directory/'stdout.log')+objects(directory/'stderr.log')
        if mode == 'full': assert run['trace_closure']['valid']
        pcm = pcm_report(directory,records)
        boundaries = [r for r in records if r.get('event') == 'gui_smoke_binary_boundary_capture']
        assert held_settings(boundaries)['synthetic_key_mac'] == KEYS[args.key]['mac']
        terminals = [r for r in records if r.get('event') == 'replay_terminal_assignment' and r['tx_serial'] == 16]
        summaries = [r for r in records if r.get('event') == 'replay_terminal_summary' and r['tx_serial'] == 16]
        assert len(summaries) == 1
        summary = summaries[0]
        assert summary['handoff_eligible'] and summary['dropped'] == 0 and summary['truncated_records'] == 0
        assert summary['attempts'] == summary['emitted'] == len(terminals) and not summary['io_failed_before_summary']
        result.update(measurement_valid=True,pcm=pcm,terminals=terminals,summary=summary,boundaries=boundaries)
    except (AssertionError,ValueError,KeyError,OSError) as error:
        result['measurement_error'] = type(error).__name__+': '+str(error)
    result['archive'] = run['archive'];result['binary'] = run['binary']
    base.atomic_json(directory/'causal-result.json',result)
    return result

def pair(args):
    assert base.digest(args.fixture) == KEYS[args.key]['key']
    root = Path(args.evidence);root.mkdir(parents=True,exist_ok=False)
    modes = ['full','minimal'] if args.key == 'key1' else ['minimal','full']
    results = []
    for attempt,mode in enumerate(modes,1):
        results.append(one(args,mode,attempt))
        assert base.digest(args.fixture) == KEYS[args.key]['key'], 'trusted fixture changed'
        if results[-1]['application_exit'] is None:
            break  # Shared setup must be repaired before another app attempt.
    matched = len(results) == 2 and all(r['measurement_valid'] for r in results)
    if matched:
        for key in ('samples','pcm_sha256','submission_lengths_sha256'):
            matched = matched and results[0]['pcm'][key] == results[1]['pcm'][key]
        matched = matched and results[0]['binary']['sha256'] == results[1]['binary']['sha256']
        matched = matched and held_settings(results[0]['boundaries']) == held_settings(results[1]['boundaries'])
    base.atomic_json(root/'pair-result.json',{'key':args.key,'epoch':EPOCH,'matched_input':matched,'results':results,
        'scope':'diagnostic only; no single timing pair establishes performance; pre-binary failures are not stop latencies',
        'decision':'first independent terminal loss -> staging; no32 completion -> receiver/input; raw32 final but GUI wrong -> presentation; no failure/incomplete matching -> inconclusive'})
    if not matched or any(r['application_exit'] != 0 for r in results):
        raise SystemExit('functional failure or incomplete/unmatched diagnostic; evidence retained')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command',required=True)
    p=commands.add_parser('prepare');p.add_argument('--base',required=True);p.add_argument('--record',required=True);p.set_defaults(function=prepare)
    p=commands.add_parser('fixture');p.add_argument('--key',choices=KEYS,required=True);p.add_argument('--output',required=True);p.add_argument('--record',required=True);p.set_defaults(function=fixture)
    p=commands.add_parser('unit');p.add_argument('--archive',required=True);p.add_argument('--output',required=True);p.set_defaults(function=unit)
    p=commands.add_parser('pair');p.add_argument('--archive',required=True);p.add_argument('--evidence',required=True);p.add_argument('--fixture',required=True);p.add_argument('--key',choices=KEYS,required=True);p.add_argument('--source-manifest',required=True);p.set_defaults(function=pair)
    args=parser.parse_args();args.function(args)

if __name__ == '__main__':
    main()

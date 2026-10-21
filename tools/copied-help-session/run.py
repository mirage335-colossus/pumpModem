#!/usr/bin/env python3
"""Measure raw32 Help through a fresh default-clock Session and GUI reducer; diagnostic only."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

BASE = '37f4315c553b9fc3807f2f173d9a0fae3f239ebc'
INPUT_TOOL = '0fd21fd9aa7daeccafb9bd7549aecdf91dfca5a4'
INPUT_RUN, INPUT_JOB, INPUT_ARTIFACT = 39591997211, 119195007741, 12343105539
INPUT_SHA = 'c3841c4aa698e8f809163aad7daf4e1d7db401a9f6d6979feae97f87c279fcc4'
INPUT_BYTES = 386035397
SDK_SHA = '4fdcc4b8d57e0a67eeb2ce7fac70667b75f40f01215105de95b771c77c5303c3'
MIB = 1024 * 1024
CASES = (1, 2, 3, 4)


def require(value, reason):
    if not value:
        raise ValueError(reason)


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(MIB), b''):
            h.update(block)
    return h.hexdigest()


def record(path):
    path = Path(path)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest(path)}


def git(root, *args):
    return subprocess.check_output(['git', '-c', 'safe.directory=' + str(root), '-C', str(root), *args], timeout=35)


def source_identity(root):
    require(git(root, 'rev-parse', 'HEAD').decode().strip() == BASE, 'application source differs from source37')
    require(not git(root, 'status', '--porcelain', '--untracked-files=all'), 'application source has dirty or untracked inputs')
    paths = git(root, 'ls-files', '-z').split(b'\0')
    return {os.fsdecode(path): digest(root / os.fsdecode(path)) for path in paths if path}


def source_identity_after_overlay(root, expected):
    require(git(root, 'rev-parse', 'HEAD').decode().strip() == BASE, 'application HEAD changed')
    return {path: digest(root / path) for path in expected}


def pcm_closure(directory, records, capture, shared):
    # Same accepted recorder integrity checks as copied-help-capture.py, without
    # inventing its GUI phase21 boundary in this fresh public Session.
    files = {name: directory / name for name in ('input.f32', 'feeds.jsonl', 'events.jsonl', 'seal.json')}
    require(directory.is_dir() and set(p.name for p in directory.iterdir()) == set(files), 'PCM inventory differs')
    require(all(p.is_file() and not p.is_symlink() for p in files.values()), 'PCM inventory is not regular')
    seal = shared.unique_json(files['seal.json'].read_text())
    require(seal.get('schema') == 1 and seal.get('complete') is True, 'PCM capture incomplete')
    require(all(seal.get(k) is True for k in ('writers_closed', 'all_attempts_committed', 'data_files_ok',
                                             'float_little_endian', 'float_iec559')), 'PCM writer/format incomplete')
    require(all(seal.get(k) is False for k in ('sample_overflow', 'descriptor_overflow', 'metadata_overflow',
                'identity_overflow', 'config_incomplete', 'input_incomplete')), 'PCM capture lost input/configuration')
    require(seal.get('float_bytes') == 4 and type(seal.get('samples')) is int and
            0 < seal['samples'] <= 1048576 and files['input.f32'].stat().st_size == seal['samples'] * 4,
            'PCM sample count differs')
    for name, field in (('input.f32', 'pcm_sha256'), ('events.jsonl', 'events_sha256'), ('feeds.jsonl', 'feeds_sha256')):
        require(digest(files[name]) == seal.get(field), 'PCM sealed file hash differs: ' + name)
    feeds = capture.jsonl(files['feeds.jsonl'], shared.unique_json)
    events = capture.jsonl(files['events.jsonl'], shared.unique_json)
    require(feeds and len(feeds) == seal.get('attempts') and len(feeds) <= 8192 and
            len(events) == seal.get('records') and files['events.jsonl'].stat().st_size == seal.get('metadata_bytes'),
            'PCM event/descriptor inventory differs')
    offset, ends = 0, {}
    for feed in feeds:
        require(all(type(feed.get(k)) is int and feed[k] >= 0 for k in
                    ('ordinal', 'session', 'bank', 'generation', 'revision', 'first', 'count', 'pcm_offset', 'end', 'final_call', 'status')),
                'PCM descriptor integer invalid')
        identity = tuple(feed[k] for k in ('session', 'bank', 'generation', 'revision'))
        require(feed['status'] in (1, 2) and feed['final_call'] > feed['ordinal'] and feed['pcm_offset'] == offset and
                (feed['status'] != 1 or feed['end'] == feed['first'] + feed['count']), 'PCM partition/commit differs')
        offset += feed['count']
        ends[identity] = feed['end']
    require(offset == seal['samples'], 'PCM descriptors do not cover retained samples')
    banks = seal.get('banks')
    require(isinstance(banks, list) and banks and len(banks) == len(ends), 'PCM bank inventory differs')
    require(all(tuple(b.get(k) for k in ('session', 'bank', 'generation', 'revision')) in ends for b in banks),
            'PCM bank not bound to feed inventory')
    final = [r for r in records if r.get('event') == 'copied_help_pcm_finalization']
    require(len(final) == 1 and final[0].get('complete') is True and final[0].get('seal_close_ok') is True,
            'PCM external finalization failed')
    return {'valid': True, 'seal': seal, 'feeds': len(feeds), 'events': len(events),
            'target_binding': 'fresh Session source context; verify actual tx identity in independent readback'}


def main(args):
    tool, source, sdk = (Path(value).resolve() for value in (args.tool_root, args.source_root, args.sdk))
    evidence, scratch = Path(args.evidence).resolve(), Path(args.scratch).resolve()
    require(not evidence.exists() and not scratch.exists(), 'evidence/scratch must be fresh')
    require(evidence != scratch and evidence not in scratch.parents and scratch not in evidence.parents,
            'scratch and evidence must be disjoint')
    evidence.mkdir(parents=True)
    scratch.mkdir(parents=True)
    spec = importlib.util.spec_from_file_location('retained_process', tool / 'tools/native-copy-smoke-capture.py')
    process = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(process)
    capture_spec = importlib.util.spec_from_file_location('retained_capture', tool / 'tools/copied-help-capture.py')
    capture_helper = importlib.util.module_from_spec(capture_spec)
    capture_spec.loader.exec_module(capture_helper)
    shared = capture_helper.helper('sdk-rev-smoke-capture')
    result = {'purpose': __doc__, 'application_source': BASE, 'input_workflow': INPUT_TOOL,
              'input_run': INPUT_RUN, 'input_artifact': INPUT_ARTIFACT, 'input_sha256': INPUT_SHA,
              'status': 'setup_incomplete', 'cases': [],
              'unexecuted': list(CASES),
              'limits': 'Fresh default-clock Sessions; inherited GUI smoke history absent. No old-input attribution or qualification.'}

    def watch():
        require(process.bounded_tree(evidence) < 300 * MIB, 'diagnostic output exceeds 300 MiB')

    def execute(command, label, timeout, directory=evidence, stdout_limit=2 * MIB):
        data, stdout, stderr = process.run([str(value) for value in command], directory, label, timeout,
                                         watch, stdout_limit=stdout_limit)
        require(process.successful(data), label + ' failed; actual exit/process evidence retained')
        return data, stdout, stderr

    def api(endpoint, label):
        _, stdout, _ = execute(['gh', 'api', endpoint], label, 180)
        return json.loads(stdout.read_text())

    try:
        result['tool_source'] = git(tool, 'rev-parse', 'HEAD').decode().strip()
        before = source_identity(source)
        process.save(evidence / 'application-inputs.json', before)
        result['sdk_manifest'] = record(sdk / 'share/datapump-sdk/manifest.json')
        require(result['sdk_manifest']['sha256'] == SDK_SHA, 'published SDK recipe differs from captured application')
        base = 'repos/' + os.environ['GITHUB_REPOSITORY'] + '/actions/'
        art = api(base + 'artifacts/' + str(INPUT_ARTIFACT), 'input-artifact-api')
        run = api(base + 'runs/' + str(INPUT_RUN), 'input-run-api')
        job = api(base + 'jobs/' + str(INPUT_JOB), 'input-job-api')
        require(art['id'] == INPUT_ARTIFACT and art['size_in_bytes'] == INPUT_BYTES and
                art['digest'] == 'sha256:' + INPUT_SHA and not art['expired'] and
                art['workflow_run']['id'] == INPUT_RUN and art['workflow_run']['head_sha'] == INPUT_TOOL,
                'input artifact identity mismatch')
        require(run['id'] == INPUT_RUN and run['head_sha'] == INPUT_TOOL and run['status'] == 'completed' and
                run['conclusion'] == 'success' and job['run_id'] == INPUT_RUN and job['id'] == INPUT_JOB and
                job['status'] == 'completed' and job['conclusion'] == 'success', 'input producer identity/result mismatch')
        download, archive, _ = execute(['gh', 'api', base + 'artifacts/' + str(INPUT_ARTIFACT) + '/zip'],
                                       'input-download', 180, scratch, 400 * MIB)
        require(archive.stat().st_size == INPUT_BYTES and digest(archive) == INPUT_SHA, 'input ZIP hash/size mismatch')
        process.save(evidence / 'input-download.json', download)
        extracted = scratch / 'capture'
        with zipfile.ZipFile(archive) as package:
            inventory = process.zip_inventory(package)
            process.save(evidence / 'input-zip-inventory.json', inventory)
            package.extractall(extracted)
        capture = extracted / '__w/pumpModem/pumpModem/evidence'
        settings = json.loads((capture / 'metadata/source-settings.json').read_text())
        require(settings['base_commit'] == BASE and settings['tool_commit'] == INPUT_TOOL and
                settings['sdk_manifest']['sha256'] == SDK_SHA, 'captured source/SDK settings mismatch')
        keys = list((capture / 'smokes/run-1/smoke-dir').glob('*.key'))
        require(len(keys) == 1 and digest(keys[0]) == 'aebc6bd5e2d1f024840aed43f9fbb673d66b741dc1fb15f4f9de731f22aa93e8',
                'retained run1 whole-keyfile identity mismatch')
        result['retained_keyfile'] = record(keys[0])
        execute(['python3', tool / 'tools/copied-help-probe/prepare.py', '--base', source,
                 '--record', evidence / 'overlay.json'], 'prepare-overlay', 60)
        overlay = json.loads((evidence / 'overlay.json').read_text())
        expected = dict(before)
        for entry in overlay['files']:
            expected[entry['path']] = entry['post_sha256']
        def verify_inputs():
            require(git(source, 'rev-parse', 'HEAD').decode().strip() == BASE, 'application HEAD changed')
            actual = {path: digest(source / path) for path in expected}
            require(actual == expected, 'application or overlay bytes changed')
            return actual
        process.save(evidence / 'application-overlay-inputs.json', verify_inputs())
        project = scratch / 'project'
        project.mkdir()
        project_file = project / 'session-target.cmake'
        project_file.write_text('if(NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${APPLICATION_ROOT}" OR NOT "${PROJECT_NAME}" STREQUAL "DataPump")\n  return()\nendif()\nget_property(session_scheduled GLOBAL PROPERTY COPIED_HELP_SESSION_SCHEDULED)\nif(session_scheduled)\n  return()\nendif()\nset_property(GLOBAL PROPERTY COPIED_HELP_SESSION_SCHEDULED TRUE)\nfunction(copied_help_add_session_target)\n  add_executable(copied_help_session "${SESSION_ROOT}/driver.cpp" "${SESSION_ROOT}/raw32_context.cpp"\n    "${SESSION_ROOT}/probes.cpp" "${APPLICATION_ROOT}/src/gui/state.cpp")\n  target_link_libraries(copied_help_session PRIVATE datapump datapump_audio_native)\n  target_include_directories(copied_help_session PRIVATE "${SESSION_ROOT}" "${APPLICATION_ROOT}/src")\nendfunction()\ncmake_language(DEFER CALL copied_help_add_session_target)\n')
        shutil.copyfile(project_file, evidence / 'session-target.cmake')
        build = scratch / 'build'
        execute([sdk / 'bin/cmake', '-S', source, '-B', build, '-G', 'Ninja',
                 '-DCMAKE_MAKE_PROGRAM=' + str(sdk / 'bin/ninja'), '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(source / 'cmake/toolchains/source-sdk.cmake'),
                 '-DDATAPUMP_SDK_ROOT=' + str(sdk), '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                 '-DAPPLICATION_ROOT=' + str(source), '-DSESSION_ROOT=' + str(tool / 'tools/copied-help-session'),
                 '-DCMAKE_PROJECT_INCLUDE=' + str(project_file), '-DBUILD_TESTING=OFF',
                 '-DDATAPUMP_BUILD_CLI=ON', '-DDATAPUMP_BUILD_GUI=OFF', '-DDATAPUMP_BUILD_TUI=OFF',
                 '-DDATAPUMP_BUILD_FB=OFF', '-DDATAPUMP_BUILD_WEB_WORKER=OFF', '-DDATAPUMP_PORTABLE=ON'], 'configure', 180)
        execute([sdk / 'bin/cmake', '--build', build, '--parallel', '2', '--target', 'copied_help_session'],
                'build', 1200, stdout_limit=8 * MIB)
        for name in ('CMakeCache.txt', 'compile_commands.json'):
            shutil.copyfile(build / name, evidence / name)
        shutil.copytree(build / 'sdk-runtime', evidence / 'sdk-runtime', symlinks=False)
        binary = build / 'copied_help_session'
        shutil.copyfile(binary, evidence / binary.name)
        result['binary'] = record(binary)
        result['status'] = 'fresh_session_measurement_incomplete'
        for index, case in enumerate(CASES):
            label = 'session-' + str(case)
            output_dir = evidence / label
            output_dir.mkdir()
            output = output_dir / 'session.jsonl'
            command = ['/usr/bin/env', 'PATH=', 'PYTHONHOME=/nonexistent', 'PYTHONPATH=/nonexistent', 'LD_LIBRARY_PATH=',
                       'DATAPUMP_COPIED_HELP_PCM_TRACE=' + str(output_dir / 'pcm'),
                       'DATAPUMP_COPIED_HELP_CANDIDATE_TRACE=' + str(output_dir / 'candidate.jsonl'),
                       'DATAPUMP_COPIED_HELP_SESSION_TRACE=' + str(output_dir / 'verbose.jsonl'),
                       str(binary), '--keyfile', str(keys[0]), '--output', str(output)]
            data, stdout, stderr = process.run(command, evidence, label, 180, watch, stdout_limit=8 * MIB)
            rows = [json.loads(line) for line in output.read_text().splitlines()] if output.is_file() else []
            records = []
            for log in (stdout, stderr):
                records += [json.loads(line) for line in log.read_text().splitlines() if line.startswith('{')]
            finalizers = [r for r in records if r.get('event') == 'session_output_finalization']
            pcm_seal_path = output_dir / 'pcm/seal.json'
            pcm_seal = json.loads(pcm_seal_path.read_text()) if pcm_seal_path.is_file() else None
            candidate_path = output_dir / 'candidate.jsonl'
            candidate_rows = [json.loads(line) for line in candidate_path.read_text().splitlines()] if candidate_path.is_file() else []
            traces = [r for r in records if r.get('event') == 'trace_finalization']
            result['cases'].append({'label': label, 'process': data, 'result': rows[-1] if rows else None,
                                    'external_finalizers': finalizers, 'pcm_seal': pcm_seal,
                                    'candidate_footer': candidate_rows[-1] if candidate_rows else None,
                                    'trace_finalizers': traces})
            result['unexecuted'] = list(CASES[index + 1:])
            require(data.get('process_group_closed') is True, 'Session writers did not close')
            result['cases'][-1]['candidate_closure'] = capture_helper.candidate_closure(
                candidate_path, (stdout, stderr), shared.unique_json)
            result['cases'][-1]['pcm_closure'] = pcm_closure(output_dir / 'pcm', records, capture_helper, shared)
            result['cases'][-1]['trace_closure'] = process.trace_closure(
                output_dir / 'verbose.jsonl', (stdout, stderr), shared.unique_json)
            require(source_identity_after_overlay(source, expected) == expected, 'source inputs changed during Session')
            require(len(finalizers) == 1 and finalizers[0].get('flush_ok') is True and finalizers[0].get('close_ok') is True,
                    'Session output external finalization incomplete')
            require(pcm_seal and pcm_seal.get('complete') is True and pcm_seal.get('writers_closed') is True,
                    'PCM/constructor/context capture incomplete; retain original result')
            require(candidate_rows and candidate_rows[-1].get('type') == 'seal' and
                    candidate_rows[-1].get('complete') is True, 'candidate journal incomplete')
            require(len(traces) == 1 and traces[0].get('result') == 'complete' and
                    traces[0].get('flush_ok') is True and traces[0].get('close_ok') is True,
                    'bounded selected trace incomplete')
            if not process.successful(data) or not rows or rows[-1].get('status') != 'passed':
                result['status'] = 'fresh_session_failure'
                raise ValueError('fresh Session case failed; stop wave at first failure for causal readback')
        verify_inputs()
        result['status'] = 'four_fresh_sessions_passed_old_C_unresolved'
    except Exception as error:
        result['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        reports = list(evidence.glob('*.process.json')) + list(scratch.glob('*.process.json'))
        closed = all(json.loads(path.read_text()).get('process_group_closed') is True for path in reports)
        build = scratch / 'build'
        if closed and build.is_dir():
            for name in ('CMakeCache.txt', 'compile_commands.json', 'CMakeFiles/CMakeConfigureLog.yaml'):
                path = build / name
                if path.is_file():
                    target = evidence / name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, target)
        result['writers_closed'] = closed
        result['retention_finalization'] = 'closed' if closed else 'blocked_live_writers'
        process.save(evidence / 'run.json', result)
        watch()
        if closed:
            process.save(evidence / 'manifest.json', {'files': [record(path) for path in sorted(evidence.rglob('*'))
                         if path.is_file() and path != evidence / 'manifest.json'],
                         'writers_closed': True, 'qualification': False})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for option in ('tool-root', 'source-root', 'sdk', 'evidence', 'scratch'):
        parser.add_argument('--' + option, required=True)
    main(parser.parse_args())

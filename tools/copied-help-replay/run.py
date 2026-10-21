#!/usr/bin/env python3
"""Replay four sealed receiver prefixes; this is diagnostic, never qualification."""
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
CASES = ((1, 1), (1, 4), (2, 13), (2, 14))
PARTITIONS = ('original', 'unsplit', 'ragged', 'bins512')


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
    result = {'purpose': __doc__, 'application_source': BASE, 'input_workflow': INPUT_TOOL,
              'input_run': INPUT_RUN, 'input_artifact': INPUT_ARTIFACT, 'input_sha256': INPUT_SHA,
              'status': 'setup_incomplete', 'cases': [],
              'unexecuted': [('run' + str(a) + '_receiver' + str(r), mode) for a, r in CASES for mode in PARTITIONS],
              'limits': 'Fixed captured prefixes only; no EOF flush, new input, original-failure attribution or qualification.'}

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
        generated = evidence / 'generated'
        generated.mkdir()
        labels = []
        capture_labels = {}
        for attempt, receiver in CASES:
            label = 'run' + str(attempt) + '_receiver' + str(receiver)
            labels.append(label)
            capture_labels[label] = 'run-' + str(attempt) + '-receiver-' + str(receiver)
            execute(['python3', tool / 'tools/copied-help-replay/generate.py', '--capture-root',
                     capture / ('smokes/run-' + str(attempt)), '--receiver-id', receiver,
                     '--namespace', 'case_' + label, '--output', generated / label], 'generate-' + label, 60)
        project = scratch / 'project'
        project.mkdir()
        # Preserve the application's own root/project assumptions. The hook is
        # called at the end of every project(), so arm only source37 DataPump.
        cmake = ['if(NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${APPLICATION_ROOT}" OR NOT "${PROJECT_NAME}" STREQUAL "DataPump")',
                 '  return()', 'endif()',
                 'get_property(replay_scheduled GLOBAL PROPERTY COPIED_HELP_REPLAY_SCHEDULED)',
                 'if(replay_scheduled)', '  return()', 'endif()',
                 'set_property(GLOBAL PROPERTY COPIED_HELP_REPLAY_SCHEDULED TRUE)',
                 'function(copied_help_add_replay_targets)']
        for label in labels:
            target = 'replay_' + label
            cmake += ['add_executable(' + target + ' "${REPLAY_ROOT}/adapter.cpp")',
                      'target_link_libraries(' + target + ' PRIVATE datapump)',
                      'target_include_directories(' + target + ' PRIVATE "${REPLAY_ROOT}" "${GENERATED_ROOT}")',
                      'target_compile_definitions(' + target + ' PRIVATE COPIED_HELP_FIXTURE_HEADER="' +
                      label + '/fixture.hpp" COPIED_HELP_FIXTURE_NAMESPACE=datapump::diagnostic::copied_help_replay::case_' + label + ')']
        cmake += ['endfunction()', 'cmake_language(DEFER CALL copied_help_add_replay_targets)']
        project_file = project / 'replay-targets.cmake'
        project_file.write_text('\n'.join(cmake) + '\n')
        shutil.copyfile(project_file, evidence / 'replay-targets.cmake')
        build = scratch / 'build'
        execute([sdk / 'bin/cmake', '-S', source, '-B', build, '-G', 'Ninja',
                 '-DCMAKE_MAKE_PROGRAM=' + str(sdk / 'bin/ninja'), '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(source / 'cmake/toolchains/source-sdk.cmake'),
                 '-DDATAPUMP_SDK_ROOT=' + str(sdk), '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                 '-DAPPLICATION_ROOT=' + str(source), '-DREPLAY_ROOT=' + str(tool / 'tools/copied-help-replay'),
                 '-DGENERATED_ROOT=' + str(generated), '-DCMAKE_PROJECT_INCLUDE=' + str(project_file),
                 '-DBUILD_TESTING=OFF', '-DDATAPUMP_BUILD_CLI=ON', '-DDATAPUMP_BUILD_GUI=OFF',
                 '-DDATAPUMP_BUILD_TUI=OFF', '-DDATAPUMP_BUILD_FB=OFF', '-DDATAPUMP_BUILD_WEB_WORKER=OFF',
                 '-DDATAPUMP_PORTABLE=ON'], 'configure', 180)
        execute([sdk / 'bin/cmake', '--build', build, '--parallel', '2', '--target',
                 *['replay_' + label for label in labels]], 'build', 1200, stdout_limit=8 * MIB)
        for name in ('CMakeCache.txt', 'compile_commands.json'):
            shutil.copyfile(build / name, evidence / name)
        runtime = build / 'sdk-runtime'
        if runtime.is_dir():
            shutil.copytree(runtime, evidence / 'sdk-runtime', symlinks=False)
        result['status'] = 'baseline_incomplete'
        for label in labels:
            shutil.copyfile(build / ('replay_' + label), evidence / ('replay_' + label))
        failures = []
        pending = [(label, mode) for label in labels for mode in PARTITIONS]
        projection_hashes = {}
        for index, (label, mode) in enumerate(pending):
            binary = build / ('replay_' + label)
            case_label = label + '-' + mode
            output = evidence / (case_label + '.jsonl')
            data, stdout, stderr = process.run([str(binary), '--fixture-base', str(capture), '--output', str(output),
                                               '--partition', mode], evidence, case_label, 180, watch, stdout_limit=8 * MIB)
            rows = [json.loads(line) for line in output.read_text().splitlines()] if output.is_file() else []
            finalizers = [json.loads(line) for line in stdout.read_text().splitlines()
                          if line.startswith('{') and json.loads(line).get('event') == 'replay_output_finalization']
            projected = [row for row in rows if row.get('event') == 'projection_ready']
            result['cases'].append({'label': label, 'partition': mode, 'process': data, 'binary': record(binary),
                                    'output': record(output) if output.is_file() else None,
                                    'result': rows[-1] if rows else None, 'external_finalizers': finalizers,
                                    'projection': projected})
            result['unexecuted'] = pending[index + 1:]
            # Process/retention failures stop the wave. A closed native mismatch
            # retains its evidence while other independent fixed cases proceed.
            require(data.get('process_group_closed') is True, 'native case has live writers')
            require(output.is_file() and output.stat().st_size < 8 * MIB, 'native case output missing/oversized')
            require(len(finalizers) == 1 and finalizers[0].get('flush_ok') is True and
                    finalizers[0].get('close_ok') is True, 'native output external finalization incomplete')
            require(rows and rows[-1].get('event') == 'case_result', 'native case final result missing')
            begins = [row for row in rows if row.get('event') == 'case_begin']
            require(len(begins) == 1 and begins[0].get('label') == capture_labels[label] and
                    rows[-1].get('partition') == mode, 'native case/mode identity mismatch')
            if mode != 'original':
                require(len(projected) == 1 and projected[0].get('all_mixer_witnesses_exact') is True,
                        'fixed projected waveform proof missing/invalid; not partition evidence')
                actual_hash = projected[0]['projected_native_complex_sha256']
                if mode == 'unsplit':
                    projection_hashes[label] = actual_hash
                else:
                    require(actual_hash == projection_hashes[label], 'projected waveform changed across partitions')
            if not process.successful(data) or rows[-1].get('matched') is not True:
                first_difference = next((row for row in rows if row.get('event') == 'first_difference'), None)
                failures.append({'label': label, 'partition': mode, 'first_difference': first_difference,
                                 'result': rows[-1]})
                # A failed original or precomputed control invalidates comparisons
                # for this case; do not interpret its variants as partition effects.
                if mode in ('original', 'unsplit'):
                    result['status'] = 'matched_control_failed'
                    raise ValueError('matched control failed; native evidence retained')
                if not (data.get('exit_code') == 1 and data.get('capture_error') is None and
                        rows[-1].get('status') == 'mismatch' and rows[-1].get('matched') is False and first_difference):
                    result['status'] = 'native_case_error'
                    raise ValueError('native setup/receiver/capture error; not a partition comparison result')
                continue
        result['failures'] = failures
        result['projected_waveform_sha256'] = projection_hashes
        require(source_identity(source) == before, 'application inputs changed during replay')
        result['status'] = 'partition_difference' if failures else 'four_captured_prefixes_all_partitions_matched'
        require(not failures, 'partition differences retained; diagnostic failure is not qualification')
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

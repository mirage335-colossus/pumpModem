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
              'status': 'setup_incomplete', 'cases': [], 'unexecuted': list(CASES),
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
        for attempt, receiver in CASES:
            label = 'run' + str(attempt) + '_receiver' + str(receiver)
            labels.append(label)
            execute(['python3', tool / 'tools/copied-help-replay/generate.py', '--capture-root',
                     capture / ('smokes/run-' + str(attempt)), '--receiver-id', receiver,
                     '--namespace', 'case_' + label, '--output', generated / label], 'generate-' + label, 60)
        project = scratch / 'project'
        project.mkdir()
        cmake = ['cmake_minimum_required(VERSION 3.21)', 'project(CopiedHelpReplay LANGUAGES C CXX)',
                 'set(CMAKE_CXX_STANDARD 20)', 'set(CMAKE_CXX_STANDARD_REQUIRED ON)',
                 'set(CMAKE_CXX_EXTENSIONS OFF)', 'set(BUILD_TESTING OFF CACHE BOOL "" FORCE)',
                 'set(DATAPUMP_BUILD_CLI OFF CACHE BOOL "" FORCE)', 'set(DATAPUMP_BUILD_GUI OFF CACHE BOOL "" FORCE)',
                 'set(DATAPUMP_BUILD_TUI OFF CACHE BOOL "" FORCE)', 'set(DATAPUMP_BUILD_FB OFF CACHE BOOL "" FORCE)',
                 'set(DATAPUMP_BUILD_WEB_WORKER OFF CACHE BOOL "" FORCE)',
                 'set(DATAPUMP_PORTABLE ON CACHE BOOL "" FORCE)',
                 'include("${APPLICATION_ROOT}/cmake/SdkBuild.cmake")',
                 'add_subdirectory("${APPLICATION_ROOT}" application EXCLUDE_FROM_ALL)']
        for label in labels:
            target = 'replay_' + label
            cmake += ['add_executable(' + target + ' "${REPLAY_ROOT}/adapter.cpp")',
                      'target_link_libraries(' + target + ' PRIVATE datapump)',
                      'target_include_directories(' + target + ' PRIVATE "${REPLAY_ROOT}" "${GENERATED_ROOT}")',
                      'target_compile_definitions(' + target + ' PRIVATE COPIED_HELP_FIXTURE_HEADER="' +
                      label + '/fixture.hpp" COPIED_HELP_FIXTURE_NAMESPACE=datapump::diagnostic::copied_help_replay::case_' + label + ')']
        project_file = project / 'CMakeLists.txt'
        project_file.write_text('\n'.join(cmake) + '\n')
        shutil.copyfile(project_file, evidence / 'replay-CMakeLists.txt')
        build = scratch / 'build'
        execute([sdk / 'bin/cmake', '-S', project, '-B', build, '-G', 'Ninja',
                 '-DCMAKE_MAKE_PROGRAM=' + str(sdk / 'bin/ninja'), '-DCMAKE_BUILD_TYPE=Release',
                 '-DCMAKE_TOOLCHAIN_FILE=' + str(source / 'cmake/toolchains/source-sdk.cmake'),
                 '-DDATAPUMP_SDK_ROOT=' + str(sdk), '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                 '-DAPPLICATION_ROOT=' + str(source), '-DREPLAY_ROOT=' + str(tool / 'tools/copied-help-replay'),
                 '-DGENERATED_ROOT=' + str(generated)], 'configure', 180)
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
        for index, label in enumerate(labels):
            binary = build / ('replay_' + label)
            output = evidence / (label + '.jsonl')
            data, stdout, stderr = process.run([str(binary), '--fixture-base', str(capture), '--output', str(output)],
                                                evidence, label, 180, watch, stdout_limit=8 * MIB)
            rows = [json.loads(line) for line in output.read_text().splitlines()] if output.is_file() else []
            finalizers = [json.loads(line) for line in stdout.read_text().splitlines()
                          if line.startswith('{') and json.loads(line).get('event') == 'replay_output_finalization']
            result['cases'].append({'label': label, 'process': data, 'binary': record(binary),
                                    'output': record(output) if output.is_file() else None,
                                    'result': rows[-1] if rows else None, 'external_finalizers': finalizers})
            result['unexecuted'] = list(CASES[index + 1:])
            if not process.successful(data):
                result['status'] = 'native_case_mismatch' if any(row.get('event') == 'first_difference' for row in rows) else 'native_case_error'
            require(process.successful(data), 'native case failed; original output and exit retained')
            require(output.is_file() and output.stat().st_size < 8 * MIB, 'native case output missing/oversized')
            require(len(finalizers) == 1 and finalizers[0].get('flush_ok') is True and
                    finalizers[0].get('close_ok') is True, 'native output external finalization incomplete')
            require(rows and rows[-1].get('event') == 'case_result' and rows[-1].get('matched') is True,
                    'native case did not establish complete output equality')
        require(source_identity(source) == before, 'application inputs changed during replay')
        result['status'] = 'four_captured_prefixes_matched'
    except Exception as error:
        result['error'] = type(error).__name__ + ': ' + str(error)
        raise
    finally:
        reports = list(evidence.glob('*.process.json')) + list(scratch.glob('*.process.json'))
        closed = all(json.loads(path.read_text()).get('process_group_closed') is True for path in reports)
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

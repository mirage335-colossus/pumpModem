#!/usr/bin/env python3
"""Same-binary A4 fidelity replay with constant census and paired FFT scopes."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import runpy
import sys

HERE = Path(__file__).resolve().parent
MODES = ('0', '1', '0', '1', '0', '1')
STAGES = ('fft_execution_job_prepare', 'fft_execution_convolution', 'fft_execution_score')


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def capture(root, report, timeout_seconds):
    root, report = root.resolve(strict=True), report.resolve(strict=True)
    if timeout_seconds != 300 or (report / 'outputs').exists() or (report / 'capture-summary.json').exists():
        raise ValueError('fresh outputs and original 300-second diagnostic watchdog required')
    prep = load('execution_preparation', HERE / 'prepare.py')
    manifest, census = prep.checked_inputs()
    base_path = HERE.parent / 'acquisition/capture.py'
    if prep.sha(base_path) != census['a4_reader_sha256']:
        raise ValueError('original A4 capture/semantic gate differs')
    base = load('unchanged_a4_capture', base_path)
    reader = load('unchanged_storage_reader', HERE / 'census/reader.py')
    integrated, source, pcm = base.validate_integrated_source(root, report)
    config, cache, binary, versions = base.validate_build(root, source)
    if integrated['execution_components'] != json.loads((HERE / 'pins.json').read_text()):
        raise ValueError('prepared component pins differ from executing helper')
    composed_path = Path(integrated['composed_census_manifest_path'])
    if prep.sha(composed_path) != integrated['composed_census_manifest_sha256']:
        raise ValueError('composed census source gate changed')
    composed = json.loads(composed_path.read_text())
    expected_composed = dict(census)
    expected_composed['read_pins'] = {**census['read_pins'], **manifest['fft_postimages']}
    expected_composed['composition'] = {'parent_manifest_sha256': prep.sha(HERE / 'census/manifest.json'),
                                      'fft_patch_sha256': manifest['fft_patch_sha256'],
                                      'additional_postimages': manifest['fft_postimages']}
    if composed != expected_composed:
        raise ValueError('unreviewed change to composed census gate')
    validator = runpy.run_path(str(root / 'tools/gui-stop-differential.py'))['trace_seal']
    original_environment = base.safe_environment
    # Retain the accepted acquisition scopes in BOTH modes. Only the new FFT
    # stages alternate; labels OFF/ON refer to that explicit new toggle.
    base.SCOPES = ('1',) * 6
    records, failure = [], None
    binary_hash = prep.sha(binary)
    try:
        for index, mode in enumerate(MODES):
            def environment():
                env = original_environment()
                env['DATAPUMP_FFT_EXECUTION_INNER_CPU'] = mode
                return env
            base.safe_environment = environment
            record = base.run_one(root=root, report=report, binary=binary, pcm_path=pcm,
                                  pcm_sha256=integrated['fixture']['pcm_sha256'], index=index,
                                  timeout_seconds=timeout_seconds, trace_validator=validator)
            record['fft_execution_scope'] = mode
            records.append(record)
            if record['exit_code'] or record['timed_out'] or record['trace_seal'] is None or not record['input_unchanged_after_run']:
                raise ValueError('process/input/strict trace seal failed')
            readback = base.semantic_readback(Path(record['trace_path']), Path(integrated['fixture']['oracle_path']))
            if not readback['passed'] or readback['output_sha256'] != '46e0bf93c4e86d8fa1f8a7600642418f2dfca6559e1f6aa2d7fef1c0d86eec1d':
                raise ValueError('unchanged A4 timeline/work/output/physical completion fidelity failed: ' + str(readback))
            profile = readback['profile']
            for stage in STAGES:
                ns, calls = profile['cpu_' + stage + '_ns'], profile['cpu_' + stage + '_calls']
                if (mode == '0' and (ns != 0 or calls != 0)) or (mode == '1' and (ns <= 0 or calls <= 0)):
                    raise ValueError('new FFT scope switch mismatch: ' + stage)
            if mode == '1' and profile['cpu_fft_execution_score_calls'] != 2 * 2871980:
                raise ValueError('new score scope did not cover every expected job/bit')
            record['semantic_readback'] = readback
            record['storage_readback'] = reader.read(Path(record['trace_path']), Path(record['stderr_path']),
                                                     report / 'integrated-source.json', base_path,
                                                     Path(integrated['fixture']['oracle_path']), manifest=composed)
            if prep.sha(binary) != binary_hash:
                raise ValueError('same-binary comparison changed executable')
            for name, digest in integrated['compiled_overlay_sha256'].items():
                if prep.sha(source / name) != digest:
                    raise ValueError('compiled source changed during replay: ' + name)
            base.write_json(Path(record['stdout_path']).parent / 'run.json', record)
    except Exception as error:
        failure = type(error).__name__ + ': ' + str(error)
        if records:
            records[-1]['failure'] = failure
            base.write_json(Path(records[-1]['stdout_path']).parent / 'run.json', records[-1])
    summary = dict(scope='simulation-acquisition-partition-diagnostic', classification=base.CLASSIFICATION,
                   interpretation='six fixed-input diagnostic replays; old acquisition scopes and terminal census constant ON; three matched new FFT-scope OFF/ON pairs; not qualification',
                   launcher_source_commit=integrated['launcher_source_commit'], application_source_commit=integrated['application_source_commit'],
                   compiled_overlay_sha256=integrated['compiled_overlay_sha256'], fixture=integrated['fixture'],
                   integrated_source_sha256=prep.sha(report / 'integrated-source.json'), configuration=config,
                   cache_sha256=prep.sha(cache), compiler_versions=versions, binary_sha256=binary_hash,
                   runner_helper_sha256=prep.sha(Path(__file__)), requested_runs=6, completed_runs=len(records),
                   records=records, failure=failure,
                   status='captured_diagnostic_prefix_replay' if failure is None and len(records) == 6 else 'incomplete_or_failed_diagnostic')
    base.write_json(report / 'capture-summary.json', summary)
    print(json.dumps({'status': summary['status'], 'completed_runs': len(records), 'failure': failure}), flush=True)
    if failure is not None or len(records) != 6:
        raise SystemExit(1)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--timeout-seconds', type=int, default=300)
    args = parser.parse_args()
    capture(args.root, args.report, args.timeout_seconds)

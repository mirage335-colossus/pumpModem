#!/usr/bin/env python3
"""Test published portable assets without replacing them; attach per-run evidence."""
import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import tarfile
import tempfile
from urllib.parse import quote
import zipfile

SPEC = importlib.util.spec_from_file_location('datapump_release', Path(__file__).with_name('release.py'))
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
WINDOWS_SPEC = importlib.util.spec_from_file_location('datapump_windows_certification',
    Path(__file__).with_name('windows-certification.py'))
windows_certification = importlib.util.module_from_spec(WINDOWS_SPEC)
WINDOWS_SPEC.loader.exec_module(windows_certification)
SMOKE_SPEC = importlib.util.spec_from_file_location('datapump_gui_smoke',
    Path(__file__).with_name('run-gui-smoke.py'))
gui_smoke = importlib.util.module_from_spec(SMOKE_SPEC)
SMOKE_SPEC.loader.exec_module(gui_smoke)
gh = release.gh
REQUIRED_JOBS = {'linux-tests', 'windows-tests', 'compatibility'}
DISPLAY_WARNING_POLICY = ('Display cadence warnings do not fail certification; '
                          'content/physical/pending checks remain mandatory.')
HOSTED_WARNING_POLICY = ('The recognized hosted graphics exclusion does not block certification or '
                         'Latest eligibility. The listed graphics checks remain untested; '
                         'all other required checks remain mandatory.')
SMOKE_WARNING_POLICY = ('A typed workload cap with validated recent progress is incomplete GUI smoke '
                        'coverage, not a smoke pass. It does not block hosted certification or '
                        'Latest eligibility; all other required checks remain mandatory.')
SCOPE = ('Hosted source contract, GUI and packaging tests, plus checks of the exact '
         'published archives. Linux containers share the runner kernel; Windows '
         'uses the selected Windows x64 hosted runner, with its image recorded in '
         'the workflow logs. Physical audio devices, Raspberry '
         'Pi and Chromebook hardware, and Windows 10/11 client installations are '
         'not qualified by this report.')


def validate_warnings(value, metadata):
    if not isinstance(value, list) or len(value) > 1:
        raise ValueError('Certification warnings must be a bounded list of known exceptions')
    if value:
        if ('windows-x86_64-rev' not in release.application_targets(metadata)
                or value != [windows_certification.warning_record()]):
            raise ValueError('Unknown or inconsistent certification warning/coverage exclusion')
    return value


def validate_smoke_warnings(value, metadata, inventory_sha, run_id, run_attempt):
    if not isinstance(value, list) or len(value) > 64:
        raise ValueError('Smoke warnings must be a bounded list of at most 64 records')
    fields = {'code', 'target', 'scope', 'diagnostic', 'source_sha',
              'inventory_sha256', 'run_id', 'run_attempt'}
    identities = {'source_sha': metadata['source_sha'], 'inventory_sha256': inventory_sha,
                  'run_id': str(run_id)}
    targets = release.application_targets(metadata)
    seen = set()
    for item in value:
        if (not isinstance(item, dict) or set(item) != fields
                or any(not isinstance(field, str) for field in item.values())):
            raise ValueError('Smoke warning must contain exactly the expected string fields')
        if (item['code'] != 'gui-smoke-workload-incomplete' or item['target'] not in targets
                or any(item[key] != expected for key, expected in identities.items())):
            raise ValueError('Smoke warning identity differs from this release, inventory or run')
        # A partial rerun may reuse a successful job from an earlier attempt of
        # this same run. Keep its actual originating attempt in immutable evidence.
        if (not re.fullmatch(r'[1-9][0-9]*', item['run_attempt'])
                or int(item['run_attempt']) > int(run_attempt)):
            raise ValueError('Smoke warning attempt must be positive and no later than the recording attempt')
        scope = item['scope']
        if (len(scope) > 128 or not (scope == 'source/native'
                or re.fullmatch(r'published/[A-Za-z0-9][A-Za-z0-9._:/@+-]*', scope))):
            raise ValueError('Smoke warning scope must be source/native or a bounded published image')
        if not gui_smoke.valid_budget_diagnostic(item['diagnostic']):
            raise ValueError('Smoke warning lacks the exact typed recent-progress budget diagnostic')
        identity = (item['target'], scope, item['diagnostic'])
        if identity in seen:
            raise ValueError('Duplicate smoke warning for the same target, scope and diagnostic')
        seen.add(identity)
    return value


def collect_smoke_warnings(directory, run_id, run_attempt):
    """Keep the newest artifact per job, preserving any earlier successful job's origin."""
    run_id, run_attempt = str(run_id), str(run_attempt)
    if not all(re.fullmatch(r'[1-9][0-9]*', value) for value in (run_id, run_attempt)):
        raise ValueError('Smoke collection run ID and attempt must be positive integers')
    directory = Path(directory)
    newest = {}
    for artifact in sorted(directory.iterdir()) if directory.exists() else []:
        match = re.fullmatch(r'(certification-smoke-[A-Za-z0-9][A-Za-z0-9_.-]*)-attempt-([1-9][0-9]*)',
                             artifact.name)
        if not artifact.is_dir() or not match or int(match[2]) > int(run_attempt):
            raise ValueError(f'Malformed or future smoke artifact: {artifact.name}')
        job, attempt = match.groups()
        if job not in newest or int(attempt) > int(newest[job][0]):
            newest[job] = (attempt, artifact)
    warnings = []
    for attempt, artifact in newest.values():
        identity = {'run_id': run_id, 'run_attempt': attempt}
        manifest_path = artifact / 'job-coverage.json'
        if not manifest_path.is_file():
            raise ValueError(f'Smoke artifact has no job coverage manifest: {artifact.name}')
        if json.loads(manifest_path.read_text(encoding='utf-8')) != identity:
            raise ValueError(f'Smoke artifact manifest identity differs: {artifact.name}')
        for path in sorted(artifact.rglob('*.json')):
            if path == manifest_path:
                continue
            report = json.loads(path.read_text(encoding='utf-8'))
            if not isinstance(report, dict):
                raise ValueError(f'Malformed smoke report: {path.name}')
            if (path.name == 'native-tests.json' and not {'run_id', 'run_attempt'} & report.keys()
                    and report.get('status') in ('passed', 'failed', 'passed_with_warnings')):
                continue
            if any(report.get(key) != value for key, value in identity.items()):
                raise ValueError(f'Smoke report identity differs from its artifact: {path.name}')
            if report.get('status') == 'incomplete':
                warning = report.get('warning')
                if (not isinstance(warning, dict)
                        or any(warning.get(key) != value for key, value in identity.items())
                        or warning.get('source_sha') != report.get('source_sha')):
                    raise ValueError(f'Smoke warning identity differs from its report: {path.name}')
                warnings.append(warning)
                if len(warnings) > 64:
                    raise ValueError('Smoke collection contains more than 64 warning records')
            elif report.get('status') not in ('passed', 'failed') or 'warning' in report:
                raise ValueError(f'Malformed smoke report outcome: {path.name}')
    return warnings


def required_jobs(metadata):
    return (REQUIRED_JOBS | ({'apt-repository'} if metadata['schema'] >= 3 else set())
            | ({'distro-recipes'} if metadata['schema'] >= 4 else set()))


def required_coverage(metadata):
    sdk = metadata['linux_baseline'] == 'bookworm-sdk'
    linux = ['Debian 12', 'Debian 13', 'Ubuntu 24.04', 'Ubuntu 26.04']
    checks = ['SHA-256', 'package manifest', 'CLI commands', 'GUI self-check and smoke',
              'relocation to a path with spaces', 'runtime dependency closure']
    if metadata.get('frontends'):
        checks += ['required TUI and framebuffer binaries/manuals', 'TUI and framebuffer self-checks']
    platforms = {
        'source_tests': {
            'linux-x86_64': {'environment': 'Debian 12 (source SDK)' if sdk else 'Ubuntu 22.04',
                            'groups': ['build', 'contract', 'gui', 'packaging']},
            'linux-aarch64': {'environment': 'Ubuntu 22.04',
                             'groups': ['build', 'contract', 'gui', 'packaging']},
            'windows-x86_64': {'environment': 'Selected Windows x64 hosted runner',
                              'groups': ['contract', 'gui', 'packaging']},
        },
        'published_archives': {
            'linux-x86_64': {'environments': linux + ([] if sdk else ['Ubuntu 22.04']) + ['Arch Linux'],
                            'checks': checks + ['ELF ABI ceiling'], 'glibc_max': '2.36' if sdk else '2.35'},
            'linux-aarch64': {'environments': linux + ['Ubuntu 22.04'],
                             'checks': checks + ['ELF ABI ceiling'], 'glibc_max': '2.35'},
            'windows-x86_64': {'environments': ['Selected Windows x64 hosted runner'], 'checks': checks},
        },
    }
    coverage = {section: {} for section in platforms}
    if metadata.get('frontends'):
        for source in platforms['source_tests'].values():
            source['groups'] = source['groups'] + ['frontends (independent job)']
    for target in release.application_targets(metadata):
        platform = release.target_platform(metadata, target)
        backend = release.target_backend(metadata, target)
        for section, items in platforms.items():
            coverage[section][target] = dict(items[platform])
            if metadata['schema'] >= 2:
                coverage[section][target].update(platform=platform, gui_backend=backend)
    return coverage


def validate_location(repository, tag):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*/[A-Za-z0-9][A-Za-z0-9_.-]*', repository):
        raise ValueError('Repository must be OWNER/REPO')
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,100}', tag) or '..' in tag:
        raise ValueError('Invalid portable release tag')


def api(endpoint):
    return json.loads(gh(['api', endpoint]).stdout)


def checksums(path):
    inventory = {}
    for line in path.read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9_.+-]*)', line)
        if not match or match[2] == 'SHA256SUMS.txt' or match[2] in inventory:
            raise ValueError('Malformed or duplicate published checksum entry')
        inventory[match[2]] = match[1]
    if not inventory:
        raise ValueError('Published checksum inventory is empty')
    return inventory


def tag_commit(repository, tag):
    value = api(f'repos/{repository}/git/ref/tags/{quote(tag, safe="")}')['object']
    for _ in range(8):
        if value.get('type') == 'commit' and release.SHA.fullmatch(value.get('sha', '')):
            return value['sha']
        if value.get('type') != 'tag' or not release.SHA.fullmatch(value.get('sha', '')):
            break
        value = api(f'repos/{repository}/git/tags/{value["sha"]}')['object']
    raise ValueError('Release tag does not resolve to a commit')


def download_asset(repository, tag, name, directory, assets, expected=None):
    if name not in assets:
        raise ValueError(f'Missing published asset: {name}')
    path = directory / name
    if path.exists() or path.is_symlink():
        raise ValueError(f'Refusing to replace downloaded file: {path}')
    # Download the exact REST asset ID: the release's embedded asset list (and
    # therefore `gh release download`) can be empty despite available assets.
    release.download_asset(repository, assets[name], path, require_digest=False)
    if not path.is_file() or path.is_symlink():
        raise ValueError(f'Download is not a regular file: {name}')
    actual = release.digest(path)
    server_digest = assets[name].get('digest')
    if server_digest and server_digest != 'sha256:' + actual:
        raise ValueError(f'GitHub asset digest mismatch: {name}')
    if expected and actual != expected:
        raise ValueError(f'Published checksum mismatch: {name}')
    return path


def prepare(repository, tag, directory, expected_inventory=None):
    validate_location(repository, tag)
    if directory.is_symlink():
        raise ValueError('Download directory must not be a symlink')
    directory = directory.resolve()
    if directory.exists() and (directory.is_symlink() or not directory.is_dir() or any(directory.iterdir())):
        raise ValueError('Download directory must be new or empty')
    directory.mkdir(parents=True, exist_ok=True)
    published = api(f'repos/{repository}/releases/tags/{quote(tag, safe="")}')
    if published.get('draft') or published.get('tag_name') != tag:
        raise ValueError('Certification requires the exact published, non-draft release')
    if type(published.get('id')) is not int or published['id'] <= 0:
        raise ValueError('Published release has no valid REST release ID')
    # Per-run certificates accumulate here; enumerate every page rather than
    # trusting a partial or empty assets field in the release response.
    items = release.api_pages(f'repos/{repository}/releases/{published["id"]}/assets?per_page=100')
    assets = {item['name']: item for item in items}
    if len(assets) != len(items):
        raise ValueError('Release has duplicate asset names')
    sums = download_asset(repository, tag, 'SHA256SUMS.txt', directory, assets)
    inventory_sha = release.digest(sums)
    if expected_inventory is not None and inventory_sha != expected_inventory:
        raise ValueError('Published inventory changed after certification preparation')
    inventory = checksums(sums)
    metadata_path = download_asset(repository, tag, 'release-metadata.json', directory, assets,
                                   inventory.get('release-metadata.json'))
    metadata = release.load_metadata(metadata_path)
    required = release.required_assets(metadata)
    if not required <= inventory.keys() or not set(inventory) <= assets.keys():
        raise ValueError('Published inventory is missing application or support assets')
    extra = set(inventory) - required
    if extra:
        if metadata['schema'] >= 6:
            raise ValueError('Published inventory contains assets outside its recorded dependency recipes')
        release.verify_sdk_pair(extra)
    for name, expected in inventory.items():
        server_digest = assets[name].get('digest')
        if server_digest and server_digest != 'sha256:' + expected:
            raise ValueError(f'Published checksum differs from GitHub asset digest: {name}')
    if metadata['tag'] != tag or tag_commit(repository, tag) != release.tag_revision(metadata):
        raise ValueError('Release source commit or tag differs from its metadata')
    if metadata['experiment'] and (published.get('name') != 'experiment' or not published.get('prerelease')):
        raise ValueError('Experimental release lost its exact title or prerelease designation')
    return {'metadata': metadata, 'inventory': inventory, 'inventory_sha256': inventory_sha,
            'published': published, 'assets': assets, 'directory': directory}


def extract_archive(archive, directory, metadata, target):
    if target not in release.application_targets(metadata):
        raise ValueError('Unknown application target for this release')
    roots = set(release.package_bases(metadata, target))
    # Schemas 2 and 3 identify the backend inside the checksummed archive, not
    # just in its filename. Legacy releases retain their original root format.
    release.verify_archive_backend(archive, metadata, target)
    seen, selected = set(), set()
    destination = directory / 'offline destination with spaces'
    if destination.exists():
        raise ValueError('Refusing to replace an extracted archive')

    def path_for(name, regular, folder):
        path = PurePosixPath(name)
        if (not name or '\\' in name or ':' in name or path.is_absolute() or '..' in path.parts
                or not path.parts or path.parts[0] not in roots or not (regular or folder)
                or path.as_posix() in seen):
            raise ValueError(f'Unsafe or duplicate archive member: {name}')
        seen.add(path.as_posix())
        selected.add(path.parts[0])
        return destination.joinpath(*path.parts)

    if archive.name.endswith('.tar.gz'):
        with tarfile.open(archive) as source:
            members = [(member, path_for(member.name, member.isfile(), member.isdir()))
                       for member in source.getmembers()]
            if len(selected) != 1:
                raise ValueError('Archive must contain exactly one expected package root')
            for member, path in members:
                if member.isdir():
                    path.mkdir(parents=True, exist_ok=True)
                else:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with source.extractfile(member) as data, path.open('xb') as output:
                        shutil.copyfileobj(data, output)
                    path.chmod(member.mode & 0o777)
    else:
        with zipfile.ZipFile(archive) as source:
            members = []
            for member in source.infolist():
                mode = member.external_attr >> 16
                regular = stat.S_IFMT(mode) in (0, stat.S_IFREG) and not member.is_dir()
                members.append((member, path_for(member.filename, regular, member.is_dir())))
            if len(selected) != 1:
                raise ValueError('Archive must contain exactly one expected package root')
            for member, path in members:
                if member.is_dir():
                    path.mkdir(parents=True, exist_ok=True)
                else:
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with source.open(member) as data, path.open('xb') as output:
                        shutil.copyfileobj(data, output)
    root = destination / next(iter(selected))
    extension = '.exe' if release.target_platform(metadata, target).startswith('windows-') else ''
    if not all((root / name).is_file() for name in ('manifest.sha256', 'bin/pump' + extension,
                                                   'bin/datapump-gui' + extension)):
        raise ValueError('Archive is missing its package manifest or application executables')
    return root


def download(repository, tag, target, directory, inventory_sha):
    if not re.fullmatch(r'[0-9a-f]{64}', inventory_sha):
        raise ValueError('A complete prepared inventory SHA-256 is required')
    state = prepare(repository, tag, directory, inventory_sha)
    if target not in release.application_targets(state['metadata']):
        raise ValueError('Unknown application target for this release')
    name = release.application_names(state['metadata'])[target]
    archive = download_asset(repository, tag, name, state['directory'], state['assets'], state['inventory'][name])
    state['archive'] = archive
    state['package_root'] = extract_archive(archive, state['directory'], state['metadata'], target)
    return state


def download_apt(repository, tag, directory, inventory_sha, trusted_fingerprint=None):
    """Pin the signed repository and original payloads to the prepared release."""
    if not re.fullmatch(r'[0-9a-f]{64}', inventory_sha):
        raise ValueError('A complete prepared inventory SHA-256 is required')
    state = prepare(repository, tag, directory, inventory_sha)
    metadata = state['metadata']
    if metadata['schema'] < 3:
        raise ValueError('This release does not contain a signed APT repository')
    names = release.apt_assets(metadata) | release.distribution_assets(metadata) | {
        name for target, name in release.application_names(metadata).items()
        if release.target_platform(metadata, target).startswith('linux-')}
    for name in sorted(names):
        download_asset(repository, tag, name, state['directory'], state['assets'], state['inventory'][name])
    release.apt_tool().verify(state['directory'], metadata, repository=repository,
                              trusted_fingerprint=trusted_fingerprint)
    if metadata['schema'] >= 4:
        release.distro_tool().verify(state['directory'], metadata, repository=repository)
    release.verify_channels(state['directory'], metadata, repository, trusted_fingerprint)
    return state


def download_distro(repository, tag, directory, inventory_sha, trusted_fingerprint=None):
    state = download_apt(repository, tag, directory, inventory_sha, trusted_fingerprint)
    if state['metadata']['schema'] < 4:
        raise ValueError('This release does not contain distribution recipes')
    return state


def record(repository, tag, run_id, results_path, run_attempt='1'):
    if not re.fullmatch(r'[1-9][0-9]*', str(run_id)) or not re.fullmatch(r'[1-9][0-9]*', str(run_attempt)):
        raise ValueError('Run ID and attempt must be positive integers')
    results = json.loads(results_path.read_text(encoding='utf-8'))
    if not isinstance(results, dict):
        raise ValueError('Results must be a JSON object')
    jobs = results.get('jobs', {})
    if not isinstance(jobs, dict) or any(not isinstance(k, str) or not re.fullmatch(r'[A-Za-z0-9_.-]+', k)
                                        or v not in ('success', 'failure', 'cancelled', 'skipped', 'timed_out', 'pending')
                                        for k, v in jobs.items()):
        raise ValueError('Results must contain job names and conclusion strings')
    inventory_sha = results.get('inventory_sha256', '')
    if not re.fullmatch(r'[0-9a-f]{64}', inventory_sha):
        raise ValueError('Results require the prepared inventory SHA-256')
    with tempfile.TemporaryDirectory(prefix='datapump-certification-') as scratch:
        state = prepare(repository, tag, Path(scratch) / 'published', inventory_sha)
        metadata = state['metadata']
        warnings = validate_warnings(results.get('warnings', []), metadata)
        smoke_warnings = validate_smoke_warnings(results.get('smoke_warnings', []), metadata,
                                                 inventory_sha, run_id, run_attempt)
        required = required_jobs(metadata)
        jobs_passed = required <= jobs.keys() and all(value == 'success' for value in jobs.values())
        if results.get('source_sha') != metadata['source_sha']:
            raise ValueError('Tested source commit differs from the published release')
        names = release.application_names(metadata)
        tested_targets = results.get('tested_targets', list(names) if metadata['schema'] == 1 else [])
        if (not isinstance(tested_targets, list) or any(not isinstance(target, str) for target in tested_targets)
                or len(set(tested_targets)) != len(tested_targets) or not set(tested_targets) <= names.keys()):
            raise ValueError('Tested targets must be unique application identities from this release')
        passed = jobs_passed and set(tested_targets) == names.keys()
        # Exact hosted exclusions and incomplete smoke results stay visible.
        # Neither excuses other failures nor attests to coverage not completed.
        latest_eligible = passed and not metadata['experiment'] and metadata['schema'] >= 5
        status = ('passed_with_warnings' if warnings or smoke_warnings else 'passed') if passed else 'failed'
        if latest_eligible:
            latest_note = 'This ordinary release remains eligible for Latest.'
        elif metadata['experiment']:
            latest_note = 'This experiment remains a prerelease and is not eligible for Latest.'
        elif metadata['schema'] < 5:
            latest_note = 'This older release lacks the metadata and delivery channels required for Latest.'
        else:
            latest_note = 'Failed or incomplete required checks keep this release ineligible for Latest.'
        # GitHub normally supplies SHA-256 asset digests. Older hosts require
        # re-reading bytes before a report can describe the current assets.
        checked_names = (list(names.values()) + sorted(release.apt_assets(metadata) | release.distribution_assets(metadata)
                                                      | release.dependency_assets(metadata))
                         + (['warning.log'] if metadata['schema'] >= 2 else []))
        for name in checked_names:
            if not state['assets'][name].get('digest'):
                download_asset(repository, tag, name, state['directory'], state['assets'], state['inventory'][name])
        run_url = state['published']['html_url'].rsplit('/releases/tag/', 1)[0] + '/actions/runs/' + str(run_id)
        asset_base = state['published']['html_url'].rsplit('/tag/', 1)[0] + '/download/' + quote(tag, safe='')
        evidence = {
            'schema': metadata['schema'], 'status': status, 'repository': repository,
            'tag': tag, 'source_sha': metadata['source_sha'], 'inventory_sha256': inventory_sha,
            'linux_baseline': metadata['linux_baseline'], 'experiment': metadata['experiment'],
            'run_id': str(run_id), 'run_attempt': str(run_attempt), 'run_url': run_url,
            'created_at': datetime.now(timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z'),
            'required_jobs': sorted(required), 'jobs': jobs, 'scope': SCOPE,
            'required_coverage': required_coverage(metadata),
            'assets': {name: state['inventory'][name] for name in names.values()},
            'latest_eligible': latest_eligible,
        }
        if warnings:
            evidence['warnings'] = warnings
            evidence['coverage_exclusions'] = {
                warning['target']: warning['omitted_checks'] for warning in warnings}
            evidence['hosted_warning_policy'] = HOSTED_WARNING_POLICY
        if smoke_warnings:
            evidence['smoke_warnings'] = smoke_warnings
            evidence['smoke_warning_policy'] = SMOKE_WARNING_POLICY
            evidence['incomplete_smoke_coverage'] = {
                target: sorted({item['scope'] for item in smoke_warnings if item['target'] == target})
                for target in sorted({item['target'] for item in smoke_warnings})}
        if metadata['schema'] >= 2:
            evidence.update(
                gui_backends=metadata['gui_backends'], tested_targets=sorted(tested_targets),
                known_warning_asset='warning.log', warning_sha256=state['inventory']['warning.log'],
                warning_policy=DISPLAY_WARNING_POLICY,
                application_targets={target: {
                    'platform': release.target_platform(metadata, target),
                    'gui_backend': release.target_backend(metadata, target),
                    'asset': name, 'sha256': state['inventory'][name],
                } for target, name in names.items()})
        if metadata['schema'] >= 3:
            evidence['apt_assets'] = {name: state['inventory'][name] for name in sorted(release.apt_assets(metadata))}
            evidence['packager_sha'] = metadata['packager_sha']
            evidence['tag_sha'] = release.tag_revision(metadata)
            if 'repackaged_from' in metadata:
                evidence['repackaged_from'] = metadata['repackaged_from']
        if metadata['schema'] >= 4:
            evidence['distribution_assets'] = {name: state['inventory'][name] for name in sorted(release.distribution_assets(metadata))}
        if metadata['schema'] >= 6:
            evidence['dependencies'] = metadata['dependencies']
            evidence['dependency_assets'] = {name: state['inventory'][name] for name in sorted(release.dependency_assets(metadata))}
        evidence['frontends'] = metadata.get('frontends', [])
        stem = f'certification-{run_id}-attempt-{run_attempt}'
        if any(stem + suffix in state['assets'] for suffix in (
                '.json', '.md', '-warning.log', '-smoke-warnings.json', '-smoke-warnings.log')):
            raise ValueError('Certification evidence already exists; never overwrite a prior run')
        json_path, markdown_path = Path(scratch) / (stem + '.json'), Path(scratch) / (stem + '.md')
        warning_paths = []
        warning_text = ''
        smoke_warning_text = ''
        if warnings:
            warning_path = Path(scratch) / (stem + '-warning.log')
            warning_text = '\n'.join(f'WARNING {item["code"]}: {item["message"]}\n'
                f'Omitted checks for {item["target"]}: {", ".join(item["omitted_checks"])}\n'
                f'Observed probe output: {item["probe"]["output"]}\n' for item in warnings)
            warning_path.write_text(warning_text, encoding='utf-8')
            warning_paths.append(str(warning_path))
            evidence['warning_report_asset'] = warning_path.name
            evidence['warning_report_sha256'] = release.digest(warning_path)
        if smoke_warnings:
            smoke_json_path = Path(scratch) / (stem + '-smoke-warnings.json')
            smoke_log_path = Path(scratch) / (stem + '-smoke-warnings.log')
            release.write_json(smoke_json_path, smoke_warnings)
            smoke_warning_text = '\n'.join(
                f'WARNING {item["code"]}: {item["target"]} / {item["scope"]}\n'
                f'Incomplete GUI smoke coverage, not a smoke pass. {item["diagnostic"]}\n'
                f'Source {item["source_sha"]}; inventory {item["inventory_sha256"]}; '
                f'run {item["run_id"]}, attempt {item["run_attempt"]}.\n'
                for item in smoke_warnings)
            smoke_log_path.write_text(smoke_warning_text, encoding='utf-8')
            warning_paths.extend((str(smoke_json_path), str(smoke_log_path)))
            evidence['smoke_warning_json_asset'] = smoke_json_path.name
            evidence['smoke_warning_json_sha256'] = release.digest(smoke_json_path)
            evidence['smoke_warning_log_asset'] = smoke_log_path.name
            evidence['smoke_warning_log_sha256'] = release.digest(smoke_log_path)
        release.write_json(json_path, evidence)
        coverage = evidence['required_coverage']
        status_label = evidence['status'].replace('_', ' ')
        markdown_path.write_text(
            f'# Release certification: {status_label}\n\n'
            f'Release `{tag}`; source `{metadata["source_sha"]}`; '
            f'[workflow run {run_id}, attempt {run_attempt}]({run_url}/attempts/{run_attempt}).\n\n{SCOPE}\n\n'
            + ('TUI and framebuffer delivery and independent source checks are required by this release inventory.\n\n'
               if metadata.get('frontends') else
               'Historical release: TUI/framebuffer delivery is not declared; no frontend coverage is claimed.\n\n')
            + ('**Windows Rev graphics coverage unavailable.** '
               + ('Hosted certification passed with documented exclusions. ' if passed
                  else 'Hosted certification failed. ')
               + HOSTED_WARNING_POLICY + ' ' + latest_note + '\n\n'
               + warning_text + '\n' if warnings else '')
            + ('**Incomplete GUI smoke coverage.** '
               + ('Hosted certification passed with documented incomplete smoke coverage. ' if passed
                  else 'Hosted certification failed. ')
               + SMOKE_WARNING_POLICY + ' ' + latest_note + '\n\n'
               + f'[Smoke warning JSON]({asset_base}/{stem}-smoke-warnings.json), '
               + f'[smoke warning log]({asset_base}/{stem}-smoke-warnings.log).\n\n'
               + smoke_warning_text + '\n' if smoke_warnings else '')
            + '\n'.join(f'- {job}: **{jobs.get(job, "missing")}**' for job in sorted(required | jobs.keys()))
            + ('\n\nRecorded application targets: ' + ', '.join(f'`{target}`' for target in tested_targets)
               + '. Missing targets: ' + (', '.join(f'`{target}`' for target in names if target not in tested_targets) or 'none')
               + '.' if metadata['schema'] >= 2 else '')
            + (f'\n\n{DISPLAY_WARNING_POLICY} Known limitations: '
               f'[warning.log]({asset_base}/warning.log).' if metadata['schema'] >= 2 else '')
            + '\n\nHosted certification succeeds with status **passed** or **passed_with_warnings**. '
            'Documented exclusions remain untested; incomplete smoke coverage remains unqualified. '
            'All other required coverage must pass. '
            'Source suites rebuild the recorded release commit, including the full calibration tests. '
            'Archive checks run the published bytes identified by the hashes below.\n\n'
            + '\n'.join(f'- Source `{target}`: {item["environment"]}; groups '
                        + ', '.join(item['groups']) + '.' for target, item in coverage['source_tests'].items())
            + '\n\n'
            + '\n'.join(f'- Published `{target}`: ' + ', '.join(item['environments']) + '; '
                        + ', '.join(item['checks'])
                        + (f'; GLIBC <= {item["glibc_max"]}' if 'glibc_max' in item else '') + '.'
                        for target, item in coverage['published_archives'].items())
            + ('\n\nSigned APT repository: signatures, indexes, package metadata and payloads must pass '
               'the apt-repository job against this same release inventory.\n' if metadata['schema'] >= 3 else '')
            + '\n\nPublished application SHA-256 values:\n\n'
            + '\n'.join(f'- `{name}`: `{digest}`' for name, digest in evidence['assets'].items()) + '\n'
            + ('\nPublished APT asset SHA-256 values:\n\n'
               + '\n'.join(f'- `{name}`: `{digest}`' for name, digest in evidence['apt_assets'].items())
               + '\n' if metadata['schema'] >= 3 else '')
            + ('\nPublished distribution delivery SHA-256 values (distro-recipes job required):\n\n'
               + '\n'.join(f'- `{name}`: `{digest}`' for name, digest in evidence['distribution_assets'].items())
               + '\n' if metadata['schema'] >= 4 else '')
            + ('\nPreserved SDK/dependency SHA-256 values:\n\n'
               + '\n'.join(f'- `{name}`: `{digest}`' for name, digest in evidence['dependency_assets'].items())
               + '\n' if metadata['schema'] >= 6 else ''),
            encoding='utf-8')
        # No --clobber, no binary upload, and no promotion before evidence upload.
        gh(['release', 'upload', tag, '--repo', repository, str(json_path), str(markdown_path), *warning_paths])
        body = state['published'].get('body') or ''
        # Replace only the current status marker; keep all original prose and
        # immutable per-run report history, including earlier passed reports.
        body = body.replace(release.CERTIFICATION_PENDING,
                            f'> **Certification {status_label}.** See the certification reports below.', 1)
        body = re.sub(r'\*\*Certification (?:pending|passed|passed with warnings|passed_with_warnings|failed)\.\*\*',
                      f'**Certification {status_label}.**', body, count=1)
        body += (f'\n\nCertification [run {run_id}, attempt {run_attempt}]({run_url}/attempts/{run_attempt}): '
                 f'**{status_label}** '
                 f'([report]({asset_base}/{stem}.md), [JSON]({asset_base}/{stem}.json)). '
                 'This report applies only to the recorded source and asset hashes.\n')
        if warnings:
            body += (f'**Windows Rev native graphics remain unqualified on this runner.** '
                     f'[Environment warning log]({asset_base}/{stem}-warning.log). '
                     + ('Hosted certification passed with documented exclusions. ' if passed
                        else 'Hosted certification failed. ')
                     + HOSTED_WARNING_POLICY + ' ' + latest_note + '\n')
        if smoke_warnings:
            body += ('**Incomplete GUI smoke coverage, not a smoke pass.** '
                     + ('Hosted certification passed with documented incomplete smoke coverage. ' if passed
                        else 'Hosted certification failed. ')
                     + SMOKE_WARNING_POLICY + ' ' + latest_note + '\n\n'
                     + '\n'.join(f'- `{target}`: ' + ', '.join(f'`{scope}`' for scope in scopes)
                                 for target, scopes in evidence['incomplete_smoke_coverage'].items())
                     + f'\n\n[Smoke warning JSON]({asset_base}/{stem}-smoke-warnings.json), '
                     + f'[smoke warning log]({asset_base}/{stem}-smoke-warnings.log).\n')
        if metadata['schema'] < 5:
            body += ('This older release cannot become Latest because it lacks the signed APT repository '
                     'or signed Arch/Gentoo update channels required by current consumers.\n')
        notes = Path(scratch) / 'release-body.md'
        notes.write_text(body, encoding='utf-8')
        flags = ['--latest=true', '--prerelease=false'] if latest_eligible else ['--latest=false']
        if metadata['experiment']:
            flags += ['--title', 'experiment', '--prerelease']
        gh(['release', 'edit', tag, '--repo', repository, '--notes-file', str(notes), *flags])
    return evidence


def output_values(state, output):
    metadata = state['metadata']
    values = {'source_sha': metadata['source_sha'], 'baseline': metadata['linux_baseline'],
              'experiment': str(metadata['experiment']).lower(), 'inventory_sha256': state['inventory_sha256'],
              'schema': str(metadata['schema']),
              'apt_repository': str(metadata['schema'] >= 3).lower(),
              'distro_recipes': str(metadata['schema'] >= 4).lower(),
              'distro_channels': str(metadata['schema'] >= 5).lower(),
              'frontends': str(bool(metadata.get('frontends'))).lower(),
              'gui_backends': json.dumps(metadata.get('gui_backends', ['fltk']), separators=(',', ':')),
              'application_targets': json.dumps(list(release.application_targets(metadata)), separators=(',', ':'))}
    for name in ('archive', 'package_root'):
        if name in state:
            values[name] = str(state[name])
    if output:
        with output.open('a', encoding='utf-8') as stream:
            for name, value in values.items():
                if '\n' in value or '\r' in value:
                    raise ValueError('Workflow output must be a single line')
                stream.write(f'{name}={value}\n')
    return values


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('prepare', 'download', 'download-apt', 'download-distro', 'record'):
        command = commands.add_parser(name)
        command.add_argument('--repo', required=True)
        command.add_argument('--tag', required=True)
        if name == 'record':
            command.add_argument('--run-id', required=True)
            command.add_argument('--run-attempt', default=os.environ.get('GITHUB_RUN_ATTEMPT', '1'))
            command.add_argument('--results-json', type=Path, required=True)
        else:
            command.add_argument('--directory', type=Path, required=True)
            command.add_argument('--github-output', type=Path)
        if name == 'download':
            command.add_argument('--target', required=True)
        if name in ('download', 'download-apt', 'download-distro'):
            command.add_argument('--inventory-sha256', required=True)
        if name in ('download-apt', 'download-distro'):
            command.add_argument('--apt-signing-fingerprint')
    args = parser.parse_args(argv)
    try:
        if args.command == 'prepare':
            result = output_values(prepare(args.repo, args.tag, args.directory), args.github_output)
        elif args.command == 'download-distro':
            result = output_values(download_distro(args.repo, args.tag, args.directory, args.inventory_sha256,
                                                   args.apt_signing_fingerprint), args.github_output)
        elif args.command == 'download-apt':
            result = output_values(download_apt(args.repo, args.tag, args.directory, args.inventory_sha256,
                                                args.apt_signing_fingerprint), args.github_output)
        elif args.command == 'download':
            result = output_values(download(args.repo, args.tag, args.target, args.directory, args.inventory_sha256), args.github_output)
        else:
            result = record(args.repo, args.tag, args.run_id, args.results_json, args.run_attempt)
        print(json.dumps(result, sort_keys=True))
        if args.command == 'record' and result['status'] not in ('passed', 'passed_with_warnings'):
            return 1
    except (ValueError, KeyError, OSError, RuntimeError, subprocess.CalledProcessError, tarfile.TarError, zipfile.BadZipFile) as error:
        parser.exit(1, f'certification: {release.failure_message(error)}\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

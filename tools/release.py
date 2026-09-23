#!/usr/bin/env python3
"""Create, verify and publish the manual portable-release inventory with gh.

Packaging and platform qualification belong to build.sh and the workflow. This
helper only accepts their complete, checksummed archive pairs; it never rebuilds
an archive or treats a failed GitHub request as evidence that a tag is available.
"""
import argparse
from datetime import datetime, timedelta, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

ROOT = Path(__file__).resolve().parents[1]
TARGETS = {
    'linux-x86_64': ('Linux', ('x86_64', 'amd64'), '.tar.gz'),
    'linux-aarch64': ('Linux', ('aarch64', 'arm64'), '.tar.gz'),
    'windows-x86_64': ('Windows', ('AMD64', 'x86_64', 'amd64'), '.zip'),
}
GUI_BACKENDS = ('fltk', 'rev')
LABEL = re.compile(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,63}')
SHA = re.compile(r'[0-9a-f]{40}|[0-9a-f]{64}')
SUPPORT_FILES = {'release-metadata.json', 'release-notes.md'}
WARNING_LOG = ('REV_REPLAY_CADENCE: Rev replay/waterfall may refresh below the display target.\n'
               'Display cadence deviations are warnings; data integrity and physical-completion\n'
               'checks remain mandatory. Actual observed warnings are recorded in later\n'
               'certification reports/logs.\n')
CERTIFICATION_PENDING = ('> **Certification pending:** this release has not completed release '
                         'certification and is not marked Latest.')


def project_version(root=ROOT):
    contents = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    match = re.search(r'project\(DataPump\s+VERSION\s+(\d+(?:\.\d+){1,3})\s', contents)
    if not match:
        raise ValueError('Cannot read the DataPump project version from CMakeLists.txt')
    return match[1]


def chicago_time(instant):
    if instant.tzinfo is None or instant.utcoffset() is None:
        raise ValueError('Build timestamp must include a UTC offset')
    try:
        return instant.astimezone(ZoneInfo('America/Chicago'))
    except ZoneInfoNotFoundError:
        # Windows Python may not have the IANA database. Modern US rules permit
        # stdlib-only operation there; historical dates require the database.
        utc = instant.astimezone(timezone.utc)
        if utc.year < 2007:
            raise ValueError('America/Chicago timezone data is required before 2007')
        march = datetime(utc.year, 3, 1, 8, tzinfo=timezone.utc)
        november = datetime(utc.year, 11, 1, 7, tzinfo=timezone.utc)
        start = march + timedelta(days=(6 - march.weekday()) % 7 + 7)
        end = november + timedelta(days=(6 - november.weekday()) % 7)
        daylight = start <= utc < end
        return utc.astimezone(timezone(timedelta(hours=-5 if daylight else -6),
                                      'CDT' if daylight else 'CST'))


def make_metadata(*, source_sha, run_id, run_attempt, version='', experiment=False,
                  linux_baseline='bookworm-sdk', now=None, cmake_version=None, schema=2):
    if type(schema) is not int or schema not in (1, 2):
        raise ValueError('Unsupported release metadata schema')
    cmake_version = cmake_version or project_version()
    if not re.fullmatch(r'\d+(?:\.\d+){1,3}', cmake_version):
        raise ValueError('Invalid CMake project version')
    version = version or f'v{cmake_version}'
    if not LABEL.fullmatch(version) or '..' in version or version.endswith('.'):
        raise ValueError('Version must be a safe label of 1–64 letters, digits, dots, underscores or hyphens')
    if not SHA.fullmatch(source_sha):
        raise ValueError('Source SHA must be a complete lowercase Git object ID')
    if not re.fullmatch(r'[1-9][0-9]*', str(run_id)) or not re.fullmatch(r'[1-9][0-9]*', str(run_attempt)):
        raise ValueError('Workflow run ID and attempt must be positive integers')
    if type(experiment) is not bool:
        raise ValueError('Experiment must be a boolean')
    if linux_baseline not in ('bookworm-sdk', 'ubuntu-22.04'):
        raise ValueError('Unknown Linux baseline')
    instant = now or datetime.now(timezone.utc)
    local = chicago_time(instant)
    tag = f'{version}-{local:%Y-%m-%d-%H%M%Z}'
    value = {
        'schema': schema, 'version': version, 'project_version': cmake_version,
        'tag': tag, 'title': 'experiment' if experiment else tag,
        'experiment': experiment, 'linux_baseline': linux_baseline,
        'source_sha': source_sha, 'run_id': str(run_id), 'run_attempt': str(run_attempt),
        'created_at': instant.astimezone(timezone.utc).isoformat(timespec='seconds').replace('+00:00', 'Z'),
        'build_date': local.strftime('%Y-%m-%d-%H%M%Z'), 'timezone': 'America/Chicago',
    }
    if schema == 2:
        value['gui_backends'] = list(GUI_BACKENDS)
    return value


def load_metadata(path):
    value = json.loads(path.read_text(encoding='utf-8'))
    try:
        expected = make_metadata(source_sha=value['source_sha'], run_id=value['run_id'],
                                 run_attempt=value['run_attempt'], version=value['version'],
                                 experiment=value['experiment'], linux_baseline=value['linux_baseline'],
                                 now=datetime.fromisoformat(value['created_at'].replace('Z', '+00:00')),
                                 cmake_version=value['project_version'], schema=value['schema'])
    except (KeyError, TypeError, AttributeError) as error:
        raise ValueError('Incomplete or invalid release metadata') from error
    if value != expected:
        raise ValueError('Release metadata does not match its timestamp, label or source fields')
    return value


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def check_inventory(directory, checksum_name='SHA256SUMS.txt'):
    """Require flat regular files, one strict digest per file and no extras."""
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError(f'Missing or unsafe artifact directory: {directory}')
    entries = list(directory.iterdir())
    if any(entry.is_symlink() or not entry.is_file() for entry in entries):
        raise ValueError(f'Artifact inventory must contain only regular files: {directory}')
    checksum = directory / checksum_name
    if not checksum.is_file():
        raise ValueError(f'Missing checksum inventory: {checksum}')
    inventory = {}
    for line in checksum.read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9_.-]*)', line)
        if not match or match[2] == checksum_name or match[2] in inventory:
            raise ValueError(f'Invalid or duplicate checksum entry: {line!r}')
        inventory[match[2]] = match[1]
    actual = {entry.name for entry in entries if entry.name != checksum_name}
    if not inventory or set(inventory) != actual:
        raise ValueError(f'Checksum inventory does not match files in {directory}')
    for name, expected in inventory.items():
        if digest(directory / name) != expected:
            raise ValueError(f'Checksum mismatch: {directory / name}')
    return inventory


def application_targets(metadata):
    """Map download identities to their physical platform/archive format."""
    if metadata['schema'] == 1:
        return dict(TARGETS)
    if metadata['schema'] != 2 or metadata.get('gui_backends') != list(GUI_BACKENDS):
        raise ValueError('Unsupported release schema or GUI backend inventory')
    return {f'{platform}-{backend}': details for platform, details in TARGETS.items()
            for backend in GUI_BACKENDS}


def target_platform(metadata, target):
    if target not in application_targets(metadata):
        raise ValueError('Unknown portable release target')
    return target if metadata['schema'] == 1 else target.rsplit('-', 1)[0]


def target_backend(metadata, target):
    target_platform(metadata, target)
    return 'fltk' if metadata['schema'] == 1 else target.rsplit('-', 1)[1]


def build_matrices(metadata):
    """Use the release identities for both builders and copied-binary coverage."""
    baseline = metadata['linux_baseline']
    if baseline not in ('bookworm-sdk', 'ubuntu-22.04'):
        raise ValueError('Unknown Linux baseline')
    linux, windows, compatibility = [], [], []
    for target in application_targets(metadata):
        platform = target_platform(metadata, target)
        backend = target_backend(metadata, target)
        if platform == 'windows-x86_64':
            windows.append({'backend': backend, 'target': target})
            continue
        arch = platform.removeprefix('linux-')
        sdk = arch == 'x86_64' and baseline == 'bookworm-sdk'
        row = {'arch': arch,
               'runner': 'ubuntu-24.04-arm' if arch == 'aarch64' else 'ubuntu-24.04',
               'image': 'debian:bookworm-slim' if sdk else 'ubuntu:22.04',
               'sdk': sdk, 'glibc': '2.36' if sdk else '2.35',
               'backend': backend, 'target': target}
        linux.append(row)
        images = ['debian:bookworm-slim', 'debian:trixie-slim', 'ubuntu:24.04', 'ubuntu:26.04']
        if row['glibc'] == '2.35':
            images.append('ubuntu:22.04')
        if arch == 'x86_64':
            images.append('archlinux:base')
        compatibility.extend(dict(row, image=image) for image in images)
    return {'linux_matrix': {'include': linux}, 'windows_matrix': {'include': windows},
            'compatibility_matrix': {'include': compatibility}}


def package_bases(metadata, target):
    system, architectures, _ = TARGETS[target_platform(metadata, target)]
    suffix = '' if metadata['schema'] == 1 else '-' + target_backend(metadata, target)
    return [f'DataPump-{metadata["project_version"]}-{system}-{arch}-native{suffix}'
            for arch in architectures]


def verify_archive_backend(archive, metadata, target):
    """Check new bundles' root and shipped build provenance without extracting."""
    target_platform(metadata, target)
    if metadata['schema'] == 1:
        return  # Existing releases predate backend-qualified package roots.
    bases = set(package_bases(metadata, target))
    provenance = 'share/doc/datapump/build-info.txt'
    contents = None
    roots = set()

    def inspect(name, size, regular, read):
        nonlocal contents
        name = name.removeprefix('./').rstrip('/')
        parts = name.split('/')
        if (not name or any(part in ('', '.', '..') for part in parts)
                or '\\' in name or parts[0] not in bases):
            raise ValueError(f'Archive has an unexpected package root or path for {target}')
        roots.add(parts[0])
        if '/'.join(parts[1:]) == provenance:
            if contents is not None or not regular or size > 65536:
                raise ValueError(f'Archive has duplicate or unsafe backend build-info for {target}')
            contents = read()

    try:
        if archive.name.endswith('.tar.gz'):
            with tarfile.open(archive, 'r:gz') as source:
                for member in source:
                    inspect(member.name, member.size, member.isfile(),
                            lambda member=member: source.extractfile(member).read())
        elif archive.name.endswith('.zip'):
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    kind = (member.external_attr >> 16) & 0o170000
                    inspect(member.filename, member.file_size,
                            not member.is_dir() and kind in (0, 0o100000),
                            lambda member=member: source.read(member))
        else:
            raise ValueError('Unsupported application archive format')
        if len(roots) != 1 or contents is None:
            raise ValueError(f'Archive is missing backend build-info for {target}')
        lines = [line for line in contents.decode('utf-8').splitlines() if line.startswith('GUI:')]
        backend = target_backend(metadata, target)
        if len(lines) != 1 or not re.fullmatch(rf'GUI: (?:ON|TRUE|YES|1) \({backend}\)', lines[0]):
            raise ValueError(f'Archive backend build-info does not match {target}')
    except (tarfile.TarError, zipfile.BadZipFile, UnicodeError) as error:
        raise ValueError(f'Invalid application archive or backend build-info for {target}') from error


def native_pair(directory, metadata, target):
    inventory = check_inventory(directory)
    bases = [base for base in package_bases(metadata, target)
             if set(inventory) == {base + '.tar.gz', base + '.zip'}]
    if len(bases) != 1:
        raise ValueError(f'Expected one matching native TGZ/ZIP pair for {target}')
    for extension in ('.tar.gz', '.zip'):
        verify_archive_backend(directory / (bases[0] + extension), metadata, target)
    return directory / (bases[0] + application_targets(metadata)[target][2])


def application_names(metadata):
    return {target: f'DataPump-{metadata["tag"]}-{target}{details[2]}'
            for target, details in application_targets(metadata).items()}


def support_files(metadata):
    return SUPPORT_FILES | ({'warning.log'} if metadata['schema'] == 2 else set())


def write_warning(metadata, directory):
    if metadata['schema'] == 2:
        (directory / 'warning.log').write_text(WARNING_LOG, encoding='utf-8')


def verify_warning(metadata, directory):
    if metadata['schema'] == 2 and (directory / 'warning.log').read_text(encoding='utf-8') != WARNING_LOG:
        raise ValueError('Release warning.log must preserve the known Rev presentation limitation')


def verify_sdk_pair(names):
    binaries = [(name, re.fullmatch(r'datapump-sdk-([A-Za-z0-9][A-Za-z0-9_.-]*)-linux-x86_64\.tar\.gz', name))
                for name in names]
    binaries = [(name, match[1]) for name, match in binaries if match]
    if len(binaries) != 1 or set(names) != {
            binaries[0][0], f'datapump-sdk-sources-{binaries[0][1]}.tar.gz'}:
        raise ValueError('SDK assets must be one binary and its matching preserved source archive')


def release_notes(metadata, details):
    warning = ('> **Experimental build:** this prerelease is for evaluation. '
               'Users needing assurance should use a qualified regular release.\n\n'
               if metadata['experiment'] else '')
    baseline = ('x86-64: glibc 2.36 (Bookworm source SDK); AArch64: glibc 2.35'
                if metadata['linux_baseline'] == 'bookworm-sdk'
                else 'x86-64 and AArch64: glibc 2.35 (Ubuntu 22.04)')
    return (f'{warning}{CERTIFICATION_PENDING}\n\nBuild **{metadata["tag"]}**\n\n'
            f'- Source commit: `{metadata["source_sha"]}`\n'
            f'- Build date: `{metadata["build_date"]}` (America/Chicago)\n'
            f'- Workflow run: `{metadata["run_id"]}`, attempt `{metadata["run_attempt"]}`\n'
            f'- Application version: `{metadata["project_version"]}`\n'
            f'- GUI downloads: {", ".join(metadata.get("gui_backends", ["fltk"]))}\n'
            f'- Linux ABI: {baseline}\n\n{details}')


def assemble(artifacts, metadata_path, notes, output, sdk_artifacts=None):
    metadata = load_metadata(metadata_path)
    if not notes.is_file() or notes.is_symlink() or not notes.read_text(encoding='utf-8').strip():
        raise ValueError('Release notes must be a nonempty regular UTF-8 file')
    if output.exists():
        raise ValueError(f'Refusing to replace an existing output directory: {output}')
    copies = []
    for target in application_targets(metadata):
        directory = artifacts / target
        copies.append((native_pair(directory, metadata, target), application_names(metadata)[target]))
    if sdk_artifacts:
        inventory = check_inventory(sdk_artifacts, 'SHA256SUMS')
        verify_sdk_pair(inventory)
        copies.extend((sdk_artifacts / name, name) for name in sorted(inventory))
    output.parent.mkdir(parents=True, exist_ok=True)
    # Validate everything before creating the visible output. A copy failure
    # leaves no apparently complete release directory behind.
    with tempfile.TemporaryDirectory(prefix='.release-', dir=output.parent) as temporary:
        staged = Path(temporary) / 'assets'
        staged.mkdir()
        for source, name in copies:
            shutil.copyfile(source, staged / name)
        write_json(staged / 'release-metadata.json', metadata)
        (staged / 'release-notes.md').write_text(
            release_notes(metadata, notes.read_text(encoding='utf-8')), encoding='utf-8')
        write_warning(metadata, staged)
        checksums = ''.join(f'{digest(path)}  {path.name}\n' for path in sorted(staged.iterdir()))
        (staged / 'SHA256SUMS.txt').write_text(checksums, encoding='utf-8')
        verify_release(staged)
        staged.rename(output)
    return metadata


def verify_release(directory):
    inventory = check_inventory(directory)
    metadata = load_metadata(directory / 'release-metadata.json')
    required = set(application_names(metadata).values()) | support_files(metadata)
    if not required <= set(inventory):
        raise ValueError('Release inventory is missing an application target or support file')
    extra = set(inventory) - required
    if extra:
        verify_sdk_pair(extra)
    verify_warning(metadata, directory)
    for target, name in application_names(metadata).items():
        verify_archive_backend(directory / name, metadata, target)
    return metadata, sorted(inventory)


def gh(arguments, *, check=True):
    return subprocess.run(['gh', *arguments], check=check, text=True, capture_output=True)


def require_absent(endpoint):
    response = gh(['api', '--include', endpoint], check=False)
    if response.returncode == 0:
        raise ValueError(f'Refusing to overwrite an existing GitHub release or tag: {endpoint}')
    # --include preserves the response status, distinguishing a missing resource
    # from authentication, rate limiting, networking and service failures.
    if not re.search(r'^HTTP/\S+ 404(?:\s|$)', response.stdout, re.MULTILINE):
        raise RuntimeError(f'Cannot confirm GitHub resource is absent: {endpoint}\n{response.stderr.strip()}')


def repository_name(repository):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*/[A-Za-z0-9][A-Za-z0-9_.-]*', repository):
        raise ValueError('Repository must be OWNER/REPO')
    return repository


def api_pages(endpoint):
    response = gh(['api', '--paginate', endpoint]).stdout
    decoder = json.JSONDecoder()
    rows = []
    while response.strip():
        response = response.lstrip()
        page, end = decoder.raw_decode(response)
        if not isinstance(page, list):
            raise ValueError('Expected a paginated GitHub array')
        rows.extend(page)
        response = response[end:]
    return rows


def draft_info(metadata, repository):
    repository_name(repository)
    tag = metadata['tag']
    # Authenticated release listing includes drafts and works with older gh
    # versions whose release view JSON does not support databaseId.
    matches = [item for item in api_pages(f'repos/{repository}/releases?per_page=100')
               if item.get('tag_name') == tag]
    info = matches[0] if len(matches) == 1 else {}
    if (not info.get('draft') or info.get('tag_name') != tag
            or info.get('name') != metadata['title']
            or info.get('prerelease') != metadata['experiment']
            or type(info.get('id')) is not int or info['id'] <= 0):
        raise ValueError('Release must be the matching reserved draft with its original title and experiment status')
    reference = json.loads(gh(['api', f'repos/{repository}/git/ref/tags/{tag}']).stdout)
    if reference.get('object') != {'type': 'commit', 'sha': metadata['source_sha']}:
        # GitHub also includes an object URL; only type and SHA define identity.
        obj = reference.get('object', {})
        if obj.get('type') != 'commit' or obj.get('sha') != metadata['source_sha']:
            raise ValueError('Reserved release tag does not identify the exact source commit')
    assets = api_pages(f'repos/{repository}/releases/{info["id"]}/assets?per_page=100')
    inventory = {asset['name']: asset for asset in assets}
    required = support_files(metadata)
    allowed = set(application_names(metadata).values()) | required
    if len(inventory) != len(assets) or not required <= set(inventory) or not set(inventory) <= allowed:
        raise ValueError('Reserved draft has missing support files, duplicate, finalized or unexpected assets')
    return inventory


def download_asset(repository, asset, path, *, require_digest=True):
    """Stream an inventoried asset by ID, without gh's embedded asset lookup."""
    repository_name(repository)
    asset_id = asset.get('id')
    expected = asset.get('digest')
    if type(asset_id) is not int or asset_id <= 0 or asset.get('state') != 'uploaded':
        raise ValueError(f'GitHub asset is not a completed upload: {path.name}')
    if ((require_digest or expected is not None)
            and (not isinstance(expected, str) or not re.fullmatch(r'sha256:[0-9a-f]{64}', expected))):
        raise ValueError(f'GitHub has not supplied a completed SHA-256 digest for {path.name}')
    # gh follows the asset API redirect. stdout goes directly to disk, avoiding
    # text decoding or buffering hundreds of megabytes of SDK data in memory.
    with path.open('xb') as output:
        try:
            subprocess.run(['gh', 'api', f'repos/{repository}/releases/assets/{asset_id}',
                            '-H', 'Accept:application/octet-stream'],
                           check=True, stdout=output, stderr=subprocess.PIPE)
        except BaseException:
            output.close()
            path.unlink()
            raise
    if ((expected is not None and digest(path) != expected.removeprefix('sha256:'))
            or ('size' in asset and path.stat().st_size != asset['size'])):
        path.unlink()
        raise ValueError(f'Downloaded release asset checksum mismatch: {path.name}')


def download_assets(metadata, repository, directory, assets, wanted):
    for name in sorted(wanted):
        download_asset(repository, assets[name], directory / name)


def check_draft_metadata(metadata, directory):
    if load_metadata(directory / 'release-metadata.json') != metadata:
        raise ValueError('Reserved draft metadata belongs to another workflow run or source commit')
    if CERTIFICATION_PENDING not in (directory / 'release-notes.md').read_text(encoding='utf-8'):
        raise ValueError('Reserved draft notes must retain the pending certification status')
    verify_warning(metadata, directory)


def reserve(metadata_path, notes, repository):
    repository_name(repository)
    metadata = load_metadata(metadata_path)
    if not notes.is_file() or notes.is_symlink() or not notes.read_text(encoding='utf-8').strip():
        raise ValueError('Release notes must be a nonempty regular UTF-8 file')
    # Listing also finds a pre-existing draft whose pending tag has no Git ref.
    existing = api_pages(f'repos/{repository}/releases?per_page=100')
    if any(item.get('tag_name') == metadata['tag'] for item in existing):
        raise ValueError('Refusing to overwrite an existing release or draft')
    with tempfile.TemporaryDirectory(prefix='release-reserve-') as temporary:
        directory = Path(temporary)
        write_json(directory / 'release-metadata.json', metadata)
        (directory / 'release-notes.md').write_text(
            release_notes(metadata, notes.read_text(encoding='utf-8')), encoding='utf-8')
        write_warning(metadata, directory)
        create_draft(metadata, repository, directory / 'release-notes.md')
        gh(['release', 'upload', metadata['tag'], '--repo', repository,
            *[str(directory / name) for name in sorted(support_files(metadata))]])
    return metadata


def upload(metadata_path, repository, target, directory):
    metadata = load_metadata(metadata_path)
    source = native_pair(directory, metadata, target)
    assets = draft_info(metadata, repository)
    name = application_names(metadata)[target]
    if name in assets:
        raise ValueError(f'Refusing to overwrite an existing target asset: {name}')
    with tempfile.TemporaryDirectory(prefix='release-upload-') as temporary:
        staged = Path(temporary)
        download_assets(metadata, repository, staged, assets, support_files(metadata))
        check_draft_metadata(metadata, staged)
        shutil.copyfile(source, staged / name)
        gh(['release', 'upload', metadata['tag'], '--repo', repository, str(staged / name)])
    return metadata


def finalize(metadata_path, repository, directory, publish_now=False):
    metadata = load_metadata(metadata_path)
    if directory.exists() or directory.is_symlink():
        raise ValueError('Refusing to replace an existing final release directory')
    assets = draft_info(metadata, repository)
    expected = set(application_names(metadata).values()) | support_files(metadata)
    if set(assets) != expected:
        count = 'three' if metadata['schema'] == 1 else 'six'
        raise ValueError(f'Draft is missing one or more of the {count} portable targets')
    directory.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.release-finalize-', dir=directory.parent) as temporary:
        staged = Path(temporary) / 'assets'
        staged.mkdir()
        download_assets(metadata, repository, staged, assets, expected)
        if {path.name for path in staged.iterdir()} != expected:
            raise ValueError('Downloaded final release inventory contains unexpected files')
        check_draft_metadata(metadata, staged)
        sums = ''.join(f'{digest(path)}  {path.name}\n' for path in sorted(staged.iterdir()))
        (staged / 'SHA256SUMS.txt').write_text(sums, encoding='utf-8')
        verify_release(staged)
        staged.rename(directory)
    gh(['release', 'upload', metadata['tag'], '--repo', repository, str(directory / 'SHA256SUMS.txt')])
    if publish_now:
        gh(['release', 'edit', metadata['tag'], '--repo', repository, '--draft=false', '--latest=false'])
    return metadata


def create_draft(metadata, repository, notes):
    repository_name(repository)
    gh(['api', f'repos/{repository}'])
    tag = metadata['tag']
    require_absent(f'repos/{repository}/releases/tags/{tag}')
    require_absent(f'repos/{repository}/git/ref/tags/{tag}')
    gh(['api', '--method', 'POST', f'repos/{repository}/git/refs',
        '-f', f'ref=refs/tags/{tag}', '-f', f'sha={metadata["source_sha"]}'])
    flags = ['--prerelease', '--latest=false'] if metadata['experiment'] else ['--latest=false']
    gh(['release', 'create', tag, '--repo', repository,
        '--target', metadata['source_sha'], '--title', metadata['title'],
        '--notes-file', str(notes), '--draft', '--verify-tag', *flags])


def publish(directory, repository, *, publish_now=False):
    repository_name(repository)
    metadata, inventory = verify_release(directory)
    # Establish repository visibility first: private-repository denial can also
    # be reported as 404 for the individual tag/release resources.
    tag = metadata['tag']
    # Creating the ref is atomic: unlike release create --target alone, it fails
    # if someone else claimed this tag between preflight and creation.
    create_draft(metadata, repository, directory / 'release-notes.md')
    # No --clobber: an unexpected collision must fail. Publishing is the final
    # operation; upload failures intentionally leave a recoverable draft.
    gh(['release', 'upload', tag, '--repo', repository,
        *[str(directory / name) for name in inventory], str(directory / 'SHA256SUMS.txt')])
    if publish_now:
        gh(['release', 'edit', tag, '--repo', repository, '--draft=false', '--latest=false'])
    return metadata


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    metadata = commands.add_parser('metadata', help='Create one release identity for all jobs')
    metadata.add_argument('--version', default='')
    metadata.add_argument('--source-sha', required=True)
    metadata.add_argument('--run-id', required=True)
    metadata.add_argument('--run-attempt', default='1')
    metadata.add_argument('--experiment', action='store_true')
    metadata.add_argument('--linux-baseline', choices=('bookworm-sdk', 'ubuntu-22.04'), default='bookworm-sdk')
    metadata.add_argument('--output', type=Path, required=True)
    metadata.add_argument('--github-output', type=Path)
    matrices = commands.add_parser('matrices', help='Select schema-aware build and compatibility jobs')
    matrices.add_argument('--metadata', type=Path, required=True)
    matrices.add_argument('--github-output', type=Path)
    stage = commands.add_parser('assemble', help='Verify inputs and stage the minimal release assets')
    stage.add_argument('--artifacts', type=Path, required=True)
    stage.add_argument('--metadata', type=Path, required=True)
    stage.add_argument('--notes', type=Path, required=True)
    stage.add_argument('--output', type=Path, required=True)
    stage.add_argument('--sdk-artifacts', type=Path)
    release = commands.add_parser('publish', help='Create a draft, upload all assets, optionally publish')
    release.add_argument('--directory', type=Path, required=True)
    release.add_argument('--repo', required=True)
    release.add_argument('--publish', action='store_true')
    for name in ('reserve', 'upload', 'finalize'):
        command = commands.add_parser(name)
        command.add_argument('--metadata', type=Path, required=True)
        command.add_argument('--repo', required=True)
        if name == 'reserve':
            command.add_argument('--notes', type=Path, required=True)
        else:
            command.add_argument('--directory', type=Path, required=True)
        if name == 'upload':
            command.add_argument('--target', required=True,
                                 help='Platform identity, including -fltk or -rev for schema 2')
        if name == 'finalize':
            command.add_argument('--publish', action='store_true')
    args = parser.parse_args(argv)
    try:
        if args.command == 'metadata':
            value = make_metadata(source_sha=args.source_sha, run_id=args.run_id,
                                  run_attempt=args.run_attempt, version=args.version,
                                  experiment=args.experiment, linux_baseline=args.linux_baseline)
            write_json(args.output, value)
            if args.github_output:
                with args.github_output.open('a', encoding='utf-8') as output:
                    for key in ('tag', 'title', 'version', 'experiment', 'build_date', 'source_sha'):
                        text = str(value[key]).lower() if type(value[key]) is bool else value[key]
                        output.write(f'{key}={text}\n')
        elif args.command == 'matrices':
            metadata = load_metadata(args.metadata)
            value = build_matrices(metadata)
            if args.github_output:
                with args.github_output.open('a', encoding='utf-8') as output:
                    for key, data in {'metadata_json': metadata, **value}.items():
                        output.write(f'{key}={json.dumps(data, separators=(",", ":"))}\n')
        elif args.command == 'assemble':
            value = assemble(args.artifacts, args.metadata, args.notes, args.output, args.sdk_artifacts)
        elif args.command == 'reserve':
            value = reserve(args.metadata, args.notes, args.repo)
        elif args.command == 'upload':
            value = upload(args.metadata, args.repo, args.target, args.directory)
        elif args.command == 'finalize':
            value = finalize(args.metadata, args.repo, args.directory, args.publish)
        else:
            value = publish(args.directory, args.repo, publish_now=args.publish)
    except (ValueError, OSError, RuntimeError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'release: {error}\n')
    print(json.dumps(value, sort_keys=True))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Create and verify the signed Gentoo release update channel."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile


def sync_module():
    spec = importlib.util.spec_from_file_location('datapump_gentoo_sync', Path(__file__).with_name('gentoo-sync.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def asset_names(metadata):
    if metadata.get('schema', 0) < 5:
        return set()
    client = sync_module()
    return {client.CHANNEL, client.CHANNEL + '.asc', client.HELPER, client.ADAPTER_NAME}


def manifest(directory, metadata, repository, signing_fingerprint):
    client = sync_module()
    if metadata.get('schema') != 5:
        raise ValueError('Gentoo update channels require release schema 5')
    directory = Path(directory)
    result = {key: metadata[key] for key in ('tag', 'created_at', 'project_version', 'version',
                                            'run_id', 'run_attempt', 'experiment')}
    result.update(schema=1, kind='datapump-gentoo-channel', repository=repository,
                  signing_fingerprint=client.fingerprint(signing_fingerprint),
                  metadata_sha256=client.digest(json.dumps(metadata, sort_keys=True, separators=(',', ':')).encode()),
                  assets={name: {'sha256': client.digest((directory / name).read_bytes()),
                                 'size': (directory / name).stat().st_size} for name in sorted(client.ASSETS)})
    client.validate_manifest(client.canonical(result), repository, signing_fingerprint, metadata['tag'])
    return result


def sign(directory, signing_key, expected_fingerprint):
    client = sync_module()
    directory = Path(directory).resolve()
    with tempfile.TemporaryDirectory(prefix='datapump-gentoo-sign-') as temporary:
        home = Path(temporary)
        def gpg(*arguments):
            return subprocess.run(['gpg', '--batch', '--homedir', str(home), *map(str, arguments)],
                                  capture_output=True, check=True)
        gpg('--import', Path(signing_key).resolve())
        keyring = home / 'public.gpg'
        keyring.write_bytes(gpg('--export', client.fingerprint(expected_fingerprint)).stdout)
        gpg('--yes', '--armor', '--pinentry-mode', 'loopback', '--passphrase', '',
            '--digest-algo', 'SHA256', '--local-user', client.fingerprint(expected_fingerprint),
            '--output', directory / (client.CHANNEL + '.asc'), '--detach-sign', directory / client.CHANNEL)
        client.verify_signature((directory / client.CHANNEL).read_bytes(),
                                (directory / (client.CHANNEL + '.asc')).read_bytes(), keyring, expected_fingerprint)


def build(directory, metadata, repository, signing_key, signing_fingerprint):
    client = sync_module()
    directory = Path(directory)
    if any((directory / name).exists() or (directory / name).is_symlink() for name in asset_names(metadata)):
        raise ValueError('Refusing to overwrite existing Gentoo channel assets')
    (directory / client.HELPER).write_bytes(Path(__file__).with_name('gentoo-sync.py').read_bytes())
    (directory / client.ADAPTER_NAME).write_text(client.ADAPTER)
    (directory / client.HELPER).chmod(0o755)
    (directory / client.ADAPTER_NAME).chmod(0o644)
    result = manifest(directory, metadata, repository, signing_fingerprint)
    (directory / client.CHANNEL).write_bytes(client.canonical(result))
    sign(directory, signing_key, signing_fingerprint)
    return result


def verify(directory, metadata, repository=None, expected_fingerprint=None):
    client = sync_module()
    directory = Path(directory)
    result = client.strict_json((directory / client.CHANNEL).read_bytes())
    repository = repository or result['repository']
    expected_fingerprint = expected_fingerprint or result['signing_fingerprint']
    verified = client.verify_local_assets(directory, directory / 'datapump-archive-keyring.gpg',
                                         expected_fingerprint, repository, metadata['tag'])
    if verified != manifest(directory, metadata, repository, expected_fingerprint):
        raise ValueError('Gentoo channel differs from release metadata and asset inventory')
    # The signed hashes bind the exact historical helper and adapter. Comparing
    # them with today's source would make an updater fix invalidate older
    # releases that are still correctly signed and immutable.
    return verified

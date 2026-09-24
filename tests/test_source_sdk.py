#!/usr/bin/env python3
"""Exercise SDK preservation, archive safety and ABI checks without a toolchain build."""
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('source_sdk', ROOT / 'tools/build-sdk.py')
sdk = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(sdk)


def make_tar(path, entries):
    """Entries are (name, type, contents/link target), preserving supplied names."""
    with tarfile.open(path, 'w:gz') as archive:
        for name, kind, value in entries:
            member = tarfile.TarInfo(name)
            member.type = kind
            member.mode = 0o755 if kind == tarfile.DIRTYPE else 0o644
            if kind == tarfile.REGTYPE:
                data = value.encode()
                member.size = len(data)
                archive.addfile(member, io.BytesIO(data))
            else:
                member.linkname = value
                archive.addfile(member)


class SourceSdkTests(unittest.TestCase):
    def setUp(self):
        # Buildroot installation explicitly forbids whitespace in its paths.
        self.temp = tempfile.TemporaryDirectory(prefix='datapump-source-sdk-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.cache = self.root / 'cache'
        self.cache.mkdir()

    def inventory(self):
        source = self.cache / 'downloads/fixture/source.tar.gz'
        source.parent.mkdir(parents=True)
        source.write_bytes(b'preserved dependency source')
        state = {'id': 'fixture-id', 'files': {
            str(source.relative_to(self.cache)): sdk.digest(source)}}
        sdk.write_json(self.cache / 'sources.json', state)
        return source, state

    def test_offline_fetch_requires_intact_archive_and_never_contacts_network(self):
        source = self.root / 'package.tar.gz'
        item = {'url': 'https://example.invalid/source',
                'sha256': hashlib.sha256(b'intact').hexdigest()}
        with patch.object(sdk.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            with self.assertRaisesRegex(ValueError, 'Missing'):
                sdk.fetch_file(item, source, False)
            source.write_bytes(b'intact')
            sdk.fetch_file(item, source, False)
            source.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                sdk.fetch_file(item, source, False)

    def test_corrupt_download_is_not_installed_or_left_as_a_partial_file(self):
        source = self.root / 'download/archive.tar.gz'
        item = {'url': 'https://example.invalid/source', 'sha256': '0' * 64}
        with patch.object(sdk.urllib.request, 'urlopen', return_value=io.BytesIO(b'corruption')):
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                sdk.fetch_file(item, source, True)
        self.assertEqual(list(source.parent.iterdir()), [])

    def test_offline_inventory_rejects_missing_changed_and_wrong_recipe_sources(self):
        with self.assertRaisesRegex(ValueError, 'No complete source inventory'):
            sdk.validate_sources(self.cache, 'fixture-id')
        source, _ = self.inventory()
        sdk.validate_sources(self.cache, 'fixture-id')
        with self.assertRaisesRegex(ValueError, 'another recipe'):
            sdk.validate_sources(self.cache, 'new-id')
        source.write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Missing or changed'):
            sdk.validate_sources(self.cache, 'fixture-id')
        source.unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or changed'):
            sdk.validate_sources(self.cache, 'fixture-id')

    def test_resolved_inventory_deduplicates_shared_downloads_and_ignores_old_cache(self):
        selected, _ = self.inventory()
        old = selected.parent / 'older-version.tar.gz'
        old.write_bytes(b'cached previous release')
        package = {'dl_dir': 'fixture', 'downloads': [{'source': selected.name}]}
        packages = {'compiler-initial': package, 'compiler-final': package,
                    'host-compiler': package, 'virtual-toolchain': {'virtual': True}}
        self.assertEqual(sdk.resolved_downloads(self.cache, packages), [selected])

    def test_resolved_inventory_rejects_missing_and_escaping_downloads(self):
        selected, _ = self.inventory()
        outside = self.root / 'outside.tar.gz'
        outside.write_bytes(b'outside cache')
        (selected.parent / 'escape.tar.gz').symlink_to(outside)
        for directory, source, diagnostic in (
            ('fixture', 'missing.tar.gz', 'Missing or changed'),
            ('../outside', 'source.tar.gz', 'Invalid resolved download path'),
            (str(self.root), 'outside.tar.gz', 'Invalid resolved download path'),
            ('fixture', '../../outside.tar.gz', 'Invalid resolved download path'),
            ('fixture', str(outside), 'Invalid resolved download path'),
            ('fixture', 'escape.tar.gz', 'escapes download cache'),
        ):
            with self.subTest(directory=directory, source=source):
                packages = {'fixture': {'dl_dir': directory, 'downloads': [{'source': source}]}}
                with self.assertRaisesRegex(ValueError, diagnostic):
                    sdk.resolved_downloads(self.cache, packages)

    def test_preserved_inventory_rejects_download_symlink_outside_cache(self):
        selected, state = self.inventory()
        outside = self.root / 'outside.tar.gz'
        selected.rename(outside)
        selected.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'escapes download cache'):
            sdk.validate_sources(self.cache, state['id'])

    def test_offline_preparation_disables_buildroot_network_transports(self):
        bootstrap = self.cache / 'bootstrap/buildroot-fixture.tar.gz'
        bootstrap.parent.mkdir()
        make_tar(bootstrap, [('buildroot-fixture/Makefile', tarfile.REGTYPE, '# fixture')])
        libc = self.cache / 'downloads/glibc/glibc-fixture.tar.gz'
        libc.parent.mkdir(parents=True)
        libc.write_bytes(b'preserved glibc source')
        manifest = {'buildroot': {'file': bootstrap.name, 'version': 'fixture',
                                 'sha256': sdk.digest(bootstrap)},
                    'glibc_source': {'file': libc.name, 'sha256': sdk.digest(libc)}}
        generated_locales = ''
        def configure(args, **kwargs):
            output = Path(next(str(arg)[2:] for arg in args if str(arg).startswith('O=')))
            output.mkdir(parents=True, exist_ok=True)
            (output / '.config').write_text('\n'.join([
                'BR2_GCC_VERSION_15_X=y', 'BR2_PACKAGE_LIBGLEW=y',
                'BR2_PACKAGE_XLIB_LIBXFT=y', 'BR2_PACKAGE_LIBOPENSSL=y',
                'BR2_TOOLCHAIN_BUILDROOT_GLIBC=y', f'BR2_GENERATE_LOCALE="{generated_locales}"',
            ]) + '\n')
        with patch.object(sdk, 'patch_buildroot'), patch.object(sdk, 'run', side_effect=configure), \
                patch.object(sdk.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            command, _ = sdk.prepare(self.cache, manifest, 'fixture-id', False, 2)
            # Newer Buildroot's host-localedef cannot generate this older libc's
            # locale data. The SDK deliberately uses destination-system locales.
            generated_locales = 'en_US.UTF-8'
            with self.assertRaisesRegex(ValueError, 'BR2_GENERATE_LOCALE'):
                sdk.prepare(self.cache, manifest, 'fixture-id', False, 2)
        for transport in ('WGET', 'GIT', 'SVN', 'HG', 'CVS', 'BZR', 'SCP', 'SFTP'):
            self.assertIn(f'BR2_{transport}=/bin/false', command)

    def test_source_archive_replays_recipe_identity_from_an_independent_checkout(self):
        original = self.root / 'original'
        recipe = original / 'third_party/build-support/source-sdk'
        shutil.copytree(sdk.RECIPE, recipe)
        helper = original / 'tools/build-sdk.py'
        helper.parent.mkdir(parents=True)
        shutil.copy2(sdk.__file__, helper)
        shutil.copy2(ROOT / 'LICENSE', original / 'LICENSE')
        source, state = self.inventory()
        unselected = source.parent / 'obsolete-source.tar.gz'
        unselected.write_bytes(b'old cached version must not inflate releases')
        bootstrap = self.cache / 'bootstrap/buildroot.tar.xz'
        bootstrap.parent.mkdir()
        bootstrap.write_bytes(b'preserved bootstrap')
        obsolete_bootstrap = bootstrap.parent / 'buildroot-previous.tar.xz'
        obsolete_bootstrap.write_bytes(b'old Buildroot release must stay in cache')
        manifest = {'source_date_epoch': 1,
                    'buildroot': {'file': bootstrap.name, 'sha256': sdk.digest(bootstrap)}}
        with patch.object(sdk, 'ROOT', original), patch.object(sdk, 'RECIPE', recipe), \
                patch.object(sdk, '__file__', str(helper)):
            identity = sdk.recipe_id()
            state['id'] = identity
            sdk.write_json(self.cache / 'sources.json', state)
            sdk.export_sources(self.cache, manifest, identity)
        archive = self.cache / 'releases' / f'datapump-sdk-sources-{identity}.tar.gz'
        sdk.check_archive(archive, None)
        replay = self.root / 'replay'
        replay.mkdir()
        sdk.safe_extract(archive, replay)
        checkout = replay / f'datapump-sdk-sources-{identity}'
        result = subprocess.run([sys.executable, checkout / 'tools/build-sdk.py', 'id'],
                                check=True, text=True, capture_output=True)
        self.assertEqual(result.stdout.strip(), identity)
        restored_cache = checkout / 'third_party/build-support/cache/source-sdk'
        with patch.object(sdk.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            sdk.validate_sources(restored_cache, identity)
        self.assertEqual((restored_cache / source.relative_to(self.cache)).read_bytes(), source.read_bytes())
        self.assertFalse((restored_cache / unselected.relative_to(self.cache)).exists())
        self.assertEqual((restored_cache / bootstrap.relative_to(self.cache)).read_bytes(), bootstrap.read_bytes())
        self.assertFalse((restored_cache / obsolete_bootstrap.relative_to(self.cache)).exists())
        (checkout / 'third_party/build-support/source-sdk/Config.in').write_text('changed recipe')
        changed = subprocess.run([sys.executable, checkout / 'tools/build-sdk.py', 'id'],
                                 check=True, text=True, capture_output=True)
        self.assertNotEqual(changed.stdout.strip(), identity)

    def test_source_export_rejects_missing_or_corrupt_bootstrap_without_downloading(self):
        self.inventory()
        bootstrap = self.cache / 'bootstrap/buildroot.tar.xz'
        bootstrap.parent.mkdir()
        manifest = {'source_date_epoch': 1,
                    'buildroot': {'file': bootstrap.name,
                                 'sha256': hashlib.sha256(b'correct bootstrap').hexdigest()}}
        with patch.object(sdk.urllib.request, 'urlopen', side_effect=AssertionError('network')):
            with self.assertRaisesRegex(ValueError, 'Missing'):
                sdk.export_sources(self.cache, manifest, 'fixture-id')
            bootstrap.write_bytes(b'corrupt bootstrap')
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                sdk.export_sources(self.cache, manifest, 'fixture-id')
        self.assertFalse((self.cache / 'releases').exists())

    def test_safe_extract_accepts_internal_symbolic_and_hard_links(self):
        archive = self.root / 'safe.tar.gz'
        make_tar(archive, [('pkg/data', tarfile.REGTYPE, 'preserved'),
                           ('pkg/symbolic', tarfile.SYMTYPE, 'data'),
                           ('pkg/hard', tarfile.LNKTYPE, 'pkg/data')])
        destination = self.root / 'restored'
        sdk.safe_extract(archive, destination)
        self.assertEqual((destination / 'pkg/symbolic').read_text(), 'preserved')
        self.assertEqual((destination / 'pkg/hard').read_text(), 'preserved')

    def test_archive_traversal_special_files_and_escaping_links_are_rejected(self):
        for entries in (
            [('../outside', tarfile.REGTYPE, 'bad')],
            [('/outside', tarfile.REGTYPE, 'bad')],
            [('pkg/../../outside', tarfile.REGTYPE, 'bad')],
            [('pkg/link', tarfile.SYMTYPE, '../../outside')],
            [('pkg/link', tarfile.SYMTYPE, '/outside')],
            [('pkg/hard', tarfile.LNKTYPE, '../outside')],
            [('pkg/fifo', tarfile.FIFOTYPE, '')],
            [('pkg/file', tarfile.REGTYPE, 'first'), ('pkg/file', tarfile.REGTYPE, 'second')],
            [('pkg/file', tarfile.REGTYPE, 'first'), ('pkg/./file', tarfile.REGTYPE, 'second')],
            [('pkg/a', tarfile.SYMTYPE, '..'), ('pkg/escape', tarfile.SYMTYPE, 'a/../outside')],
        ):
            with self.subTest(entries=entries):
                archive = self.root / 'malicious.tar.gz'
                make_tar(archive, entries)
                destination = self.root / 'restored'
                with self.assertRaises((ValueError, tarfile.TarError)):
                    sdk.safe_extract(archive, destination)
                shutil.rmtree(destination, ignore_errors=True)
        self.assertFalse((self.root / 'outside').exists())

    def test_archive_hardlink_cannot_follow_an_escaping_symlink_chain(self):
        outside = self.root / 'outside'
        outside.write_text('keep')
        archive = self.root / 'malicious.tar.gz'
        make_tar(archive, [('pkg/a', tarfile.SYMTYPE, '..'),
                           ('pkg/link', tarfile.SYMTYPE, 'a/../outside'),
                           ('pkg/hard', tarfile.LNKTYPE, 'pkg/link')])
        with self.assertRaises((ValueError, tarfile.TarError)):
            sdk.safe_extract(archive, self.root / 'restored')
        self.assertEqual(outside.read_text(), 'keep')

    def test_bootstrap_absolute_links_are_inert_and_cannot_be_written_through(self):
        archive = self.root / 'bootstrap.tar.gz'
        sentinel = self.root / 'sentinel'
        sentinel.write_text('keep')
        make_tar(archive, [('pkg/alias', tarfile.SYMTYPE, '.'),
                           ('pkg/link', tarfile.SYMTYPE, str(sentinel)),
                           ('pkg/alias/link', tarfile.REGTYPE, 'overwrite')])
        with self.assertRaises((ValueError, tarfile.TarError)):
            sdk.safe_extract(archive, self.root / 'restored', allow_absolute_links=True)
        self.assertEqual(sentinel.read_text(), 'keep')

    def sdk_archive(self, failing=False):
        source = self.root / 'prepared-sdk'
        metadata = source / 'share/datapump-sdk'
        metadata.mkdir(parents=True)
        (metadata / 'manifest.json').write_text('{"schema_version": 1}')
        (metadata / 'relocated-root.txt').write_text('/previous/build/root\n')
        relocate = source / 'relocate-sdk.sh'
        relocate.write_text('#!/bin/sh\n' + ('exit 1\n' if failing else
                            'printf relocated > "$(dirname "$0")/relocation-ran"\n'))
        relocate.chmod(0o755)
        archive = self.root / 'releases/sdk.tar.gz'
        sdk.archive_tree(archive, [(source, source.name)], 1)
        return archive

    def test_sdk_install_checks_archive_and_runs_relocation_before_marking_installed(self):
        archive = self.sdk_archive()
        destination = self.root / 'installed-sdk'
        sdk.install_sdk(archive, destination, None)
        self.assertEqual((destination / 'relocation-ran').read_text(), 'relocated')
        self.assertEqual((destination / 'share/datapump-sdk/relocated-root.txt').read_text(),
                         str(destination.resolve()) + '\n')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            sdk.install_sdk(archive, destination, None)

    def test_bad_archive_checksum_fails_without_creating_destination(self):
        archive = self.sdk_archive()
        destination = self.root / 'installed-sdk'
        archive.write_bytes(archive.read_bytes() + b'changed')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            sdk.install_sdk(archive, destination, None)
        self.assertFalse(destination.exists())

    def test_failed_relocation_removes_only_new_installation(self):
        archive = self.sdk_archive(failing=True)
        destination = self.root / 'installed-sdk'
        with self.assertRaises(subprocess.CalledProcessError):
            sdk.install_sdk(archive, destination, None)
        self.assertFalse(destination.exists())
        self.assertTrue(archive.exists())

    def test_checksum_file_must_identify_archive_exactly_once(self):
        archive = self.sdk_archive()
        sums = archive.parent / 'SHA256SUMS'
        record = sums.read_text()
        for text in ('', record + record):
            with self.subTest(text=text):
                sums.write_text(text)
                with self.assertRaisesRegex(ValueError, 'exactly once'):
                    sdk.check_archive(archive, None)
        sums.unlink()
        with self.assertRaisesRegex(ValueError, 'SHA256SUMS'):
            sdk.check_archive(archive, None)
        sdk.check_archive(archive, sdk.digest(archive))

    def test_absolute_sysroot_links_are_rewritten_but_external_host_links_fail(self):
        root = self.root / 'sdk'
        sysroot = root / 'target/sysroot'
        (sysroot / 'lib').mkdir(parents=True)
        (sysroot / 'lib/libc.so.6').write_text('library')
        link = sysroot / 'lib/libc.so'
        link.symlink_to('/lib/libc.so.6')
        (root / 'usr').symlink_to('.')  # Buildroot's host/usr alias.
        (root / 'self').symlink_to(root)
        sdk.normalized_links(root, sysroot)
        self.assertEqual((root / 'usr').resolve(), root)
        self.assertEqual(os.readlink(root / 'self'), '.')
        self.assertEqual(os.readlink(link), 'libc.so.6')
        self.assertEqual(link.read_text(), 'library')
        (root / 'escaping').symlink_to('/etc/passwd')
        with self.assertRaisesRegex(ValueError, 'external absolute link'):
            sdk.normalized_links(root, sysroot)

    def test_sdk_prunes_target_virtual_filesystems_without_dropping_development_data(self):
        root = self.root / 'sdk'
        sysroot = root / 'target/sysroot'
        for directory in ('dev', 'proc', 'sys', 'run', 'tmp', 'var/run', 'var/lock',
                          'var/tmp', 'etc', 'usr/include', 'usr/lib', 'var/cache'):
            (sysroot / directory).mkdir(parents=True)
        (sysroot / 'dev/fd').symlink_to('/proc/self/fd')
        (sysroot / 'dev/stdin').symlink_to('../proc/self/fd/0')
        os.mkfifo(sysroot / 'dev/runtime-pipe')
        (sysroot / 'etc/mtab').symlink_to('../proc/self/mounts')
        (sysroot / 'etc/resolv.conf').symlink_to('/run/resolv.conf')
        for relative in ('usr/include/fixture.h', 'usr/lib/libfixture.a',
                         'etc/fixture.conf', 'var/cache/fixture-data'):
            (sysroot / relative).write_text('preserved development/package data')
        sdk.prune_sdk_runtime_paths(root, sysroot)
        sdk.normalized_links(root, sysroot)
        for relative in ('dev', 'proc', 'sys', 'run', 'tmp', 'var/run', 'var/lock',
                         'var/tmp', 'etc/mtab', 'etc/resolv.conf'):
            self.assertFalse((sysroot / relative).exists())
            self.assertFalse((sysroot / relative).is_symlink())
        self.assertEqual((sysroot / 'usr/include/fixture.h').read_text(), 'preserved development/package data')
        self.assertTrue((sysroot / 'usr/lib/libfixture.a').is_file())
        self.assertTrue((sysroot / 'etc/fixture.conf').is_file())
        self.assertTrue((sysroot / 'var/cache/fixture-data').is_file())

    def test_runtime_pruning_never_follows_external_links_or_weakens_library_checks(self):
        root = self.root / 'sdk'
        sysroot = root / 'target/sysroot'
        (sysroot / 'usr/lib').mkdir(parents=True)
        outside = self.root / 'outside'
        outside.mkdir()
        keep = outside / 'keep'
        keep.write_text('untouched')
        (sysroot / 'dev').symlink_to(outside, target_is_directory=True)
        (sysroot / 'usr/lib/escape.so').symlink_to(keep)
        sdk.prune_sdk_runtime_paths(root, sysroot)
        self.assertEqual(keep.read_text(), 'untouched')
        self.assertFalse((sysroot / 'dev').is_symlink())
        with self.assertRaisesRegex(ValueError, 'dangling/escaping link'):
            sdk.normalized_links(root, sysroot)

    def test_elf_version_parser_separates_provider_and_consumer_requirements(self):
        output = '''Version symbols section '.gnu.version' contains 4 entries:
  000:   0 (*local*) 2 (GLIBCXX_99.0)
Version definition section '.gnu.version_d' contains 2 entries:
  0x001c: Rev: 1 Flags: none Index: 2 Cnt: 1 Name: GLIBCXX_3.4
Version needs section '.gnu.version_r' contains 2 entries:
  0x0000: Version: 1 File: libgcc_s.so.1 Cnt: 1
  0x0010: Name: GCC_3.0 Flags: none Version: 4
  0x0020: Version: 1 File: libc.so.6 Cnt: 1
  0x0030: Name: GLIBC_2.36 Flags: none Version: 5
'''
        with patch.object(sdk.subprocess, 'check_output', return_value=output):
            provided, required = sdk.elf_versions(self.root / 'fixture', self.root / 'readelf')
        self.assertEqual(provided, {'GLIBCXX_3.4'})
        self.assertEqual(required, {'libgcc_s.so.1': {'GCC_3.0'}, 'libc.so.6': {'GLIBC_2.36'}})

    def test_host_runtime_rejects_newer_host_abi_before_copying_any_files(self):
        root = self.root / 'sdk'
        sysroot = root / 'target/sysroot'
        (sysroot / 'usr/lib').mkdir(parents=True)
        (root / 'bin').mkdir()
        (root / 'bin/patchelf').write_text('fixture')
        (root / 'bin/target-readelf').write_text('fixture')
        elf = bytearray(20)
        elf[:4], elf[4], elf[5] = b'\x7fELF', 2, 1
        elf[16:18], elf[18:20] = (3).to_bytes(2, 'little'), (62).to_bytes(2, 'little')
        for name in ('libstdc++.so.6', 'libgcc_s.so.1'):
            (sysroot / 'usr/lib' / name).write_bytes(elf)
        consumer = root / 'bin/consumer'
        consumer.write_bytes(elf)
        def inspect(args, **kwargs):
            return str(args[-1].name) + '\n' if '--print-soname' in args else 'libstdc++.so.6\nlibgcc_s.so.1\n'
        for library, unavailable in (('libstdc++.so.6', 'GLIBCXX_99.0'),
                                     ('libstdc++.so.6', 'CXXABI_99.0'),
                                     ('libgcc_s.so.1', 'GCC_99.0')):
            with self.subTest(library=library, unavailable=unavailable):
                def versions(path, readelf):
                    return (set(), {library: {unavailable}}) if path == consumer else ({'GLIBCXX_3.4', 'CXXABI_1.3', 'GCC_3.0'}, {})
                with patch.object(sdk.subprocess, 'check_output', side_effect=inspect), \
                        patch.object(sdk, 'elf_versions', side_effect=versions), \
                        patch.object(sdk.platform, 'system', return_value='Linux'), \
                        patch.object(sdk.platform, 'machine', return_value='x86_64'):
                    with self.assertRaisesRegex(ValueError, unavailable.replace('.', r'\.')):
                        sdk.bundle_host_cxx_runtime(root, sysroot, 'x86_64')
                self.assertFalse((root / 'lib').exists())

    def test_host_runtime_rejects_other_architectures(self):
        with self.assertRaisesRegex(ValueError, 'x86_64 Linux host and target'):
            sdk.bundle_host_cxx_runtime(self.root / 'sdk', self.root / 'sdk/target/sysroot', 'aarch64')

    def test_glibc_audit_separates_host_and_target_and_checks_compiler_output(self):
        root = self.root / 'sdk'
        metadata = root / 'share/datapump-sdk'
        metadata.mkdir(parents=True)
        (root / 'bin').mkdir()
        sysroot = root / 'target/sysroot'
        (sysroot / 'lib').mkdir(parents=True)
        target_libc = sysroot / 'lib/libc.so.6'
        host_compiler = root / 'bin/target-g++'
        for path in (target_libc, host_compiler):
            path.write_bytes(b'\x7fELFfixture')
        (root / 'bin/target-readelf').write_text('readelf fixture')
        sdk.write_json(metadata / 'manifest.json', {
            'baseline': {'glibc': '2.36'}, 'target': {'triple': 'target',
                'sysroot': 'target/sysroot', 'cxx_compiler': 'bin/target-g++'}})
        (metadata / 'relocated-root.txt').write_text(str(root) + '\n')
        floors = {str(target_libc): '2.36', str(host_compiler): '2.36', 'probe': '2.36'}
        def readelf(args, **kwargs):
            path = str(args[-1])
            return subprocess.CompletedProcess(args, 0,
                stdout=f'Name: GLIBC_{floors.get(path, floors["probe"])} GLIBC_PRIVATE\n')
        def compile_probe(args, **kwargs):
            if '-o' in args:
                Path(args[args.index('-o') + 1]).write_bytes(b'\x7fELFprobe')
        with patch.object(sdk.subprocess, 'run', side_effect=readelf), \
                patch.object(sdk, 'run', side_effect=compile_probe):
            self.assertEqual(sdk.verify_sdk(root, '2.36'), {'host': (2, 36), 'target': (2, 36)})
            floors[str(target_libc)] = '2.37'
            with self.assertRaisesRegex(ValueError, 'Target ABI'):
                sdk.verify_sdk(root, '2.36')
            floors[str(target_libc)] = '2.36'
            floors[str(host_compiler)] = '2.37'
            with self.assertRaisesRegex(ValueError, 'host tools require'):
                sdk.verify_sdk(root, '2.36')
            floors[str(host_compiler)] = '2.36'
            floors['probe'] = '2.37'
            with self.assertRaisesRegex(ValueError, 'Compiler probe exceeded'):
                sdk.verify_sdk(root, '2.36')


HOST_TEST_TOOLS = {
    'cxx': os.environ.get('DATAPUMP_TEST_CXX_COMPILER') or shutil.which('c++'),
    'readelf': os.environ.get('DATAPUMP_TEST_READELF') or shutil.which('readelf'),
    'patchelf': os.environ.get('DATAPUMP_TEST_PATCHELF') or shutil.which('patchelf'),
}


@unittest.skipUnless(platform.system() == 'Linux' and platform.machine() == 'x86_64'
                     and all(HOST_TEST_TOOLS.values()),
                     'Host runtime ELF fixtures require x86_64 Linux, C++, readelf and patchelf')
class HostCxxRuntimeIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='datapump-host-runtime-')
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.root = self.work / 'sdk'
        self.sysroot = self.root / 'target/sysroot'
        self.libs = self.sysroot / 'usr/lib'
        self.libs.mkdir(parents=True)
        (self.root / 'bin').mkdir()
        shutil.copy2(HOST_TEST_TOOLS['patchelf'], self.root / 'bin/patchelf')
        # Keep the native inspection helper's unrelated distro dependencies out
        # of this tiny SDK fixture; real exported SDKs contain their own readelf.
        readelf = self.root / 'bin/target-readelf'
        readelf.write_text('#!/bin/sh\nexec ' + shlex.quote(HOST_TEST_TOOLS['readelf']) + ' "$@"\n')
        readelf.chmod(0o755)
        self.source_hashes = {}
        for name in ('libstdc++.so.6', 'libgcc_s.so.1'):
            actual = Path(self.command([HOST_TEST_TOOLS['cxx'], f'-print-file-name={name}']).stdout.strip()).resolve()
            self.assertTrue(actual.is_file(), f'Compiler did not provide {name}')
            target = self.libs / actual.name
            shutil.copy2(actual, target)
            if target.name != name:
                (self.libs / name).symlink_to(target.name)
            self.source_hashes[target] = sdk.digest(target)

    def command(self, args, **kwargs):
        return subprocess.run([str(arg) for arg in args], check=True, text=True,
                              capture_output=True, **kwargs)

    def test_host_tools_and_deep_plugins_use_private_cxx_runtime_after_relocation(self):
        plugin_dir = self.root / 'lib/gcc/target/15/plugin'
        plugin_dir.mkdir(parents=True)
        plugin = plugin_dir / 'fixture.so'
        source = self.work / 'plugin.cpp'
        source.write_text('#include <stdexcept>\nextern "C" int fixture_value() {\n'
                          'try { throw std::runtime_error("fixture"); } catch (...) { return 73; } }\n')
        self.command([HOST_TEST_TOOLS['cxx'], '-shared', '-fPIC', source, '-o', plugin])
        source = self.work / 'main.cpp'
        source.write_text('#include <string>\n#include <dlfcn.h>\nint main(int argc, char**argv) {\n'
                          'if (argc != 2) return 1; std::string path(argv[1]);\n'
                          'void *p=dlopen(path.c_str(), RTLD_NOW); if (!p) return 2;\n'
                          'auto f=reinterpret_cast<int(*)()>(dlsym(p,"fixture_value"));\n'
                          'return f && f()==73 ? 0 : 3; }\n')
        (self.root / 'aux').mkdir()
        self.command([HOST_TEST_TOOLS['cxx'], source, '-ldl', '-Wl,-rpath,$ORIGIN/../aux',
                      '-o', self.root / 'bin/consumer'])
        result = sdk.bundle_host_cxx_runtime(self.root, self.sysroot, 'x86_64')
        self.assertEqual(set(result), {'lib/libstdc++.so.6', 'lib/libgcc_s.so.1'})
        for name in ('libstdc++.so.6', 'libgcc_s.so.1'):
            copied = self.root / 'lib' / name
            self.assertTrue(copied.is_file())
            self.assertFalse(copied.is_symlink())
            self.assertEqual(self.command([self.root / 'bin/patchelf', '--print-rpath', copied]).stdout.strip(), '$ORIGIN')
        self.assertFalse((self.root / 'lib/libc.so.6').exists())
        self.assertIn('$ORIGIN/../aux', self.command([
            self.root / 'bin/patchelf', '--print-rpath', self.root / 'bin/consumer']).stdout)
        for path, original_hash in self.source_hashes.items():
            self.assertEqual(sdk.digest(path), original_hash)
        relocated = self.work / 'relocated-sdk'
        self.root.rename(relocated)
        shutil.rmtree(relocated / 'target')
        environment = dict(os.environ, LD_LIBRARY_PATH='', LD_PRELOAD='')
        self.command([relocated / 'bin/consumer', relocated / plugin.relative_to(self.root)], env=environment)
        for consumer in (relocated / 'bin/consumer', relocated / 'bin/patchelf'):
            trace = self.command([consumer], env=dict(environment, LD_TRACE_LOADED_OBJECTS='1')).stdout
            for name in ('libstdc++.so.6', 'libgcc_s.so.1'):
                self.assertRegex(trace, rf'{re.escape(name)} => {re.escape(str(relocated))}/[^\n]+')

    def test_missing_target_runtime_fails_before_host_libraries_are_installed(self):
        (self.libs / 'libstdc++.so.6').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing target C\\+\\+ runtime'):
            sdk.bundle_host_cxx_runtime(self.root, self.sysroot, 'x86_64')
        self.assertFalse((self.root / 'lib/libgcc_s.so.1').exists())

    def test_unpreserved_host_library_is_rejected_before_copying_runtimes(self):
        source = self.work / 'foreign.cpp'
        source.write_text('extern "C" int foreign_value() { return 0; }\n')
        library = self.work / 'libdatapump_foreign.so.1'
        self.command([HOST_TEST_TOOLS['cxx'], '-shared', '-fPIC', source,
                      '-Wl,-soname,libdatapump_foreign.so.1', '-o', library])
        source = self.work / 'consumer.cpp'
        source.write_text('extern "C" int foreign_value();\nint main() { return foreign_value(); }\n')
        self.command([HOST_TEST_TOOLS['cxx'], source, library, '-o', self.root / 'bin/consumer'])
        with self.assertRaisesRegex(ValueError, 'libdatapump_foreign.*absent from SDK host/lib'):
            sdk.bundle_host_cxx_runtime(self.root, self.sysroot, 'x86_64')
        self.assertFalse((self.root / 'lib/libgcc_s.so.1').exists())


if __name__ == '__main__':
    unittest.main()

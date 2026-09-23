#!/usr/bin/env python3
"""Recipe generation, source identity, binary install phases and tamper regressions."""
from datetime import datetime, timezone
import gzip
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('distro', ROOT / 'tools/distro-release.py')
distro = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(distro)
apt = distro.apt_module()
release = apt.release_module()


def archive(path, metadata, target):
    backend = release.target_backend(metadata, target)
    root = release.package_bases(metadata, target)[0]
    files = {'bin/pump': (b'#!/bin/sh\nprintf "fixture\\n"\n', 0o755),
             'bin/datapump-gui': (b'#!/bin/sh\nexit 0\n', 0o755),
             'lib/example.so': (b'private binary library\x00', 0o644),
             'share/doc/datapump/LICENSE': (b'fixture license: terms retained\n', 0o444),
             'share/doc/datapump/third_party/rev/README.datapump.md': (b'Provenance is not a new license grant\n', 0o644),
             'share/doc/datapump/build-info.txt': (f'GUI: ON ({backend})\n'.encode(), 0o644)}
    sums = ''.join(f'{hashlib.sha256(data).hexdigest()}  {name}\n' for name, (data, _) in files.items())
    files['manifest.sha256'] = (sums.encode(), 0o644)
    with tarfile.open(path, 'w:gz') as stream:
        for name, (data, mode) in files.items():
            member = tarfile.TarInfo(root + '/' + name)
            member.size, member.mode = len(data), mode
            stream.addfile(member, io.BytesIO(data))


class DistroReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='distro-release-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.assets = self.root / 'release'
        self.assets.mkdir()
        self.repository = 'owner/project'
        self.metadata = release.make_metadata(source_sha='a' * 40, run_id='123', run_attempt='2',
            cmake_version='0.7.2', experiment=True, schema=4, version='v001_00',
            now=datetime(2026, 9, 23, 12, 0, tzinfo=timezone.utc))
        for target, name in release.application_names(self.metadata).items():
            if target.startswith('linux-'):
                archive(self.assets / name, self.metadata, target)
        self.manifest = distro.build(self.assets, self.metadata, self.repository)

    def test_both_recipe_formats_bind_all_four_sources_and_preserve_notices(self):
        self.assertEqual(distro.asset_names(self.metadata), {
            'datapump-arch-recipes.tar.gz', 'datapump-gentoo-overlay.tar.gz', 'distro-packages.json'})
        self.assertEqual(set(self.manifest['archives']), {f'linux-{arch}-{backend}'
                         for arch in distro.ARCHES for backend in distro.BACKENDS})
        for row in self.manifest['archives'].values():
            self.assertIn('/releases/download/' + self.metadata['tag'] + '/', row['url'])
            self.assertEqual(row['sha256'], apt.sha256(self.assets / row['archive']))
        for kind in distro.ROOTS:
            root = distro.extract(self.assets, self.metadata, kind, self.root / kind)
            self.assertTrue((root / 'README.md').is_file())
        license_text = (self.root / 'gentoo/datapump-gentoo-overlay/licenses/DataPump-Bundled').read_text()
        self.assertIn('fixture license: terms retained', license_text)
        self.assertIn('Provenance is not a new license grant', license_text)
        self.assertIn('grants no additional rights', license_text)

    def test_versions_are_legal_and_sort_across_central_daylight_transition(self):
        self.assertEqual(distro.distro_version(self.metadata), '0.7.2_p20260923120000_p123_p2')
        before = dict(self.metadata, created_at='2026-11-01T06:59:00Z')
        after = dict(self.metadata, created_at='2026-11-01T07:00:00Z')
        self.assertLess(distro.distro_version(before), distro.distro_version(after))
        with self.assertRaisesRegex(ValueError, '18 digits'):
            distro.distro_version(dict(self.metadata, run_id='1' * 19))

    def test_recipes_have_native_arch_dependencies_no_build_and_no_strip(self):
        trees, _ = distro.expected(self.assets, self.metadata, self.repository)
        for backend in distro.BACKENDS:
            arch = trees['arch'][f'datapump-{backend}-bin/PKGBUILD'][0].decode()
            srcinfo = trees['arch'][f'datapump-{backend}-bin/.SRCINFO'][0].decode()
            ebuild = next(data.decode() for name, (data, _) in trees['gentoo'].items()
                          if f'datapump-{backend}-bin/' in name and name.endswith('.ebuild'))
            for script in (arch, ebuild):
                subprocess.run(['bash', '-n'], input=script, text=True, check=True)
                self.assertNotIn('cmake', script)
                self.assertNotIn('ninja', script)
            self.assertIn("'!strip'", arch)
            self.assertIn("'!purge'", arch)
            self.assertIn('depends_x86_64 = glibc>=2.36', srcinfo)
            self.assertIn('depends_aarch64 = glibc>=2.35', srcinfo)
            self.assertIn('RESTRICT="strip mirror"', ebuild)
            self.assertIn('src_compile() { :; }', ebuild)
            self.assertIn('media-libs/libglvnd[X]', ebuild)
            self.assertIn('>=sys-libs/glibc-2.36', ebuild)
            manifest = trees['gentoo'][f'media-radio/datapump-{backend}-bin/Manifest'][0].decode()
            for arch_name in distro.ARCHES:
                row = self.manifest['archives'][f'linux-{arch_name}-{backend}']
                self.assertIn('SHA256 ' + row['sha256'], manifest)
                self.assertIn('SHA512 ' + row['sha512'], manifest)

    def execute_recipe(self, kind, backend, architecture):
        destination = self.root / f'{kind}-{backend}-{architecture}'
        tree = distro.extract(self.assets, self.metadata, kind, destination / 'recipes')
        work, installed = destination / 'source with spaces', destination / 'installed'
        work.mkdir()
        installed.mkdir()
        target = f'linux-{architecture}-{backend}'
        with tarfile.open(self.assets / release.application_names(self.metadata)[target]) as fixture:
            # These members were generated by this test and are all regular files.
            for member in fixture:
                path = work / member.name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(fixture.extractfile(member).read())
                path.chmod(member.mode)
        env = dict(os.environ, srcdir=str(work), pkgdir=str(installed), CARCH=architecture,
                   WORKDIR=str(work), ED=str(installed), ARCH=distro.ARCHES[architecture])
        if kind == 'arch':
            package = tree / f'datapump-{backend}-bin'
            for path in package.iterdir():
                if path.name not in ('PKGBUILD', '.SRCINFO'):
                    shutil.copyfile(path, work / path.name)
            command = 'set -eu\nsource "$1"\npackage\n'
            recipe = package / 'PKGBUILD'
        else:
            package = tree / 'media-radio' / f'datapump-{backend}-bin'
            env['FILESDIR'] = str(package / 'files')
            recipe = next(package.glob('*.ebuild'))
            command = '''set -eu
            die() { echo "$*" >&2; exit 1; }
            dodir() { install -dm755 "$ED/$1"; }
            dobin() { for source in "$@"; do install -Dm755 "$source" "$ED/usr/bin/$(basename "$source")"; done; }
            insinto() { install_dest="$1"; }
            doins() { for source in "$@"; do install -Dm644 "$source" "$ED/$install_dest/$(basename "$source")"; done; }
            docompress() { :; }
            source "$1"
            src_prepare
            src_configure
            src_compile
            src_install
            '''
        subprocess.run(['bash', '-c', command, 'recipe-test', str(recipe)], env=env, check=True)
        distro.verify_installed(self.assets, self.metadata, backend, architecture, installed)
        return installed

    def test_arch_and_gentoo_install_phases_preserve_bytes_and_modes_for_both_arches(self):
        for kind in distro.ROOTS:
            for backend in distro.BACKENDS:
                for architecture in distro.ARCHES:
                    with self.subTest(kind=kind, backend=backend, arch=architecture):
                        self.execute_recipe(kind, backend, architecture)

    def test_tampered_bundle_manifest_or_recipe_is_rejected(self):
        for filename in ('distro-packages.json', 'datapump-arch-recipes.tar.gz', 'datapump-gentoo-overlay.tar.gz'):
            path = self.assets / filename
            original = path.read_bytes()
            if filename.endswith('.json'):
                modified = json.loads(original)
                modified['tag'] = 'changed'
                path.write_text(json.dumps(modified))
            else:
                raw = gzip.decompress(original).replace(b'makepkg', b'evilcmd') if 'arch' in filename else gzip.decompress(original).replace(b'EAPI=8', b'EAPI=7')
                path.write_bytes(gzip.compress(raw, mtime=0))
            with self.subTest(name=filename), self.assertRaises(ValueError):
                distro.verify(self.assets, self.metadata, self.repository)
            path.write_bytes(original)
        archive_path = self.assets / next(iter(self.manifest['archives'].values()))['archive']
        archive_path.write_bytes(b'corruption')
        with self.assertRaises((ValueError, tarfile.TarError)):
            distro.verify(self.assets, self.metadata, self.repository)

    def test_foreign_compression_is_accepted_but_extra_tar_entry_cannot_extract(self):
        path = self.assets / 'datapump-arch-recipes.tar.gz'
        raw = gzip.decompress(path.read_bytes())
        path.write_bytes(gzip.compress(raw, compresslevel=1, mtime=0))
        distro.verify(self.assets, self.metadata, self.repository)
        buffer = io.BytesIO(raw)
        with tarfile.open(fileobj=buffer, mode='a') as archive:
            member = tarfile.TarInfo('../../escape')
            member.size = 1
            archive.addfile(member, io.BytesIO(b'x'))
        path.write_bytes(gzip.compress(buffer.getvalue(), mtime=0))
        with self.assertRaises(ValueError):
            distro.extract(self.assets, self.metadata, 'arch', self.root / 'must-not-exist')
        self.assertFalse((self.root / 'must-not-exist').exists())

    def test_replaced_installed_binary_and_extras_are_rejected(self):
        installed = self.execute_recipe('arch', 'fltk', 'x86_64')
        binary = installed / 'opt/datapump/fltk/bin/pump'
        original = binary.read_bytes()
        binary.write_bytes(b'stripped or changed')
        with self.assertRaisesRegex(ValueError, 'payload differs'):
            distro.verify_installed(self.assets, self.metadata, 'fltk', 'amd64', installed)
        binary.write_bytes(original)
        binary.chmod(0o600)
        with self.assertRaisesRegex(ValueError, 'payload differs'):
            distro.verify_installed(self.assets, self.metadata, 'fltk', 'amd64', installed)
        binary.chmod(0o755)
        (installed / 'opt/datapump/fltk/extra').write_text('unlisted')
        with self.assertRaisesRegex(ValueError, 'unexpected or missing'):
            distro.verify_installed(self.assets, self.metadata, 'fltk', 'amd64', installed)

    def test_existing_outputs_and_symlink_assets_are_rejected(self):
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            distro.build(self.assets, self.metadata, self.repository)
        directory = self.root / 'existing'
        directory.mkdir()
        with self.assertRaisesRegex(ValueError, 'overwrite'):
            distro.extract(self.assets, self.metadata, 'arch', directory)
        path = self.assets / 'datapump-arch-recipes.tar.gz'
        original = self.root / 'original'
        path.rename(original)
        path.symlink_to(original)
        with self.assertRaisesRegex(ValueError, 'unsafe'):
            distro.verify(self.assets, self.metadata)


if __name__ == '__main__':
    unittest.main()

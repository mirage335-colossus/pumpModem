#!/usr/bin/env python3
"""Real archive/manifest tests for independently produced browser delivery."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from web_delivery_fixture import payload


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release = load('release')
web = load('release-web')


def metadata():
    return release.make_metadata(source_sha='a' * 40, run_id=42, run_attempt=1,
                                 frontends=['tui', 'framebuffer'], web=web.CAPABILITY)


def browser(values):
    # Production page assembler and notice inventory; only compiled bytes are fixtures.
    files = dict(payload(values))
    files = {k: v for k, v in files.items() if k.startswith((web.WEB, web.DOC + 'wasm-'))}
    return files


def native(values, target):
    windows = target.startswith('windows-')
    backend = target.rsplit('-', 1)[1]
    files = {'bin/pump' + ('.exe' if windows else ''): (b'fixture executable', 0o755)}
    for name in ('datapump-tui', 'datapump-fb'):
        files['bin/' + name + ('.exe' if windows else '')] = (b'fixture frontend', 0o755)
        files['share/man/man1/' + name + '.1'] = (b'manual', 0o644)
    if not windows:
        files.update({k: v for k, v in payload(values).items() if not k.startswith((web.WEB, web.DOC + 'wasm-'))})
    files[web.DOC + 'build-info.txt'] = ((f'Source commit: {values["source_sha"]}\nGUI: ON ({backend})\nTUI: ON ({"winconsole" if windows else "ncurses"})\n'
        'Framebuffer: ON (sdl)\n' + ('Web worker: ON (inherited-pipes; no sockets)\n' if not windows else '')).encode(), 0o644)
    files['manifest.sha256'] = (web.manifest(files), 0o644)
    return files


def producer(directory, values):
    directory.mkdir()
    files = browser(values)
    files[web.DOC + 'build-info.txt'] = files.pop(web.DOC + 'wasm-build-info.txt')
    for suffix in ('.tar.gz', '.zip'):
        web.write_archive(directory / ('DataPump-wasm' + suffix), 'DataPump-wasm', files)
    (directory / 'SHA256SUMS.txt').write_text(''.join(release.digest(p) + '  ' + p.name + '\n' for p in sorted(directory.iterdir())))
    return files


class BrowserDelivery(unittest.TestCase):
    def setUp(self):
        self.values = metadata()
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def test_metadata_requires_wasm_dependency_and_exact_capability(self):
        p = self.root / 'metadata.json'
        release.write_json(p, self.values)
        self.assertEqual(release.load_metadata(p), self.values)
        for capability in ({}, True, {'schema': 1, 'browser': 'wasm'}, dict(web.CAPABILITY, schema=True)):
            with self.subTest(capability=capability), self.assertRaises(ValueError):
                release.make_metadata(source_sha='a' * 40, run_id=1, run_attempt=1, web=capability)
        deps = dict(self.values['dependencies'])
        deps.pop('wasm-sdk')
        with self.assertRaisesRegex(ValueError, 'declared together'):
            release.make_metadata(source_sha='a' * 40, run_id=1, run_attempt=1, web=web.CAPABILITY, dependencies=deps)
        with self.assertRaisesRegex(ValueError, 'declared together'):
            release.make_metadata(source_sha='a' * 40, run_id=1, run_attempt=1, dependencies=self.values['dependencies'])

    def test_browser_pair_and_native_bundle_all_platforms_and_formats(self):
        directory = self.root / 'browser'
        producer(directory, self.values)
        browser_files = web.browser_payload(directory, self.values)
        for target in release.application_targets(self.values):
            for suffix in ('.tar.gz', '.zip'):
                with self.subTest(target=target, suffix=suffix):
                    base = release.package_bases(self.values, target)[0]
                    source = self.root / ('source' + suffix)
                    output = self.root / ('output' + suffix)
                    original = native(self.values, target)
                    web.write_archive(source, base, original)
                    web.bundle(source, output, browser_files)
                    release.verify_archive_backend(output, self.values, target)
                    _, actual = web.archive_files(output)
                    for name, value in original.items():
                        if name != 'manifest.sha256':
                            self.assertEqual(actual[name], value)

    def test_corrupt_page_or_missing_notice_fails_even_with_updated_outer_manifest(self):
        for path, replacement in ((web.WEB + 'datapump-wasm.html', b'changed'),
                                  (web.DOC + 'wasm-runtime-notices/OpenSSL-LICENSE.txt', None),
                                  (web.DOC + 'wasm-runtime-notices/OpenSSL-LICENSE.txt', b'wrong notice')):
            files = native(self.values, 'linux-x86_64-fltk') | browser(self.values)
            if replacement is None:
                del files[path]
            else:
                files[path] = (replacement, 0o644)
            files['manifest.sha256'] = (web.manifest(files), 0o644)
            archive = self.root / 'bad.zip'
            web.write_archive(archive, release.package_bases(self.values, 'linux-x86_64-fltk')[0], files)
            with self.subTest(path=path, replacement=replacement), self.assertRaises(ValueError):
                release.verify_archive_backend(archive, self.values, 'linux-x86_64-fltk')

    def test_source_and_sdk_mismatch_are_rejected(self):
        for kind in ('source_sha', 'sdk'):
            directory = self.root / kind
            producer(directory, self.values)
            wrong = dict(self.values)
            if kind == 'source_sha':
                wrong['source_sha'] = 'b' * 40
            else:
                wrong['dependencies'] = dict(wrong['dependencies'], **{'wasm-sdk': 'f' * 20})
            with self.assertRaisesRegex(ValueError, 'differs from release'):
                web.browser_payload(directory, wrong)

    def test_native_source_mismatch_is_rejected_independently_of_browser(self):
        for target in ('linux-x86_64-fltk', 'windows-x86_64-rev'):
            wrong = dict(self.values, source_sha='b' * 40)
            files = native(wrong, target) | browser(self.values)
            files['manifest.sha256'] = (web.manifest(files), 0o644)
            archive = self.root / 'wrong-source.zip'
            web.write_archive(archive, release.package_bases(self.values, target)[0], files)
            with self.assertRaisesRegex(ValueError, 'Native producer source'):
                release.verify_archive_backend(archive, self.values, target)

    def test_worker_required_only_on_linux(self):
        for target in ('linux-x86_64-fltk', 'windows-x86_64-rev'):
            files = native(self.values, target) | browser(self.values)
            if target.startswith('linux'):
                del files['bin/datapump-worker']
            else:
                files['bin/datapump-worker.exe'] = (b'unqualified', 0o755)
            files['manifest.sha256'] = (web.manifest(files), 0o644)
            archive = self.root / 'worker.zip'
            web.write_archive(archive, release.package_bases(self.values, target)[0], files)
            with self.assertRaises(ValueError):
                web.verify_native(archive, self.values, target)

    def test_modified_native_input_and_browser_replacement_are_rejected(self):
        source, dest = self.root / 'native.zip', self.root / 'output.zip'
        files = native(self.values, 'linux-x86_64-fltk')
        files['bin/pump'] = (b'changed', 0o755)
        web.write_archive(source, 'DataPump', files)
        with self.assertRaisesRegex(ValueError, 'manifest'):
            web.bundle(source, dest, browser(self.values))
        files.update(browser(self.values))
        files['manifest.sha256'] = (web.manifest(files), 0o644)
        web.write_archive(source, 'DataPump', files)
        with self.assertRaisesRegex(ValueError, 'Refusing to replace'):
            web.bundle(source, dest, browser(self.values))

    def test_different_browser_archive_pair_rejected(self):
        directory = self.root / 'browser'
        files = producer(directory, self.values)
        files[web.DOC + 'wasm-sdk-recipe.json'] = (b'changed recipe', 0o644)
        web.write_archive(directory / 'DataPump-wasm.zip', 'DataPump-wasm', files)
        (directory / 'SHA256SUMS.txt').write_text(''.join(release.digest(p) + '  ' + p.name + '\n'
            for p in sorted(directory.iterdir()) if p.name != 'SHA256SUMS.txt'))
        with self.assertRaisesRegex(ValueError, 'recipe document'):
            web.browser_payload(directory, self.values)

    def test_unsafe_duplicate_and_linked_archive_members_rejected(self):
        import io, tarfile
        for names in (['root/../escape'], ['root/file', 'root/file']):
            p = self.root / 'unsafe.tar.gz'
            with tarfile.open(p, 'w:gz') as archive:
                for name in names:
                    item = tarfile.TarInfo(name); item.size = 1
                    archive.addfile(item, io.BytesIO(b'x'))
            with self.assertRaises(ValueError):
                web.archive_files(p)
        p = self.root / 'link.tar.gz'
        with tarfile.open(p, 'w:gz') as archive:
            item = tarfile.TarInfo('root/link'); item.type = tarfile.SYMTYPE; item.linkname = '/etc/passwd'
            archive.addfile(item)
        with self.assertRaises(ValueError):
            web.archive_files(p)

    def test_finalizer_publishes_combined_native_assets_once(self):
        meta = self.root / 'metadata.json'; release.write_json(meta, self.values)
        directory = self.root / 'browser'; producer(directory, self.values)
        artifacts = self.root / 'native'; artifacts.mkdir()
        for target in release.application_targets(self.values):
            dest = artifacts / target; dest.mkdir(); base = release.package_bases(self.values, target)[0]
            for suffix in ('.tar.gz', '.zip'):
                web.write_archive(dest / (base + suffix), base, native(self.values, target))
            (dest / 'SHA256SUMS.txt').write_text(''.join(release.digest(p) + '  ' + p.name + '\n' for p in sorted(dest.iterdir())))
        def download(metadata, repository, staged, assets, expected):
            release.write_json(staged / 'release-metadata.json', metadata)
            (staged / 'release-notes.md').write_text(release.CERTIFICATION_PENDING)
            release.write_warning(metadata, staged)
        def verify(staged):
            for target, name in release.application_names(self.values).items():
                release.verify_archive_backend(staged / name, self.values, target)
        with patch.object(release, 'draft_info', return_value=dict.fromkeys(release.support_files(self.values))), \
             patch.object(release, 'download_assets', side_effect=download), \
             patch.object(release, 'preserve_dependencies'), patch.object(release, 'build_delivery_assets'), \
             patch.object(release, 'verify_release', side_effect=verify), patch.object(release, 'gh') as gh:
            release.finalize(meta, 'test/repo', self.root / 'final', artifacts=artifacts, wasm_artifacts=directory)
            self.assertEqual(gh.call_count, 1)
            args = gh.call_args.args[0]
            self.assertNotIn('--clobber', args)
            for name in release.application_names(self.values).values():
                self.assertIn(str(self.root / 'final' / name), args)


if __name__ == '__main__':
    unittest.main()

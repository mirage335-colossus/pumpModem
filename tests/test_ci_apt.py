#!/usr/bin/env python3
"""Offline container APT recovery and mirror policy fixtures (no system writes)."""
import json
import functools
import http.server
import threading
import os
from pathlib import Path
import shutil
import signal
import time
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HELPER = ROOT / 'tools/ci-apt.sh'


class CiAptTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='datapump-ci-apt-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.apt = self.root / 'etc/apt'
        (self.apt / 'sources.list.d').mkdir(parents=True)
        self.bin = self.root / 'bin'
        self.bin.mkdir()
        self.env = dict(os.environ, CI_APT_ROOT=str(self.root),
                        PATH=str(self.bin) + os.pathsep + os.environ['PATH'],
                        TEST_APT_ROOT=str(self.root), TEST_APT_ARCH='amd64')
        self.executable('dpkg', '#!/bin/sh\nprintf "%s\\n" "$TEST_APT_ARCH"\n')
        self.executable('sleep', '#!/bin/sh\nprintf "%s\\n" "$1" >> "$TEST_APT_ROOT/sleeps"\n')
        self.executable('apt-get', f'''#!{sys.executable}
import json, os, pathlib, sys
root = pathlib.Path(os.environ['TEST_APT_ROOT'])
with (root / 'calls').open('a') as stream:
    stream.write(json.dumps(sys.argv[1:]) + '\\n')
steps = json.loads((root / 'steps.json').read_text())
if not steps:
    print('Unexpected extra APT call', file=sys.stderr)
    sys.exit(97)
step = steps.pop(0)
(root / 'steps.json').write_text(json.dumps(steps))
command = 'install' if 'install' in sys.argv else 'update'
if command != step['command']:
    print('Unexpected APT command: ' + command, file=sys.stderr)
    sys.exit(98)
print(step.get('output', ''), flush=True)
sys.exit(step.get('status', 0))
''')
        self.distro('ubuntu', 'noble')

    def executable(self, name, text):
        path = self.bin / name
        path.write_text(text)
        path.chmod(0o755)

    def distro(self, name, codename, arch='amd64'):
        (self.root / 'etc/os-release').write_text(f'ID={name}\nVERSION_CODENAME={codename}\n')
        self.env['TEST_APT_ARCH'] = arch

    def source(self, text, name='sources.list'):
        path = self.apt / name
        path.write_text(text)
        return path

    def run_helper(self, *args, steps=(), expected=0, extra_env=None):
        (self.root / 'steps.json').write_text(json.dumps(list(steps)))
        environment = dict(self.env, **(extra_env or {}))
        result = subprocess.run(['sh', str(HELPER), *args], env=environment,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, expected, result.stdout + result.stderr)
        self.assertEqual(json.loads((self.root / 'steps.json').read_text()), [])
        return result

    def calls(self):
        return [json.loads(line) for line in (self.root / 'calls').read_text().splitlines()]

    @staticmethod
    def step(command, output='', status=0):
        return dict(command=command, output=output, status=status)

    def enable_timeout(self, *, mode='normal', gnu=True):
        self.env['TEST_APT_TIMEOUT_MODE'] = mode
        self.env['TEST_APT_TIMEOUT_GNU'] = '1' if gnu else '0'
        script = """#!%s
import json, os, subprocess, sys
if sys.argv[1:] == ['--version']:
    print('timeout (GNU coreutils) 9.1' if os.environ['TEST_APT_TIMEOUT_GNU'] == '1' else 'custom timeout')
    sys.exit(0)
if sys.argv[1:] == ['--help']:
    print(' --verbose')
    sys.exit(0)
args = sys.argv[1:]
while args and args[0].startswith('--'):
    args = args[1:]
limit = args.pop(0)
with open(os.path.join(os.environ['TEST_APT_ROOT'], 'timeout-calls'), 'a') as stream:
    stream.write(json.dumps([limit] + args) + '\\n')
result = subprocess.run(args, capture_output=True, text=True)
sys.stdout.write(result.stdout)
sys.stderr.write(result.stderr)
if os.environ.get('TEST_APT_TIMEOUT_MODE') in ('expire', 'kill'):
    print('timeout: sending signal TERM to command', file=sys.stderr)
    sys.exit(137 if os.environ['TEST_APT_TIMEOUT_MODE'] == 'kill' else 124)
sys.exit(result.returncode)
""" % sys.executable
        self.executable('timeout', script)

    def prepare_real_watchdog(self, resistant_worker=False):
        timeout = shutil.which('timeout')
        if not timeout or not Path('/proc').is_dir():
            self.skipTest('GNU timeout and Linux process observation required')
        if 'GNU coreutils' not in subprocess.check_output([timeout, '--version'], text=True):
            self.skipTest('GNU timeout required')
        self.env['CI_APT_DOWNLOAD_TIMEOUT'] = '1'
        self.executable('apt-get', f"""#!{sys.executable}
import json,os,pathlib,subprocess,sys,time
if 'update' in sys.argv: sys.exit(0)
if '--no-download' in sys.argv: sys.exit(0)
child=subprocess.Popen([sys.executable,'-c',{'import signal,time; signal.signal(signal.SIGTERM, signal.SIG_IGN); time.sleep(60)' if resistant_worker else 'import time; time.sleep(60)'!r}])
with pathlib.Path(os.environ['TEST_APT_ROOT']).joinpath('acquisition-pids').open('a') as output:
    output.write(json.dumps(dict(pid=os.getpid(), child=child.pid, pgrp=os.getpgrp()))+'\\n')
time.sleep(60)
""")

    def acquisition_identities(self):
        path = self.root / 'acquisition-pids'
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def assert_acquisition_closed(self):
        for identity in self.acquisition_identities():
            self.assertNotEqual(identity['pgrp'], os.getpgrp())
            for pid in (identity['pid'], identity['child']):
                path = Path('/proc') / str(pid) / 'stat'
                if path.exists():
                    self.assertEqual(path.read_text().rsplit(')', 1)[1].split()[0], 'Z')

    def close_fixture_processes(self, child):
        # Failure cleanup only targets process groups created by this fixture.
        for identity in self.acquisition_identities():
            if identity['pgrp'] != os.getpgrp():
                try: os.killpg(identity['pgrp'], signal.SIGKILL)
                except ProcessLookupError: pass
        if child.poll() is None:
            os.killpg(child.pid, signal.SIGKILL)
        child.wait(timeout=5)

    def test_cancellation_at_watchdog_launch_closes_its_owned_group(self):
        self.prepare_real_watchdog()
        self.env['CI_APT_DOWNLOAD_TIMEOUT'] = '60'
        real_timeout = shutil.which('timeout')
        self.executable('timeout', f"""#!{sys.executable}
import json,os,pathlib,signal,sys
if sys.argv[1:] not in (['--version'], ['--help']):
    pathlib.Path(os.environ['TEST_APT_ROOT']).joinpath('launch-pid').write_text(str(os.getpid()))
    os.kill(os.getppid(), signal.SIGTERM)
os.execv({real_timeout!r}, [{real_timeout!r}]+sys.argv[1:])
""")
        child = subprocess.Popen(['sh', str(HELPER), 'install', 'fixture'], env=self.env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                 start_new_session=True)
        try:
            stdout, stderr = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 143, stdout + stderr)
            pid = int((self.root / 'launch-pid').read_text())
            for entry in Path('/proc').iterdir():
                if not entry.name.isdecimal(): continue
                try: fields = (entry / 'stat').read_text().rsplit(')', 1)[1].split()
                except (FileNotFoundError, ProcessLookupError, PermissionError): continue
                self.assertFalse(int(fields[2]) == pid and fields[0] != 'Z')
            self.assert_acquisition_closed()
        finally:
            self.close_fixture_processes(child)

    def test_real_watchdog_expiry_closes_apt_and_its_worker_before_retry(self):
        self.prepare_real_watchdog()
        child = subprocess.Popen(['sh', str(HELPER), 'install', 'fixture'], env=self.env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                 start_new_session=True)
        try:
            stdout, stderr = child.communicate(timeout=15)
            self.assertEqual(child.returncode, 124, stdout + stderr)
            self.assertEqual(len(self.acquisition_identities()), 3)
            self.assertEqual(stdout.count('exceeded 1s'), 3)
            self.assert_acquisition_closed()
        finally:
            self.close_fixture_processes(child)

    def test_real_watchdog_closes_term_resistant_orphan_holding_stdout(self):
        self.prepare_real_watchdog(resistant_worker=True)
        child = subprocess.Popen(['sh', str(HELPER), 'install', 'fixture'], env=self.env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                 start_new_session=True)
        try:
            stdout, stderr = child.communicate(timeout=15)
            self.assertEqual(child.returncode, 124, stdout + stderr)
            self.assertEqual(len(self.acquisition_identities()), 3)
            self.assert_acquisition_closed()
        finally:
            self.close_fixture_processes(child)

    def test_parent_signal_closes_real_watchdog_apt_and_worker(self):
        self.prepare_real_watchdog()
        self.env['CI_APT_DOWNLOAD_TIMEOUT'] = '60'
        for sig, expected in ((signal.SIGTERM, 143), (signal.SIGINT, 130)):
            with self.subTest(signal=sig):
                path = self.root / 'acquisition-pids'
                if path.exists(): path.unlink()
                child = subprocess.Popen(['sh', str(HELPER), 'install', 'fixture'], env=self.env,
                                         stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                         start_new_session=True)
                try:
                    deadline = time.monotonic() + 5
                    while not path.exists() and time.monotonic() < deadline:
                        time.sleep(.01)
                    self.assertTrue(path.exists(), 'acquisition child did not start')
                    child.send_signal(sig)
                    stdout, stderr = child.communicate(timeout=5)
                    self.assertEqual(child.returncode, expected, stdout + stderr)
                    self.assert_acquisition_closed()
                finally:
                    self.close_fixture_processes(child)

    def test_mixed_unknown_and_network_errors_are_not_recovered_by_watchdog(self):
        self.enable_timeout(mode='expire')
        self.run_helper('install', 'package', steps=[self.step('update'),
            self.step('install', 'E: Disk quota exceeded\nE: Failed to fetch http://mirror.invalid/package 503 Service Unavailable')],
            expected=124, extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '2'})
        self.assertEqual(len(self.calls()), 2)

    def test_mixed_unknown_and_network_errors_are_fatal_without_expiry_too(self):
        self.enable_timeout()
        self.run_helper('install', 'package', steps=[self.step('update'),
            self.step('install', 'E: Disk quota exceeded\nE: Failed to fetch http://mirror.invalid/package 503 Service Unavailable', 100)],
            expected=100, extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '2'})
        self.assertEqual(len(self.calls()), 2)

    def test_opt_in_acquires_then_installs_offline_without_intermediate_update(self):
        self.enable_timeout()
        result = self.run_helper('install', 'libexpat1', 'python3', steps=[
            self.step('update'), self.step('install'), self.step('install')],
            extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '180'})
        calls = self.calls()
        self.assertEqual(['update' if call[-1:] == ['update'] else
                          call[call.index('install')] for call in calls],
                         ['update', 'install', 'install'])
        acquisition, offline = calls[1], calls[2]
        self.assertEqual(acquisition[-6:], ['install', '--download-only', '-y',
                         '--no-install-recommends', 'libexpat1', 'python3'])
        self.assertEqual(offline[-6:], ['install', '--no-download', '-y',
                         '--no-install-recommends', 'libexpat1', 'python3'])
        self.assertEqual(len((self.root / 'timeout-calls').read_text().splitlines()), 1)
        self.assertEqual(json.loads((self.root / 'timeout-calls').read_text())[:1], ['180'])
        self.assertNotIn('exceeded', result.stdout)

    def test_download_watchdog_recovers_only_verified_expirations_and_is_bounded(self):
        self.enable_timeout(mode='expire')
        steps = []
        for _ in range(3):
            steps += [self.step('update'), self.step('install')]
        result = self.run_helper('install', 'package', steps=steps, expected=124,
                                 extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '1'})
        calls = self.calls()
        self.assertEqual(len(calls), 6)
        self.assertEqual(sum('install' in call for call in calls), 3)
        self.assertEqual(result.stdout.count('exceeded 1s'), 3)
        self.assertIn('watchdog recovery exhausted', result.stderr)
        self.assertFalse((self.root / 'sleeps').exists())
        mirrors = (self.apt / 'ci-ubuntu-http.mirrors').read_text().splitlines()
        self.assertEqual(mirrors, ['http://azure.archive.ubuntu.com/ubuntu/\tpriority:1',
                                   'http://archive.ubuntu.com/ubuntu/\tpriority:2',
                                   'http://security.ubuntu.com/ubuntu/\tpriority:3'])
        self.assertEqual(len((self.root / 'timeout-calls').read_text().splitlines()), 3)
        self.assertIn('Acquire::http::No-Cache=true', calls[3])

    def test_apt_output_cannot_impersonate_watchdog_expiry(self):
        self.enable_timeout()
        self.run_helper('install', 'package', steps=[self.step('update'),
            self.step('install', 'timeout: sending signal TERM to command', 124)],
            expected=124, extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '2'})
        self.assertEqual(len(self.calls()), 2)

    def test_hard_watchdog_expiry_preserves_unmanaged_mirror_files(self):
        self.enable_timeout(mode='kill')
        unrelated = self.apt / 'ci-unrelated-https.mirrors'
        unrelated.write_text('https://example.invalid/one\tpriority:1\n'
                             'https://example.invalid/two\tpriority:2\n')
        original = unrelated.read_bytes()
        steps = [self.step(command) for _ in range(3) for command in ('update', 'install')]
        self.run_helper('install', 'package', steps=steps, expected=137,
                        extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '1'})
        self.assertEqual(unrelated.read_bytes(), original)
        self.assertFalse(any('--no-download' in call for call in self.calls()))

    def test_apt_return_124_without_watchdog_expiry_is_fatal(self):
        self.enable_timeout()
        self.run_helper('install', 'package', steps=[self.step('update'),
                        self.step('install', status=124)], expected=124,
                        extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '2'})
        self.assertEqual(len(self.calls()), 2)
        self.assertFalse((self.root / 'sleeps').exists())

    def test_timeout_does_not_recover_authentication_or_unclassified_apt_errors(self):
        for error in ['NO_PUBKEY 0123456789ABCDEF', 'E: unusual repository failure']:
            with self.subTest(error=error):
                self.enable_timeout(mode='expire')
                call_log = self.root / 'calls'
                if call_log.exists():
                    call_log.unlink()
                self.run_helper('install', 'package', steps=[self.step('update'),
                                self.step('install', error)], expected=124,
                                extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '2'})
                self.assertEqual(len(self.calls()), 2)

    def test_invalid_or_unavailable_opt_in_fails_before_configuration_mutation(self):
        source = self.source('deb http://archive.ubuntu.com/ubuntu noble main\n')
        original = source.read_text()
        for value in ['', '0', '00', '-1', '1.5', 'one']:
            with self.subTest(value=value):
                result = self.run_helper('configure', expected=2,
                                         extra_env={'CI_APT_DOWNLOAD_TIMEOUT': value})
                self.assertEqual(source.read_text(), original)
                self.assertFalse((self.root / 'calls').exists())
                self.assertIn('CI_APT_DOWNLOAD_TIMEOUT', result.stderr)
        self.enable_timeout(gnu=False)
        result = self.run_helper('configure', expected=2,
                                 extra_env={'CI_APT_DOWNLOAD_TIMEOUT': '10'})
        self.assertEqual(source.read_text(), original)
        self.assertIn('requires GNU coreutils timeout', result.stderr)

    def test_ubuntu_legacy_sources_keep_options_and_prefer_azure(self):
        source = self.source('deb [arch=amd64 signed-by=/keys/ubuntu.gpg] http://archive.ubuntu.com/ubuntu/ noble main universe\n'
                             'deb-src http://security.ubuntu.com/ubuntu noble-security main\n')
        self.run_helper('configure')
        self.assertEqual(source.read_text(),
                         f'deb [arch=amd64 signed-by=/keys/ubuntu.gpg] mirror+file:{self.apt}/ci-ubuntu-http.mirrors noble main universe\n'
                         f'deb-src mirror+file:{self.apt}/ci-ubuntu-http.mirrors noble-security main\n')
        mirrors = (self.apt / 'ci-ubuntu-http.mirrors').read_text().splitlines()
        self.assertEqual(mirrors, ['http://azure.archive.ubuntu.com/ubuntu/\tpriority:1',
                                  'http://archive.ubuntu.com/ubuntu/\tpriority:2',
                                  'http://security.ubuntu.com/ubuntu/\tpriority:3'])

    def test_deb822_https_preserves_tls_security_suites_and_key(self):
        source = self.source('Types: deb deb-src\nURIs: https://archive.ubuntu.com/ubuntu/\n'
                             'Suites: noble noble-updates noble-backports noble-security\n'
                             'Components: main universe restricted multiverse\n'
                             'Architectures: amd64\nSigned-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg\n',
                             'sources.list.d/ubuntu.sources')
        before = source.read_text()
        self.run_helper('configure')
        self.assertEqual(source.read_text(), before.replace('https://archive.ubuntu.com/ubuntu/',
                         f'mirror+file:{self.apt}/ci-ubuntu-https.mirrors'))
        self.assertTrue(all(line.startswith('https://') for line in
                            (self.apt / 'ci-ubuntu-https.mirrors').read_text().splitlines()))

    def test_third_party_and_lookalike_uris_remain_verbatim(self):
        original = ('# deb http://archive.ubuntu.com/ubuntu noble main\n'
                    'deb https://packages.microsoft.com/repos/code stable main\n'
                    'deb https://github.com/owner/project/releases/download/tag/ ./\n'
                    'deb http://archive.ubuntu.com/ubuntu-other noble main\n'
                    'deb https://example.invalid/?origin=http://archive.ubuntu.com/ubuntu noble main\n'
                    'deb https://archive.ubuntu.com.evil.invalid/ubuntu noble main\n')
        source = self.source(original)
        self.run_helper('configure')
        self.assertEqual(source.read_text(), original)

    def test_adjacent_deb822_uris_are_each_rewritten(self):
        source = self.source('Types: deb\nURIs: http://archive.ubuntu.com/ubuntu http://archive.ubuntu.com/ubuntu/\n'
                             'Suites: noble\nComponents: main\n', 'sources.list.d/ubuntu.sources')
        self.run_helper('configure')
        self.assertEqual(source.read_text().count('mirror+file:'), 2)

    def test_debian_archive_and_security_have_separate_fallbacks(self):
        self.distro('debian', 'bookworm', 'arm64')
        source = self.source('Types: deb\nURIs: http://deb.debian.org/debian\nSuites: bookworm bookworm-updates\n'
                             'Components: main\nSigned-By: /keys/debian.gpg\n\n'
                             'Types: deb\nURIs: http://security.debian.org/debian-security\n'
                             'Suites: bookworm-security\nComponents: main\nSigned-By: /keys/debian.gpg\n',
                             'sources.list.d/debian.sources')
        self.run_helper('configure')
        contents = source.read_text()
        self.assertIn(f'URIs: mirror+file:{self.apt}/ci-debian-http.mirrors\n', contents)
        self.assertIn(f'URIs: mirror+file:{self.apt}/ci-debian-security-http.mirrors\n', contents)
        self.assertEqual(contents.count('Signed-By: /keys/debian.gpg'), 2)
        security = (self.apt / 'ci-debian-security-http.mirrors').read_text()
        self.assertEqual(security, 'http://deb.debian.org/debian-security/\tpriority:1\n'
                                  'http://security.debian.org/debian-security/\tpriority:2\n')

    def test_debian_cdn_security_is_not_rewritten_as_main_archive(self):
        self.distro('debian', 'trixie')
        source = self.source('deb http://deb.debian.org/debian-security/ trixie-security main\n')
        self.run_helper('configure')
        self.assertEqual(source.read_text(),
                         f'deb mirror+file:{self.apt}/ci-debian-security-http.mirrors trixie-security main\n')

    def test_older_arm_and_other_ports_remain_on_ports(self):
        for codename, arch in [('jammy', 'arm64'), ('noble', 'arm64'), ('resolute', 'armhf')]:
            with self.subTest(codename=codename, arch=arch):
                self.distro('ubuntu', codename, arch)
                original = f'deb http://ports.ubuntu.com/ubuntu-ports {codename} main universe\n'
                source = self.source(original)
                self.run_helper('configure')
                self.assertEqual(source.read_text(), original)

    def test_resolute_arm64_can_use_azure_main_archive(self):
        self.distro('ubuntu', 'resolute', 'arm64')
        source = self.source('deb http://archive.ubuntu.com/ubuntu resolute main\n')
        self.run_helper('configure')
        self.assertIn('ci-ubuntu-http.mirrors', source.read_text())

    def test_configuration_is_idempotent_and_persists_strict_policy(self):
        source = self.source('deb http://archive.ubuntu.com/ubuntu noble main\n')
        self.run_helper('configure')
        original = source.read_text()
        self.run_helper('configure')
        self.assertEqual(source.read_text(), original)
        policy = (self.apt / 'apt.conf.d/99datapump-ci').read_text()
        self.assertIn('Acquire::Retries "3";', policy)
        self.assertIn('Acquire::http::Timeout "30";', policy)
        self.assertIn('APT::Update::Error-Mode "any";', policy)
        self.assertNotRegex(policy, r'(?i)(AllowUnauthenticated|AllowInsecure|Verify-Peer|Check-Valid-Until)')

    def test_install_refreshes_before_retrying_a_stale_package_404(self):
        result = self.run_helper('install', 'libexpat1', 'python3', steps=[
            self.step('update'),
            self.step('install', 'E: Failed to fetch http://security.ubuntu.com/ubuntu/pool/libexpat1.deb  404 Not Found', 100),
            self.step('update'), self.step('install')])
        calls = self.calls()
        self.assertEqual(['install' if 'install' in call else 'update' for call in calls],
                         ['update', 'install', 'update', 'install'])
        self.assertEqual(calls[1][-5:], ['install', '-y', '--no-install-recommends', 'libexpat1', 'python3'])
        for call in calls:
            self.assertIn('Acquire::Retries=3', call)
            self.assertIn('APT::Update::Error-Mode=any', call)
        self.assertEqual((self.root / 'sleeps').read_text(), '5\n')
        self.assertNotIn('Acquire::http::No-Cache=true', calls[0])
        self.assertIn('Acquire::http::No-Cache=true', calls[2])
        self.assertIn('attempt 2/3', result.stdout)

    def test_partial_update_fetch_failure_retries_before_install(self):
        self.run_helper('install', 'git', steps=[
            self.step('update', 'E: Failed to fetch http://deb.debian.org/debian/dists/bookworm/InRelease Temporary failure resolving deb.debian.org', 100),
            self.step('update'), self.step('install')])
        self.assertEqual(sum('install' in call for call in self.calls()), 1)

    def test_recovery_exhaustion_is_fatal_and_bounded(self):
        steps = []
        for _ in range(3):
            steps += [self.step('update'), self.step('install',
                      'E: Failed to fetch http://mirror.invalid/missing.deb 404 Not Found', 100)]
        result = self.run_helper('install', 'package', steps=steps, expected=100)
        self.assertEqual(len(self.calls()), 6)
        self.assertEqual((self.root / 'sleeps').read_text(), '5\n15\n')
        self.assertIn('exhausted after 3 attempts', result.stderr)

    def test_update_only_retries_transient_http_failure(self):
        self.run_helper('update', steps=[
            self.step('update', 'E: Failed to fetch http://mirror.invalid/InRelease 503 Service Unavailable', 100),
            self.step('update')])
        self.assertEqual(len(self.calls()), 2)

    def test_authentication_and_non_network_errors_are_immediately_fatal(self):
        errors = [
            'E: The repository http://mirror.invalid stable InRelease is not signed.',
            'E: Failed to fetch https://mirror.invalid/InRelease Certificate verification failed.',
            'W: GPG error: NO_PUBKEY 0123456789ABCDEF\nE: Failed to fetch http://mirror.invalid/InRelease 503 Service Unavailable',
            'E: Failed to fetch http://mirror.invalid/package.deb Hash Sum mismatch',
            'E: Unable to locate package does-not-exist',
            'dpkg: error processing package broken\nE: Failed to fetch http://mirror.invalid/package.deb 404 Not Found',
            'E: The repository http://mirror.invalid missing Release does not have a Release file.',
            'E: Disk quota exceeded',
        ]
        for error in errors:
            with self.subTest(error=error):
                self.run_helper('install', 'package', steps=[self.step('update'),
                                self.step('install', error, 100)], expected=100)
                self.assertFalse((self.root / 'sleeps').exists())

    def test_unknown_distribution_keeps_its_repositories(self):
        self.distro('other', 'custom')
        original = 'deb http://archive.ubuntu.com/ubuntu noble main\n'
        source = self.source(original)
        self.run_helper('configure')
        self.assertEqual(source.read_text(), original)

    def test_usage_errors_fail_before_running_apt(self):
        for args in [(), ('install',), ('configure', 'extra'), ('update', 'extra'), ('upgrade',)]:
            with self.subTest(args=args):
                self.run_helper(*args, expected=2)
        self.assertFalse((self.root / 'calls').exists())


@unittest.skipUnless(all(shutil.which(tool) for tool in
                     ('apt-get', 'apt-ftparchive', 'gpg', 'dpkg-deb')),
                     'native APT archive/signing tools required')
class NativeAptMirrorTests(unittest.TestCase):
    def test_signed_mirror_fallback_downloads_identical_package(self):
        """Real APT must recover a missing payload using its second mirror."""
        with tempfile.TemporaryDirectory(prefix='datapump-native-apt-') as directory:
            root = Path(directory)
            root.chmod(0o755)

            def run(*args, cwd=root):
                result = subprocess.run(args, cwd=cwd, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return result.stdout

            package = root / 'package'
            (package / 'DEBIAN').mkdir(parents=True)
            (package / 'DEBIAN/control').write_text(
                'Package: datapump-apt-fixture\nVersion: 1\nArchitecture: all\n'
                'Maintainer: Test <apt@example.invalid>\nDescription: signed mirror fixture\n')
            payload = package / 'usr/share/apt-fixture.txt'
            payload.parent.mkdir(parents=True)
            payload.write_text('Fallback must retain these exact signed package bytes.\n')
            fallback = root / 'fallback'
            (fallback / 'pool').mkdir(parents=True)
            deb = fallback / 'pool/datapump-apt-fixture_1_all.deb'
            run('dpkg-deb', '--build', str(package), str(deb))
            indexes = fallback / 'dists/stable/main/binary-amd64'
            indexes.mkdir(parents=True)
            (indexes / 'Packages').write_text(run('apt-ftparchive', 'packages', 'pool', cwd=fallback))
            release_dir = fallback / 'dists/stable'
            release = release_dir / 'Release'
            release.write_text(run('apt-ftparchive',
                                   '-o', 'APT::FTPArchive::Release::Codename=stable',
                                   '-o', 'APT::FTPArchive::Release::Suite=stable',
                                   '-o', 'APT::FTPArchive::Release::Architectures=amd64',
                                   '-o', 'APT::FTPArchive::Release::Components=main',
                                   'release', 'dists/stable', cwd=fallback))
            keys = root / 'keys'
            keys.mkdir(mode=0o700)
            gpg = ['gpg', '--batch', '--homedir', str(keys), '--pinentry-mode', 'loopback', '--passphrase', '']
            run(*gpg, '--quick-generate-key', 'APT Fixture <apt@example.invalid>', 'ed25519', 'sign', '0')
            keyring = root / 'fixture.gpg'
            run(*gpg, '--output', str(keyring), '--export')
            run(*gpg, '--output', str(release_dir / 'InRelease'), '--clearsign', str(release))
            run('gpgconf', '--homedir', str(keys), '--kill', 'gpg-agent')
            primary = root / 'primary'
            shutil.copytree(fallback, primary)
            (primary / 'pool/datapump-apt-fixture_1_all.deb').unlink()

            etc = root / 'etc/apt'
            (etc / 'sources.list.d').mkdir(parents=True)
            (root / 'etc/os-release').write_text('ID=debian\nVERSION_CODENAME=bookworm\n')
            (etc / 'sources.list').write_text(
                f'deb [signed-by={keyring}] http://deb.debian.org/debian stable main\n')
            configured = subprocess.run(['sh', str(HELPER), 'configure'],
                                        env=dict(os.environ, CI_APT_ROOT=str(root)),
                                        capture_output=True, text=True)
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            (etc / 'ci-debian-http.mirrors').write_text(
                f'{primary.as_uri()}/\tpriority:1\n{fallback.as_uri()}/\tpriority:2\n')
            state = root / 'state'
            (state / 'lists/partial').mkdir(parents=True)
            (state / 'status').write_text('')
            cache = root / 'cache'
            (cache / 'archives/partial').mkdir(parents=True)
            downloads = root / 'downloads'
            downloads.mkdir()
            apt = ['apt-get', '-o', f'Dir::Etc={etc}', '-o', f'Dir::State={state}',
                   '-o', f'Dir::State::status={state}/status', '-o', f'Dir::Cache={cache}',
                   '-o', 'APT::Architecture=amd64', '-o', 'Acquire::Languages=none']
            run(*apt, 'update')
            output = run(*apt, 'download', 'datapump-apt-fixture=1', cwd=downloads)
            self.assertIn('datapump-apt-fixture', output)
            self.assertEqual((downloads / deb.name).read_bytes(), deb.read_bytes())
            self.assertFalse((primary / 'pool' / deb.name).exists())
            # Exercise the opt-in helper with real signed APT acquisition.
            # Only the final installation is simulated; this fixture must never
            # modify the host package database or install its payload.
            wrapper_dir = root / 'wrapper'
            wrapper_dir.mkdir()
            real_apt = shutil.which('apt-get')
            wrapper = wrapper_dir / 'apt-get'
            wrapper.write_text(f"""#!{sys.executable}
import os,sys
prefix={apt[1:]!r}
args=sys.argv[1:]
if 'install' in args and '--no-download' in args: args=['--simulate']+args
os.execv({real_apt!r}, [{real_apt!r}]+prefix+args)
""")
            wrapper.chmod(0o755)
            # Preserve this fixture's already-configured signed file mirrors.
            (root / 'etc/os-release').write_text('ID=fixture\nVERSION_CODENAME=stable\n')
            # file: acquisitions may use the repository payload in place. Use
            # HTTP for this prefetch/offline-install boundary, as hosted CI does.
            handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(root))
            server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
            server_thread = threading.Thread(target=server.serve_forever)
            server_thread.start()
            port = server.server_port
            (etc / 'ci-debian-http.mirrors').write_text(
                f'http://127.0.0.1:{port}/primary/\tpriority:1\n'
                f'http://127.0.0.1:{port}/fallback/\tpriority:2\n')
            try:
                helper_result = subprocess.run(['sh', str(HELPER), 'install', 'datapump-apt-fixture=1'],
                    env=dict(os.environ, CI_APT_ROOT=str(root), CI_APT_DOWNLOAD_TIMEOUT='10',
                             PATH=str(wrapper_dir)+os.pathsep+os.environ['PATH']),
                    capture_output=True, text=True, timeout=30)
            finally:
                server.shutdown()
                server.server_close()
                server_thread.join(timeout=5)
                self.assertFalse(server_thread.is_alive())
            self.assertEqual(helper_result.returncode, 0, helper_result.stdout + helper_result.stderr)
            self.assertEqual((cache / 'archives' / deb.name).read_bytes(), deb.read_bytes())
            self.assertIn('Download complete and in download only mode', helper_result.stdout)
            self.assertIn('Inst datapump-apt-fixture', helper_result.stdout)
            self.assertFalse((Path('/usr/share') / 'apt-fixture.txt').exists())


if __name__ == '__main__':
    unittest.main()

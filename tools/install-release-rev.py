#!/usr/bin/env python3
"""Prepare a Jammy-native Rev compiler and build tool without rebuilding the SDK."""
import argparse
import hashlib
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile


LLVM_VERSION = '1:19.1.7~++20250114103320+cd708029e0b2-1~exp1~20250114103432.75'
# Primary fingerprint published at https://apt.llvm.org/.
LLVM_FINGERPRINT = '6084F3CF814B57C1CF12EFD515CF4D18AF4F7421'
# Same source and digest as package/ninja/ninja.{mk,hash} in the source SDK's
# pinned Buildroot 2026.08. Keep this host-tool bootstrap outside the SDK recipe.
NINJA_VERSION = '1.13.2'
NINJA_SHA256 = '974d6b2f4eeefa25625d34da3cb36bdcebe7fbce40f4c16ac0835fd1c0cbae17'


def run(arguments, **kwargs):
    print('+', ' '.join(str(argument) for argument in arguments), flush=True)
    return subprocess.run([str(argument) for argument in arguments], check=True, **kwargs)


def download(url, destination):
    run(['curl', '--fail', '--silent', '--show-error', '--location', '--retry', '3',
         '--proto', '=https', '--tlsv1.2', url, '--output', destination])


def primary_fingerprints(listing):
    fingerprints = []
    primary = False
    for line in listing.splitlines():
        fields = line.split(':')
        if fields[0] == 'pub':
            primary = True
        elif fields[0] == 'sub':
            primary = False
        elif fields[0] == 'fpr' and primary:
            fingerprints.append(fields[9])
            primary = False
    return fingerprints


def install(destination, system_root=Path('/')):
    destination = Path(destination)
    if not destination.is_absolute() or destination.exists() or destination.is_symlink():
        raise ValueError('Rev tool destination must be an absolute, new directory')
    release = dict(line.split('=', 1) for line in (system_root / 'etc/os-release').read_text().splitlines()
                   if '=' in line and not line.startswith('#'))
    if (release.get('ID', '').strip('"'), release.get('VERSION_ID', '').strip('"')) != ('ubuntu', '22.04'):
        raise ValueError('Native release Rev tools require Ubuntu 22.04')
    if platform.system() != 'Linux' or platform.machine() not in ('x86_64', 'amd64', 'aarch64', 'arm64'):
        raise ValueError('Native release Rev tools support Linux x86_64 and ARM64 only')
    if os.geteuid() != 0:
        raise ValueError('The CI toolchain preparation requires root for apt packages')
    architecture = 'amd64' if platform.machine() in ('x86_64', 'amd64') else 'arm64'
    apt_environment = {**os.environ, 'DEBIAN_FRONTEND': 'noninteractive'}
    run(['apt-get', 'update'], env=apt_environment)
    run(['apt-get', 'install', '-y', '--no-install-recommends',
         'ca-certificates', 'curl', 'gnupg', 'make', 'g++-11'], env=apt_environment)
    with tempfile.TemporaryDirectory(prefix='datapump-rev-tools-') as work:
        scratch = Path(work)
        key = scratch / 'llvm.asc'
        keyring = scratch / 'llvm.gpg'
        gnupg = scratch / 'gnupg'
        gnupg.mkdir(mode=0o700)
        download('https://apt.llvm.org/llvm-snapshot.gpg.key', key)
        listing = run(['gpg', '--batch', '--homedir', gnupg, '--with-colons', '--show-keys', key],
                      capture_output=True, text=True).stdout
        if primary_fingerprints(listing) != [LLVM_FINGERPRINT]:
            raise ValueError('Official LLVM signing key fingerprint does not match the release pin')
        run(['gpg', '--batch', '--homedir', gnupg, '--dearmor', '--output', keyring, key])
        installed_key = system_root / 'usr/share/keyrings/datapump-release-llvm19.gpg'
        installed_key.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(keyring, installed_key)
        installed_key.chmod(0o644)
        source = system_root / 'etc/apt/sources.list.d/datapump-release-llvm19.list'
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_text(f'deb [arch={architecture} signed-by={installed_key}] '
                          'https://apt.llvm.org/jammy/ llvm-toolchain-jammy-19 main\n')
        source.chmod(0o644)
        run(['apt-get', 'update'], env=apt_environment)
        run(['apt-get', 'install', '-y', '--no-install-recommends',
             f'clang-19={LLVM_VERSION}', f'clang-tools-19={LLVM_VERSION}',
             'libxrandr-dev', 'libgl-dev', 'libglew-dev', 'libfreetype-dev'], env=apt_environment)

        archive = scratch / 'ninja.tar.gz'
        download(f'https://github.com/ninja-build/ninja/archive/refs/tags/v{NINJA_VERSION}.tar.gz', archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != NINJA_SHA256:
            raise ValueError('Ninja source checksum does not match the release pin')
        ninja_source = scratch / 'source'
        ninja_source.mkdir()
        run(['tar', '-xzf', archive, '--strip-components=1', '-C', ninja_source])
        build = scratch / 'build'
        staged = scratch / 'install'
        run(['cmake', '-S', ninja_source, '-B', build, '-G', 'Unix Makefiles',
             '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_COMPILER=g++-11',
             '-DBUILD_TESTING=OFF', f'-DCMAKE_INSTALL_PREFIX={staged}'])
        run(['cmake', '--build', build, '--parallel', str(os.cpu_count() or 1)])
        run(['cmake', '--install', build])
        version = run([staged / 'bin/ninja', '--version'], capture_output=True, text=True).stdout.strip()
        if version != NINJA_VERSION:
            raise ValueError('Built Ninja version does not match the release pin')
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(staged), destination)
    run(['clang-19', '--version'])
    run(['clang-scan-deps-19', '--version'])
    print(f'Installed Ninja {NINJA_VERSION}; add {destination}/bin to PATH/GITHUB_PATH')
    print('Configure Rev with CC=clang-19 CXX=clang++-19 and CMake 3.28 or newer')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    try:
        install(args.destination)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f'Rev toolchain preparation failed: {error}\n')


if __name__ == '__main__':
    main()

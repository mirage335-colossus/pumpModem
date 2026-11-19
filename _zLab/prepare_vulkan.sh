#!/bin/sh
# Called in the freshly created deployment directory, before building the worker.
set -eu

if [ -r /etc/os-release ]; then . /etc/os-release; fi
if [ "${ID-}:${VERSION_CODENAME-}" = ubuntu:noble ] && [ "$(dpkg --print-architecture)" = amd64 ]; then
    packages="libvulkan-dev glslang-dev glslang-tools spirv-tools"
    if ! command -v ninja >/dev/null 2>&1; then
        packages="$packages ninja-build"
    fi
    if command -v nvcc >/dev/null 2>&1; then
        # The toolkit may provide a conflicting loader at this soname. Use
        # the signed Ubuntu ICD loader for managed deployments.
        packages="$packages opencl-c-headers ocl-icd-libopencl1:amd64"
    fi
    missing=
    for package in $packages; do
        if [ "$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null || true)" != installed ]; then
            missing="$missing $package"
        fi
    done
    if [ -n "$missing" ]; then
        sources=$(mktemp -d)
        trap 'rm -rf "$sources"' EXIT
        chmod 755 "$sources"
        mkdir -m 755 "$sources/lists"
        mirror=https://archive.ubuntu.com/ubuntu
        apt_source() {
            printf 'deb [arch=amd64 target=Packages signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] %s %s %s\n' "$mirror" "$1" "$2"
        }
        write_sources() {
            {
                apt_source noble main
                apt_source noble-updates 'main universe'
                apt_source noble-security 'main universe'
            } > "$sources/ubuntu.list"
        }
        set -- -o "Dir::Etc::sourcelist=$sources/ubuntu.list" -o Dir::Etc::sourceparts=- \
            -o "Dir::State::lists=$sources/lists" -o Acquire::Retries=0 -o Acquire::https::Timeout=15 \
            -o Acquire::Languages=none -o Acquire::IndexTargets::deb::DEP-11::DefaultEnabled=false
        write_sources
        if ! timeout -k 5 45 apt-get "$@" update --error-on=any; then
            echo 'PUMP_LAB_STAGE=dependency-mirror'
            mirror=https://security.ubuntu.com/ubuntu
            write_sources
            timeout -k 5 60 apt-get "$@" update --error-on=any
        fi
        # The amd64 shader tools are published in the updates and security
        # pockets. Avoid the large base Universe index when APT can already
        # resolve the full requested install using those signed package lists.
        if ! apt-get "$@" --simulate install --no-install-recommends --no-remove $missing; then
            apt_source noble universe >> "$sources/ubuntu.list"
            apt-get "$@" update --error-on=any
        fi
        DEBIAN_FRONTEND=noninteractive apt-get "$@" install --no-install-recommends --no-remove -y $missing
    fi
fi

# Use the NVIDIA EGL driver for headless Vulkan device discovery.
# Use the injected driver's API version and keep the system ICD unchanged.
umask 077
mkdir -p build/lab
python3 - <<'PY'
from pathlib import Path
import json
import os
import re
import stat
import tempfile
sources = (Path('/etc/vulkan/icd.d/nvidia_icd.json'), Path('/usr/share/vulkan/icd.d/nvidia_icd.json'))
source = next((p for p in sources if p.is_file()), None)
if source is None:
    raise SystemExit('Missing NVIDIA Vulkan ICD manifest')
driver = json.loads(source.read_text())['ICD']
version = driver['api_version']
if (driver['library_path'] not in ('libGLX_nvidia.so.0', 'libEGL_nvidia.so.0')
        or not isinstance(version, str) or not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version)):
    raise SystemExit('Unsupported NVIDIA Vulkan ICD manifest')
manifest = {'file_format_version': '1.0.0', 'ICD': {'library_path': 'libEGL_nvidia.so.0', 'api_version': version}}

def private_manifest(target, data, driver_name):
    with tempfile.NamedTemporaryFile(dir=target.parent, prefix='.icd-') as out:
        out.write(data)
        out.flush()
        os.fsync(out.fileno())
        try:
            os.link(out.name, target, follow_symlinks=False)
        except FileExistsError:
            # A matching private manifest may be left by an interrupted build.
            fd = os.open(target, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            with os.fdopen(fd, 'rb') as existing:
                info = os.fstat(existing.fileno())
                if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077
                        or existing.read(len(data) + 1) != data):
                    raise SystemExit(f'Existing NVIDIA {driver_name} manifest does not match this deployment')


build = Path('build/lab')
private_manifest(build / 'nvidia-headless-icd.json', json.dumps(manifest).encode('utf-8'), 'Vulkan')
vendors = build / 'nvidia-opencl-vendors'
vendors.mkdir(mode=0o700, exist_ok=True)
info = vendors.lstat()
if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
    raise SystemExit('Unsafe deployment OpenCL vendor directory')
private_manifest(vendors / 'nvidia.icd', b'libnvidia-opencl.so.1\n', 'OpenCL')
PY

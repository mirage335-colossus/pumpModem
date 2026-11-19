#!/bin/sh
# Called in the freshly created deployment directory, before building the worker.
set -eu

if [ -r /etc/os-release ]; then . /etc/os-release; fi
if [ "${ID-}:${VERSION_CODENAME-}" = ubuntu:noble ] && [ "$(dpkg --print-architecture)" = amd64 ]; then
    packages="libvulkan-dev glslang-dev glslang-tools spirv-tools"
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
        mkdir "$sources/lists"
        for suite in noble noble-updates noble-security; do
            printf 'deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://archive.ubuntu.com/ubuntu %s main universe\n' "$suite"
        done > "$sources/ubuntu.list"
        set -- -o "Dir::Etc::sourcelist=$sources/ubuntu.list" -o Dir::Etc::sourceparts=- \
            -o "Dir::State::lists=$sources/lists" -o Acquire::Retries=1 -o Acquire::https::Timeout=15 \
            -o Acquire::Languages=none -o Acquire::IndexTargets::deb::DEP-11::DefaultEnabled=false
        apt-get "$@" update --error-on=any
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
import re
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
with open('build/lab/nvidia-headless-icd.json', 'x') as out:
    json.dump(manifest, out)
PY

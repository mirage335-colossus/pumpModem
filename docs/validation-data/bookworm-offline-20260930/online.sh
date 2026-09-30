#!/bin/bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
install -d /srv/recovery/inputs/native /srv/recovery/inputs/wasm /srv/recovery/outputs
apt-get update
apt-get install -y --no-install-recommends ca-certificates git curl python3 file binutils make cmake ninja-build perl patch xz-utils unzip zip xvfb xauth fonts-dejavu-core fontconfig-config libgl1 libopengl0 libgl1-mesa-dri libxtst6 nodejs iproute2 util-linux
id builder >/dev/null 2>&1 || useradd -m -s /bin/bash builder
cat /etc/os-release > /srv/recovery/outputs/host-os-release
uname -a > /srv/recovery/outputs/host-kernel
lscpu > /srv/recovery/outputs/host-cpu
dpkg-query -W > /srv/recovery/outputs/host-packages
for compiler in cc gcc g++ clang clang++; do if command -v "$compiler"; then echo 'Unexpected host compiler'; exit 1; fi; done
git clone --filter=blob:none https://github.com/mirage335-colossus/pumpModem.git /srv/recovery/source
git -C /srv/recovery/source checkout --detach db31ad1b3109c0d7c9a1b87c0db9ab4d3093ce02
release=https://github.com/mirage335-colossus/pumpModem/releases/download/v001_00-2026-09-30-0648CDT
curl -fL --retry 3 "$release/datapump-sdk-6c4884fdff9c745ab0a0-linux-x86_64.tar.gz" -o /srv/recovery/inputs/native/datapump-sdk-6c4884fdff9c745ab0a0-linux-x86_64.tar.gz &
p1=$!
curl -fL --retry 3 "$release/datapump-sdk-sources-6c4884fdff9c745ab0a0.tar.gz" -o /srv/recovery/inputs/native/datapump-sdk-sources-6c4884fdff9c745ab0a0.tar.gz &
p2=$!
curl -fL --retry 3 "$release/sdk-6c4884fdff9c745ab0a0-SHA256SUMS.txt" -o /srv/recovery/inputs/native/SHA256SUMS
wait "$p1" "$p2"
curl -fL --retry 3 "$release/datapump-wasm-sdk-e66e98abb90b466cab32-linux-x86_64.tar.gz" -o /srv/recovery/inputs/wasm/datapump-wasm-sdk-e66e98abb90b466cab32-linux-x86_64.tar.gz &
p1=$!
curl -fL --retry 3 "$release/datapump-wasm-sdk-sources-e66e98abb90b466cab32.tar.gz" -o /srv/recovery/inputs/wasm/datapump-wasm-sdk-sources-e66e98abb90b466cab32.tar.gz &
p2=$!
curl -fL --retry 3 "$release/wasm-sdk-e66e98abb90b466cab32-SHA256SUMS.txt" -o /srv/recovery/inputs/wasm/SHA256SUMS
wait "$p1" "$p2"
chown -R builder:builder /srv/recovery
chmod -R a-w /srv/recovery/inputs
# Parent SSH stays connected; all recovery subprocesses inherit the disconnected child namespace.
unshare --net bash -c 'ip link set lo up; exec runuser -u builder -- python3 -u /srv/recovery/offline.py'

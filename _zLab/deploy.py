#!/usr/bin/env python3
"""Deploy a lab worker over authenticated direct SSH to a managed pod directory.

The SSH server host key must already be verified in ``known_hosts``. This helper
never performs trust-on-first-use or starts a pod. It creates a private
profile-specific directory (``/workspace/pump-lab`` for simulation or
``/workspace/pump-lab-live`` for live). Each source and build profile gets a
private revision. A verified build is reused and an interrupted build can
resume without affecting the last usable worker.
Only an allowlisted native source bundle is uploaded. A timeout ends the local
SSH client but does not prove that the remote build has stopped. Pod lifecycle
and cleanup remain the caller's responsibility. CUDA builds require compatible
CUDA tools, drivers and C++11 ABI LibTorch, and have not been qualified here.
The optional live profile builds a distinct lab worker; it remains experimental
and building it alone does not qualify physical audio or remote reception.
"""
from __future__ import annotations

import argparse
import hashlib
import ipaddress
import math
import os
from pathlib import Path
import re
import selectors
import shlex
import subprocess
import time
import sys
import tempfile

import bundle

ROOT = Path(__file__).resolve().parent.parent
REMOTE_DIR = '/workspace/pump-lab'
LIVE_REMOTE_DIR = '/workspace/pump-lab-live'
DEFAULT_TIMEOUT = 3600
DEFAULT_MODE = 'gpu'
BUILD_MODES = ('cpu', 'cuda', 'vulkan', 'gpu', 'native-cpu')
_STAGE_MESSAGES = {
    b'upload': 'Uploading the source bundle to the compute host…',
    b'unpack': 'Verifying and unpacking the source bundle…',
    b'environment': 'Checking the remote build environment…',
    b'dependencies': 'Preparing remote build dependencies…',
    b'dependency-mirror': 'Trying the Ubuntu security mirror for build dependencies…',
    b'configure': 'Configuring the native worker…',
    b'build-plan': 'Selecting parallel workers for the native build…',
    b'compile': 'Compiling the native worker…',
    b'check': 'Checking the built worker…',
    b'reuse': 'Checking the existing worker…',
}

# All text interpreted by the remote shell is fixed here or constructed from a
# locally computed SHA-256 digest. Provider data and user paths never enter it.
REMOTE_BUILD = r'''set -eu
mode=$2
profile=$3
unset requested_mode PUMP_LAB_OPTIONAL_NVIDIA_ICD
case "$mode" in cuda|vulkan|gpu|native-cpu) ;; *) exit 2 ;; esac
case "$profile" in simulation|live) ;; *) exit 2 ;; esac
# REMOTE_DEPLOY selected and opened this private revision under its deployment lock.
directory=$(pwd -P)
export CCACHE_BASEDIR="$directory"
printf '%s  %s\n' "$1" source.tar.gz | sha256sum -c -
tar --extract --gzip --file source.tar.gz --no-same-owner --no-same-permissions
set -- -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDATAPUMP_BUILD_GUI=OFF \
  -DDATAPUMP_BUILD_CLI=ON -DDATAPUMP_BUILD_WEB_WORKER=ON -DDATAPUMP_PORTABLE=OFF \
  -DDATAPUMP_COMPILER_CACHE=ON
# A noninteractive SSH shell can omit the image's CUDA bin directory.
# CUDA uses this toolkit for Torch and CMake. Vulkan can use nvcc to
# embed the optional Blackwell OpenCL program without loading Torch.
cuda_root=${CUDAToolkit_ROOT:-${CUDA_HOME:-${CUDA_PATH:-}}}
if [ -n "${CUDACXX:-}" ]; then
  nvcc=$(command -v "$CUDACXX" || true)
elif [ -n "$cuda_root" ]; then
  nvcc="$cuda_root/bin/nvcc"
else
  nvcc=$(command -v nvcc || printf '%s\n' /usr/local/cuda/bin/nvcc)
fi
if [ -f "$nvcc" ] && [ -x "$nvcc" ]; then
  nvcc=$(readlink -f "$nvcc")
  cuda_bin=$(dirname "$nvcc")
  cuda_root=$(dirname "$cuda_bin")
  export CUDACXX="$nvcc" PATH="$cuda_bin:$PATH"
elif [ "$mode" = cuda ]; then
  echo "CUDA build requires nvcc. Set CUDACXX or CUDA_HOME to the installed toolkit." >&2
  exit 78
fi
if [ "$mode" = native-cpu ]; then
  requested_mode=native-cpu
  printf 'PUMP_LAB_STAGE=dependencies\n'
  command -v apt-get >/dev/null 2>&1 || { echo 'Native CPU builds require apt-get on Ubuntu 24.04.' >&2; exit 78; }
  timeout -k 10 900 apt-get -o DPkg::Lock::Timeout=90 update
  DEBIAN_FRONTEND=noninteractive timeout -k 10 900 apt-get -o DPkg::Lock::Timeout=90 install -y --no-install-recommends \
    build-essential ccache cmake libssl-dev ninja-build python3
  mode=cpu
fi
if [ "$mode" = cuda ] || [ "$mode" = gpu ]; then
  requested_mode=$mode
  mode=vulkan
  if [ -x "$nvcc" ] && python3 -c 'import torch; assert torch.cuda.is_available() and torch.compiled_with_cxx11_abi()'; then
    mkdir -p build/lab
    c++ -std=c++20 -Iinclude "-I$cuda_root/include" _zLab/probe_cuda_worker.cpp src/host/sandbox.cpp \
      "-L$cuda_root/lib64" "-Wl,-rpath,$cuda_root/lib64" -lcudart -o build/lab/probe-cuda-worker
    if timeout 30 build/lab/probe-cuda-worker; then
      mode=cuda
    elif [ "$?" -ne 2 ]; then
      echo "CUDA worker discovery failed unexpectedly." >&2
      exit 1
    elif [ "$requested_mode" = cuda ]; then
      echo "CUDA device discovery failed inside the worker's no-socket boundary. Use --mode gpu or --mode vulkan for a new Vulkan-capable deployment." >&2
      exit 78
    fi
  elif [ "$requested_mode" = cuda ]; then
    echo "CUDA build requires C++11 ABI LibTorch and a visible CUDA device." >&2
    exit 78
  fi
  if [ "$requested_mode" = gpu ]; then
    printf 'Selected GPU backend: %s\n' "$mode"
  fi
fi
if [ "$mode" = cuda ]; then
  python3 -c 'import torch; assert torch.cuda.is_available() and torch.compiled_with_cxx11_abi(); print(torch.__version__, torch.version.cuda, torch.cuda.get_device_name())'
  torch_prefix=$(python3 -c 'from torch.utils import cmake_prefix_path; print(cmake_prefix_path)')
  native_sm=$(python3 -c 'import torch; print("".join(map(str,torch.cuda.get_device_capability())))')
  export TORCH_CUDA_ARCH_LIST="$(python3 -c 'import torch; print(".".join(map(str,torch.cuda.get_device_capability())))')"
  set -- "$@" -DDATAPUMP_TRIAD_TENSOR=ON -DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON -DDATAPUMP_TRIAD_VULKAN=OFF \
    "-DCMAKE_CUDA_COMPILER=$nvcc" "-DCUDAToolkit_ROOT=$cuda_root" "-DCUDA_TOOLKIT_ROOT_DIR=$cuda_root" \
    "-DCMAKE_PREFIX_PATH=$torch_prefix" "-DCMAKE_CUDA_ARCHITECTURES=$native_sm"
elif [ "$mode" = vulkan ] || [ "$mode" = gpu ]; then
  # Only automatic GPU mode may lack a host NVIDIA driver: the compiled
  # Vulkan backend remains optional at runtime and other GPUs may be visible.
  # Explicit Vulkan mode still requires the NVIDIA headless manifest.
  unset PUMP_LAB_OPTIONAL_NVIDIA_ICD
  if [ "${requested_mode:-}" = gpu ]; then
    export PUMP_LAB_OPTIONAL_NVIDIA_ICD=1
  fi
  timeout -k 10 480 sh _zLab/prepare_vulkan.sh
  unset PUMP_LAB_OPTIONAL_NVIDIA_ICD
  set -- "$@" -DDATAPUMP_TRIAD_TENSOR=OFF -DDATAPUMP_TRIAD_VULKAN=ON -DDATAPUMP_TRIAD_VULKAN_REQUIRE_DEVICE=ON
  opencl_loader=/usr/lib/x86_64-linux-gnu/libOpenCL.so.1
  if [ ! -r "$opencl_loader" ] || [ "$(dpkg-query -W -f='${db:Status-Status}' ocl-icd-libopencl1:amd64 2>/dev/null || true)" != installed ]; then
    opencl_loader=
  fi
  set -- "$@" "-DDATAPUMP_TRIAD_OPENCL_LOADER_PATH=$opencl_loader"
else
  # Linode's native CPU profile avoids GPU toolkit, Vulkan package and runtime
  # discovery on the fresh Ubuntu image.
  set -- "$@" -DDATAPUMP_TRIAD_TENSOR=OFF -DDATAPUMP_TRIAD_VULKAN=OFF
fi
worker=datapump-worker
if [ "$profile" = live ]; then
  set -- "$@" "-DCMAKE_PROJECT_DataPump_INCLUDE=$directory/_zLab/audio/inject.cmake"
  worker=datapump-lab-worker
fi
if [ -f build/lab/CMakeCache.txt ]; then
  generator=$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' build/lab/CMakeCache.txt | sed -n '1p')
  case "$generator" in
    "Unix Makefiles"|Ninja) ;;
    *) echo "Existing worker build uses an unsupported or unreadable CMake generator." >&2; exit 1 ;;
  esac
elif command -v ninja >/dev/null 2>&1; then
  generator=Ninja
else
  generator="Unix Makefiles"
fi
set -- "$@" -G "$generator"
measure_stage() {
  stage=$1
  shift
  python3 - "$stage" "$@" <<'PUMP_LAB_MEASURE_EOF'
import resource, subprocess, sys, time
stage, *command = sys.argv[1:]
start = time.monotonic()
result = subprocess.run(command, check=False)
usage = resource.getrusage(resource.RUSAGE_CHILDREN)
rss = int(usage.ru_maxrss * 1024)
print('PUMP_LAB_MEASURE=' + stage +
      ' wall_seconds={:.3f} child_user_cpu_seconds={:.3f} child_system_cpu_seconds={:.3f} child_peak_rss_bytes={}'.format(
          time.monotonic() - start, usage.ru_utime, usage.ru_stime, rss), flush=True)
raise SystemExit(result.returncode)
PUMP_LAB_MEASURE_EOF
}
measure_stage configure cmake -S . -B build/lab "$@"
cache_value() {
  sed -n "s/^$1:[^=]*=//p" build/lab/CMakeCache.txt | sed -n '1p'
}
build_info_value() {
  sed -n "s/^$1: *//p" build/lab/build-info.txt 2>/dev/null | sed -n '1p'
}
compiler_summary=$(build_info_value Compiler)
compiler_id=${compiler_summary%% *}
compiler_version=${compiler_summary#* }
[ -n "$compiler_id" ] || compiler_id=unknown
[ -n "$compiler_summary" ] || compiler_version=unknown
printf 'PUMP_LAB_CXX_COMPILER=%s\n' "$(cache_value CMAKE_CXX_COMPILER)"
printf 'PUMP_LAB_CXX_COMPILER_ID=%s\n' "$compiler_id"
printf 'PUMP_LAB_CXX_COMPILER_VERSION=%s\n' "$compiler_version"
compiler_launcher=$(build_info_value 'C++ launcher')
case "$compiler_launcher" in
  *ccache*) printf 'PUMP_LAB_COMPILER_CACHE=ccache\n' ;;
  '') printf 'PUMP_LAB_COMPILER_CACHE=unavailable-or-disabled\n' ;;
  *) printf 'PUMP_LAB_COMPILER_CACHE=other:%s\n' "$compiler_launcher" ;;
esac
build_jobs=$(python3 -B tools/build_capacity.py --runpod "$mode")
case "$build_jobs" in ''|*[!0-9]*) echo "Invalid native build parallel job count." >&2; exit 1 ;; esac
printf 'PUMP_LAB_BUILD_GENERATOR=%s\n' "$generator"
printf 'PUMP_LAB_BUILD_JOBS=%s\n' "$build_jobs"
printf 'PUMP_LAB_STAGE=build-plan\n'
if [ "${requested_mode:-}" = native-cpu ]; then
  measure_stage compile cmake --build build/lab --parallel "$build_jobs" \
    --target "$worker" triad_receiver_probe
else
  measure_stage compile cmake --build build/lab --parallel "$build_jobs" --target "$worker"
fi
"build/lab/$worker" --self-check
printf 'PUMP_LAB_PROFILE=%s\n' "$profile"
printf 'PUMP_LAB_MODE=%s\n' "$mode"
if [ "$mode" = cuda ]; then
  python3 -c 'import torch; print("PUMP_LAB_DEVICE=" + str(torch.cuda.get_device_name())); print("PUMP_LAB_TORCH=" + str(torch.__version__)); print("PUMP_LAB_CUDA=" + str(torch.version.cuda))'
else
  if [ "$mode" = cpu ]; then
    printf 'PUMP_LAB_DEVICE=CPU-only worker\nPUMP_LAB_TORCH=not requested\nPUMP_LAB_CUDA=not selected\n'
  else
    printf 'PUMP_LAB_DEVICE=Vulkan support compiled (runtime device selection pending)\nPUMP_LAB_TORCH=not requested\nPUMP_LAB_CUDA=not selected\n'
  fi
fi
worker_hash=$(sha256sum "build/lab/$worker")
printf 'PUMP_LAB_WORKER_SHA256=%s\n' "${worker_hash%% *}"
if [ "${requested_mode:-}" = native-cpu ]; then
  probe_hash=$(sha256sum build/lab/triad_receiver_probe)
  printf 'PUMP_LAB_PROBE_SHA256=%s\n' "${probe_hash%% *}"
fi
'''


# This native-host inspection runs on the pod after the build. It reports
# affinity and cgroup limits separately; neither is a GPU-execution receipt.
RESOURCE_RECEIPT = r'''import os
import platform
import posixpath
import re
import shutil
import subprocess
from fractions import Fraction
from pathlib import Path


def clean(value):
    return re.sub(r'[^ -~]', '?', str(value))[:160]


def mount_unescape(value):
    return re.sub(r'\\([0-7]{3})', lambda m: chr(int(m.group(1), 8)), value)


def membership(proc_root, controller):
    try:
        lines = (proc_root / 'self/cgroup').read_text().splitlines()
        mounts = (proc_root / 'self/mountinfo').read_text().splitlines()
    except OSError:
        return None
    memberships = []
    for line in lines:
        fields = line.split(':', 2)
        if len(fields) == 3 and (not fields[1] or controller in fields[1].split(',')):
            memberships.append((not fields[1], fields[2]))
    candidates = []
    for line in mounts:
        parts = line.split(' - ', 1)
        if len(parts) != 2:
            continue
        left, right = parts[0].split(), parts[1].split()
        if len(left) < 5 or len(right) < 3:
            continue
        kind = right[0]
        options = set(right[2].split(','))
        for unified, member in memberships:
            if not ((unified and kind == 'cgroup2') or
                    (not unified and kind == 'cgroup' and controller in options)):
                continue
            root, mount = mount_unescape(left[3]), mount_unescape(left[4])
            if not (root.startswith('/') and mount.startswith('/') and member.startswith('/')):
                continue
            rel = posixpath.relpath(member, root)
            if rel == '..' or rel.startswith('../'):
                continue
            current = Path(mount) / rel
            candidates.append((len(root), current, Path(mount), unified))
    if not candidates:
        return None
    _, current, mount, unified = max(candidates, key=lambda x: x[0])
    return current, mount, unified


def cgroup_limit(proc_root, controller):
    location = membership(proc_root, controller)
    if location is None:
        return 'unavailable'
    current, mount, unified = location
    values = []
    try:
        while True:
            if controller == 'memory':
                file = 'memory.max' if unified else 'memory.limit_in_bytes'
                text = (current / file).read_text().strip()
                if text != 'max':
                    limit = int(text)
                    if limit < 0:
                        return 'unavailable'
                    # v1 uses LONG_MAX rounded to page boundaries as infinity.
                    if unified or limit < (1 << 60):
                        values.append(limit)
            elif unified:
                quota, period = (current / 'cpu.max').read_text().split()
                if quota != 'max':
                    if int(quota) <= 0 or int(period) <= 0:
                        return 'unavailable'
                    values.append(Fraction(int(quota), int(period)))
            else:
                quota = int((current / 'cpu.cfs_quota_us').read_text())
                period = int((current / 'cpu.cfs_period_us').read_text())
                if quota != -1:
                    if quota <= 0 or period <= 0:
                        return 'unavailable'
                    values.append(Fraction(quota, period))
            if current == mount:
                break
            parent = current.parent
            if parent == current or mount not in parent.parents and parent != mount:
                return 'unavailable'
            current = parent
    except (OSError, ValueError, ZeroDivisionError):
        return 'unavailable'
    return str(min(values)) if values else 'unlimited'


def cpu_model(proc_root):
    try:
        for line in (proc_root / 'cpuinfo').read_text(errors='replace').splitlines():
            key, separator, value = line.partition(':')
            if separator and key.strip() in ('model name', 'Hardware'):
                return clean(value.strip())
    except OSError:
        pass
    return clean(platform.processor() or 'unavailable')


def memory_total(proc_root):
    try:
        for line in (proc_root / 'meminfo').read_text().splitlines():
            match = re.fullmatch(r'MemTotal:\s*([0-9]+)\s+kB\s*', line)
            if match:
                return str(int(match.group(1)) * 1024)
    except OSError:
        pass
    return 'unavailable'


def gpu_receipts():
    mode = os.environ.get('PUMP_LAB_RECEIPT_MODE')
    if mode not in ('cuda', 'vulkan'):
        print('PUMP_LAB_GPU_SCOPE=not requested')
        return
    if mode == 'cuda':
        print('PUMP_LAB_GPU_SCOPE=torch CUDA device 0; full-device allocation not established')
        try:
            import torch
            props = torch.cuda.get_device_properties(0)
            print('PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=' + str(int(props.total_memory)))
            uuid = clean(getattr(props, 'uuid', 'unavailable'))
            print('PUMP_LAB_GPU_VISIBLE_UUID=' + uuid)
            print('PUMP_LAB_GPU_PARTITION_STATUS=' + ('MIG indicated by UUID' if uuid.startswith('MIG-') else 'unverified'))
        except Exception:
            print('PUMP_LAB_GPU_VISIBLE_MEMORY_BYTES=unavailable')
            print('PUMP_LAB_GPU_VISIBLE_UUID=unavailable')
            print('PUMP_LAB_GPU_PARTITION_STATUS=unverified')
    else:
        print('PUMP_LAB_GPU_SCOPE=NVIDIA inventory probe during Vulkan build; verify device use in the worker')
    binary = shutil.which('nvidia-smi')
    try:
        if not binary:
            raise OSError('nvidia-smi unavailable')
        result = subprocess.run([binary, '-L'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                                timeout=5, check=True)
        text = ' | '.join(result.stdout.decode('utf-8', 'replace').splitlines())
        print('PUMP_LAB_GPU_INVENTORY=' + clean(text))
    except (OSError, subprocess.SubprocessError):
        print('PUMP_LAB_GPU_INVENTORY=unavailable')


def main():
    proc = Path('/proc')
    try:
        affinity = str(len(os.sched_getaffinity(0)))
    except (AttributeError, OSError):
        affinity = 'unavailable'
    print('PUMP_LAB_RECEIPT_SCOPE=deployment SSH process; worker execution not established')
    print('PUMP_LAB_CPU_MODEL=' + cpu_model(proc))
    print('PUMP_LAB_CPU_MODEL_SCOPE=first reported processor')
    print('PUMP_LAB_CPU_AFFINITY_LOGICAL=' + affinity)
    print('PUMP_LAB_KERNEL_MEMORY_TOTAL_BYTES=' + memory_total(proc))
    print('PUMP_LAB_CGROUP_SCOPE=visible ancestors only; hidden limits may be tighter')
    print('PUMP_LAB_CGROUP_MEMORY_LIMIT_BYTES=' + cgroup_limit(proc, 'memory'))
    print('PUMP_LAB_CGROUP_CPU_QUOTA_CORES=' + cgroup_limit(proc, 'cpu'))
    gpu_receipts()


if __name__ == '__main__':
    main()
'''
REMOTE_BUILD += "\nPUMP_LAB_RECEIPT_MODE=\"$mode\" python3 - <<'PUMP_LAB_RESOURCE_EOF'\n" + RESOURCE_RECEIPT + "\nPUMP_LAB_RESOURCE_EOF\n"


# One SSH process owns the lock from archive upload through build completion.
# A disconnected attempt can therefore finish safely while a retry waits for
# that lock to be released. Receipts are published only after a successful build.
REMOTE_DEPLOY = r'''import fcntl, hashlib, json, os, re, stat, subprocess, sys, tempfile
from pathlib import Path

digest, mode, profile, size_text = sys.argv[1:]
if not re.fullmatch('[a-f0-9]{64}', digest) or mode not in ('cuda','vulkan','gpu','native-cpu') or profile not in ('simulation','live'):
    raise SystemExit('Invalid deployment request')
allowed_backends = ('cuda', 'vulkan') if mode == 'gpu' else ('cpu',) if mode == 'native-cpu' else (mode,)
size = int(size_text)
if not 0 < size <= 256 * 1024 * 1024:
    raise SystemExit('Source archive exceeded the deployment limit')
directory = '/workspace/pump-lab-live' if profile == 'live' else '/workspace/pump-lab'
workspace = os.path.dirname(directory)
os.umask(0o077)
created = False
if mode == 'native-cpu':
    try:
        os.mkdir(workspace, 0o755)
    except FileExistsError:
        pass
    parent_info = os.lstat(workspace)
    if (not stat.S_ISDIR(parent_info.st_mode) or parent_info.st_uid != os.getuid()
            or parent_info.st_mode & 0o022):
        raise SystemExit('The deployment parent must be a non-writable directory owned by the SSH user.')
try:
    os.mkdir(directory, 0o700)
    created = True
except FileExistsError:
    pass
directory_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
info = os.fstat(directory_fd)
if info.st_uid != os.getuid() or info.st_mode & 0o077:
    raise SystemExit('Deployment directory must be private and owned by this user')
os.fchdir(directory_fd)
lock = os.open('.deploy.lock', os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
info = os.fstat(lock)
if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o022:
    raise SystemExit('Invalid deployment lock')
try:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    raise SystemExit('Deployment is already running on this pod. Retry after it finishes.')

def read_json(name):
    fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    try:
        info = os.fstat(fd)
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or info.st_mode & 0o022 or info.st_size > 16384):
            raise SystemExit('Invalid managed deployment record')
        return json.loads(os.read(fd, 16385))
    finally:
        os.close(fd)

def write_json(name, value):
    fd, temporary = tempfile.mkstemp(prefix='.receipt-', dir='.')
    try:
        with os.fdopen(fd, 'w') as out:
            json.dump(value, out)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, name)
        os.fsync(directory_fd)
    finally:
        if os.path.exists(temporary): os.unlink(temporary)

request = {'source':digest, 'mode':mode, 'profile':profile,
           'builder':hashlib.sha256(BUILD.encode()).hexdigest()}
layout = {'layout':2, 'profile':profile}
if created:
    write_json('.deployment.json', layout)
else:
    try:
        previous = read_json('.deployment.json')
    except FileNotFoundError:
        raise SystemExit('Existing deployment directory is unmanaged. Use a fresh pod for automatic deployment.')
    # Keep a legacy flat tree usable by existing workers and research sessions.
    # Only the new revision and the new current pointer belong to this deploy.
    if previous != layout and not (
            isinstance(previous, dict) and set(previous) == set(request)
            and previous['profile'] == profile and previous['mode'] in ('gpu','cuda','vulkan','native-cpu')
            and all(isinstance(previous[key], str) and re.fullmatch('[0-9a-f]{64}', previous[key])
                    for key in ('source','builder'))):
        raise SystemExit('Existing deployment directory has an invalid managed layout.')
    layout = previous

def open_private_directory(parent, name):
    fd = os.open(name, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
    info = os.fstat(fd)
    if info.st_uid != os.getuid() or info.st_mode & 0o077:
        raise SystemExit('Deployment subdirectory must be private and owned by this user')
    return fd

def private_directory(parent, name):
    try:
        os.mkdir(name, 0o700, dir_fd=parent)
    except FileExistsError:
        pass
    return open_private_directory(parent, name)

revision = hashlib.sha256(('\n'.join((digest, mode, profile, request['builder']))).encode('ascii')).hexdigest()
root_fd = directory_fd
revisions_fd = private_directory(root_fd, 'revisions')
try:
    directory_fd = open_private_directory(revisions_fd, revision)
except FileNotFoundError:
    # Publish the request and its private directory together. An interrupted
    # initialization cannot leave an unmarked directory at the revision path.
    with tempfile.TemporaryDirectory(prefix='.init-', dir='revisions') as staging:
        pending = Path(staging) / 'revision'
        pending.mkdir(mode=0o700)
        with (pending / '.deployment.json').open('x') as out:
            json.dump(request, out)
            out.flush()
            os.fsync(out.fileno())
        pending.rename(Path('revisions') / revision)
        os.fsync(revisions_fd)
    directory_fd = open_private_directory(revisions_fd, revision)
os.fchdir(directory_fd)
revision_record = read_json('.deployment.json')
sealed_record = dict(request, published=True)
if revision_record != request and revision_record != sealed_record:
    raise SystemExit('Existing revision does not match its deployment request')
run_lock = os.open('.run.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
info = os.fstat(run_lock)
if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
    raise SystemExit('Invalid worker lease')
cache_fd = private_directory(root_fd, 'ccache')
os.close(cache_fd)
os.environ.setdefault('CCACHE_DIR', directory + '/ccache')

def publish():
    # Atomically select only a complete revision. Interactive workers use the
    # returned concrete path even if another deployment advances this pointer.
    if revision_record != sealed_record:
        write_json('.deployment.json', sealed_record)
    target = 'revisions/' + revision
    try:
        prior = os.readlink('current', dir_fd=root_fd)
        if not re.fullmatch('revisions/[0-9a-f]{64}', prior):
            raise SystemExit('Refusing to replace an unmanaged current pointer')
    except FileNotFoundError:
        pass
    if layout.get('layout') == 2:
        for name in ('build', 'tools', '_zLab', '.worker-receipt.json'):
            alias = 'current/' + name
            try:
                os.symlink(alias, name, dir_fd=root_fd)
            except FileExistsError:
                if os.readlink(name, dir_fd=root_fd) != alias:
                    raise SystemExit('Refusing to replace an unmanaged compatibility path')
    temporary = '.current-' + os.urandom(16).hex()
    os.symlink(target, temporary, dir_fd=root_fd)
    try:
        os.replace(temporary, 'current', src_dir_fd=root_fd, dst_dir_fd=root_fd)
    finally:
        try:
            os.unlink(temporary, dir_fd=root_fd)
        except FileNotFoundError:
            pass
    os.fsync(root_fd)
    print('PUMP_LAB_SOURCE_SHA256=' + digest, flush=True)
    print('PUMP_LAB_REVISION=' + revision, flush=True)

print('PUMP_LAB_STAGE=upload', flush=True)
fd, temporary = tempfile.mkstemp(prefix='.upload-', dir='.')
try:
    checksum = hashlib.sha256()
    remaining = size
    with os.fdopen(fd, 'wb') as out:
        while remaining:
            chunk = sys.stdin.buffer.read(min(1024 * 1024, remaining))
            if not chunk: raise SystemExit('Source upload ended early. Retry deployment.')
            out.write(chunk)
            checksum.update(chunk)
            remaining -= len(chunk)
        if sys.stdin.buffer.read(1) or checksum.hexdigest() != digest:
            raise SystemExit('Source archive checksum mismatch')
        out.flush()
        os.fsync(out.fileno())
    os.replace(temporary, 'source.tar.gz')
finally:
    if os.path.exists(temporary): os.unlink(temporary)

worker = Path('build/lab/datapump-lab-worker' if profile == 'live' else 'build/lab/datapump-worker')
probe = Path('build/lab/triad_receiver_probe') if mode == 'native-cpu' else None
try:
    receipt = read_json('.worker-receipt.json')
except FileNotFoundError:
    receipt = None
relay = Path('_zLab/worker_relay.py')
def file_hash(path):
    # Hash only regular repository outputs below the selected revision.
    parent = os.dup(directory_fd)
    try:
        for part in path.parts[:-1]:
            child = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=parent)
            os.close(parent)
            parent = child
        fd = os.open(path.name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=parent)
        with os.fdopen(fd, 'rb') as source:
            info = os.fstat(source.fileno())
            if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o022:
                return None
            result = hashlib.sha256()
            for block in iter(lambda: source.read(1024 * 1024), b''):
                result.update(block)
            return result.hexdigest()
    except OSError:
        return None
    finally:
        os.close(parent)

if receipt is not None:
    if (receipt.get('request') != request or receipt.get('backend') not in allowed_backends
            or not receipt.get('worker_sha256') or file_hash(worker) != receipt['worker_sha256']
            or not receipt.get('relay_sha256') or file_hash(relay) != receipt['relay_sha256']
            or (probe and (not receipt.get('probe_sha256') or file_hash(probe) != receipt['probe_sha256']))):
        raise SystemExit('Published deployment failed its integrity check. Refusing to overwrite its files.')
    print('PUMP_LAB_STAGE=reuse', flush=True)
    subprocess.run([str(worker), '--self-check'], check=True)
    print(receipt['text'], flush=True)
    environment = dict(os.environ, PUMP_LAB_RECEIPT_MODE=receipt['backend'])
    subprocess.run([sys.executable, '-c', RESOURCES], env=environment, check=True)
    publish()
    raise SystemExit(0)

if revision_record == sealed_record:
    raise SystemExit('Published deployment receipt is missing. Refusing to overwrite its files.')

try:
    fcntl.flock(run_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    raise SystemExit('This worker revision is active. Its build cannot be repaired while in use.')

# Keep the builder identity bound to its commands. These fixed writes only
# report progress and need not invalidate a matching checked binary.
reported_build = BUILD
for stage, command in (
    ('unpack', 'tar --extract --gzip '),
    ('environment', 'cuda_root=${CUDAToolkit_ROOT:-${CUDA_HOME:-${CUDA_PATH:-}}}\n'),
    ('dependencies', '  timeout -k 10 480 sh _zLab/prepare_vulkan.sh\n'),
    ('configure', 'measure_stage configure cmake -S . -B build/lab '),
    ('compile', 'measure_stage compile cmake --build build/lab '),
    ('check', '"build/lab/$worker" --self-check\n'),
):
    reported_build = reported_build.replace(command, 'echo PUMP_LAB_STAGE=' + stage + '\n' + command)
prefixes = (b'PUMP_LAB_BUILD_JOBS=', b'PUMP_LAB_BUILD_GENERATOR=',
            b'PUMP_LAB_MEASURE=configure ', b'PUMP_LAB_MEASURE=compile ',
            b'PUMP_LAB_PROFILE=', b'PUMP_LAB_MODE=', b'PUMP_LAB_DEVICE=', b'PUMP_LAB_TORCH=',
            b'PUMP_LAB_CUDA=', b'PUMP_LAB_WORKER_SHA256=', b'PUMP_LAB_CXX_COMPILER=',
            b'PUMP_LAB_PROBE_SHA256=',
            b'PUMP_LAB_CXX_COMPILER_ID=', b'PUMP_LAB_CXX_COMPILER_VERSION=',
            b'PUMP_LAB_COMPILER_CACHE=')
summary_lines = {}
receipt_line_limit = 1024
pending = b''
oversized = False
def record(line):
    if len(line) > receipt_line_limit or not line.startswith(prefixes):
        return
    prefix = next(prefix for prefix in prefixes if line.startswith(prefix))
    summary_lines[prefix] = line.decode('utf-8', 'replace')

with tempfile.TemporaryFile() as script:
    script.write(reported_build.encode())
    script.seek(0)
    child = subprocess.Popen(['sh', '-s', '--', digest, mode, profile], stdin=script,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, pass_fds=(lock, run_lock))
while True:
    # read() fills its buffer before returning and can hold the first stage
    # of a quiet compiler or package manager for minutes.
    chunk = child.stdout.read1(4096)
    if not chunk: break
    sys.stdout.buffer.write(chunk)
    sys.stdout.buffer.flush()
    parts = chunk.split(b'\n')
    for part in parts[:-1]:
        if not oversized:
            record(pending + part)
        pending, oversized = b'', False
    if not oversized and len(pending) + len(parts[-1]) <= receipt_line_limit:
        pending += parts[-1]
    else:
        pending, oversized = b'', True
build_status = child.wait()
if build_status == 78 and mode == 'cuda': raise SystemExit('CUDA worker prerequisites were not met.')
if build_status: raise SystemExit('Remote build failed. Retry to resume the same deployment.')
if not oversized:
    record(pending)
lines = summary_lines.values()
text = '\n'.join(lines)
for receipt_line in lines:
    print(receipt_line, flush=True)
backend = next((line.partition('=')[2] for line in lines if line.startswith('PUMP_LAB_MODE=')), None)
worker_hash, relay_hash = file_hash(worker), file_hash(relay)
probe_hash = file_hash(probe) if probe else None
if (backend not in allowed_backends or not worker_hash or not relay_hash
        or (probe and (not probe_hash or summary_lines.get(b'PUMP_LAB_PROBE_SHA256=') is None))):
    raise SystemExit('Build did not return a complete deployment receipt')
receipt = {'request':request, 'text':text, 'backend':backend,
           'worker_sha256':worker_hash, 'relay_sha256':relay_hash}
if probe:
    receipt['probe_sha256'] = probe_hash
write_json('.worker-receipt.json', receipt)
publish()
'''


class DeployError(RuntimeError):
    """Deployment failed; ``uncertain`` means remote work may still be active."""

    def __init__(self, message, *, uncertain=False):
        super().__init__(message)
        self.uncertain = uncertain


def _host(value):
    if not isinstance(value, str) or len(value) > 253 or not value or value.startswith('-'):
        raise ValueError('Invalid SSH host')
    try:
        ip = ipaddress.ip_address(value)
        if ip.version != 4:
            raise ValueError('Only DNS names and IPv4 addresses are supported')
        return value
    except ValueError:
        pass
    # Limit to DNS labels rather than accepting SSH option, shell or SCP syntax.
    if not all(re.fullmatch(r'[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?', label)
               for label in value.split('.')):
        raise ValueError('Invalid SSH host')
    return value


def _user(value):
    if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_-]{0,31}', value):
        raise ValueError('Invalid SSH username')
    return value


def _path(value, label):
    path = Path(value).expanduser().resolve(strict=True)
    if not path.is_file() or any(c.isspace() or c in '\\"\'%$ ' for c in str(path)):
        raise ValueError(f'{label} must be an existing regular file with no whitespace, quotes, backslashes, dollar or percent signs in its path')
    return str(path)


def _connection(host, user, port, known_hosts, identity):
    host, user = _host(host), _user(user)
    if isinstance(port, bool) or not isinstance(port, int) or not 1 <= port <= 65535:
        raise ValueError('SSH port must be in 1–65535')
    known = _path(known_hosts, 'known_hosts')
    options = ['-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
               '-o', 'UserKnownHostsFile=' + known, '-o', 'GlobalKnownHostsFile=/dev/null',
               '-o', 'ConnectTimeout=15', '-o', 'ClearAllForwardings=yes',
               '-o', 'ForwardAgent=no']
    if identity is not None:
        options += ['-o', 'IdentitiesOnly=yes', '-i', _path(identity, 'SSH identity')]
    return host, user, options


def _stage_reader(callback):
    """Recognize short, complete and allowlisted progress lines."""
    pending = b''
    oversized = False
    reported = set()
    prefix = b'PUMP_LAB_STAGE='
    jobs_prefix = b'PUMP_LAB_BUILD_JOBS='
    build_jobs = None
    limit = len(prefix) + max(map(len, _STAGE_MESSAGES)) + 1

    def read(chunk):
        nonlocal pending, oversized, build_jobs
        lines = chunk.split(b'\n')
        for line in lines[:-1]:
            if not oversized and len(pending) + len(line) <= limit:
                stage = (pending + line).removesuffix(b'\r')
                if stage.startswith(jobs_prefix):
                    value = stage[len(jobs_prefix):]
                    if value.isdigit() and 1 <= len(value) <= 3 and int(value) <= 512:
                        build_jobs = int(value)
                elif stage.startswith(prefix):
                    code = stage[len(prefix):]
                    if code in _STAGE_MESSAGES and code not in reported:
                        reported.add(code)
                        message = _STAGE_MESSAGES[code]
                        if code == b'compile' and build_jobs is not None:
                            message += f' ({build_jobs} parallel jobs)'
                        callback(message)
            pending, oversized = b'', False
        trailing = lines[-1]
        if not oversized and len(pending) + len(trailing) <= limit:
            pending += trailing
        else:
            pending, oversized = b'', True

    return read


def _bounded_process(argv, input_data, timeout, *, on_progress=None, cancelled=None):
    """Drain a child without retaining more than 4 KiB of untrusted output."""
    if cancelled is not None and cancelled.is_set():
        raise DeployError('Deployment cancelled before starting SSH.')
    child = subprocess.Popen(argv, stdin=subprocess.PIPE if input_data is not None else subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    tail = bytearray()
    pending = memoryview(input_data or b'')
    deadline = time.monotonic() + timeout
    output_eof = False
    read_progress = _stage_reader(on_progress) if on_progress is not None else None
    try:
        with selectors.DefaultSelector() as selector:
            os.set_blocking(child.stdout.fileno(), False)
            selector.register(child.stdout, selectors.EVENT_READ)
            if child.stdin is not None:
                os.set_blocking(child.stdin.fileno(), False)
                if pending:
                    selector.register(child.stdin, selectors.EVENT_WRITE)
                else:
                    child.stdin.close()
            while True:
                if cancelled is not None and cancelled.is_set():
                    raise DeployError('Deployment cancelled. The pod may still be running.', uncertain=True)
                left = deadline - time.monotonic()
                if left <= 0:
                    raise subprocess.TimeoutExpired(argv, timeout)
                if child.poll() is not None and output_eof:
                    if pending and child.returncode == 0:
                        raise DeployError('Remote input closed before the build script was sent; remote state is uncertain.',
                                          uncertain=True)
                    break
                # A subprocess inheriting stdout must not keep us past timeout.
                for key, mask in selector.select(min(left, .2)):
                    if key.fileobj is child.stdout:
                        try:
                            chunk = os.read(child.stdout.fileno(), 65536)
                        except BlockingIOError:
                            continue
                        if chunk:
                            if read_progress is not None:
                                read_progress(chunk)
                            tail.extend(chunk)
                            if len(tail) > 4096:
                                del tail[:-4096]
                        else:
                            selector.unregister(child.stdout)
                            output_eof = True
                    else:
                        try:
                            count = os.write(child.stdin.fileno(), pending[:65536])
                            pending = pending[count:]
                        except BlockingIOError:
                            continue
                        except BrokenPipeError:
                            # A rejected resume can exit before consuming the
                            # archive. Drain its bounded explanation and status.
                            selector.unregister(child.stdin)
                            child.stdin.close()
                            continue
                        if not pending:
                            selector.unregister(child.stdin)
                            child.stdin.close()
            return child.returncode, bytes(tail)
    finally:
        if child.poll() is None:
            child.kill()
        child.wait()
        if child.stdin is not None and not child.stdin.closed:
            child.stdin.close()
        child.stdout.close()


def _run(argv, *, stage, timeout, runner, input_data=None, on_progress=None, cancelled=None):
    try:
        if runner is None:
            code, raw_tail = _bounded_process(argv, input_data, timeout, on_progress=on_progress, cancelled=cancelled)
        else:
            # Injected runners are only used by local tests.
            with tempfile.TemporaryFile() as output:
                result = runner(argv, input=input_data, stdout=output, stderr=subprocess.STDOUT,
                                timeout=timeout, check=False)
                output.seek(0, os.SEEK_END)
                output.seek(max(0, output.tell() - 4096))
                raw_tail, code = output.read(), result.returncode
        tail = raw_tail.decode('utf-8', 'replace')
        tail = ''.join(c if c in '\n\t' or ord(c) >= 32 and not 127 <= ord(c) < 160 else '?' for c in tail)
        if code:
            raise DeployError(f'{stage} failed (exit {code}): {tail.strip()}', uncertain=code == 255)
        return tail.strip()
    except subprocess.TimeoutExpired as exc:
        raise DeployError(f'{stage} timed out; remote work or files may remain. Inspect the pod before retrying.', uncertain=True) from exc
    except OSError as exc:
        raise DeployError(f'{stage} could not start: {exc.strerror or type(exc).__name__}') from exc


def deployment_revision(digest, mode, profile, builder):
    return hashlib.sha256(('\n'.join((digest, mode, profile, builder))).encode('ascii')).hexdigest()


def deploy(host, user, port, known_hosts, *, mode=DEFAULT_MODE, profile='simulation', identity=None,
           timeout=DEFAULT_TIMEOUT, runner=None, on_output=None, on_progress=None, cancelled=None):
    """Bundle, upload and build; return the expected remote worker path.

    ``runner`` is injectable for tests. Network commands are executed only by an
    explicit call; this function does not manage or terminate the paid pod.
    ``on_progress`` receives only fixed stage descriptions. Setting the
    optional ``cancelled`` event stops the local SSH process, and the controller
    still owns termination of the pod.
    """
    host, user, options = _connection(host, user, port, known_hosts, identity)
    if mode not in BUILD_MODES:
        raise ValueError('Build mode must be cpu, cuda, vulkan, gpu or native-cpu')
    if mode == 'cpu':
        mode = DEFAULT_MODE
    if profile not in ('simulation', 'live'):
        raise ValueError('Build profile must be simulation or live')
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('Timeout must be finite and positive')
    deadline = time.monotonic() + timeout

    def remaining():
        if cancelled is not None and cancelled.is_set():
            raise DeployError('Deployment cancelled before starting SSH.')
        seconds = deadline - time.monotonic()
        if seconds <= 0:
            raise DeployError('Deployment deadline reached; inspect the pod before retrying.')
        return seconds

    directory = LIVE_REMOTE_DIR if profile == 'live' else REMOTE_DIR
    ssh = ['ssh', '-F', '/dev/null', '-T', '-p', str(port), '-l', user, *options, host]
    if cancelled is not None and cancelled.is_set():
        raise DeployError('Deployment cancelled before starting SSH.')
    if on_progress is not None:
        on_progress('Preparing the source bundle…')
    with tempfile.TemporaryDirectory(prefix='datapump-lab-') as staging:
        archive = Path(staging) / 'source.tar.gz'
        if mode == 'native-cpu':
            bundle.create_bundle(ROOT, archive, cpu_research=True)
        else:
            bundle.create_bundle(ROOT, archive)
        payload = archive.read_bytes()
        digest = hashlib.sha256(payload).hexdigest()
        # The command contains only our fixed wrapper, checked mode/profile,
        # and locally computed digest/length. The archive travels over stdin.
        script = 'BUILD = ' + repr(REMOTE_BUILD) + '\nRESOURCES = ' + repr(RESOURCE_RECEIPT) + '\n' + REMOTE_DEPLOY
        command = f'python3 -c {shlex.quote(script)} {digest} {mode} {profile} {len(payload)}'
        if on_progress is not None:
            on_progress('Connecting to the compute host for worker setup…')
        output = _run(ssh + [command], stage='Deploying the remote worker',
                      timeout=remaining(), runner=runner, input_data=payload,
                      on_progress=on_progress, cancelled=cancelled)
        if on_output is not None:
            on_output(output)
    worker = 'datapump-lab-worker' if profile == 'live' else 'datapump-worker'
    revision = deployment_revision(digest, mode, profile, hashlib.sha256(REMOTE_BUILD.encode()).hexdigest())
    return directory + '/revisions/' + revision + '/build/lab/' + worker


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--user', default='root')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--known-hosts', required=True, help='file containing the independently verified SSH host key')
    parser.add_argument('--identity', help='optional local SSH private-key path')
    parser.add_argument('--mode', choices=BUILD_MODES, default=DEFAULT_MODE,
                        help='gpu (default) and its legacy cpu alias build a GPU-capable worker. '
                             'native-cpu installs Ubuntu build prerequisites and builds a CPU-only worker; '
                             'cuda and vulkan require their named backend')
    parser.add_argument('--profile', choices=('simulation', 'live'), default='simulation',
                        help='worker profile; live audio is experimental and separately unqualified')
    parser.add_argument('--timeout', type=float, default=DEFAULT_TIMEOUT,
                        help='shared deployment time budget in seconds; each remote stage gets the remaining time')
    args = parser.parse_args(argv)
    try:
        path = deploy(args.host, args.user, args.port, args.known_hosts, mode=args.mode,
                      profile=args.profile, identity=args.identity, timeout=args.timeout, on_output=print)
    except (ValueError, OSError, bundle.BundleError, DeployError) as exc:
        print(f'lab deploy: {exc}', file=sys.stderr)
        return 1
    if args.mode == 'native-cpu':
        print(f'Built {path} and the native CPU receiver probe.')
    else:
        print(f'Built {path}. Confirm the actual device/backend before claiming GPU use.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

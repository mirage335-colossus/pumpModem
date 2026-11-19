#!/usr/bin/env python3
"""Deploy a lab worker over authenticated direct SSH to an unused pod directory.

The SSH server host key must already be verified in ``known_hosts``. This helper
never performs trust-on-first-use or starts a pod. It creates an exclusive fresh
profile-specific directory (``/workspace/pump-lab`` for simulation or
``/workspace/pump-lab-live`` for live); it will not overwrite an existing deployment.
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
import subprocess
import time
import sys
import tempfile

import bundle

ROOT = Path(__file__).resolve().parent.parent
REMOTE_DIR = '/workspace/pump-lab'
LIVE_REMOTE_DIR = '/workspace/pump-lab-live'
DEFAULT_TIMEOUT = 3600

# All text interpreted by the remote shell is fixed here or constructed from a
# locally computed SHA-256 digest. Provider data and user paths never enter it.
REMOTE_BUILD = r'''set -eu
mode=$2
profile=$3
case "$mode" in cpu|cuda) ;; *) exit 2 ;; esac
case "$profile" in
  simulation) directory=/workspace/pump-lab ;;
  live) directory=/workspace/pump-lab-live ;;
  *) exit 2 ;;
esac
cd "$directory"
printf '%s  %s\n' "$1" source.tar.gz | sha256sum -c -
tar --extract --gzip --file source.tar.gz --no-same-owner --no-same-permissions
set -- -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DDATAPUMP_BUILD_GUI=OFF \
  -DDATAPUMP_BUILD_CLI=ON -DDATAPUMP_BUILD_WEB_WORKER=ON -DDATAPUMP_PORTABLE=OFF \
  -DDATAPUMP_COMPILER_CACHE=OFF -DDATAPUMP_TRIAD_VULKAN=OFF
if [ "$mode" = cuda ]; then
  python3 -c 'import torch; assert torch.cuda.is_available() and torch.compiled_with_cxx11_abi(); print(torch.__version__, torch.version.cuda, torch.cuda.get_device_name())'
  torch_prefix=$(python3 -c 'from torch.utils import cmake_prefix_path; print(cmake_prefix_path)')
  native_sm=$(python3 -c 'import torch; print("".join(map(str,torch.cuda.get_device_capability())))')
  export TORCH_CUDA_ARCH_LIST="$(python3 -c 'import torch; print(".".join(map(str,torch.cuda.get_device_capability())))')"
  set -- "$@" -DDATAPUMP_TRIAD_TENSOR=ON -DDATAPUMP_TRIAD_TENSOR_REQUIRE_CUDA=ON \
    "-DCMAKE_PREFIX_PATH=$torch_prefix" "-DCMAKE_CUDA_ARCHITECTURES=$native_sm"
else
  set -- "$@" -DDATAPUMP_TRIAD_TENSOR=OFF
fi
worker=datapump-worker
if [ "$profile" = live ]; then
  set -- "$@" "-DCMAKE_PROJECT_DataPump_INCLUDE=$directory/_zLab/audio/inject.cmake"
  worker=datapump-lab-worker
fi
cmake -S . -B build/lab "$@"
cmake --build build/lab --parallel 4 --target "$worker"
"build/lab/$worker" --self-check
printf 'PUMP_LAB_PROFILE=%s\n' "$profile"
printf 'PUMP_LAB_MODE=%s\n' "$mode"
if [ "$mode" = cuda ]; then
  python3 -c 'import torch; print("PUMP_LAB_DEVICE=" + str(torch.cuda.get_device_name())); print("PUMP_LAB_TORCH=" + str(torch.__version__)); print("PUMP_LAB_CUDA=" + str(torch.version.cuda))'
else
  printf 'PUMP_LAB_DEVICE=CPU (processor not identified)\nPUMP_LAB_TORCH=not requested\nPUMP_LAB_CUDA=not requested\n'
fi
worker_hash=$(sha256sum "build/lab/$worker")
printf 'PUMP_LAB_WORKER_SHA256=%s\n' "${worker_hash%% *}"
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
    if os.environ.get('PUMP_LAB_RECEIPT_MODE') != 'cuda':
        print('PUMP_LAB_GPU_SCOPE=not requested')
        return
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


def _bounded_process(argv, input_data, timeout):
    """Drain a child without retaining more than 4 KiB of untrusted output."""
    child = subprocess.Popen(argv, stdin=subprocess.PIPE if input_data is not None else subprocess.DEVNULL,
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=0)
    tail = bytearray()
    pending = memoryview(input_data or b'')
    deadline = time.monotonic() + timeout
    output_eof = False
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
                left = deadline - time.monotonic()
                if left <= 0:
                    raise subprocess.TimeoutExpired(argv, timeout)
                if child.poll() is not None and output_eof:
                    if pending:
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
                        except BrokenPipeError as exc:
                            raise DeployError('Remote input closed before the build script was sent; remote state is uncertain.',
                                              uncertain=True) from exc
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


def _run(argv, *, stage, timeout, runner, input_data=None):
    try:
        if runner is None:
            code, raw_tail = _bounded_process(argv, input_data, timeout)
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


def deploy(host, user, port, known_hosts, *, mode='cpu', profile='simulation', identity=None,
           timeout=DEFAULT_TIMEOUT, runner=None, on_output=None):
    """Bundle, upload and build; return the expected remote worker path.

    ``runner`` is injectable for tests. Network commands are executed only by an
    explicit call; this function does not manage or terminate the paid pod.
    """
    host, user, options = _connection(host, user, port, known_hosts, identity)
    if mode not in ('cpu', 'cuda'):
        raise ValueError('Build mode must be cpu or cuda')
    if profile not in ('simulation', 'live'):
        raise ValueError('Build profile must be simulation or live')
    if isinstance(timeout, bool) or not isinstance(timeout, (int, float)) or not math.isfinite(timeout) or timeout <= 0:
        raise ValueError('Timeout must be finite and positive')
    directory = LIVE_REMOTE_DIR if profile == 'live' else REMOTE_DIR
    ssh = ['ssh', '-F', '/dev/null', '-T', '-p', str(port), '-l', user, *options, host]
    scp = ['scp', '-F', '/dev/null', '-P', str(port), *options]
    with tempfile.TemporaryDirectory(prefix='datapump-lab-') as staging:
        archive = Path(staging) / 'source.tar.gz'
        bundle.create_bundle(ROOT, archive)
        digest = hashlib.sha256(archive.read_bytes()).hexdigest()
        _run(ssh + ['mkdir', '-m', '700', '--', directory], stage='Creating the deployment directory',
             timeout=timeout, runner=runner)
        _run(scp + [str(archive), f'{user}@{host}:{directory}/source.tar.gz'],
             stage='Uploading the source bundle', timeout=timeout, runner=runner)
        # Values appended to the shell command are validated choices and hex
        # digest, never a provider-supplied string.
        command = f'sh -s -- {digest} {mode} {profile}'
        output = _run(ssh + [command], stage='Verifying and building the remote worker',
                      timeout=timeout, runner=runner, input_data=REMOTE_BUILD.encode('utf-8'))
        if on_output is not None:
            on_output(output)
    worker = 'datapump-lab-worker' if profile == 'live' else 'datapump-worker'
    return directory + '/build/lab/' + worker


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--user', default='root')
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--known-hosts', required=True, help='file containing the independently verified SSH host key')
    parser.add_argument('--identity', help='optional local SSH private-key path')
    parser.add_argument('--mode', choices=('cpu', 'cuda'), default='cpu')
    parser.add_argument('--profile', choices=('simulation', 'live'), default='simulation',
                        help='worker profile; live audio is experimental and separately unqualified')
    parser.add_argument('--timeout', type=float, default=DEFAULT_TIMEOUT, help='timeout in seconds per SSH/SCP command')
    args = parser.parse_args(argv)
    try:
        path = deploy(args.host, args.user, args.port, args.known_hosts, mode=args.mode,
                      profile=args.profile, identity=args.identity, timeout=args.timeout, on_output=print)
    except (ValueError, OSError, bundle.BundleError, DeployError) as exc:
        print(f'lab deploy: {exc}', file=sys.stderr)
        return 1
    print(f'Built {path}. Confirm the actual device/backend before claiming GPU use.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())

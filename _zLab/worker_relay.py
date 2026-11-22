#!/usr/bin/env python3
"""Relay DPW1 frames to a sandboxed native worker through anonymous pipes.

Run on the worker host: ``python3 worker_relay.py /path/to/datapump-worker``.
For SSH use a non-PTY channel (for example ``ssh -T``) and connect the local
client's binary input and output to this process. Standard error is diagnostic;
standard output contains only DPW1 frames. The worker is always started in
simulation mode unless ``--live`` is explicitly supplied; the host file-service
pipe is opt-in. Pass ``--prefer-gpu`` to start with GPU selected while leaving
it available as a runtime choice, or ``--require-gpu`` for strict qualification.
Omit both for CPU jobs. This is a transport adapter, not
authentication or a provider launcher. A separate local adapter is required for
physical audio and its dated sample transport.
Each direction accepts one frame of at most 8 MiB at a time and propagates
backpressure. EOF is transport shutdown, never a receiver completion event.
With --file-channel, stdin carries DPL1 + little-endian channel and byte count
+ one complete DPW1 frame; channel 0 carries ordinary UI and, in live mode,
the lab's dated audio messages. Channel 1 carries trusted file imports. Only an
authenticated, trusted lab host may produce this envelope.
The Linux worker process group is stopped on shutdown; a descendant which
explicitly escapes that group is outside this relay's process-control boundary.
"""

import argparse
import json
import os
from pathlib import Path
import re
import selectors
import signal
import stat
import struct
import subprocess
import sys
import time

MAX_PAYLOAD = 8 * 1024 * 1024
HEADER_SIZE = 12
CHUNK = 65536
SHUTDOWN_SECONDS = 10
DEPLOYMENTS = {
    False: Path("/workspace/pump-lab/build/lab/datapump-worker"),
    True: Path("/workspace/pump-lab-live/build/lab/datapump-lab-worker"),
}


class RelayError(Exception):
    pass


class Direction:
    """Incrementally validate a frame before forwarding it without changes."""

    def __init__(self, source, destination, label, allowed=None):
        self.source = source
        self.destination = destination
        self.label = label
        self.allowed = allowed
        self.buffer = bytearray()
        self.total = None
        self.sent = 0
        self.eof = False

    @property
    def ready(self):
        return self.total is not None and len(self.buffer) == self.total

    def read(self):
        need = (self.total if self.total is not None else HEADER_SIZE) - len(self.buffer)
        try:
            data = os.read(self.source, min(CHUNK, need))
        except BlockingIOError:
            return
        if not data:
            if self.buffer:
                raise RelayError(f"{self.label}: truncated frame")
            self.eof = True
            return
        self.buffer.extend(data)
        if self.total is None and len(self.buffer) == HEADER_SIZE:
            if self.buffer[:4] != b"DPW1":
                raise RelayError(f"{self.label}: invalid frame magic")
            size = struct.unpack_from("<I", self.buffer, 8)[0]
            if size > MAX_PAYLOAD:
                raise RelayError(f"{self.label}: frame exceeds 8 MiB")
            self.total = HEADER_SIZE + size
            if self.allowed is not None:
                kind = struct.unpack_from("<I", self.buffer, 4)[0]
                if kind not in self.allowed:
                    raise RelayError(f"{self.label}: unsupported frame type")

    def write(self):
        try:
            n = os.write(self.destination, memoryview(self.buffer)[self.sent:self.sent + CHUNK])
        except BlockingIOError:
            return
        if n == 0:
            raise RelayError(f"{self.label}: output closed")
        self.sent += n
        if self.sent == self.total:
            self.buffer = bytearray()
            self.total = None
            self.sent = 0


class MuxDirection(Direction):
    """DPL1 envelope routes one validated DPW1 frame to an anonymous pipe."""

    def __init__(self, source, ordinary, trusted, live=False):
        super().__init__(source, ordinary, "input")
        self.ordinary, self.trusted = ordinary, trusted
        self.live = live

    def read(self):
        need = (self.total if self.total is not None else HEADER_SIZE) - len(self.buffer)
        try:
            data = os.read(self.source, min(CHUNK, need))
        except BlockingIOError:
            return
        if not data:
            if self.buffer:
                raise RelayError("input: truncated multiplexed frame")
            self.eof = True
            return
        self.buffer.extend(data)
        if self.total is None and len(self.buffer) == HEADER_SIZE:
            magic, channel, size = struct.unpack("<4sII", self.buffer)
            if magic != b"DPL1" or channel not in (0, 1) or not HEADER_SIZE <= size <= MAX_PAYLOAD + HEADER_SIZE:
                raise RelayError("input: invalid multiplex envelope")
            self.destination = self.ordinary if channel == 0 else self.trusted
            self.channel = channel
            self.total = HEADER_SIZE + size
        if self.total is not None and len(self.buffer) == self.total:
            magic, kind, size = struct.unpack_from("<4sII", self.buffer, HEADER_SIZE)
            allowed = ((1, 2, 15, *range(220, 230)) if self.live else (1, 2)) if self.channel == 0 else (2, 10, 11, 12)
            if magic != b"DPW1" or size > MAX_PAYLOAD or size + 2 * HEADER_SIZE != self.total or kind not in allowed:
                raise RelayError("input: invalid channel or embedded frame")
            # Only the validated DPW1 message, never its authority envelope,
            # is sent down the worker's separate inherited anonymous pipe.
            del self.buffer[:HEADER_SIZE]
            self.total -= HEADER_SIZE


def _wait_for_exit(pid, timeout, stop_requested=lambda: False):
    """Observe exit without reaping, reserving the process-group ID for cleanup."""
    deadline = time.monotonic() + timeout
    while True:
        if stop_requested():
            raise RelayError("terminated")
        if os.waitid(os.P_PID, pid, os.WEXITED | os.WNOHANG | os.WNOWAIT):
            return True
        if time.monotonic() >= deadline:
            return False
        time.sleep(0.02)


def _stop_group(child):
    # Do not poll/reap the leader before signaling: its PID reserves the group
    # identity, including when it has exited but descendants still hold pipes.
    try:
        os.killpg(child.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        _wait_for_exit(child.pid, 2)
    finally:
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        child.wait()


def gpu_environment(worker, live):
    """Use the headless Vulkan driver selected by this private lab deployment."""
    deployed = DEPLOYMENTS[live]
    if os.fspath(worker) != str(deployed):
        return None
    manifest = deployed.parent / "nvidia-headless-icd.json"
    try:
        fd = os.open(manifest, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    except FileNotFoundError:
        return None  # The CUDA build uses the default driver environment.
    with os.fdopen(fd, "rb") as source:
        entry = os.fstat(fd)
        if (not stat.S_ISREG(entry.st_mode) or entry.st_uid != os.geteuid()
                or entry.st_mode & 0o077 or not 0 < entry.st_size <= 1024):
            raise RelayError("Unsafe deployment Vulkan manifest")
        deployment = deployed.parents[2]  # deploy.py creates it with mode 0700.
        for folder in (deployment, deployed.parents[1], deployed.parent):
            info = folder.lstat()
            if (not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid()
                    or (folder == deployment and info.st_mode & 0o077)):
                raise RelayError("Unsafe deployment Vulkan directory")
        try:
            contents = json.loads(source.read(1025))
            api_version = contents["ICD"]["api_version"]
            format_version = contents["file_format_version"]
            if (not re.fullmatch(r"1\.\d{1,3}\.\d{1,3}", api_version)
                    or format_version not in ("1.0.0", "1.0.1")
                    or contents != {"file_format_version": format_version, "ICD": {
                        "library_path": "libEGL_nvidia.so.0", "api_version": api_version}}):
                raise ValueError()
        except (KeyError, TypeError, ValueError, RecursionError):
            raise RelayError("Invalid deployment Vulkan manifest") from None
    environment = dict(os.environ, VK_DRIVER_FILES=str(manifest), VK_ICD_FILENAMES=str(manifest))
    vendors = deployed.parent / "nvidia-opencl-vendors"
    try:
        info = vendors.lstat()
    except FileNotFoundError:
        return environment  # Existing Vulkan deployments can lack this optional manifest.
    if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
        raise RelayError("Unsafe deployment OpenCL vendor directory")
    if set(vendors.glob("*.icd")) != {vendors / "nvidia.icd"}:
        raise RelayError("Unexpected deployment OpenCL vendor manifest")
    fd = os.open(vendors / "nvidia.icd", os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, "rb") as source:
        entry = os.fstat(fd)
        expected = b"libnvidia-opencl.so.1\n"
        if (not stat.S_ISREG(entry.st_mode) or entry.st_uid != os.geteuid() or entry.st_mode & 0o077
                or source.read(len(expected) + 1) != expected):
            raise RelayError("Invalid deployment OpenCL vendor manifest")
    for variable in ("OCL_ICD_FILENAMES", "OPENCL_LAYERS", "OPENCL_LAYER_PATH"):
        environment.pop(variable, None)
    return dict(environment, OPENCL_VENDOR_PATH=str(vendors), OCL_ICD_VENDORS=str(vendors))


def relay(worker, stop_requested=lambda: False, require_gpu=False, file_channel=False, live=False,
          prefer_gpu=False):
    # Explicit pipes ensure the worker's no-socket/anonymous-pipe checks remain
    # meaningful even when this relay's own stdio is an SSH channel.
    command = [worker] + ([] if live else ["--simulation-only"])
    environment = None
    if require_gpu:
        command.append("--require-gpu")
    elif prefer_gpu:
        command.append("--prefer-gpu")
    if require_gpu or prefer_gpu:
        environment = gpu_environment(worker, live)
    file_read = file_write = None
    if file_channel:
        file_read, file_write = os.pipe()
        command.extend(["--file-events-fd", str(file_read)])
    try:
        child = subprocess.Popen(command, stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=None, bufsize=0,
                                 pass_fds=(file_read,) if file_channel else (),
                                 close_fds=True, start_new_session=True, env=environment)
    except BaseException:
        if file_write is not None:
            os.close(file_write)
        raise
    finally:
        if file_read is not None:
            os.close(file_read)
    try:
        # All operations after successful Popen, including fd setup, are inside
        # the cleanup region. CLI signal handlers only set a flag, so they cannot
        # interrupt the tiny interval between Popen returning and entering here.
        incoming = (MuxDirection(sys.stdin.fileno(), child.stdin.fileno(), file_write, live=live)
                    if file_channel else Direction(sys.stdin.fileno(), child.stdin.fileno(), "input",
                                                  allowed=(1, 2, 15, *range(220, 230)) if live else None))
        outgoing = Direction(child.stdout.fileno(), sys.stdout.fileno(), "worker output")
        fds = [incoming.source, incoming.destination, outgoing.source, outgoing.destination]
        if file_channel:
            fds.append(file_write)
        for fd in fds:
            os.set_blocking(fd, False)
        input_closed_at = None
        worker_exited_at = None
        while True:
            if stop_requested():
                raise RelayError("terminated")
            if incoming.eof and not child.stdin.closed:
                child.stdin.close()
                if file_write is not None:
                    os.close(file_write)
                    file_write = None
                input_closed_at = time.monotonic()
            if outgoing.eof:
                if incoming.buffer:
                    raise RelayError("worker stopped before input frame was forwarded")
                break
            if input_closed_at is not None and time.monotonic() - input_closed_at > SHUTDOWN_SECONDS:
                raise RelayError("worker did not stop after input closed")
            if os.waitid(os.P_PID, child.pid, os.WEXITED | os.WNOHANG | os.WNOWAIT):
                if worker_exited_at is None:
                    worker_exited_at = time.monotonic()
                elif outgoing.ready:
                    # A slow transport may still be draining an intact frame.
                    worker_exited_at = time.monotonic()
                elif time.monotonic() - worker_exited_at > 2:
                    raise RelayError("worker exited without closing output")
            # poll(2) also supports ordinary files used for controlled replay.
            with selectors.PollSelector() as selector:
                for direction in (incoming, outgoing):
                    if not direction.eof and not direction.ready:
                        selector.register(direction.source, selectors.EVENT_READ, (direction, "read"))
                    if direction.ready:
                        selector.register(direction.destination, selectors.EVENT_WRITE, (direction, "write"))
                for key, _ in selector.select(0.2):
                    direction, operation = key.data
                    getattr(direction, operation)()
        if not _wait_for_exit(child.pid, SHUTDOWN_SECONDS, stop_requested):
            raise RelayError("worker closed output but did not exit")
    finally:
        try:
            _stop_group(child)
        finally:
            child.stdin.close()
            child.stdout.close()
            if file_write is not None:
                os.close(file_write)
    if child.returncode:
        raise RelayError(f"worker exited with status {child.returncode}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-gpu", action="store_true",
                        help="require the native GPU receiver policy")
    parser.add_argument("--prefer-gpu", action="store_true",
                        help="start with GPU selected while allowing runtime opt-out")
    parser.add_argument("--live", action="store_true",
                        help="start the worker for live audio, not simulation")
    parser.add_argument("--file-channel", action="store_true",
                        help="enable DPL1 multiplex and a separate trusted file pipe")
    parser.add_argument("worker", help="path to the native datapump-worker executable")
    args = parser.parse_args()

    stopping = False

    def terminate(_signum, _frame):
        nonlocal stopping
        stopping = True

    signal.signal(signal.SIGTERM, terminate)
    signal.signal(signal.SIGINT, terminate)
    try:
        if args.require_gpu and args.prefer_gpu:
            parser.error("--require-gpu and --prefer-gpu are mutually exclusive")
        relay(args.worker, lambda: stopping, require_gpu=args.require_gpu,
              file_channel=args.file_channel, live=args.live, prefer_gpu=args.prefer_gpu)
    except (RelayError, OSError) as exc:
        print(f"worker relay: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("worker relay: interrupted", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())

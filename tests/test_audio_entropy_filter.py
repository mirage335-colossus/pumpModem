"""Exercise the production helper's Linux process isolation using a test-only capture stub."""
import os
import select
import struct
import subprocess
import sys


def read_exact(pipe, count, timeout=10):
    result = bytearray()
    while len(result) < count:
        ready, _, _ = select.select([pipe], [], [], timeout)
        if not ready:
            raise AssertionError("helper did not complete its bounded handshake")
        chunk = os.read(pipe.fileno(), count - len(result))
        if not chunk:
            raise AssertionError("helper exited before completing its handshake")
        result.extend(chunk)
    return bytes(result)


def launch(path):
    return subprocess.Popen([path, "--device", "test", "--exclusive", "0", "--parent", str(os.getpid())],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    path = sys.argv[1]
    challenge = os.urandom(16)
    process = launch(path)
    try:
        process.stdin.write(challenge)
        process.stdin.flush()
        assert read_exact(process.stdout, 16) == challenge
        process.stdin.write(b"\x01")
        process.stdin.flush()
        process.stdin.close()
        magic, rate, count = struct.unpack("<4sII", read_exact(process.stdout, 12))
        assert (magic, rate, count) == (b"DPA1", 48000, 2)
        assert struct.unpack("<2f", read_exact(process.stdout, 8)) == (0.125, -0.25)
        assert process.wait(timeout=10) == 0, process.stderr.read().decode(errors="replace")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)

    # A child whose parent withdraws release must exit without opening audio.
    process = launch(path)
    try:
        process.stdin.write(challenge)
        process.stdin.flush()
        assert read_exact(process.stdout, 16) == challenge
        process.stdin.close()
        assert process.wait(timeout=10) != 0
        assert process.stdout.read() == b""
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=10)


if __name__ == "__main__":
    main()

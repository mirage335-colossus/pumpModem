#!/usr/bin/env python3
"""Real inherited-pipe worker checks; never start a web server or socket."""
import json
import os
import select
import struct
import subprocess
import sys
import time
import tempfile
from pathlib import Path

worker = sys.argv[1]


def frame(kind, payload=b""):
    return b"DPW1" + struct.pack("<II", kind, len(payload)) + payload


def read_frame(process, deadline=8):
    end = time.monotonic() + deadline

    def exact(size):
        result = bytearray()
        while len(result) < size:
            remaining = end - time.monotonic()
            assert remaining > 0, "worker output deadline"
            assert select.select([process.stdout], [], [], remaining)[0], "worker output stalled"
            part = os.read(process.stdout.fileno(), size - len(result))
            assert part, "worker ended before expected output"
            result.extend(part)
        return bytes(result)

    header = exact(12)
    assert header[:4] == b"DPW1"
    kind, size = struct.unpack("<II", header[4:])
    assert size <= 8 * 1024 * 1024
    return kind, exact(size)


def launch():
    return subprocess.Popen([worker, "--simulation"], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)


checked = subprocess.run([worker, "--self-check"], stdin=subprocess.DEVNULL, capture_output=True, timeout=10)
assert checked.returncode == 0, checked.stderr
assert b"No-socket worker boundary active" in checked.stdout

# Redirected files are rejected: the worker cannot become an implicit request
# server or use socket-backed CGI/FastCGI streams by accepting generic fd 0/1.
with open(os.devnull, "wb") as sink:
    rejected = subprocess.run([worker], stdin=subprocess.DEVNULL, stdout=sink, stderr=subprocess.PIPE, timeout=10)
assert rejected.returncode != 0 and b"anonymous pipes" in rejected.stderr

process = launch()
try:
    kind, value = read_frame(process)
    assert kind == 101
    snapshot = json.loads(value)
    assert isinstance(snapshot, dict)
    # Fragments are accepted without losing the framing boundary.
    ping = frame(15, struct.pack("<Qd", 7, 123.5))
    for part in [ping[:1], ping[1:7], ping[7:13], ping[13:]]:
        process.stdin.write(part)
    while True:
        kind, value = read_frame(process)
        if kind == 108:
            nonce, client, received, sent = struct.unpack("<Qddd", value)
            assert nonce == 7 and client == 123.5 and sent >= received > 0
            break
    # File-service authority is not a flag that ordinary browser events gain.
    process.stdin.write(frame(13))
    while True:
        kind, value = read_frame(process)
        if kind == 103:
            size = struct.unpack("<I", value[:4])[0]
            assert value[4:4+size] == b"file export requires the host file channel"
            break
    process.stdin.close()
    while process.poll() is None:
        try:
            read_frame(process)
        except AssertionError:
            break
    assert process.wait(timeout=10) == 0, process.stderr.read()
finally:
    if process.poll() is None:
        process.kill()
        process.wait()

# Host file paths cross only the separate inherited capability pipe. Perform
# a real C++ modem round trip, then verify the explicit server-side save.
def string(value):
    data = value.encode("utf-8")
    return struct.pack("<I", len(data)) + data

with tempfile.TemporaryDirectory(prefix="datapump-file-contract-") as directory:
    fixture = bytes([0, 1, 2, 3, 255, 192, 128, 68, 97, 116, 97, 80, 117, 109, 112, 10])
    source, destination = Path(directory) / "a.bin", Path(directory) / "received.bin"
    source.write_bytes(fixture)
    file_read, file_write = os.pipe()
    process = subprocess.Popen([worker, "--simulation", "--file-events-fd", str(file_read)],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               pass_fds=(file_read,), bufsize=0)
    os.close(file_read)
    try:
        snapshot = None
        def update():
            global snapshot
            kind, value = read_frame(process, 60)
            if kind == 101:
                snapshot = json.loads(value)
            elif kind == 103:
                raise AssertionError("C++ worker rejected fixture: " + value[4:].decode())
        def wait_for(predicate, seconds=90):
            deadline = time.monotonic() + seconds
            while not predicate():
                assert time.monotonic() < deadline, "file round trip progress deadline"
                update()
        def control(label):
            return next((item for item in snapshot["controls"] if item["label"] == label), None)
        def event(kind, target, value="", trusted=False):
            sequence = int(snapshot["ack"]) + 1
            payload = (struct.pack("<IQQQIII", 1, int(snapshot["generation"]), sequence,
                                   int(target), kind, 0, 0) + string(value) + string(""))
            packet = frame(2, payload)
            if trusted:
                assert os.write(file_write, packet) == len(packet)
            else:
                process.stdin.write(packet)
            wait_for(lambda: int(snapshot["ack"]) >= sequence)
        wait_for(lambda: snapshot is not None)
        event(1, control("Path loss")["id"], "6 dB")
        event(1, control("Short ≤16 B target SNR (dB-Hz)")["id"], "80")
        event(1, control("Long / file target SNR (dB-Hz)")["id"], "80")
        event(4, control("Attach file")["id"])
        wait_for(lambda: snapshot.get("service", {}).get("kind") == "open_file" if snapshot.get("service") else False)
        event(13, snapshot["service"]["id"], str(source), trusted=True)
        wait_for(lambda: control("Transmit") and control("Transmit")["enabled"])
        event(4, control("Transmit")["id"])
        wait_for(lambda: control("Files in memory") and control("Files in memory")["records"])
        files = control("Files in memory")
        event(7, files["id"], files["records"][0]["id"])
        wait_for(lambda: snapshot.get("service", {}).get("kind") == "save_file" if snapshot.get("service") else False)
        event(13, snapshot["service"]["id"], str(destination), trusted=True)
        assert destination.read_bytes() == fixture, "server file changed through C++ modem round trip"
        process.stdin.close()
        os.close(file_write)
        file_write = None
        while process.poll() is None:
            try:
                read_frame(process)
            except AssertionError:
                break
        assert process.wait(timeout=10) == 0, process.stderr.read()
    finally:
        if file_write is not None:
            os.close(file_write)
        if process.poll() is None:
            process.kill()
            process.wait()

# Oversized and truncated framing are terminal, never successful EOF.
for bad in [b"DPW1" + struct.pack("<II", 1, 8*1024*1024+1), frame(1, b"12345678")[:-1]]:
    process = launch()
    output, error = process.communicate(bad, timeout=15)
    assert process.returncode != 0, (output[-200:], error)
    assert b"exceeds its bound" in error or b"truncated" in error, error

print("Pipe worker framing, bounds, capability separation, server-file round trip, clock and shutdown passed")

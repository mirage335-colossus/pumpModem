#!/usr/bin/env python3
"""Bounded relay and real native-worker smoke tests (no network or pod)."""
import os
from pathlib import Path
import select
import signal
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

from _zLab import worker_relay

HERE = Path(__file__).resolve().parent
RELAY = HERE / "worker_relay.py"
REAL_WORKER = HERE.parent / "build/dev-cli-web/datapump-worker"


def frame(kind, data=b""):
    return b"DPW1" + struct.pack("<II", kind, len(data)) + data


def read_frame(pipe, timeout=15):
    deadline = time.monotonic() + timeout
    def exact(length):
        result = bytearray()
        while len(result) < length:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([pipe], [], [], remaining)[0]:
                raise AssertionError("relay output timed out")
            chunk = os.read(pipe.fileno(), length - len(result))
            if not chunk:
                raise AssertionError("relay output ended unexpectedly")
            result.extend(chunk)
        return bytes(result)
    header = exact(12)
    assert header[:4] == b"DPW1"
    kind, size = struct.unpack("<II", header[4:])
    assert size <= 8 * 1024 * 1024
    return kind, exact(size)


class RelayTests(unittest.TestCase):
    def test_headless_driver_only_for_verified_deployment(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            binary = root / "build/lab/datapump-worker"
            binary.parent.mkdir(parents=True)
            manifest = binary.parent / "nvidia-headless-icd.json"
            good = '{"file_format_version":"1.0.1","ICD":{"library_path":"libEGL_nvidia.so.0","api_version":"1.4.329"}}'
            with mock.patch.dict(worker_relay.DEPLOYMENTS, {False: binary}):
                self.assertIsNone(worker_relay.gpu_environment(binary, False))
                manifest.write_text(good)
                manifest.chmod(0o600)
                env = worker_relay.gpu_environment(binary, False)
                self.assertEqual(env["VK_ICD_FILENAMES"], str(manifest))
                self.assertEqual(env["VK_DRIVER_FILES"], str(manifest))
                self.assertIsNone(worker_relay.gpu_environment(binary, True))
                self.assertIsNone(worker_relay.gpu_environment(root / "other-worker", False))
                vendors = binary.parent / "nvidia-opencl-vendors"
                vendors.mkdir(mode=0o700)
                vendor = vendors / "nvidia.icd"
                vendor.write_text("libnvidia-opencl.so.1\n")
                vendor.chmod(0o600)
                with mock.patch.dict(os.environ, {"OCL_ICD_FILENAMES": "extra.so", "OPENCL_LAYERS": "layer.so"}):
                    env = worker_relay.gpu_environment(binary, False)
                self.assertEqual(env["OCL_ICD_VENDORS"], str(vendors))
                self.assertEqual(env["OPENCL_VENDOR_PATH"], str(vendors))
                self.assertNotIn("OCL_ICD_FILENAMES", env)
                self.assertNotIn("OPENCL_LAYERS", env)
                vendor.write_text("wrong-driver.so\n")
                with self.assertRaisesRegex(worker_relay.RelayError, "Invalid deployment OpenCL vendor manifest"):
                    worker_relay.gpu_environment(binary, False)
                vendor.write_text("libnvidia-opencl.so.1\n")
                manifest.write_text(good.replace("libEGL_nvidia.so.0", "/tmp/extra.so"))
                with self.assertRaisesRegex(worker_relay.RelayError, "Invalid deployment Vulkan manifest"):
                    worker_relay.gpu_environment(binary, False)
                manifest.write_text(good)
                manifest.chmod(0o666)
                with self.assertRaisesRegex(worker_relay.RelayError, "Unsafe deployment Vulkan manifest"):
                    worker_relay.gpu_environment(binary, False)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.worker = Path(self.tmp.name) / "fake_worker"
        self.worker.write_text('''#!/usr/bin/env python3
import os, stat, struct, sys
expected = ([] if os.environ.get("EXPECT_LIVE") else ["--simulation-only"])
if os.environ.get("EXPECT_GPU"):
    expected += ["--require-gpu"]
elif os.environ.get("EXPECT_PREFER_GPU"):
    expected += ["--prefer-gpu"]
if os.environ.get("EXPECT_FILES"):
    expected += ["--file-events-fd", sys.argv[-1]]
    assert stat.S_ISFIFO(os.fstat(int(sys.argv[-1])).st_mode)
assert sys.argv[1:] == expected, sys.argv
assert stat.S_ISFIFO(os.fstat(0).st_mode) and stat.S_ISFIFO(os.fstat(1).st_mode)
if os.environ.get("WORKER_PID"):
    with open(os.environ["WORKER_PID"], "w") as f: f.write(str(os.getpid()))
if os.environ.get("EMIT_208"):
    os.write(1, b"DPW1" + struct.pack("<II", 208, 3) + b"ack")
if os.environ.get("BAD_OUTPUT"):
    os.write(1, b"DPW1" + struct.pack("<II", 1, 8*1024*1024+1))
    sys.exit(0)
while True:
    buf = os.read(0, 4096)
    if not buf: break
    pos = 0
    while pos < len(buf): pos += os.write(1, buf[pos:])
''')
        self.worker.chmod(0o755)

    def tearDown(self):
        self.tmp.cleanup()

    def launch(self, worker=None, env=None, relay_options=()):
        return subprocess.Popen([sys.executable, str(RELAY), *relay_options, str(worker or self.worker)],
                                stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, env=env, bufsize=0)

    def test_frames_preserve_binary_and_fragmentation(self):
        p = self.launch()
        data = bytes(range(256)) * 400
        packets = frame(2, data) + frame(15)
        for offset in range(0, len(packets), 777):
            # Read concurrently after the first frame to avoid deliberately
            # filling the output pipe while this thread writes the next frame.
            if offset >= len(frame(2, data)):
                break
            os.write(p.stdin.fileno(), packets[offset:min(offset+777, len(frame(2, data)))])
        self.assertEqual(read_frame(p.stdout), (2, data))
        p.stdin.write(frame(15))
        self.assertEqual(read_frame(p.stdout), (15, b""))
        p.stdin.close()
        self.assertEqual(p.wait(timeout=10), 0, p.stderr.read().decode())
        p.stdout.close(); p.stderr.close()

    def test_gpu_flag_is_literal_and_worker_path_is_not_shell_code(self):
        # A metacharacter-containing executable name must remain one argument.
        marker = Path(self.tmp.name) / "injected"
        unusual = Path(self.tmp.name) / "worker;touch injected"
        self.worker.rename(unusual)
        p = self.launch(worker=unusual, env=dict(os.environ, EXPECT_GPU="1"),
                        relay_options=("--require-gpu",))
        out, err = p.communicate(frame(2, b"test"), timeout=15)
        self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(out, frame(2, b"test"))
        self.assertFalse(marker.exists())

    def test_gpu_preference_is_runtime_choice_and_worker_path_is_not_shell_code(self):
        p = self.launch(env=dict(os.environ, EXPECT_PREFER_GPU="1"),
                        relay_options=("--prefer-gpu",))
        out, err = p.communicate(frame(2, b"test"), timeout=15)
        self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(out, frame(2, b"test"))

    def test_live_mode_permits_only_dated_audio_protocol(self):
        env = dict(os.environ, EXPECT_LIVE='1', EXPECT_GPU='1')
        p = self.launch(env=env, relay_options=("--live", "--require-gpu"))
        packet = b''.join(frame(kind, b'payload') for kind in (1, 2, 15, *range(220, 230)))
        out, err = p.communicate(packet, timeout=15)
        self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(out, packet)
        for kind in (3, 4, 5, 6, 7, 8, 16, 219, 230):
            with self.subTest(kind=kind):
                p = self.launch(env=env, relay_options=("--live", "--require-gpu"))
                _, err = p.communicate(frame(kind), timeout=15)
                self.assertNotEqual(p.returncode, 0)
                self.assertIn(b'unsupported frame type', err)

    def test_live_forwards_capture_cutoff_from_worker(self):
        p = self.launch(env=dict(os.environ, EXPECT_LIVE='1', EMIT_208='1'),
                        relay_options=("--live",))
        out, err = p.communicate(timeout=15)
        self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(out, frame(208, b'ack'))

    def test_live_multiplex_preserves_channel_authority(self):
        def mux(channel, packet):
            return b'DPL1' + struct.pack('<II', channel, len(packet)) + packet
        env = dict(os.environ, EXPECT_LIVE='1', EXPECT_FILES='1')
        options = ("--live", "--file-channel")
        packets = [frame(kind, b'a') for kind in (1, 2, 15, *range(220, 230))]
        p = self.launch(env=env, relay_options=options)
        out, err = p.communicate(b''.join(mux(0, packet) for packet in packets), timeout=15)
        self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(out, b''.join(packets))
        for channel, kind in ((0, 4), (0, 10), (1, 220), (1, 15)):
            with self.subTest(channel=channel, kind=kind):
                p = self.launch(env=env, relay_options=options)
                _, err = p.communicate(mux(channel, frame(kind)), timeout=15)
                self.assertNotEqual(p.returncode, 0)
                self.assertIn(b'invalid channel or embedded frame', err)

    def test_maximum_payload_with_bidirectional_backpressure(self):
        payload = bytes(range(256)) * 32768
        p = self.launch()
        out, err = p.communicate(frame(2, payload), timeout=30)
        self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(out, frame(2, payload))

    def test_rejects_oversize_and_truncated_input(self):
        for bad, expected in [(b"DPW1" + struct.pack("<II", 1, 8*1024*1024+1), b"exceeds"),
                              (frame(2, b"abcd")[:-1], b"truncated"),
                              (b"BAD!" + b"\0"*8, b"magic")]:
            with self.subTest(expected=expected):
                p = self.launch()
                out, err = p.communicate(bad, timeout=15)
                self.assertNotEqual(p.returncode, 0)
                self.assertIn(expected, err)
                self.assertEqual(out, b"")

    def test_rejects_oversize_worker_output(self):
        env = dict(os.environ, BAD_OUTPUT="1")
        p = self.launch(env=env)
        out, err = p.communicate(timeout=15)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn(b"exceeds", err)
        self.assertEqual(out, b"")

    def test_setup_error_cleans_up_spawned_worker(self):
        spawned = []
        real_popen = subprocess.Popen
        def spawn(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            spawned.append(child)
            return child
        with mock.patch.object(worker_relay.subprocess, "Popen", side_effect=spawn), \
             mock.patch.object(worker_relay.os, "set_blocking", side_effect=OSError("setup failure")):
            with self.assertRaisesRegex(OSError, "setup failure"):
                worker_relay.relay(str(self.worker))
        self.assertEqual(len(spawned), 1)
        self.assertIsNotNone(spawned[0].returncode)
        with self.assertRaises(ProcessLookupError):
            os.kill(spawned[0].pid, 0)

    def test_termination_during_setup_cleans_up(self):
        spawned = []
        stopping = [False]
        real_popen = subprocess.Popen
        def spawn(*args, **kwargs):
            child = real_popen(*args, **kwargs)
            spawned.append(child)
            return child
        def terminate_during_setup(_fd, _blocking):
            stopping[0] = True
        with mock.patch.object(worker_relay.subprocess, "Popen", side_effect=spawn), \
             mock.patch.object(worker_relay.os, "set_blocking", side_effect=terminate_during_setup):
            with self.assertRaisesRegex(worker_relay.RelayError, "terminated"):
                worker_relay.relay(str(self.worker), lambda: stopping[0])
        self.assertIsNotNone(spawned[0].returncode)
        with self.assertRaises(ProcessLookupError):
            os.kill(spawned[0].pid, 0)

    def test_descendant_holding_output_after_leader_exit_is_stopped(self):
        pidfile = Path(self.tmp.name) / "descendant-pid"
        self.worker.write_text("""#!/usr/bin/env python3
import os, signal, sys, time
pid = os.fork()
if pid == 0:
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    with open(os.environ['WORKER_PID'], 'w') as f: f.write(str(os.getpid()))
    while True: time.sleep(1)
while not os.path.exists(os.environ['WORKER_PID']): time.sleep(.01)
sys.exit(0)
""")
        p = self.launch(env=dict(os.environ, WORKER_PID=str(pidfile)))
        out, err = p.communicate(timeout=8)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn(b"without closing output", err)
        self.assertEqual(out, b"")
        pid = int(pidfile.read_text())
        proc_stat = Path(f"/proc/{pid}/stat")
        # In containers, PID 1 may leave a dead orphan as a zombie briefly.
        if proc_stat.exists():
            self.assertEqual(proc_stat.read_text().split()[2], "Z")

    @unittest.skipUnless(REAL_WORKER.exists(), "native worker not built")
    def test_file_multiplex_rejects_invalid_authority_and_envelopes(self):
        def mux(channel, payload):
            return b'DPL1' + struct.pack('<II', channel, len(payload)) + payload
        invalid = [mux(0, frame(10)), mux(1, frame(15)), mux(3, frame(1)),
                   mux(1, frame(10))[:-1], frame(10)]
        for packet in invalid:
            with self.subTest(packet=packet[:16]):
                p = self.launch(worker=REAL_WORKER, relay_options=("--file-channel",))
                _, err = p.communicate(packet, timeout=15)
                self.assertNotEqual(p.returncode, 0)
                self.assertIn(b'input:', err)

    def test_termination_stops_child(self):
        pidfile = Path(self.tmp.name) / "pid"
        p = self.launch(env=dict(os.environ, WORKER_PID=str(pidfile)))
        try:
            deadline = time.monotonic() + 5
            while not pidfile.exists() and time.monotonic() < deadline:
                time.sleep(.01)
            self.assertTrue(pidfile.exists())
            pid = int(pidfile.read_text())
            p.send_signal(signal.SIGTERM)
            p.communicate(timeout=5)
            self.assertNotEqual(p.returncode, 0)
            with self.assertRaises(ProcessLookupError):
                os.kill(pid, 0)
        finally:
            if p.poll() is None:
                p.kill(); p.wait()

    @unittest.skipUnless(REAL_WORKER.exists(), "native worker not built")
    def test_real_worker_snapshot_and_clock(self):
        p = self.launch(worker=REAL_WORKER)
        try:
            kind, snapshot = read_frame(p.stdout)
            self.assertEqual(kind, 101)
            self.assertIn(b'"controls"', snapshot)
            p.stdin.write(frame(15, struct.pack("<Qd", 7, 123.5)))
            while True:
                kind, payload = read_frame(p.stdout)
                if kind == 108:
                    self.assertEqual(struct.unpack_from("<Qd", payload), (7, 123.5))
                    break
            p.stdin.close()
            while p.stdout.read(65536):
                pass
            self.assertEqual(p.wait(timeout=15), 0, p.stderr.read().decode())
        finally:
            if p.poll() is None:
                p.kill(); p.wait()
            p.stdout.close(); p.stderr.close()


if __name__ == "__main__":
    unittest.main()

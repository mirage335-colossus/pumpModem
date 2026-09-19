#!/usr/bin/env python3
"""Optional Linux fixture reproduction using an installed FLDigi and fake audio.

No FLDigi dependency is needed for DataPump's normal build or regression tests.
This recorder captures the external transmitter; it never calls DataPump DSP.
"""
import argparse
import hashlib
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import tempfile
import time
import xmlrpc.client


def unused_port():
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def stop(process):
    if process is None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="directory for reproduced PCM files")
    parser.add_argument("--fldigi", default=shutil.which("fldigi"))
    parser.add_argument("--xvfb", default=shutil.which("Xvfb"))
    parser.add_argument("--display-number", type=int, default=450)
    args = parser.parse_args()
    if not args.fldigi or not args.xvfb:
        parser.error("installed fldigi and Xvfb are required for optional reproduction")
    socket.setdefaulttimeout(5)
    args.output.mkdir(parents=True, exist_ok=True)
    source = Path(__file__).with_name("psk_reference_audio.c")
    with tempfile.TemporaryDirectory(prefix="legacy-psk-reference-") as temporary:
        base = Path(temporary)
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", str(source),
                        "-o", str(base / "audio.so")], check=True)
        config = base / "config"
        config.mkdir()
        (config / "fldigi_def.xml").write_text(
            "<FLDIGI_DEFS><MYCALL>TEST</MYCALL><AUDIOIO>2</AUDIOIO></FLDIGI_DEFS>",
            encoding="ascii")
        display = f":{args.display_number}"
        port = unused_port()
        application = None
        with (base / "application.log").open("w") as log:
            server = subprocess.Popen([args.xvfb, display, "-screen", "0",
                                       "1200x800x24", "-nolisten", "tcp", "-ac"],
                                      stdout=log, stderr=log)
            try:
                time.sleep(.5)
                if server.poll() is not None:
                    raise RuntimeError("private Xvfb failed; choose an unused display number")
                environment = dict(os.environ, DISPLAY=display,
                                   LD_PRELOAD=str(base / "audio.so"),
                                   LEGACY_PSK_REFERENCE_OUTPUT=str(base / "capture.f32"))
                application = subprocess.Popen([
                    args.fldigi, "--home-dir", str(base), "--config-dir", str(config),
                    "--xmlrpc-server-address", "127.0.0.1", "--xmlrpc-server-port", str(port),
                    "--arq-server-port", str(unused_port()),
                    "--kiss-server-port-io", str(unused_port())],
                    env=environment, stdout=log, stderr=log)
                rpc = xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}")
                for _ in range(60):
                    try:
                        print(rpc.fldigi.name_version(), flush=True)
                        break
                    except OSError:
                        if application.poll() is not None:
                            raise RuntimeError("FLDigi exited before XMLRPC startup")
                        time.sleep(.5)
                else:
                    raise RuntimeError("FLDigi XMLRPC startup timed out")
                for mode in (31, 125):
                    rpc.modem.set_by_name(f"BPSK{mode}")
                    rpc.modem.set_carrier(1500)
                    rpc.main.set_afc(False)
                    rpc.main.set_txid(False)
                    rpc.text.clear_tx()
                    capture = base / "capture.f32"
                    capture.unlink(missing_ok=True)
                    rpc.text.add_tx("CQ de N0CALL Test 123 % Z p^r")
                    rpc.main.tx()
                    time.sleep(.5)
                    deadline = time.monotonic() + 30
                    while rpc.main.get_trx_state() != "RX":
                        if time.monotonic() >= deadline:
                            raise RuntimeError("reference transmission timed out")
                        time.sleep(.25)
                    time.sleep(.5)
                    samples = [v[0] for v in struct.iter_unpack("<f", capture.read_bytes())]
                    pcm = b"".join(struct.pack("<h", round(max(-1., min(1., v)) * 32760))
                                   for v in samples)
                    output = args.output / f"bpsk{mode}-fldigi-4.2.06.s16le"
                    output.write_bytes(pcm)
                    print(output, len(samples), "samples", hashlib.sha256(pcm).hexdigest(),
                          flush=True)
            finally:
                stop(application)
                stop(server)


if __name__ == "__main__":
    main()

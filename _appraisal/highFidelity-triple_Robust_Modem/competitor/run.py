#!/usr/bin/env python3
"""Bounded upstream-codec comparison through the native appraisal channel.

Python owns process/XMLRPC orchestration, hashes and text comparison only.
Waveforms, channel propagation, noise and audio conversion execute in C/C++/
Fortran. See README.md for service and synchronization differences.
"""
import argparse
import hashlib
import json
import math
import os
import re
from pathlib import Path
import shutil
import socket
import subprocess
import time
import xmlrpc.client

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / ".build/competitors/root/usr/bin"
DATA = ROOT / ".build/competitors/root/usr/share/wsjtx"
PAYLOAD = "HELLO"
WSPR_PAYLOAD = "K2HFA FN31 30"


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def stop(process):
    if process is not None and process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


def command(argv, directory, timeout=60, allowed=(0,)):
    started = time.monotonic()
    result = subprocess.run([str(x) for x in argv], cwd=directory, capture_output=True,
                            text=True, timeout=timeout, env=dict(os.environ, OMP_NUM_THREADS="1"))
    receipt = {"command": [str(x) for x in argv], "returncode": result.returncode,
               "stdout": result.stdout, "stderr": result.stderr,
               "wall_seconds": time.monotonic() - started}
    if len(result.stdout) + len(result.stderr) > 2 * 1024 * 1024:
        raise RuntimeError("external command output exceeds bounded receipt size")
    if result.returncode not in allowed:
        raise RuntimeError(json.dumps(receipt))
    return receipt


class Fldigi:
    def __init__(self, directory, display, interposer, mode, eof_padding_seconds=0):
        self.base = directory
        self.base.mkdir()
        config = self.base / "config"
        config.mkdir()
        # No user radio configuration, network logging, or external callsigns.
        (config / "fldigi_def.xml").write_text(
            "<FLDIGI_DEFS><MYCALL>TEST</MYCALL><AUDIOIO>2</AUDIOIO>"
            "<OLIVIA8BIT>0</OLIVIA8BIT><OLIVIASINTEG>4</OLIVIASINTEG>"
            "<OLIVIASMARGIN>8</OLIVIASMARGIN></FLDIGI_DEFS>", encoding="ascii")
        self.log = (self.base / "fldigi.log").open("w")
        self.process = None
        rpc_port = port()
        environment = dict(os.environ, DISPLAY=display, LD_PRELOAD=str(interposer),
                           HF_COMPETITOR_PACE="8", HF_COMPETITOR_INPUT=str(self.base / "input.f32"),
                           HF_COMPETITOR_OUTPUT=str(self.base / "tx.f32"),
                           HF_COMPETITOR_EOF_PAD_FRAMES=str(round(eof_padding_seconds*8000)),
                           HF_COMPETITOR_DONE=str(self.base / "input.done"))
        self.process = subprocess.Popen([
            "/usr/bin/fldigi", "--home-dir", str(self.base), "--config-dir", str(config),
            "--xmlrpc-server-address", "127.0.0.1", "--xmlrpc-server-port", str(rpc_port),
            "--arq-server-port", str(port()), "--kiss-server-port-io", str(port())],
            env=environment, stdout=self.log, stderr=self.log)
        self.rpc = xmlrpc.client.ServerProxy(f"http://127.0.0.1:{rpc_port}")
        try:
            deadline = time.monotonic() + 15
            while True:
                try:
                    self.version = self.rpc.fldigi.name_version()
                    break
                except OSError:
                    if self.process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("isolated fldigi did not start; inspect fldigi.log")
                    time.sleep(.1)
            names = self.rpc.modem.get_names()
            selected = "BPSK31" if mode == "psk31" else "OLIVIA-8/250"
            if selected not in names:
                raise RuntimeError(f"installed fldigi lacks {selected}: {names}")
            self.rpc.modem.set_by_name(selected)
            self.rpc.modem.set_carrier(1500)
            self.rpc.main.set_afc(False)
            self.rpc.main.set_squelch(False)
            self.rpc.main.set_txid(False)
            self.rpc.main.set_rsid(False)
            self.rpc.text.clear_rx()
            self.settings = {"mode": self.rpc.modem.get_name(), "carrier_hz": self.rpc.modem.get_carrier(),
                             "afc": False, "squelch": False, "rsid": False,
                             "sample_clock_hz": 8000, "wall_clock_delivery_speed": 8,
                             "explicit_post_input_zero_seconds": eof_padding_seconds,
                             "version": self.version}
            if mode == "olivia8-250":
                self.settings.update({"olivia_tones_readback": self.rpc.modem.olivia.get_tones(),
                                      "olivia_bandwidth_readback": self.rpc.modem.olivia.get_bandwidth(),
                                      "active_modem_bandwidth_readback": self.rpc.modem.get_bandwidth(),
                                      "olivia_sync_integration_configured": 4,
                                      "olivia_sync_margin_configured": 8})
        except BaseException:
            self.close()
            raise

    def transmit(self):
        self.rpc.text.clear_tx()
        self.rpc.text.add_tx(PAYLOAD + "\n^r")
        self.rpc.main.tx()
        time.sleep(.25)
        deadline = time.monotonic() + 90
        while self.rpc.main.get_trx_state() != "RX":
            if time.monotonic() > deadline:
                raise RuntimeError("external fldigi transmission time limit")
            time.sleep(.1)
        time.sleep(.2)
        output = self.base / "tx.f32"
        if not output.is_file() or output.stat().st_size == 0:
            raise RuntimeError("external fldigi did not emit a waveform")
        return output

    def receive(self, path):
        self.rpc.text.clear_rx()
        temporary = self.base / "pending.f32"
        shutil.copyfile(path, temporary)
        temporary.rename(self.base / "input.f32")
        deadline = time.monotonic() + 90
        while not (self.base / "input.done").exists():
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("external fldigi receive time limit")
            time.sleep(.1)
        # The audio read blocks at EOF; this only lets XMLRPC observe characters
        # already computed from the explicitly accounted input frames.
        time.sleep(.4)
        boundary = json.loads((self.base / "input.done").read_text())
        if boundary.get("status") != "completed" or boundary.get("post_completion_frames") != 0:
            raise RuntimeError("external audio boundary did not complete its declared finite input")
        source_frames = Path(path).stat().st_size // 4
        padding = boundary.get("requested_zero_padding_frames", -1)
        alignment = boundary.get("alignment_zero_padding_frames", -1)
        block = boundary.get("audio_block_frames", 0)
        if (Path(path).stat().st_size % 4 or boundary.get("source_frames") != source_frames
                or padding != round(self.settings["explicit_post_input_zero_seconds"] * 8000)
                or not 0 <= alignment < block
                or boundary.get("delivered_frames") != source_frames + padding + alignment):
            raise RuntimeError("external audio frame accounting differs from the supplied capture")
        size = self.rpc.text.get_rx_length()
        value = self.rpc.text.get_rx(0, size)
        data = value.data if isinstance(value, xmlrpc.client.Binary) else str(value).encode()
        return {"received_text": data.decode("utf-8", errors="replace"),
                "delivered_frames": boundary["delivered_frames"], "audio_boundary": boundary,
                "settings": self.settings}

    def close(self):
        stop(self.process)
        self.log.close()


def prepare(mode, directory, display, interposer):
    if mode in ("psk31", "olivia8-250"):
        app = Fldigi(directory / "tx", display, interposer, mode)
        try:
            path = app.transmit()
            receipt = {"generator": app.settings, "message": PAYLOAD}
        finally:
            app.close()
        return path, receipt
    if mode == "ft8":
        result = command([BIN / "ft8sim", PAYLOAD, "1500", "0", "0", "0", "1", "100"], directory)
        path = directory / "000000_000001.wav"
    else:
        path = directory / "000000_0001.c2"
        result = command([BIN / "wsprsim", "-s", "50", "-o", path, WSPR_PAYLOAD], directory, allowed=(0, 1))
    if not path.is_file():
        raise RuntimeError("upstream generator did not create expected waveform")
    result["binary_sha256"] = sha(BIN / ("ft8sim" if mode == "ft8" else "wsprsim"))
    result["message"] = PAYLOAD if mode == "ft8" else WSPR_PAYLOAD
    result["upstream_simulator_channel"] = "disabled; signal-only high-SNR special case"
    return path, result


def decode(mode, path, directory, display, interposer, eof_padding_seconds=0, tier="standard"):
    if mode in ("psk31", "olivia8-250"):
        app = Fldigi(directory / "rx", display, interposer, mode, eof_padding_seconds)
        try:
            return app.receive(path)
        finally:
            app.close()
    decode_dir = directory / "decoder"
    decode_dir.mkdir()
    if mode == "ft8":
        args = [BIN / "jt9", "-8", "-p", "15", "-d", "3", "-c", "b", "-x", "b",
                "-a", decode_dir, "-t", decode_dir, "-r", DATA, path]
        if tier == "strong":
            args[1:1] = ["-M", "-N", "12", "-E", "3", "-C", "3", "-R", "3"]
    else:
        args = [BIN / "wsprd", "-d", "-H", "-a", decode_dir, path]
        if tier == "strong":
            args[1:1] = ["-o", "5"]
    result = command(args, directory, timeout=90)
    result["binary_sha256"] = sha(args[0])
    result["decoder_tier"] = tier
    result["received_text"] = result["stdout"]
    result["search_prior"] = ("UTC slot and nominal audio frequency supplied; own/DX calls blanked; generic FT8 AP remains enabled"
                              if mode == "ft8" else "UTC slot, 1500-Hz audio center; no hash-table hints; coherent block demodulation retained")
    return result


def outcomes(mode, text, expected, wanted):
    normalized = " ".join(text.split())
    stream = mode in ("psk31", "olivia8-250")
    records = []
    if not stream:
        expression = (r"^\d{6}\s+[-\d]+\s+[-\d.]+\s+[-\d]+\s+~\s+(.*)$" if mode == "ft8"
                      else r"^\d{4}\s+[-\d]+\s+[-\d.]+\s+[\d.]+\s+[-\d]+\s+(.*)$")
        records = [match.group(1).strip() for line in text.splitlines()
                   if (match := re.match(expression, line))]
    found = expected in normalized if stream else expected in records
    residual = text.replace(expected, "", 1) if found else text
    return {"correct_payload_found": wanted and found,
            "decoded_records": records,
            "false_decoded_records_on_noise": len(records) if not wanted else None,
            "extra_nonwhitespace_stream_characters": len("".join(residual.split())) if stream else None,
            "stream_characters_on_noise": len(text) if stream and not wanted else None,
            "physical_completion_evaluated": False,
            "framing_scope": ("substring text recovery; all extra/raw output retained; no Robust-style physical completion"
                              if stream else "exact parsed decoded-message match; no Robust-style physical completion")}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, default=ROOT / ".build/bin/high_fidelity_competitor")
    parser.add_argument("--campaign", type=Path, default=Path(__file__).with_name("campaign.json"))
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--setup-only", action="store_true", help="verify all four upstream codecs without the appraisal channel; not sensitivity evidence")
    parser.add_argument("--reuse-sources", type=Path, help="reuse exact clean TX captures from a completed comparator run under local-evidence")
    args = parser.parse_args()
    socket.setdefaulttimeout(5)
    output = args.output_dir.resolve()
    if ROOT / "local-evidence" not in output.parents:
        parser.error("output must be a fresh directory under appraisal local-evidence")
    output.mkdir(parents=True, exist_ok=False)
    plan = json.loads(args.campaign.read_text())
    if args.setup_only:
        plan = {"name": "upstream-noiseless-setup", "max_wall_seconds": 360,
                "episodes": [{"id": mode, "mode": mode} for mode in ("psk31", "olivia8-250", "ft8", "wspr")]}
    episodes = plan["episodes"]
    if not 0 < len(episodes) <= 24:
        raise RuntimeError("campaign exceeds 24-episode ceiling")
    ids = set()
    for episode in episodes:
        name = episode.get("id", "")
        if not re.fullmatch(r"[a-z0-9][a-z0-9-]{0,79}", name) or name in ids:
            raise RuntimeError("episode IDs must be unique simple directory names")
        ids.add(name)
        if episode.get("mode") not in ("psk31", "olivia8-250", "ft8", "wspr"):
            raise RuntimeError("unsupported upstream mode")
        if not args.setup_only:
            if episode.get("control", "wanted") not in ("wanted", "noise"):
                raise RuntimeError("invalid comparator control")
            value = episode.get("cn0_db_hz")
            if isinstance(value, bool) or not isinstance(value, (float, int)) or not math.isfinite(value) or not -80 <= value <= 120:
                raise RuntimeError("invalid comparator C/N0")
            if type(episode.get("seed")) is not int or not 0 <= episode["seed"] < 2**64:
                raise RuntimeError("invalid comparator seed")
    prior = None
    if args.reuse_sources:
        args.reuse_sources = args.reuse_sources.resolve()
        if ROOT / "local-evidence" not in args.reuse_sources.parents:
            raise RuntimeError("source reuse must refer to appraisal local-evidence")
        prior = json.loads((args.reuse_sources / "run.json").read_text())
        if prior.get("status") != "completed":
            raise RuntimeError("source reuse requires a completed frozen campaign")
    (output / "frozen-input.json").write_text(json.dumps(plan, indent=2) + "\n")
    interposer = ROOT / ".build/competitors/pulse_boundary.so"
    command(["cc", "-shared", "-fPIC", "-O2", Path(__file__).with_name("pulse_boundary.c"), "-o", interposer], output)
    log = (output / "xvfb.log").open("w")
    display = f":{600 + os.getpid() % 300}"
    server = subprocess.Popen([str(BIN / "Xvfb"), display, "-screen", "0", "1200x800x24", "-nolisten", "tcp", "-ac"], stdout=log, stderr=log)
    result = {"schema_version": 1, "campaign": plan, "bridge_sha256": None if args.setup_only else sha(args.probe),
              "interposer_sha256": sha(interposer), "fldigi_sha256": sha("/usr/bin/fldigi"),
              "appraisal_scripts": {p.name: sha(p) for p in Path(__file__).parent.glob("*.py")}, "episodes": []}
    if prior:
        result["reused_source_directory"] = str(args.reuse_sources)
        result["reused_source_receipt_sha256"] = sha(args.reuse_sources / "run.json")
    start = time.monotonic()
    try:
        time.sleep(.4)
        if server.poll() is not None:
            raise RuntimeError("private Xvfb could not start")
        sources = {}
        for episode in episodes:
            if time.monotonic() - start > plan["max_wall_seconds"]:
                raise RuntimeError("predeclared aggregate comparator wall-time ceiling")
            mode = episode["mode"]
            if mode not in ("psk31", "olivia8-250", "ft8", "wspr"):
                raise RuntimeError("unsupported declared upstream mode")
            if mode not in sources:
                if prior:
                    source_dir = args.reuse_sources / ("source-" + mode)
                    generation = json.loads((source_dir / "generation.json").read_text())
                    expected = WSPR_PAYLOAD if mode == "wspr" else PAYLOAD
                    if generation.get("message") != expected:
                        raise RuntimeError("reused transmitter message does not match this comparison")
                    path = source_dir / ("tx/tx.f32" if mode in ("psk31", "olivia8-250") else
                                         "000000_0001.c2" if mode == "wspr" else "000000_000001.wav")
                    receipts = [row for row in prior["episodes"] if row["input"]["mode"] == mode]
                    if not receipts or sha(path) != receipts[0]["source_sha256"]:
                        raise RuntimeError("reused clean transmitter hash mismatch")
                else:
                    source_dir = output / ("source-" + mode)
                    source_dir.mkdir()
                    path, generation = prepare(mode, source_dir, display, interposer)
                    (source_dir / "generation.json").write_text(json.dumps(generation, indent=2) + "\n")
                sources[mode] = path
            directory = output / episode["id"]
            directory.mkdir()
            if args.setup_only:
                decoder = decode(mode, sources[mode], directory, display, interposer,
                                 25 if mode in ("psk31", "olivia8-250") else 0)
                expected = WSPR_PAYLOAD if mode == "wspr" else PAYLOAD
                row = {"input": episode, "decoder": decoder, "source_sha256": sha(sources[mode]),
                       **outcomes(mode, decoder["received_text"], expected, True),
                       "scope": "direct upstream encode/decode setup only; no channel, level or sensitivity claim"}
                result["episodes"].append(row)
                (directory / "receipt.json").write_text(json.dumps(row, indent=2) + "\n")
                print(episode["id"], "correct=" + str(row["correct_payload_found"]), repr(decoder["received_text"]), flush=True)
                if not row["correct_payload_found"]:
                    raise RuntimeError("noiseless upstream codec control did not recover the payload")
                continue
            path = directory / ("000000_0001.c2" if mode == "wspr" else "000000_000001.wav")
            raw = directory / "received.f32"
            args_bridge = [args.probe, "--input", sources[mode], "--output", path,
                           "--scenario", episode["scenario"], "--cn0", str(episode["cn0_db_hz"]),
                           "--seed", str(episode["seed"]), "--control", episode.get("control", "wanted"),
                           "--halfband", "10" if mode == "wspr" else "300",
                           "--carrier", "1521.875" if mode == "ft8" else "1500"]
            if mode in ("psk31", "olivia8-250"):
                args_bridge.extend(["--raw-output", raw, "--padding-seconds", "25"])
            bridge = command(args_bridge, directory, timeout=90)
            channel = json.loads(bridge["stdout"])
            expected_control = episode.get("control", "wanted")
            if (channel.get("status") != "completed" or channel.get("scenario") != episode["scenario"]
                    or channel.get("seed") != episode["seed"] or channel.get("control") != expected_control
                    or channel.get("cn0_db_hz") != episode["cn0_db_hz"]):
                raise RuntimeError("native bridge receipt differs from requested frozen input")
            decoder = decode(mode, raw if mode in ("psk31", "olivia8-250") else path, directory, display, interposer)
            expected = WSPR_PAYLOAD if mode == "wspr" else PAYLOAD
            text = decoder["received_text"]
            wanted = episode.get("control", "wanted") == "wanted"
            row = {"input": episode, "channel": channel, "bridge": bridge, "decoder": decoder,
                   "expected_text": expected, **outcomes(mode, text, expected, wanted),
                   "rf_center_convention": "FT8 fixed eight-tone midpoint1521.875Hz; PSK/Olivia selected1500Hz; WSPR complexcenter0Hz",
                   "source_sha256": sha(sources[mode]), "capture_sha256": sha(path),
                   "total_wall_seconds_so_far": time.monotonic() - start}
            result["episodes"].append(row)
            (directory / "receipt.json").write_text(json.dumps(row, indent=2) + "\n")
            (output / "run.json").write_text(json.dumps(result, indent=2) + "\n")
            print(episode["id"], "correct=" + str(row["correct_payload_found"]), repr(text), flush=True)
        result["status"] = "completed"
    except BaseException as exc:
        result["status"] = "incomplete"
        result["reason"] = str(exc)
        raise
    finally:
        stop(server)
        log.close()
        result["wall_seconds"] = time.monotonic() - start
        (output / "run.json").write_text(json.dumps(result, indent=2) + "\n")


if __name__ == "__main__":
    main()

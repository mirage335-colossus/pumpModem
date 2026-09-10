"""GUI state and subprocess boundary. Does not access any input hardware itself."""
from __future__ import annotations

import base64
import collections
import json
import pathlib
import subprocess
import time


class ReceiveCache:
    def __init__(self, capacity=256 * 1024 * 1024):
        if capacity <= 0:
            raise ValueError("Memory capacity must be positive")
        self.capacity = capacity
        self.used = 0
        self.items = collections.OrderedDict()

    def add(self, packet):
        if not packet.get("validated"):
            raise ValueError("Only fully validated packets can be saved or copied")
        data = base64.b64decode(packet["data_base64"], validate=True)
        if len(data) > self.capacity:
            raise ValueError("Received file exceeds memory budget")
        packet = dict(packet)
        # Diagnostics describe the latest capture, not each cached payload.
        # Retaining thousands of Python float vectors would defeat the RAM cap.
        packet.pop("diagnostics", None)
        packet["data"] = data
        # Keep only one payload representation; never persist the receive cache.
        packet.pop("data_base64")
        identifier = packet["id"]
        if identifier in self.items:
            self.used -= len(self.items.pop(identifier)["data"])
        while self.items and (self.used + len(data) > self.capacity or len(self.items) >= 4096):
            _, old = self.items.popitem(last=False)
            self.used -= len(old["data"])
        self.items[identifier] = packet
        self.used += len(data)
        return packet

    def clear(self):
        self.items.clear()
        self.used = 0

    def save(self, identifier, chosen_path):
        # The supplied UI path is always explicit. Received filenames are labels.
        with open(chosen_path, "xb") as output:
            output.write(self.items[identifier]["data"])


class Controller:
    def __init__(self, executable, clock=time.monotonic):
        self.executable = str(pathlib.Path(executable).resolve())
        self.clock = clock
        self.next_transmission = 0.0
        self.process = None

    def remaining_delay(self):
        return max(0.0, self.next_transmission - self.clock())

    def transmitted(self, delay=6.0):
        if delay < 0:
            raise ValueError("Delay cannot be negative")
        self.next_transmission = self.clock() + delay

    def run(self, command, options, payload=None, json_output=True):
        if command in ("tx", "status-tx") and self.remaining_delay() > 0:
            raise ValueError("Transmission cooldown is active")
        args = [self.executable, command]
        if json_output:
            args.append("--json")
        args.extend(options)
        # Never invoke a shell; decoded content never becomes a command.
        self.process = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, shell=False)
        try:
            stdout, stderr = self.process.communicate(payload)
            if self.process.returncode:
                raise RuntimeError(stderr.decode("utf-8", errors="replace").strip() or "Command failed")
            if command in ("tx", "status-tx"):
                self.transmitted()
            return json.loads(stdout) if json_output else stdout
        finally:
            self.process = None

    def cancel(self):
        process = self.process
        if process is not None and process.poll() is None:
            process.terminate()


def find_pump(explicit=None):
    if explicit:
        return explicit
    import shutil
    candidates = (pathlib.Path(__file__).resolve().parents[1] / "build" / "pump",
                  pathlib.Path(__file__).resolve().parents[1] / "build" / "Release" / "pump.exe",
                  pathlib.Path(__file__).resolve().parent / "pump.exe",
                  pathlib.Path(__file__).resolve().parent / "pump")
    return next((str(p) for p in candidates if p.is_file()), shutil.which("pump") or "pump")

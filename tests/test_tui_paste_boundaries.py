#!/usr/bin/env python3
"""Focused PTY regressions for ncurses input mode and paste drain boundaries."""
import argparse
import fcntl
import importlib.util
import os
from pathlib import Path
import re
import signal
import time
from types import SimpleNamespace


PASTE_BEGIN = b"\x1b[200~"
PASTE_END = b"\x1b[201~"


def load_fixture():
    fixture_path = Path(__file__).with_name("test_tui_terminal.py")
    spec = importlib.util.spec_from_file_location("tui_terminal_fixture", fixture_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SeparateInputPty:
    """Reuse the established PTY helper while giving stdin its own OFD."""

    def __init__(self, fixture, binary):
        original_subprocess = fixture.subprocess
        original_popen = original_subprocess.Popen
        opened = {}

        def popen_with_separate_stdin(*args, **kwargs):
            output_fd = kwargs["stdout"]
            tty_path = os.ttyname(output_fd)
            input_fd = os.open(tty_path, os.O_RDWR | os.O_NOCTTY)
            input_flags = fcntl.fcntl(input_fd, fcntl.F_GETFL)
            try:
                kwargs["stdin"] = input_fd
                child = original_popen(*args, **kwargs)
            except BaseException:
                os.close(input_fd)
                raise
            opened.update(fd=input_fd, flags=input_flags, tty=tty_path)
            return child

        fixture.subprocess = SimpleNamespace(Popen=popen_with_separate_stdin)
        try:
            self.pty = fixture.Pty(binary)
        finally:
            fixture.subprocess = original_subprocess
        assert opened, "Pty did not create its child process"
        self.stdin_fd = opened["fd"]
        self.stdin_flags = opened["flags"]
        assert opened["tty"] == os.ttyname(self.pty.slave)
        assert self.stdin_fd != self.pty.slave
        assert os.fstat(self.stdin_fd).st_rdev == os.fstat(self.pty.slave).st_rdev

    def __getattr__(self, name):
        return getattr(self.pty, name)

    def assert_input_flags(self, where):
        actual = fcntl.fcntl(self.stdin_fd, fcntl.F_GETFL)
        assert actual == self.stdin_flags, (
            f"stdin flags changed at {where}: {self.stdin_flags:#x} -> {actual:#x}"
        )

    def close(self):
        try:
            self.pty.close()
        finally:
            os.close(self.stdin_fd)


def state(pty):
    screen = pty.screen.text().splitlines()
    head = re.search(r"Terminal contract ticks=(\d+) size=(\d+)x(\d+)", screen[0])
    events = re.search(r"events=(\d+) pastes=(\d+) ([^\n]*)", screen[1])
    assert head and events, f"Fixture status lines are missing:\n{pty.screen.text()}"
    return (int(head.group(1)), int(head.group(2)), int(head.group(3)),
            int(events.group(1)), int(events.group(2)), events.group(3), screen[2])


def wait_state(pty, predicate, label, timeout=2.0):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        pty.read(min(0.03, max(0.0, deadline - time.monotonic())))
        current = state(pty)
        if predicate(current):
            return current
        assert pty.process.poll() is None, (
            f"Terminal exited during {label}: {pty.process.returncode}"
        )
    raise AssertionError(f"Timed out during {label}; last state={state(pty)}")


def assert_ticks_advance_without_events(pty, baseline, label):
    return wait_state(
        pty,
        lambda s: s[0] > baseline[0] and s[3:5] == baseline[3:5],
        label,
    )


def assert_exact_status_after_idle(pty, expected, label):
    pty.read(0.15)
    actual = state(pty)
    assert actual[3:6] == expected[3:6], f"Extra or missing {label} event: {actual}"
    pty.assert_input_flags(f"after {label}")


def run(binary):
    fixture = load_fixture()
    pty = SeparateInputPty(fixture, binary)
    try:
        pty.wait_for("Terminal contract ticks=")
        pty.assert_input_flags("initial quiescent state")

        # An incomplete paste must leave the main loop responsive while idle.
        before = state(pty)
        pty.send(PASTE_BEGIN + b"partial draft")
        partial = assert_ticks_advance_without_events(pty, before, "open paste")
        pty.assert_input_flags("open paste")
        pty.resize(100, 32)
        resized = wait_state(
            pty,
            lambda s: s[1:3] == (100, 32) and s[0] > partial[0]
            and s[3:5] == partial[3:5],
            "resize during open paste",
        )
        pty.assert_input_flags("resized open paste")

        # Ctrl-Q is literal paste data. The marker is split across writes and
        # the trailing Tab shares the final write, so event order is observable.
        pty.send(b"\x11still data" + PASTE_END[:4])
        pending = assert_ticks_advance_without_events(pty, resized, "split end marker")
        assert pty.process.poll() is None, "Ctrl-Q inside paste exited the terminal"
        pty.send(PASTE_END[4:] + b"\t")
        completed = wait_state(
            pty,
            lambda s: s[3] == pending[3] + 2 and s[4] == pending[4] + 1
            and s[5].startswith("key 4 shift=0 ctrl=0"),
            "paste end and trailing Tab",
        )
        assert "partial draft_still data" in completed[6], (
            f"Paste payload prefix was not preserved: {completed[6]!r}"
        )
        assert_exact_status_after_idle(pty, completed, "one paste followed by Tab")

        # Cross the adapter's 256-input-iteration boundary and its multiple.
        # The display exposes a bounded prefix, so check that prefix as well as
        # event/paste counts and the following ordinary key's ordering.
        for length in (255, 256, 257, 511, 512, 513):
            before = state(pty)
            payload = bytes(ord("a") + (index % 26) for index in range(length))
            pty.send(PASTE_BEGIN + payload + PASTE_END + b"\t")
            completed = wait_state(
                pty,
                lambda s: s[3] == before[3] + 2 and s[4] == before[4] + 1
                and s[5].startswith("key 4 shift=0 ctrl=0"),
                f"{length}-byte paste and trailing Tab",
            )
            visible_prefix = payload[:100].decode("ascii")
            assert completed[6].startswith(visible_prefix), (
                f"{length}-byte paste prefix changed: expected {visible_prefix!r}, "
                f"got {completed[6]!r}"
            )
            assert_exact_status_after_idle(pty, completed, f"{length}-byte paste")

        pty.finish(sig=signal.SIGTERM)
        pty.assert_input_flags("after finish")
    finally:
        pty.close()
    print("TUI paste boundary regression passed")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    run(os.path.abspath(args.binary))

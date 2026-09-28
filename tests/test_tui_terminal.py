#!/usr/bin/env python3
"""Real PTY contract for the generic ncurses adapter fixture; no audio needed."""
import argparse
import errno
import fcntl
import os
import re
import select
import signal
import struct
import subprocess
import termios
import time


class Screen:
    """Small VT observer for the ASCII operations emitted by ncurses/terminfo."""
    def __init__(self, width, height):
        self.width, self.height = width, height
        self.cells = [[" "] * width for _ in range(height)]
        self.x = self.y = 0
        self.pending = b""

    def feed(self, data):
        self.pending += data
        while self.pending:
            b = self.pending[0]
            if b == 27:
                if len(self.pending) < 2:
                    return
                if self.pending[1] == ord("["):
                    match = re.match(rb"\x1b\[([0-9;?=>]*)([ -/]*)([@-~])", self.pending)
                    if not match:
                        return
                    params, _, final = match.groups()
                    self.pending = self.pending[match.end():]
                    if params.startswith(b"?"):
                        continue
                    values = [int(v) if v else 0 for v in params.split(b";")]
                    n = values[0] or 1
                    if final in (b"H", b"f"):
                        self.y = min(self.height - 1, n - 1)
                        self.x = min(self.width - 1, (values[1] if len(values) > 1 and values[1] else 1) - 1)
                    elif final == b"G":
                        self.x = min(self.width - 1, n - 1)
                    elif final == b"d":
                        self.y = min(self.height - 1, n - 1)
                    elif final == b"A":
                        self.y = max(0, self.y - n)
                    elif final in (b"B", b"e"):
                        self.y = min(self.height - 1, self.y + n)
                    elif final in (b"C", b"a"):
                        self.x = min(self.width - 1, self.x + n)
                    elif final == b"D":
                        self.x = max(0, self.x - n)
                    elif final == b"J":
                        if values[0] in (2, 3):
                            self.cells = [[" "] * self.width for _ in range(self.height)]
                        elif values[0] == 0:
                            for y in range(self.y, self.height):
                                start = self.x if y == self.y else 0
                                self.cells[y][start:] = [" "] * (self.width - start)
                    elif final == b"K":
                        start = 0 if values[0] in (1, 2) else self.x
                        stop = self.x + 1 if values[0] == 1 else self.width
                        self.cells[self.y][start:stop] = [" "] * (stop - start)
                    elif final == b"P":
                        stop = min(self.width, self.x + n)
                        row = self.cells[self.y]
                        row[self.x:] = row[stop:] + [" "] * (stop - self.x)
                    elif final == b"@":
                        count = min(n, self.width - self.x)
                        row = self.cells[self.y]
                        row[self.x:] = [" "] * count + row[self.x:self.width - count]
                    elif final == b"X":
                        stop = min(self.width, self.x + n)
                        self.cells[self.y][self.x:stop] = [" "] * (stop - self.x)
                    elif final == b"b":
                        character = self.cells[self.y][max(0, self.x - 1)]
                        for _ in range(n):
                            self.put(character)
                    continue
                if self.pending[1] in b"()*+":
                    if len(self.pending) < 3:
                        return
                    self.pending = self.pending[3:]
                elif self.pending[1] == ord("]"):
                    raise AssertionError("Unexpected OSC terminal control sequence")
                else:
                    self.pending = self.pending[2:]
                continue
            self.pending = self.pending[1:]
            if b == 13:
                self.x = 0
            elif b == 10:
                self.y = min(self.height - 1, self.y + 1)
            elif b == 8:
                self.x = max(0, self.x - 1)
            elif b == 9:
                self.x = min(self.width - 1, ((self.x // 8) + 1) * 8)
            elif 32 <= b <= 126:
                self.put(chr(b))
            elif b >= 128:
                raise AssertionError("Non-ASCII byte escaped the terminal cell sink")

    def put(self, character):
        if self.x >= self.width:
            self.x = 0
            self.y = min(self.height - 1, self.y + 1)
        self.cells[self.y][self.x] = character
        self.x += 1

    def text(self):
        return "\n".join("".join(row) for row in self.cells)


class Pty:
    def __init__(self, binary, terminal="xterm-256color", width=80, height=24):
        self.master, self.slave = os.openpty()
        self.original_mode = termios.tcgetattr(self.slave)
        self.original_flags = fcntl.fcntl(self.slave, fcntl.F_GETFL)
        self.resize(width, height, notify=False)
        self.screen = Screen(width, height)
        self.raw = bytearray()
        env = dict(os.environ, TERM=terminal, LC_ALL="C.UTF-8")
        # A PTY models terminal bytes, not a physical Linux VT or its GPM
        # daemon. Keep that separate device integration out of this fixture.
        # In particular, libgpm retains its console-name allocation when the
        # console probe fails and ncurses subsequently unloads the library.
        env["NCURSES_GPM_TERMS"] = ""
        self.process = subprocess.Popen([binary], stdin=self.slave, stdout=self.slave,
                                        stderr=self.slave, env=env, start_new_session=True,
                                        close_fds=True)
        os.set_blocking(self.master, False)

    def resize(self, width, height, notify=True):
        fcntl.ioctl(self.slave, termios.TIOCSWINSZ, struct.pack("HHHH", height, width, 0, 0))
        if notify:
            self.screen = Screen(width, height)
            self.process.send_signal(signal.SIGWINCH)

    def read(self, duration=0.03):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            if not select.select([self.master], [], [], max(0, deadline - time.monotonic()))[0]:
                break
            try:
                data = os.read(self.master, 65536)
            except OSError as error:
                if error.errno in (errno.EAGAIN, errno.EIO):
                    break
                raise
            if not data:
                break
            self.raw.extend(data)
            self.screen.feed(data)

    def wait_for(self, text, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.read()
            if text in self.screen.text():
                return self.screen.text()
            if self.process.poll() is not None:
                break
        raise AssertionError(f"Missing {text!r}; status={self.process.poll()} screen:\n{self.screen.text()}")

    def send(self, value):
        os.write(self.master, value)

    def send_all(self, value, timeout=20):
        remaining = memoryview(value)
        deadline = time.monotonic() + timeout
        while remaining:
            assert self.process.poll() is None, (
                f"Terminal exited during input: status={self.process.returncode}; "
                f"sent={len(value) - len(remaining)}/{len(value)}; tail={bytes(self.raw[-6000:])!r}")
            assert time.monotonic() < deadline, (
                f"Terminal input stopped draining: sent={len(value) - len(remaining)}/{len(value)}; "
                f"tail={bytes(self.raw[-6000:])!r}")
            _, writable, _ = select.select([], [self.master], [], 0.02)
            if writable:
                try:
                    remaining = remaining[os.write(self.master, remaining[:65536]):]
                except BlockingIOError:
                    pass
            self.read(0.001)

    def finish(self, sig=None, read=True):
        if self.process.poll() is None:
            if sig:
                self.process.send_signal(sig)
            else:
                self.send(b"\x11")
        deadline = time.monotonic() + 3
        while self.process.poll() is None and time.monotonic() < deadline:
            if read:
                self.read()
            else:
                time.sleep(0.01)
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
            raise AssertionError("Terminal shutdown blocked on output")
        if read:
            self.read()
        assert self.process.returncode == 0, (
            f"Terminal exited with {self.process.returncode}; tail={bytes(self.raw[-6000:])!r}")
        assert termios.tcgetattr(self.slave) == self.original_mode, "Terminal mode was not restored"
        assert fcntl.fcntl(self.slave, fcntl.F_GETFL) == self.original_flags, "Output flags were not restored"

    def close(self):
        if self.process.poll() is None:
            self.process.kill()
            self.process.wait()
        os.close(self.master)
        os.close(self.slave)


def run(binary):
    terminal = Pty(binary)
    try:
        terminal.wait_for("Terminal contract ticks=")
        terminal.wait_for("literal:_]52;c;BAD_____")
        assert b"\x1b]52" not in terminal.raw
        assert all(byte < 128 for byte in terminal.raw)
        plot = terminal.screen.text().splitlines()[5]
        assert "." in plot[:20] and "#" in plot[:20] and "%" in plot[:20], plot
        assert terminal.screen.text().splitlines()[4].startswith("...CLIP..."), terminal.screen.text()
        assert plot[30:35] == " " * 5 and plot[45:50] == " " * 5, plot
        assert plot[35:45].strip(), plot
        assert terminal.screen.text().splitlines()[7][30:50] == " " * 20
        terminal.send(b"\t")
        terminal.wait_for("key 4 shift=0 ctrl=0")
        terminal.send(b"\x1b[Z")
        terminal.wait_for("key 4 shift=1 ctrl=0")
        terminal.send(b"\x1bOD")
        terminal.wait_for("key 5 shift=0 ctrl=0")
        terminal.send(b"\x1b[3~")
        terminal.wait_for("key 10 shift=0 ctrl=0")
        terminal.send(b"\x1bOP")
        terminal.wait_for("key 15 shift=0 ctrl=0")
        terminal.send(b"\x1bOQ")
        terminal.wait_for("key 2 shift=0 ctrl=1")
        terminal.send(b"\x1bOR")
        terminal.wait_for("key 2 shift=1 ctrl=0")
        terminal.send(b"\x1b[1;3B")
        terminal.wait_for("key 8 shift=0 ctrl=0 alt=1")
        terminal.send(b"\x1b[1;2C")
        terminal.wait_for("key 6 shift=1 ctrl=0 alt=0")
        terminal.send(b"\x1b[1;5H")
        terminal.wait_for("key 11 shift=0 ctrl=1 alt=0")
        terminal.send(b"\x1bOS")
        terminal.wait_for("key 8 shift=0 ctrl=0 alt=1")
        terminal.send(b" ")
        terminal.wait_for("key 3 shift=0 ctrl=0 alt=0")
        # Distinct Enter encodings from CSI-u and xterm modifyOtherKeys retain
        # their modifiers; ordinary Return remains an unmodified key.
        modified_enter = [
            (b"\x1b[13;5u", "key 2 shift=0 ctrl=1 alt=0"),
            (b"\x1b[27;5;13~", "key 2 shift=0 ctrl=1 alt=0"),
            (b"\x1b[13;2u", "key 2 shift=1 ctrl=0 alt=0"),
            (b"\x1b[27;2;13~", "key 2 shift=1 ctrl=0 alt=0"),
            (b"\x1b[13;6u", "key 2 shift=1 ctrl=1 alt=0"),
            (b"\x1b[27;6;13~", "key 2 shift=1 ctrl=1 alt=0"),
            (b"\r", "key 2 shift=0 ctrl=0 alt=0"),
            (b"\x1b[5~", "key 13 shift=0 ctrl=0 alt=0"),
            (b"\x1b[6~", "key 14 shift=0 ctrl=0 alt=0"),
        ]
        for count, (encoded, expected) in enumerate(modified_enter, start=13):
            terminal.send(encoded)
            terminal.wait_for(f"events={count} pastes=0 {expected}")
        # Paste contains newline, tab, quit, and an OSC-shaped sequence. It must
        # reach the presenter as one data event; no key or quit is synthesized.
        terminal.send(b"\x1b[200~paste\n\t\x11\x1b[13;5u\x1b[27;5;13~\x1b]52;c;INJECT\x07\x1b[201~")
        terminal.wait_for("events=22 pastes=1 paste")
        assert terminal.process.poll() is None
        assert b"\x1b]52" not in terminal.raw
        terminal.send(b"\x1b[<0;12;7M\x1b[<0;12;7m")
        terminal.wait_for("pointer 11,6")
        terminal.send(b"\x1b[<64;12;7M")
        terminal.wait_for("wheel 1")
        terminal.resize(100, 32)
        terminal.wait_for("size=100x32")
        terminal.send(b"\x1b[200~saved draft\x1b[201~")
        terminal.wait_for("pastes=2 paste")
        terminal.send_all(b"\x1b[200~" + b"x" * (4 * 1024 * 1024 + 1) + b"\n\t\x11\x1b[201~")
        terminal.wait_for("rejected: Paste exceeds", timeout=10)
        assert "saved draft" in terminal.screen.text(), terminal.screen.text()
        assert "pastes=2" in terminal.screen.text(), terminal.screen.text()
        assert terminal.process.poll() is None
        terminal.send(b"\x1b[200~after rejection\x1b[201~")
        terminal.wait_for("pastes=3 paste")
        terminal.wait_for("after rejection")
        terminal.finish()
        assert b"\x1b[?2004l" in terminal.raw, "Paste mode was not disabled"
        assert b"\x1b[?1049l" in terminal.raw, "Alternate screen was not restored"
    finally:
        terminal.close()

    # Linux VT terminfo must support the ASCII keyboard baseline without xterm
    # mouse/truecolor capabilities or a running GPM daemon.
    console = Pty(binary, terminal="linux")
    try:
        console.wait_for("Terminal contract ticks=")
        console.send(b"\t")
        console.wait_for("key 4 shift=0 ctrl=0")
        console.finish(sig=signal.SIGTERM)
    finally:
        console.close()

    stalled = Pty(binary, width=180, height=70)
    try:
        stalled.wait_for("Terminal contract ticks=")
        stalled.send(b"\x1b[200~flood\x1b[201~")
        stalled.wait_for("pastes=1 paste")
        # Stop reading until the PTY output buffer fills. The UI tick/input loop
        # must still process quit, terminate and restore termios in bounded time.
        time.sleep(0.8)
        started = time.monotonic()
        stalled.finish(read=False)
        assert time.monotonic() - started < 2, "Output backpressure blocked shutdown"
    finally:
        stalled.close()
    print("TUI PTY contract passed: ASCII sink, keyboard, paste, terminal mouse, resize, VT terminfo, restoration, backpressure; physical GPM not exercised")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    run(os.path.abspath(parser.parse_args().binary))

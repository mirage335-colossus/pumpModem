"""Optional real Tk smoke test. Run under Xvfb on Linux; never opens audio devices."""
import argparse
import pathlib
import subprocess
import sys
import tempfile
import time
from unittest.mock import patch

parser = argparse.ArgumentParser()
parser.add_argument("--pump", required=True)
parser.add_argument("--screenshot")
parser.add_argument("--gui-dir", type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parents[1] / "gui")
options = parser.parse_args()
sys.path.insert(0, str(options.gui_dir.resolve()))
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import datapump_gui

def descendants(widget):
    for child in widget.winfo_children():
        yield child
        yield from descendants(child)

def exercise(root):
    root.update()
    widgets = list(descendants(root))
    buttons = {str(w.cget("text")): w for w in widgets if isinstance(w, ttk.Button)}
    editor = next(w for w in widgets if isinstance(w, tk.Text) and w.cget("bg") == "#172a38")
    tree = next(w for w in widgets if isinstance(w, ttk.Treeview))
    qr = next(w for w in widgets if isinstance(w, tk.Canvas) and int(w.cget("width")) == 185)
    payload = "CQ CQ · Clipboard transfer verified\nHello from the Data Pump desktop console."
    editor.insert("1.0", payload)
    editor.event_generate("<KeyRelease>")
    buttons["Run simulation"].invoke()
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline and (not tree.get_children() or len(qr.find_all()) < 10):
        root.update()
        time.sleep(.02)
    assert len(tree.get_children()) == 1, "GUI failed to receive the simulation"
    assert len(qr.find_all()) > 10, "GUI did not render its QR preview"
    assert buttons["Receive audio"].winfo_ismapped(), "Audio controls are clipped"
    for button in buttons.values():
        assert button.winfo_height() >= button.winfo_reqheight(), f"Button clipped: {button.cget('text')}"
    for canvas in (w for w in widgets if isinstance(w, tk.Canvas)):
        assert canvas.winfo_rooty() + canvas.winfo_height() <= root.winfo_rooty() + root.winfo_height(), "Signal plot is outside the window"
    buttons["Copy text"].invoke()
    assert root.clipboard_get() == payload, "Clipboard changed verified text"
    with tempfile.TemporaryDirectory() as folder:
        path = pathlib.Path(folder) / "selected.txt"
        with patch.object(filedialog, "asksaveasfilename", return_value=str(path)):
            buttons["Save selected…"].invoke()
        assert path.read_bytes() == payload.encode(), "GUI save changed the verified payload"
    if options.screenshot:
        subprocess.run(["import", "-window", str(root.winfo_id()), options.screenshot], check=True)
    # Exercise the largest supported Unicode QR without clipping its quiet zone.
    editor.delete("1.0", "end")
    editor.insert("1.0", "🌍" * 500)
    editor.event_generate("<KeyRelease>")
    deadline = time.monotonic() + 5
    last_count = len(qr.find_all())
    while time.monotonic() < deadline:
        root.update()
        if len(qr.find_all()) != last_count and len(qr.find_all()) > 100:
            break
        time.sleep(.02)
    bounds = qr.bbox("all")
    assert bounds and min(bounds) >= 0 and max(bounds) <= 185, f"QR clipped: {bounds}"
    buttons["Clear received data"].invoke()
    assert not tree.get_children(), "Cache was not cleared"
    root.destroy()
    print("Real Tk smoke test passed: simulation, receive, QR, clipboard, exclusive save, clear")

def fail_dialog(title, message):
    raise AssertionError(f"Unexpected GUI error: {title}: {message}")

with patch.object(tk.Tk, "mainloop", exercise), patch.object(messagebox, "showerror", fail_dialog), \
     patch.object(sys, "argv", ["datapump-gui", "--pump", options.pump]):
    datapump_gui.main()

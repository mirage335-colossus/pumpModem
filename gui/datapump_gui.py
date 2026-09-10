#!/usr/bin/env python3
"""Optional Tk front end; the compiled pump executable owns every modem operation."""
from __future__ import annotations

import argparse
import base64
import cmath
import math
import pathlib
import queue
import threading
import time

from model import Controller, ReceiveCache, find_pump


def main():
    parser = argparse.ArgumentParser(description="Data Pump desktop console")
    parser.add_argument("--pump", help="Path to compiled pump executable")
    args = parser.parse_args()
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox, ttk
    except ImportError:
        raise SystemExit("This Python interpreter does not contain Tk. Use the datapump-gui launcher from a complete offline bundle, or copy a known-working bundle from another compatible computer. See docs/offline-installation.md. No packages are downloaded automatically.")

    class App:
        def __init__(self, root):
            self.root = root
            self.controller = Controller(find_pump(args.pump))
            self.qr_controller = Controller(find_pump(args.pump))
            self.cache = ReceiveCache()
            self.events = queue.Queue()
            self.busy = False
            self.qr_generation = 0
            self.qr_timer = None
            self.qr_running = False
            self.root.title("Data Pump · Audio transfer console")
            self.root.geometry("1280x950")
            self.root.minsize(1100, 950)
            self.root.configure(bg="#101c27")
            style = ttk.Style()
            style.theme_use("clam")
            style.configure(".", background="#101c27", foreground="#dce8f0", fieldbackground="#1a2b3a", font=("Arial", 10))
            style.configure("TFrame", background="#101c27")
            style.configure("TLabel", background="#101c27")
            style.configure("TButton", padding=(12, 8), background="#243a4c")
            style.map("TButton", background=[("active", "#365a70")])
            style.configure("Accent.TButton", background="#176f69", foreground="white")
            style.configure("TEntry", fieldbackground="#1a2b3a", padding=5)
            style.configure("TCombobox", fieldbackground="#1a2b3a", padding=4)
            style.map("TCombobox", fieldbackground=[("readonly", "#1a2b3a")], foreground=[("readonly", "#dce8f0")])
            style.configure("TLabelframe", background="#101c27", bordercolor="#304654")
            style.configure("TLabelframe.Label", foreground="#82adaf", background="#101c27")
            style.configure("Treeview", background="#152532", fieldbackground="#152532", foreground="#dce8f0", rowheight=29)
            style.configure("Treeview.Heading", background="#243a4c", foreground="#dce8f0")
            self.vars = {name: tk.StringVar(value=value) for name, value in {
                "callsign": "", "grid": "", "device": "default", "bw": "1200", "fec": "20",
                "spreading": "1", "keyfile": "", "pad": "", "seconds": "20", "snr": "12",
                "time": "", "search": "6"}.items()}
            self.repeatable = tk.BooleanVar()
            self.scramble = tk.BooleanVar()
            self.dsss = tk.BooleanVar()
            self.ctrl_enter = tk.BooleanVar()
            self.status = tk.StringVar(value="Ready · received data stays in memory until you save it")
            self.metrics = tk.StringVar(value="No signal decoded")
            self.file_path = None
            self.build_ui()
            self.root.protocol("WM_DELETE_WINDOW", self.close)
            self.root.after(80, self.poll)

        def build_ui(self):
            outer = ttk.Frame(self.root, padding=18)
            outer.pack(fill="both", expand=True)
            outer.columnconfigure(0, weight=1)
            outer.rowconfigure(3, weight=1)
            header = ttk.Frame(outer)
            header.grid(row=0, column=0, sticky="ew", pady=(0, 15))
            ttk.Label(header, text="DATA PUMP", font=("Arial", 23, "bold"), foreground="#7cddd2").pack(side="left")
            ttk.Label(header, text="  /  AUDIO TRANSFER", foreground="#87a2b3").pack(side="left", pady=(7, 0))
            ttk.Button(header, text="Clear received data", command=self.clear).pack(side="right")
            ttk.Label(header, text="LOCAL • NO NETWORK LISTENER", foreground="#86aaa6").pack(side="right", padx=18)
            setup = ttk.Labelframe(outer, text="STATION & SIGNAL", padding=10)
            setup.grid(row=1, column=0, sticky="ew", pady=(0, 10))
            fields = [("Callsign", "callsign", None, 14), ("Grid", "grid", None, 9),
                      ("Audio device", "device", None, 17), ("Bandwidth Hz", "bw", ("1200", "2400", "22050", "24000"), 10),
                      ("RS parity %", "fec", ("20", "60", "off"), 7),
                      ("Spreading", "spreading", ("1", "4", "8", "16", "32", "128", "1024", "16384"), 9)]
            for column, (label, name, values, width) in enumerate(fields):
                cell = ttk.Frame(setup)
                cell.grid(row=0, column=column, padx=7, sticky="ew")
                ttk.Label(cell, text=label, foreground="#8cabbc").pack(anchor="w", pady=(0, 4))
                if values:
                    ttk.Combobox(cell, textvariable=self.vars[name], values=values, state="readonly", width=width).pack(fill="x")
                else:
                    ttk.Entry(cell, textvariable=self.vars[name], width=width).pack(fill="x")
                setup.columnconfigure(column, weight=1)
            ttk.Checkbutton(setup, text="Repeatable (max64KiB)", variable=self.repeatable).grid(row=1, column=0, columnspan=2, sticky="w", padx=7, pady=(10, 0))
            ttk.Button(setup, text="List audio devices", command=self.list_devices).grid(row=1, column=2, sticky="w", pady=(10, 0))
            ttk.Label(setup, text="Capture seconds").grid(row=1, column=3, pady=(10, 0))
            ttk.Entry(setup, textvariable=self.vars["seconds"], width=8).grid(row=1, column=4, pady=(10, 0))
            secure = ttk.Frame(outer)
            secure.grid(row=2, column=0, sticky="ew", pady=(0, 12))
            ttk.Button(secure, text="Choose shared key…", command=self.choose_key).pack(side="left")
            self.key_label = ttk.Label(secure, text="Encryption off", foreground="#8cabbc")
            self.key_label.pack(side="left", padx=9)
            ttk.Button(secure, text="Clear key", command=self.clear_key).pack(side="left")
            ttk.Button(secure, text="Pad…", command=self.choose_pad).pack(side="left", padx=5)
            self.scramble_control = ttk.Checkbutton(secure, text="Encrypted patterns", variable=self.scramble, state="disabled")
            self.scramble_control.pack(side="left", padx=8)
            self.dsss_control = ttk.Checkbutton(secure, text="DSSS", variable=self.dsss, state="disabled")
            self.dsss_control.pack(side="left")
            ttk.Label(secure, text="Epoch / drift ±s").pack(side="left", padx=(15, 4))
            ttk.Entry(secure, textvariable=self.vars["time"], width=12).pack(side="left")
            ttk.Entry(secure, textvariable=self.vars["search"], width=5).pack(side="left", padx=4)

            panes = ttk.Panedwindow(outer, orient="horizontal")
            panes.grid(row=3, column=0, sticky="nsew")
            left, right = ttk.Frame(panes), ttk.Frame(panes)
            panes.add(left, weight=3)
            panes.add(right, weight=2)
            receive = ttk.Labelframe(left, text="RECEIVED · VERIFIED CONTENT", padding=10)
            receive.pack(fill="both", expand=True, padx=(0, 10))
            receive.columnconfigure(0, weight=1)
            receive.rowconfigure(0, weight=2)
            receive.rowconfigure(1, weight=1)
            self.tree = ttk.Treeview(receive, columns=("type", "name", "bytes"), show="headings", height=6)
            for key, label, width in (("type", "Integrity", 110), ("name", "Content", 260), ("bytes", "Bytes", 60)):
                self.tree.heading(key, text=label)
                self.tree.column(key, width=width)
            self.tree.grid(row=0, column=0, sticky="nsew")
            self.tree.bind("<<TreeviewSelect>>", self.show_selected)
            self.preview = tk.Text(receive, height=6, bg="#0c1720", fg="#dce8f0", insertbackground="#7cddd2", relief="flat", wrap="word", padx=10, pady=8)
            self.preview.grid(row=1, column=0, sticky="nsew", pady=(8, 0))
            self.preview.configure(state="disabled")
            buttons = ttk.Frame(receive)
            buttons.grid(row=2, column=0, sticky="ew", pady=(8, 0))
            ttk.Button(buttons, text="Copy text", command=self.copy).pack(side="left")
            ttk.Button(buttons, text="Save selected…", command=self.save).pack(side="left", padx=6)
            ttk.Button(buttons, text="Decode WAV…", command=self.decode_file).pack(side="right")

            send = ttk.Labelframe(right, text="COMPOSE & TRANSMIT", padding=10)
            send.pack(fill="both", expand=True)
            send.columnconfigure(0, weight=1)
            send.rowconfigure(0, weight=1)
            compose_top = ttk.Frame(send)
            compose_top.grid(row=0, column=0, sticky="nsew")
            self.editor = tk.Text(compose_top, height=5, width=26, bg="#172a38", fg="#e7f2f7", insertbackground="#7cddd2", relief="flat", wrap="word", padx=10, pady=10, undo=True)
            self.editor.pack(side="left", fill="both", expand=True)
            self.editor.bind("<<Modified>>", self.text_changed)
            self.editor.bind("<Return>", self.enter)
            self.editor.bind("<Control-Return>", self.control_enter)
            row = ttk.Frame(send)
            row.grid(row=1, column=0, sticky="ew", pady=8)
            ttk.Button(row, text="Attach file / screenshot…", command=self.attach).pack(side="left")
            ttk.Button(row, text="Remove", command=self.detach).pack(side="left", padx=4)
            self.file_label = ttk.Label(send, text="Text message", foreground="#8cabbc")
            self.file_label.grid(row=2, column=0, sticky="w")
            row = ttk.Frame(send)
            row.grid(row=3, column=0, sticky="ew", pady=8)
            ttk.Button(row, text="Transmit audio", style="Accent.TButton", command=self.transmit).pack(side="left")
            ttk.Button(row, text="Export WAV…", command=self.export).pack(side="left", padx=6)
            ttk.Checkbutton(send, text="Transmit on Ctrl+Enter (otherwise Enter)", variable=self.ctrl_enter).grid(row=4, column=0, sticky="w")
            qr_frame = ttk.Frame(compose_top)
            qr_frame.pack(side="right", padx=(10, 0))
            self.qr_canvas = tk.Canvas(qr_frame, width=185, height=185, bg="#0c1720", highlightthickness=0)
            self.qr_canvas.pack()
            ttk.Label(qr_frame, text="QR · Level L · ≤500 chars", foreground="#8cabbc", justify="center").pack(pady=4)

            controls = ttk.Frame(outer)
            controls.grid(row=4, column=0, sticky="ew", pady=12)
            ttk.Button(controls, text="Receive audio", style="Accent.TButton", command=self.record).pack(side="left")
            ttk.Button(controls, text="Cancel operation", command=self.controller.cancel).pack(side="left", padx=8)
            ttk.Label(controls, text="Loopback SNR dB").pack(side="left", padx=(20, 5))
            ttk.Combobox(controls, textvariable=self.vars["snr"], values=("30", "20", "12", "6", "0", "-6", "-12", "-20"), width=6).pack(side="left")
            ttk.Button(controls, text="Run simulation", command=self.simulate).pack(side="left", padx=8)
            ttk.Label(controls, textvariable=self.metrics, foreground="#7cddd2").pack(side="right")
            plots = ttk.Frame(outer)
            plots.grid(row=5, column=0, sticky="ew")
            self.plots = {}
            for title in ("WAVEFORM", "SPECTRUM", "DIFFERENTIAL CONSTELLATION"):
                box = ttk.Labelframe(plots, text=title, padding=4)
                box.pack(side="left", fill="both", expand=True, padx=3)
                canvas = tk.Canvas(box, height=105, bg="#0c1720", highlightthickness=0)
                canvas.pack(fill="both", expand=True)
                self.plots[title] = canvas
            ttk.Label(outer, textvariable=self.status, foreground="#8cabbc").grid(row=6, column=0, sticky="w", pady=(12, 0))

        def options(self):
            options = ["--bw", self.vars["bw"].get(), "--fec", self.vars["fec"].get(),
                       "--spreading", self.vars["spreading"].get(), "--search-seconds", self.vars["search"].get()]
            for name in ("callsign", "grid", "keyfile", "pad", "time"):
                value = self.vars[name].get().strip()
                if value:
                    options.extend(["--" + name, value])
            for name, var in (("repeatable", self.repeatable), ("scramble", self.scramble), ("dsss", self.dsss)):
                if var.get():
                    options.append("--" + name)
            return options

        def payload(self):
            if self.file_path:
                kind = "screenshot" if pathlib.Path(self.file_path).suffix.lower() in (".png", ".jpg", ".jpeg", ".webp") else "file"
                return ["--input", self.file_path, "--kind", kind], None
            return ["--input", "-", "--kind", "text"], self.editor.get("1.0", "end-1c").encode("utf-8")

        def launch(self, command, extra=(), send=False, json_output=True):
            if self.busy:
                self.status.set("An operation is already running; wait or cancel it")
                return
            if command == "tx" and self.controller.remaining_delay() > 0:
                self.status.set(f"Transmission cooldown: {self.controller.remaining_delay():.1f}s remaining")
                return
            options = self.options() + list(extra)
            payload = None
            if send:
                send_options, payload = self.payload()
                options += send_options
            self.busy = True
            self.status.set(f"{command.capitalize()} running · decoding and verification in progress")
            started = time.monotonic()
            def work():
                try:
                    result = self.controller.run(command, options, payload, json_output)
                    self.events.put(("success", command, result, time.monotonic() - started))
                except Exception as exc:
                    self.events.put(("error", str(exc)))
            threading.Thread(target=work, daemon=True).start()

        def poll(self):
            try:
                while True:
                    event = self.events.get_nowait()
                    if event[0] == "qr":
                        self.qr_running = False
                        if event[1] == self.qr_generation:
                            self.draw_qr(event[2])
                        else:
                            self.queue_qr()
                        continue
                    self.busy = False
                    if event[0] == "error":
                        self.status.set(event[1])
                        messagebox.showerror("Data Pump", event[1])
                    else:
                        _, command, result, elapsed = event
                        if command in ("simulate", "rx"):
                            self.received(result)
                        elif command == "devices":
                            messagebox.showinfo("Audio devices", result.decode(errors="replace") or "No devices found")
                        self.status.set(f"{command.capitalize()} completed in {elapsed:.2f}s · cache {self.cache.used:,} bytes")
            except queue.Empty:
                pass
            self.root.after(80, self.poll)

        def received(self, result):
            packet = self.cache.add(result)
            for identifier in self.tree.get_children():
                if identifier not in self.cache.items:
                    self.tree.delete(identifier)
            identifier = packet["id"]
            name = packet.get("filename") or packet["data"].decode("utf-8", errors="replace")[:55].replace("\n", " ")
            values = ("Authenticated" if packet["authenticated"] else "Integrity checked", name, len(packet["data"]))
            if self.tree.exists(identifier):
                self.tree.item(identifier, values=values)
            else:
                self.tree.insert("", "end", iid=identifier, values=values)
            self.tree.selection_set(identifier)
            self.tree.see(identifier)
            self.show_selected()
            d = result.get("diagnostics", {})
            self.metrics.set(f"{d.get('bit_rate', 0):.0f} bit/s · {packet.get('corrected_bytes', 0)} corrected bytes · match {d.get('correlation', 0):.3f}")
            self.draw_plots(d)

        def selected(self):
            selection = self.tree.selection()
            return self.cache.items.get(selection[0]) if selection else None

        def show_selected(self, event=None):
            packet = self.selected()
            if not packet:
                return
            text = packet["data"].decode("utf-8", errors="replace") if packet["kind"] == "text" else f"{packet['filename']}\n{len(packet['data']):,} verified bytes\nChoose Save selected to write this file."
            self.preview.configure(state="normal")
            self.preview.delete("1.0", "end")
            self.preview.insert("1.0", text[:65536])
            self.preview.configure(state="disabled")

        def copy(self):
            packet = self.selected()
            if packet and packet["kind"] == "text":
                try:
                    text = packet["data"].decode("utf-8", errors="strict")
                except UnicodeDecodeError:
                    messagebox.showerror("Cannot copy binary data", "This payload is not valid UTF-8. Use Save selected to preserve the original bytes.")
                    return
                self.root.clipboard_clear()
                self.root.clipboard_append(text)
                self.status.set("Selected text copied to clipboard")

        def save(self):
            packet = self.selected()
            if not packet:
                return
            path = filedialog.asksaveasfilename(initialfile=packet.get("filename") or "message.txt")
            if path:
                try:
                    self.cache.save(packet["id"], path)
                    self.status.set("Saved to " + path)
                except OSError as exc:
                    messagebox.showerror("Save failed", str(exc))

        def clear(self):
            self.cache.clear()
            self.tree.delete(*self.tree.get_children())
            self.preview.configure(state="normal")
            self.preview.delete("1.0", "end")
            self.preview.configure(state="disabled")
            self.status.set("Received data cleared from application memory")

        def choose_key(self):
            path = filedialog.askopenfilename(title="Select shared symmetric keyfile")
            if path:
                self.vars["keyfile"].set(path)
                self.key_label.configure(text=pathlib.Path(path).name)
                self.scramble_control.configure(state="normal")
                self.dsss_control.configure(state="normal")

        def clear_key(self):
            self.vars["keyfile"].set("")
            self.vars["pad"].set("")
            self.scramble.set(False)
            self.dsss.set(False)
            self.key_label.configure(text="Encryption off")
            self.scramble_control.configure(state="disabled")
            self.dsss_control.configure(state="disabled")

        def choose_pad(self):
            path = filedialog.askopenfilename(title="Select the >1GiB pad bound to this key")
            if path:
                self.vars["pad"].set(path)
                self.status.set("Pad selected: " + pathlib.Path(path).name)

        def attach(self):
            path = filedialog.askopenfilename(title="Attach file or screenshot")
            if path:
                self.file_path = path
                self.file_label.configure(text=pathlib.Path(path).name)

        def detach(self):
            self.file_path = None
            self.file_label.configure(text="Text message")

        def list_devices(self):
            self.launch("devices", json_output=False)

        def simulate(self):
            self.launch("simulate", ["--snr", self.vars["snr"].get()], send=True)

        def transmit(self):
            self.launch("tx", ["--device", self.vars["device"].get(), "--tx-delay", "0"], send=True, json_output=False)

        def export(self):
            path = filedialog.asksaveasfilename(defaultextension=".wav", filetypes=[("Audio WAV", "*.wav")])
            if path:
                self.launch("tx", ["--output", path], send=True, json_output=False)

        def record(self):
            self.launch("rx", ["--device", self.vars["device"].get(), "--seconds", self.vars["seconds"].get()])

        def decode_file(self):
            path = filedialog.askopenfilename(filetypes=[("Audio WAV", "*.wav"), ("All files", "*")])
            if path:
                self.launch("rx", ["--input", path])

        def enter(self, event):
            if not self.ctrl_enter.get():
                self.transmit()
                return "break"

        def control_enter(self, event):
            if self.ctrl_enter.get():
                self.transmit()
            else:
                self.editor.insert("insert", "\n")
            return "break"

        def text_changed(self, event=None):
            if not self.editor.edit_modified():
                return
            self.editor.edit_modified(False)
            self.update_qr()

        def update_qr(self):
            self.qr_generation += 1
            if self.qr_timer:
                self.root.after_cancel(self.qr_timer)
            self.qr_timer = self.root.after(300, self.queue_qr)

        def queue_qr(self):
            self.qr_timer = None
            if self.qr_running:
                return
            text = self.editor.get("1.0", "end-1c")
            if not text or len(text) > 500:
                self.qr_canvas.delete("all")
                self.qr_canvas.create_text(92, 92, text="Type text for QR" if not text else "QR limit:500 characters", fill="#8cabbc", width=170)
                return
            generation = self.qr_generation
            self.qr_running = True
            def work():
                try:
                    data = self.qr_controller.run("qr", ["--input", "-", "--format", "pbm"], text.encode("utf-8"), json_output=False)
                except Exception:
                    data = None
                self.events.put(("qr", generation, data))
            threading.Thread(target=work, daemon=True).start()

        def draw_qr(self, data):
            self.qr_canvas.delete("all")
            if not data:
                self.qr_canvas.create_text(92, 92, text="QR unavailable", fill="#8cabbc")
                return
            try:
                tokens = data.decode("ascii").split()
                if tokens[0] != "P1":
                    raise ValueError("PBM format")
                width, height = map(int, tokens[1:3])
                if not 1 <= width <= 185 or width != height or len(tokens) != width * height + 3:
                    raise ValueError("QR size")
                # Integer scaling retains crisp modules. The white quiet zone is in PBM.
                scale = max(1, 185 // width)
                offset = (185 - width * scale) // 2
                self.qr_canvas.create_rectangle(offset, offset, offset+width*scale, offset+width*scale, fill="white", outline="")
                for y in range(height):
                    for x in range(width):
                        if tokens[3+y*width+x] == "1":
                            self.qr_canvas.create_rectangle(offset+x*scale, offset+y*scale, offset+(x+1)*scale, offset+(y+1)*scale, fill="black", outline="")
            except (ValueError, IndexError):
                self.qr_canvas.create_text(92, 92, text="QR unavailable", fill="#8cabbc")

        def draw_plots(self, d):
            waveform = d.get("waveform", [])[:1024]
            spectrum = []
            if waveform:
                values = waveform[:512]
                for k in range(64):
                    transform = sum(value * cmath.exp(-2j * math.pi*k*n/len(values)) for n, value in enumerate(values))
                    spectrum.append(20*math.log10(max(abs(transform), 1e-8)))
            for name, data in (("WAVEFORM", waveform), ("SPECTRUM", spectrum)):
                canvas = self.plots[name]
                canvas.delete("all")
                w, h = canvas.winfo_width(), canvas.winfo_height()
                canvas.create_line(0, h/2, w, h/2, fill="#203947")
                if len(data) > 1:
                    low, high = (min(data), max(data)) if name == "SPECTRUM" else (-max(1, max(map(abs, data))), max(1, max(map(abs, data))))
                    points = [coordinate for i, value in enumerate(data) for coordinate in (i*w/(len(data)-1), h-8-(value-low)/(high-low or 1)*(h-16))]
                    canvas.create_line(*points, fill="#7cddd2", width=1)
            canvas = self.plots["DIFFERENTIAL CONSTELLATION"]
            canvas.delete("all")
            w, h = canvas.winfo_width(), canvas.winfo_height()
            canvas.create_line(w/2, 0, w/2, h, fill="#203947")
            canvas.create_line(0, h/2, w, h/2, fill="#203947")
            points = d.get("constellation", [])[:512]
            norm = max((max(abs(x), abs(y)) for x, y in points), default=1) or 1
            for x, y in points:
                px, py = w/2+x/norm*min(w, h)*.42, h/2-y/norm*min(w, h)*.42
                canvas.create_oval(px-1.5, py-1.5, px+1.5, py+1.5, fill="#7cddd2", outline="")

        def close(self):
            self.controller.cancel()
            self.qr_controller.cancel()
            self.cache.clear()
            self.root.destroy()

    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()

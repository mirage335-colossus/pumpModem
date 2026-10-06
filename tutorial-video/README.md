# Data Pump: a practical introduction

Standalone production sources for one narrated beginner tutorial. This folder
does not change or build the application. Keep footage, voice models, key files,
downloaded tools, rendered frames and finished videos **outside this repository**.

`script.txt` is the readable narration. `storyboard.json` is its editable source,
with capture filenames, cues and illustrative scenes. `installation.txt` supplies
the setup links mentioned in the video. `sources.txt` records the claim checks.
`evidence.json` records the actual demonstrations and final delivery checks.
Run `python3 scripts/write_script.py` after editing the storyboard to refresh
the readable narration.

The walkthrough covers purpose and installation; matching sound settings;
encrypted Fast text and a 30,000-byte file; the approved keyed Robust simulation;
Legacy BPSK31; the terminal interface; QR scanning; and optional radio/optical use.
The phone is an explicitly labelled illustration, ending with an SMS draft.

## Production

Prerequisites: Python 3 with Pillow, NumPy and Piper TTS; an English Piper voice;
FFmpeg/ffprobe with X11/Pulse support; xdotool; Xvfb; an existing Data Pump GUI,
CLI and matching library. Use a private external tools directory. This project
does not install packages, change audio settings or compile application sources.
The reviewed render used FFmpeg 7.1.5 and the Python versions pinned in
`requirements.txt`.

1. Follow the repository's coordination instructions and claim the production
   directory, external outputs, chosen display, audio device and timing workload.
2. Copy existing application binaries into a fresh external working directory.
   Provide a disposable demo key named `Tutorial` and a 30,000-byte text fixture.
   `scripts/make_demo_file.py EXTERNAL_FILE.txt` recreates the reviewed fixture.
   Never publish a real encryption key. Prepare an independent listening receiver
   with matching key/settings before the Fast takes.
3. `scripts/supervise.py --root EXTERNAL_WORK` starts two GUI peers on display
   `:91`; `--robust` starts the simulation profile. This helper stops and reaps
   children when `EXTERNAL_WORK/stop` (or `stop-robust`) appears. Check the display
   is available before launching it. The helper writes a peers JSON file.
4. Review `scenarios/*.json` against the current 1280×1000 GUI. They contain
   absolute coordinates and example external paths; adapt those paths first.
   `scripts/capture.py SCENARIO --output EXTERNAL_TAKE.mp4 --tools TOOLS
   --display :91 --peers PEERS.json` records cursor movement. For live Fast/Legacy
   audio, also provide `--pulse-source` with the chosen output monitor. Simulation
   takes have no live output audio. Do not run capture against the user's desktop.
5. Inspect actual receive results. Save the received Fast attachment and compare
   all bytes with the source. Check the fresh Robust result before choosing its
   result cut; never infer reception from the planner's probability estimate.
6. For the Robust audio sample, compile `scripts/export_audio.cpp` against the
   existing matching library and run it with `KEYFILE EXTERNAL_OUTPUT.wav`.
   It exports a separate fixed-epoch waveform with the same approved profile.
   It is labelled separately in the video and is not the GUI simulation's audio.
7. Generate `command-qr.pbm` using `pump qr --text 'uname -a' --format pbm` and
   capture the real TUI into `tui-screen.txt`. Both files belong with external
   media. The illustrated QR scanner uses automatic Enter to submit the example
   command. This is a labelled illustration, not a claim of a hardware scanner test.
8. The current planner take runs at its recorded speed. The original takes and
   action timestamps are kept externally. Review the bit-result cut and update
   the storyboard cuts/cues if recording a new take.
9. Run `scripts/render.py --media EXTERNAL_MEDIA --output FRESH_EXTERNAL_OUTPUT
   --tools TOOLS --voice VOICE.onnx`. The tools directory contains
   `usr/bin/ffmpeg` and `usr/bin/ffprobe`. Supply its library path and Piper's
   Python path if using an extracted runtime. The voice JSON must sit beside
   the model. `--narration-only` prepares speech and the timeline for review.

The renderer creates H.264/AAC MP4, open captions, matching WebVTT, 18 chapters,
a SHA-256 digest and delivery/audio reports. Voice is normalized to approximately
−18 LUFS before mixing; modem sound is kept lower and reduced under narration.
The audible Robust excerpt gets a six-second narration gap and a slightly higher
level than the background Fast/Legacy recordings.
The output size gate is 30 MB. Check the final encoded video visually, decode
both complete streams, inspect speech/tone balance, and confirm source/media
separation before delivery. Stop all capture, GUI and display children, verify
audio settings, then release the coordination claims.

## Scope of the demonstrations

- Fast uses the default `Speakers / mic · short` profile and encryption. The
  30,000-byte fixture is deliberately compressible and is described that way.
- Robust uses 3600 Hz rate, 1500 Hz carrier, 3 dBm (about 2 mW), 150 dB
  path loss, −164 dBm/Hz noise, `gpsdo-xo`, target 6.326999095983183 dB/Hz,
  and `auto-keystream` with the selected key. The planner shows about 17.71
  seconds TX, 7.60 seconds reference CPU, >99% modeled RX and about 108×
  observer/receiver time. These are model estimates, not stopwatch results.
  Its projected GPU row is not a measured GPU result. The one-bit format is
  encrypted but has no authentication tag. The result is from a real simulation.
  The GPSDO model assumes disciplined modem carrier and sampling clocks;
  updating a computer's wall RTC from GPS alone does not establish this behavior.
- Legacy transmits the pangram using BPSK31 at 1500 Hz. The phone receive,
  selection, copy and SMS paste are illustrations; no Android decode or SMS
  delivery is asserted. Keep the illustration label visible.
- Hardware illustrations explain complementary protections. They are not
  photographs, product endorsements or a certification of a complete system.
- Windows instructions select Rev. Debian/Ubuntu instructions retain FLTK and
  show both the Latest release URL and the direct repository setup guide.
- The security introduction separates direct connection attacks from file trust.
  It mentions limited authenticator memory without claiming that small storage
  alone proves malware cannot be carried. The arcane serial jargon is omitted.

Finished media can be attached to a dedicated GitHub Release; it is not committed
to Git. Publishing is separate from local rendering.

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
encrypted Fast text and a 30,000-byte file; Legacy BPSK31; the terminal
interface; QR scanning; and optional radio/optical use. Robust controls remain
visible in the GUI, but Robust operation is unavailable pending reconnection to
the canonical native triad stack. This tutorial contains no Robust demonstration.
The phone is an explicitly labelled illustration, ending with an SMS draft.

## Production

Prerequisites: Python 3 with Pillow, NumPy and Piper TTS; an English Piper voice;
FFmpeg/ffprobe with X11/Pulse support; xdotool; Xvfb; an existing Data Pump GUI,
CLI and matching library. Use a private external tools directory. This project
does not install packages, change audio settings or compile application sources.
The reviewed render used FFmpeg 7.1.5 and the Python versions pinned in
`requirements.txt`.

1. Reserve the production directory and external outputs. Confirm the chosen
   display, audio device and timing-sensitive workload are available for the takes.
2. Copy existing application binaries into a fresh external working directory.
   Provide a disposable demo key named `Tutorial` and a 30,000-byte text fixture.
   `scripts/make_demo_file.py EXTERNAL_FILE.txt` recreates the reviewed fixture.
   Never publish a real encryption key. Prepare an independent listening receiver
   with matching key/settings before the Fast takes.
3. `scripts/supervise.py --root EXTERNAL_WORK` starts two GUI peers on display
   `:91`. This helper stops and reaps children when
   `EXTERNAL_WORK/stop` appears. Check the display is available before launching
   it. The helper writes a peers JSON file.
4. Review `scenarios/*.json` against the current 1280×1000 GUI. They contain
   absolute coordinates and example external paths; adapt those paths first.
   `scripts/capture.py SCENARIO --output EXTERNAL_TAKE.mp4 --tools TOOLS
   --display :91 --peers PEERS.json` records cursor movement. For live Fast/Legacy
   audio, also provide `--pulse-source` with the chosen output monitor. Do not run
   capture against the user's desktop.
5. Inspect actual receive results. Save the received Fast attachment and compare
   all bytes with the source.
7. Generate `command-qr.pbm` using `pump qr --text 'uname -a' --format pbm` and
   capture the real TUI into `tui-screen.txt`. Both files belong with external
   media. The illustrated QR scanner uses automatic Enter to submit the example
   command. This is a labelled illustration, not a claim of a hardware scanner test.
8. Run `scripts/render.py --media EXTERNAL_MEDIA --output FRESH_EXTERNAL_OUTPUT
   --tools TOOLS --voice VOICE.onnx`. The tools directory contains
   `usr/bin/ffmpeg` and `usr/bin/ffprobe`. Supply its library path and Piper's
   Python path if using an extracted runtime. The voice JSON must sit beside
   the model. `--narration-only` prepares speech and the timeline for review.

The renderer creates H.264/AAC MP4, open captions, matching WebVTT, one chapter
per storyboard scene, a SHA-256 digest and delivery/audio reports. Voice is normalized to approximately
−18 LUFS before mixing; modem sound is kept lower and reduced under narration.
The output size gate is 30 MB. Check the final encoded video visually, decode
both complete streams, inspect speech/tone balance, and confirm source/media
separation before delivery. Stop and reap capture, GUI and display children,
verify audio settings, and close timing-sensitive processes before reusing them.

## Scope of the demonstrations

- Fast uses the default `Speakers / mic · short` profile and encryption. The
  30,000-byte fixture is deliberately compressible and is described that way.
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

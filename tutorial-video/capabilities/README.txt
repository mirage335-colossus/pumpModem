DATA PUMP — SAFE TRANSFER. A DEMANDING PROVING GROUND.

A separate, concise overview film. Revision4 leads with safe data transfer and
the broader impetus to test practices for a reusable example project. The practical introduction in the parent
folder is unchanged. Narration, editable motion graphics and production code
live here; all footage, synthesized speech, screenshots and exports live outside
Git. No application source is modified.

Contents
  script.txt       Complete spoken script
  storyboard.json  Scene order, footage cuts and sound instructions
  visuals.py       Original editable illustrations and animation
  render.py        Voice synthesis, captions, mixing, chapters and H.264 export
  sources.txt      Evidence and interpretation behind the claims
  evidence.json    Completed production and verification details
  verify.py        Complete encoded decode, captions, frames and audio checks

Requirements
  Python, Pillow, NumPy, piper-tts and onnxruntime; FFmpeg with libx264/AAC.
  A separately installed Piper voice (.onnx and matching .onnx.json).
  Actual inputs named in evidence.json, stored in an external input directory.
  The existing tutorial requirements.txt identifies the earlier runtime; this
  production's evidence records its actual versions.

Example (all media paths must be outside the repository)
  python render.py --media /external/inputs --output /external/final \
    --tools /external/ffmpeg-root --voice /external/voice.onnx

The tools directory must contain usr/bin/ffmpeg and usr/bin/ffprobe. Set
LD_LIBRARY_PATH to its usr/lib/x86_64-linux-gnu when using the extracted Linux
FFmpeg tool bundle. --narration-only builds a timing preview. The renderer refuses
to overwrite an existing picture/final movie and rejects output inside this repo.

Presentation
  Wide 16:9 composition, synthetic English narration, actual application excerpts,
  original motion diagrams and selected actual Software Foundation screenshots.
  The recorded Fast file is deliberately compressible repeated text. The 500 kB
  physical trial is a separate documented result, explicitly labelled. Robust
  reception is simulation; its held received snapshot is labelled. Its audible
  excerpt is a separately generated native waveform using the same configuration.

Claim scope
  Demonstrated software and recorded results lead the story. Rankings such as
  first ever, decades ahead, or greatest since PGP are not stated without a
  comparative study. Neither the LPI model nor encryption guarantees invisibility
  or zero interference. The roughly two-week starting effort is the project owner’s estimate. A few
  hours, potentially one, is an explicit future target for comparable development
  breadth, not measured completion time.

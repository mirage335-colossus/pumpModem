# Short acoustic minimum airtime (2026-09-22)

This update applies one minimum-airtime selection rule across the short acoustic
profile's Expected SNR settings. Only `acoustic_short` can enable its compact
convolutional format. The original channel profiles retain their settings.

## Reproduction

Build the Release CLI and tests, then run:

```sh
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure -j 2 -R '^fast_acoustic_short'
ctest --test-dir build --output-on-failure -j 2 -R '^(fast_codec|fast_presets|fast_cli|gui_fast)$'
./build/pump fast-info --profile acoustic-short --expected-snr -6 --estimate-bytes 60
python3 docs/validation-data/fast/acoustic-short-20260922/compact-reference.py
```

The sampled tests use production encoding, waveforms and receive processing.
Noise is calibrated over the original 17.5 kHz channel with constant noise
density through training, payload and silence, rather than treating the narrowed
receive band as the reference. Echo amplitude is 0.30 at a 240-sample delay
(5 ms at 48 kHz), with up to ±100 ppm clock error. Sources cover UTF-8 text,
public and keyed transfers, incompressible files, in-band filenames and XZ.
The 3 dB 2.5 KiB target remains bounded at twelve seconds. At −6 dB, the
approximately ten-second assertion applies to minimal text; larger sources
must transmit more complete locally fixed coding cycles.

Controls reject noise-only input, incomplete acquisition and a corrupted
independent marker verification region. EOF and 5.5 seconds of absent complete
symbols cannot expose a source. Six seconds of fully scored absence can finish
reception. Codec tests retain missing-cycle positions, check malformed protected
flags/padding and wrong keys, and verify source withholding before physical end.

## Independent coding and profile evidence

[The independent compact reference](compact-reference.py) implements GF(65536)
parity, K=7 convolutional encoding, puncturing, profile hashing and whitening
without calling production coding code. Its [whole-wire hashes](compact-reference.log)
are frozen assertions in `test_fast_codec`. Existing classic, capacity and
[short-LDPC independent fixtures](../acoustic-short-20260921/README.md) remain.

[Preset timings](presets.json) include bootstrap, fixed coding cycles, training,
tracking and physical-end observation. The minimum fixture is 60 encoded bytes;
keyed capacity at this size is also checked by `fast_presets`.
[Profile isolation](profile-isolation.json) compares 172 CLI reports against
pre-change results for wire, SSB, FM and the original acoustic channel: no
fields changed. It includes classic/capacity defaults and every supported
integer Expected SNR, with a 2,560-byte estimate.

## Limits

These are deterministic generated-audio and Linux UI checks, not physical room
trials or statistical whole-file success rates. Rates below −6 dB can require
more than ten seconds even for the minimum fixture. Both peers need matching
software and settings. The compact SC automatic rate is capped at 1,000
symbols/s to retain the tested echo span; higher-rate selections use OFDM.

## Sampled results

The [−6 dB suite](sampled-minus6.log) passes all ten cases. Minimum public/keyed
UTF-8 text takes 9.796 seconds; named 32-byte attachments take 11.396 seconds;
2.5 KiB text takes 49.790 seconds; the keyed 2.5 KiB attachment takes 76.986
seconds. All payload bytes and metadata recover exactly, with corrected hard-bit
errors and no erased bit positions in the valid waveform fixtures.

At −3 dB, [public text](sampled-minus3-text.log) and the
[keyed 2.5 KiB file](sampled-minus3-file.log) both pass the same echo/clock
impairments. The smallest 60-byte XZ stream takes 9.513 seconds; the tested
45-byte UTF-8 message compresses to a larger stream and takes 10.985 seconds.
The keyed file takes 71.337 seconds. [Preset tests](preset-tests.log) check the
minimum-time constraint across the menu, code identity, validation and isolation.

The first broad runs overlapped the ordinary receiver calibration and both GUI
workflows. [Fast/shared GUI](fast-gui-initial.log) passed 39/40; the original
acoustic SNR subprocess hit its 300-second wall-clock limit. Native controls
passed in [FLTK](native-fltk-initial.log) (adapter/document) and
[Rev](native-rev-initial.log) (adapter/platform/1x/2x coordinates), while the
full workflow timed out in FLTK and missed its three-second replay frame cadence
in Rev. These timing failures are retained; assertions and deadlines were not
relaxed for reruns.

The [SNR rerun](fast-snr-rerun.log) passes all 92 existing cases in 377.07
seconds with the original per-subprocess 300-second limit. Combined with the
first run, all 40 selected Fast/shared-GUI checks pass.

The [FLTK workflow rerun](workflow-fltk-rerun.log) passes in 268.93 seconds.
The [Rev workflow rerun](workflow-rev-rerun.log), with the calibration paused,
still fails its existing three-second measured-frame replay assertion (seven
frames and six changes). Its native adapter/platform/coordinate checks pass;
the complete Rev workflow is not claimed as passing. The replay assertion and
unrelated adapter code remain unchanged.

[Ordinary compatibility](ordinary-compatibility.log) passes 28/29 cases,
including exact short bits, fixed intervals, pending rows, recovery, and sampled
physical-end behavior. The separate `differential_receiver_probability`
statistical calibration reaches CTest's 1,500-second default timeout. It had
also been paused briefly to isolate GUI timing; its completed portions and the
timeout are retained in the log. Its code and assertions are unchanged, and this
run does not establish full calibration coverage. The timed-out process was
terminated by CTest; no paused calibration remains.

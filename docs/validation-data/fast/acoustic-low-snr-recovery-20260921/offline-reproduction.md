# Offline full-source reproduction

`offline-full-source.cpp` streams a complete deterministic, integrity-protected
bootstrap and 15-byte encrypted source using the actual acoustic -10 dB preset:
16-QAM, LDPC 3/4, one frame per coding cycle, about 73.0898 baud, marker spacing
four intervals and pilot spacing 64 symbols. It never stores the full PCM
waveform. This diagnostic complements the live captures and the registered
`fast_pilot_presence` regression; it does not send or record audio.

From a built repository root:

```sh
c++ -std=c++20 -O2 -Iinclude \
  docs/validation-data/fast/acoustic-low-snr-recovery-20260921/offline-full-source.cpp \
  build/libdatapump_fast.a build/libdatapump.a \
  build/third_party/xz/liblzma.a -lcrypto -ldl -pthread \
  -o /tmp/fast-acoustic-full-source
/tmp/fast-acoustic-full-source clean
/tmp/fast-acoustic-full-source fade
/tmp/fast-acoustic-full-source phase
/tmp/fast-acoustic-full-source clock
/tmp/fast-acoustic-full-source echo
```

The program returns failure unless physical completion yields the exact source
bytes. `fade` zeros a 10-symbol-wide PCM section centered on the first four-symbol
pilot word, about 0.137 seconds at this preset. Its matched-filter effects cause
one failed pilot group while the subsequent waveform continues. `phase` applies
a permanent 180-degree carrier phase step at the beginning of that section.
`clock` uses +80 ppm transmit symbol timing and a separate +0.03 Hz carrier
offset. `echo` adds a static 0.45-gain, 800-sample/16.667 ms delayed copy. The
other cases contain no added noise. These controls are not a calibrated model
of the measured loudspeaker/microphone path.

Before the fix, clean, clock-offset and echo controls completed all 64
intervals, while the short fade produced the user's exact geometry error at
36.837 seconds, even though the intended signal continued for over eight
minutes. One failed pilot permanently cleared `marker_good`; later good pilots
could neither recover the group nor reset the six-second absence count before
the next sparse marker. The fixed fade and phase-step cases recover the entire
protected source with no failed LDPC frames. The reported end increases from
526.583 to 527.513 seconds because the corrected absence scorer's real silence
allowance covers a second completed pilot-group observation at this low baud.
`offline-observations.txt` preserves the observed before/after output.

The fix does not change the transmitted format or relax initial exact marker
admission. Marker framing, current physical presence, and group demapping
quality are separate decisions. Damaged groups retain neutral soft positions;
later known pilots resume reception. A coherent but phase-wrong pilot erases
its current group and restores only common phase for the following group,
without treating a phase step as frequency drift. Entirely absent intervals
remain deferred until later observed signal confirms their positions, so
trailing silence does not append phantom codec intervals.

A subsequent targeted regression covers a phase step halfway through a
scheduled marker at 5 baud. Its two halves cancel in a whole-word correlation,
even though signal continues throughout. Once cadence has already been
acquired, physical presence can additionally use four fixed 16-symbol marker
quarters, requiring at least three coherent, sufficiently energetic pieces
and sufficient aggregate coherence. This fallback cannot acquire a boundary
or set framing validity: initial acquisition and every accepted marker still
use the original whole-word quality and exact-sign checks.
The prior binary ends prematurely at 884.941 simulated seconds; the fixed
test retains four explicitly erased intervals and recovers the other eight
exactly, including recovery at the next valid full marker. Both acquired-stream
silence/noise replacement controls still complete. The observed logs are
`offline-marker-baseline.log` and `offline-marker-fixed.log`; reproduce with
`ctest --test-dir build --output-on-failure -V -R '^fast_pilot_presence$'`.

The regular `fast_pilot_presence` test additionally checks a 5-baud pilot fade,
recovery before any second full marker, a continuously noisy physical-end tail,
5.5 seconds of insufficient absence, input EOF after acquisition, and
noise-only non-acquisition. Existing modem vectors and the low-rate suite also
passed the presence fix. Source completion still depends on physical absence;
source syntax, correction, and authentication never manufacture that event.

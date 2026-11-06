# Gentle correction with a narrow public delay basis

This is the final bounded receiver candidate; no additional search followed.
The original one-tap, wide13-tap and gentle one-tap results remain unchanged.
`protocol.json` was frozen before this candidate's measurements.

The full-audio controller retains its gentle50ppm phase-only catch-up default,
1000ppm total rate allowance and10ppm/s slew. The receiver uses one public arrival
at−12ms and seven replicas at0,4,8,12,16,20,24ms delay, giving effective±12ms.
All taps have Gaussian prior power1/7. The predeclared coherence mixture is
0.1/0.5/1s and blocks remain0.5s. All tap/model nuisance dimensions are retained
in the existing receiver likelihood; there is no oracle choice of a tap or
model and no private clock trajectory.

The±12ms envelope exceeds the11ms worst-case cumulative **phase-only** relative
motion of two50ppm endpoints over110s. It does not cover arbitrary duration,
physical device-rate mismatch, the full1000ppm software allowance, propagation
delay motion, or an unknown initial offset. The ideal exactly dated physical
ADC/DAC clocks here are nominal. This is an explicit envelope for this diagnostic,
not an automatically qualified operational bank.

| Seed | Stable log-e | Corrected log-e | Earlier gentle one-tap corrected |
|---|---:|---:|---:|
|94002|214.139|136.435|37.037|
|94003|196.423|188.144|79.602|

All four raw decisions are correct and all four conditional gates exceed22.
Both seeds have appeared in earlier diagnostics; this protocol was fixed before
running their seven-tap outputs, but they are not newly generated field trials.
No success-probability confidence bound follows. Larger nuisance support costs
stable-channel evidence; it provides substantially better protection against
this tested changing delay/phase history. Evidence ratios are not reported as
sensitivity penalties in dB.

All waveform,8dB-Hz noise, Helper, padding and physical-null caveats in the parent
README still apply. In particular, post-ASRC noise is colored; all operational
admissions remain withheld. Helper is carried through the full audio map but
is excluded from the DATA projection, not used for timing assistance. None/Off
propagation is an explicit diagnostic control, not a fading qualification.

Campaign wall time30.490s. Receiver CPU2.965–3.229s per109s input, approximately0.027–0.030CPU s/input s, excluding source generation and audio conversion. No production code or hardware access was involved.

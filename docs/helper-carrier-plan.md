# Helper Carrier implementation and qualification plan

Updated 23 October 2026. The implementation reference and public ASCII waveform
are in [Helper Carrier](helper-carrier.md); actual checks and limits belong in
[validation](validation.md). Initial source investigation used revision
`a858f6f31d19431d3d15b077752f67a30cd1866f`.

## Governing requirements

Helper Carrier is an independent experimental reference transmission, optionally
assisting Robust reception when GPSDO hardware is unavailable. It can accompany
manual two-way traffic or operate alone. Preserve general-purpose configuration
options, including 1.5 kW. Radio operating conditions and authorization, including
any Special Temporary Authority, are the operator's responsibility. A public
seven-bit ASCII callsign format supports the identification objective; software
alone does not certify an installation's regulatory compliance.

Robust takes priority. Preserve its wire format, source-bit positions, logical
sample rates, configured data power/SNR meaning, next-poll progress, physical
completion, key/calendar addresses and full admitted search coverage. Resolve
bandwidth, dynamic range and CPU conflicts on the helper side. Successful
APSK-2097152 metadata decoding is optional and is not required for presence,
frequency observation or displaying a helper reception.

## Implemented design

1. Shared GUI controls below the clock/oscillator row: Power defaults to 0,
   requested power presets plus existing Robust presets; Ratio defaults to
   90 dB; High/Low/0 Hz side selection; independent 19-character printable ASCII
   callsign. Companion controls disable at zero power. Developer callsign edits
   copy one way without reconfiguring live receivers. Import/export is
   transactional. Extra window height preserves existing plot/body dimensions.
2. A separate final-device-rate generator bypasses payload Interleave, DSSS,
   masking and time-shifting streams. Physical 400 ms dwells, 4 ms ramps,
   an optional 8 ms amplitude/phase reversal centered at 240 ms, and a trailing public
   eight-symbol APSK-2097152 callsign frame leave most airtime a pure tone.
   The existing keyed dwell calendar is retained. Current Fake FHSS does not
   retune radio hardware.
3. Linear relative power scaling uses square-root power ratios and leaves peak
   headroom. The ratio ceiling reduces helper power rather than raising Robust
   power. Separation grows with the effective ratio, using a conservative
   periodic digital leakage bound. Both short/long profiles share the larger
   occupied width, including gating allowance. Integrated High placement also
   lies beyond the original logical sample rate plus 250 Hz, allowing the
   existing physical capture converter to isolate the helper. Unsupported placement
   disables the receive observer without changing Robust reception. An enabled-helper
   transmission fails before payload PCM rather than emitting unscaled data or
   silence; ordinary transmission remains available with helper power zero.
4. Native/browser playback adds the helper after payload resampling. A raw
   capture observer sees it before payload downsampling and scheduled capture
   exclusion. Capture gaps reset evidence rather than becoming observed silence.
   Existing logical transmit plots remain payload previews; use input/full-duplex
   capture or generated WAV for the actual composite signal.
   Compile-time channel co-residence and a runtime tracker capacity bound keep
   observer storage inside the original sampled-channel reservation, including
   mode handoff. Helper options do not shrink the modem bank or audio reserve.
5. Noise retains its ordinary low-power noise component when the helper is on.
   The separate Helper action and `pump listen --helper-only` transmit the
   reference without payload/noise. Finite endpoints taper without extending
   Robust bits or its physical-end rule. Boundary partial dwells may therefore
   contain incomplete metadata; complete interior dwells carry the identifier.
6. A bounded independent tone tracker produces stable helper rows. Evidence
   groups begin with four 100 ms fits and grow logarithmically with history;
   admission combines a 6 dB fit-cell level gate
   with a conditional Gaussian 1% total false-admission budget over searched
   cells and repeated looks. Admitted tones retain the 6 dB level gate until
   six seconds of observed absence retires admission. Six seconds of observed
   presence confirms a row;
   a shorter reception can complete after six seconds of observed absence.
   Brief fades retain identity. Helper rows confer no payload bit/copy/save
   permissions and never hold a data reception open.
7. Frequency observations support diagnostics and manual alignment. Robust's
   search, hypothesis order, coverage and evidence accounting remain unchanged.
   Candidate reordering was removed after receiver review showed the present
   FFT path still scores the complete bank before admitting signals; the
   amplitude path builds its own bank. Neither establishes a compute benefit.

## Deliberate helper-side tradeoffs

- The metadata rate is 125 baud rather than approaching 300 baud, improving the
  occupied spectrum while retaining a long coherent tone. There is no helper
  metadata decoder or encrypted timestamp implementation in this version.
  Empty callsign means pure tone with ramps and optional punctuation. Streaming
  output omits punctuation to keep phase independent of future stop requests;
  fixed-end WAVs retain it where the straight-tone guards fit. No replacement Robust
  decoder is introduced.
- Receiver tracking covers a configured center within at least +/-20 Hz and
  at most +/-100 Hz. Peers currently match helper power/data reference/ratio/side
  and modem geometry because placement depends on that ratio. Unknown full-band
  helper discovery remains future work; callsign content does not retune it.
- Low and co-channel 0 Hz offset are retained as choices but report unavailable
  in the integrated path until separate isolation is qualified. Literal DC
  audio is not generated. A new receive FIR was rejected for this version:
  correlated filtered noise would invalidate the existing detector's independent
  noise model. The actual unisolated 1.5 kW / 4 W sampled case admitted an extra
  bit; this is a preservation blocker, not an acceptable sensitivity tradeoff.
  The radio must also pass the wider helper frequency; ordinary voice filters
  may exclude it. Lower original modem carrier/rate configurations can help
  narrowband experiments, without changing the receive algorithm.
- Native output uses device-rate composition, including very-low-rate Robust
  modes. Equal-rate sampled simulation retains ordinary Robust and reports
  helper unavailable; helper-only simulation is synchronously rejected. A future
  true multirate physical simulation can remove that restriction. Its AWGN reference stays
  with payload power, not total composite power.
- A single tone does not independently determine RF offset, Doppler, sample
  clock error, absolute UTC or keyed hop ordinal. This version supplies offset
  diagnostics and makes no automatic synchronization, CPU-ceiling or
  frequency-range improvement claim. Future automatic centering/pruning
  requires robust fallback and sensitivity evidence rather than assumptions
  about tone association.

## Validation and acceptance

Use focused independent vectors and actual sampled paths first, then the normal
applicable regression groups through `./build.sh`. Existing tests and their
acceptance limits remain intact. Cover exact disabled PCM/noise, unchanged
short/raw/interval wire vectors, next-poll `0`, `00`, `001` progress, complete
payload-symbol absence, stable helper rows, helper-only/noise operation,
endpoint/dwell transitions, callsign edits during active transmission and
short/long profile placement. Test the actual data decoder with mixed helper
PCM as well as detecting the helper itself.

Spectral fixtures must measure the actual planned data band, include real-carrier
images, and normalize finite-burst leakage over active source duration rather
than zero-padded FFT span. Keep periodic analytic bounds distinct from sampled
finite-vector evidence. Hardware spurs, quantization, phase noise, compression,
near-threshold sensitivity and end-to-end CPU margin need separate qualification.
At a 90 dB ratio a full-scale 16-bit helper can leave a weaker sine near one LSB;
bit depth alone proves neither failure nor success after narrow filtering.

Run normal contract and affected GUI/audio/browser checks for the final source.
Use meaningful sanitizer coverage, recording environment limitations explicitly.
Native rendering requires an available display and physical radio/DAC behavior
requires measurements; neither is inferred from shared GUI tests. Record actual
results, initial failures, corrections and omitted/unavailable coverage in
`docs/validation.md`. Publication, certification and promotion remain separate
operations and are not authorized by implementation or diagnosis alone.

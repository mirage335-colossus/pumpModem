# HF Robust Modem simulation configurations — 18 November 2026

Open this file with **Open list** in the [Robust test list editor](tools-endUser/robust-test-list-editor.html). Each title gives the observed result and bit-pattern duration. Expand **other lines preserved** under the editable fields for the physical channel, slot cadence, individual trials and evidence. These are historical measurements. Editing a field changes the configuration and leaves the recorded result attached as a note.

<a id="simulation-conditions"></a>

The first seven blocks pair the main stable and severe results. The remaining blocks give the power and duration sweeps, alternative rates and incomplete trials. Repeated seeds and controls at the same settings share a block. A quiet decision on a transmitted bit is an erasure. Quiet on a silent control is correct. An ambiguous decision means the receiver withheld the bit. A timeout, plan or ideal-observer calculation supplies no receiver decision.

Reported bit decisions use the receiver’s Current admission policy unless a note names another policy. The transmitter used no Helper. The configured receiver knew the public calendar and keyed sequence, used a broad arrival search from −7 to +7.07 s, and scored both bit alternatives. The 30 MHz shift was an oscillator reference. Fake FCC imposed the dwell schedule in baseband. The simulator used a 6 kHz sample rate and −164 dBm/Hz white noise. With 180 dB loss, 2 mW gives nominal received C/N₀ of −12.99 dB-Hz and 100 mW gives +4 dB-Hz. Target SNR sets the planned waveform length in discrete steps. Historical overrides of received C/N₀ are identified in their own blocks.

Stable runs retained the receiver’s HF Worst, 3× and Each 65% fields and bypassed the propagation impairments only in the simulator. The severe GUI model included bounded common and relative Doppler, a delayed echo and frequency-selective modulation. The independent Gaussian model used two equal-average-power fading taps at 0 and 7 ms with 30 Hz full 2σ Doppler width and the selected Each 65% modulation. These experiments did not measure site-specific lightning or interference. Bit duration is transmit pattern length, cadence is the time between scheduled slots, and receiver scoring times are processing wall times.

The [operating guide](docs/indexes/robust.md) and [campaign results](docs/validation.md#bounded-2-mw-hf-fhss-investigation--18-november-2026) provide the maintained interpretation. Per-block receipt paths refer to the local investigation records. The separate [manual capacity list](_tests-RobustModem.md) supplies the eight cases for the strict 16-field capacity runner. This simulation list uses the visual editor’s additional Baseband Carrier and Target SNR fields.


- 2 mW, stable | 3.6 Hz unspread | Target −10 | 962.4 s/bit | Both bits and two complete messages decoded
<a id="stable-2mw-complete-messages"></a>
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-10	Target SNR dB/1Hz

Open “other lines preserved” on each card for its historical measurements and sources. Target and the other waveform fields can be edited. The first seven cards pair the principal results. No tested severe 2 mW setting recovered a bit. At 100 mW, Target −21 recovered one bit under both severe fading models.
Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
Slot cadence 990 s. Bit 0 at seed 940600 and bit 1 at seed 940601 decoded. A separate noise-only slot at seed 940602 scored quiet. The planner switched from five to six tiles between Targets −9.67578125 and −9.6796875 dB-Hz. The scored six-tile setting was −10.
The complete receiver decoded the byte e on seeds 940600 and 950101. Each replay returned 0, 0, 1 and two physical quiet slots. Both EOF controls passed. Five silent slots on a separate matched replay produced zero messages.
The 4,964.5535 s observation took 521 and 563 s of offline receiver and audio work on the Ryzen 5 PRO 5650U with 24 workers. Peak reservations were 8.50 and 9.00 GiB. The holdout interval lacked all its input at the starting commit, leaving sustained backlog performance unresolved.
The native e transmission used 5.78227 J. In one matched stable trial WSJT-X repeated the same byte 228 times using 5.76384 J, with no decoded text in 330 complete listening slots. Both full-duration noise controls produced no messages. The common observation cutoff was 4,964.5535 s. Live delivery latency was unmeasured.
In a separate single-frame comparison at both 2 mW and 100 mW, standard FT8 returned zero decoded messages in two trials per power on each of the stable, severe GUI and Gaussian paths. Each 77-bit FT8 frame lasted 12.64 s. Two strong stable controls decoded correctly. [Single-frame comparison](docs/validation.md#standard-ft8-single-frame-comparison).
Evidence [1](local-evidence/lowrate-2mw-native/stable-threshold-results.md) · [2](local-evidence/2mw-capacity-lowrate-stable-m10-message.json) · [3](local-evidence/2mw-stable-m10-message-holdout-950101.json) · [4](local-evidence/lowrate-2mw-native/stable-e-five-slot-silence.json) · [5](local-evidence/ft8-reference-20261118/repeated-alarm.md)


- 2 mW, severe GUI | 3.6 Hz unspread | Target −10 | 962.4 s/bit | Both transmitted bits erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-10	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 990 s. The transmitted zero at seed 940600 and one at seed 940601 both scored quiet. The zero completed on an EPYC 4565P after an earlier host reached a 480 s simulation deadline without a decision.
Evidence [1](local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-severe-m10.json) · [2](local-evidence/lowrate-2mw-native/severe_6tiles_seed940601_bit1.json)


- 2 mW, stable | 36 Hz × 10, hopping | Target −12.99 | 1,797.6 s/bit | Both bits decoded
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 1800 s. Bit 0 at seed 940600 and bit 1 at 940601 decoded. The separate empty slot at 940602 scored quiet. The modeled energy of bit 0 was 2.571 J.
The planner switches from six to seven tiles between Target −12.45 and −12.46 dB-Hz. The successful trials used the Target shown. The −12 control used the immediately shorter pattern.
Evidence [1](local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12p98970004336-admission-seed940600-bit0.json) · [2](local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12p98970004336-admission-seed940601-bit1.json) · [3](local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12p98970004336-admission-seed940602-bit0-noise.json) · [4](local-evidence/lowrate-2mw-runpod/2mw-original-stable-tile-thresholds.json) · [5](local-evidence/lowrate-2mw-native/fhss-stable-bracket-source-energy.json)


- 2 mW, severe GUI | 36 Hz × 10, hopping | Target −12.99 | 1,797.6 s/bit | Both bits erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 1800 s. Bit 0 at seed 940600 and bit 1 at 940601 both scored quiet. Under the same link budget and the same two seeds the stable channel decoded both bits. The local 24-worker slot quote was about 19.2 GiB, above the 9 GiB local allowance.
Evidence [1](local-evidence/hf-2mw-gpu-20261118/long36.json) · [2](local-evidence/hf-100mw-extended-20261118/2mw-admission-active-940601-1-target--12.98970004336.json)


- 2 mW, Gaussian | 36 Hz × 10, hopping | Target −12.99 | 1,797.6 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Gaussian propagation (--channel-model itu-disturbed). Two independent taps at 0 and 7 ms had 30 Hz full 2σ Doppler width, plus Each 65% modulation. The receiver retained its severe GUI search.
The public slot cadence was 1800 s. Bit 0 at seed 940600 scored quiet. DATA presence was −9.060 against 13.102 required. The CPU-only EPYC 4565P scored the slot in 927.78 s.
Evidence [1](local-evidence/gaussian-2mw-runpod-cpu/results/original-admission.json)


- 100 mW, severe GUI | 36 Hz × 10, hopping | Target −21 | 12,069.6 s/bit | One of three bits decoded
<a id="severe-gui-100mw-result"></a>
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-21	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 12090 s. Bit 1 at seed 940601 decoded with a 6.353 separation against 5.293 required. Bit 0 at seeds 940600 and 940603 gave ambiguous erasures with gaps 4.747 and 0.281 respectively. The latter trial favored bit 1.
On the successful seed, both the +4 and −20 Target controls erased the same transmitted one. The scored whole-dB comparison demonstrates a working setting on that realization.
Evidence [1](local-evidence/hf-100mw-extended-20261118/admission-active-940601-1-target--21.json) · [2](local-evidence/hf-100mw-extended-20261118/admission-active-940603-0-target--21.json) · [3](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m21-admission-seed940600-bit0.json)


- 100 mW, Gaussian | 36 Hz × 10, hopping | Target −21 | 12,069.6 s/bit | One of two bits decoded
<a id="gaussian-100mw-result"></a>
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-21	Target SNR dB/1Hz

Gaussian propagation (--channel-model itu-disturbed). Two independent taps at 0 and 7 ms had 30 Hz full 2σ Doppler width, plus Each 65% modulation. The receiver retained its severe GUI search.
The public slot cadence was 12090 s. Bit 1 at seed 940601 decoded with a gap of 5.616 against 5.293 required. Bit 0 at seed 940603 gave an ambiguous erasure. Its 4.949 gap favored the correct bit. The seeds were paired with the severe GUI trials.
Scoring took 1878.21 s and 2552.80 s on the full RTX PRO 6000 Blackwell Server Edition. Only Current admitted the transmitted one.
Evidence [1](local-evidence/hf-100mw-gaussian-transfer-20261118/results/admission-active-940601-1-target--21.json) · [2](local-evidence/hf-100mw-gaussian-transfer-20261118/results/admission-active-940603-0-target--21.json)


- 2 mW, stable | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 270 s. Bit 0 at seed 940600 scored quiet. The local Ryzen scored it in 796.55 s.
Evidence [1](local-evidence/lowrate-2mw-native/original_stable_2mw_p4_admission.json)


- 2 mW, stable | 36 Hz × 10, hopping | Target −6.97 | 513.6 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-6.97	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 540 s. Bit 0 at seed 940600 scored quiet after 216.85 s on the RTX PRO 4500. A local attempt on the same capture reached its 1500 s deadline. A separate, unscored three-tile plan at Target −9 gave 770.4 s per bit, a 780 s cadence and a 13.04 GiB slot quote.
Evidence [1](local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m6p97-admission-seed940600-bit0.json) · [2](local-evidence/lowrate-2mw-native/original_stable_2mw_m6p97_admission.json) · [3](local-evidence/lowrate-2mw-native/original_stable_2mw_m9_plan.json)


- 2 mW, stable | 36 Hz × 10, hopping | Target −10 | 1,027.2 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-10	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 1050 s. Bit 0 was transmitted at seed 940600. The four-tile pattern scored quiet.
Evidence [1](local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m10-admission-seed940600-bit0.json)


- 2 mW, stable | 36 Hz × 10, hopping | Target −11 | 1,284 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-11	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 1290 s. Bit 0 was transmitted at seed 940600. The five-tile pattern scored quiet.
Evidence [1](local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m11-admission-seed940600-bit0.json)


- 2 mW, stable | 36 Hz × 10, hopping | Target −12 | 1,540.8 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 1560 s. Bit 0 was transmitted at seed 940600. The six-tile pattern scored quiet. DATA presence was 7.628 against 13.236 required and the bit gap was 2.683 against 5.293.
Evidence [1](local-evidence/lowrate-2mw-runpod/stable2-gpu-target-m12-admission-seed940600-bit0.json)


- 2 mW, severe GUI | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. Bit 0 at seed 940600 scored quiet. The local Ryzen scored this slot in 662.72 s with 24 workers, 2.455 times its 270 s cadence.
Evidence [1](local-evidence/sensitivity-2mw-row6-target-p4-seed940600-bit0.json)


- 2 mW, severe GUI | 36 Hz × 10, hopping | Target −6.97 | 513.6 s/bit | Deadline before bit decision
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-6.97	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 540 s. The local run for bit 0 at seed 940600 reached its 1200 s deadline. The slot quote was 7.03 GiB. The separate ideal observer gave 11.8767% equal-prior binary error with exact signal, timing and channel knowledge.
Evidence [1](local-evidence/sensitivity-2mw-row6-target-m6p97-seed940600-bit0.json) · [2](local-evidence/2mw-oracle-36hz10x-target-m6p97-seed940600.json)


- 2 mW, severe GUI | 36 Hz × 10, hopping | Target −16 | 3,595.2 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-16	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 3600 s. Bit 0 at seed 940600 scored quiet. The observed gap of 3.925 favored the wrong bit against 5.293 required.
Evidence [1](local-evidence/hf-2mw-gpu-20261118/long36m16.json)


- 2 mW, severe GUI | 36 Hz × 10, hopping | Target −18 | 5,906.4 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-18	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 5910 s. Bit 0 at seed 940600 scored quiet after 1882.69 s of GPU-assisted scoring. DATA presence was −11.326 and bit separation was 0.455. An earlier explicit 64 GiB GPU attempt stopped at the workspace limit without a decision.
Supplying the true arrival to one diagnostic left just a 0.0312 correct-bit gap. A separate diagnostic that supplied the true common phase yielded a 16.714 gap with a persistent gain model. Resetting state every 4 or 16 s gave gaps favoring the wrong bit by 1.100 or the correct bit by 3.180. Those diagnostics supplied information unavailable to the receiver.
Evidence [1](local-evidence/hf-2mw-gpu-20261118/canonical/autolong36m18.json) · [2](local-evidence/hf-2mw-gpu-20261118/canonical/long36m18.json) · [3](local-evidence/hf-2mw-gpu-20261118/known-lag-evaluator.json) · [4](local-evidence/hf-2mw-gpu-20261118/common-phase-evaluator.json) · [5](local-evidence/hf-2mw-gpu-20261118/window-evaluator/result.json) · [6](local-evidence/hf-2mw-gpu-20261118/window-evaluator/result16.json)


- 2 mW, severe GUI | 36 Hz × 10, hopping | Target −20 | 9,501.6 s/bit | Stopped before scoring
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-20	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 9510 s. The cloud plan quoted 105.91 GB. The trial stopped during setup following weak evidence in the shorter runs. The separate −21 plan gave 12069.6 s per bit and was also unscored.
Evidence [1](local-evidence/hf-2mw-gpu-20261118/canonical/pauto36m20.json) · [2](local-evidence/hf-2mw-gpu-20261118/p36m21-local-plan.json)


- 2 mW, severe GUI | 0.01 Hz unspread | Target −12.99 | 57,600.4 s/bit | Planning only
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
0.01 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 67,230 s. At Target +4 and −12.98970004336 with hopping off the planner selected the same 57,600.4 s waveform and quoted 219.68 GiB. Neither run scored a bit. Other unspread nonhopping planning runs used 0.1, 1, 3.6, 6, 12 and 36 Hz.
Evidence [1](local-evidence/lowrate-2mw-native/plan-off-0.01hz-target-12.9897.json) · [2](local-evidence/lowrate-2mw-native/plan-off-0.01hz-target4.json) · [3](docs/validation.md)


- 2 mW, severe GUI | 0.01 Hz unspread, hopping | Target −12.99 | no bit pattern | Rejected during waveform planning
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
0.01 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

The planned channel had the severe GUI settings above.
The 0.4 s Fake FCC dwell has no protected bit-bearing cells for this unspread rate. Waveform planning also rejected 0.1, 1, 3.6, 6, 12 and 36 Hz at 1× under the same hopping settings. There was no transmitted bit or receiver outcome.
Evidence [1](local-evidence/lowrate-2mw-native/plan-on-0.01hz-target-12.9897.json) · [2](local-evidence/lowrate-2mw-native/plan-on-36hz-target-12.9897.json)


- 2 mW, stable | 3.6 Hz unspread | Target +4 | 160.4 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
Slot cadence 210 s. The receiver scored bit 0 as quiet on seed 940600. The Ryzen 5 PRO 5650U took 12.91 s to score the slot.
Evidence [1](local-evidence/lowrate-2mw-native/short_target_stable_2mw.json) · [2](local-evidence/lowrate-2mw-native/stable-threshold-results.md)


- 2 mW, stable | 3.6 Hz unspread | Target −6.5 | 481.2 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-6.5	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
Slot cadence 510 s. The receiver scored bit 0 as quiet on seed 940600. The Ryzen 5 PRO 5650U took 38.88 s to score the slot.
Evidence [1](local-evidence/lowrate-2mw-native/stable_3tiles_seed940600_bit0.json) · [2](local-evidence/lowrate-2mw-native/stable-threshold-results.md)


- 2 mW, stable | 3.6 Hz unspread | Target −8 | 641.6 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-8	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
Slot cadence 690 s. The receiver scored bit 0 as quiet on seed 940600. The Ryzen 5 PRO 5650U took 60.01 s to score the slot.
Evidence [1](local-evidence/lowrate-2mw-native/stable_4tiles_seed940600_bit0.json) · [2](local-evidence/lowrate-2mw-native/stable-threshold-results.md)


- 2 mW, stable | 3.6 Hz unspread | Target −9 | 802 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-9	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
Slot cadence 840 s. The receiver scored bit 0 as quiet on seed 940600. The Ryzen 5 PRO 5650U took 75.28 s to score the slot.
This five-tile waveform was the immediately shorter tested length than the six-tile success.
Evidence [1](local-evidence/lowrate-2mw-native/stable_5tiles_seed940600_bit0.json) · [2](local-evidence/lowrate-2mw-native/stable-threshold-results.md)


- 2 mW, stable | 3.6 Hz unspread | Target −12.99 | 1,764.4 s/bit | Bit 0 decoded
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
Slot cadence 1,800 s. Bit 0 at seed 940600 decoded. Stable bit separation was 52.918 against 5.293 required. The Ryzen scored the slot in 182.45 s.
Evidence [1](local-evidence/lowrate-2mw-native/matched_target_stable_2mw.json)


- 2 mW, severe GUI | 3.6 Hz unspread | Target +4 | 160.4 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 210 s. Bit 0 at seed 940600 scored quiet in 14.75 s on the Ryzen. Modeled bit-zero energy was 0.321 J.
On a separate ten-slot high-backlog replay at seed 85000, all six transmitted DATA slots and all four physical quiet slots scored quiet. No message decoded. Receiver and audio processing used 0.0707 s per input second. The predeclared interval did not qualify sustained service. The separate fine-continuation capacity probes hit their 90 s limits.
Evidence [1](local-evidence/lowrate-2mw-native/admission-off-3.6hz-target4.json) · [2](local-evidence/2mw-capacity-lowrate-p4-backlog.json) · [3](local-evidence/2mw-capacity-lowrate-p4.json) · [4](local-evidence/lowrate-2mw-native/emitted-energy.txt)


- 2 mW, severe GUI | 3.6 Hz unspread | Target −12.99 | 1,764.4 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 1,800 s. Bit 0 at seed 940600 scored quiet. The 0.6 Hz fine-chip rate prevented short-block Doppler tracking. The Ryzen scored all samples in 218.14 s using a 6.09 GiB slot allowance. Modeled bit-zero energy was 3.532 J.
Evidence [1](local-evidence/lowrate-2mw-native/admission-off-3.6hz-target-12.9897.json) · [2](local-evidence/lowrate-2mw-native/emitted-energy.txt)


- 2 mW, severe GUI | 3.6 Hz unspread | Target −14 | 2,245.6 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-14	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 2,280 s. Bit 0 at seed 940600 scored quiet. The Ryzen took 342.34 s within its 9 GiB allowance. The next 2,406 s tile exceeded the allowance at planning.
Evidence [1](local-evidence/lowrate-2mw-native/local_limit_target_m14.json) · [2](local-evidence/hf-2mw-cpu-20261118/larger-plans.json)


- 2 mW, severe GUI | 3.6 Hz unspread | Target −19 | 7,378.4 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-19	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 7,410 s. Bit 0 at seed 940600 scored quiet. The EPYC 4565P took 309.25 s to score this slot.
Evidence [1](local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-severe-m19.json)


- 2 mW, severe GUI | 3.6 Hz unspread | Target −21.5 | 13,473.6 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-21.5	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 13,530 s. Bit 0 at seed 940600 scored quiet. Its gap was 5.288 against 5.293 required, and signal presence was −14.773 against +11.156 required. The EPYC 4565P took 1,029.78 s for scoring. Longer Targets −21.75 and −22 were only planned at 14,275.6 and 15,238 s.
Evidence [1](local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-severe-m21p5.json) · [2](local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-plan-m21p75.json) · [3](local-evidence/hf-2mw-cpu-20261118/cpu2/cpu2-plan-m22.json)


- 2 mW, severe GUI | 30 Hz × 12, hopping | Target +4 | 308 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
12x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
30 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 330 s. Bit 0 at seed 940600 scored quiet. The Ryzen needed 977.05 s for the slot. At Target approximately −12.99 the planner selected 1,848 s and quoted 19.36 GiB. That longer setting received no bit decision.
Evidence [1](local-evidence/sensitivity-2mw-row6-rate30-spread12-target-p4-seed940600-bit0.json) · [2](docs/validation.md)


- 2 mW, severe GUI | 60 Hz unspread | Target −12.99 | 1,752 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
60 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 1,770 s. Bit 0 at seed 940600 scored quiet despite short-block Doppler tracking at 10 fine chips/s. The bit gap was 4.514 against 5.293 required. On the same Blackwell host the full-slot time fell from 1,018.68 to 272.62 s after moving segmented search to the GPU. At the tested transform sizes the separate Tensor Core implementation took 1.87–4.01 times the FP32 cuFFT time. [GPU measurements](docs/triad-gpu-optimization.md).
Evidence [1](local-evidence/lowrate-2mw-runpod/60hz1x-target-m12p9897-seed940600-bit0.json) · [2](local-evidence/lowrate-2mw-runpod/60hz1x-target-m12p9897-segmented.json)


- 2 mW, severe GUI | 60 Hz unspread | Target −19 | 7,296 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
60 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-19	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 7,320 s. Bit 0 at seed 940600 scored quiet with a gap of 0.019 against 5.293 required. Scoring took 1,351.62 s. A separate Target −22 plan selected 15,168 s and produced no receiver decision.
Evidence [1](local-evidence/lowrate-2mw-runpod/60hz1x-target-m19-segmented-seed940600-bit0.json) · [2](local-evidence/lowrate-2mw-runpod/60hz1x-target-m22-planning-only.json)


- 2 mW, severe GUI | 72 Hz × 5, hopping | Target +4 | 128.8 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
5x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
72 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 150 s. Bit 0 at seed 940600 scored quiet. Ryzen scoring took 346.97 s. A matched Automatic GPU run fell from 53.01 to 34.40 s with the same decision. An ideal observer supplied with exact timing, channel and bit presence had a conditional average forced-bit error of 27.88%.
Evidence [1](local-evidence/sensitivity-2mw-row6-rate72-spread5-target-p4-seed940600-bit0.json) · [2](local-evidence/2mw-oracle-72hz5x-target-p4-seed940600.json) · [3](local-evidence/hf-2mw-gpu-20261118/canonical/auto72.json)


- 2 mW, severe GUI | 72 Hz × 5, hopping | Target −5 | 386.4 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
5x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
72 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-5	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 390 s. Bit 0 at seed 940600 scored quiet. Ryzen scoring took 1,182.06 s. The matched ideal observer had 15.42% conditional average forced-bit error.
Evidence [1](local-evidence/sensitivity-2mw-row6-rate72-spread5-target-m5-seed940600-bit0.json) · [2](local-evidence/2mw-oracle-72hz5x-target-m5-seed940600.json)


- 2 mW, severe GUI | 72 Hz × 5, hopping | Target −12.99 | 1,803.2 s/bit | Capture and ideal observer only
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
5x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
72 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.989700043360187	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 1,830 s. The 1 GiB preflight generated the seed-940600 capture. The full receiver needed approximately 19.18 GiB and did not score a bit. The evaluator knew the channel, timing and both possible bit patterns and measured 1.3378% conditional average forced-bit error.
Evidence [1](local-evidence/2mw-oracle-72hz5x-target-m12p9897-native-preflight.json) · [2](local-evidence/2mw-oracle-72hz5x-target-m12p9897.json)


- 2 mW, Gaussian | 72 Hz × 5, hopping | Target +4 | 128.8 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
5x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
72 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Gaussian propagation (--channel-model itu-disturbed). Two independent taps at 0 and 7 ms had 30 Hz full 2σ Doppler width, plus Each 65% modulation. The receiver retained its severe GUI search.
Slot cadence 150 s. Under independent Gaussian fading, bit 0 at seed 940600 scored quiet under all admission policies. CPU scoring took 341.80 s. The matched ideal observer had 28.5742% conditional average forced-bit error. The earlier 1 GiB preflight generated the capture without scoring.
Evidence [1](local-evidence/sensitivity-2mw-row6-rate72-spread5-target-p4-itu-disturbed-seed940600-bit0.json) · [2](local-evidence/2mw-oracle-itu-72hz5x-target-p4-seed940600.json) · [3](local-evidence/2mw-adapter-itu-preflight-128.json)


- 2 mW, severe GUI | 360 Hz unspread | Target −12.99 | 1,751.2 s/bit | Bit 0 erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
2 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
360 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 1,770 s. Bit 0 at seed 940600 scored quiet after 414.05 s on the GPU host. Increasing subcarrier spacing to 75 Hz produced a gap of 1.561 against 5.293 required. A separate hopping-enabled plan at the same nominal Target selected 1,768.8 s and was not scored.
Evidence [1](local-evidence/lowrate-2mw-runpod/2mw-360hz1x-nohop-target-m12p9897-seed940600-bit0.json) · [2](local-evidence/hf-2mw-gpu-20261118/p360.json)


- 6.31 mW, stable | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
6.30957344480193 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 270 s. Bit 0 at seed 940600 scored quiet at a physical C/N₀ of −8 dB-Hz in the fixed-duration power sweep.
Evidence [1](local-evidence/hf-airtime-penalty-20261118/stable-m8.json)


- 10 mW, stable | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
10 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 270 s. Bit 0 at seed 940600 scored quiet at a physical C/N₀ of −6 dB-Hz in the fixed-duration power sweep.
Evidence [1](local-evidence/hf-airtime-penalty-20261118/stable-m6.json)


- 15.85 mW, stable | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Both bits decoded
Yes	Encryption
0 W	Helper Carrier Transmit Power
15.848931924611134 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 270 s. At physical C/N₀ −4 dB-Hz, bit 0 at seed 940600 and bit 1 at seed 940601 decoded. The nearest tested lower-power stable miss was 10 mW at −6 dB-Hz.
Evidence [1](local-evidence/hf-airtime-penalty-20261118/stable-m4.json) · [2](local-evidence/hf-airtime-penalty-20261118/fresh1-stable-m4.json) · [3](local-evidence/hf-airtime-penalty-20261118/summary.json)


- 100 mW, stable | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Bit decoded
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 270 s. Bit 0 at seed 940600 decoded. The local Ryzen scored the slot in 787.88 s. The severe GUI trial of this same bit and duration scored quiet.
Evidence [1](local-evidence/lowrate-2mw-native/original_stable_100mw_p4_admission.json)


- 100 mW, severe GUI | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Both bits erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. Bit 0 at seed 940600 and bit 1 at seed 940601 scored quiet. The latter realization decoded at Target −21 with a 12069.6 s pattern.
Evidence [1](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-p4-seed940600-bit0.json) · [2](local-evidence/hf-100mw-extended-20261118/admission-active-940601-1-target-4.json)


- 100 mW, severe GUI | 36 Hz × 10, hopping | Target −12.99 | 1,797.6 s/bit | Bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 1800 s. Bit 0 at seed 940600 scored quiet. The DATA score was −4.989 against 13.102 required.
Evidence [1](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m12p9897-seed940600-bit0.json)


- 100 mW, severe GUI | 36 Hz × 10, hopping | Target −18 | 5,906.4 s/bit | Ambiguous bit
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-18	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 5910 s. At seed 940600 the transmitted zero led by 2.645 against 5.293 required. DATA presence reached 38.048 against 11.918. An earlier explicit GPU attempt stopped when its fixed tile width exceeded the workspace.
Evidence [1](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m18-auto-seed940600-bit0.json) · [2](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m18-explicit64-plan.json)


- 100 mW, severe GUI | 36 Hz × 10, hopping | Target −20 | 9,501.6 s/bit | Both bits ambiguous
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-20	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 9510 s. Bit 0 at seed 940600 led by 4.273 and bit 1 at seed 940601 led by 4.061. Both fell short of 5.293 required and were erased. The latter bit decoded at −21 with 12069.6 s.
Evidence [1](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m20-explicit64-seed940600-bit0.json) · [2](local-evidence/hf-100mw-extended-20261118/admission-active-940601-1-target--20.json)


- 100 mW, severe GUI | 36 Hz × 10, hopping | Target −22.5 | 17,205.6 s/bit | Ambiguous bit
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-22.5	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 17220 s. Bit 0 at seed 940600 remained ambiguous. Presence reached 152.025 but the bit gap fell to 4.068 from 4.747 at −21. This 17205.6 s transmission used 1245.61 J. The 1 W, 256.8 s transmission on that seed used 188.21 J and decoded.
Other planner-only settings in the same sweep were −20.75 at 11299.2 s, −21.25 at 12840 s, −21.5 at 13610.4 s, −22 at 15408 s and −23 at 19516.8 s. These do not supply bit decisions.
Evidence [1](local-evidence/hf-100mw-extended-20261118/admission-active-940600-0-target--22.5.json) · [2](local-evidence/lowrate-2mw-native/alarm-pattern-energy.json) · [3](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m20p75-W26-cpu-plan.json) · [4](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m21p25-W26-cpu-plan.json) · [5](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m21p5-W26-cpu-plan.json) · [6](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m22-W26-cpu-plan.json) · [7](local-evidence/lowrate-2mw-runpod/100mw-36hz10x-target-m23-W26-cpu-plan.json)


- 100 mW, severe GUI | 3.6 Hz unspread | Target −10 | 962.4 s/bit | Ambiguous bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-10	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 990 s. Bit 0 at seed 940600 produced a bit gap of 2.925 against 5.293 required. Ryzen scoring took 85.03 s.
Evidence [1](local-evidence/lowrate-2mw-native/100mw_severe_6tiles_seed940600_bit0.json)


- 100 mW, severe GUI | 60 Hz unspread | Target −8 | 552 s/bit | Ambiguous bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
60 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-8	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 570 s. Bit 0 at seed 940600 produced an ambiguous erasure on the full GPU rerun after a local Ryzen attempt reached its 900 s deadline without a bit decision. GPU scoring took 208.04 s.
Evidence [1](local-evidence/lowrate-2mw-runpod/severe100-gpu-target-m8-admission-seed940600-bit0.json) · [2](local-evidence/lowrate-2mw-native/100mw_severe_60hz_1x_m8_seed940600_bit0.json)


- 100 mW, severe GUI | 60 Hz unspread | Target −12.99 | 1,752 s/bit | Ambiguous bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
100 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
60 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
-12.98970004336	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 1,770 s. Bit 0 at seed 940600 led by 2.246 against 5.293 required. The receiver erased it. GPU scoring took 673.45 s.
Evidence [1](local-evidence/lowrate-2mw-runpod/severe100-gpu-target-m12p98970004336-admission-seed940600-bit0.json)


- 251.19 mW, severe GUI | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Matched older erasure
Yes	Encryption
0 W	Helper Carrier Transmit Power
251.188643150958 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. The physical +8 dB-Hz preflight reproduced every noisy sample and public-search observation of the scored 4 W Target 32 test with observed C/N₀ overridden to +8. The physical-power run stopped at its deliberate memory limit. The matched reference erased bit 0 at seed 940600 as ambiguous.
Evidence [1](local-evidence/hf-airtime-penalty-20261118/severe-8-preflight.json) · [2](local-evidence/hf-airtime-penalty-20261118/anchor-comparison.json) · [3](local-evidence/sensitivity-row6-fixed-severe-8.json)


- 501.19 mW, severe GUI | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Ambiguous bit
Yes	Encryption
0 W	Helper Carrier Transmit Power
501.1872336272722 mW	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. At physical +11 dB-Hz, the signal was present but bit 0 at seed 940600 led by 2.322 against 5.293 required. This was the highest tested miss in the fixed-airtime severe sweep.
Evidence [1](local-evidence/hf-airtime-penalty-20261118/severe-11.json) · [2](local-evidence/hf-airtime-penalty-20261118/summary.json)


- 1 W, severe GUI | 36 Hz × 10, hopping | Target +4 | 256.8 s/bit | Both bits decoded
Yes	Encryption
0 W	Helper Carrier Transmit Power
1 W	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. The 1 W trial at physical +14 dB-Hz decoded bit 1 at seed 940601. For seed 940600, the physical-power preflight reproduced the input and receiver calibration of the scored 4 W Target 32 test with observed +14 dB-Hz. That reference decoded bit 0.
The passing power is 18 dB above the lower stable passing power of 15.85 mW at the same airtime. The two observed brackets imply 15–20 dB additional physical C/N₀ under a local single-transition assumption.
Evidence [1](local-evidence/hf-airtime-penalty-20261118/fresh1-severe-14.json) · [2](local-evidence/hf-airtime-penalty-20261118/anchor-comparison.json) · [3](local-evidence/hf-airtime-penalty-20261118/summary.json)


- 4 W, stable | 36 Hz × 10, hopping | Target +32 | 256.8 s/bit | Overridden +8 timed out
Yes	Encryption
0 W	Helper Carrier Transmit Power
4 W	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
32	Target SNR dB/1Hz

Stable propagation (--channel-model gui --channel-severity 0). The receiver kept its HF Worst search. Oscillator errors and white noise remained active.
The public slot cadence was 270 s. Historical stable capture with the same 4 W editor power and a +8 dB-Hz observed C/N₀ override. The slot at seed 940600 reached the 600 s deadline before a bit decision.
Evidence [1](local-evidence/sensitivity-row6-fixed-stable-8.json)


- 4 W, severe GUI | 36 Hz × 10, hopping | Target +32 | 256.8 s/bit | Nominal bit decoded
Yes	Encryption
0 W	Helper Carrier Transmit Power
4 W	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
32	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. Reference row from 17 November. Nominal +20.0206 dB-Hz at 180 dB loss and −164 dBm/Hz. The repaired receiver decoded bit 0 at seed 940600 under Current in 449.36 s on the local CPU. The stronger admission policies erased the bit. An earlier midpoint error produced no decision.
Evidence [1](local-evidence/sensitivity-row6-fixed-nominal.json) · [2](local-evidence/sensitivity-row6-admission-severe-nominal.json)


- 4 W, severe GUI | 36 Hz × 10, hopping | Target +32 | 256.8 s/bit | Overridden +14 bit decoded
Yes	Encryption
0 W	Helper Carrier Transmit Power
4 W	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
32	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. Historical observed C/N₀ override +14 dB-Hz. Its link equivalent is 1 W, while the editor power remains 4 W. Seed 940600 bit 0 decoded on the 900 s attempt after 624.34 s of receiver work. The original 600 s attempt timed out.
Evidence [1](local-evidence/sensitivity-row6-fixed-severe-14-900s.json) · [2](local-evidence/sensitivity-row6-fixed-severe-14.json) · [3](local-evidence/hf-airtime-penalty-20261118/anchor-comparison.json)


- 4 W, severe GUI | 36 Hz × 10, hopping | Target +32 | 256.8 s/bit | Overridden +8 ambiguous
Yes	Encryption
0 W	Helper Carrier Transmit Power
4 W	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
32	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. Historical observed C/N₀ override +8 dB-Hz. Its link equivalent is 251.19 mW, while the editor power remains 4 W. Bit 0 at seed 940600 erased as ambiguous after 587.64 s of receiver scoring.
Evidence [1](local-evidence/sensitivity-row6-fixed-severe-8.json) · [2](local-evidence/hf-airtime-penalty-20261118/anchor-comparison.json)


- 4 W, severe GUI | 36 Hz × 10, hopping | Target +32 | 256.8 s/bit | −12 dB signal gain, trial incomplete
Yes	Encryption
0 W	Helper Carrier Transmit Power
4 W	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Interleave	DSSS
10x	Spreading
Fake: FCC	FHSS
Off	Symbol_burst
36 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
32	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
The public slot cadence was 270 s. Historical fixed receiver/noise reference at +32 dB-Hz with −12 dB signal gain gave an effective +20 dB-Hz. This differs from the nominal +20.0206 dB-Hz link. The single-slot and message attempts timed out or hit the earlier midpoint-bound error before a decision.
Evidence [1](local-evidence/sensitivity-row6-admission-severe-20-long.json) · [2](local-evidence/sensitivity-row6-admission-severe-20.json) · [3](local-evidence/sensitivity-row6-20-adjusted.json)


- 4 W, severe GUI | 3.6 Hz unspread | Target +4 | 160.4 s/bit | Ambiguous bit erased
Yes	Encryption
0 W	Helper Carrier Transmit Power
4 W	Transmit Power
180 dB	Path loss
Default several seconds receiver search space	Clock sync / Region / Offset / Audio error
GPSDO hobbyist XO (no oven)	Base Oscillator
GPSDO hobbyist XO (no oven)	Shift Oscillator
HF worst 30Hz 7ms	Doppler/delay spread
Each - 65%	Additional frequency selective amplitude modulation
3x	Margin
Off	DSSS
0x	Spreading
Off	FHSS
Off	Symbol_burst
3.6 Hz	Rate
1.725 kHz	Baseband Carrier
30 MHz	Shift
4	Target SNR dB/1Hz

Severe GUI propagation at the default full severity. HF Worst at 3× allowed 90 Hz common and relative Doppler motion and 21 ms excess delay, with Each 65% modulation.
Slot cadence 210 s. The short severe-channel control at 4 W and +20.0206 dB-Hz physical C/N₀ produced an ambiguous decision on bit 0 at seed 940600.
Evidence [1](local-evidence/lowrate-2mw-native/lowrate_strong_target4.json)

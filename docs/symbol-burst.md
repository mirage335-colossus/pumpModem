# Symbol_Burst whole-symbol scheduling

Symbol_Burst is an optional Robust Modem calendar that leaves time to process
received symbols between transmissions. It uses the existing symbol waveform,
iterative receiver searches, transport and physical-completion rule. It adds no
wire bits or decoder. Both peers configure the same wait settings locally.
See the [validation record](validation.md) for qualification and remaining limits.

## Controls

The two selectors immediately right of FHSS choose a minimum wait of
**Off, 1, 2, 3, 5 or 10 symbols**, and a random extra-wait limit of
**0, 1, 2 or 3 symbols**. Off is the default. Equivalent launch options are:

```sh
--symbol-burst 3 --symbol-burst-random 2
```

The extra wait is a cryptographically selected whole number from zero through
the limit, inclusive. Without a communication key its effective value is zero;
the saved limit remains available when encryption is enabled. The wait stream
has its own versioned cryptographic domain and uses the actual symbol UTC
address. Existing stream domains and keyfiles retain their meanings.

Symbol_Burst does not use dwell times or introduce ramps. FHSS remains a separate
layer with its existing power ramps and hop timing. This implementation rejects
simultaneously enabled Sched_Hop and Symbol_Burst; they describe different source
calendars. The low-level diagnostic deferred-FHSS calendar is also incompatible.
Production FHSS retains its independent waveform and usable-power checks.

## Transmit calendar

Let D be a complete symbol duration, P its existing pulse padding on each side,
N the selected minimum wait, and J the agreed random extra wait. A symbol's
complete support begins at B, its payload at C=B+P, and its support ends at
E=C+D+P. The next complete support begins at

```text
ceil_to_UTC_30_seconds(max(E + (N + J) * D, B + 60 seconds))
```

The first support also begins on that UTC grid. Starts are therefore at least
60 seconds apart. Complete symbols and their existing pulse tails are preserved;
the interval between supports is literal silence. The existing settling prefix
and final suppression cover occur once per transmission. They are not repeated
in the inter-symbol gaps. Data masking, private templates, outer DSSS and
interval authentication use actual payload UTC coordinates, including P's
sample phase. There is no transmitted schedule header or extra completion bit.

## Listening, backlog and completion

Start the receiver before the first symbol, allowing the leading observation
guard and the configured clock, audio, region and offset uncertainty. Cold
receivers listen normally until they acquire a symbol. Public native FFT
acquisition keeps its existing rolling search; private and finite-window paths
retain their configured clock, frequency, key and profile alternatives.
Acquisition work already promised at the first accepted symbol is completed.
The private FFT's original initial-symbol alternatives are retained.

After acquisition, the receiver captures each complete symbol and its pulse
support, with at least 20 seconds on both sides and the full configured timing,
audio and original rate uncertainty. It excludes PCM only when every retained
consumer proves that the interval is outside its observation window. Missing or
contradictory timestamp evidence keeps PCM. During a proven gap, sample
coordinates advance and queued decoding can continue; skipped input is not a
failed symbol observation.

The existing end rule remains: fully score the expected subsequent symbols and
observe consecutive failed symbol durations covering at least six seconds.
An hours-long symbol must finish before it can be absent. Planned silence, EOF,
cancellation and successful source decoding do not end a message. Accepted bits
remain available on the next progress poll through the existing pending row and
profile-revision arbitration.

For D below six seconds, completion needs `ceil(6/D)` fully scored empty
scheduled slots. The intervening waits do not count toward those six seconds.
This can make completion slow for short symbols; Symbol_Burst is intended for
long, difficult symbols rather than faster signalling.

UTC/GPS remains authoritative. This feature does not discipline a host clock,
steer an oscillator or infer a new rate from successive GPS corrections.
Continuation fits remain inside the original configured clock/rate envelope;
contradictory evidence disables capture exclusion. Unsynchronised residual rate
uncertainty keeps ageing from the original capture and can eventually consume
the quiet interval; Symbol_Burst does not learn a replacement clock rate.
GPS/GPSDO and suitable Audio, region and offset settings remain appropriate for
very long symbols.

## Planner interpretation

The CPU curve includes initial acquisition and the retained acquisition tail,
then guarded full-symbol work per scheduled interval. It distinguishes the
conditional successful-acquisition case from ongoing seeking. Extra random
waits are not assumed to rescue the shortest permitted J=0 interval. Minimum,
mean and maximum spacing are reported separately. Headline and curve calculations
use the same configured receive keys and profiles. Memory sharing counts each
profile's own residency instead of repeating the longest profile for every key
and target. Finite acquisition can still exceed a short wait's CPU allowance;
the planner retains that cost and can recommend a longer wait.

Silence does not reduce peak DSP workspace, oscillator coverage or the work
needed to acquire the first symbol. If guards or outstanding acquisition windows
overlap, the receiver keeps listening; increase the wait to create a useful gap.
A long wait cannot make an individually unaffordable full-symbol search fit RAM.
The Earth-Moon-Earth example selects 75% of available DSP workspace; the other
standard examples retain 50%. These fractions are planning inputs, not claims
about available memory while another process consumes it.

The existing representative compact estimates do not qualify automatic rolling
private acquisition; see [the estimator's scope](simulation-estimates.md#sustained-listening-versus-a-finite-draft).
For an encrypted symbol lasting days, many possible first UTC starts can remain
unresolved until a whole symbol finishes. Different payload epochs select
different private templates. GPS accuracy narrows arrival error but does not
identify which half-minute boundary began the message. Symbol_Burst reduces
the former one-second admission cadence to 30 seconds, but does not establish
that such cold acquisition fits CPU or memory. Its post-acquisition silence
must not be read as a measurement or qualification of that separate workload.

The original [implementation handoff](symbol-burst-implementation-plan.md)
records the design decisions and test obligations. It is historical planning
context; current source and the validation record determine implemented behavior.

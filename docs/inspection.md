# Modem and transmission inspection

The native desktop keeps shared key, metadata and modem controls visible above
and below three tabs. **Console** contains composition, the signal browser,
received files and live plots. Switching tabs leaves the receiver and current
three-second simulation replay running.

**Modem flow** shows the currently selected transmit and receive paths as ordered
stage cards. Active processing, disabled options and unavailable algorithms have
different labels. It includes the ideal phase/amplitude alphabets used for
training and data, the chosen pattern or tone mode, symbol integration, packet
coding, encryption, and the appropriate audio or simulation path. These are
configured ideal points; measured received symbols remain on the Console.

The view describes the implemented gain estimate accurately: framed acquisition
searches timing and gain candidates using the protected bootstrap, then holds
that gain for body decoding. There is no continuous audio AGC or convolutional
encoder/Viterbi decoder. Raw binary simulation has an aligned known-length
receiver with nominal unit gain; continuous real audio reception still discovers
framed packets. Simulation uses integrated channel observations, whereas real
audio passes through the hardware/internal-clock conversion.

**Transmission layout** shows the physical on-air sequence separately from the
logical packet fields before interleaving. Blocks are schematic, with byte,
symbol and duration counts giving their scale. The payload is a placeholder;
the diagrams never display input contents, metadata values, key bytes, encrypted
training, or private spreading sequences.

For packets, the fixed five-second training consists of 64 four-bit APSK
segments. The following 40-byte header always carries 32 Reed–Solomon parity
bytes, including when body FEC is Off. The body contains metadata, the selected
compressed or original payload, and a 32-byte digest or keyed MAC. The view
shows body Reed–Solomon block geometry and column-wise interleaving, including
shortened final blocks. Header and body symbol padding are independent. No
diagram represents interleaved parity as a single physical footer.

Selecting Binary instead shows the exact bit count, full symbols and any final
partial alphabet. This path has no preamble, packet header, compression,
Reed–Solomon parity or integrity tag. Selected stream encryption adds no bits.

Inspection shares the asynchronous airtime calculation and current edit
revision. Its packet layout comes from the real encoder, including whether
short-message compression actually saves bytes. Editing a message or settings
clears the previous diagram until the new result is ready; invalid inputs show
their error. Inspection does not synthesize a waveform, so a symbol lasting
hours or a high internal sample rate does not allocate an hours-long sample
array. Packet identifiers generated for a future transmission are represented
by their field size rather than by a speculative random value.

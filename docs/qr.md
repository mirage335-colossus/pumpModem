# Optical QR transfers

`encode_qr` produces QR Code Model 2 with Level L error correction. It emits
UTF-8 ECI assignment 26 followed by one byte-mode segment, preserves embedded
zero bytes, chooses the smallest fitting version, and evaluates all eight
standard masks. Error correction is kept at Level L rather than automatically
raised for short messages.

Input is limited to 500 Unicode scalar values, or at most 2000 UTF-8 bytes.
Combining characters count separately. Invalid UTF-8, surrogate code points,
overlong encodings, and values beyond U+10FFFF are rejected. Versions 1 through
40 are available, so 500 four-byte characters fit as well as 500 ASCII
characters. Longer text should use the modem transfer path.

`QrCode::dark(x,y)` exposes the matrix for native GUI rendering. Out-of-range
coordinates are white. SVG and plain PBM (`P1`) exporters include a four-module
white quiet zone on every edge. Export scale is 1 through 32 pixels per module,
default 4. SVG consists exclusively of locally generated rectangles/path
geometry and never interpolates the input text into markup. PBM needs no image
library to produce or display in a compatible GUI toolkit.

The optical representation is plaintext: it does not automatically add modem
encryption, authentication, or content trust. It transports the exact supplied
UTF-8 bytes through a standard QR reader. Users decide how to handle the
decoded text.

## Vendored implementation

The encoder is Project Nayuki's MIT-licensed C++ QR generator, release **v1.8.0**.
Its source and header are kept unchanged in `third_party/qrcodegen`, with the
license alongside them. It uses only the C++ standard library and adds no
runtime dependency. The wrapper implements input validation and output
rendering; it does not implement another QR Reed-Solomon variant.

Upstream files and recorded SHA256 checksums:

| Source | SHA256 |
| --- | --- |
| [qrcodegen.hpp v1.8.0](https://github.com/nayuki/QR-Code-generator/blob/v1.8.0/cpp/qrcodegen.hpp) | `b779c3b156cf7a57ce789d6fee4fc991ccc2913774d26c909d22bb8f26b2a793` |
| [qrcodegen.cpp v1.8.0](https://github.com/nayuki/QR-Code-generator/blob/v1.8.0/cpp/qrcodegen.cpp) | `1f3b3fcdac6954c32cf583ccd02ec9b5901f756a38c461acedc70be4a77d3757` |

Tests exercise UTF-8 and capacity boundaries, embedded zeros, finder/quiet-zone
geometry, SVG safety, PBM pixel parity, and a frozen module matrix computed by
the release's separate Python implementation. Independent ZXing-C++ decoding
of exported PBMs is also used during implementation validation, without adding
that decoder to the project's build dependencies.

[Upstream library documentation](https://www.nayuki.io/page/qr-code-generator-library)
describes the implementation's standard encoding and licensing.

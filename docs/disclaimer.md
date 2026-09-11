# Public release and scope

Data Pump is an independently developed civilian audio modem for text and file
transfer. Version0.3 implements the features recorded in `requirements.md`.
It excludes routed addressing, built-in repeaters, asymmetric key exchange,
and rapid uncontrolled Doppler tracking. These statements describe its actual
functionality; they are not a legal classification or a claim that a particular
class of user could never use the software.

The implementation uses publicly documented standard symmetric cryptographic
primitives through OpenSSL: AES256-CTR, AES256-GCM, HKDF-SHA256, and HMAC-SHA256.
The packet and waveform specifications are documented alongside the source.
No assertion is made that these choices alone establish an export-control
classification, public-information exclusion, RF authorization, or exemption.

US public-release rules for encryption depend on the actual software and the
applicable regulations. Current regulatory text distinguishes publicly available
encryption source and corresponding object code and addresses notification for
non-standard cryptography. See the primary texts in
[15CFR734.17](https://www.ecfr.gov/current/title-15/subtitle-B/chapter-VII/subchapter-C/part-734/section-734.17),
[15CFR742.15](https://www.ecfr.gov/current/title-15/subtitle-B/chapter-VII/subchapter-C/part-742/section-742.15),
and the [BIS encryption guidance](https://www.bis.gov/learn-support/encryption-controls/encryption-items-not-subject-to-ear).
No applicable USML/ECCN determination has been obtained for this implementation.
The original supplied disclaimer is retained only within the preserved design
document; its broader legal and performance assertions are not adopted as
verified facts by this release.

The software does not certify permitted frequencies, power, modulation masks,
spreading, or licenses for any jurisdiction. It has no FHSS/ISM compliance mode.
Operators choose hardware and authorized operating conditions. See the
[CC0 1.0 Universal license](../LICENSE) for the software warranty terms.

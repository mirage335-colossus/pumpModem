# Public release and scope

Data Pump is an independently developed civilian audio modem for text and file
transfer. The implemented features are recorded in [requirements.md](requirements.md)
and the version 001_00 [scope record](original-specification.md). These statements
describe its functionality; they are not a legal classification or a claim that
a particular class of user could never use the software.

The maintained services use standard cryptographic primitives through OpenSSL;
their current constructions and limits are documented in [crypto.md](crypto.md)
and [security.md](security.md). Robust transmission and reception are
unavailable in the GUI pending backend connection; this notice does not define
a Robust wire format.
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
Legal and performance assertions in the original proposal are not adopted as
verified facts by this release.

The software does not certify permitted frequencies, power, modulation masks,
spreading, or licenses for any jurisdiction. Operators choose hardware and
authorized operating conditions. See the
[CC0 1.0 Universal license](../LICENSE) for the software warranty terms.

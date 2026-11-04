# Focused broad-HF FFT checks

Both focused component checks passed with eligible exact historical FP32
precision, existing cached fixtures, and unchanged historical anchors. Each
measured one actual full score on each implementation.

| Backend | Component wall | Measured helper | Expanded ratio interval |
| --- | ---: | ---: | --- |
| CPU | 206.889412 s | 192.965079 s | [0.882159, 1.148666] |
| GPU | 33.771707 s | 26.474293 s | [0.898242, 1.106221] |

The endpoint calibration guard did not trigger. Source, fixture, and anchor
identity checks passed and worker descendants were stopped. These are conditional
component decisions using the retained historical model envelope; they do not
qualify future prediction coverage or a complete current suite.

Component walls include setup, preflight, and worker lifecycle costs around the
reported helper measurements. Hardware stress was not repeated: prior health
reports are retained and descriptors compared. Neither result establishes a new
hardware-health pass. The original incomplete CPU/GPU reports remain unchanged;
the CPU run's 1684.791382 s and this component's 206.889412 s are not one measured
complete-run wall time.

The archive retains both wrappers, expected-source sidecars, all 14 measured
source files, additional declared native/worker identities, raw JSONL responses,
precision/preflight/measurement receipts, endpoint guards, and original anchors.
`source-index.json.gz` binds 25 exact file versions at 168 recorded identities.
The wrappers and sidecars were byte-identical on CPU/GPU. Prior health reports are
linked by their original digests in `external-inputs.json`. Physical arrays and
executables are omitted. The original focused-check archive did not include a
separate array audit; the subsequent cache assembly below records one.

No receiver work was rerun while archiving. Run `python3 verify.py` for exact-byte,
source, sidecar, anchor, and outcome checks.

The separate [cache assembly receipt and procedure](cache-assembly/README.md)
records a reusable GPU cache assembled after all measurements. Its 20 public
files (109,435,833 bytes) were hashed against the selected fixture receipts and
the applicable broad/free anchors. It retains the original broad-HF metadata and
uses the newer strict-identical free-crystal metadata, with unchanged original
binary arrays. This is file-identity evidence, not another GPU run or a current
complete-suite result.

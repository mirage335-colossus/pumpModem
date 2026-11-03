# FFT timing campaign preservation bundle

## Verification status

**Verified locally.** The full archive verifier passed in 44.056 seconds. It checked 2,414 tar members (2,413 payload files), 13,592,331,852 logical payload bytes, and 747 hardlink aliases, while extracting zero payload bytes. The verified archive SHA-256 is `48ace219ff5f3306453a1de647dabece9dce70b7ae779cabfc9aa4bfc2011238`; the verified inventory SHA-256 is `7929c63f5f402752468f6e49090eba8b0cae574e5b2e71119939f077ea24472c`; and the verified archive-metadata SHA-256 is `cb89f95422faec0ccfe05d354828da337e3624a4a7343bdd14f82a164db636db`. The exact verifier receipt is preserved in [verification.json](verification.json).

The packer reports an archive size of **9,138,052,300 bytes** and SHA-256 `48ace219ff5f3306453a1de647dabece9dce70b7ae779cabfc9aa4bfc2011238`. Its inventory SHA-256 is `7929c63f5f402752468f6e49090eba8b0cae574e5b2e71119939f077ea24472c`.

After this verification, the disposable study Pod and its temporary container
storage were removed on 2026-11-03 UTC. No network volume was created; the
loaded provider billing page then showed zero current spend. All download,
assembly and verification processes completed or were explicitly reaped.

## Contents and scope

The archive contains 2,413 logical files, totaling 13,592,331,852 bytes before deduplication. It represents 232 completed physical, long-duration, and follow-up captures, plus the six-job GPU comparison, sampled inputs, environment details, build artifacts, source packages, campaign plans, runners, and bootstrap logs. Identical payloads are represented by 747 tar hardlinks. The inventory records each member's path, size, and SHA-256, binds the three campaign receipts and capture input files, and records GPU score/status checksums.

The GPU status hashes in the inventory were calculated while preparing the preservation bundle and compared with the preserved summary/status fields. They are preservation-time checks, not independent contemporaneous attestations. The inventory's source paths are provenance labels from the run environment; they do not imply that those remote paths remain available.

Separate integrity reviews are recorded in [source-review.json](source-review.json) and [binding-review.json](binding-review.json). The source review checked all 1,769 nested source-package members against the exact source manifest and the 16 source-file hashes in each of three campaign plans. The binding review independently matched all 928 capture-input identities to the campaign receipts. These reviews supplement the full archive verification.

The compressed archive is stored at:

`local-evidence/triad-fft-timing/preservation/triad-fft-evidence.tar.zst`

The adjacent `triad-fft-evidence.archive.json` and `triad-fft-evidence.inventory.json` files are the compact packer receipts. The archived packer and verifier are included here as `pack.py` and `verify.py`. The recorded packer SHA-256 is `44e8dc34fff4d952c3a37053345bde403379938691ff56a83928bd7a46d75cba`; the verifier SHA-256 is `922187b74e9f782988b8f0638a148710137725d3a4764ade1b5762a43834aaf1`.

## Verify the complete archive

After the download has completed, verify the archive before using any of its contents:

```sh
python3 docs/validation-data/triad-v3/2026-11-03-fft-timing/preservation/verify.py \
  --archive local-evidence/triad-fft-timing/preservation/triad-fft-evidence.tar.zst \
  --inventory docs/validation-data/triad-v3/2026-11-03-fft-timing/preservation/triad-fft-evidence.inventory.json \
  --metadata docs/validation-data/triad-v3/2026-11-03-fft-timing/preservation/triad-fft-evidence.archive.json \
  --timeout 300
```

The verifier checks the archive's SHA-256 and size, then streams the zstd-compressed tar and checks every member's path, type, size, and content hash. It verifies hardlinks only when their earlier target has already been verified. It does not extract the large capture arrays. The passing receipt is retained as [verification.json](verification.json).

## Inspect or extract selected items

Prefer listing or writing one selected member to standard output after full verification:

```sh
zstd -dc local-evidence/triad-fft-timing/preservation/triad-fft-evidence.tar.zst | tar -tf - | head
zstd -dc local-evidence/triad-fft-timing/preservation/triad-fft-evidence.tar.zst \
  | tar -xOf - 'results/physical/results.json' > /tmp/triad-physical-results.json
```

Archive paths begin with `results/`, `ops/`, `wide-input/`, or `metadata/`. For multiple files, extract only the required paths into a fresh, empty directory; do not unpack the complete multi-gigabyte archive for routine inspection. Hardlinked entries point to an earlier archived member, so include that target when selectively extracting a hardlink alias.

## Reproduce the preservation package

With the original campaign outputs and the allowlisted operations files present, reproduce the packer's receipt and archive using:

```sh
python3 docs/validation-data/triad-v3/2026-11-03-fft-timing/preservation/pack.py \
  --results-root /workspace/triad/results \
  --ops-root /tmp/triad-fft-ops \
  --bootstrap-logs-root /tmp/triad-fft-logs \
  --wide-input-root /workspace/triad-input/local-evidence/triad-fft-timing/wide-pilot/sampled \
  --outbase /workspace/triad/triad-fft-evidence
```

The packer refuses incomplete campaign receipts, changed source/binary identities, duplicate capture IDs, missing or mismatched sampled-input hashes, incomplete GPU jobs, and mismatched GPU score/status hashes. It includes only its explicit operations-file allowlist and bootstrap logs. It does not perform cloud operations. Reproduction requires the exact source archive, plans, scripts, binary, outputs, and runtime inputs listed in the inventory; remote source paths alone are not sufficient.

# QR Code generator source in DataPump

- Upstream: [Project Nayuki QR Code generator library](https://www.nayuki.io/page/qr-code-generator-library).
- Repository: [nayuki/QR-Code-generator](https://github.com/nayuki/QR-Code-generator).
- Vendored release: **v1.8.0**; [release notes](https://github.com/nayuki/QR-Code-generator/releases/tag/v1.8.0),
  [all releases](https://github.com/nayuki/QR-Code-generator/releases).
- Upstream commit: [`720f62bddb7226106071d4728c292cb1df519ceb`](https://github.com/nayuki/QR-Code-generator/commit/720f62bddb7226106071d4728c292cb1df519ceb)
  (peeled `v1.8.0` tag); [pinned C++ sources](https://github.com/nayuki/QR-Code-generator/tree/720f62bddb7226106071d4728c292cb1df519ceb/cpp).
- License: MIT; see [LICENSE](LICENSE), installed as `QR-LICENSE`.

Only `qrcodegen.cpp` and `qrcodegen.hpp` are vendored; both were verified
byte-for-byte against this commit. Their SHA-256 checksums are recorded in
[the QR documentation](../../docs/qr.md#vendored-implementation).
[CMakeLists.txt](../../CMakeLists.txt) compiles the source directly into
`datapump`; it needs only the C++ standard library.

When upgrading, replace both files together, retain the license, and update
this note and the version/commit/checksums in `docs/qr.md`. Review API and encoding
changes against [src/qr.cpp](../../src/qr.cpp): UTF-8 ECI 26 plus a byte segment,
Level L error correction without automatic boosting, and automatic version/mask
selection. Rebuild `test_qr` and run
`ctest --test-dir <build-dir> -R '^qr$' --output-on-failure`.
[tests/test_qr.cpp](../../tests/test_qr.cpp) covers input
limits, embedded zeros, geometry, SVG/PBM export, and a frozen v1.8.0 reference
matrix; investigate matrix changes and verify exported symbols with an
independent QR decoder before refreshing that reference.

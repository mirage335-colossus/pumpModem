# Vendored dependencies

These sources are checked into the repository; CMake does not download them.
The linked notes record upstream locations, pinned versions, licenses, and
upgrade considerations.

| Dependency | Vendored version | Source and maintenance notes |
| --- | --- | --- |
| FLTK | 1.4.5 (`release-1.4.5`) | [FLTK-SOURCE.md](FLTK-SOURCE.md), including FLTK's bundled libraries |
| Nayuki QR Code generator (C++) | 1.8.0 (`v1.8.0`) | [qrcodegen/README.datapump.md](qrcodegen/README.datapump.md) |
| XZ / liblzma | 5.8.4 (`v5.8.4`) | [xz/README.datapump.md](xz/README.datapump.md) |

When upgrading, update the relevant note and recorded checksums, preserve
license notices, and run the integration checks described there.

OpenSSL and platform libraries are supplied by the build environment rather
than vendored here. See [build requirements](../README.md#build-and-run) and
[portable packaging](../docs/offline-installation.md); their exact versions
depend on the build environment.

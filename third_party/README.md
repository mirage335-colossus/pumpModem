# Vendored dependencies

These sources are checked into the repository; CMake does not download them.
The linked notes record upstream locations, pinned versions/commits, licenses, and
upgrade considerations.

| Dependency | Vendored version | Full upstream commit | Source and maintenance notes |
| --- | --- | --- | --- |
| FLTK | 1.4.5 (`release-1.4.5`) | [a9b1113516ffd15fc7602a6d425a317df30f4720](https://github.com/fltk/fltk/commit/a9b1113516ffd15fc7602a6d425a317df30f4720) | [FLTK-SOURCE.md](FLTK-SOURCE.md), including FLTK's bundled libraries |
| Nayuki QR Code generator (C++) | 1.8.0 (`v1.8.0`) | [720f62bddb7226106071d4728c292cb1df519ceb](https://github.com/nayuki/QR-Code-generator/commit/720f62bddb7226106071d4728c292cb1df519ceb) | [qrcodegen/README.datapump.md](qrcodegen/README.datapump.md) |
| XZ / liblzma | 5.8.4 (`v5.8.4`) | [d3e650e63c110e830fd5391e7f8b45df0b91d3da](https://github.com/tukaani-project/xz/commit/d3e650e63c110e830fd5391e7f8b45df0b91d3da) | [xz/README.datapump.md](xz/README.datapump.md) |
| Rev (optional GUI) | `clean` snapshot | [d73faa7759b5cfd30d592057790ab458568b569b](https://github.com/ryanpmcguire/Rev/commit/d73faa7759b5cfd30d592057790ab458568b569b) | [rev/README.datapump.md](rev/README.datapump.md), including local fixes and unresolved upstream distribution terms |

Hashes identify commits, not annotated tag objects. When upgrading, resolve
the tag with `git rev-parse '<tag>^{commit}'` in the upstream checkout, compare
the copied files against that commit, and record paths, omissions, and patches.
Update the relevant note and checksums, preserve license notices, and run the
integration checks described there.

OpenSSL and platform libraries are supplied by the build environment rather
than vendored here. See [build requirements](../README.md#build-and-run) and
[portable packaging](../docs/offline-installation.md); their exact versions
depend on the build environment.

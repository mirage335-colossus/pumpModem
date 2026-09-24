# XZ / liblzma source in DataPump

This software includes code from XZ Utils <https://tukaani.org/xz/>.

- Upstream release: **XZ Utils 5.8.4**, released September 9, 2026.
- Repository: <https://github.com/tukaani-project/xz.git>
- Git commit: [`d3e650e63c110e830fd5391e7f8b45df0b91d3da`](https://github.com/tukaani-project/xz/commit/d3e650e63c110e830fd5391e7f8b45df0b91d3da)
  (peeled `v5.8.4^{commit}`, not the annotated tag object).
- Immutable source tree: <https://github.com/tukaani-project/xz/tree/d3e650e63c110e830fd5391e7f8b45df0b91d3da>
- Source archive: <https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz>
- Archive SHA-256: `0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9`
- Release: <https://github.com/tukaani-project/xz/releases/tag/v5.8.4>

The copied upstream files are unchanged. This subset contains `src/liblzma`,
`src/common`, `cmake`, `lib`, the upstream `CMakeLists.txt`, and its licensing
and basic source documentation. Autotools `Makefile.am`/`Makefile.in` files
are omitted. `UPSTREAM.sha256` records the copied files, independently of
DataPump's own wrapper and this provenance note.

The application compiles a generated receive-hardening overlay rather than
modifying these checksum-pinned files. `cmake/XzReceiveHardening.cmake` verifies
every upstream hash before copying the inventory into the build directory. It
changes exactly three implementation files there: `src/liblzma/lz/lz_decoder.h`
adds dependent bounds clipping to received dictionary distances and resulting
copy offsets; `src/liblzma/lz/lz_decoder.c` initializes the small reset/fallback
prefix including SIMD overread space; and `src/liblzma/lzma/lzma2_decoder.c`
places a validation barrier before accepted received properties configure the
literal decoder. The rest of the copied implementation is unchanged. Missing
patch context is a configuration error. The overlay uses DataPump's documented
`include/datapump/speculation.h`; no code is downloaded or generated from received
data. See [scope and limitations](../../docs/receive-processing-hardening.md).

Verified September 12, 2026: all 213 files in `UPSTREAM.sha256` match both
the Git commit above and the checksum-pinned release archive byte for byte,
at identical relative paths. This subset has no release-generated files
absent from Git. Recheck the local inventory with
`cmake -P tests/vendored_lzma.cmake` from the DataPump repository root.

`cmake/VendoredLzma.cmake` builds the library statically with the project's C
compiler, including native MSVC and its static runtime. Only LZMA1/LZMA2
filters, the BT4 match finder, and the library's mandatory CRC32 support are
enabled. XZ executables, scripts, translations, shared libraries, and threaded
container codecs are disabled. CMake performs no source download and does not
search for a system liblzma. No liblzma DLL or system package is needed when a
working DataPump bundle is copied to a compatible computer.

The unused `lib/getopt*` sources are retained because the unmodified upstream
CMake file configures that separate object target on hosts such as Windows
even when all its command-line tools are disabled. That target is excluded
from the application build and **is not linked into DataPump**. These source
files carry LGPL-2.1-or-later notices and `COPYING.LGPLv2.1` is retained for
source redistribution. The linked liblzma code is under the BSD Zero Clause
License; see `COPYING.0BSD` and upstream's summary `COPYING`.

The regular modem uses raw LZMA2 with a locally fixed 4 MiB dictionary, no
original-length field, and preset 9 extreme encoder search settings. Encoder
and decoder scratch caps are 64 MiB and 8 MiB, respectively, enforced by a
bounded allocator. Fast uses an XZ container restricted to the compiled
LZMA1/LZMA2 filters and CRC32, with a 64 MiB decoder-memory limit. Both paths
decompress only after physical completion and enforce separate local output
quotas. The earlier adaptive-dictionary/packet-size description is historical;
the current [protocol](../../docs/protocol.md), [Fast format](../../docs/fast-mode.md)
and source wrappers define the supported profiles. Preset 9 extreme does not
guarantee mathematically minimal output.

Updating the dependency requires reviewing the release, resolving its tag to
the full commit hash, replacing this source subset from its pinned archive,
comparing the subset against that commit, updating the commit/archive links
and checksums, and running the codec, malformed-stream, packet, and
relocated-package tests.
The generated overlay must also be reviewed against the new dictionary
invariants and validation sites; its context checks must not be bypassed merely
to make a new upstream version build.

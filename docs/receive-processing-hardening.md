# Receive processing and targeted speculation hardening

Data Pump limits the processing of received bytes and adds selected defenses
against speculative bounds-check bypass. The aim is practical risk reduction
while retaining modem performance. It does not add process or VM isolation,
change the wire format, or replace the existing receiver with a raw-only mode.
The fixed geometry, physical-completion gate, source quotas and immediate pending
bit progress remain requirements of the [development contract](development.md).

Ordinary C++, an ASCII allowlist, successful authentication and an explicit Save
action are not proofs that received data is harmless. The measures below cover
specific accesses and transitions. They do not establish protection against all
Spectre variants, Meltdown, other CPU weaknesses, memory-safety defects, or a
compromised host.

## Processing and presentation boundary

The application-controlled receive paths contain no JavaScript engine, embedded
browser, payload interpreter, or JIT that compiles received content. They do not
execute received commands or automatically open received files. FLTK uses native
widgets; Rev uses compiled C++ modules with OpenGL and FreeType. Rev's Python
resource generator runs at build configuration time, not on received data.
This audit does not characterize every internal operation of system graphics,
font, audio or filesystem libraries.

Physical reception still requires DSP, error correction and, when configured,
cryptography. Existing source decoding and bounded attachment interpretation
run after physical completion. Those codecs necessarily inspect raw data; the
presentation layer does not gain permission to interpret arbitrary bytes.

[`received_text.hpp`](../include/datapump/received_text.hpp) maps each source byte
independently to the restricted ASCII set or an underscore, without Unicode,
locale or escape decoding. GUI Shellcode mode permits printable ASCII only when
Developer mode also permits it. Native widgets, clipboard requests, received
filenames, CLI text and received-derived transmit drafts get the permitted view.
Original bytes remain separate for explicit saving. See the complete
[received-text policy](security.md#received-text-boundary), including revocation
and the locally entered QR input exception.

The filter ends with `datapump_speculation_barrier()` before its output is handed
to a more complex consumer. The barrier is once per filtered view, not once per
byte. Filtering remains necessary: a barrier alone does not validate content.

## Selected accesses and validation boundaries

The review distinguished received values that can affect an address or decoder
geometry from ordinary arithmetic on fixed, locally bounded arrays.

| Area | Targeted treatment |
| --- | --- |
| Regular short dictionary, `src/compression_short.cpp` | Preserve input and endpoint checks, bound bit selectors independently, provide full-domain small decode tables, and carry input-index bounds into each dependent read. The dictionary codes and exact endpoints are unchanged. |
| Regular Reed–Solomon, `src/stream_codec.cpp` | Clip selected locator, syndrome, erasure, correction-position and pivot indices. Keep validation barriers at checked dimensions, repair acceptance, interval shape and keyed verification boundaries. |
| Hard-bit recovery, `src/recovery.cpp` | Validate retained input and pack offsets, then clip retained-bit indices, preserving missing-slot status and existing recovery semantics. |
| Fast capacity Reed–Solomon, `include/datapump/fast/outer_rs.hpp` | Bound selected locator, erasure, root-count, pivot and correction indices, with checked geometry before allocating matrix scratch. |
| Acoustic OFDM interpolation, `src/fast/acoustic_ofdm.cpp` | Use a legal fallback sample for empty storage and bound selected PCM tap offsets through the address dependency. GNU/Clang targets use the register-mask helper; the MSVC helper instead fences accepted indices and needs separate performance validation. |
| LZMA/LZMA2 history and properties | Apply the generated XZ overlay described below. |
| Source decoding and attachment metadata | Place barriers after accepted source dimensions, output quotas and filename lengths; clip selected filename and UTF-8 continuation reads. Existing metadata formats and validation rules remain unchanged. |
| Text and file output | Filter received text before native consumers, and place a barrier before direct explicit payload writes. |

The full-domain GF(256) and GF(65536) log/exp tables are unchanged: their element
types and table extents cover the arithmetic indices used by those field
operations. The reviewed Legacy modem tables and fixed-profile LDPC graph
tables/inner arithmetic are also unchanged. Received samples affect their
values; local geometry and fixed tables determine the relevant traversal.
These observations explain where this change avoids extra hot-loop work; they
are not proofs that those subsystems have no other speculative or ordinary
memory-safety issues. No global speculative-load-hardening compiler mode or
blanket DSP serialization is enabled.

## Helper contract and architecture coverage

[`speculation.h`](../include/datapump/speculation.h) is shared by C and C++:

- `datapump_index_nospec(index, size)` retains an in-range index and otherwise
  returns zero. For an actual access, `size` must describe trusted, nonempty
  storage, and the caller must use the returned index. Numeric clipping with
  `size == 0` does not authorize a dereference. Ordinary invalid-input rejection
  stays in place; clipping is not a substitute for validation.
- `datapump_speculation_barrier()` supplies a supported target's barrier and
  compiler ordering at selected validation boundaries.

| Compiler and target | Index helper | Validation barrier |
| --- | --- | --- |
| GNU/Clang x86 | One inline-assembly `CMP`/`SBB`/`AND` register dependency; the compiler cannot replace this block with an ordinary conditional branch | `LFENCE` on x86-64 or SSE2-enabled x86 |
| GNU/Clang AArch64 with 64-bit `size_t` | `CMP`/`CSEL` followed by `CSDB` | `DSB SY` followed by `ISB` |
| MSVC x86/x64 | Successful checked path uses the intrinsic barrier before returning the index | `_mm_lfence()` and compiler barriers |
| Other combinations | Architectural clipping only, capability reported unsupported | No claimed CPU speculation barrier |

Compiler support is not a runtime assessment of the processor. In particular,
LFENCE's required execution-ordering behavior depends on the deployed CPU and
its configuration. This application does not configure or verify host firmware,
microcode or operating-system mitigations, and does not count them as protection
implemented by this change. Deployment evaluation remains separate.

The index dependency follows the bounds-preservation approach explained by the
[Linux kernel speculation documentation](https://www.kernel.org/doc/html/latest/staging/speculation.html).
The AArch64 sequence follows Arm's
[speculation-barrier guidance](https://documentation-service.arm.com/static/6227498d8804d00769e9b32f).
Intel describes the relevant LFENCE behavior in
[hardware features related to speculative execution](https://www.intel.com/content/www/us/en/developer/articles/technical/software-security-guidance/technical-documentation/hardware-behavior-related-to-speculative-execution.html);
Microsoft documents the intrinsic approach and its limits in
[C++ guidance for speculative execution side channels](https://learn.microsoft.com/en-us/cpp/security/developer-guidance-speculative-execution).
These references describe mechanisms, not a security certification of Data Pump.

## Pristine XZ source and generated receive overlay

[`cmake/XzReceiveHardening.cmake`](../cmake/XzReceiveHardening.cmake) verifies every
entry in the pinned `third_party/xz/UPSTREAM.sha256` inventory, copies the source
into `build/<profile>/generated/xz-receiver`, and applies exact-context changes
there. [`VendoredLzma.cmake`](../cmake/VendoredLzma.cmake) compiles that generated
tree into the private static library. The checked-in upstream tree and existing
vendor hash tests remain intact. A source hash or expected replacement-context
mismatch stops configuration instead of silently accepting a different patch.

Only three copied source files are modified:

1. `src/liblzma/lz/lz_decoder.h` clips received match distances and resulting
   dictionary offsets, including the copy extent, before history access. It
   preserves the optimized dictionary-copy loop. GNU/Clang targets use register
   dependencies; MSVC's index helper has the different cost described above.
2. `src/liblzma/lz/lz_decoder.c` initializes the bounded fallback region used by
   those accesses, including the reset/empty-dictionary case and extra initial
   SIMD-read space.
3. `src/liblzma/lzma/lzma2_decoder.c` places a barrier after received properties
   have been accepted, before they select the probability-table reset geometry.

The existing LZMA quotas, format checks and physical-end gate still apply. This
overlay is a narrowly scoped maintained difference from the pinned upstream
source, not a comprehensive hardening of liblzma or a new compression format.

## Explicit save and native services

Regular and Fast save dialogs receive a restricted filename suggestion, never
the raw payload. FLTK file preview is disabled; the Rev save service is a bounded
path prompt. A locally selected destination then reaches `write_new_file` in
[`src/runtime.cpp`](../src/runtime.cpp) or `ReceivedFile::save` in
[`src/fast/codec.cpp`](../src/fast/codec.cpp). Both use exclusive creation and
write the retained bytes directly with `fwrite`; a barrier precedes the payload
write. The save action does not decode, preview, launch or execute the payload.

This limits application processing; it does not make the saved file safe to
open later, prove standard-library or kernel code free of defects, or control
external indexing, antivirus, backup or other host services. The filename
allowlist and a user-authorized save do not establish a CPU-level isolation
boundary. No process sandbox or VM is added or credited here.

## Verification and build provenance

[`cmake/BuildInfo.cmake`](../cmake/BuildInfo.cmake) compiles capability probes
against the actual header macros. The probes do not execute target code and
therefore work with cross-compilation. Compiler target support or an explicit
unsupported status is recorded in configuration output and `build-info.txt`,
alongside the restriction to selected receive accesses and boundaries.

The `speculation` C test checks in-range and rejected indices, zero-extent numeric
behavior, integer extremes and dependent reads from nonempty arrays. On
supported GNU/Clang 64-bit x86 and AArch64 builds, the Python
`build_speculation_codegen` test disassembles optimized witnesses and checks the
mask-to-load dependency and barrier-before-load ordering. Those witnesses are
built without LTO so their symbols and instructions remain inspectable. The
check does not prove properties of every caller, every compiler configuration
or actual transient CPU execution. MSVC and other targets need their own
generated-code and hardware validation; support in the header is not that
validation.

Independent wire vectors, malformed-source checks, exact-byte save comparisons,
physical-completion tests and GUI receive-policy regressions remain necessary.
Address/undefined-behavior sanitizer runs test ordinary memory behavior; they do
not detect or certify the absence of speculative side channels.

## Performance validation

Compare the same compiler, optimization, payload, coding profile and machine
before and after the change. Measure source decompression separately from Fast
decoder throughput and sampled modem reception, since the targeted operations
have different execution frequencies. Record wall-clock timings and limitations
with the functional checks in [validation](validation.md). A generated-code
check or unchanged wire vector does not establish acceptable runtime overhead.

The recorded GCC/x86-64 comparison found about 1.2% higher sampled OFDM processing
time, 2.0% for patterned LZMA2 output and up to 4.5% across the measured
Reed–Solomon workloads. Short decoding rose from about 0.068 to 0.273 µs for a
16-byte message. These are local workload measurements with scheduling/boost
noise, not universal performance guarantees. The validation record includes the
full table, per-run results and workload definitions; MSVC's fencing fallback has
not been benchmarked here.

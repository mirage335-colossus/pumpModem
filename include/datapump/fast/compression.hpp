#pragma once

#include "datapump/fast/codec.hpp"
#include <stop_token>

namespace datapump::fast {
// The production source is one XZ stream with LZMA2 and CRC32, independent of
// the raw Fast coding API. Reader calls and scratch stay locally bounded.
SourceReader xz_source(SourceReader,std::uint64_t source_quota=256ULL*1024*1024,
                       std::stop_token={});
struct PreparedXzSource { Bytes encoded; std::uint64_t source_bytes=0; };
// Compress once into bounded immutable RAM before opening playback. This avoids
// variable compression work stalling the live audio producer.
PreparedXzSource prepare_xz_source(SourceReader,std::uint64_t source_quota=256ULL*1024*1024,
                                  std::stop_token={});
// Decoding is invoked only after physical completion and coding/integrity checks.
Bytes decode_xz(std::span<const std::uint8_t>,std::uint64_t output_quota);
// Bound for this pinned streaming encoder (one block, no intermediate flush),
// for a source whose compressed content is not yet known.
std::uint64_t xz_size_bound(std::uint64_t source_bytes);
// Streams a local compression prepass without retaining the complete source.
// The returned airtime includes actual XZ bytes and physical end silence;
// source_bps counts original source bytes, not XZ bytes.
TransmitEstimate estimate_xz_transmission(const Profile&,bool encrypted,SourceReader,
    std::uint64_t source_quota=256ULL*1024*1024,std::stop_token={});
}

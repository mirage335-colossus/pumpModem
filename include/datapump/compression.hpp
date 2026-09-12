#pragma once
#include "datapump/types.hpp"
#include <optional>
#include <span>

namespace datapump::compression {
// One fixed code over bytes. There are no phrases, codebook headers or codec
// identifiers. Space/e/t/a/o use 3 bits, i/n use 4, fourteen other lowercase
// bytes use 6; every remaining byte uses a 5-bit escape plus its 8 literal bits.
Bytes encode_short(std::span<const std::uint8_t> input,
                   std::size_t output_limit = default_memory_limit);
// The caller supplies the exact uncompressed size. Requires a complete,
// canonical stream with no extra data and at most 7 trailing zero pad bits.
Bytes decode_short(std::span<const std::uint8_t> encoded,
                   std::size_t original_size,
                   std::size_t output_limit = default_memory_limit);
// Returns only complete decoded bytes, stopping at incomplete input or the
// requested output bound. Reaching original_size also checks final padding.
// Canonical-code errors throw; previews are not integrity-verified content.
Bytes preview_short(std::span<const std::uint8_t> encoded,
                    std::size_t original_size, std::size_t maximum_output);

// Raw LZMA2 uses a fixed profile whose history size is inferred from the known
// original length. No container header or dictionary identifier is emitted.
inline constexpr std::size_t long_encoder_limit=768U*1024U*1024U;
inline constexpr std::size_t long_decoder_limit=80U*1024U*1024U;
std::size_t long_encoder_workspace(std::size_t original_size);
std::size_t long_decoder_workspace(std::size_t original_size);
// No candidate is returned unless it is strictly smaller than the input.
std::optional<Bytes> encode_long(std::span<const std::uint8_t> input,
                               std::size_t workspace_limit=long_encoder_limit);
Bytes decode_long(std::span<const std::uint8_t> encoded, std::size_t original_size,
                  std::size_t output_limit=default_memory_limit,
                  std::size_t workspace_limit=long_decoder_limit);
// At most 65536 complete bytes; truncated input returns its decoded prefix.
// Malformed input encountered before the output bound throws. This does not
// authenticate the prefix or validate bytes beyond that bound.
Bytes preview_long(std::span<const std::uint8_t> encoded, std::size_t original_size,
                   std::size_t maximum_output,
                   std::size_t workspace_limit=long_decoder_limit);
}

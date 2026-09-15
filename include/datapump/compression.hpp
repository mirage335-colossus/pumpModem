#pragma once
#include "datapump/types.hpp"
#include <span>

namespace datapump::compression {
// Application codec: one raw LZMA2 stream with an agreed fixed dictionary.
// No original-size field is needed. The decoder is a post-physical-end API;
// there is deliberately no speculative/preview entry point.
inline constexpr std::size_t lzma2_dictionary_bytes=4U*1024U*1024U;
inline constexpr std::size_t long_encoder_limit=64U*1024U*1024U;
inline constexpr std::size_t long_decoder_limit=8U*1024U*1024U;
std::size_t lzma2_encoder_workspace();
std::size_t lzma2_decoder_workspace();
// Always emits a stream, including for empty or incompressible input.
Bytes encode_lzma2(std::span<const std::uint8_t> input,
                  std::size_t output_limit=default_memory_limit,
                  std::size_t workspace_limit=long_encoder_limit);
struct Lzma2Decoded { Bytes data; std::size_t consumed_bytes=0; };
// Incremental bounded output. Stops only at the codec end; returns its exact
// consumed extent so the source layer can check fixed final zero padding.
// Truncation, malformed input, output/scratch exhaustion throw Error.
Lzma2Decoded decode_lzma2(std::span<const std::uint8_t> encoded,
                  std::size_t output_limit=default_memory_limit,
                  std::size_t workspace_limit=long_decoder_limit);
}

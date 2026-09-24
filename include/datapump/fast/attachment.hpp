#pragma once

#include "datapump/fast/codec.hpp"
#include <string_view>

namespace datapump::fast::attachment {
inline constexpr std::string_view opening="#ATTACHMENT### ";
inline constexpr std::string_view closing=" #ATTACHMENT### ";
inline constexpr std::size_t filename_limit=255;
inline constexpr std::size_t prefix_limit=opening.size()+filename_limit+closing.size();
// This source convention is independent of the regular modem's envelope.
// A name is only a suggested basename, never a destination path or write request.
std::string filename_from_path(const std::filesystem::path&);
std::string prefix(std::string_view filename);
std::uint64_t source_limit(std::uint64_t content_limit);
SourceReader source(SourceReader content,std::string_view filename,std::uint64_t content_limit);
struct Description { std::string filename; std::size_t prefix_bytes=0; };
// Call only after physical completion and source decompression. Inspect at byte
// zero within the fixed local prefix bound. Invalid prefixes remain ordinary data.
Description inspect(std::span<const std::uint8_t>);
}

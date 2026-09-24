#pragma once
#include "datapump/stream_codec.hpp"
#include <string_view>

namespace datapump::attachment {
inline constexpr std::string_view prefix="#ATTACHMENT### ";
inline constexpr std::string_view suffix=" ###ATTACHMENT# ";
inline constexpr std::size_t filename_limit=255;
inline constexpr std::size_t marker_limit=prefix.size()+filename_limit+suffix.size();
// Application source convention, never modem framing. A bounded basename is
// a suggested Save name only; it never selects a path or causes a file write.
std::string marker(std::string_view filename);
std::size_t source_limit(std::size_t content_limit);
Bytes encode(const Message&,std::size_t content_limit);
// Call only on a completed, decoded source. Invalid/missing markers leave the
// source as ordinary bytes; no content sniffing or extension selects a file.
void interpret(Message&,std::size_t content_limit);
}

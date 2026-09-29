#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace datapump::host::protocol {
// Local pipe / Worker-message framing, not an HTTP or modem wire protocol.
// All integers and float32 values use little-endian encoding. An allocation is
// bounded before its remotely supplied length is used.
inline constexpr std::uint32_t version=1;
inline constexpr std::size_t max_frame=8*1024*1024;
inline constexpr std::size_t max_text=1024*1024;
enum class Type : std::uint32_t {
    viewport=1,event=2,audio_configure=3,audio_capture=4,audio_ready=5,
    audio_progress=6,audio_error=7,audio_cancelled=8,reconnect=9,
    upload_begin=10,upload_chunk=11,upload_commit=12,download_prepare=13,clock_ping=15,
    snapshot=101,audio=102,error=103,closed=104,
    file_begin=105,file_chunk=106,file_end=107,clock_reply=108
};
struct Frame {Type type;std::vector<std::byte> payload;};
class Reader {
public:
    explicit Reader(std::span<const std::byte> bytes):bytes_(bytes){}
    std::uint32_t u32();
    std::uint64_t u64();
    float f32();
    double f64();
    std::string text(std::size_t limit=max_text);
    void end() const;
private:
    std::span<const std::byte> take(std::size_t count);
    std::span<const std::byte> bytes_;
};
class Writer {
public:
    void u32(std::uint32_t value);
    void u64(std::uint64_t value);
    void f32(float value);
    void f64(double value);
    void text(const std::string& value);
    std::vector<std::byte> bytes;
};
std::vector<std::byte> encode(const Frame& frame);
// One decoder per pipe. feed accepts an arbitrary fragment; EOF must call finish
// so a truncated frame cannot be silently accepted as an orderly disconnect.
class Decoder {
public:
    std::vector<Frame> feed(std::span<const std::byte> bytes);
    void finish() const;
private:
    std::vector<std::byte> buffer_;
};
}

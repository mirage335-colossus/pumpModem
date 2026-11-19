#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace datapump::lab {

// A shared Runtime output span is one whole DPW1 frame unless the worker has
// already started writing that frame. Only inspect a header at a frame
// boundary. Ordinary UI frames never get precedence over audio.
inline bool is_clock_reply(std::span<const std::byte> bytes) {
    if (bytes.size() < 12 || bytes[0] != std::byte{'D'} || bytes[1] != std::byte{'P'} ||
        bytes[2] != std::byte{'W'} || bytes[3] != std::byte{'1'}) return false;
    return bytes[4] == std::byte{108} && bytes[5] == std::byte{0} &&
           bytes[6] == std::byte{0} && bytes[7] == std::byte{0};
}

// Continue any partly written frame before choosing another. At a boundary,
// prefer clock replies over audio, but allow one audio frame after at most
// four consecutive replies so an untrusted ping stream cannot starve audio.
inline bool select_audio(bool audio_pending, bool audio_partial,
                         bool runtime_partial, bool runtime_clock,
                         unsigned clock_burst) {
    if (audio_partial) return true;
    if (runtime_partial) return false;
    return audio_pending && (!runtime_clock || clock_burst >= 4);
}

}

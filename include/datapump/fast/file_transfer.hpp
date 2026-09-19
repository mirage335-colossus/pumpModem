#pragma once
#include "datapump/fast/session.hpp"
#include <functional>
#include <stop_token>

namespace datapump::fast {
using ProgressCallback=std::function<void(const Snapshot&)>;
// Streaming local WAV container I/O, separate from the packetless audio wire.
// TX records actual silence after its waveform; RX EOF never completes a file.
Snapshot transmit_wave(const Settings&,const std::filesystem::path& source,
    const std::filesystem::path& wave,ProgressCallback={},std::stop_token={});
Snapshot transmit_text_wave(const Settings&,const std::string& text,
    const std::filesystem::path& wave,ProgressCallback={},std::stop_token={});
Snapshot receive_wave(const Settings&,const std::filesystem::path& wave,
    ProgressCallback={},std::stop_token={});
}

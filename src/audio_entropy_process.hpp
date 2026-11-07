#pragma once

#include "datapump/audio.hpp"
#include <memory>
#include <stop_token>
#include <string>

namespace datapump::audio::detail {
// Native-only isolated capture. The registration must remain alive until the
// child is known to have stopped using the selected input.
void capture_entropy_process(const std::string& device, bool exclusive,
    std::shared_ptr<const EntropyCaptureOwner> owner, std::stop_token stop);
void reap_quarantined_entropy_process() noexcept;
}

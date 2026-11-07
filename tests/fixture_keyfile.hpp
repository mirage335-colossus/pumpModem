#pragma once

#include "datapump/crypto.hpp"

#include <array>
#include <cstdint>

// Deterministic, nonphysical audio for isolated keyfile tests only. This is
// not an entropy source and must never be used to generate a production key.
namespace fixture_keyfile {
inline datapump::KeyfileGenerationOptions generation() {
    datapump::KeyfileGenerationOptions options;
    options.audio=[](datapump::AudioEntropySink& sink,std::stop_token stop) {
        std::uint32_t state=0x6d2b79f5U;
        std::array<float,1024> samples{};
        while(!sink.complete()) {
            if(stop.stop_requested()) throw datapump::Error("keyfile generation cancelled");
            for(auto& sample:samples) {
                state^=state<<13; state^=state>>17; state^=state<<5;
                sample=static_cast<float>(static_cast<std::int32_t>(state))/2147483648.0F;
            }
            sink.add_samples(samples,8000);
        }
    };
    return options;
}
} // namespace fixture_keyfile

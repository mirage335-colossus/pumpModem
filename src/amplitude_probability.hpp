#pragma once
#include "datapump/pattern_correlator.hpp"
#include <stop_token>
#include <string>
namespace datapump::simulation::detail {
// Conditional one-symbol engineering reference. This does not model acquisition
// ownership, competing keys/epochs, continuation or physical completion.
struct AmplitudeProbabilityResult {
    bool available=false;
    double correct=0,wrong=0,threshold=0;
    std::size_t trials=0,source_blocks=0;
    std::uint64_t physical_blocks=0;
    std::string limit;
};
AmplitudeProbabilityResult amplitude_probability(const modem::Config&,
    const modem::PatternSearch&,double symbol_snr_db,double frequency_offset_hz,
    std::size_t trials=4096,std::stop_token = {});
}

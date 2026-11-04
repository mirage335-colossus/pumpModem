#pragma once
// PRIVATE finite-observation integration, not a maintained receiver API.
#include "conditioned_reference_export.hpp"
#include "conditioned_first_bit.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/channel.hpp"
#include <optional>
#include <string>
namespace datapump::simulation::detail::conditioned_factory {
struct SourceConditions {
    // Exact production sampled channel for one fixed initial phase trajectory.
    // Phase diffusion is outside this first finite reference: nonzero declines.
    modem::ChannelConfig channel;
    // Known-onset-cropped isolated first bit, at up to32 nearby starts.
    // Full original batch trial charge does not make this an arrival-search
    // acquisition or complete-message probability model.
    std::size_t starts=1,trials=256;
    // Exclusive last-observed raw frontier. A helper Reference at inclusive
    // sample k maps to k+1 with checked overflow, never directly to k.
    std::uint64_t prior_evidence_end_sample=0;
    bool independent_prior=false;
    std::optional<double> payload_filter_hz;
    // First factory scope retains original RF coordinates; nonzero declines.
    double frequency_correction_hz=0,signal_scale=1,raw_noise_variance=1;
    std::size_t maximum_rendered_samples=131072;
    std::size_t transmitter_workspace_bytes=8*1024*1024;
};
struct FactoryWork {
    std::uint32_t source_sample_rate=0,detector_sample_rate=0;
    std::size_t source_raw_samples=0,retained_raw_samples=0;
    std::size_t receiver_workspace_bytes=0,transmitter_workspace_bytes=0,channel_workspace_bytes=0;
    std::size_t descriptor_bytes=0,original_jobs=0,selected_jobs=0,original_starts=0;
    std::uint64_t original_threshold_trials=0;
};
struct Result {
    std::optional<conditioned_private::FirstBitRequest> request;
    FactoryWork work;
    std::string reason;
};
// The caller supplies an ACTUAL pristine ordinary owner prepared with normal
// Auto/rolling admission and immutable full RF/LF search. Selection is indexed
// by that owner's original frequency/LF pairs, not a regenerated sorted bank.
Result make_first_bit_request(const modem::PatternReceiver& ordinary,
    std::span<const std::uint8_t> selected_original_pairs,const SourceConditions&,std::stop_token={});
}

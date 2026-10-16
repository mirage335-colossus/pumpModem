#pragma once
#include "datapump/live.hpp"
#include "datapump/streaming_modem.hpp"
#include "live_pattern_scores.hpp"
#include "live_deferred_pattern_history.hpp"
#include "scheduled_source_binding_types.hpp"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace datapump::modem::detail {struct SourceFamily;}
namespace datapump::live::detail {
// The runtime owns these SAME types; allocation accounting never duplicates a
// guessed representation. Include the complete SourceFamily before creating or
// destroying ReceiverState. Pure layout callers only inspect sizeof.
struct ReceiverState {
    transfer::Options options;
    Bytes key_tag;
    std::uint64_t epoch = 0;
    double admitted_at = 0;
    bool prefer_streamed_templates = false;
    bool clock_window_modeled = false;
    std::size_t amplitude_fft_samples=0;
    double last_confident_at = 0;
    std::uint64_t last_confident_end = 0;
    std::uint64_t sample_origin = 0, family = 0;
    modem::detail::scheduled_source_samples::PointFamilyToken point_family;
    std::uint64_t point_consumer=UINT64_MAX;
    PatternScoreHistory pattern_score_history;
    DeferredPatternHistoryOrdinals history_ordinals;
    DeferredPatternHistoryPoll history_poll;
    struct Presentation {
        std::uint64_t candidate_id=0, signal_id=0, published_revision=0;
        bool content_reported=false;
    };
    // Candidate chunks may interleave. Retain an identity while a detector or
    // queued chunk can publish it; abandonment never implies physical END.
    std::map<std::pair<std::uint64_t,std::uint64_t>,Presentation> presentations;
    std::vector<modem::PatternBurst> bursts;
    // Immutable capability survives optional arena trims.
    std::unique_ptr<const modem::detail::SourceFamily> source_curve_family;
    std::unique_ptr<modem::StreamingReceiver> modem;
    std::unique_ptr<transfer::StreamReceiver> content;
};
struct ReplayFrame {
    std::vector<float> waveform, spectrum;
    std::optional<double> simulation_spectrum_gain_db;
    std::vector<std::complex<float>> constellation, pattern_scores;
    std::vector<PatternScoreObservation> pattern_score_observations;
    std::uint64_t pattern_score_observation_id = 0;
    ConstellationSource source = ConstellationSource::input;
    double fraction = 0;
    std::uint64_t dropped = 0;
    modem::TransmitTrace transmit_trace;
};

inline constexpr std::size_t plot_size=2048,maximum_events=64,replay_frames=60,
    replay_wave_samples=256,replay_bins=257,minimum_constellation_limit=2048,
    pattern_score_limit=Snapshot::pattern_score_limit,replay_text_limit=4096,
    minimum_workspace=512*1024;
extern const std::size_t replay_frame_base,replay_result_workspace;
std::size_t constellation_limit(const modem::Config& config);
std::size_t constellation_limit(const Settings& settings);
std::size_t replay_workspace(const Settings& settings);
std::size_t plot_workspace(const Settings& settings);
std::size_t audio_reserve(const Settings& settings);
// Reservation arithmetic alone, also usable for analytical non-Live inputs.
// Exhausted budgets have no receiver capacity; live admission throws separately.
std::size_t available_bank_capacity(const Settings& settings);
std::size_t bank_capacity(const Settings& normalized_settings);
std::size_t source_budget(std::size_t content);
modem::ChannelConfig channel_config(const Settings& settings);
// All value/geometry/allocation normalization is shared; provider capability
// acceptance stays at the actual Live boundary, before configuration mutation.
unsigned effective_epoch_search_seconds(const transfer::Options& options,bool strict_live=true);
Settings normalized_layout(Settings settings);
}

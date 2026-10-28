#pragma once
#include "datapump/live.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/symbol_burst.hpp"
#include "live_pattern_scores.hpp"
#include "live_source_handoff.hpp"
#include "live_helper_score_frequency.hpp"
#include "native_point_workspace.hpp"
#include "live_deferred_pattern_history.hpp"
#include "scheduled_source_binding_types.hpp"
#include <complex>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <utility>
#include <type_traits>
#include <vector>

namespace datapump::modem::detail {struct SourceFamily;}
namespace datapump::live::detail {
struct HelperInputRecipe;
struct HelperInputView;
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
    // These bytes occupy existing alignment padding before amplitude state.
    // Input provenance is immutable until a full numeric reconstruction;
    // optional scheduling and joined Error recovery are separate capabilities.
    enum HelperFlag : std::uint8_t {helper_filtered_input=1,helper_eligible=2,
        helper_deferred_error=4,helper_departure=8,helper_broad_audit=16,helper_payload_dependency=32,
        helper_compact_phase=64,helper_qualified_mask=128};
    std::uint8_t helper_flags=0,helper_audit_group=0;
    std::uint8_t helper_input_view=0;
    bool helper_audit_scored=false;
    std::size_t amplitude_fft_samples=0,amplitude_channel_count=0;
    // Actual selected passbands for coarse-frequency presentation arbitration.
    std::optional<modem::PatternAmplitudeGeometry> amplitude_geometry;
    double last_confident_at = 0;
    std::uint64_t last_confident_end = 0;
    std::uint64_t sample_origin = 0, family = 0;
    std::uint64_t helper_view_next_sample=0;
    // Immutable translated views and raw broad-audit owners are disjoint.
    // Reuse their bookkeeping word; neither increases ordinary row storage.
    union {
        std::uint64_t helper_view_jobs_accounted=0;
        std::uint64_t helper_audit_generation;
    };
    modem::detail::scheduled_source_samples::PointFamilyToken point_family;
    std::uint64_t point_consumer=UINT64_MAX;
    modem::detail::NativePointOwner native_points;
    PatternScoreHistory pattern_score_history;
    DeferredPatternHistoryOrdinals history_ordinals;
    DeferredPatternHistoryPoll history_poll;
    struct Presentation {
        std::uint64_t candidate_id=0, signal_id=0, published_revision=0;
        bool content_reported=false;
        enum class CoordinateKind : std::uint8_t {burst,ordinary};
        CoordinateKind coordinate_kind=CoordinateKind::burst;
        struct BurstCoordinates {
            std::optional<modem::SymbolBurstCoordinate> first;
            std::optional<modem::SymbolBurstSlot> next;
        };
        union Coordinates {
            BurstCoordinates burst{};
            ReceptionSourcePublication ordinary;
        } coordinates{};
        const ReceptionSourcePublication* source_publication()const noexcept {
            return coordinate_kind==CoordinateKind::ordinary?&coordinates.ordinary:nullptr;
        }
        void source_publication(const ReceptionSourcePublication& value) noexcept {
            if(coordinate_kind!=CoordinateKind::ordinary) {
                std::construct_at(&coordinates.ordinary,value);coordinate_kind=CoordinateKind::ordinary;
            } else coordinates.ordinary=value;
        }
        void clear_source_publication()noexcept {
            if(coordinate_kind==CoordinateKind::ordinary)coordinates.ordinary={};
        }
        // Exactly the preceding Burst-only fixed footprint. Ordinary helper-off
        // rows neither reserve nor allocate any additional publication state.
        static_assert(sizeof(ReceptionSourcePublication)<=sizeof(BurstCoordinates));
        static_assert(sizeof(Coordinates)==sizeof(BurstCoordinates));
        static_assert(std::is_trivially_destructible_v<BurstCoordinates> &&
            std::is_trivially_destructible_v<ReceptionSourcePublication>);
    };
    struct PreviousPresentationLayout {
        std::uint64_t candidate_id=0,signal_id=0,published_revision=0;
        bool content_reported=false;
        std::optional<modem::SymbolBurstCoordinate> burst_first;
        std::optional<modem::SymbolBurstSlot> burst_next;
    };
    static_assert(sizeof(Presentation)==sizeof(PreviousPresentationLayout));
    static_assert(alignof(Presentation)==alignof(PreviousPresentationLayout));
    // Candidate chunks may interleave. Retain an identity while a detector or
    // queued chunk can publish it; abandonment never implies physical END.
    std::map<std::pair<std::uint64_t,std::uint64_t>,Presentation> presentations;
    std::vector<modem::PatternBurst> bursts;
    // Immutable capability survives optional arena trims.
    std::unique_ptr<const modem::detail::SourceFamily> source_curve_family;
    std::unique_ptr<modem::StreamingReceiver> modem;
    std::unique_ptr<transfer::StreamReceiver> content;
    std::shared_ptr<const HelperInputRecipe> helper_input_recipe;
    // Immutable actual-constructor coverage; no heap receipt, PCM, timestamp,
    // phase or END authority. Only full numeric reconstruction renews it.
    std::optional<HelperScoreFrequencyLease> realtime_constructor_lease;
    std::weak_ptr<const HelperInputView> realtime_constructor_view;
};
// The helper controls must fit the padding that already followed the two
// bools. Fail on any ABI that would move ordinary amplitude/payload state.
// Supported compilers evaluate this non-standard-layout offset as an extension.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
static_assert(offsetof(ReceiverState,amplitude_fft_samples)==
    (offsetof(ReceiverState,clock_window_modeled)+sizeof(bool)+alignof(std::size_t)-1)/
        alignof(std::size_t)*alignof(std::size_t));
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
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
extern const std::size_t replay_frame_base,replay_result_workspace,replay_pending_workspace;
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

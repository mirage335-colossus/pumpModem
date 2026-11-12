#pragma once

#include "datapump/fast/codec.hpp"
#include "datapump/fast/modem.hpp"
#include "ssb_candidate_search.hpp"
#include "ssb_quiet.hpp"
#include <memory>

namespace datapump::fast::detail {

struct SsbCycleRefinementProgress {
    std::uint64_t attempted = 0;
    std::uint64_t accepted = 0;
    std::uint64_t rate_updates = 0;
    std::uint64_t phase_updates = 0;
    std::optional<std::uint64_t> first_accepted_wire_ordinal;
    std::optional<std::uint64_t> first_phase_update_wire_ordinal;
};

struct SsbQuietScoreProgress {
    std::uint64_t units = 0;
    std::optional<SsbCenterRange> last;
    std::size_t last_groups = 0, last_observable_groups = 0;
    double last_squared_coherence = 0.;
    double last_projected_to_raw_energy = 0.;
};

struct SsbQuietProgress {
    SsbQuietScoreProgress absent, present, no_detection, unscored;
    std::uint64_t no_detection_unobservable_units = 0;
    std::uint64_t unscored_payload_intervals = 0;
    std::uint64_t longest_quiet_ticks = 0;
    std::uint64_t low_power_calibrated_units = 0, low_power_coherent_veto_units = 0;
    std::optional<double> low_power_ceiling;
    std::optional<std::uint64_t> quiet_eligible_after_tick;
    std::optional<SsbCenterRange> current_quiet_run, pending_observation, last_predictive_payload;
};

enum class SsbReceiverModel { sparse_delay_doppler, local_fir };
enum class SsbReceiverModelPolicy { sparse_delay_doppler, validate_local_fir };
enum class SsbCycleObservation { before_refinement, after_refinement, refinement_failed };

struct SsbReceiverModelProgress {
    std::optional<SsbReceiverModel> selected;
    bool local_eligible = false;
    bool local_tested = false;
    bool local_admission_present = false, local_prospective_present = false;
    std::optional<double> local_admission_nmse, local_prospective_nmse, local_pooled_nmse;
};

struct SsbAcquisitionAttempt {
    std::uint64_t training_first_center_tick, observed_past_last_center_tick;
    double earliest_epoch_from_training_ticks, latest_epoch_from_training_ticks;
    double wall_seconds, training_nmse;
    SsbAcquisitionDiagnostics::Status status;
    std::size_t marker_evaluations;
    std::size_t marker_primary_rejections, marker_decoy_banks;
    double marker_primary_seconds, marker_decoy_seconds;
    double marker_row_seconds, marker_gram_seconds, marker_factor_seconds;
};

// Continuous single-carrier SSB receive path for a locally matched profile.
class SsbReceiver {
public:
    using IntervalSink = ValidatedIntervalSink;
    using CycleObserver = std::function<void(std::uint64_t,SsbCycleObservation)>;

    SsbReceiver(const Profile&,SsbFrontendGeometry,SsbAcquisitionRequest,double minimum_coherence,
                IntervalSink,std::stop_token stop={},
                SsbReceiverModelPolicy policy=SsbReceiverModelPolicy::sparse_delay_doppler,
                SymbolObserver symbol_observer={},SymbolObserver input_observer={});
    ~SsbReceiver();
    SsbReceiver(SsbReceiver&&) noexcept;
    SsbReceiver& operator=(SsbReceiver&&) noexcept;

    std::size_t push(std::span<const float>);
    void finish() noexcept;
    // Local diagnostics only. Set before feeding PCM.
    void set_cycle_observer(CycleObserver);
    const ModemProgress& progress() const noexcept;
    const SsbCandidateSearchProgress& acquisition_progress() const noexcept;
    const SsbAcquisitionDiagnostics& last_acquisition_diagnostics() const noexcept;
    std::span<const SsbAcquisitionAttempt> first_acquisition_attempts() const noexcept;
    const SsbCycleRefinementProgress& refinement_progress() const noexcept;
    const SsbQuietProgress& quiet_progress() const noexcept;
    const SsbReceiverModelProgress& model_progress() const noexcept;
    // Raw input count at the current frontend callback, including FIR lookahead.
    std::uint64_t consumed_input_samples() const noexcept;
    std::optional<std::uint64_t> completion_tick() const noexcept;
    std::size_t workspace_bytes() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

bool supports_modeled_ssb_receiver(const Profile&) noexcept;
SsbReceiver make_ssb_model_receiver(const Profile&,ValidatedIntervalSink,std::stop_token stop={},
                                   SymbolObserver symbol_observer={},SymbolObserver input_observer={});

// Internal measurements use the operating receiver without changing dispatch.
struct ReceiverDiagnostics {
    static SsbReceiver* ssb(Receiver& receiver) noexcept { return receiver.ssb_.get(); }
    static const SsbReceiver* ssb(const Receiver& receiver) noexcept { return receiver.ssb_.get(); }
};

} // namespace datapump::fast::detail

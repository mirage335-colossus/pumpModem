#pragma once
#include "datapump/pattern_receiver.hpp"
#include <string>

namespace datapump::modem {
enum class ReceiverBackend { existing_complex, frequency_axis, amplitude_energy };
struct ReceiverStrategyPlan {
    ReceiverBackend backend=ReceiverBackend::existing_complex;
    bool supported=true,experimental=false,workspace_limited=false;
    // No candidate in this particular keyed epoch intersects the arrival
    // prior. This is proved exclusion, not a missing decoder or quota failure.
    bool empty_scheduled_epoch=false;
    bool full_frequency_coverage=false,full_clock_coverage=false;
    std::string reason;
    double cpu_budget=0,existing_processing_ratio=0,frequency_axis_processing_ratio=0;
    double amplitude_processing_ratio=0,selected_processing_ratio=0;
    double complex_tracking_processing_ratio=0;
    // Resolved extent of the selected amplitude geometry, including explicit
    // hypothesis endpoints beyond a rounded requested interval.
    double amplitude_frequency_half_width_hz=0,amplitude_clock_half_width_ppm=0;
    std::size_t estimated_workspace_bytes=0,amplitude_channels=0;
    std::size_t selected_bank_count=1,selected_workspace_bytes=0;
    double amplitude_cohort_lifetime_seconds=0;
    bool scheduled=false,scheduled_cost_calibrated=false;
    bool scheduled_implicit_acquisition=false;
    std::size_t scheduled_bank_required_bytes=0,scheduled_local_capture_bytes=0,scheduled_shared_capture_bytes=0,scheduled_optional_tile_bytes=0;

    unsigned scheduled_frame_hops=1;
    std::uint64_t scheduled_active_hops=1;
    double scheduled_effective_multiplier=1,scheduled_active_seconds=0,scheduled_frame_seconds=0;
    double scheduled_minimum_quiet_seconds=0,scheduled_flat_duty=1;
    double scheduled_recording_union_upper=1,scheduled_fft_seconds=0;
    double scheduled_fft_union_upper=1,scheduled_interval_width_seconds=0;
    double scheduled_timing_spacing_seconds=0,scheduled_phase_spacing_seconds=0;
    double scheduled_capture_frame_bytes_upper=0;
    std::size_t scheduled_canonical_phases=0;
    double scheduled_timing_positions=0,scheduled_full_timing_positions=0;
    double scheduled_mean_processing_ratio=0,scheduled_deadline_processing_ratio=0;
    double scheduled_guaranteed_recovery_seconds=0;
};
// Immutable receiver-local selection. Ratios are engineering elapsed-time
// estimates for the supplied cohort, not measured throughput or probabilities.
// Neither this policy nor its estimates may change the transmitted waveform.
ReceiverStrategyPlan resolve_receiver_strategy(const Config&,const PatternSearch&,std::size_t workspace_bytes);
// Planning only: expected layout, never a runtime owner/availability grant.
// Use the SAME cohort capacity as Live (after capture/TX/plot/audio reserves),
// with the actual configured hard consumer allocation and a conservative or
// exact control margin. No measured CPU/probability is implied by fitting RAM.
ReceiverStrategyPlan plan_receiver_strategy(const Config&,const PatternSearch&,std::size_t per_bank_allowance,
    std::size_t cohort_workspace,std::size_t maximum_capture_consumers,
    std::size_t receiver_control_margin);
const char* receiver_backend_name(ReceiverBackend);
std::size_t receiver_strategy_bank_count(const Config&,unsigned epoch_radius,std::size_t keys,std::size_t profiles);
// Ordinary amplitude's unconfirmed rolling acquisition allowance includes the
// first fully observed symbol, slowest clock row and FFT endpoint slack. Not a
// bound on synchronized tracks or stalled/backlogged processing. Scheduled
// receivers retain their separately modeled calendar cohort.
std::size_t receiver_strategy_amplitude_bank_count(const Config&,const PatternSearch&,
    std::size_t keys,std::size_t profiles);
}

#include "scheduled_acquisition_policy.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/scheduled_complex_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "pattern_drift.hpp"
#include "pattern_envelope.hpp"
#include "pattern_differential.hpp"
#include "pattern_fft_batch.hpp"
#include "pattern_start_geometry.hpp"
#include "search_parallel.hpp"
#include "shared_scheduled_capture.hpp"
#include <array>
#include <cstdlib>
#include <string_view>
#include <cmath>
#include <limits>
#include <set>

namespace datapump::modem {
namespace {
double finite_ratio(long double v) { return static_cast<double>(std::min(v,static_cast<long double>(std::numeric_limits<double>::max()))); }
std::size_t pow2(std::size_t n) { std::size_t p=1;while(p<n && p<=std::numeric_limits<std::size_t>::max()/2)p*=2;return p; }
}
const char* receiver_backend_name(ReceiverBackend b) {
    switch(b) {
    case ReceiverBackend::frequency_axis:return "complex frequency-axis FFT";
    case ReceiverBackend::amplitude_energy:return "amplitude energy";
    default:return "existing complex";
    }
}
std::size_t receiver_strategy_bank_count(const Config& c,unsigned radius,std::size_t keys,std::size_t profiles) {
    const auto logical=static_cast<long double>(symbol_sample_count(c))+training_sample_count(c)+
        pattern_pulse_padding_samples(c);
    long double epochs=c.scramble||c.dsss?2.L*radius+3+std::ceil(logical/c.sample_rate):1;
    if(c.scheduled_hopping&&c.scheduled_hopping->enabled()&&!c.scheduled_hopping->continuous()) {
        const auto& plan=*c.scheduled_hopping;plan.validate();
        const auto group=static_cast<long double>(2ULL*c.sample_rate/5)*plan.group_hops;
        if(!(group>0))throw Error("scheduled receiver source group has no samples");
        // Live admits only UTC epoch seconds touching a non-null active group.
        // One source lifetime spans at most ceil(L/B)+3 such groups; a group
        // touches at most ceil(.4G)+1 integer epoch intervals. Keep initial
        // timing alternatives and a straddling frame outside that product.
        // Planning heuristic, not a certified resident-bank upper bound. A
        // negative-origin acquisition may need a truncated symbol followed by
        // a complete one before retirement (nearly two source symbols). Live
        // still checks actual required consumers/remaining workspace; neither
        // this estimate nor wall-clock age authorizes early retirement.
        epochs=2.L*radius+3+(std::ceil(logical/group)+3)*(std::ceil(.4L*plan.group_hops)+1);
    }
    const auto total=epochs*std::max<std::size_t>(1,keys)*std::max<std::size_t>(1,profiles);
    return static_cast<std::size_t>(std::min(total,static_cast<long double>(std::numeric_limits<std::size_t>::max())));
}
namespace {
struct PlanningCaptureContext {std::size_t cohort_workspace=0,maximum_consumers=0;};
ReceiverStrategyPlan resolve_receiver_strategy_impl(const Config& c,const PatternSearch& search,std::size_t workspace,
                                                   const PlanningCaptureContext* planning) {
    if(search.receiver_strategy_workspace_bytes)workspace=std::min(workspace,search.receiver_strategy_workspace_bytes);
    ReceiverStrategyPlan p;p.cpu_budget=c.receiver_cpu_budget;
    p.frequency_axis_processing_ratio=p.amplitude_processing_ratio=std::numeric_limits<double>::infinity();
    const auto symbol=symbol_sample_count(c),chip=pattern_chip_samples(c);
    const auto seconds=static_cast<double>(symbol)/c.sample_rate;
    std::string scheduled_complex_failure;
    // Streaming mapped contraction is independent of a symbol-sized FFT.
    // Resolve it before any transform sizing, including hours-long symbols.
    if(scheduled_bursts(c) && !packed_scheduled_symbols(c) &&
       c.receiver_strategy!=ReceiverStrategy::amplitude_only) {
        double correction=0;
        if(c.oscillator_search && search.admitted_clock_half_width_ppm &&
           *search.admitted_clock_half_width_ppm>oscillator_pattern_search(c).requested_clock_half_width_ppm)
            correction=utc_transmit_rate_limit(c);
        const auto refined=scheduled_complex_search(c,search,correction);
        const auto& g=refined.geometry;
        const bool zero_domain=search.admitted_frequency_half_width_hz && search.admitted_clock_half_width_ppm &&
            *search.admitted_frequency_half_width_hz==0 && *search.admitted_clock_half_width_ppm==0;
        const bool diagnostic=!c.oscillator_search && (zero_domain ||
            (!search.admitted_frequency_half_width_hz && !search.admitted_clock_half_width_ppm));
        const bool complete=refined.complete_coverage;
        const bool runnable=complete || (diagnostic && g.supported);
        const double width=search.qualified_start_window?
            static_cast<double>(2*search.qualified_start_window->half_width_samples/c.sample_rate):2*search.start_uncertainty_seconds;
        const auto& s=*c.scheduled_hopping;
        const auto spacing=static_cast<double>(chip)/(2*std::max<std::uint64_t>(1,g.receiver_timing_subdivisions)*c.sample_rate);
        const auto lanes=static_cast<long double>(g.retained_hypotheses);
        // The cheap geometry shares the constructor's exact origin/phase-cell
        // enumeration. Reject only a necessary fixed-state lower bound here;
        // the real constructor checks complete caches, optional detectors,
        // retention and prefixes against this same cohort workspace share.
        // This avoids an arbitrary 8 KiB-per-lane admission restriction.
        PatternScheduledAcquisitionPolicy implicit;
        if(planning) {
            // Planning NEVER injects a dummy runtime capture owner. The same
            // mandatory requirements are checked against a pure layout only.
            const auto projected=pattern_scheduled_acquisition_memory_projection(c,refined.search,g,workspace,
                planning->cohort_workspace,planning->maximum_consumers);
            implicit.requested=projected.requested;implicit.supported=projected.projected_fits;
            implicit.shared_capture=false; // No actual owner is bound here.
            implicit.per_bank_required_bytes=projected.per_bank_required_bytes;
            implicit.local_capture_bytes=projected.capture_fence_bytes;
            implicit.shared_capture_bytes=projected.shared_capture_bytes;
            implicit.optional_tile_bytes=projected.optional_tile_bytes;
            implicit.logical_lanes=projected.logical_lanes;implicit.batch_samples=projected.batch_samples;
            implicit.reason=projected.reason;
        } else implicit=pattern_scheduled_acquisition_policy(c,refined.search,g,workspace);
        const auto state=implicit.supported?static_cast<long double>(implicit.per_bank_required_bytes+implicit.local_capture_bytes):
            static_cast<long double>(g.workspace_lower_bound_bytes);
        // Once the complete mandatory eager layout requires implicit
        // storage, a failed implicit layout cannot fall back to the smaller
        // fit-only geometry bound and claim that eager construction fits.
        const bool fits=implicit.requested?implicit.supported && state<=workspace:state<workspace;
        if(runnable && !g.retained_hypotheses) {
            p.scheduled=p.experimental=true;p.supported=false;p.empty_scheduled_epoch=true;
            p.full_frequency_coverage=p.full_clock_coverage=complete;
            p.selected_processing_ratio=std::numeric_limits<double>::infinity();
            p.reason="no active split-symbol phase intersects the admitted arrival cells";
            return p;
        }
        if(runnable && fits) {
            p.scheduled=p.experimental=true;p.backend=ReceiverBackend::existing_complex;
            p.full_frequency_coverage=p.full_clock_coverage=complete;
            p.scheduled_frame_hops=s.time_multiplier;p.scheduled_active_hops=s.group_hops;
            p.scheduled_frame_seconds=.4*s.time_multiplier;
            p.scheduled_active_seconds=static_cast<double>(2ULL*c.sample_rate/5)*s.group_hops/c.sample_rate;
            p.scheduled_effective_multiplier=p.scheduled_frame_seconds/p.scheduled_active_seconds;
            p.scheduled_minimum_quiet_seconds=.4*(s.time_multiplier-s.group_hops-(s.time_multiplier-s.group_hops)/2);
            p.scheduled_flat_duty=p.scheduled_active_seconds/p.scheduled_frame_seconds;
            p.scheduled_recording_union_upper=1;
            p.scheduled_capture_frame_bytes_upper=p.scheduled_frame_seconds*c.sample_rate*sizeof(float);
            p.scheduled_interval_width_seconds=width;
            p.scheduled_canonical_phases=g.phase_positions;
            p.scheduled_timing_spacing_seconds=spacing;
            p.scheduled_phase_spacing_seconds=static_cast<double>(g.receiver_phase_step_samples)/c.sample_rate;
            p.scheduled_timing_positions=finite_ratio(lanes);
            p.scheduled_full_timing_positions=static_cast<double>(g.full_hypotheses);
            p.scheduled_fft_union_upper=std::min(1.,(p.scheduled_active_seconds+width)/p.scheduled_frame_seconds);
            p.scheduled_guaranteed_recovery_seconds=std::max(0.,p.scheduled_minimum_quiet_seconds-width);
            const auto banks=std::max<std::size_t>(1,search.receiver_bank_count);
            // Keep the complete raw mapped allowance outside the measured
            // crossover scope. The bounded pulse/curve path changes arithmetic
            // work, never the admitted lanes, capture union or trial count.
            const bool measured_reuse=c.search_arithmetic!=SearchArithmetic::fp64 && c.scramble &&
                c.dsss_factor==1 && pattern_pulse_enabled(c) && !c.fake_hopping &&
                c.sample_rate==6000 && chip==1500 && symbol==96000 && s.time_multiplier==10 && s.group_hops==1 &&
                search.qualified_start_window && width<=.2 && refined.search.hypotheses.size()<=5 &&
                state+1024*1024<workspace;
            // Full identical-PCM pairs observed about1.9--2.17x CPU reduction.
            // Use2x only for that local geometry; absolute work remains an
            // engineering estimate, not a realtime or sensitivity certificate.
            const auto reuse=measured_reuse?2.L:1.L;
            const auto cost=banks*(c.sample_rate*35e-9L+lanes*c.sample_rate*60e-9L*p.scheduled_fft_union_upper/reuse);
            p.scheduled_mean_processing_ratio=p.existing_processing_ratio=finite_ratio(cost);
            const auto available=p.scheduled_active_seconds+p.scheduled_guaranteed_recovery_seconds;
            p.scheduled_deadline_processing_ratio=finite_ratio(cost*p.scheduled_frame_seconds/std::max(.001,available));
            // Queued work may cross several cycles. The per-hop deadline is
            // a diagnostic stricter bound, not the sustainable CPU criterion.
            p.selected_processing_ratio=p.scheduled_mean_processing_ratio;
            p.estimated_workspace_bytes=static_cast<std::size_t>(state);
            p.reason=complete?
                "calendar-aware split complex; physical I/Q phase; refined physical-aperture bank; FP64 bulk arithmetic with eligible exact source/family reuse; bounded pulse/curve reuse bypassed by FP64 force; original fallback; total cost uncalibrated":
                "calendar-aware split complex; exact supplied diagnostic candidates; no continuous-domain coverage certificate; FP64 mapped fallback; total cost uncalibrated";
            if(implicit.supported) {
                p.scheduled_implicit_acquisition=true;p.scheduled_bank_required_bytes=implicit.per_bank_required_bytes;
                p.scheduled_local_capture_bytes=implicit.local_capture_bytes;p.scheduled_shared_capture_bytes=implicit.shared_capture_bytes;
                p.scheduled_optional_tile_bytes=implicit.optional_tile_bytes;
                p.scheduled_mean_processing_ratio=p.existing_processing_ratio=p.selected_processing_ratio=std::numeric_limits<double>::infinity();
                p.scheduled_deadline_processing_ratio=std::numeric_limits<double>::infinity();
                p.reason=implicit.reason+"; actual arithmetic/reuse/fallback counters are runtime evidence; CPU estimate unavailable";
            }
            if(std::isfinite(p.selected_processing_ratio) && p.selected_processing_ratio>p.cpu_budget)p.reason+="; estimated processing exceeds budget, no measured cheaper equivalent selected";
            if(measured_reuse)p.reason+="; historical matching local pulse/curve crossover included; new shared-family/RAW/affine execution is not separately discounted; absolute model remains engineering";
            return p;
        }
        if(c.receiver_strategy==ReceiverStrategy::complex_only || c.receiver_strategy==ReceiverStrategy::existing_only) {
            p.scheduled=p.experimental=true;p.supported=false;p.workspace_limited=runnable&&!fits;
            p.full_frequency_coverage=p.full_clock_coverage=complete;
            p.selected_processing_ratio=std::numeric_limits<double>::infinity();
            p.reason=runnable?"complete split complex state exceeds rolling workspace share":refined.reason;
            if(implicit.requested && !implicit.supported)p.reason+="; implicit acquisition: "+implicit.reason;
            return p;
        }
        // Automatic may consider the separately validated energy detector only
        // when complete complex geometry/grid/state cannot be represented.
        if(implicit.requested && !implicit.supported)scheduled_complex_failure=implicit.reason;
    }
    auto hypotheses=search.hypotheses;
    bool limited=false;
    if(!hypotheses.empty()) {
        // Explicit pairs are authoritative. A Config-only bank would omit UTC
        // steering or invent Cartesian alternatives not present in this bank.
        p.full_frequency_coverage=search.complex_frequency_coverage.value_or(!search.admitted_frequency_half_width_hz);
        p.full_clock_coverage=search.complex_clock_coverage.value_or(!search.admitted_clock_half_width_ppm);
    } else if(c.oscillator_search && search.frequency_offsets_hz.empty() &&
              search.clock_errors_ppm==std::vector<double>{0} && !search.couple_clock_to_carrier) {
        const auto bank=oscillator_pattern_search(c);
        hypotheses=bank.hypotheses;
        p.full_frequency_coverage=!bank.frequency.limited;
        p.full_clock_coverage=bank.clock_half_width_ppm>=bank.requested_clock_half_width_ppm;
    } else {
        auto offsets=search.frequency_offsets_hz;
        auto coupled=search.couple_clock_to_carrier;
        auto uncoupled=search.uncoupled_frequency_count;
        if(offsets.empty()) {
            if(search.expand_clock_search) {
                const auto bank=default_pattern_frequency_search(c);
                offsets=default_pattern_frequency_offsets(c);
                p.full_frequency_coverage=p.full_clock_coverage=!bank.limited;
            } else {
                const auto step=.25*c.sample_rate/static_cast<double>(symbol);
                offsets={0,-step,step,-2*step,2*step};
                const auto limit=pattern_frequency_offset_limit(c);
                std::erase_if(offsets,[&](double offset){return std::abs(offset)>limit;});
                p.full_frequency_coverage=p.full_clock_coverage=true;
            }
            coupled=offsets.size()>5 && c.spreading_mode==SpreadingMode::pattern &&
                search.clock_errors_ppm==std::vector<double>{0};
            if(coupled) {uncoupled=offsets.size();const auto copy=offsets;offsets.insert(offsets.end(),copy.begin(),copy.end());}
        } else p.full_frequency_coverage=p.full_clock_coverage=true;
        for(auto clock:search.clock_errors_ppm)for(std::size_t i=0;i<offsets.size();++i) {
            const auto scale=coupled&&i>=uncoupled?1+offsets[i]/c.carrier_hz:1.;
            const auto ppm=scale==1?clock:(scale*(1+clock*1e-6)-1)*1e6;
            hypotheses.push_back({offsets[i],std::clamp(ppm,-10000.,10000.)});
        }
    }
    double actual_frequency=0,actual_clock=0;
    for(const auto& h:hypotheses) {
        actual_frequency=std::max(actual_frequency,std::abs(h.frequency_offset_hz));
        actual_clock=std::max(actual_clock,std::abs(h.clock_error_ppm));
    }
    // Even supplied provenance cannot certify an endpoint that is absent.
    if(search.admitted_frequency_half_width_hz && actual_frequency<*search.admitted_frequency_half_width_hz)
        p.full_frequency_coverage=false;
    if(search.admitted_clock_half_width_ppm && actual_clock<*search.admitted_clock_half_width_ppm)
        p.full_clock_coverage=false;
    limited=!p.full_frequency_coverage||!p.full_clock_coverage;
    double maximum=0,minimum_rate=1;
    std::set<double> clocks;
    for(const auto& h:hypotheses){maximum=std::max(maximum,std::abs(h.frequency_offset_hz));minimum_rate=std::min(minimum_rate,1+h.clock_error_ppm*1e-6);clocks.insert(h.clock_error_ppm);}
    const auto bin=pattern_projection_bin_samples(c,maximum);
    const auto n=std::max<std::size_t>(4,static_cast<std::size_t>(std::ceil(static_cast<long double>(symbol)/bin/minimum_rate)));
    auto transform=pow2(2*n);
    if(seconds>=16) {const auto small=pow2(n+4);if(small-n+1>=std::max<std::size_t>(4,n/4))transform=std::min(transform,small);}
    const auto hop=seconds>=16?std::min(transform-n+1,std::max<std::size_t>(1,n/2)):transform-n+1;
    const auto hop_seconds=static_cast<double>(hop)*static_cast<double>(bin)/c.sample_rate;
    const auto banks=std::max<std::size_t>(1,search.receiver_bank_count);
    const auto workers=detail::search_concurrency(search.worker_threads);
    const bool scheduled=scheduled_bursts(c);
    const bool packed=scheduled && packed_scheduled_symbols(c);
    const auto phases=search.search_stream_phases?(packed?packed_scheduled_phase_count(c,c.stream_epoch):
        std::max<std::uint64_t>(1,(canonical_phase_extent(c)-1)/canonical_phase_step(c)+1)):1;
    if(packed && !phases) {
        p.scheduled=true;p.experimental=true;p.supported=false;p.empty_scheduled_epoch=true;
        p.selected_processing_ratio=std::numeric_limits<double>::infinity();
        p.reason="no active scheduled payload anchor intersects this representative key/epoch arrival bank";
        return p;
    }
    const auto initial=(c.scramble||c.dsss)?search.initial_stream_symbols:1;
    const auto cohorts=static_cast<long double>(phases)*initial;
    const auto correlator_groups=phases>1?(c.fake_hopping&&c.fake_hopping->matched_gaps?phases:
        symbol>=c.sample_rate?2U:3U):1U;
    // One established stream competes against the full original frequency
    // bank. Rounded aggregate fit from complete private 32 s/F1025 and
    // 256 s/F3565 captures; not multiplied by unaccepted key/epoch cohorts.
    const auto tracking=(c.scramble?0.5e-6L:0.25e-6L)*n*
        (hypotheses.size()+5.L*correlator_groups)/seconds;
    p.complex_tracking_processing_ratio=finite_ratio(tracking);
    // Provisional rounded engineering cost in seconds; complete execution
    // probes must calibrate these at the manual checkpoint. Prototypes alone do not
    // calibrate epoch construction, live scheduling or tracking throughput.
    const auto frontend=static_cast<long double>(c.sample_rate)*35e-9L;
    const auto fft_cost=2.0e-9L*transform*std::log2(static_cast<double>(transform));
    const auto rows=static_cast<long double>(n)*(c.scramble?160e-9L:65e-9L);
    auto old=(frontend+cohorts*hypotheses.size()*(16*fft_cost+rows)/std::max(.001,hop_seconds)/workers)*banks;
    // The preceding long-private path is a streamed pulse correlator, not an
    // imaginary whole FFT. Include its timing/clock lanes and changed pulse
    // coefficients; this remains an engineering reference, not a measured cap.
    if(search.compact_clock_search && search.start_offset_seconds) {
        const auto width=search.qualified_start_window?2*search.qualified_start_window->half_width_samples/c.sample_rate:2*search.start_uncertainty_seconds;
        const auto starts=std::ceil(width*c.sample_rate/(chip/2.L))+1;
        const auto lanes=correlator_groups*starts*hypotheses.size();
        old=banks*(frontend+lanes*(c.sample_rate/static_cast<long double>(chip))*12e-6L/workers);
    }
    old+=tracking;
    p.existing_processing_ratio=finite_ratio(old);p.selected_processing_ratio=p.existing_processing_ratio;
    p.reason="existing complex search retained";
    const auto core=static_cast<long double>(transform)*16*5+(4.L*n+2.L*hop)*16+
        2.L*n*16+(transform+1.L)*8+hypotheses.size()*512.L+512*1024;
    const auto axis_starts=search.qualified_start_window?std::min<std::size_t>(hop,
        static_cast<std::size_t>(std::ceil(2*search.qualified_start_window->half_width_samples/bin))+6):hop;
    const auto score_bytes=static_cast<long double>(axis_starts)*hypotheses.size()*sizeof(detail::FftSearchScore);
    bool axis=!(c.scheduled_hopping && c.scheduled_hopping->enabled() && !c.scheduled_hopping->continuous()) && pattern_pulse_enabled(c) && !c.dsss && c.dsss_factor<=1 && !c.fake_hopping &&
        bin>1 && detail::drift_section_count(c,search.drift_tolerant)==4 &&
        !detail::differential_window_samples(symbol,chip,c.sample_rate,search.differential_window_seconds) &&
        n<=65536 && clocks.size()<=3 && hypotheses.size()>=128 &&
        score_bytes<=64.L*1024*1024 && core+score_bytes+8.L*pow2(n+hypotheses.size())*16<workspace;
    detail::frequency_axis::Requirements axis_requirements;
    auto axis_tracking=tracking;
    if(axis) {
        detail::FftSearchGeometry g;
        g.pattern={c.stream_epoch,chip,pattern_chips_per_symbol(c),symbol,c.sample_rate,
            static_cast<std::uint32_t>(c.spreading_mode),1U,c.scramble?1U:0U,0U,c.spreading_seed,c.dsss_seed,1};
        g.bins_per_symbol=n;g.bin_samples=bin;g.carrier_hz=c.carrier_hz;g.evidence_count=static_cast<double>(n);
        g.drift_sections=4;g.search_arithmetic=static_cast<std::uint32_t>(SearchArithmetic::fp64);
        std::vector<detail::FftSearchJob> jobs; jobs.reserve(hypotheses.size());
        for(std::size_t i=0;i<hypotheses.size();++i) {
            jobs.push_back({0,0,i,hypotheses[i].frequency_offset_hz});
            jobs.back().clock_ratio=1+hypotheses[i].clock_error_ppm*1e-6;
        }
        axis_requirements=detail::frequency_axis::choose_geometry(g,jobs,axis_starts,axis_starts,
            workspace-static_cast<std::size_t>(core),workers);
        axis=axis_requirements.supported();
        if(axis) {
            // Continuation uses the same operator for one start. Its stricter
            // crossover may retain direct scoring even when acquisition uses
            // CZT. Partial observations are no larger than this full-symbol
            // allowance; original local timing fits and winner refinement stay.
            const auto continuation=detail::frequency_axis::choose_geometry(g,jobs,1,1,
                workspace-static_cast<std::size_t>(core),1);
            if(continuation.supported()) {
                const auto length=pow2(n);
                const auto units=continuation.relative_work*4.L*hypotheses.size()*
                    length*std::log2(static_cast<double>(length));
                axis_tracking=(0.73e-6L*n*clocks.size()+2.4e-9L*4*units+
                    (c.scramble?0.5e-6L:0.25e-6L)*n*(5.L*correlator_groups+1))/seconds;
            }
        }
    }
    if(axis) {
        // Aggregate fit to complete production kernel execution (private/public,
        // N~2049/F3565, 16 and 1024 starts, three isolated pairs per geometry).
        // relative_work encodes the actual quarter/clock CZT lengths; multiplying
        // a whole-bank length by clock count would charge the wrong transforms.
        const auto timing_fft=pow2(n+axis_starts-1);
        const auto transform_units=axis_requirements.relative_work*4.L*hypotheses.size()*
            timing_fft*std::log2(static_cast<double>(timing_fft))/axis_starts;
        const auto setup=0.73e-6L*n*clocks.size();
        const auto execution=2.4e-9L*4*axis_starts*transform_units/axis_requirements.workers;
        const auto cost=banks*(frontend+cohorts*(setup+execution)/std::max(.001,hop_seconds))+axis_tracking;
        p.frequency_axis_processing_ratio=finite_ratio(cost);
        if(c.receiver_strategy!=ReceiverStrategy::existing_only && cost<old*.8L) {
            p.backend=ReceiverBackend::frequency_axis;p.selected_processing_ratio=p.frequency_axis_processing_ratio;
            p.complex_tracking_processing_ratio=finite_ratio(axis_tracking);
            p.reason="exact clock rows shared across carrier hypotheses; measured crossover checked again at execution";
            p.estimated_workspace_bytes=static_cast<std::size_t>(core)+axis_requirements.score_bytes+axis_requirements.scratch_bytes;
        }
    }
    p.scheduled=scheduled;
    if(scheduled) {
        const auto& schedule=*c.scheduled_hopping;schedule.validate();
        p.scheduled_frame_hops=schedule.time_multiplier;p.scheduled_active_hops=schedule.group_hops;
        p.scheduled_frame_seconds=.4*schedule.time_multiplier;
        p.scheduled_active_seconds=static_cast<double>(2ULL*c.sample_rate/5)*schedule.group_hops/c.sample_rate;
        p.scheduled_effective_multiplier=p.scheduled_frame_seconds/p.scheduled_active_seconds;
        p.scheduled_minimum_quiet_seconds=.4*(schedule.time_multiplier-schedule.group_hops-
            (schedule.time_multiplier-schedule.group_hops)/2);
        // Recording cannot assume the current qualified bank is the complete
        // future-admission union. Live's ahead certificate also includes broad
        // constructor/timestamp/rate envelopes and may retain every callback.
        // Without that actual certificate, full recording is the safe bound.
        p.scheduled_recording_union_upper=1;
        p.scheduled_capture_frame_bytes_upper=p.scheduled_recording_union_upper*p.scheduled_frame_seconds*
            c.sample_rate*sizeof(float);
        const auto guard=2*(schedule.relative_clock_region_seconds()+schedule.ramp_seconds)+schedule.audio_error_seconds;
        p.scheduled_flat_duty=schedule.continuous()?1.:
            std::max(0.,p.scheduled_active_seconds-guard)/p.scheduled_frame_seconds;
        if(c.fake_hopping) {
            const auto& fhss=*c.fake_hopping;
            const auto full=std::max(0.,fhss.dwell_seconds-
                2*(fhss.power_clock_region_seconds()+fhss.ramp_seconds)-fhss.audio_error_seconds);
            p.scheduled_flat_duty=std::min(p.scheduled_flat_duty,
                p.scheduled_active_seconds/p.scheduled_frame_seconds*full/fhss.dwell_seconds);
        }
        // These fields describe source geometry and conservative capture union.
        // A duty ratio alone does not calibrate DSSS weight construction or the
        // rolling key/epoch lifecycle; those costs require complete execution.
    }
    if(packed && c.receiver_strategy!=ReceiverStrategy::amplitude_only && !limited) {
        // Every complete packed symbol and its finite pulse support lies inside
        // a full-gain slot. It uses the original complex statistic, with exact
        // keyed slot times and ordinary local waveform clock scaling. Null and
        // waiting time is not a failed symbol. Do not force short symbols through
        // the spectral-energy detector's independently necessary 64-block gate.
        p.backend=ReceiverBackend::existing_complex;p.experimental=true;
        p.estimated_workspace_bytes=static_cast<std::size_t>(std::min<long double>(core,
            std::numeric_limits<std::size_t>::max()));
        if(core>workspace) {
            p.supported=false;p.workspace_limited=true;
            p.selected_processing_ratio=std::numeric_limits<double>::infinity();
            p.reason="packed scheduled complex FFT core exceeds the configured rolling workspace share";
            return p;
        }
        p.scheduled_canonical_phases=static_cast<std::size_t>(phases);
        p.scheduled_phase_spacing_seconds=0; // exact singleton slot phases
        p.scheduled_timing_spacing_seconds=static_cast<double>(bin)/c.sample_rate;
        const auto width=search.qualified_start_window?
            2*search.qualified_start_window->half_width_samples/c.sample_rate:2*search.start_uncertainty_seconds;
        p.scheduled_interval_width_seconds=finite_ratio(width);
        // Bounded engineering position allowance, not a measured acquisition
        // count. Runtime tracing records actually scored FFT alternatives.
        p.scheduled_timing_positions=finite_ratio(cohorts*hypotheses.size()*(std::ceil(width*c.sample_rate/bin)+1));
        p.scheduled_full_timing_positions=finite_ratio(cohorts*hypotheses.size()*
            (std::ceil(2*search.start_uncertainty_seconds*c.sample_rate/bin)+1));
        p.scheduled_fft_seconds=static_cast<double>(transform)*static_cast<double>(bin)/c.sample_rate;
        p.scheduled_fft_union_upper=1;
        p.scheduled_guaranteed_recovery_seconds=finite_ratio(std::max(0.L,p.scheduled_minimum_quiet_seconds-width));
        // Keep the full-input/FFT engineering cost until complete rolling-bank
        // measurements justify a geometry-specific reduction. Nominal duty
        // alone does not certify capture omission or a processing deadline.
        p.scheduled_mean_processing_ratio=p.existing_processing_ratio;
        p.scheduled_deadline_processing_ratio=p.existing_processing_ratio;
        p.selected_processing_ratio=p.existing_processing_ratio;
        p.reason="calendar-aware packed complex FFT; exact keyed singleton starts; full frequency/clock coverage; total cost uncalibrated";
        if(c.receiver_strategy==ReceiverStrategy::existing_only)p.reason+="; alternative decoders disabled";
        if(std::isfinite(p.selected_processing_ratio) && p.selected_processing_ratio>p.cpu_budget)
        p.reason+="; estimated processing still exceeds selected CPU budget";
        return p;
    }
    if(scheduled && (c.receiver_strategy==ReceiverStrategy::complex_only ||
                     c.receiver_strategy==ReceiverStrategy::existing_only)) {
        // Policy rejection precedes geometry and key/epoch eligibility. A null
        // representative epoch or an unsupported amplitude factory must not
        // disguise a disabled decoder as a workspace or arrival-bank problem.
        p.backend=ReceiverBackend::existing_complex;p.supported=false;p.experimental=true;
        p.selected_processing_ratio=std::numeric_limits<double>::infinity();
        p.reason=packed?"packed scheduled complex search does not cover the complete declared frequency/clock bank":
            "scheduled split complex geometry/grid is unsupported; selected policy prohibits amplitude fallback";
        return p;
    }
    const auto amplitude=pattern_amplitude_geometry(c,search);
    if(amplitude.supported) {
        p.scheduled_fft_seconds=scheduled?amplitude.block_seconds:0;
        p.amplitude_frequency_half_width_hz=amplitude.admitted_frequency_half_width_hz;
        p.amplitude_clock_half_width_ppm=amplitude.clock_half_width_ppm;
        const auto width=search.qualified_start_window?2*search.qualified_start_window->half_width_samples/c.sample_rate:2*search.start_uncertainty_seconds;
        const auto starts=std::ceil(width*c.sample_rate/(chip/2.L))+1;
        const auto full_starts=std::ceil(2*search.start_uncertainty_seconds*c.sample_rate/(chip/2.L))+1;
        long double lanes=correlator_groups*starts*amplitude.clock_hypotheses;
        long double reserved_lanes=correlator_groups*std::max(starts,full_starts)*amplitude.clock_hypotheses;
        if(scheduled) {
            const auto lifetime_source=symbol_schedule_detail::add(symbol,
                symbol_schedule_detail::add(training_sample_count(c),pattern_pulse_padding_samples(c)));
            const auto lifetime_wall=static_cast<long double>(scheduled_physical_span_samples(c,lifetime_source))/c.sample_rate;
            const auto clock_radius=(amplitude.clock_hypotheses>1?amplitude.clock_step_ppm/2:
                amplitude.clock_half_width_ppm)*1e-6L;
            // Bound the actual complete-FFT eligibility interval over one
            // unconfirmed cohort lifetime: prior, canonical phase cell,
            // half-chip timing cell and between-rate-cell displacement. An
            // established longer stream can add age and needs live counters.
            p.scheduled_timing_spacing_seconds=static_cast<double>(chip)/
                (2.*amplitude.receiver_timing_subdivisions*c.sample_rate);
            p.scheduled_phase_spacing_seconds=static_cast<double>(amplitude.receiver_phase_step_samples)/c.sample_rate;
            const auto phase_cell=search.search_stream_phases?amplitude.receiver_phase_step_samples-1.L:0.L;
            const auto timing_cell=search.start_uncertainty_seconds>0?
                chip/(2.L*amplitude.receiver_timing_subdivisions*c.sample_rate):0.L;
            const auto interval_width=static_cast<long double>(width)+phase_cell/c.sample_rate+
                timing_cell+2*clock_radius*lifetime_wall;
            p.scheduled_interval_width_seconds=finite_ratio(interval_width);
            p.scheduled_fft_union_upper=finite_ratio(std::min(1.L,
                std::max<std::size_t>(1,search.receiver_schedule_key_count)*
                (p.scheduled_active_seconds+interval_width)/p.scheduled_frame_seconds));
            // A split warp requires independent canonical phase state. Count
            // original coverage cells, including fractional phase cells, rather
            // than multiplying a narrow width by the entire phase bank.
            const auto phase_step=amplitude.receiver_phase_step_samples;
            const auto phase_lower=search.search_stream_phases&&(c.scramble||c.dsss)?0:c.stream_phase_samples;
            const auto phase_upper=search.search_stream_phases&&(c.scramble||c.dsss)?
                (canonical_phase_extent(c)-1)/phase_step*phase_step:c.stream_phase_samples;
            auto prior=search.qualified_start_window;
            if(prior&&search.search_stream_phases) {
                const auto cell=phase_step-1;
                prior->epoch_origin_samples+=cell*prior->phase_scale/2;
                prior->half_width_samples+=cell*prior->phase_scale/2;
            }
            const auto lower=(static_cast<long double>(*search.start_offset_seconds)-search.start_uncertainty_seconds)*c.sample_rate;
            const auto upper=(static_cast<long double>(*search.start_offset_seconds)+search.start_uncertainty_seconds)*c.sample_rate;
            // Match the runtime preflight's key-specific active canonical
            // intervals. Impossible null payload anchors are not candidates,
            // even in the full timing fallback. Count them before allocation,
            // rather than rejecting a valid bank with an inverse-duty excess.
            const SchedHopSourceMap source_map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
            const auto epoch_sample=symbol_schedule_detail::multiply(c.stream_epoch,c.sample_rate);
            std::array<std::pair<std::uint64_t,std::uint64_t>,8> active_phases{};
            std::size_t active_count=0;
            auto cursor=SchedHopCoordinate{epoch_sample,0}.add_samples(phase_lower);
            const auto last=SchedHopCoordinate{epoch_sample,0}.add_samples(phase_upper);
            while(!packed&&cursor.distance(last)<=0) {
                const auto group=source_map.locate(cursor);
                if(!group.active) {
                    const auto next=source_map.source_to_physical(group.source,false);
                    if(next.distance(cursor)<=0)throw Error("scheduled model phase inverse did not advance");
                    cursor=next;continue;
                }
                const auto low=std::max(phase_lower,group.physical_begin>epoch_sample?group.physical_begin-epoch_sample:0);
                const auto high=std::min(phase_upper,group.physical_end-1-epoch_sample);
                const auto first=phase_lower+(low-phase_lower+phase_step-1)/phase_step*phase_step;
                const auto end=phase_lower+(high-phase_lower)/phase_step*phase_step;
                if(first<=end) {
                    if(active_count==active_phases.size())throw Error("scheduled model active phase union exceeds bounded capacity");
                    active_phases[active_count++]={first,end};
                }
                cursor={group.physical_end,0};
            }
            lanes=0;p.scheduled_full_timing_positions=0;
            bool bounded=true;
            const auto count_phases=[&](std::uint64_t first,std::uint64_t last) {
                if(!bounded)return;
                p.scheduled_canonical_phases+=(last-first)/phase_step+1;
                for(std::size_t i=0;i<amplitude.clock_hypotheses;++i) {
                    const auto half=(i+1)/2;
                    // Match the constructor's double-valued ppm lanes before
                    // lifting rate/coordinates into their wider arithmetic.
                    const auto ppm=i?static_cast<double>(half)*amplitude.clock_step_ppm*(i%2?-1:1):0.;
                    const auto rate=1+ppm*1e-6L;
                    detail::PatternStartLattice lattice(lower,upper,chip/(2*rate*amplitude.receiver_timing_subdivisions),first,last,phase_step,prior);
                    const auto range=lattice.retained_index_range();
                    if(prior&&range.second-range.first>1000000) {bounded=false;return;}
                    lanes+=lattice.retained_phase_count();
                    p.scheduled_full_timing_positions+=lattice.full_phase_count();
                }
            };
            if(packed) {
                // Packed payloads begin only at exact full-power slots. Their
                // guard-offset phases span the UTC second and are singleton
                // cells; neither Fs samples nor the old min(T,Fs) grid is the
                // correct bank. Share the transmitter/receiver enumerator.
                for_each_packed_scheduled_phase(c,c.stream_epoch,[&](std::uint64_t phase) {
                    if(phase>=phase_lower&&phase<=phase_upper)count_phases(phase,phase);
                });
            } else for(std::size_t a=0;a<active_count;++a)
                count_phases(active_phases[a].first,active_phases[a].second);
            if(!bounded) {
                p.backend=ReceiverBackend::amplitude_energy;p.supported=false;p.experimental=true;
                p.selected_processing_ratio=std::numeric_limits<double>::infinity();
                p.reason="scheduled arrival/phase model exceeds bounded geometry enumeration; execution coverage is not estimated";
                return p;
            }
            p.scheduled_timing_positions=finite_ratio(lanes);
            reserved_lanes=lanes;
            if(!lanes) {
                p.backend=ReceiverBackend::amplitude_energy;p.supported=false;p.experimental=true;
                p.empty_scheduled_epoch=true;
                p.selected_processing_ratio=std::numeric_limits<double>::infinity();
                p.reason="no active scheduled payload anchor intersects this representative key/epoch arrival bank";
                return p;
            }
        }
        // Keep detector band partitioning and its trial count independent of
        // storage optimization. This is the preceding unfactored admission
        // allowance; production passes its count into the correlator.
        const auto base_bytes=192*1024.L+amplitude.fft_samples*36.L+
            (amplitude.bin_count+1.L)*sizeof(long double)+reserved_lanes*4096.L;
        const auto per_channel=reserved_lanes*sizeof(std::array<detail::EnvelopeFit,2>)+sizeof(PatternAmplitudeChannel);
        const auto fit=workspace>base_bytes?std::floor((workspace-base_bytes)/per_channel):0.L;
        p.amplitude_channels=static_cast<std::size_t>(std::min(static_cast<long double>(amplitude.channel_count),fit));
        const auto amplitude_bytes=base_bytes+reserved_lanes*sizeof(detail::EnvelopeTemplateFit)+
            p.amplitude_channels*(reserved_lanes*sizeof(detail::EnvelopeChannelFit)+sizeof(PatternAmplitudeChannel));
        // Conservative per-bank frontend pricing: Live may share FFT/prefix
        // blocks, but transfer or mixed geometry can exhaust that cache. The
        // independently keyed weights are paid once per lane, moments per band.
        // Amplitude push is currently serial; hardware thread count must not
        // divide its elapsed cost merely because complex search has workers.
        // Rounded local complete-execution engineering allowance (October12).
        // Native FP32 FFT and packed-envelope gains are priced separately.
        // Original pulse geometry is paid per lane, wide factored moments per band.
        // Keep the setup term and conservative per-bank preprocessing: optional
        // shared spectra can miss/exhaust and do not remove rolling epoch work.
        const bool double_fft=c.search_arithmetic==SearchArithmetic::fp64;
        const auto amplitude_frontend=(double_fft?1.5e-9L:1.0e-9L)*c.sample_rate*
            std::log2(static_cast<double>(amplitude.fft_samples));
        const auto lifetime=seconds+static_cast<double>(training_sample_count(c)+pattern_pulse_padding_samples(c))/
            c.sample_rate+2*search.start_uncertainty_seconds+3;
        const auto setup=(double_fft?4e-9L:20e-9L)*amplitude.fft_samples/std::max(1.,lifetime);
        const auto duty=scheduled?p.scheduled_flat_duty:1.;
        const auto fft_union=scheduled?p.scheduled_fft_union_upper:1.;
        // Raw capture/validation remains full-rate when a future constructor
        // prevents capture omission. This is separate from omitted per-bank
        // FFTs and paid once for this profile's conservative work allowance.
        const auto input_floor=scheduled?frontend*p.scheduled_recording_union_upper:0.L;
        const auto cost=input_floor+banks*(amplitude_frontend*fft_union+setup+
            lanes*(c.sample_rate/static_cast<long double>(amplitude.fft_samples))*duty*
            ((double_fft?2.5e-6L:2.35e-6L)+0.08e-6L*p.amplitude_channels));
        p.amplitude_processing_ratio=finite_ratio(cost);
        if(scheduled) {
            // Adjacent keyed choices can place the next active group before
            // its mean frame spacing. Price the guaranteed drain interval as
            // well as mean duty. Independent schedules and arrival uncertainty
            // consume that quiet opportunity; no hypothesis is removed here.
            const auto key_count=std::max<std::size_t>(1,search.receiver_schedule_key_count);
            const auto admitted=static_cast<long double>(p.scheduled_interval_width_seconds);
            const auto active=static_cast<long double>(p.scheduled_active_seconds);
            p.scheduled_guaranteed_recovery_seconds=finite_ratio(std::max(0.L,p.scheduled_minimum_quiet_seconds-admitted-
                (key_count-1)*(active+admitted)));
            const auto deadline=active+p.scheduled_guaranteed_recovery_seconds;
            const auto fft_interval=std::min(1.L,key_count*(active+admitted)/deadline);
            const auto flat=p.scheduled_flat_duty*p.scheduled_frame_seconds/deadline;
            const auto deadline_cost=input_floor+banks*(amplitude_frontend*fft_interval+setup+
                lanes*(c.sample_rate/static_cast<long double>(amplitude.fft_samples))*flat*
                ((double_fft?2.5e-6L:2.35e-6L)+0.08e-6L*p.amplitude_channels));
            p.scheduled_mean_processing_ratio=finite_ratio(cost);
            p.scheduled_deadline_processing_ratio=finite_ratio(deadline_cost);
            // The bounded FIFO may carry work across active groups. Keep the
            // worst per-hop ratio visible, but select by sustainable throughput.
            p.amplitude_processing_ratio=p.scheduled_mean_processing_ratio;
        }
        if(p.amplitude_channels && amplitude_bytes<=workspace && ((scheduled && c.receiver_strategy==ReceiverStrategy::automatic) ||
           c.receiver_strategy==ReceiverStrategy::amplitude_only ||
           (c.receiver_strategy==ReceiverStrategy::automatic &&
            (limited || p.selected_processing_ratio>p.cpu_budget) && cost<p.selected_processing_ratio))) {
            p.backend=ReceiverBackend::amplitude_energy;p.experimental=true;
            p.full_frequency_coverage=p.full_clock_coverage=true;
            p.selected_processing_ratio=p.amplitude_processing_ratio;
            p.reason="phase-insensitive energy in "+std::to_string(p.amplitude_channels)+" complete-coverage coarse bands; sensitivity and interference remain experimental";
            if(scheduled) {
                p.reason+="; keyed split-symbol calendar, unchanged arrival/clock candidates; guaranteed drain-window cost included; split-DSSS/setup cost uncalibrated";
                if(p.scheduled_guaranteed_recovery_seconds==0)p.reason+="; admitted capture union leaves no guaranteed quiet recovery interval";
            }
            if(p.amplitude_channels<amplitude.channel_count)p.reason+="; bands widened for workspace";
            p.estimated_workspace_bytes=static_cast<std::size_t>(amplitude_bytes);
        } else if(scheduled || c.receiver_strategy==ReceiverStrategy::amplitude_only) {
            p.backend=ReceiverBackend::amplitude_energy;p.supported=false;p.experimental=true;p.workspace_limited=true;
            p.estimated_workspace_bytes=static_cast<std::size_t>(base_bytes+per_channel);
            p.reason="complete rolling amplitude arrival/clock/phase bank exceeds workspace";
        }
    } else if(scheduled || c.receiver_strategy==ReceiverStrategy::amplitude_only) {
        p.backend=ReceiverBackend::amplitude_energy;p.supported=false;p.experimental=true;
        p.reason=amplitude.reason;p.selected_processing_ratio=std::numeric_limits<double>::infinity();
        p.full_frequency_coverage=p.full_clock_coverage=false;
    }
    if(c.receiver_strategy==ReceiverStrategy::existing_only)
        p.reason="new decoders disabled; original complex reference";
    if(std::isfinite(p.selected_processing_ratio) && p.selected_processing_ratio>p.cpu_budget)
        p.reason+="; estimated processing still exceeds selected CPU budget";
    if(!p.supported && !scheduled_complex_failure.empty())
        p.reason+="; complex implicit acquisition: "+scheduled_complex_failure;
    return p;
}
} // namespace
ReceiverStrategyPlan resolve_receiver_strategy(const Config& c,const PatternSearch& s,std::size_t workspace) {
    return resolve_receiver_strategy_impl(c,s,workspace,nullptr);
}
ReceiverStrategyPlan plan_receiver_strategy(const Config& c,const PatternSearch& source,std::size_t workspace,
        std::size_t cohort_workspace,std::size_t consumers,std::size_t receiver_control_margin) {
    if(!detail::scheduled_acquisition_policy::requested() || !scheduled_bursts(c) || packed_scheduled_symbols(c) ||
       source.scheduled_capture || !c.data_key ||
       detail::drift_section_count(c,source.drift_tolerant)!=1 ||
       symbol_sample_count(c)<6ULL*c.sample_rate)return resolve_receiver_strategy(c,source,workspace);
    try {
        const auto layout=detail::scheduled_capture::SharedScheduledCapture::cohort_layout(cohort_workspace,consumers);
        auto search=source;
        const auto available=cohort_workspace-layout.reserved_bytes;
        const auto share=available/std::max<std::size_t>(1,search.receiver_bank_count);
        const auto projected_share=share>receiver_control_margin?share-receiver_control_margin:1;
        // A previously explicit hard cap is not enlarged by planning. Callers
        // replace obsolete guessed cohort shares before entering this API.
        search.receiver_strategy_workspace_bytes=source.receiver_strategy_workspace_bytes?
            std::min(source.receiver_strategy_workspace_bytes,projected_share):projected_share;
        const PlanningCaptureContext planning{cohort_workspace,consumers};
        return resolve_receiver_strategy_impl(c,search,workspace,&planning);
    } catch(const detail::scheduled_capture::Error& error) {
        ReceiverStrategyPlan out;out.supported=false;out.workspace_limited=true;
        out.backend=ReceiverBackend::existing_complex;out.scheduled=true;out.experimental=true;
        out.selected_processing_ratio=std::numeric_limits<double>::infinity();
        out.reason=std::string("Planning capture layout unavailable: ")+error.what();return out;
    }
}
} // namespace datapump::modem

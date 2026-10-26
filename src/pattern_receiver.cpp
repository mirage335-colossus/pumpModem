#include <openssl/crypto.h>
#include "datapump/pattern_receiver.hpp"
#include "native_peak_workspace.hpp"
#include "native_point_workspace.hpp"
#include "pattern_start_geometry.hpp"
#include "native_diagnostic_history.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/scheduled_complex_search.hpp"
#include "datapump/symbol_burst.hpp"
#include "datapump/receiver_strategy.hpp"
#include "datapump/symbol_schedule.hpp"
#include "search_parallel.hpp"
#include "pattern_fft_batch.hpp"
#include "pattern_fft_window.hpp"
#include "pattern_observation_layout.hpp"
#include "pattern_correlator_batch.hpp"
#include "pattern_drift.hpp"
#include "audio_trace.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cfenv>
#include <limits>
#include <numbers>
#include <numeric>
#include <new>

namespace datapump::modem {
namespace {
using Complex = std::complex<double>;
constexpr double tau = 2 * std::numbers::pi;
void cancelled(std::stop_token stop) { if(stop.stop_requested()) throw Error("pattern search cancelled"); }
using detail::pattern_fft;
using detail::pattern_evidence;
std::vector<double> local_frequency_offsets(const Config& config) {
    const auto step=.25*config.sample_rate/static_cast<double>(symbol_sample_count(config));
    std::vector<double> result{0,-step,step,-2*step,2*step};
    const auto limit=pattern_frequency_offset_limit(config);
    std::erase_if(result,[&](double offset){return std::abs(offset)>limit;});
    return result;
}
}
bool pattern_source_identities_disjoint(const PatternSourceIdentity& a,std::uint64_t first_a,
        std::uint64_t end_a,std::uint64_t symbol_a,const PatternSourceIdentity& b,std::uint64_t first_b,
        std::uint64_t end_b,std::uint64_t symbol_b)noexcept {
    if(!symbol_a || symbol_a!=symbol_b || !a.sample_rate || a.sample_rate!=b.sample_rate ||
       first_a>=end_a || first_b>=end_b)return false;
    const auto first=std::max(first_a,first_b),end=std::min(end_a,end_b);
    if(first>=end)return false;
    const auto valid=[](const PatternSourceIdentity& id) {
        return id.phase_lower<=id.phase_upper && id.phase_upper<id.sample_rate &&
            std::isfinite(id.clock_half_width_ppm) && id.clock_half_width_ppm>=0 && id.clock_half_width_ppm<1e6;
    };
    if(!valid(a) || !valid(b))return false;
    const auto epoch_delta=b.epoch>=a.epoch?static_cast<long double>(b.epoch-a.epoch):
        -static_cast<long double>(a.epoch-b.epoch);
    const auto shift=epoch_delta*a.sample_rate;
    const auto bounds=[&](const PatternSourceIdentity& id,std::uint64_t anchor,std::uint64_t at,
                          long double epoch_shift,std::array<long double,2>& out) {
        const auto base=static_cast<long double>(id.first_symbol)*symbol_a;
        const auto distance=static_cast<long double>(at-anchor);
        const auto radius=static_cast<long double>(id.clock_half_width_ppm)*1e-6L;
        const auto lower=base+id.phase_lower+distance*(1-radius);
        const auto upper=base+id.phase_upper+distance*(1+radius);
        // Two whole symbols cover the original symbol-cell ambiguity and
        // the sub-symbol refinement at a drainable chunk's physical anchor.
        const auto guard=2.L*symbol_a;
        const auto magnitude=std::max({1.L,std::abs(base),std::abs(shift),std::abs(upper),distance});
        if(!std::isfinite(lower) || !std::isfinite(upper) || lower<0 || magnitude>=1e15L)return false;
        const auto seconds=std::ceil((upper+guard)/id.sample_rate);
        if(!std::isfinite(seconds) || seconds<0 || seconds>=1e15L ||
           static_cast<std::uint64_t>(seconds)>UINT64_MAX-id.epoch)return false;
        const auto error=256*std::numeric_limits<double>::epsilon()*magnitude;
        out={epoch_shift+std::max(0.L,lower-guard)-error,epoch_shift+upper+guard+error};
        return std::isfinite(out[0]) && std::isfinite(out[1]);
    };
    int order=0;
    for(const auto at:{first,end}) {
        std::array<long double,2> x{},y{};
        if(!bounds(a,first_a,at,0,x) || !bounds(b,first_b,at,shift,y))return false;
        const int current=x[1]+symbol_a<y[0]?-1:y[1]+symbol_a<x[0]?1:0;
        if(!current || (order && current!=order))return false;
        order=current;
    }
    // Every endpoint product of the two affine rate cones has the same
    // strict order throughout the overlap. A source ordinal cell is at most
    // one symbol wide, including across second/epoch boundaries.
    return true;
}
bool PatternPhaseCurve::configure(std::uint64_t id,std::uint32_t rate,double baseline,
        std::uint64_t guard,std::size_t maximum_bin)noexcept {
    if(view_id_ || !id || !rate || !std::isfinite(baseline) || !maximum_bin || maximum_bin>UINT32_MAX)return false;
    view_id_=id;sample_rate_=rate;baseline_=baseline;input_guard_=guard;
    maximum_bin_samples_=static_cast<std::uint32_t>(maximum_bin);return true;
}
double PatternPhaseCurve::value_at(long double sample)const noexcept {
    if(!size_ || sample<segments_[0].first_sample)return 0;
    std::size_t selected=0;
    while(selected+1<size_ && sample>=segments_[selected+1].first_sample)++selected;
    const auto& segment=segments_[selected];
    return static_cast<double>(static_cast<long double>(segment.radians)+
        (sample-segment.first_sample)*segment.radians_per_sample);
}
bool PatternPhaseCurve::requalify(std::uint64_t evidence,std::uint64_t until)noexcept {
    if(!view_id_ || !size_ || evidence<latest_evidence_ || until<=evidence)return false;
    latest_evidence_=evidence;qualified_until_=until;return true;
}
bool PatternPhaseCurve::append(std::span<const PatternPhaseSegment> additions,std::uint64_t evidence,
        std::uint64_t until)noexcept {
    if(!view_id_ || additions.empty() || additions.size()>2 || additions.size()>available_segments() ||
       evidence<latest_evidence_ || until<=evidence || input_guard_>UINT64_MAX-maximum_bin_samples_ ||
       std::max(evidence,observed_until_)>UINT64_MAX-input_guard_-maximum_bin_samples_)return false;
    const auto first_allowed=std::max(evidence,observed_until_)+input_guard_+maximum_bin_samples_;
    const PatternPhaseSegment* previous=size_?&segments_[size_-1]:nullptr;
    for(const auto& segment:additions) {
        if(segment.first_sample<=first_allowed || segment.first_sample>until ||
           !std::isfinite(segment.radians) || std::abs(segment.radians)>1e12 ||
           !std::isfinite(segment.radians_per_sample) || std::abs(segment.radians_per_sample)>1e6 ||
           (previous && segment.first_sample<=previous->first_sample))return false;
        const auto expected=previous?static_cast<long double>(previous->radians)+
            (static_cast<long double>(segment.first_sample)-previous->first_sample)*previous->radians_per_sample:0.L;
        const auto tolerance=128*std::numeric_limits<double>::epsilon()*std::max(1.L,std::abs(expected));
        if(!std::isfinite(expected) || std::abs(segment.radians-expected)>tolerance)return false;
        previous=&segment;
    }
    for(const auto& segment:additions)segments_[size_++]=segment;
    latest_evidence_=evidence;qualified_until_=until;return true;
}
struct PatternReceiver::Impl {
    Config config;
    PatternSearch search;
    bool fp32_exact_fallback=false;
    std::size_t native_active_bytes=0;
    std::unique_ptr<detail::search_fft::FloatPlan> native_float_plan;
    std::unique_ptr<detail::search_fft::DoublePlan> native_double_plan;
    std::size_t native_plan_bytes()const {
        return (native_float_plan?native_float_plan->working_bytes()+sizeof(*native_float_plan):0)+
            (native_double_plan?native_double_plan->working_bytes()+sizeof(*native_double_plan):0);
    }
    PatternCode code;
    // Immutable geometry: never rebuild pulse validation/calendar guards in
    // the phase-bank binary searches or per-track address comparisons.
    std::optional<FhssSymbolCalendar> symbol_calendar;
    std::unique_ptr<ScheduledSymbolCalendar> packed_calendar;
    std::unique_ptr<SymbolBurstPlan> burst_plan;
    struct BurstPolicy {long double minimum_rate=1,maximum_rate=1;bool capture_revoked=false;};
    std::unique_ptr<BurstPolicy> burst_policy;
    mutable std::unique_ptr<SymbolBurstCursor> burst_cursor;
    SymbolBurstCoordinate burst_origin;
    bool acquisition_suspended=false;
    bool defer_acquisition=false;
    std::uint64_t deferred_acquisition_frontier=UINT64_MAX;
    std::vector<std::uint8_t> acquisition_mask;
    double frequency_origin_hz=0,acquisition_origin_hz=0;
    const PatternPhaseCurve* phase_curve=nullptr;
    const PatternPhaseCurve* execution_phase_view=nullptr;
    const PatternPhaseCurve* cached_phase_view=nullptr;
    std::uint64_t phase_cache_first=0;
    std::size_t phase_cache_count=0;
    bool prior_dispatch=false;
    std::size_t prior_pending_count=0;
    std::uint64_t shifted_trials=0,accepted_serial=0,track_serial=0;
    std::uint64_t prior_track_floor=0,prior_acceptance_floor=0;
    struct LinkEvidence {std::uint64_t serial=0;double frequency_offset_hz=0,clock_error_ppm=0;
        const PatternPhaseCurve* phase_view=nullptr;};
    LinkEvidence link_evidence;
    std::uint64_t prior_link_floor=0;
    bool prior_candidate_before=false;
    bool prior_schedule_saved=false,prior_early_qualified=false,prior_component_qualified=false;
    std::uint64_t acquisition_last_start=0,acquisition_capture_end=0,unobserved_until=0;
    std::vector<std::uint64_t> packed_phases;
    // A broad symbol-zero prediction is not a qualified UTC phase map. Keep
    // its admission bound private so diagnostics never imply GPS qualification.
    std::optional<PatternStartWindow> acquisition_window;
    bool broad_start_window=false;
    long double packed_latest_start_samples=std::numeric_limits<long double>::infinity();
    long double packed_rate_displacement=0;
    std::size_t packed_ring_extra=0;
    bool calendar_symbols() const {return burst_plan || symbol_calendar.has_value() || static_cast<bool>(packed_calendar);}
    std::uint64_t calendar_rank(std::uint64_t phase) const {
        if(burst_plan)return 0; // A Burst epoch owns one exact payload phase.
        return packed_calendar?packed_calendar->first_rank(config.stream_epoch,phase):
            symbol_calendar->first_rank(config.stream_epoch,phase);
    }
    std::unique_ptr<PatternCorrelator> fallback;
    bool frequency_axis_selected=false;
    std::size_t budget=0,fixed_reservation=0,arithmetic_reservation=0,configured_bit_limit=0,bin_samples=0,length=0,transform=0,hop=0;
    std::vector<Complex> ring,work,spectrum,product,reference;
    std::vector<detail::FftDriftAccumulator> drift_scratch;
    std::vector<detail::DifferentialAccumulator> differential_scratch;
    std::vector<detail::FftSearchScore> drift_scores;
    unsigned drift_sections=1;
    std::uint64_t differential_window=0;
    double projection_image_ratio=0;
    double timing_minimum_rate=.99,timing_maximum_rate=1.01;
    std::uint64_t scheduled_start=std::numeric_limits<std::uint64_t>::max();
    std::size_t scheduled_observed_starts=0;
    using TimingComponents = std::array<detail::FftStartRange,256>;
    std::unique_ptr<TimingComponents> timing_components;
    static_assert(std::tuple_size_v<TimingComponents><=std::numeric_limits<std::uint16_t>::max());
    std::uint16_t timing_component_count=0,timing_component_index=0;
    struct HelperOverlapStorage {
        struct PublishedSource {PatternSourceIdentity identity;std::uint64_t first=0;};
        std::vector<std::optional<PublishedSource>> completed;
        std::size_t history_limit=0;
        std::size_t working_bytes()const {return sizeof(*this)+completed.capacity()*sizeof(completed.front());}
    };
    std::unique_ptr<HelperOverlapStorage> helper_overlap;
    std::uint64_t logical_hop_begin=0,logical_hop_end=0;
    bool component_hop=false,acquisition_prepaid=false;
    bool early_eof_pending=false,early_eof_replayable=false,final_component_replay=false;
    std::uint64_t early_eof_begin=0,early_eof_end=0,early_trials_before=0,early_history_before=0;
    std::uint64_t continuation_schedule=std::numeric_limits<std::uint64_t>::max();
    std::uint64_t earliest_unscored_start=0;
    std::vector<double> energy_prefix;
    std::vector<std::array<std::vector<Complex>,2>> templates;
    std::vector<std::array<double,2>> template_energy;
    std::vector<std::array<Complex,2>> template_square;
    struct CachedTemplates {
        std::vector<std::array<std::vector<Complex>,2>> rows;
        std::vector<std::array<double,2>> energy;
        std::vector<std::array<Complex,2>> square;
        bool valid=false;
    };
    std::vector<CachedTemplates> cached_templates;
    std::size_t cache_reservation=0;
    struct ExactInitialRows {
        static constexpr std::size_t maximum_symbols=4,maximum_bins=32;
        std::array<std::array<Complex,2>,maximum_symbols*maximum_bins> values{};
        std::array<detail::FftExactRawReference,maximum_symbols> references{};
        std::size_t count=0;
        bool valid=false;
        ~ExactInitialRows() {
            OPENSSL_cleanse(values.data(),sizeof(values));
            OPENSSL_cleanse(references.data(),sizeof(references));
        }
    };
    std::unique_ptr<ExactInitialRows> exact_initial_rows;
    struct ScoringStorage {
        std::vector<detail::FftSearchJob> jobs;
        std::vector<detail::FftPreparedTemplate> templates;
        std::vector<detail::FftSearchScore> outputs;
        std::vector<detail::FftSearchWorkspace> workspaces;
        std::vector<PatternCode> tracking_workers;
        std::size_t tracking_reservation=0;
        std::vector<std::array<Complex,2>> nominal_reference;
    };
    // Exactly one push-scoped owner. Keeping only the original vector-sized
    // handle here preserves the idle footprint visible to the receiver bank.
    std::vector<ScoringStorage> scoring;
    std::size_t scoring_reservation=0;
    std::vector<std::array<Complex,2>> tracking_reference;
    std::uint64_t tracking_reference_index=0;
    std::size_t tracking_reference_frequency=0;
    bool tracking_reference_valid=false;
    std::uint64_t prepared_template_index=0;
    bool templates_valid=false,streamed_templates=false,local_search_fallback=false,cached_from_streamed=false;
    bool long_symbol=false,required_tracking_reference=false,variable_clock=false;
    std::uint64_t active_stream_phase=0,phase_step=1,phase_upper=0;
    // Compact receivers do not retain an unused FFT telemetry record.
    std::unique_ptr<PatternFftWork> fft_workload;
    std::uint64_t sample=0,bins=0,next_start=0;
    Complex sum{},oscillator{1,0},rotation{},previous_chip{};
    double noise_condition=1;
    bool real_rank=false,sample_fit=false,shaped=false;
    std::size_t partial=0;
    bool finished=false,oscillator_valid=true,candidate_limit_reached=false,helper_gap_protection=false;
    std::uint64_t trials=0,history_insertions=0;
    std::vector<PatternEvidence> history,peaks;
    std::unique_ptr<detail::NativeDiagnosticHistoryTail<PatternEvidence>> diagnostic_tail;
    struct Completed {std::uint64_t first=0,end=0;double frequency=0;};
    std::vector<Completed> completed;
    std::vector<PatternBurst> bursts;
    std::vector<Complex> points;
    std::size_t point_cursor=0;
    struct Track {
        PatternBurst burst;
        double frequency_origin_hz=0;
        const PatternPhaseCurve* phase_view=nullptr;
        std::uint64_t serial=0;
        std::uint64_t next=0,index=1,unconfirmed_symbols=0;
        std::uint64_t phase_lower=0,phase_upper=0;
        double total_score=0,penalty=0;
        double pending_score=0,confirmed_score=0;
        double total_support=0,confirmed_support=0;
        std::vector<double> frequency_scores;
        std::size_t confirmed=0,gap_slots=0;
        std::uint64_t confirmed_end=0;
        // Calendar continuation is predicted from the last accepted start.
        // Failed slots/nulls never reset accumulated timing displacement.
        std::uint64_t anchor_start=0,anchor_index=0;
        long double absent_samples=0;
        std::size_t frequency=0;
        bool admitted=false,established=false,pending_gap=false,helper_gap_protected=false;
    };
    std::vector<Track> tracks;
    PatternBurst latest;

    Impl(Config c,std::size_t bytes,PatternSearch options,detail::NativePeakConstruction* construction=nullptr)
        :config(c),search(std::move(options)),code(c,c.stream_epoch),budget(bytes) {
        detail::NativePeakConstruction::ReceiverScope construction_peaks(construction,peaks,search.candidate_limit);
        validate(c);
        if(c.symbol_burst.enabled()) {
            burst_plan=std::make_unique<SymbolBurstPlan>(c);
            burst_policy=std::make_unique<BurstPolicy>();
            const auto slot=burst_plan->bind_payload(c.stream_epoch,c.stream_phase_samples);
            burst_origin=slot.payload_begin;
            burst_cursor=std::make_unique<SymbolBurstCursor>(*burst_plan,slot);
            // UTC support boundaries determine one exact cipher phase. Arrival
            // uncertainty remains in the complete existing timing search.
            search.search_stream_phases=false;
        }
        if(packed_scheduled_symbols(c))packed_calendar=std::make_unique<ScheduledSymbolCalendar>(c);
        if(deferred_symbols(c))symbol_calendar.emplace(*c.fake_hopping,c.sample_rate,
            pattern_pulse_padding_samples(c),code.symbol_samples());
        shaped=pattern_pulse_enabled(c);
        drift_sections=detail::drift_section_count(c,search.drift_tolerant);
        if(!std::isfinite(search.differential_window_seconds) || search.differential_window_seconds<0)
            throw Error("invalid differential integration window");
        if(drift_sections>1)differential_window=detail::differential_window_samples(code.symbol_samples(),
            code.chip_samples(),c.sample_rate,search.differential_window_seconds);
        if(!c.pattern_symbols)throw Error("pattern receiver requires binary pattern transport");
        if(!std::isfinite(search.false_alarm_probability) || search.false_alarm_probability<=0 || search.false_alarm_probability>=1 ||
           !std::isfinite(search.retain_score) || search.retain_score<0 ||
           !search.candidate_limit || search.candidate_limit>65536 || !search.track_limit || search.track_limit>128 ||
           !search.bit_limit || !search.chunk_bits || !search.initial_stream_symbols || search.initial_stream_symbols>64)
            throw Error("invalid pattern search limits");
        if((search.start_offset_seconds&&!std::isfinite(*search.start_offset_seconds)) ||
           !std::isfinite(search.start_uncertainty_seconds) || search.start_uncertainty_seconds<0 ||
           (search.amplitude_start_offset_seconds&&!std::isfinite(*search.amplitude_start_offset_seconds)) ||
           (search.amplitude_start_uncertainty_seconds&&(!std::isfinite(*search.amplitude_start_uncertainty_seconds)||
               *search.amplitude_start_uncertainty_seconds<0)))
            throw Error("invalid pattern system-clock window");
        if(search.admitted_clock_cell_half_width_ppm &&
           (!std::isfinite(*search.admitted_clock_cell_half_width_ppm) || *search.admitted_clock_cell_half_width_ppm<0))
            throw Error("invalid original pattern clock-cell radius");
        // Public Burst has the same single initial opportunity as the ordinary
        // public receiver, including an early amplitude fallback.
        if(c.symbol_burst.enabled() && !c.scramble && !c.dsss)search.initial_stream_symbols=1;
        const auto strategy=resolve_receiver_strategy(config,search,bytes);
        if(!strategy.supported)throw Error(strategy.reason);
        if(strategy.backend==ReceiverBackend::amplitude_energy) {
            construction_peaks.require_native();
            if(search.amplitude_start_offset_seconds)search.start_offset_seconds=search.amplitude_start_offset_seconds;
            if(search.amplitude_start_uncertainty_seconds)search.start_uncertainty_seconds=*search.amplitude_start_uncertainty_seconds;
            if(search.amplitude_start_window)search.qualified_start_window=search.amplitude_start_window;
            const auto wrapper=wrapper_bytes();
            search.amplitude_channel_limit=strategy.amplitude_channels;
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain amplitude control state");
            fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper,
                PatternCorrelatorOptions{.amplitude_only=true});
            return;
        }
        if(scheduled_bursts(config) && !packed_scheduled_symbols(config)) {
            construction_peaks.require_native();
            double correction=0;
            if(config.oscillator_search && search.admitted_clock_half_width_ppm &&
               *search.admitted_clock_half_width_ppm>oscillator_pattern_search(config).requested_clock_half_width_ppm)
                correction=utc_transmit_rate_limit(config);
            auto prepared=scheduled_complex_search(config,search,correction);
            search=std::move(prepared.search);
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain split-complex control state");
            fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
            return;
        }
        if(strategy.backend==ReceiverBackend::frequency_axis) {
            frequency_axis_selected=true;search.compact_clock_search=false;
        }
        if(c.oscillator_search && search.hypotheses.empty() && search.frequency_offsets_hz.empty() &&
           search.clock_errors_ppm==std::vector<double>{0} && !search.couple_clock_to_carrier) {
            auto declared=oscillator_pattern_search(c);
            search.hypotheses=std::move(declared.hypotheses);
            search.admitted_clock_cell_half_width_ppm=declared.clock_step_ppm/2;
        }
        const bool permit_local_fallback=search.allow_local_clock_fallback && search.expand_clock_search &&
            search.hypotheses.empty() && search.frequency_offsets_hz.empty() && !search.couple_clock_to_carrier &&
            search.clock_errors_ppm==std::vector<double>{0};
        if(!search.hypotheses.empty()) {
            if(search.hypotheses.size()>maximum_pattern_frequency_rate_hypotheses)
                throw Error("pattern frequency/rate bank exceeds finite hypothesis limit");
            search.frequency_offsets_hz.clear();
            search.frequency_offsets_hz.reserve(search.hypotheses.size());
            for(const auto& hypothesis:search.hypotheses) {
                if(!std::isfinite(hypothesis.clock_error_ppm) || std::abs(hypothesis.clock_error_ppm)>10000)
                    throw Error("clock-rate hypotheses must fit +/-10000 ppm");
                search.frequency_offsets_hz.push_back(hypothesis.frequency_offset_hz);
            }
            search.couple_clock_to_carrier=false;
            search.uncoupled_frequency_count=0;
            search.clock_errors_ppm={0};
        } else {
            if(search.clock_errors_ppm.empty() || search.clock_errors_ppm.size()>maximum_pattern_rate_hypotheses)
                throw Error("clock-rate bank must contain 1..65 hypotheses");
            for(auto ppm:search.clock_errors_ppm)
                if(!std::isfinite(ppm)||std::abs(ppm)>10000)throw Error("clock-rate hypotheses must fit +/-10000 ppm");
        }
        if(search.frequency_offsets_hz.empty()) {
            search.frequency_offsets_hz=search.expand_clock_search?default_pattern_frequency_offsets(c):
                local_frequency_offsets(c);
            search.couple_clock_to_carrier=search.frequency_offsets_hz.size()>5 &&
                c.spreading_mode==SpreadingMode::pattern && search.clock_errors_ppm==std::vector<double>{0};
            if(search.couple_clock_to_carrier) {
                search.uncoupled_frequency_count=search.frequency_offsets_hz.size();
                const auto offsets=search.frequency_offsets_hz;
                search.frequency_offsets_hz.insert(search.frequency_offsets_hz.end(),offsets.begin(),offsets.end());
            }
        }
        if(search.frequency_offsets_hz.size()>2*maximum_pattern_frequency_hypotheses ||
           search.uncoupled_frequency_count>search.frequency_offsets_hz.size())
            throw Error("pattern frequency bank exceeds finite hypothesis limit");
        double maximum_offset=0;
        for(auto offset:search.frequency_offsets_hz) {
            if(!std::isfinite(offset) || std::abs(offset)>pattern_frequency_offset_limit(c))
                throw Error("pattern carrier offset exceeds sampled passband headroom");
            if(search.couple_clock_to_carrier && std::abs(offset/c.carrier_hz)>.01)
                throw Error("coupled clock hypotheses must fit +/-10000 ppm");
            maximum_offset=std::max(maximum_offset,std::abs(offset));
        }
        // The former FFT path validated independent rates but silently used
        // nominal time. Flatten them once, in the compact receiver's rate-first
        // order, so all backends score the same finite alternatives.
        if(search.hypotheses.empty() && search.clock_errors_ppm!=std::vector<double>{0}) {
            search.frequency_rate_competition=search.couple_clock_to_carrier;
            if(search.frequency_offsets_hz.size()>maximum_pattern_frequency_rate_hypotheses/search.clock_errors_ppm.size())
                throw Error("pattern frequency/rate bank exceeds finite hypothesis limit");
            for(auto ppm:search.clock_errors_ppm)for(std::size_t f=0;f<search.frequency_offsets_hz.size();++f) {
                const auto coupled=search.couple_clock_to_carrier && f>=search.uncoupled_frequency_count?
                    1+search.frequency_offsets_hz[f]/c.carrier_hz:1.;
                const auto combined_ppm=coupled==1?ppm:(coupled*(1+ppm*1e-6)-1)*1e6;
                if(std::abs(combined_ppm)>10000+1e-8)throw Error("combined clock-rate hypotheses must fit +/-10000 ppm");
                search.hypotheses.push_back({search.frequency_offsets_hz[f],std::clamp(combined_ppm,-10000.,10000.)});
            }
            search.frequency_offsets_hz.clear();search.frequency_offsets_hz.reserve(search.hypotheses.size());
            for(const auto& hypothesis:search.hypotheses)search.frequency_offsets_hz.push_back(hypothesis.frequency_offset_hz);
            search.couple_clock_to_carrier=false;search.uncoupled_frequency_count=0;search.clock_errors_ppm={0};
        }
        variable_clock=search.couple_clock_to_carrier || std::any_of(search.hypotheses.begin(),search.hypotheses.end(),
            [](const auto& hypothesis){return hypothesis.clock_error_ppm!=0;});
        if(!search.hypotheses.empty()) {
            timing_minimum_rate=timing_maximum_rate=clock_ratio(0);
            for(std::size_t f=1;f<search.hypotheses.size();++f) {
                timing_minimum_rate=std::min(timing_minimum_rate,clock_ratio(f));
                timing_maximum_rate=std::max(timing_maximum_rate,clock_ratio(f));
            }
        }
        configured_bit_limit=search.bit_limit;
        if(burst_policy) {
            auto& policy=*burst_policy;
            policy.minimum_rate=policy.maximum_rate=clock_ratio(0);
            for(std::size_t i=1;i<search.frequency_offsets_hz.size();++i) {
                policy.minimum_rate=std::min(policy.minimum_rate,static_cast<long double>(clock_ratio(i)));
                policy.maximum_rate=std::max(policy.maximum_rate,static_cast<long double>(clock_ratio(i)));
            }
            if(search.admitted_clock_half_width_ppm) {
                const auto radius=static_cast<long double>(*search.admitted_clock_half_width_ppm)*1e-6L;
                policy.minimum_rate=1-radius;policy.maximum_rate=1+radius;
            }
            if(!std::isfinite(policy.minimum_rate) || !std::isfinite(policy.maximum_rate) ||
               policy.minimum_rate<=0 || policy.minimum_rate>policy.maximum_rate)
                throw Error("Symbol_Burst original clock policy is not finite");
        }
        if(!c.scramble&&!c.dsss)search.initial_stream_symbols=1;
        if(deferred_symbols(c) && search.search_stream_phases) {
            search.initial_stream_symbols=std::max<std::size_t>(search.initial_stream_symbols,
                symbol_calendar->lower_bound(c.stream_epoch,0,c.sample_rate));
        }
        if(search.compact_clock_search && !search.couple_clock_to_carrier && (!calendar_symbols() || burst_plan)) {
            construction_peaks.require_native();
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
            return;
        }
        fft_workload=std::make_unique<PatternFftWork>();
        const auto symbols=code.symbol_samples();
        long_symbol=symbols>=16ULL*c.sample_rate;
        active_stream_phase=c.stream_phase_samples;
        phase_step=canonical_phase_step(c);
        phase_upper=search.search_stream_phases?
            (canonical_phase_extent(c)-1)/phase_step*phase_step:c.stream_phase_samples;
        if(packed_calendar && search.search_stream_phases) {
            const auto phases=packed_calendar->first_rank(symbol_schedule_detail::add(c.stream_epoch,1),0)-
                packed_calendar->first_rank(c.stream_epoch,0);
            if(phases>bytes/sizeof(std::uint64_t))throw Error("packed complex phases exceed DSP workspace");
            packed_phases.reserve(static_cast<std::size_t>(phases));
            packed_calendar->for_each_phase_in_epoch(c.stream_epoch,[&](std::uint64_t phase) {
                if(packed_phases.size()>=bytes/sizeof(std::uint64_t))
                    throw Error("packed complex phases exceed DSP workspace");
                packed_phases.push_back(phase);
            });
            if(packed_phases.empty())throw Error("no packed scheduled payload start in this UTC epoch");
            phase_upper=packed_phases.back();
        }
        if(packed_calendar || burst_plan) {
            auto radius=search.admitted_clock_half_width_ppm.value_or(0.);
            for(std::size_t f=0;f<search.frequency_offsets_hz.size();++f)
                radius=std::max(radius,static_cast<double>(std::abs(clock_ratio(f)-1)*1e6L));
            if(!std::isfinite(radius) || radius>=1e6)
                throw Error("packed complex clock interval is not finite");
            const auto error=static_cast<long double>(radius)*1e-6L;
            // Both the selected lane and true declared rate are in this
            // interval. This bound does not assume a finer joint-cell proof.
            packed_rate_displacement=error==0?0:std::nextafter(1/(1-error)-1/(1+error),
                std::numeric_limits<long double>::infinity());
        }
        acquisition_window=search.qualified_start_window;
        if((packed_calendar || burst_plan) && !acquisition_window && search.start_offset_seconds) {
            broad_start_window=true;
            acquisition_window=PatternStartWindow{
                static_cast<long double>(*search.start_offset_seconds)*c.sample_rate,1,
                static_cast<long double>(search.start_uncertainty_seconds)*c.sample_rate};
        }
        if(acquisition_window) {
            // Keep the original short FFT phase groups and retirement grid.
            // Qualified work selection never changes their coordinates.
            auto status=PatternFftWindowStatus::unsupported_geometry;
            if((!long_symbol || frequency_axis_selected) && (c.scramble||c.dsss) && c.spreading_mode==SpreadingMode::pattern &&
               (!search.hypotheses.empty() || broad_start_window) && !search.couple_clock_to_carrier && search.start_offset_seconds) {
                status=detail::pattern_fft_window_finite(*acquisition_window,broad_start_window?0:phase_upper)?
                    PatternFftWindowStatus::active:PatternFftWindowStatus::invalid_numerics;
            }
            if(!broad_start_window)fft_workload->timing_window=status;
            if(status!=PatternFftWindowStatus::active) {
                acquisition_window.reset();search.qualified_start_window.reset();
            } else fft_workload->restricted_timing_active=search.restricted_timing_search;
        }
        if((packed_calendar || burst_plan) && !acquisition_window && search.start_offset_seconds) {
            // Invalid/unsupported tight timing returns to the original broad
            // admission policy, never to an unbounded calendar acquisition.
            broad_start_window=true;
            const PatternStartWindow broad{
                static_cast<long double>(*search.start_offset_seconds)*c.sample_rate,1,
                static_cast<long double>(search.start_uncertainty_seconds)*c.sample_rate};
            if(detail::pattern_fft_window_finite(broad,0)) {
                acquisition_window=broad;
                fft_workload->restricted_timing_active=search.restricted_timing_search;
            }
        }
        if((packed_calendar || burst_plan) && acquisition_window) {
            // Calendar deltas need not be monotone across canonical phases:
            // one initial alternative may cross a long null while another
            // stays inside its active group. Cache the exact union endpoint.
            packed_latest_start_samples=-std::numeric_limits<long double>::infinity();
            const auto include=[&](std::uint64_t phase) {
                const auto geometry=window_geometry(search.initial_stream_symbols-1,{phase,phase});
                packed_latest_start_samples=std::max(packed_latest_start_samples,
                    acquisition_window->epoch_origin_samples+acquisition_window->phase_scale*geometry.upper+
                    acquisition_window->half_width_samples+
                    static_cast<long double>(geometry.samples)*geometry.index/timing_minimum_rate);
            };
            if(search.search_stream_phases)for(const auto phase:packed_phases)include(phase);
            else include(c.stream_phase_samples);
        }
        // Chip and symbol boundaries must fall on bin boundaries. Rounding a
        // symbol to whole bins would accumulate timing drift and count edges
        // of adjacent symbols twice, especially with partial final chips.
        bin_samples=detail::pattern_observation_bin_samples(c,maximum_offset);
        const auto omega=tau*c.carrier_hz/c.sample_rate;
        const auto sine=std::sin(omega);
        const auto bin_image=[&](std::size_t count) {
            return std::abs(sine)>1e-12?std::abs(std::sin(static_cast<double>(count)*omega)/sine):static_cast<double>(count);
        };
        // Preserve compact private bins when their two carrier quadratures
        // are orthogonal: that fit already retains all projected energy.
        // Short nonorthogonal/singular private bins need exact sample fits
        // to avoid a high-SNR confidence ceiling. Public short patterns keep
        // their existing sample-resolution timing behavior.
        sample_fit=bin_samples==1 && (symbols<=256 || (!c.scramble && !c.dsss));
        const auto image=bin_image(bin_samples);
        projection_image_ratio=detail::pattern_projection_image_ratio(bin_samples,c.carrier_hz,c.sample_rate);
        const auto small=static_cast<double>(bin_samples)-image;
        real_rank=small<=1e-10*static_cast<double>(bin_samples);
        if(!real_rank)noise_condition=(static_cast<double>(bin_samples)+image)/small;
        double minimum_rate=search.couple_clock_to_carrier?1-maximum_offset/c.carrier_hz:1.;
        if(!search.hypotheses.empty()) {
            minimum_rate=clock_ratio(0);
            for(std::size_t f=1;f<search.hypotheses.size();++f)minimum_rate=std::min(minimum_rate,clock_ratio(f));
        }
        const auto observation_layout=detail::pattern_observation_layout(c,bin_samples,minimum_rate);
        length=observation_layout.length;transform=observation_layout.transform;hop=observation_layout.hop;
        required_tracking_reference=variable_clock || transform<2*length;
        // Transform and baseband history allocations are checked before any
        // allocation. The RAM dropdown is a ceiling, not an allocation target.
        if(packed_calendar) {
            const auto failed=(6ULL*c.sample_rate+symbols-1)/symbols;
            const auto groups=2+(failed+packed_calendar->slots_per_group()-1)/packed_calendar->slots_per_group();
            const auto source=symbol_schedule_detail::multiply(groups,
                symbol_schedule_detail::multiply(2ULL*c.sample_rate/5,c.scheduled_hopping->group_hops));
            const auto span=scheduled_physical_span_samples(c,source);
            const auto extra=std::ceil(2*static_cast<long double>(span)*packed_rate_displacement/bin_samples)+8;
            if(!std::isfinite(extra) || extra>std::numeric_limits<std::size_t>::max()-4*length-2*hop)
                throw Error("packed complex continuation coverage exceeds address space");
            packed_ring_extra=static_cast<std::size_t>(extra);
        }
        if(burst_plan) {
            // Residual timing displacement accumulates across physical gaps.
            // Reserve the same full failed-symbol run as the physical END rule.
            const auto failed=(6ULL*c.sample_rate+symbols-1)/symbols;
            const auto maximum_step=symbol_schedule_detail::add(
                symbol_schedule_detail::add(symbol_schedule_detail::multiply(2,burst_plan->padding_samples()),
                    symbol_schedule_detail::multiply(c.symbol_burst.wait_symbols+burst_plan->random_limit()+1,symbols)),
                90ULL*c.sample_rate);
            const auto span=symbol_schedule_detail::multiply(failed+1,maximum_step);
            const auto extra=std::ceil(2*static_cast<long double>(span)*packed_rate_displacement/bin_samples)+8;
            if(!std::isfinite(extra) || extra>std::numeric_limits<std::size_t>::max()-4*length-2*hop)
                throw Error("Symbol_Burst continuation coverage exceeds address space");
            packed_ring_extra=static_cast<std::size_t>(extra);
        }
        const auto arrays=5+2*search.frequency_offsets_hz.size();
        long double required=sizeof(Impl)+sizeof(PatternFftWork)+sizeof(TimingComponents)+static_cast<long double>(transform)*sizeof(Complex)*arrays+
            static_cast<long double>(4*length+2*hop+packed_ring_extra)*sizeof(Complex)+
            (required_tracking_reference?2.L*length*sizeof(Complex):0.L)+
            static_cast<long double>(transform+1)*sizeof(double)+
            static_cast<long double>(2*search.candidate_limit)*sizeof(PatternEvidence)+
            static_cast<long double>(search.track_limit)*(sizeof(Track)+sizeof(PatternBurst)+sizeof(Completed))+
            static_cast<long double>(search.track_limit+2)*search.frequency_offsets_hz.size()*sizeof(double)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(templates)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(template_energy)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.size())*sizeof(decltype(template_square)::value_type)+
            static_cast<long double>(search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            static_cast<long double>(search.hypotheses.capacity())*sizeof(PatternFrequencyRateHypothesis)+
            4096*sizeof(Complex)+code.working_bytes()+sizeof(PatternReceiver)+packed_phases.capacity()*sizeof(std::uint64_t)+(packed_calendar?packed_calendar->working_bytes():0)+
            (burst_plan?burst_plan->working_bytes()+burst_cursor->working_bytes()+sizeof(BurstPolicy):0);
        if(drift_sections>1)required+=static_cast<long double>(hop)*
            (sizeof(detail::FftDriftAccumulator)+sizeof(detail::FftSearchScore));
        if(differential_window)required+=static_cast<long double>(hop)*sizeof(detail::DifferentialAccumulator);
        // Describe only a known native allocation shortfall. This is not
        // credit for absent backing or a change to any reservation/bit limit.
        const auto native_minimum_without_rows=required-
            2.L*search.frequency_offsets_hz.size()*transform*sizeof(Complex)+2*search.track_limit+2;
        const auto native_construction_shortfall=[&,required_before_rows=required]()noexcept -> std::size_t {
            const auto exact_integer_limit=std::ldexp(1.L,std::numeric_limits<long double>::digits);
            const auto maximum=static_cast<long double>(std::numeric_limits<std::size_t>::max());
            // Beyond the exactly representable integer domain, keep the old
            // unknown-shortfall redirect rather than advertise rounded bytes.
            if(!std::isfinite(required_before_rows) || required_before_rows>=exact_integer_limit ||
               !std::isfinite(native_minimum_without_rows) || native_minimum_without_rows>=exact_integer_limit ||
               static_cast<long double>(bytes)>=exact_integer_limit || native_minimum_without_rows>=maximum ||
               native_minimum_without_rows<=static_cast<long double>(bytes))return 0;
            return static_cast<std::size_t>(std::ceil(native_minimum_without_rows-static_cast<long double>(bytes)));
        };
        // Large private rows change at every initial symbol/phase and cannot
        // use the small-symbol persistent transform cache below. Streaming
        // them frees the redundant per-frequency rows for native FFT plans.
        // Keep small/public/parallel geometries and the FP64 diagnostic control
        // on their existing choice; the crossover is execution-measured.
        // Automatic application searches otherwise choose the parallel FP64
        // fallback before reaching the measured native path. For large coherent
        // private rows, serial FP32 plus shared template reuse reduced complete
        // CPU and wall time versus that fallback in the paired local crossover.
        // Explicit worker requests, small/public and drift geometries keep their
        // existing scheduling; no timing/frequency/epoch alternative is removed.
        // Do not serialize an automatic bank unless streamed fixed storage,
        // native float buffers and its plan fit together. Runtime checks still
        // account for tracks/caches and fall back if later payload growth wins.
        const bool automatic_native_fits=required-
            2.L*search.frequency_offsets_hz.size()*transform*sizeof(Complex)+
            24.L*transform+65536.L<=bytes;
        if(search.worker_threads==0 && automatic_native_fits &&
           (config.search_arithmetic==SearchArithmetic::default_mode || config.search_arithmetic==SearchArithmetic::fp32) &&
           transform>=65536 && !sample_fit && drift_sections==1 && !differential_window &&
           (c.scramble||c.dsss) && symbols>256 && search.initial_stream_symbols>1)
            search.worker_threads=1;
        const bool native_private_rows=config.search_arithmetic!=SearchArithmetic::fp64 &&
            transform>=65536 && !sample_fit && drift_sections==1 && !differential_window &&
            search.worker_threads==1 && (c.scramble||c.dsss) && symbols>256 &&
            search.initial_stream_symbols>1;
        // Optional template rows must leave room for at least one retained bit.
        if(required+2*search.track_limit+2>bytes || drift_sections>1 || native_private_rows || (search.prefer_streamed_templates &&
           (variable_clock || (fft_workload->restricted_timing_active && search.partitioned_timing_search && !sample_fit)))) {
            const auto without_rows=required-2.L*search.frequency_offsets_hz.size()*transform*sizeof(Complex);
            // A wide bank needs every hypothesis, but not every transformed
            // template at once. Generate rows in bounded worker scratch (or
            // the existing serial product buffer) when retaining them all
            // would force an unnecessarily expensive clock-window fallback.
            if(without_rows+2*search.track_limit+2<=bytes) {
                streamed_templates=true;required=without_rows;
            }
        }
        if(required>bytes && packed_calendar)
            throw Error("complete packed complex timing/clock coverage exceeds DSP workspace");
        if(required>bytes && search.start_offset_seconds) {
            if(permit_local_fallback && search.couple_clock_to_carrier) {
                construction_peaks.require_native(native_construction_shortfall());
                search.frequency_offsets_hz=local_frequency_offsets(c);
                search.expand_clock_search=false;search.couple_clock_to_carrier=false;
                search.uncoupled_frequency_count=0;
                // Preserve the existing late Correlator wrapper and grant.
                timing_components=std::make_unique<TimingComponents>();
                const auto wrapper=wrapper_bytes();
                if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
                fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
                if(fft_workload->timing_window==PatternFftWindowStatus::active)
                    fft_workload->timing_window=PatternFftWindowStatus::correlator_fallback;
                local_search_fallback=true;
                return;
            }
            if(search.couple_clock_to_carrier)
                throw Error("complete carrier/clock competition exceeds FFT workspace; increase the DSP limit");
            construction_peaks.require_native(native_construction_shortfall());
            // Preserve the existing late Correlator wrapper and grant.
            timing_components=std::make_unique<TimingComponents>();
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback=std::make_unique<PatternCorrelator>(config,search,bytes-wrapper);
            if(fft_workload->timing_window==PatternFftWindowStatus::active)
                fft_workload->timing_window=PatternFftWindowStatus::correlator_fallback;
            return;
        }
        if(required>bytes)
            throw Error("pattern correlation and timing coverage exceed DSP workspace; reduce integration/bandwidth or increase the limit");
        fixed_reservation=static_cast<std::size_t>(required);
        // Active tracks, queued completions, latest completion and one
        // allocation/copy in flight can each retain an independent bit vector.
        search.bit_limit=std::min(search.bit_limit,(bytes-fixed_reservation)/(2*search.track_limit+2));
        if(!search.bit_limit)throw Error("pattern workspace cannot retain bit candidates");
        ring.resize(4*length+2*hop+packed_ring_extra);
        if(!search.prefer_streamed_templates || !search.release_idle_fft_scratch)ensure_execution_scratch();
        if(drift_sections>1){drift_scratch.resize(hop);drift_scores.resize(hop);}
        if(differential_window)differential_scratch.resize(hop);
        templates.resize(search.frequency_offsets_hz.size());
        if(required_tracking_reference)tracking_reference.resize(length);
        template_energy.resize(templates.size());
        template_square.resize(templates.size());
        history.reserve(search.candidate_limit);peaks.reserve(search.candidate_limit);completed.reserve(search.track_limit);
        tracks.reserve(search.track_limit);points.reserve(2048);bursts.reserve(search.track_limit);
        rotation=std::polar(1.,-tau*c.carrier_hz/c.sample_rate);
        // Short private acquisition revisits the same bounded initial stream
        // bank every hop. Cache its exact transforms only in spare workspace;
        // payload capacity and search coverage retain their original limits.
        const auto frequencies=search.frequency_offsets_hz.size();
        const auto rank_span=packed_calendar && search.search_stream_phases?
            calendar_rank(phase_upper)-calendar_rank(packed_phases.front()):
            symbol_calendar && search.search_stream_phases?1:0;
        const auto cache_count=symbol_schedule_detail::add(search.initial_stream_symbols,rank_span);
        const long double cache_bytes=static_cast<long double>(cache_count)*
            (sizeof(CachedTemplates)+static_cast<long double>(frequencies)*
             (sizeof(decltype(templates)::value_type)+sizeof(decltype(template_energy)::value_type)+
              sizeof(decltype(template_square)::value_type)+2*static_cast<long double>(transform)*sizeof(Complex)))+
            static_cast<long double>(length)*sizeof(decltype(tracking_reference)::value_type);
        bool initial_phases_equivalent=!(config.fake_hopping&&config.fake_hopping->matched_gaps&&phase_upper);
        if(search.search_stream_phases && phase_upper)for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            const auto first=stream_address(index,0);
            const auto last=stream_address(index,phase_upper);
            if(first.epoch!=last.epoch || first.ordinal!=last.ordinal){initial_phases_equivalent=false;break;}
        }
        // Distinct sample phases often share every initial template address.
        // Cache those transforms even while later symbols must resolve phase;
        // changing phase cannot change an address proven equal over its range.
        // Do not displace the separately measured native FP32 execution path
        // with a persistent FP64 transform cache merely because it fits.
        const bool deferred_cache=calendar_symbols() && search.reuse_deferred_templates && cache_bytes<=8*1024*1024 &&
            acquisition_arithmetic()!=SearchArithmetic::fp32;
        if((deferred_cache || (!streamed_templates && initial_phases_equivalent && symbols<=256)) &&
           search.initial_stream_symbols>1 && cache_bytes<=bytes-fixed_reservation) {
            // Deferred canonical rows recur across FFT hops even when the
            // source phase changes. A bounded exact cache can replace streamed
            // construction; its full capacities remain in cache_reservation.
            cached_from_streamed=streamed_templates;streamed_templates=false;
            cache_reservation=static_cast<std::size_t>(std::ceil(cache_bytes));
            cached_templates.resize(cache_count);
            for(auto& bank:cached_templates) {
                bank.rows.resize(frequencies);bank.energy.resize(frequencies);bank.square.resize(frequencies);
                for(auto& row:bank.rows)for(auto& values:row)values.resize(transform);
            }
            tracking_reference.resize(length);
            cache_reservation=cached_templates.capacity()*sizeof(CachedTemplates)+
                tracking_reference.capacity()*sizeof(decltype(tracking_reference)::value_type);
            for(const auto& bank:cached_templates) {
                cache_reservation+=bank.rows.capacity()*sizeof(decltype(templates)::value_type)+
                    bank.energy.capacity()*sizeof(decltype(template_energy)::value_type)+
                    bank.square.capacity()*sizeof(decltype(template_square)::value_type);
                for(const auto& row:bank.rows)for(const auto& values:row)
                    cache_reservation+=values.capacity()*sizeof(Complex);
            }
            if((deferred_cache && cache_reservation>8*1024*1024) || !cache_fits(bytes,0))drop_template_cache();
        }
        reserve_exact_initial_rows(bytes);
        stream_phase(search.search_stream_phases?(packed_calendar?packed_phases.front():0):c.stream_phase_samples);prepare_templates(0,{});
        if(packed_calendar)audio::trace::record("packed_complex_constructed",[&](auto& f) {
            f.add("epoch",config.stream_epoch);f.add("sample_rate",config.sample_rate);
            f.add("chip_samples",code.chip_samples());f.add("symbol_samples",code.symbol_samples());
            f.add("bin_samples",bin_samples);f.add("transform_bins",transform);f.add("hop_bins",hop);
            f.add("phase_count",search.search_stream_phases?packed_phases.size():1);
            f.add("frequency_rate_pairs",templates.size());
            std::vector<double> frequencies=search.frequency_offsets_hz,rates;rates.reserve(templates.size());
            for(std::size_t i=0;i<templates.size();++i)rates.push_back(clock_ratio(i));
            std::sort(frequencies.begin(),frequencies.end());std::sort(rates.begin(),rates.end());
            f.add("frequency_count",std::unique(frequencies.begin(),frequencies.end())-frequencies.begin());
            f.add("clock_count",std::unique(rates.begin(),rates.end())-rates.begin());
            f.add("minimum_rate",timing_minimum_rate);
            f.add("maximum_rate",timing_maximum_rate);f.add("initial_symbols",search.initial_stream_symbols);
            f.add("restricted_timing",fft_workload->restricted_timing_active);
            f.add("arrival_window_source",acquisition_window?
                (broad_start_window?"broad symbol-zero prediction":"qualified UTC phase map"):"unbounded diagnostic fallback");
            if(acquisition_window)f.add("arrival_half_width_samples",acquisition_window->half_width_samples);
            f.add("packed_slots_per_group",packed_calendar->slots_per_group());
            f.add("retained_ring_bins",ring.size());f.add("clock_displacement_per_sample",packed_rate_displacement);
            f.add("arithmetic_policy",static_cast<unsigned>(config.search_arithmetic));
            f.add("resolved_search_operands",acquisition_arithmetic()==SearchArithmetic::fp32?"FP32":"FP64");
            f.add("fft_backend",acquisition_arithmetic()==SearchArithmetic::fp32?"native FP32 or bounded FFT":"FP64 FFT");
            f.add("score_precision","FP64/wide original evidence");
            f.add("exact_template_cache_rows",cached_templates.size());f.add("streamed_templates",streamed_templates);
            f.add("working_bytes",working_bytes());f.add("reserved_bytes",fixed_reservation+cache_reservation);
            f.add("workspace_limit",budget);f.add("raw_capture_fallback",true);
        });
    }
    void stream_phase(std::uint64_t phase) {
        if(active_stream_phase==phase)return;
        code.set_stream_phase_samples(phase);active_stream_phase=phase;
        templates_valid=false;tracking_reference_valid=false;
    }
    struct PhaseGroup {std::uint64_t lower=0,upper=0;};
    struct PhaseGroups {
        std::array<PhaseGroup,3> small{};
        std::uint64_t first=0,step=0;
        std::span<const std::uint64_t> exact;
        PhaseGroup operator[](std::size_t index) const {
            if(!exact.empty())return {exact[index],exact[index]};
            if(step){const auto phase=first+index*step;return {phase,phase};}
            return small.at(index);
        }
    };
    PhaseGroups phase_groups(std::uint64_t index,std::uint64_t lower,std::uint64_t upper,
                                        std::size_t& count)const {
        PhaseGroups groups;count=0;
        if(burst_plan){groups.small[0]={config.stream_phase_samples,config.stream_phase_samples};count=1;return groups;}
        if(packed_calendar) {
            if(!search.search_stream_phases) {groups.small[0]={lower,upper};count=1;return groups;}
            const auto first=std::lower_bound(packed_phases.begin(),packed_phases.end(),lower);
            const auto last=std::upper_bound(first,packed_phases.end(),upper);
            groups.exact=std::span<const std::uint64_t>(first,last);count=groups.exact.size();return groups;
        }
        if(config.fake_hopping && config.fake_hopping->matched_gaps && lower<=upper) {
            // Same cipher address need not mean the same UTC pulse omission.
            // Retain each already-admitted phase point without a large array.
            groups.first=lower;groups.step=phase_step;count=(upper-lower)/phase_step+1;
            return groups;
        }
        if(symbol_calendar && lower<=upper) {
            // Each range spans less than one symbol, and canonical slots are
            // at least one symbol apart. Equality retains the earlier slot.
            const auto cut=symbol_schedule_detail::add(lower,source_start(0,lower));
            groups.small[count++]={lower,std::min(upper,cut)};
            if(cut<upper)groups.small[count++]={cut+1,upper};
            return groups;
        }
        while(lower<=upper) {
            const auto address=stream_address(index,lower);
            auto low=std::uint64_t{0},high=(upper-lower)/phase_step;
            while(low<high) {
                const auto mid=low+(high-low+1)/2;
                const auto candidate=stream_address(index,lower+mid*phase_step);
                if(candidate.epoch==address.epoch && candidate.ordinal==address.ordinal)low=mid;
                else high=mid-1;
            }
            const auto end=lower+low*phase_step;
            if(count==groups.small.size())throw Error("pattern phase groups exceed finite symbol interval");
            groups.small[count++]={lower,end};
            if(end==upper)break;
            lower=end+phase_step;
        }
        return groups;
    }
    std::uint64_t source_start(std::uint64_t index,std::uint64_t phase)const {
        if(burst_plan)return burst_plan->distance(burst_origin,burst_cursor->at(index).payload_begin);
        if(packed_calendar) {
            const auto physical=packed_calendar->physical_slot(symbol_schedule_detail::add(calendar_rank(phase),index));
            const auto anchor=symbol_schedule_detail::add(symbol_schedule_detail::multiply(config.stream_epoch,config.sample_rate),phase);
            if(physical<anchor)throw Error("packed complex symbol precedes its anchor");
            return physical-anchor;
        }
        return symbol_calendar?symbol_calendar->offset(config.stream_epoch,phase,index):
            symbol_schedule_detail::multiply(index,code.symbol_samples());
    }
    SymbolStreamAddress stream_address(std::uint64_t index,std::uint64_t phase)const {
        if(burst_plan) {
            const auto payload=burst_cursor->at(index).payload_begin;
            return {payload.epoch,payload.phase/code.symbol_samples(),payload.phase};
        }
        if(!calendar_symbols())return symbol_stream_address(config.stream_epoch,phase,index,
            code.symbol_samples(),config.sample_rate);
        const auto total=symbol_schedule_detail::add(source_start(index,phase),phase);
        const auto sample=static_cast<std::uint32_t>(total%config.sample_rate);
        return {symbol_schedule_detail::add(config.stream_epoch,total/config.sample_rate),
            sample/code.symbol_samples(),sample};
    }
    CachedTemplates& template_bank(std::uint64_t index) {
        const auto slot=index+(calendar_symbols()&&search.search_stream_phases?
            calendar_rank(active_stream_phase)-calendar_rank(packed_calendar?packed_phases.front():0):0);
        return cached_templates.at(static_cast<std::size_t>(slot));
    }
    std::uint64_t advance_bins(std::uint64_t index,std::uint64_t phase,std::size_t frequency)const {
        if(!calendar_symbols())return symbol_bins(frequency);
        const auto delta=source_start(index+1,phase)-source_start(index,phase);
        return static_cast<std::uint64_t>(std::llround(static_cast<long double>(delta)/clock_ratio(frequency)/bin_samples));
    }
    struct WindowGeometry {std::uint64_t samples,index,lower,upper,step;};
    WindowGeometry window_geometry(std::uint64_t index,PhaseGroup group)const {
        if(!calendar_symbols())return {code.symbol_samples(),index,group.lower,
            search.search_stream_phases?canonical_phase_cell_upper(config,group.upper):group.upper,
            search.search_stream_phases?1U:phase_step};
        // Same actual symbol address implies the same canonical slot rank and
        // hence the same preceding/future schedule. No gap is scored as data.
        const auto first=source_start(0,group.lower),delta=source_start(index,group.lower)-first;
        const auto phase=first+group.lower;
        // Broad start_offset predicts symbol zero independently of the private
        // canonical phase; later alternatives advance by the exact keyed delta.
        // Qualified maps instead date the UTC epoch and explicitly scale phase.
        return {std::max<std::uint64_t>(1,delta),delta?1U:0U,
            broad_start_window?0:phase,broad_start_window?0:phase,1};
    }
    SearchArithmetic acquisition_arithmetic()const {
        if(fp32_exact_fallback)return SearchArithmetic::fp64;
        auto requested=config.search_arithmetic;
        if(requested==SearchArithmetic::default_mode || requested==SearchArithmetic::fp32) {
            // Native serial streamed and bounded paths have complete execution
            // evidence. Do not activate the old conversion-heavy cached/parallel
            // float path merely because the minimum operand width permits it.
            requested=streamed_templates && search.worker_threads==1?
                SearchArithmetic::fp32:SearchArithmetic::fp64;
        }
        return detail::effective_search_arithmetic(requested,sample_fit,drift_sections,differential_window);
    }
    detail::PrivateTemplateReuse acquisition_template_reuse()const {
        // Exact caches remain available at every precision. This separate policy
        // authorizes only the existing bounded approximation; FP64 force and any
        // original-operand numerical fallback must remain independent references.
        const bool permitted=config.search_arithmetic==SearchArithmetic::default_mode ||
            config.search_arithmetic==SearchArithmetic::fp32;
        return permitted && acquisition_arithmetic()==SearchArithmetic::fp32?
            detail::PrivateTemplateReuse::bounded_interpolation:detail::PrivateTemplateReuse::exact_only;
    }
    Complex arithmetic_value(Complex value)const {
        if(acquisition_arithmetic()==SearchArithmetic::fp32)
            return {static_cast<float>(value.real()),static_cast<float>(value.imag())};
        return value;
    }
    Complex arithmetic_product(Complex a,Complex b)const {
        if(acquisition_arithmetic()==SearchArithmetic::fp32) {
            const auto value=std::complex<float>{static_cast<float>(a.real()),static_cast<float>(a.imag())}*
                std::complex<float>{static_cast<float>(b.real()),static_cast<float>(b.imag())};
            return {value.real(),value.imag()};
        }
        return a*b;
    }
    void prepare_templates(std::uint64_t index,std::stop_token stop) {
        if(index>std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol())throw Error("pattern stream coordinate overflow");
        if(streamed_templates) {code.prepare_symbol(index*code.chips_per_symbol(),stop);return;}
        const auto cached=!cached_templates.empty();
        if(cached?template_bank(index).valid:(templates_valid&&prepared_template_index==index))return;
        code.prepare_symbol(index*code.chips_per_symbol(),stop);
        templates_valid=false;
        auto& rows=cached?template_bank(index).rows:templates;
        auto& energies=cached?template_bank(index).energy:template_energy;
        auto& squares=cached?template_bank(index).square:template_square;
        for(std::size_t f=0;f<templates.size();++f) {
                    if(!acquisition_lane(f))continue;
            for(unsigned bit=0;bit<2;++bit) {
                auto& row=rows[f][bit];row.resize(transform);
                std::fill(row.begin(),row.end(),Complex{});
                energies[f][bit]=0;squares[f][bit]={};
            }
            for(std::size_t i=0;i<length;++i) {
                const auto pair=template_values(code,i,index,f);
                for(unsigned bit=0;bit<2;++bit) {
                    const auto value=arithmetic_value(pair[bit]);
                    rows[f][bit][length-1-i]=std::conj(value);
                    energies[f][bit]+=std::norm(value);
                    if(sample_fit)squares[f][bit]+=value*value*carrier_square(i);
                }
            }
            for(unsigned bit=0;bit<2;++bit)
                pattern_fft(rows[f][bit],false,stop,acquisition_arithmetic());
        }
        prepared_template_index=index;templates_valid=true;
        if(cached)template_bank(index).valid=true;
    }
    bool exact_initial_cache_eligible() const {
        if(!(shaped && config.scramble && !config.dsss &&
            config.dsss_factor==1 && !config.fake_hopping && !config.scheduled_hopping &&
            !config.symbol_burst.enabled() && !legacy_public_pattern(config) &&
            config.spreading_mode==SpreadingMode::pattern && length<=ExactInitialRows::maximum_bins &&
            search.initial_stream_symbols>1 && search.initial_stream_symbols<=ExactInitialRows::maximum_symbols &&
            !sample_fit && !variable_clock && drift_sections==1 && !differential_window &&
            detail::search_concurrency(search.worker_threads)>=2 &&
            acquisition_arithmetic()==SearchArithmetic::fp64 && std::fegetround()==FE_TONEAREST))return false;
        if(!search.search_stream_phases && config.stream_phase_samples!=0)return false;
        for(std::size_t f=0;f<templates.size();++f)if(clock_ratio(f)==1)return true;
        return false;
    }
    void reserve_exact_initial_rows(std::size_t bytes) {
        // Preserve the original transformed banks while they fit. A small raw
        // owner is optional fallback only after those banks are absent.
        if(exact_initial_rows || !cached_templates.empty() || !exact_initial_cache_eligible() ||
           !cache_fits(bytes,sizeof(ExactInitialRows)))return;
        try {
            exact_initial_rows=std::make_unique<ExactInitialRows>();
            exact_initial_rows->count=search.initial_stream_symbols;
            cache_reservation+=sizeof(ExactInitialRows);
        } catch(const std::bad_alloc&) {
            // The original uncached route retains its workspace and coverage.
        }
    }
    bool drop_exact_initial_rows() {
        if(!exact_initial_rows)return false;
        cache_reservation-=sizeof(ExactInitialRows);exact_initial_rows.reset();return true;
    }
    void drop_template_cache() {
        exact_initial_rows.reset();
        std::vector<CachedTemplates>().swap(cached_templates);
        if(!required_tracking_reference)std::vector<std::array<Complex,2>>().swap(tracking_reference);
        templates_valid=false;tracking_reference_valid=false;cache_reservation=0;
        if(cached_from_streamed) {
            // Fixed reservation deliberately omitted ordinary per-lane rows.
            // Eviction must restore that original bounded execution policy,
            // not materialize unreserved rows on the next PCM push.
            streamed_templates=true;cached_from_streamed=false;drop_scoring();
        }
    }
    detail::FftSearchGeometry scoring_geometry()const {
        detail::FftSearchGeometry geometry;
        geometry.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),
            shaped?1U:0U,config.scramble?1U:0U,config.dsss?1U:0U,
            config.spreading_seed,config.dsss_seed,config.dsss_factor,
            static_cast<std::uint32_t>(config.outer_dsss_version)};
        geometry.pattern.legacy_public=legacy_public_pattern(config);
        if(config.fake_hopping && config.fake_hopping->matched_gaps) {
            const auto& plan=*config.fake_hopping;
            geometry.pattern.fhss_matched_gaps=1;geometry.pattern.fhss_dwell=plan.dwell_seconds;
            geometry.pattern.fhss_clock=plan.clock_error_seconds;geometry.pattern.fhss_peer_clock=plan.peer_clock_error_seconds;
            geometry.pattern.fhss_audio=plan.audio_error_seconds;geometry.pattern.fhss_region=plan.region_seconds;
        }
        geometry.bins_per_symbol=length;geometry.bin_samples=bin_samples;
        geometry.carrier_hz=config.carrier_hz;geometry.evidence_count=evidence_count(length);
        geometry.noise_condition=noise_condition;geometry.real_rank=real_rank;geometry.sample_fit=sample_fit;
        geometry.extended_clock_window=variable_clock;
        geometry.drift_sections=drift_sections;
        geometry.differential_window_samples=differential_window;
        geometry.search_arithmetic=static_cast<std::uint32_t>(acquisition_arithmetic());
        geometry.private_template_reuse=acquisition_template_reuse();
        return geometry;
    }
    void prepare_scoring(std::stop_token stop,bool allow_exact=true) {
        if(!scoring.empty()) {
            if(!scoring.front().jobs.empty())return;
            // A tracking-only owner is optional scratch from an earlier
            // symbol in this push. Give acquisition its original allowance.
            drop_scoring();
        }
        const auto workers=detail::search_concurrency(search.worker_threads);
        if(workers<2){drop_exact_initial_rows();return;}
        if(allow_exact)reserve_exact_initial_rows(budget);
        const auto selected_pairs=acquisition_mask.empty()?templates.size():
            static_cast<std::size_t>(std::count_if(acquisition_mask.begin(),acquisition_mask.end(),[](auto value){return value!=0;}));
        const auto jobs=search.initial_stream_symbols*selected_pairs*(phase_upper?3:1);
        const bool needs_code=drift_sections>1 || (cached_templates.empty() && !reuse_single_template());
        const auto worker_bytes=sizeof(detail::FftSearchWorkspace)+
            (needs_code?code.working_bytes():0)+transform*sizeof(Complex)+
            (drift_sections>1?hop*sizeof(detail::FftDriftAccumulator):0)+
            (differential_window?hop*sizeof(detail::DifferentialAccumulator):0);
        const auto job_bytes=sizeof(detail::FftSearchJob)+sizeof(detail::FftPreparedTemplate)+
            hop*sizeof(detail::FftSearchScore);
        long double retained=static_cast<long double>(fixed_reservation)+cache_reservation+acquisition_mask.capacity()+latest.bits.capacity();
        for(const auto& track:tracks)retained+=track.burst.bits.capacity();
        for(const auto& burst:bursts)retained+=burst.bits.capacity();
        if(retained>=budget || budget-static_cast<std::size_t>(retained)<=sizeof(ScoringStorage)) {
            if(drop_exact_initial_rows())prepare_scoring(stop,false);
            return;
        }
        auto spare=budget-static_cast<std::size_t>(retained)-sizeof(ScoringStorage);
        const auto uncached_workers=std::min({workers,jobs,spare/(worker_bytes+job_bytes)});
        bool cache_nominal=false;
        if(long_symbol && streamed_templates && !config.scramble && !config.dsss &&
           !(config.fake_hopping&&config.fake_hopping->matched_gaps) &&
           config.spreading_mode==SpreadingMode::pattern && uncached_workers>=2) {
            std::size_t nominal_jobs=0;
            for(std::size_t f=0;f<templates.size();++f)if(acquisition_lane(f) && clock_ratio(f)==1)++nominal_jobs;
            const auto bytes=static_cast<long double>(length)*sizeof(std::array<Complex,2>);
            // Reserve the optional waveform before filling spare memory with
            // FFT job outputs. Keep every affordable physical worker and at
            // least one job per worker; only logical batch capacity may shrink.
            if(nominal_jobs>uncached_workers && bytes<=spare-uncached_workers*(worker_bytes+job_bytes)) {
                spare-=static_cast<std::size_t>(bytes);cache_nominal=true;
            }
        }
        const auto worker_count=std::min({workers,jobs,spare/(worker_bytes+job_bytes)});
        if(worker_count<2) {
            if(drop_exact_initial_rows())prepare_scoring(stop,false);
            return;
        }
        // Logical batch capacity depends on the search and RAM, not the CPU
        // count. Thousands of jobs can share one dispatch; only CPU-private
        // transform/cache storage is allocated per physical worker slot.
        const auto count=std::min(jobs,(spare-worker_count*worker_bytes)/job_bytes);
        scoring.resize(1);
        auto& storage=scoring.front();
        auto& scoring_jobs=storage.jobs;auto& scoring_templates=storage.templates;
        auto& scoring_outputs=storage.outputs;auto& scoring_workspaces=storage.workspaces;
        if(cache_nominal)storage.nominal_reference.resize(length);
        scoring_jobs.resize(count);scoring_templates.resize(count);
        scoring_outputs.resize(count*hop);scoring_workspaces.reserve(worker_count);
        for(std::size_t i=0;i<worker_count;++i)scoring_workspaces.emplace_back(config,transform,needs_code,
            drift_sections>1?hop:0,differential_window?hop:0);
        scoring_reservation=scoring.capacity()*sizeof(ScoringStorage)+scoring_jobs.capacity()*sizeof(detail::FftSearchJob)+
            scoring_templates.capacity()*sizeof(detail::FftPreparedTemplate)+
            scoring_outputs.capacity()*sizeof(detail::FftSearchScore)+
            scoring_workspaces.capacity()*sizeof(detail::FftSearchWorkspace)+
            storage.nominal_reference.capacity()*sizeof(decltype(storage.nominal_reference)::value_type);
        for(const auto& workspace:scoring_workspaces)scoring_reservation+=workspace.working_bytes();
        // A vector implementation may reserve more than requested. Optional
        // batching must still fit its measured capacities on that platform.
        if(!cache_fits(budget,0)) {
            drop_scoring();if(drop_exact_initial_rows())prepare_scoring(stop,false);return;
        }
        auto& prepared_peak=acquisition_mask.empty()?fft_workload->original_prepared_jobs_peak:
            fft_workload->conditional_prepared_jobs_peak;
        auto& scoring_peak=acquisition_mask.empty()?fft_workload->original_scoring_peak_bytes:
            fft_workload->conditional_scoring_peak_bytes;
        prepared_peak=std::max(prepared_peak,scoring_jobs.capacity());
        scoring_peak=std::max(scoring_peak,scoring_reservation);
        if(cache_nominal)detail::prepare_fft_nominal_reference(scoring_geometry(),code,storage.nominal_reference,stop);
        if(exact_initial_rows && !exact_initial_rows->valid && needs_code && !scoring_workspaces.empty() &&
           scoring_workspaces.front().code && std::fegetround()==FE_TONEAREST) {
            auto& owner=*exact_initial_rows;
            auto& generator=*scoring_workspaces.front().code;
            const auto geometry=scoring_geometry();
            generator.set_stream_phase_samples(0);
            for(std::size_t symbol=0;symbol<owner.count;++symbol) {
                cancelled(stop);
                generator.prepare_symbol(symbol*code.chips_per_symbol(),stop);
                auto row=std::span(owner.values).subspan(symbol*length,length);
                for(unsigned bit=0;bit<2;++bit)for(std::size_t i=0;i<length;++i) {
                    if((i&4095U)==0)cancelled(stop);
                    const auto position=static_cast<long double>(i)*bin_samples+
                        static_cast<long double>(bin_samples-1)/2;
                    row[i][bit]=generator.shaped_value(symbol*code.chips_per_symbol(),bit,static_cast<double>(position));
                }
                auto& view=owner.references[symbol];
                view.pattern=geometry.pattern;view.symbol=symbol;view.phase=0;
                view.bins=length;view.bin_samples=bin_samples;view.values=row;
            }
            cancelled(stop);owner.valid=true;
            audio::trace::record("pattern_exact_initial_rows",[&](auto& fields) {
                fields.add("epoch",config.stream_epoch);fields.add("symbols",owner.count);
                fields.add("bins",length);fields.add("bytes",sizeof(ExactInitialRows));
                fields.add("workers",scoring_workspaces.size());fields.add("workspace",budget);
            });
        }
    }
    void drop_scoring() {
        decltype(scoring)().swap(scoring);scoring_reservation=0;
    }
    std::vector<PatternCode>* prepare_tracking(std::size_t jobs) {
        if(!long_symbol || jobs<2)return nullptr;
        const auto workers=std::min(jobs,detail::search_concurrency(search.worker_threads));
        if(workers<2)return nullptr;
        if(!scoring.empty() && !scoring.front().tracking_workers.empty())
            return &scoring.front().tracking_workers;
        const auto worker_bytes=code.working_bytes();
        const auto minimum=2*worker_bytes+(scoring.empty()?sizeof(ScoringStorage):0);
        // FFT jobs have joined before continuation starts. Their optional
        // transform scratch may leave no room even for two small private
        // pattern caches; release it only when that space is needed.
        if(!cache_fits(budget,minimum) && !scoring.empty())drop_scoring();
        long double retained=static_cast<long double>(fixed_reservation)+cache_reservation+acquisition_mask.capacity()+
            scoring_reservation+latest.bits.capacity()+(scoring.empty()?sizeof(ScoringStorage):0);
        for(const auto& track:tracks)retained+=track.burst.bits.capacity();
        for(const auto& burst:bursts)retained+=burst.bits.capacity();
        if(retained>=budget)return nullptr;
        const auto count=std::min(workers,(budget-static_cast<std::size_t>(retained))/worker_bytes);
        if(count<2)return nullptr;
        if(scoring.empty()) {
            scoring.resize(1);
            scoring_reservation=scoring.capacity()*sizeof(ScoringStorage);
        }
        auto& storage=scoring.front();
        storage.tracking_workers.reserve(count);
        for(std::size_t i=0;i<count;++i)storage.tracking_workers.emplace_back(config,config.stream_epoch);
        storage.tracking_reservation=storage.tracking_workers.capacity()*sizeof(PatternCode);
        for(const auto& worker:storage.tracking_workers)
            storage.tracking_reservation+=worker.working_bytes()-sizeof(PatternCode);
        scoring_reservation+=storage.tracking_reservation;
        // Include measured capacities in the same optional-cache accounting
        // used by acquisition and payload growth. The push scope releases all
        // worker caches on normal return or cancellation, preserving idle RAM.
        if(!cache_fits(budget,0)){drop_scoring();return nullptr;}
        return &scoring.front().tracking_workers;
    }
    void ensure_execution_scratch() {
        if(!work.empty())return;
        // fixed_reservation includes these buffers even while idle. Retain
        // that active-work bound and the original bit-capacity proof; Live
        // loans remaining bank workspace to one receiver at a time.
        work.resize(transform);spectrum.resize(transform);product.resize(transform);
        reference.resize(transform);energy_prefix.resize(transform+1);
    }
    void release_idle_scratch() {
        invalidate_phase_cache();
        // These are the last initial-symbol transforms, not detector state.
        // Unwindowed multi-symbol acquisition rebuilds its initial rows on
        // the next batch; do not retain one row set per idle peer after its
        // multi-symbol cache was evicted. Keep useful full caches and the
        // original active-work reservation, including this reconstruction.
        if(search.release_idle_fft_scratch && !acquisition_window &&
           search.initial_stream_symbols>1 && cached_templates.empty()) {
            for(auto& row:templates)for(auto& values:row)std::vector<Complex>().swap(values);
            templates_valid=false;
        }
        if(!search.prefer_streamed_templates || !search.release_idle_fft_scratch || !tracks.empty())return;
        for(auto* buffer:{&work,&spectrum,&product,&reference})std::vector<Complex>().swap(*buffer);
        std::vector<double>().swap(energy_prefix);
        // product can hold a cached tracking row between calls.
        if(tracking_reference.empty())tracking_reference_valid=false;
    }
    struct ScoringScope {
        Impl& state;
        ~ScoringScope() {state.drop_scoring();state.release_idle_scratch();}
    };
    bool reuse_single_template() const {
        return drift_sections<=1 && !streamed_templates && search.initial_stream_symbols==1 &&
            (!search.search_stream_phases || !phase_upper || (!config.scramble&&!config.dsss));
    }
    bool cache_fits(std::size_t bytes,std::size_t extra)const {
        // Retain the original fixed reservation, including scratch/copy
        // headroom, when deciding whether optional caches can coexist with
        // payload growth. The original uncached bit-capacity proof still holds.
        long double required=static_cast<long double>(fixed_reservation)+cache_reservation+scoring_reservation+
            acquisition_mask.capacity()+extra+latest.bits.capacity();
        for(const auto& track:tracks)required+=track.burst.bits.capacity();
        for(const auto& burst:bursts)required+=burst.bits.capacity();
        return required+native_plan_bytes()<=bytes;
    }
    void room_for_bits(std::size_t extra) {
        // Acquisition only collects pre-reserved peaks. Payload admission occurs
        // after all native scratch is destroyed, so plans can be evicted here.
        if(native_active_bytes)throw Error("native FFT scratch crossed admission boundary");
        if(!acquisition_mask.empty() && !cache_fits(budget,extra)) {
            const auto execution_origin=frequency_origin_hz;
            clear_prior(!prior_pending_count && !prior_dispatch);
            // A completed ordinary fit still owns its RF basis while its bit
            // is appended. Only subsequent fresh acquisition is withdrawn.
            frequency_origin_hz=execution_origin;
        }
        if(!cache_fits(budget,extra)){native_float_plan.reset();native_double_plan.reset();}
        if(!scoring.empty() && !cache_fits(budget,extra))drop_scoring();
        if((!cached_templates.empty() || exact_initial_rows) && !cache_fits(budget,extra))drop_template_cache();
    }
    Complex template_value(std::size_t bin,std::uint64_t index,unsigned bit,std::size_t f) {
        return template_value(code,bin,index,bit,f);
    }
    double frequency_offset(std::size_t f)const {return search.frequency_offsets_hz[f]+frequency_origin_hz;}
    bool acquisition_lane(std::size_t f)const {return acquisition_mask.empty() || acquisition_mask[f]!=0;}
    bool link_matches(double origin,std::span<const std::uint8_t> mask)const {
        if(link_evidence.serial<=prior_link_floor || mask.size()!=search.hypotheses.size() ||
           link_evidence.phase_view!=fresh_phase_view())return false;
        const auto target=link_evidence.frequency_offset_hz-origin;
        double nearest=std::numeric_limits<double>::infinity(),center=0;
        double minimum=std::numeric_limits<double>::infinity(),maximum=-std::numeric_limits<double>::infinity();
        const auto tolerance=64*std::numeric_limits<double>::epsilon()*
            std::max({1.,std::abs(target),std::abs(origin),std::abs(link_evidence.frequency_offset_hz)});
        for(const auto& h:search.hypotheses)if(h.clock_error_ppm==link_evidence.clock_error_ppm) {
            minimum=std::min(minimum,h.frequency_offset_hz);maximum=std::max(maximum,h.frequency_offset_hz);
            const auto distance=std::abs(h.frequency_offset_hz-target);
            if(distance<nearest){nearest=distance;center=h.frequency_offset_hz;}
        }
        if(!std::isfinite(nearest) || target<minimum-tolerance || target>maximum+tolerance)return false;
        double spacing=std::numeric_limits<double>::infinity();
        for(std::size_t f=0;f<search.hypotheses.size();++f) {
            const auto& h=search.hypotheses[f];if(h.clock_error_ppm!=link_evidence.clock_error_ppm)continue;
            if(std::abs(std::abs(h.frequency_offset_hz-target)-nearest)<=tolerance && !mask[f])return false;
            const auto gap=std::abs(h.frequency_offset_hz-center);
            if(gap>tolerance)spacing=std::min(spacing,gap);
        }
        // No endpoint extrapolation and no singleton confidence certificate.
        // This is only a conservative association with the existing cells.
        return nearest<=tolerance || (std::isfinite(spacing) && nearest<=spacing/2+tolerance);
    }
    void record_link(std::size_t f,double origin) {
        if(search.hypotheses.empty())return;
        link_evidence={accepted_serial,search.frequency_offsets_hz[f]+origin,search.hypotheses[f].clock_error_ppm,execution_phase_view};
    }
    void restore_prior_schedule() {
        if(!prior_schedule_saved)return;
        search.early_qualified_fft=prior_early_qualified;
        search.component_qualified_search=prior_component_qualified;
        prior_schedule_saved=false;
    }
    void clear_prior(bool restore_schedule=true) {
        decltype(acquisition_mask)().swap(acquisition_mask);
        prior_link_floor=accepted_serial;
        frequency_origin_hz=acquisition_origin_hz=0;drop_scoring();drop_template_cache();
        templates_valid=tracking_reference_valid=false;
        if(restore_schedule)restore_prior_schedule();
    }
    Complex template_value(PatternCode& pattern,std::size_t bin,std::uint64_t index,unsigned bit,std::size_t f) const {
        const auto sample_position=static_cast<long double>(bin)*bin_samples+static_cast<long double>(bin_samples-1)/2;
        const auto source_position=sample_position*clock_ratio(f);
        if(variable_clock && source_position>=code.symbol_samples())return {};
        const auto chip_position=source_position/code.chip_samples();
        const auto local=static_cast<std::uint64_t>(chip_position);
        if(index>(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol())throw Error("pattern stream coordinate overflow");
        const auto chip=index*code.chips_per_symbol()+local;
        const auto fraction=static_cast<double>(chip_position-local);
        // The raw observations remain disjoint PCM bins. Match the shaped
        // symbol contribution, without adding a receive filter whose correlated
        // output would incorrectly count as independent noise observations.
        // Adjacent unknown symbols are not used as timing or bit evidence.
        // Public patterns repeat independently of stream index and phase.
        // Reuse an already-budgeted acquisition cache during continuation
        // when present; this path never allocates a cache just for tracking.
        const bool cached=clock_ratio(f)==1 && !scoring.empty() && !scoring.front().nominal_reference.empty();
        const auto value=cached?scoring.front().nominal_reference[bin][bit]:shaped?
            pattern.shaped_value(index*code.chips_per_symbol(),bit,static_cast<double>(source_position)):
            pattern.value(chip,bit,fraction);
        return value*std::polar(1.,tau*frequency_offset(f)*static_cast<double>(sample_position)/config.sample_rate);
    }
    std::array<Complex,2> template_values(PatternCode& pattern,std::size_t bin,std::uint64_t index,std::size_t f) const {
        const auto sample_position=static_cast<long double>(bin)*bin_samples+static_cast<long double>(bin_samples-1)/2;
        const auto source_position=sample_position*clock_ratio(f);
        if(variable_clock && source_position>=code.symbol_samples())return {};
        const auto chip_position=source_position/code.chip_samples();
        const auto local=static_cast<std::uint64_t>(chip_position);
        if(index>(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol())throw Error("pattern stream coordinate overflow");
        const auto chip=index*code.chips_per_symbol()+local;
        const auto fraction=static_cast<double>(chip_position-local);
        // Keep the nominal reference, coordinate guards and carrier endpoint
        // identical to template_value while sharing PatternCode's paired pulse work.
        const bool cached=clock_ratio(f)==1 && !scoring.empty() && !scoring.front().nominal_reference.empty();
        std::array<Complex,2> values;
        if(cached)values={scoring.front().nominal_reference[bin][0],scoring.front().nominal_reference[bin][1]};
        else if(shaped)values=pattern.shaped_values(index*code.chips_per_symbol(),static_cast<double>(source_position));
        else values=pattern.values(chip,fraction);
        const auto carrier=std::polar(1.,tau*frequency_offset(f)*static_cast<double>(sample_position)/config.sample_rate);
        values[0]=values[0]*carrier;values[1]=values[1]*carrier;
        return values;
    }
    bool phase_geometry_supported()const noexcept {
        return !fallback && !calendar_symbols() && bin_samples && !sample_fit && !differential_window && !real_rank;
    }
    const PatternPhaseCurve* fresh_phase_view()const noexcept {
        if(!phase_geometry_supported() || !phase_curve || !phase_curve->view_id() ||
           phase_curve->sample_rate()!=config.sample_rate || bin_samples>phase_curve->maximum_bin_samples())return nullptr;
        const auto first=static_cast<long double>(search.input_sample_origin)+static_cast<long double>(next_start)*bin_samples;
        const auto end=static_cast<long double>(search.input_sample_origin)+sample;
        return first>=phase_curve->first_sample() && end<=phase_curve->qualified_until()?phase_curve:nullptr;
    }
    void invalidate_phase_cache()noexcept {cached_phase_view=nullptr;phase_cache_count=0;}
    Complex raw_at(std::uint64_t i)const {
        if(i>=bins || bins-i>ring.size())throw Error("pattern observation expired before refinement");
        return ring[static_cast<std::size_t>(i%ring.size())];
    }
    Complex phase_weight(std::uint64_t i)const {
        if(!execution_phase_view)return {1,0};
        if(cached_phase_view==execution_phase_view && i>=phase_cache_first && i-phase_cache_first<phase_cache_count)
            return reference[static_cast<std::size_t>(i-phase_cache_first)];
        const auto center=static_cast<long double>(search.input_sample_origin)+static_cast<long double>(i)*bin_samples+(bin_samples-1)/2.L;
        return std::polar(1.,-std::remainder(execution_phase_view->value_at(center),tau));
    }
    Complex at(std::uint64_t i)const {
        if(!execution_phase_view)return raw_at(i);
        if(cached_phase_view==execution_phase_view && i>=phase_cache_first && i-phase_cache_first<phase_cache_count)
            return spectrum[static_cast<std::size_t>(i-phase_cache_first)];
        return raw_at(i)*phase_weight(i);
    }
    void prepare_phase_window(std::uint64_t first,std::size_t count,std::stop_token stop) {
        if(!execution_phase_view)return;
        if(cached_phase_view==execution_phase_view && first>=phase_cache_first &&
           first-phase_cache_first<=phase_cache_count && count<=phase_cache_count-static_cast<std::size_t>(first-phase_cache_first))return;
        ensure_execution_scratch();invalidate_phase_cache();
        if(count>spectrum.size() || count>reference.size())throw Error("phase-view window exceeds original FFT scratch");
        for(std::size_t i=0;i<count;++i) {
            if((i&4095U)==0)cancelled(stop);
            const auto weight=phase_weight(first+i);
            spectrum[i]=raw_at(first+i)*weight;reference[i]=weight;
            if(weight!=Complex{1,0})++fft_workload->phase_corrected_bins;
        }
        phase_cache_first=first;phase_cache_count=count;cached_phase_view=execution_phase_view;
    }
    Complex carrier_square(std::uint64_t bin)const {
        const auto phase=std::remainder(2*static_cast<long double>(tau)*config.carrier_hz*
            (static_cast<long double>(bin)*bin_samples+(bin_samples-1)/2.L)/config.sample_rate,
            static_cast<long double>(tau));
        return std::polar(1.,static_cast<double>(phase));
    }
    double evidence_count(std::size_t observed)const {
        const auto count=static_cast<double>(observed);
        // Finer timing must not turn a held wrong-key chip into many fresh
        // random observations. Preserve the former half-chip evidence scale
        // (two complex bins, or four real dimensions, per chip) while fitting
        // the waveform against every available sample.
        return sample_fit?std::min(count,4*count/static_cast<double>(code.chip_samples())):count;
    }
    bool competing_frequencies()const {
        return search.couple_clock_to_carrier ||
            (!search.hypotheses.empty() && search.frequency_rate_competition);
    }
    double clock_ratio(std::size_t frequency)const {
        if(!search.hypotheses.empty())return 1+search.hypotheses[frequency].clock_error_ppm*1e-6;
        return search.couple_clock_to_carrier && frequency>=search.uncoupled_frequency_count?
            1+search.frequency_offsets_hz[frequency]/config.carrier_hz:1.;
    }
    std::uint64_t symbol_bins(std::size_t frequency)const {
        if(!variable_clock)return length;
        return std::max<std::uint64_t>(1,static_cast<std::uint64_t>(std::llround(
            static_cast<long double>(code.symbol_samples())/bin_samples/
            clock_ratio(frequency))));
    }
    double threshold_at(long double original_trials)const {
        // Alpha-spending-style threshold under the reference noise model.
        // Actual PCM quadrature covariance and adaptive paths need calibration;
        // this tuning parameter is not a certified lifetime false-alarm bound.
        // Optional off-catalogue RF references have a separate, smaller
        // allowance. They never raise the original-origin Robust threshold.
        const bool shifted=frequency_origin_hz!=0 || execution_phase_view;
        const auto t=static_cast<double>(shifted?std::max(original_trials,static_cast<long double>(shifted_trials)):original_trials)+1;
        return -std::log(search.false_alarm_probability)+(shifted?std::log(64.):0.)+2*std::log(t)+
            std::log(2.*static_cast<double>(templates.size()*search.initial_stream_symbols));
    }
    double threshold()const {return threshold_at(trials);}
    double continuation_penalty(std::uint64_t radius=2)const {
        // Continuation can select either bit at each timing and carrier.
        // Other carriers reuse a timing chosen from PCM, so charge the full
        // timing/carrier envelope rather than only the executed fits. This
        // retains the reference score model, not a structured-cover guarantee.
        if(radius==2)return std::log(10.*static_cast<double>(templates.size()));
        const auto timing=2.L*(2.L*radius+1);
        return static_cast<double>(std::log(timing*templates.size()));
    }
    std::uint64_t track_radius(const Track& track) const {
        if(!packed_calendar && !burst_plan)return 2;
        const auto distance=source_start(track.index,track.phase_lower)-source_start(track.anchor_index,track.phase_lower);
        const auto radius=2+std::ceil(static_cast<long double>(distance)*packed_rate_displacement/bin_samples);
        if(!std::isfinite(radius) || radius>std::numeric_limits<std::uint64_t>::max()/2 ||
           radius>(ring.size()-std::min(ring.size(),length))/2)
            throw Error("packed complex continuation timing interval exceeds retained workspace");
        return static_cast<std::uint64_t>(radius);
    }
    std::uint64_t track_start(const Track& track,std::uint64_t index) const {
        if(!packed_calendar && !burst_plan)return track.next+advance_bins(track.index,track.phase_lower,track.frequency);
        const auto delta=source_start(index,track.phase_lower)-source_start(track.anchor_index,track.phase_lower);
        const auto offset=std::round(static_cast<long double>(delta)/clock_ratio(track.frequency)/bin_samples);
        if(!std::isfinite(offset) || offset>std::numeric_limits<std::uint64_t>::max()-track.anchor_start)
            throw Error("packed complex continuation coordinate would overflow");
        return track.anchor_start+static_cast<std::uint64_t>(offset);
    }
    std::pair<long double,long double> original_burst_rates() const {
        return {burst_policy->minimum_rate,burst_policy->maximum_rate};
    }
    bool burst_anchor_allowed(const Track& track,std::uint64_t start,
            long double minimum_rate,long double maximum_rate) const {
        auto window=acquisition_window;
        auto geometry=window_geometry(track.index,{track.phase_lower,track.phase_upper});
        if(!window) {
            // Public rolling acquisition has no placeholder-epoch timing
            // prior. Its first accepted physical anchor is immutable instead.
            const auto first=track.burst.stream_first_symbol;
            const auto distance=source_start(track.index,track.phase_lower)-source_start(first,track.phase_lower);
            window=PatternStartWindow{static_cast<long double>(track.burst.stream_first_sample),1,0};
            geometry={std::max<std::uint64_t>(1,distance),distance?1U:0U,0,0,1};
        }
        const auto source=static_cast<long double>(geometry.samples)*geometry.index;
        if(!std::isfinite(minimum_rate) || !std::isfinite(maximum_rate) || minimum_rate<=0 ||
           minimum_rate>maximum_rate || !detail::pattern_fft_window_finite(*window,geometry.upper) ||
           source>=1e15L || static_cast<long double>(start)*bin_samples>=1e15L)return false;
        // Reuse the ORIGINAL full FFT-cell/refinement enclosure. This is not a
        // newly centered clock cone after each accepted continuation.
        return detail::pattern_fft_select_starts(*window,geometry.samples,geometry.index,
            geometry.lower,geometry.upper,geometry.step,start,1,bin_samples,minimum_rate,maximum_rate).selected_count!=0;
    }
    void advance_track(Track& track,std::optional<std::uint64_t> accepted={}) {
        if(packed_calendar || burst_plan) {
            if(burst_plan && accepted) {
                const auto [lower,upper]=original_burst_rates();
                if(lower==upper)accepted.reset(); // Exact GPS/rate cells retain the original physical anchor.
                else if(!burst_anchor_allowed(track,*accepted,lower,upper)) {
                    burst_policy->capture_revoked=true;accepted.reset();
                }
            }
            if(accepted){track.anchor_start=*accepted;track.anchor_index=track.index;}
            track.next=track_start(track,symbol_schedule_detail::add(track.index,1));
        } else track.next=accepted.value_or(track.next)+advance_bins(track.index,track.phase_lower,track.frequency);
        track.index=symbol_schedule_detail::add(track.index,1);
    }
    void remember(PatternEvidence item) {
        if(item.score<search.retain_score)return;
        item.admission_threshold=threshold();
        if(diagnostic_tail)diagnostic_tail->append(history,item);
        else if(history.capacity()) {
            const auto limit=helper_overlap?helper_overlap->history_limit:search.candidate_limit;
            if(history.size()==limit)history.erase(history.begin());
            history.push_back(item);
        }
        ++history_insertions;
    }
    bool ensure_helper_overlap() {
        if(helper_overlap)return true;
        // The helper may surrender diagnostics, never candidate peaks or a
        // checkpoint needed by the original restricted/component scheduler.
        if(fallback || calendar_symbols() || fft_workload->restricted_timing_active ||
           component_hop || early_eof_pending || (diagnostic_tail && diagnostic_tail->checkpoint_bytes()))return false;
        const auto released=history.capacity()*sizeof(PatternEvidence);
        const auto minimum=sizeof(HelperOverlapStorage)+search.track_limit*sizeof(std::optional<HelperOverlapStorage::PublishedSource>);
        if(minimum>released)return false;
        constexpr std::size_t retained_limit=128;
        std::array<PatternEvidence,retained_limit> saved;
        const auto saved_count=std::min(history.size(),retained_limit);
        std::copy_n(history.end()-static_cast<std::ptrdiff_t>(saved_count),saved_count,saved.begin());
        const auto old_limit=diagnostic_tail?diagnostic_tail->limit():search.candidate_limit;
        const auto planned_limit=std::min(old_limit,(released-minimum)/sizeof(PatternEvidence));
        if(diagnostic_tail && !diagnostic_tail->rebind_materialized_limit(planned_limit))return false;
        // Free first: the bank's actual working allocation cannot grow even
        // transiently by retaining the old diagnostic array beside metadata.
        std::vector<PatternEvidence>().swap(history);
        try {
            auto storage=std::make_unique<HelperOverlapStorage>();
            storage->completed.resize(search.track_limit);
            const auto bytes=storage->working_bytes();
            if(bytes>released) {
                if(diagnostic_tail)diagnostic_tail->rebind_materialized_limit(0);
                return false;
            }
            const auto limit=std::min(old_limit,(released-bytes)/sizeof(PatternEvidence));
            history.reserve(limit);
            if(history.capacity()*sizeof(PatternEvidence)>released-bytes) {
                std::vector<PatternEvidence>().swap(history);
                if(diagnostic_tail)diagnostic_tail->rebind_materialized_limit(0);
                return false;
            }
            storage->history_limit=limit;
            const auto count=std::min(saved_count,limit);
            history.insert(history.end(),saved.begin()+static_cast<std::ptrdiff_t>(saved_count-count),
                           saved.begin()+static_cast<std::ptrdiff_t>(saved_count));
            if(diagnostic_tail && !diagnostic_tail->rebind_materialized_limit(limit)) {
                std::vector<PatternEvidence>().swap(history);
                if(diagnostic_tail)diagnostic_tail->rebind_materialized_limit(0);
                return false;
            }
            helper_overlap=std::move(storage);return true;
        } catch(const std::bad_alloc&) {
            // An optional diagnostic allocation may yield completely. With
            // zero capacity remember() cannot regrow it behind the bank grant.
            std::vector<PatternEvidence>().swap(history);
            if(diagnostic_tail)diagnostic_tail->rebind_materialized_limit(0);
            return false;
        }
    }
    std::optional<PatternSourceIdentity> source_identity(std::uint64_t index,std::uint64_t phase)const try {
        if(!helper_overlap || fallback || calendar_symbols() || !(config.scramble || config.dsss) ||
           !config.sample_rate || phase>=config.sample_rate || search.hypotheses.empty())return std::nullopt;
        auto lower=phase,upper=phase;
        if(search.search_stream_phases) {
            // Continuation only narrows its birth phase group. Recover that
            // ORIGINAL partition at the immutable birth index from the current
            // representative; never pair a newly exact phase with old PCM.
            std::size_t count=0;
            const auto groups=phase_groups(index,0,phase_upper,count);
            bool found=false;
            for(std::size_t g=0;g<count;++g)if(phase>=groups[g].lower && phase<=groups[g].upper) {
                lower=groups[g].lower;upper=canonical_phase_cell_upper(config,groups[g].upper);found=true;break;
            }
            if(!found)return std::nullopt;
        }
        long double minimum=1,maximum=1;
        if(search.admitted_clock_half_width_ppm) {
            const auto radius=static_cast<long double>(*search.admitted_clock_half_width_ppm)*1e-6L;
            if(!std::isfinite(radius) || radius<0 || radius>=1)return std::nullopt;
            minimum=1-radius;maximum=1+radius;
        }
        auto minimum_step=UINT64_MAX,maximum_step=std::uint64_t{0};
        for(std::size_t f=0;f<templates.size();++f) {
            const auto rate=static_cast<long double>(clock_ratio(f));
            minimum=std::min(minimum,rate);maximum=std::max(maximum,rate);
            const auto step=symbol_bins(f);
            if(step>UINT64_MAX/bin_samples)return std::nullopt;
            minimum_step=std::min(minimum_step,step*bin_samples);
            maximum_step=std::max(maximum_step,step*bin_samples);
        }
        if(bin_samples>UINT64_MAX/2 || minimum_step<=2*bin_samples || maximum_step>UINT64_MAX-2*bin_samples)return std::nullopt;
        const auto symbol=static_cast<long double>(code.symbol_samples());
        minimum=std::min(minimum,symbol/(maximum_step+2*bin_samples));
        maximum=std::max(maximum,symbol/(minimum_step-2*bin_samples));
        if(2.L*bin_samples*maximum>symbol)return std::nullopt;
        const auto radius=std::max(1-minimum,maximum-1)*1e6L;
        if(!std::isfinite(radius) || radius>=1e6L || upper>=config.sample_rate)return std::nullopt;
        return PatternSourceIdentity{config.stream_epoch,index,
            std::nextafter(static_cast<double>(radius),std::numeric_limits<double>::infinity()),
            static_cast<std::uint32_t>(lower),static_cast<std::uint32_t>(upper),config.sample_rate};
    } catch(const Error&) {return std::nullopt;}
    bool independent_overlap(const PatternBurst& old,const PatternEvidence& item,bool helper_candidate)const {
        if(!(old.helper_acquired || helper_candidate) || item.score<threshold())return false;
        const auto a=source_identity(old.stream_first_symbol,old.stream_phase_samples);
        const auto b=source_identity(item.stream_symbol,item.stream_phase_samples);
        return a && b && pattern_source_identities_disjoint(*a,old.stream_first_sample,old.end_sample,
            code.symbol_samples(),*b,item.first_sample,item.end_sample,code.symbol_samples());
    }
    bool independent_overlap(std::size_t span_index,const PatternEvidence& item)const {
        if(!helper_overlap || span_index>=completed.size() || item.score<threshold())return false;
        const auto& identity=helper_overlap->completed[span_index];
        if(!identity)return false; // Unknown ordinary published spans retain their original veto.
        const auto candidate=source_identity(item.stream_symbol,item.stream_phase_samples);
        const auto& span=completed[span_index];
        return candidate && pattern_source_identities_disjoint(identity->identity,identity->first,span.end,code.symbol_samples(),
            *candidate,item.first_sample,item.end_sample,code.symbol_samples());
    }
    std::array<std::size_t,5> drift_edges(std::size_t frequency) const {
        std::array<std::size_t,5> edges{};edges.back()=length;
        for(unsigned section=1;section<drift_sections;++section) {
            const auto boundary=static_cast<long double>(detail::drift_boundary(section,code.symbol_samples(),drift_sections));
            const auto bin=std::ceil((boundary/clock_ratio(frequency)-(bin_samples-1)/2.L)/bin_samples);
            edges[section]=static_cast<std::size_t>(std::clamp(bin,static_cast<long double>(edges[section-1]),static_cast<long double>(length)));
        }
        return edges;
    }
    struct SectionFits {
        std::array<std::array<Complex,2>,4> dot{},square{};
        std::array<std::array<double,2>,4> norm{};
        void add(unsigned section,unsigned bit,Complex observed,Complex pattern,Complex carrier,bool exact_real) {
            dot[section][bit]+=observed*std::conj(pattern);norm[section][bit]+=std::norm(pattern);
            if(exact_real)square[section][bit]+=pattern*pattern*carrier;
        }
        double explained(unsigned bit,double condition,bool exact_real) const {
            double total=0,strongest=0;
            for(unsigned section=0;section<4;++section) {
                const auto energy=detail::pattern_explained(dot[section][bit],norm[section][bit],condition,exact_real,square[section][bit]);
                total+=energy;strongest=std::max(strongest,energy);
            }
            return total-strongest;
        }
    };
    struct DifferentialFits {
        std::array<detail::DifferentialAccumulator,2> accumulated;
        std::array<Complex,2> dot{},square{};
        std::array<double,2> norm{};
        std::uint64_t window=0,total=0,index=0;
        std::size_t bin_samples=0,length=0,end=0;
        double ratio=1,image_ratio=0;
        bool complete=false;
        std::size_t boundary(std::uint64_t position)const {
            const auto bin=std::ceil((static_cast<long double>(position)/ratio-(bin_samples-1)/2.L)/bin_samples);
            // One beyond the available bins keeps a truncated final local
            // window unfinished, instead of publishing a partial fit.
            return static_cast<std::size_t>(std::clamp(bin,0.L,static_cast<long double>(length)+1));
        }
        DifferentialFits(const Impl& owner,std::size_t frequency,std::size_t skip)
            :window(owner.differential_window),total(owner.code.symbol_samples()),bin_samples(owner.bin_samples),
             length(owner.length),ratio(owner.clock_ratio(frequency)),image_ratio(owner.projection_image_ratio) {
            if(!window || skip==length)return;
            const auto source=(static_cast<long double>(skip)*bin_samples+(bin_samples-1)/2.L)*ratio;
            index=static_cast<std::uint64_t>(source/window);
            if(index>=total/window)return;
            complete=skip==boundary(index*window);
            end=boundary((index+1)*window);
        }
        void add(unsigned bit,Complex observed,Complex pattern,Complex carrier) {
            if(!window || !complete || index>=total/window)return;
            dot[bit]+=observed*std::conj(pattern);norm[bit]+=std::norm(pattern);
            square[bit]+=pattern*pattern*carrier;
        }
        void finish_bin(std::size_t next) {
            if(!window || !end || index>=total/window || next<end)return;
            if(complete)for(unsigned bit=0;bit<2;++bit)
                accumulated[bit].add(detail::pattern_differential_whiten(dot[bit],norm[bit],square[bit],image_ratio),
                                     index,total,window);
            dot={};square={};norm={};complete=true;
            if(++index<total/window)end=boundary((index+1)*window);
        }
        double combine(double existing,unsigned bit)const {
            return detail::combine_differential_evidence(existing,accumulated[bit].score(),window!=0);
        }
    };
    void prepare_tracking_reference(std::uint64_t index,std::size_t f,std::stop_token stop) {
        // Timing refinements of one stream symbol fit the same templates.
        // Reuse their exact values; only the received window and carrier Gram
        // phase change. Without optional cache space, the idle acquisition
        // product buffer has room for both interleaved reference rows.
        if(!tracking_reference_valid || tracking_reference_index!=index || tracking_reference_frequency!=f) {
            tracking_reference_valid=false;
            if(index>std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol())throw Error("pattern stream coordinate overflow");
            code.prepare_symbol(index*code.chips_per_symbol(),stop);
            for(std::size_t i=0;i<length;++i) {
                if((i&4095U)==0)cancelled(stop);
                const auto values=template_values(code,i,index,f);
                if(tracking_reference.empty()) {
                    product[2*i]=values[0];product[2*i+1]=values[1];
                } else tracking_reference[i]=values;
            }
            tracking_reference_index=index;tracking_reference_frequency=f;tracking_reference_valid=true;
        }
    }
    double temporal_symbol_support(const PatternEvidence& item,std::size_t f,
                                   std::uint64_t observed_before_bins,std::stop_token stop) {
        const auto begin=std::max(item.first_sample,observed_before_bins*bin_samples);
        const auto old=pattern_symbol_support(static_cast<double>(item.end_sample>begin?item.end_sample-begin:0),
            item.score,code.chip_samples());
        // Admission and physical absence keep their original whole-symbol
        // statistics. This extra presentation ceiling applies only to complex
        // Burst fits whose phase is coherent within each local partition.
        if(!(old>0) || !burst_plan || differential_window)return old;
        const auto edges_for=[&](unsigned parts) {
            std::array<std::size_t,9> edges{};edges[parts]=length;
            for(unsigned j=1;j<parts;++j) {
                const auto boundary=static_cast<long double>(detail::drift_boundary(j,code.symbol_samples(),parts));
                const auto bin=std::ceil((boundary/clock_ratio(f)-(bin_samples-1)/2.L)/bin_samples);
                edges[j]=static_cast<std::size_t>(std::clamp(bin,
                    static_cast<long double>(edges[j-1]),static_cast<long double>(length)));
            }
            return edges;
        };
        // Eighths nest the existing quarter-phase model. Use fewer nested
        // partitions when the native fit has too few independent observations;
        // physical sample counts are not a substitute for evidence_count().
        unsigned parts=8;auto edges=edges_for(parts);
        while(parts>1) {
            bool usable=true;
            for(unsigned j=0;j<parts;++j)usable=usable && evidence_count(edges[j+1]-edges[j])>=4;
            if(usable)break;
            parts/=2;edges=edges_for(parts);
        }
        if(parts==1)return old;
        stream_phase(item.stream_phase_samples);
        prepare_tracking_reference(item.stream_symbol,f,stop);
        struct LocalFit {Complex dot{},square{};double energy=0,norm=0;std::size_t bins=0;};
        std::array<LocalFit,8> local{};
        const auto start=item.first_sample/bin_samples;
        const auto skip=static_cast<std::size_t>(observed_before_bins>start?
            std::min<std::uint64_t>(length,observed_before_bins-start):0);
        unsigned part=0;
        for(std::size_t i=skip;i<length;++i) {
            if((i&4095U)==0)cancelled(stop);
            while(part+1<parts && i>=edges[part+1])++part;
            auto& fit=local[part];const auto value=at(start+i);
            const auto pattern=tracking_reference.empty()?product[2*i+item.bit]:tracking_reference[i][item.bit];
            fit.dot+=value*std::conj(pattern);fit.norm+=std::norm(pattern);fit.energy+=std::norm(value);++fit.bins;
            if(sample_fit)fit.square+=pattern*pattern*carrier_square(start+i);
        }
        double supported=0;std::array<double,8> scores{};
        for(unsigned j=0;j<parts;++j) {
            const auto& fit=local[j];
            const auto score=pattern_evidence(fit.dot,fit.energy,fit.norm,evidence_count(fit.bins),
                noise_condition,real_rank,sample_fit,fit.square);
            scores[j]=score;
            supported+=pattern_symbol_support(static_cast<double>(fit.bins)*bin_samples,score,code.chip_samples());
        }
        audio::trace::record("symbol_burst_temporal_support",[&](auto& fields) {
            fields.add("epoch",config.stream_epoch);fields.add("keyed",config.data_key.has_value());
            fields.add("symbol_samples",code.symbol_samples());fields.add("chip_samples",code.chip_samples());
            fields.add("sample_origin",search.input_sample_origin);fields.add("index",item.stream_symbol);
            fields.add("begin",item.first_sample);fields.add("end",item.end_sample);
            fields.add("observed_before",observed_before_bins*bin_samples);
            fields.add("bit",item.bit);fields.add("frequency_hypothesis",f);fields.add("clock_ratio",clock_ratio(f));
            fields.add("partitions",parts);fields.add("old_support",old);
            fields.add("temporal_support",supported);fields.add("applied_support",std::min(old,supported));
            for(unsigned j=0;j<parts;++j) {
                const auto prefix="part"+std::to_string(j)+"_";
                fields.add((prefix+"known_samples").c_str(),local[j].bins*bin_samples);
                fields.add((prefix+"evidence_count").c_str(),evidence_count(local[j].bins));
                fields.add((prefix+"score").c_str(),scores[j]);
                fields.add((prefix+"support").c_str(),pattern_symbol_support(
                    static_cast<double>(local[j].bins)*bin_samples,scores[j],code.chip_samples()));
            }
        });
        // Strong cells cannot transfer surplus into a silent cell, or increase
        // the original confidence weight. Missing/reused bins contribute zero.
        return std::min(old,supported);
    }
    PatternEvidence measure(std::uint64_t start,std::uint64_t index,std::size_t f,std::uint64_t observed_before,
                            std::span<const Complex> carrier_phases,std::stop_token stop,
                            const PatternEvidence* trace_expected=nullptr) {
        if(execution_phase_view)++fft_workload->phase_view_tracking_fits;
        std::array<Complex,2> dot{},square{};std::array<double,2> norm{};double energy=0;
        SectionFits sections;const auto edges=drift_edges(f);unsigned section=0;
        prepare_tracking_reference(index,f,stop);
        const auto skip=static_cast<std::size_t>(observed_before>start?std::min<std::uint64_t>(length,observed_before-start):0);
        DifferentialFits differential(*this,f,skip);
        for(std::size_t i=skip;i<length;++i) {
            if(drift_sections>1)while(section+1<drift_sections && i>=edges[section+1])++section;
            const auto value=at(start+i);energy+=std::norm(value);
            const auto carrier=sample_fit || differential_window?carrier_phases[i]:Complex{};
            for(unsigned b=0;b<2;++b) {
                const auto pattern=tracking_reference.empty()?product[2*i+b]:tracking_reference[i][b];
                dot[b]+=value*std::conj(pattern);norm[b]+=std::norm(pattern);
                if(sample_fit)square[b]+=pattern*pattern*carrier;
                if(drift_sections>1)sections.add(section,b,value,pattern,carrier,sample_fit);
                differential.add(b,value,pattern,carrier);
            }
            differential.finish_bin(i+1);
        }
        const auto count=evidence_count(length-skip);
        auto zero=pattern_evidence(dot[0],energy,norm[0],count,noise_condition,real_rank,sample_fit,square[0]),
            one=pattern_evidence(dot[1],energy,norm[1],count,noise_condition,real_rank,sample_fit,square[1]);
        if(drift_sections>1) {
            zero=detail::combine_drift_evidence(zero,detail::drift_evidence(sections.explained(0,noise_condition,sample_fit),energy,count,drift_sections,real_rank),drift_sections);
            one=detail::combine_drift_evidence(one,detail::drift_evidence(sections.explained(1,noise_condition,sample_fit),energy,count,drift_sections,real_rank),drift_sections);
        }
        zero=differential.combine(zero,0);one=differential.combine(one,1);
        // Every candidate was scored through the common padded observation
        // end. Keep that end in evidence so continuation excludes all reused
        // bins, including the noise-only tail of a faster rate alternative.
        PatternEvidence result{start*bin_samples,(start+length)*bin_samples,index,
            config.carrier_hz+frequency_offset(f),std::max(zero,one),std::min(zero,one),one>zero?1U:0U,active_stream_phase};
        result.frequency_hypothesis=static_cast<std::uint32_t>(f);
        if(trace_expected)audio::trace::record("symbol_burst_fft_fit",[&](auto& fields) {
            fields.add("stage","selected continuation before track update; trace recomputes cached fit");
            fields.add("epoch",config.stream_epoch);fields.add("keyed",config.data_key.has_value());
            fields.add("symbol_samples",code.symbol_samples());fields.add("chip_samples",code.chip_samples());
            fields.add("sample_origin",search.input_sample_origin);fields.add("index",index);
            fields.add("begin",result.first_sample);fields.add("end",result.end_sample);
            fields.add("frequency_hz",result.frequency_hz);fields.add("stream_phase_samples",active_stream_phase);
            fields.add("frequency_hypothesis",f);fields.add("clock_ratio",clock_ratio(f));
            fields.add("trials",trials);fields.add("threshold",threshold());
            fields.add("observed_before",observed_before*bin_samples);fields.add("fft_bins",length);
            fields.add("skipped_bins",skip);fields.add("bin_samples",bin_samples);
            fields.add("known_samples",(length-skip)*bin_samples);fields.add("evidence_count",count);
            fields.add("energy",energy);fields.add("noise_condition",noise_condition);
            fields.add("real_rank",real_rank);fields.add("sample_fit",sample_fit);
            fields.add("drift_sections",drift_sections);fields.add("differential_window",differential_window);
            fields.add("bit",result.bit);fields.add("score",result.score);fields.add("alternative_score",result.alternative_score);
            fields.add("expected_bit",trace_expected->bit);fields.add("expected_score",trace_expected->score);
            fields.add("expected_alternative_score",trace_expected->alternative_score);
            fields.add("recomputed_exact",result.bit==trace_expected->bit && result.score==trace_expected->score &&
                result.alternative_score==trace_expected->alternative_score);
            for(unsigned bit=0;bit<2;++bit) {
                const auto prefix=std::string("fit")+std::to_string(bit)+"_";
                fields.add((prefix+"dot_real").c_str(),dot[bit].real());fields.add((prefix+"dot_imag").c_str(),dot[bit].imag());
                fields.add((prefix+"norm").c_str(),norm[bit]);
                fields.add((prefix+"square_real").c_str(),square[bit].real());fields.add((prefix+"square_imag").c_str(),square[bit].imag());
                fields.add((prefix+"explained").c_str(),detail::pattern_explained(dot[bit],norm[bit],noise_condition,sample_fit,square[bit]));
                fields.add((prefix+"coherent_score").c_str(),pattern_evidence(dot[bit],energy,norm[bit],count,noise_condition,real_rank,sample_fit,square[bit]));
                if(drift_sections>1)fields.add((prefix+"section_explained").c_str(),sections.explained(bit,noise_condition,sample_fit));
            }
        });
        return result;
    }
    std::array<double,2> measure_streamed(PatternCode& pattern,std::uint64_t start,std::uint64_t index,
                                        std::size_t f,std::uint64_t observed_before,
                                        std::span<const Complex> carrier_phases,std::stop_token stop)const {
        std::array<Complex,2> dot{},square{};std::array<double,2> norm{};double energy=0;
        SectionFits sections;const auto edges=drift_edges(f);unsigned section=0;
        const auto skip=static_cast<std::size_t>(observed_before>start?std::min<std::uint64_t>(length,observed_before-start):0);
        DifferentialFits differential(*this,f,skip);
        // Preserve measure()'s sample/bit accumulation order, but generate a
        // template pair immediately before using it. Each worker owns only a
        // bounded PatternCode cache, not a symbol-sized reference or FFT row.
        if(index>std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol())throw Error("pattern stream coordinate overflow");
        pattern.prepare_symbol(index*code.chips_per_symbol(),stop);
        for(std::size_t i=skip;i<length;++i) {
            if((i&4095U)==0)cancelled(stop);
            if(drift_sections>1)while(section+1<drift_sections && i>=edges[section+1])++section;
            const auto value=at(start+i);energy+=std::norm(value);
            const auto carrier=sample_fit || differential_window?carrier_phases[i]:Complex{};
            for(unsigned b=0;b<2;++b) {
                const auto reference_value=template_value(pattern,i,index,b,f);
                dot[b]+=value*std::conj(reference_value);norm[b]+=std::norm(reference_value);
                if(sample_fit)square[b]+=reference_value*reference_value*carrier;
                if(drift_sections>1)sections.add(section,b,value,reference_value,carrier,sample_fit);
                differential.add(b,value,reference_value,carrier);
            }
            differential.finish_bin(i+1);
        }
        const auto count=evidence_count(length-skip);
        std::array result{pattern_evidence(dot[0],energy,norm[0],count,noise_condition,real_rank,sample_fit,square[0]),
                          pattern_evidence(dot[1],energy,norm[1],count,noise_condition,real_rank,sample_fit,square[1])};
        if(drift_sections>1)for(unsigned bit=0;bit<2;++bit)
            result[bit]=detail::combine_drift_evidence(result[bit],detail::drift_evidence(
                sections.explained(bit,noise_condition,sample_fit),energy,count,drift_sections,real_rank),drift_sections);
        for(unsigned bit=0;bit<2;++bit)result[bit]=differential.combine(result[bit],bit);
        return result;
    }
    // Serial coherent continuation repeats the same shaped source row at
    // every RF cell sharing an exact clock ratio. Keep all original fits and
    // their arithmetic, using only the idle acquisition product as scratch.
    template<class Eligible>
    bool measure_clock_groups(std::uint64_t start,std::uint64_t index,std::uint64_t observed_before,
                              std::size_t current,Eligible eligible,
                              std::span<std::array<double,2>> scores,std::stop_token stop) {
        if(drift_sections!=1 || differential_window || sample_fit || calendar_symbols() ||
           product.size()/2<length || scores.size()!=templates.size())return false;
        std::array<std::size_t,16> representatives{};
        std::size_t groups=0,jobs=0;
        for(std::size_t f=0;f<templates.size();++f)if(f!=current && eligible(f)) {
            ++jobs;std::size_t group=0;
            while(group<groups && clock_ratio(representatives[group])!=clock_ratio(f))++group;
            if(group==groups) {
                if(groups==representatives.size())return false;
                representatives[groups++]=f;
            }
        }
        if(jobs<4 || jobs<2*groups)return false;
        if(index>std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol())
            throw Error("pattern stream coordinate overflow");
        // With no dedicated tracking row, product is also the ordinary
        // template cache. Its old tag cannot survive this scratch use, even
        // on cancellation or an exception.
        struct ScratchScope {
            Impl& state;
            explicit ScratchScope(Impl& value):state(value) {
                if(state.tracking_reference.empty())state.tracking_reference_valid=false;
            }
            ~ScratchScope(){if(state.tracking_reference.empty())state.tracking_reference_valid=false;}
        } scratch_scope{*this};
        const auto skip=static_cast<std::size_t>(observed_before>start?
            std::min<std::uint64_t>(length,observed_before-start):0);
        for(std::size_t group=0;group<groups;++group) {
            cancelled(stop);const auto representative=representatives[group];
            const auto ratio=clock_ratio(representative);
            code.prepare_symbol(index*code.chips_per_symbol(),stop);
            std::size_t valid=length;
            for(std::size_t i=0;i<length;++i) {
                if((i&4095U)==0)cancelled(stop);
                const auto sample_position=static_cast<long double>(i)*bin_samples+static_cast<long double>(bin_samples-1)/2;
                const auto source_position=sample_position*ratio;
                if(variable_clock && source_position>=code.symbol_samples()){valid=i;break;}
                const auto chip_position=source_position/code.chip_samples();
                const auto local=static_cast<std::uint64_t>(chip_position);
                if(index>(std::numeric_limits<std::uint64_t>::max()-local)/code.chips_per_symbol())
                    throw Error("pattern stream coordinate overflow");
                const auto chip=index*code.chips_per_symbol()+local;
                const auto fraction=static_cast<double>(chip_position-local);
                const bool cached=ratio==1 && !scoring.empty() && !scoring.front().nominal_reference.empty();
                std::array<Complex,2> values;
                if(cached)values={scoring.front().nominal_reference[i][0],scoring.front().nominal_reference[i][1]};
                else if(shaped)values=code.shaped_values(index*code.chips_per_symbol(),static_cast<double>(source_position));
                else values=code.values(chip,fraction);
                product[2*i]=values[0];product[2*i+1]=values[1];
            }
            for(std::size_t f=0;f<templates.size();++f) {
                if(f==current || !eligible(f) || clock_ratio(f)!=ratio)continue;
                cancelled(stop);
                std::array<Complex,2> dot{};std::array<double,2> norm{};double energy=0;
                for(std::size_t i=skip;i<length;++i) {
                    if((i&4095U)==0)cancelled(stop);
                    const auto value=at(start+i);energy+=std::norm(value);
                    std::array<Complex,2> patterns{};
                    if(i<valid) {
                        const auto sample_position=static_cast<long double>(i)*bin_samples+static_cast<long double>(bin_samples-1)/2;
                        const auto carrier=std::polar(1.,tau*frequency_offset(f)*static_cast<double>(sample_position)/config.sample_rate);
                        patterns={product[2*i]*carrier,product[2*i+1]*carrier};
                    }
                    for(unsigned bit=0;bit<2;++bit) {
                        dot[bit]+=value*std::conj(patterns[bit]);norm[bit]+=std::norm(patterns[bit]);
                    }
                }
                const auto count=evidence_count(length-skip);
                scores[f]={pattern_evidence(dot[0],energy,norm[0],count,noise_condition,real_rank,false,Complex{}),
                           pattern_evidence(dot[1],energy,norm[1],count,noise_condition,real_rank,false,Complex{})};
                if(execution_phase_view)++fft_workload->phase_view_tracking_fits;
            }
        }
        return true;
    }
    bool fine_private_chip_guard()const noexcept {
        return search.outer_presence_guard && bin_samples==1 && real_rank && !sample_fit && !shaped &&
            (config.scramble || config.dsss) && config.dsss_factor<=1 && drift_sections==1 &&
            !differential_window && !config.fake_hopping && !calendar_symbols();
    }
    bool outer_presence_check(PatternEvidence& evidence,std::uint64_t observed_before,std::stop_token stop,
            double* chain_score=nullptr,bool established_continuation=false) {
        if(chain_score)*chain_score=evidence.score;
        // Fresh acquisition and every original standalone decision retain
        // their complete original score, including declared-cell mismatch.
        const bool chip_guard=established_continuation && evidence.score<threshold() && fine_private_chip_guard();
        const bool outer_guard=search.outer_presence_guard && config.dsss_factor>1 &&
            drift_sections==1 && !differential_window;
        if((!chip_guard && !outer_guard) || evidence.score<search.retain_score)return true;
        const auto start=evidence.first_sample/bin_samples,index=evidence.stream_symbol;
        const auto f=evidence.frequency_hypothesis;
        const auto skip=static_cast<std::size_t>(observed_before>start?std::min<std::uint64_t>(length,observed_before-start):0);
        prepare_phase_window(start,length,stop);
        stream_phase(evidence.stream_phase_samples);
        detail::CorrelationOuterEvidence outer;
        std::array<detail::CorrelationFit,2> full{};
        std::array<detail::CorrelationChipEvidence,2> chips{};
        if(index>std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol())throw Error("outer chip coordinate overflow");
        const auto first_chip=index*code.chips_per_symbol();
        const auto coefficients=[&](std::uint64_t local) {
            auto values=code.values(first_chip+local);
            for(auto& value:values)value=std::conj(value);
            return values;
        };
        const auto omega=tau*config.carrier_hz/config.sample_rate;
        const auto sine=std::sin(omega);
        const auto image=std::abs(sine)>1e-12?std::sin(bin_samples*omega)/(bin_samples*sine):1.;
        for(std::size_t i=skip;i<length;++i) {
            if((i&4095U)==0)cancelled(stop);
            const auto sample_position=static_cast<long double>(i)*bin_samples+(bin_samples-1)/2.L;
            const auto source=sample_position*clock_ratio(f);
            const auto observation=at(start+i);
            const auto rotation=std::polar(1.,tau*frequency_offset(f)*static_cast<double>(sample_position)/config.sample_rate);
            if(outer_guard && source<code.symbol_samples()) {
                const auto measured=observation*std::conj(rotation);
                if(shaped)pattern_pulse_each(static_cast<double>(source),code.symbol_samples(),code.chip_samples(),
                    [&](std::uint64_t local,double pulse){outer.add(local,pulse*measured,coefficients);});
                else outer.add(static_cast<std::uint64_t>(source/code.chip_samples()),measured,coefficients);
            }
            const auto local=static_cast<std::uint64_t>(std::max(0.L,source/code.chip_samples()));
            auto gamma=image*std::conj(carrier_square(start+i));
            if(execution_phase_view) {const auto weight=phase_weight(start+i);gamma*=weight*weight;}
            const auto determinant=1-std::norm(gamma);
            double a=0,b=0,c=0;std::uint64_t rank=2;
            if(!sample_fit) {
                if(determinant>1e-12) {
                    a=(1-gamma.real())/determinant;b=-gamma.imag()/determinant;c=(1+gamma.real())/determinant;
                } else {
                    const auto angle=.5*std::arg(gamma),x=std::cos(angle),y=std::sin(angle);
                    const auto lambda=1+std::abs(gamma);a=x*x/lambda;b=x*y/lambda;c=y*y/lambda;rank=1;
                }
            }
            for(unsigned bit=0;bit<2;++bit) {
                const auto p=template_value(i,index,bit,f);
                detail::CorrelationFit fit;
                if(sample_fit) {
                    const auto dot=observation*std::conj(p),square=p*p*carrier_square(start+i);
                    const auto norm=std::norm(p);
                    fit={dot.real(),-dot.imag(),.5*(norm+square.real()),.5*(norm-square.real()),
                        .5*square.imag(),std::norm(observation),1};
                } else {
                    const auto x=observation.real(),y=observation.imag(),u=p.real(),v=p.imag();
                    const auto wx=a*x+b*y,wy=b*x+c*y;
                    fit={u*wx+v*wy,-v*wx+u*wy,a*u*u+2*b*u*v+c*v*v,
                        a*v*v-2*b*u*v+c*u*u,b*(u*u-v*v)+(c-a)*u*v,x*wx+y*wy,rank};
                }
                auto& total=full[bit];total.xc+=fit.xc;total.xs+=fit.xs;total.cc+=fit.cc;
                total.ss+=fit.ss;total.cs+=fit.cs;total.energy+=fit.energy;total.count+=fit.count;
                chips[bit].add_fit(fit,local);
            }
        }
        if(chip_guard) {
            // A fine real-sample grid is not fresh independent evidence when
            // the input is a held-chip private cover. Keep the original AWGN
            // score unless the existing nested-subspace test independently
            // qualifies strong colored excess. Its finite-noise allowance
            // removes a correctly represented payload at arbitrary strength.
            const auto& full_fit=full[evidence.bit];const auto& chip_fit=chips[evidence.bit];
            const bool colored=chip_fit.colored_excess(full_fit,threshold(),1.);
            if(colored)evidence.score=std::min(evidence.score,chip_fit.score(full_fit));
            if(chain_score)*chain_score=evidence.score;
            return true;
        }
        outer.finish(coefficients);
        // Match the compact receiver's weak-chain null. The shared hop gaps
        // make independently keyed cover strongly nonstationary: raw PCM-count
        // AWGN scores cannot safely accumulate until a stream is established.
        // Per-chip projections preserve a coherent correct signal, include both
        // real quadratures and use their actual rank, including zero pulse cells.
        // Standalone admission and reported raw scores remain unchanged.
        if(chain_score && config.fake_hopping && (config.fake_hopping->matched_gaps||config.fake_hopping->smooth_ramps))
            *chain_score=std::min(evidence.score,chips[evidence.bit].score(full[evidence.bit]));
        evidence.outer_presence_score=outer.score(evidence.bit);
        // Section/differential phase evidence needs its own independent-code
        // statistic; do not silently substitute a canceled coherent dot.
        // Enhanced branches are conservatively left unqualified until their
        // corresponding section-wise independent-code statistic is implemented.
        evidence.outer_presence_active=outer.can_admit(evidence.bit,threshold()) && drift_sections==1 && !differential_window &&
            chips[evidence.bit].colored_excess(full[evidence.bit],threshold(),
                static_cast<double>(full[evidence.bit].count)/std::max(1.,
                    evidence_count(length-skip)*(real_rank?1.:2.))*
                (sample_fit?1.:noise_condition/std::max(1e-12,1-std::abs(image))));
        return !evidence.outer_presence_active || evidence.outer_presence_score>=threshold();
    }
    void publish(Track& track,bool complete,bool flush=false,bool draining=false) {
        if(early_eof_pending && track.established) {
            early_eof_replayable=false;
            if(diagnostic_tail)diagnostic_tail->release_checkpoint();
        }
        track.burst.score=track.confirmed_score;
        track.burst.support_samples=track.confirmed_support;
        if(!track.established)return;
        const auto chunk=std::min(search.chunk_bits,search.bit_limit);
        if(!complete && !flush && track.confirmed<chunk)return;
        do {
            const auto count=std::min(track.confirmed,chunk);
            if(!count && !complete)break;
            if(draining && bursts.size()==search.track_limit)break;
            if(bursts.size()>=search.track_limit)throw Error("pattern output queue requires draining");
            PatternBurst event;
            event.first_sample=track.burst.first_sample;event.first_stream_symbol=track.burst.first_stream_symbol;
            event.stream_first_sample=track.burst.stream_first_sample;event.stream_first_symbol=track.burst.stream_first_symbol;
            event.frequency_hz=track.burst.frequency_hz;event.stream_phase_samples=track.burst.stream_phase_samples;
            event.bits.assign(track.burst.bits.begin(),track.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            event.complete=complete && count==track.confirmed;
            event.helper_acquired=track.burst.helper_acquired;
            event.end_sample=count==track.confirmed?track.confirmed_end:
                !calendar_symbols()?event.first_sample+count*(search.hypotheses.empty()?code.symbol_samples():symbol_bins(track.frequency)*bin_samples):
                event.first_sample+static_cast<std::uint64_t>(std::llround((static_cast<long double>(source_start(event.first_stream_symbol+count-1,track.phase_lower))-
                    source_start(event.first_stream_symbol,track.phase_lower)+code.symbol_samples())/clock_ratio(track.frequency)));
            if(burst_plan && !count)event.first_sample=event.end_sample;
            event.score=track.confirmed_score;
            event.support_samples=track.confirmed_support;
            if(packed_calendar)audio::trace::record(event.complete?"packed_complex_physical_end":"packed_complex_progress",[&](auto& f) {
                f.add("epoch",config.stream_epoch);f.add("sample_rate",config.sample_rate);
                f.add("observed_input_samples",sample);f.add("first_sample",event.first_sample);f.add("end_sample",event.end_sample);
                f.add("first_stream_symbol",event.first_stream_symbol);f.add("accepted_count",count);
                f.add("confirmed_count",track.confirmed);f.add("failed_source_samples",track.absent_samples);
                f.add("unconfirmed_symbols",track.unconfirmed_symbols);f.add("next_slot_bin",track.next);
                f.add("next_continuation_radius_bins",track_radius(track));f.add("trials",trials);
                f.add("score",event.score);f.add("threshold",threshold());f.add("complete",event.complete);
            });
            room_for_bits(3*event.bits.capacity());latest=event;
            if(count) {
                if(completed.size()==search.track_limit) {
                    completed.erase(completed.begin());
                    if(helper_overlap)std::move(helper_overlap->completed.begin()+1,helper_overlap->completed.end(),
                                                 helper_overlap->completed.begin());
                }
                completed.push_back({event.first_sample,event.end_sample,event.frequency_hz});
                if(helper_overlap) {
                    auto& source=helper_overlap->completed[completed.size()-1];source.reset();
                    if(event.helper_acquired)if(const auto identity=source_identity(
                            track.burst.stream_first_symbol,track.burst.stream_phase_samples))
                        source=HelperOverlapStorage::PublishedSource{*identity,track.burst.stream_first_sample};
                }
            }
            bursts.push_back(std::move(event));
            track.burst.bits.erase(track.burst.bits.begin(),track.burst.bits.begin()+static_cast<std::ptrdiff_t>(count));
            track.burst.first_stream_symbol+=count;track.burst.first_sample=bursts.back().end_sample;
            if(count && calendar_symbols()) {
                const auto next=track.burst.first_stream_symbol;
                const auto gap=source_start(next,track.phase_lower)-source_start(next-1,track.phase_lower)-code.symbol_samples();
                track.burst.first_sample+=static_cast<std::uint64_t>(std::llround(static_cast<long double>(gap)/clock_ratio(track.frequency)));
            }
            track.confirmed-=count;
            if(!track.confirmed)break;
        } while(complete || flush || track.confirmed>=chunk);
    }
    void emit_gap(Track& track,std::uint64_t resumed_sample,std::uint64_t resumed_symbol) {
        if(!track.gap_slots)return;
        publish(track,false,true);
        if(bursts.size()>=search.track_limit)throw Error("pattern output queue requires draining");
        PatternBurst event;
        event.first_sample=track.burst.first_sample;event.end_sample=resumed_sample;
        event.first_stream_symbol=track.burst.first_stream_symbol;
        event.stream_first_sample=track.burst.stream_first_sample;event.stream_first_symbol=track.burst.stream_first_symbol;
        event.frequency_hz=track.burst.frequency_hz;event.score=track.confirmed_score;
        event.support_samples=track.confirmed_support;
        event.stream_phase_samples=track.burst.stream_phase_samples;event.missing_slots=track.gap_slots;
        event.helper_acquired=track.burst.helper_acquired;
        bursts.push_back(std::move(event));track.gap_slots=0;
        track.burst.first_sample=resumed_sample;track.burst.first_stream_symbol=resumed_symbol;
    }
    void append_bit(Bytes& bits,std::uint8_t bit) {
        if(bits.size()==search.bit_limit)throw Error("pattern burst exceeds bounded bit capacity");
        if(bits.size()==bits.capacity()) {
            const auto capacity=std::max<std::size_t>(1,bits.size()>search.bit_limit/2?search.bit_limit:2*bits.size());
            room_for_bits(capacity);bits.reserve(capacity);
        }
        bits.push_back(bit);
    }
    bool tracking_frequency_axis(std::uint64_t start,std::uint64_t index,std::uint64_t observed_before,
                                 std::span<std::array<double,2>> scores,std::stop_token stop) {
        if(!frequency_axis_selected || drift_sections!=4 || differential_window || sample_fit)return false;
        const auto skip=static_cast<std::size_t>(observed_before>start?
            std::min<std::uint64_t>(length,observed_before-start):0);
        if(skip==length)return false;
        const auto job_bytes=templates.size()*sizeof(detail::FftSearchJob);
        const auto output_bytes=templates.size()*sizeof(detail::FftSearchScore);
        const auto used=std::max(working_bytes(),fixed_reservation+cache_reservation+scoring_reservation+acquisition_mask.capacity());
        if(used>=budget || job_bytes>=budget-used)return false;
        std::vector<detail::FftSearchJob> jobs(templates.size());
        for(std::size_t f=0;f<jobs.size();++f) {
            jobs[f]={index,active_stream_phase,f,frequency_offset(f)};jobs[f].clock_ratio=clock_ratio(f);
        }
        detail::FftSearchBatch batch;batch.geometry=scoring_geometry();
        batch.geometry.search_arithmetic=static_cast<std::uint32_t>(SearchArithmetic::fp64);
        batch.geometry.evidence_count=evidence_count(length-skip);
        batch.observation_skip=skip;batch.starts=batch.score_stride=1;batch.first_bin=start;
        batch.retain_floor=search.retain_score;batch.refine_threshold=threshold();
        batch.refine_threshold_max=threshold_at(static_cast<long double>(trials)+templates.size());
        const auto requirements=detail::frequency_axis::choose_geometry(batch.geometry,jobs,1,1,
            budget-used-job_bytes,detail::search_concurrency(search.worker_threads),false,skip);
        if(!requirements.supported() || !cache_fits(budget,job_bytes+output_bytes+requirements.scratch_bytes)) {
            ++fft_workload->frequency_axis_tracking_fallbacks;return false;
        }
        // Reuse idle acquisition storage for one original PCM interval. Skip
        // only evidence already charged to the preceding symbol; retain the
        // source phase and original physical quarter boundaries.
        energy_prefix[0]=0;
        for(std::size_t i=0;i<length;++i) {
            if((i&4095U)==0)cancelled(stop);
            work[i]=i<skip?Complex{}:at(start+i);
            energy_prefix[i+1]=energy_prefix[i]+std::norm(work[i]);
        }
        batch.observations=std::span(work).first(length);batch.energy_prefix=std::span(energy_prefix).first(length+1);
        const auto before=arithmetic_reservation;
        struct Reservation {std::size_t& value;std::size_t before;~Reservation(){value=before;}} reservation{arithmetic_reservation,before};
        arithmetic_reservation+=job_bytes+output_bytes+requirements.scratch_bytes;
        std::vector<detail::FftSearchScore> output(jobs.size());detail::frequency_axis::Work measured;
        if(!detail::frequency_axis::execute(batch,jobs,requirements,output,code,&measured,stop))
            throw Error("tracking frequency-axis execution left its preflight domain");
        for(std::size_t f=0;f<scores.size();++f)scores[f]={output[f].zero,output[f].one};
        ++fft_workload->frequency_axis_tracking_batches;
        fft_workload->frequency_axis_transforms+=measured.forward_transforms+measured.inverse_transforms;
        fft_workload->frequency_axis_refinements+=measured.refined_cells;
        fft_workload->frequency_axis_peak_bytes=std::max(fft_workload->frequency_axis_peak_bytes,job_bytes+measured.peak_bytes);
        return true;
    }
    void continue_tracks(std::stop_token stop,bool final=false,
                         std::uint64_t latest_start=std::numeric_limits<std::uint64_t>::max()) {
        for(auto it=tracks.begin();it!=tracks.end();) {
            cancelled(stop);auto& track=*it;bool ended=false;
            // Acquisition and each established/provisional track retain their
            // own RF reference. A broad origin-zero audit cannot reinterpret
            // an already accepted track or its clock lanes.
            struct OriginScope {
                Impl& state;double previous;const PatternPhaseCurve* previous_view;
                OriginScope(Impl& value,double origin,const PatternPhaseCurve* view):
                        state(value),previous(value.frequency_origin_hz),previous_view(value.execution_phase_view) {
                    if(previous!=origin)state.tracking_reference_valid=false;
                    state.frequency_origin_hz=origin;state.execution_phase_view=view;
                }
                ~OriginScope(){if(state.frequency_origin_hz!=state.acquisition_origin_hz)state.tracking_reference_valid=false;
                    state.frequency_origin_hz=state.acquisition_origin_hz;state.execution_phase_view=previous_view;}
            } origin_scope{*this,track.frequency_origin_hz,track.phase_view};
            while(track.next<=latest_start) {
                const auto radius=track_radius(track);
                const auto lookahead=final && !packed_calendar && !burst_plan?0:radius;
                if(track.next>std::numeric_limits<std::uint64_t>::max()-length-lookahead ||
                   track.next+length+lookahead>bins)break;
                ensure_execution_scratch();
                PatternEvidence best;best.score=-1;std::uint64_t chosen=track.next;
                std::size_t group_count=0;
                const auto groups=phase_groups(track.index,track.phase_lower,track.phase_upper,group_count);
                PhaseGroup selected{track.phase_lower,track.phase_upper};
                std::vector<std::array<double,2>> frequency_scores(templates.size());
                auto selected_frequency=track.frequency;
                const auto low=track.next>radius?track.next-radius:0;
                const auto high=std::min(track.next+radius,bins-length);
                prepare_phase_window(low,static_cast<std::size_t>(length+high-low),stop);
                // Acquisition is finished while tracks run. Its work buffer
                // fits all five overlapping timing windows, whose exact
                // carrier Gram phases also repeat across frequency fits.
                const bool wide_carriers=(sample_fit || differential_window) && length+high-low>work.size();
                if((sample_fit || differential_window) && !wide_carriers)
                    for(std::size_t i=0;i<length+high-low;++i)work[i]=carrier_square(low+i);
                auto carrier_start=std::numeric_limits<std::uint64_t>::max();
                const auto carrier_phases=[&](std::uint64_t start) {
                    if(!sample_fit && !differential_window)return std::span<const Complex>{};
                    if(!wide_carriers)return std::span<const Complex>(work).subspan(static_cast<std::size_t>(start-low),length);
                    if(carrier_start!=start) {
                        for(std::size_t i=0;i<length;++i)reference[i]=carrier_square(start+i);
                        carrier_start=start;
                    }
                    return std::span<const Complex>(reference).first(length);
                };
                for(std::size_t g=0;g<group_count;++g) {
                    stream_phase(groups[g].lower);
                    // A physical null can separate the clock lanes by many
                    // bins. Evaluate the complete bank in that timing interval;
                    // frequency-first order reuses each exact template pair.
                    const auto first_frequency=(packed_calendar || burst_plan)?0:track.frequency;
                    const auto end_frequency=(packed_calendar || burst_plan)?templates.size():track.frequency+1;
                    for(auto f=first_frequency;f<end_frequency;++f)for(auto start=low;start<=high;++start) {
                        if(bins-start>ring.size())continue;
                        auto fit=measure(start,track.index,f,track.burst.end_sample/bin_samples,
                            carrier_phases(start),stop);if(frequency_origin_hz!=0 || execution_phase_view)++shifted_trials;else ++trials;
                        if(packed_calendar || burst_plan) {
                            frequency_scores[f][fit.bit]=std::max(frequency_scores[f][fit.bit],fit.score);
                            frequency_scores[f][1-fit.bit]=std::max(frequency_scores[f][1-fit.bit],fit.alternative_score);
                        }
                        if(fit.score>best.score){best=fit;chosen=start;selected=groups[g];selected_frequency=f;}
                    }
                }
                // Acquisition can select a neighboring carrier from one
                // distorted startup symbol. Compare the same disjoint PCM
                // against the finite carrier bank as reception continues;
                // frequency is evidence accumulated over the stream, not a
                // permanent label inherited from its first admitted peak.
                stream_phase(selected.lower);
                const auto timing_fit=best;
                const auto chosen_carriers=carrier_phases(chosen);
                const auto eligible=[&](std::size_t f) {
                    if(packed_calendar || burst_plan)return false; // Full bank already scored above.
                    return competing_frequencies() || track.established || std::abs(frequency_offset(f)-frequency_offset(track.frequency))<=
                        static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
                };
                std::size_t jobs=0;
                for(std::size_t f=0;f<templates.size();++f)if(f!=track.frequency && eligible(f))++jobs;
                const bool axis=jobs>=1023 && jobs*5>=templates.size()*4 &&
                    tracking_frequency_axis(chosen,track.index,track.burst.end_sample/bin_samples,frequency_scores,stop);
                auto* workers=axis?nullptr:prepare_tracking(jobs);
                const bool grouped=!axis && !workers && measure_clock_groups(chosen,track.index,
                    track.burst.end_sample/bin_samples,track.frequency,eligible,frequency_scores,stop);
                double axis_max=best.score;
                if(axis)for(std::size_t f=0;f<templates.size();++f)if(eligible(f))
                    axis_max=std::max({axis_max,frequency_scores[f][0],frequency_scores[f][1]});
                if(workers) {
                    // Only disjoint score slots and private pattern caches are
                    // writable while the ring and carrier phases are shared.
                    // All trials, ties and admission stay in the serial loop.
                    detail::parallel_search_ranges(templates.size(),workers->size(),1,
                        [&](std::size_t worker,std::size_t begin,std::size_t end) {
                            auto& pattern=(*workers)[worker];pattern.set_stream_phase_samples(selected.lower);
                            for(auto f=begin;f<end;++f) {
                                cancelled(stop);
                                if(f==track.frequency || !eligible(f))continue;
                                frequency_scores[f]=measure_streamed(pattern,chosen,track.index,f,
                                    track.burst.end_sample/bin_samples,chosen_carriers,stop);
                            }
                        });
                    if(execution_phase_view)fft_workload->phase_view_tracking_fits+=jobs;
                }
                for(std::size_t f=0;f<templates.size();++f) {
                    if(!eligible(f))continue;
                    auto fit=timing_fit;
                    if(f!=track.frequency) {
                        if(axis || workers || grouped) {
                            const auto zero=frequency_scores[f][0],one=frequency_scores[f][1];
                            fit={chosen*bin_samples,(chosen+length)*bin_samples,track.index,
                                config.carrier_hz+frequency_offset(f),std::max(zero,one),std::min(zero,one),
                                one>zero?1U:0U,active_stream_phase};
                            fit.frequency_hypothesis=static_cast<std::uint32_t>(f);
                            // Original arithmetic decides the strongest carrier
                            // and near ties, independently of transform rounding.
                            if(axis && fit.score>=axis_max-1e-5*std::max(1.,axis_max)) {
                                fit=measure(chosen,track.index,f,track.burst.end_sample/bin_samples,chosen_carriers,stop);
                                ++fft_workload->frequency_axis_refinements;
                            }
                        } else fit=measure(chosen,track.index,f,track.burst.end_sample/bin_samples,chosen_carriers,stop);
                    }
                    if(f!=track.frequency){if(frequency_origin_hz!=0 || execution_phase_view)++shifted_trials;else ++trials;}
                    frequency_scores[f][fit.bit]=fit.score;
                    frequency_scores[f][1-fit.bit]=fit.alternative_score;
                    if(fit.score>best.score){best=fit;selected_frequency=f;}
                }
                // A weak candidate may migrate onto an already established
                // carrier during continuation. Apply overlap arbitration here
                // as well as at acquisition, before it can publish a duplicate
                // suffix of the same physical stream.
                const auto duplicate_continuation=[&](bool distinct_qualified) {
                    if(track.established)return false;
                    const auto same_frequency=[&](double frequency) {
                        return competing_frequencies() || std::abs(best.frequency_hz-frequency)<=
                            static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
                    };
                    if(std::any_of(tracks.begin(),tracks.end(),[&](const Track& other) {
                        return &other!=&track && other.established && !other.pending_gap && !other.unconfirmed_symbols &&
                            same_frequency(other.burst.frequency_hz) &&
                            track.burst.stream_first_sample>=other.burst.stream_first_sample &&
                            best.first_sample<=other.next*bin_samples+2*bin_samples &&
                            best.end_sample>other.burst.stream_first_sample &&
                            !(distinct_qualified && independent_overlap(other.burst,best,track.burst.helper_acquired));
                    }))return true;
                    for(std::size_t i=0;i<completed.size();++i) {
                        const auto& span=completed[i];
                        if(same_frequency(span.frequency) && track.burst.stream_first_sample>=span.first &&
                           best.first_sample<span.end && best.end_sample>span.first &&
                           !(distinct_qualified && independent_overlap(i,best)))return true;
                    }
                    return false;
                };
                // Preserve the ordinary early veto exactly. Helper-involved
                // exceptions first require the unchanged independent guard.
                const bool pending_duplicate=duplicate_continuation(false);
                if(pending_duplicate && !helper_overlap){ended=true;break;}
                if(burst_plan && best.score>=threshold() && audio::trace::event_enabled("symbol_burst_fft_fit"))
                    (void)measure(chosen,track.index,selected_frequency,track.burst.end_sample/bin_samples,
                        chosen_carriers,stop,&best);
                double chain_score=best.score;
                const auto presence=outer_presence_check(best,track.burst.end_sample/bin_samples,stop,&chain_score,
                    track.established);
                remember(best);
                const auto symbol_support=temporal_symbol_support(
                    best,selected_frequency,track.burst.end_sample/bin_samples,stop);
                const auto standalone=best.score>=threshold() && presence;
                if(pending_duplicate && duplicate_continuation(standalone)){ended=true;break;}
                if(acquisition_suspended && !track.established && standalone &&
                   best.first_sample/bin_samples>acquisition_last_start) {
                    // The ordinary standalone rule would replace the weak
                    // prefix with a fresh stream here. Frozen acquisition may
                    // resolve its old chain, but cannot start that new stream.
                    ended=true;break;
                }
                if(!track.unconfirmed_symbols)track.absent_samples=0;
                track.absent_samples+=static_cast<long double>(code.symbol_samples())/clock_ratio(track.frequency);
                ++track.unconfirmed_symbols;
                const auto gap_expired=detail::fft_failed_duration_complete(track.absent_samples,
                    track.unconfirmed_symbols,code.symbol_samples(),config.sample_rate,variable_clock);
                if(!presence || best.score<search.retain_score || best.score-best.alternative_score<1 || (group_count>1 && !standalone) ||
                   (track.pending_gap && !standalone)) {
                    if(track.admitted && !gap_expired) {
                        if(!track.pending_gap) {
                            track.gap_slots=track.burst.bits.size()-track.confirmed;
                            track.burst.bits.resize(track.confirmed);
                        }
                        if(track.gap_slots==std::numeric_limits<std::size_t>::max())throw Error("pattern missing-slot count overflow");
                        ++track.gap_slots;
                        track.pending_gap=true;track.total_score=track.confirmed_score;
                        track.total_support=track.confirmed_support;
                        track.pending_score=track.penalty=0;
                        advance_track(track);
                        continue;
                    }
                    publish(track,gap_expired,true);
                    if(!track.established || gap_expired){ended=true;break;}
                    // A lost symbol closes this contiguous span, but timing
                    // and the private stream advance without requiring it to
                    // decode. Do not refine timing from an obscured pattern.
                    track.burst.bits.clear();track.burst.complete=false;
                    track.admitted=false;track.confirmed=0;track.pending_gap=false;track.gap_slots=0;
                    track.total_score=track.pending_score=track.confirmed_score=track.penalty=0;
                    track.total_support=track.confirmed_support=0;
                    advance_track(track);
                    continue;
                }
                if(standalone && !track.pending_gap && track.confirmed<track.burst.bits.size()) {
                    // Keep a pending tail when the combined evidence meets
                    // its own chain threshold. Standalone confidence alone
                    // must not confirm a tail whose joint bound still fails.
                    const auto n=static_cast<double>(track.burst.bits.size()-track.confirmed+1);
                    const auto score=track.pending_score+best.score;
                    const auto bound=score>n?score-n-n*std::log(score/n)-track.penalty-continuation_penalty(radius):0;
                    if(bound<threshold()) {
                        if(track.admitted) {
                            track.gap_slots=track.burst.bits.size()-track.confirmed;track.burst.bits.resize(track.confirmed);
                            track.pending_gap=true;
                            track.total_score=track.confirmed_score;track.pending_score=track.penalty=0;
                            track.total_support=track.confirmed_support;
                        } else {
                            publish(track,false,true);track.burst.bits.clear();
                            track.burst.complete=false;track.admitted=false;
                        }
                    }
                }
                if(standalone) {
                    emit_gap(track,best.first_sample,best.stream_symbol);
                    track.phase_lower=selected.lower;track.phase_upper=selected.upper;
                    track.burst.stream_phase_samples=selected.lower;
                }
                if(track.burst.bits.empty() || (!track.admitted && standalone)) {
                    // A confident new start cannot lend its evidence to an
                    // earlier unconfirmed noise candidate. Keep weak chains
                    // that already established their own confidence.
                    track.burst.bits.clear();track.burst.first_sample=best.first_sample;
                    track.burst.first_stream_symbol=best.stream_symbol;
                    if(!track.admitted) {
                        track.burst.stream_first_sample=best.first_sample;
                        track.burst.stream_first_symbol=best.stream_symbol;
                        track.total_score=track.pending_score=track.penalty=0;
                        std::fill(track.frequency_scores.begin(),track.frequency_scores.end(),0.);
                        track.confirmed=0;track.confirmed_score=0;
                        track.total_support=track.confirmed_support=0;
                    }
                }
                if(track.burst.bits.size()==search.bit_limit && track.admitted && !standalone) {
                    track.gap_slots=track.burst.bits.size()-track.confirmed+1;track.burst.bits.resize(track.confirmed);
                    track.pending_gap=true;track.pending_score=track.penalty=0;track.total_score=track.confirmed_score;
                    track.total_support=track.confirmed_support;
                    advance_track(track);continue;
                }
                append_bit(track.burst.bits,static_cast<std::uint8_t>(best.bit));track.burst.end_sample=best.end_sample;
                track.total_score+=best.score;track.pending_score+=track.established?best.score:chain_score;track.penalty+=continuation_penalty(radius);
                track.total_support+=symbol_support;
                const auto count=static_cast<double>(track.burst.bits.size()-track.confirmed);
                // Chernoff bound for the sum of independent Exp(1) null scores,
                // with a union penalty for bit/timing alternatives. Overlapping
                // bins at the start of this window were excluded by measure,
                // so an earlier timing correction never counts evidence twice.
                const auto bound=track.pending_score>count?track.pending_score-count-count*std::log(track.pending_score/count)-track.penalty:0;
                if(bound>=threshold() || standalone) {
                    const auto prior_confirmed=track.confirmed;
                    for(std::size_t f=0;f<templates.size();++f)
                        track.frequency_scores[f]+=frequency_scores[f][best.bit];
                    const auto winner=static_cast<std::size_t>(std::max_element(track.frequency_scores.begin(),
                        track.frequency_scores.begin()+static_cast<std::ptrdiff_t>(templates.size()))-track.frequency_scores.begin());
                    track.burst.frequency_hz=config.carrier_hz+search.frequency_offsets_hz[winner]+track.frequency_origin_hz;
                    track.frequency=selected_frequency;
                    track.admitted=track.established=true;track.unconfirmed_symbols=0;
                    track.pending_gap=false;
                    track.confirmed=track.burst.bits.size();track.confirmed_end=best.end_sample;
                    accepted_serial+=track.confirmed-prior_confirmed;
                    if(track.confirmed>prior_confirmed)record_link(selected_frequency,track.frequency_origin_hz);
                    track.confirmed_score=track.total_score;track.pending_score=0;track.penalty=0;
                    track.confirmed_support=track.total_support;
                    audio::trace::record("pattern_confirmation",[&](auto& f) {
                        f.add("phase","continuation");f.add("epoch",config.stream_epoch);f.add("input_samples",sample);
                        f.add("stream_first_sample",track.burst.stream_first_sample);f.add("stream_first_symbol",track.burst.stream_first_symbol);
                        f.add("sample_rate",config.sample_rate);f.add("input_sample_origin",search.input_sample_origin);
                        f.add("phase_view_id",track.phase_view?track.phase_view->view_id():0);
                        f.add("stream_phase_samples",track.burst.stream_phase_samples);f.add("frequency_hz",track.burst.frequency_hz);
                        f.add("chunk_first_symbol",track.burst.first_stream_symbol);f.add("prior_confirmed",prior_confirmed);
                        f.add("confirmed",track.confirmed);f.add("newly_confirmed",track.confirmed-prior_confirmed);
                        f.add("end_sample",track.confirmed_end);f.add("standalone",standalone);f.add("joint_chain",!standalone);
                        f.add("scope","original detector confirmation before public drain, not shared Live selection");
                    });
                }
                advance_track(track,((!packed_calendar && !burst_plan) || bound>=threshold() || standalone)?
                    std::optional<std::uint64_t>{chosen}:std::nullopt);
                publish(track,false);
                if(gap_expired && track.unconfirmed_symbols){publish(track,true,true);ended=true;break;}
            }
            if(final && !ended) { publish(track,false,true);ended=true; }
            if(ended)it=tracks.erase(it);else ++it;
        }
    }
    void admit(const PatternEvidence& item,std::size_t f,double chain_score,double origin,
            const PatternPhaseCurve* view,std::stop_token stop) {
        if(acquisition_suspended && item.first_sample/bin_samples>acquisition_last_start)return;
        if(item.score<search.retain_score || item.score-item.alternative_score<1)return;
        remember(item);
        if(search.search_stream_phases && item.score<threshold()) {
            std::size_t group_count=0;
            phase_groups(item.stream_symbol,0,phase_upper,group_count);
            // A low-confidence start cannot select one cryptographic phase
            // and later borrow a different symbol's evidence to confirm it.
            if(group_count>1)return;
        }
        const auto same_frequency=[&](double frequency) {
            return competing_frequencies() ||
                std::abs(item.frequency_hz-frequency)<=static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
        };
        for(std::size_t i=0;i<completed.size();++i) {
            const auto& span=completed[i];
            if(same_frequency(span.frequency)&&item.first_sample<span.end&&item.end_sample>span.first &&
               !independent_overlap(i,item))return;
        }
        for(auto it=tracks.begin();it!=tracks.end();) {
            const auto& track=*it;
            const auto margin=2*bin_samples;
            if(same_frequency(track.burst.frequency_hz)&&item.first_sample+margin>=track.burst.stream_first_sample && item.first_sample<=track.next*bin_samples+margin) {
                // A helper-born false alias cannot permanently veto a distinct
                // independently qualified private stream. Keep both owners;
                // neither published prefix is retracted or used as framing.
                if(independent_overlap(track.burst,item,prior_dispatch)){++it;continue;}
                // Unknown alignment cannot withdraw a helper-involved owner
                // that already accepted bits. A fresh unpublished peak keeps
                // the overlap veto; ordinary replacement policy is unchanged.
                if(track.established && (track.burst.helper_acquired || prior_dispatch))return;
                // A weak fresh noise candidate must not displace timing that
                // already decoded a symbol and is crossing a bounded gap.
                if(track.established && !track.admitted && item.score<threshold())return;
                if(track.admitted && item.first_sample+margin<track.confirmed_end &&
                   item.score>track.total_score && item.score>=threshold()) {
                    // Admission is not a permanent timing lock. A stronger
                    // overlapping pattern replaces the weaker hypothesis;
                    // allow normal timing overlap between adjacent symbols.
                    it=tracks.erase(it);continue;
                }
                // A fresh candidate on the retained gap's clock can await
                // normal continuation. Independently timed starts must still
                // be allowed to replace an obscured old track.
                bool follows_gap=false;
                if(track.pending_gap) {
                    const auto next=track.next*bin_samples,symbol=search.hypotheses.empty()?
                        code.symbol_samples():symbol_bins(track.frequency)*bin_samples;
                    const auto distance=next>item.first_sample?next-item.first_sample:item.first_sample-next;
                    const auto slots=distance/symbol+(distance%symbol>symbol/2);
                    const auto residual=distance%symbol;
                    const auto same_clock=std::min(residual,symbol-residual)<=margin;
                    const auto same_index=(!config.scramble && !config.dsss) ||
                        (next>=item.first_sample && slots<=track.index && item.stream_symbol==track.index-slots) ||
                        (next<item.first_sample && slots<=std::numeric_limits<std::uint64_t>::max()-track.index &&
                         item.stream_symbol==track.index+slots);
                    follows_gap=same_clock && same_index;
                    if(calendar_symbols()) {
                        const auto predicted=static_cast<long double>(next)+
                            (static_cast<long double>(source_start(item.stream_symbol,track.phase_lower))-
                             source_start(track.index,track.phase_lower))/clock_ratio(track.frequency);
                        const auto expected=stream_address(item.stream_symbol,track.phase_lower);
                        const auto actual=stream_address(item.stream_symbol,item.stream_phase_samples);
                        follows_gap=expected.epoch==actual.epoch && expected.ordinal==actual.ordinal &&
                            std::abs(predicted-item.first_sample)<=margin;
                    }
                }
                if(track.admitted && !follows_gap && (track.pending_gap || track.confirmed<track.burst.bits.size()) &&
                   item.first_sample>=track.confirmed_end && item.score>=threshold()) {
                    // A qualified helper may expose a later independent
                    // start while an accepted owner is still scoring absence.
                    // Keep that owner's original clock through physical END;
                    // the fresh start still competes under the same track cap.
                    if(track.established && track.helper_gap_protected){++it;continue;}
                    // Confidence belongs only to the confirmed span. Weak
                    // pending extensions cannot veto an independently strong
                    // later start; finish the old span and consider this one.
                    publish(*it,false,true);it=tracks.erase(it);continue;
                }
                if(track.admitted||track.total_score>=item.score)return;
                it=tracks.erase(it);
            } else ++it;
        }
        if(tracks.size()==search.track_limit) {
            candidate_limit_reached=true;
            const auto worst=std::min_element(tracks.begin(),tracks.end(),[](const auto& a,const auto& b){
                if(a.established!=b.established)return !a.established;
                return a.total_score<b.total_score;
            });
            if(worst->established || worst->total_score>=item.score)return;
            tracks.erase(worst);
        }
        Track track;track.frequency_scores.resize(templates.size());
        track.helper_gap_protected=helper_gap_protection;
        track.frequency_origin_hz=origin;track.phase_view=view;track.serial=++track_serial;
        append_bit(track.burst.bits,static_cast<std::uint8_t>(item.bit));
        track.burst.first_sample=item.first_sample;track.burst.end_sample=item.end_sample;track.burst.frequency_hz=item.frequency_hz;
        track.burst.first_stream_symbol=item.stream_symbol;track.index=item.stream_symbol+1;
        track.burst.stream_first_sample=item.first_sample;track.burst.stream_first_symbol=item.stream_symbol;
        track.burst.helper_acquired=prior_dispatch;
        track.phase_lower=track.phase_upper=config.stream_phase_samples;
        if(search.search_stream_phases) {
            std::size_t group_count=0;
            const auto groups=phase_groups(item.stream_symbol,0,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g)if(item.stream_phase_samples>=groups[g].lower && item.stream_phase_samples<=groups[g].upper) {
                track.phase_lower=groups[g].lower;track.phase_upper=groups[g].upper;break;
            }
        }
        track.burst.stream_phase_samples=track.phase_lower;
        track.anchor_start=item.first_sample/bin_samples;track.anchor_index=item.stream_symbol;track.frequency=f;
        track.next=(packed_calendar || burst_plan)?track_start(track,track.index):
            item.first_sample/bin_samples+advance_bins(item.stream_symbol,track.phase_lower,f);
        track.total_score=item.score;track.penalty=std::log(2.);
        track.total_support=temporal_symbol_support(item,f,0,stop);
        track.frequency_scores[f]=item.score;
        track.admitted=track.established=item.score>=threshold();
        if(track.admitted)++accepted_serial;
        if(track.admitted)record_link(f,origin);
        if(track.admitted){track.confirmed=1;track.confirmed_end=item.end_sample;track.confirmed_score=item.score;track.penalty=0;
            track.confirmed_support=track.total_support;}
        else track.pending_score=chain_score;
        if(track.admitted)audio::trace::record("pattern_confirmation",[&](auto& f) {
            f.add("phase","acquisition");f.add("epoch",config.stream_epoch);f.add("input_samples",sample);
            f.add("stream_first_sample",track.burst.stream_first_sample);f.add("stream_first_symbol",track.burst.stream_first_symbol);
            f.add("sample_rate",config.sample_rate);f.add("input_sample_origin",search.input_sample_origin);
            f.add("phase_view_id",track.phase_view?track.phase_view->view_id():0);
                        f.add("stream_phase_samples",track.burst.stream_phase_samples);f.add("frequency_hz",track.burst.frequency_hz);
                        f.add("chunk_first_symbol",track.burst.first_stream_symbol);f.add("prior_confirmed",0);f.add("confirmed",1);f.add("newly_confirmed",1);
            f.add("end_sample",track.confirmed_end);f.add("standalone",true);f.add("joint_chain",false);
            f.add("scope","original detector confirmation before public drain, not shared Live selection");
        });
        tracks.push_back(std::move(track));publish(tracks.back(),false);
    }
    void collect_score(double a,double b,std::size_t j,std::uint64_t index,std::uint64_t phase,std::size_t f,bool startup_guide=false) {
        if(!acquisition_prepaid)++trials;
        if(startup_guide) {
            // These two records reuse the already-budgeted peak storage. Keep
            // both raw-bit maxima before normal clustering can discard one.
            if(!std::isfinite(a) || !std::isfinite(b)) {
                peaks[0].score=peaks[1].score=std::numeric_limits<double>::quiet_NaN();
                return;
            }
            for(unsigned bit=0;bit<2;++bit) {
                const auto score=bit?b:a;
                if(score>peaks[bit].score) {
                    peaks[bit]={(next_start+j)*bin_samples,(next_start+j+length)*bin_samples,index,
                        config.carrier_hz+frequency_offset(f),score,bit?a:b,bit,phase};
                    peaks[bit].frequency_hypothesis=static_cast<std::uint32_t>(f);
                }
            }
            return;
        }
        if(std::max(a,b)<search.retain_score)return;
        PatternEvidence item{(next_start+j)*bin_samples,(next_start+j+length)*bin_samples,index,
            config.carrier_hz+frequency_offset(f),std::max(a,b),std::min(a,b),b>a?1U:0U,phase};
        item.frequency_hypothesis=static_cast<std::uint32_t>(f);
        const auto existing=std::find_if(peaks.begin(),peaks.end(),[&](const auto& p){
            const auto distance=p.first_sample>item.first_sample?p.first_sample-item.first_sample:item.first_sample-p.first_sample;
            return distance<std::max<std::uint64_t>(1,code.symbol_samples()/2) &&
                (competing_frequencies() ||
                 std::abs(p.frequency_hz-item.frequency_hz)<=static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples()));
        });
        if(existing!=peaks.end()) { if(item.score>existing->score)*existing=item; }
        else if(peaks.size()<search.candidate_limit)peaks.push_back(item);
        else if(item.score-item.alternative_score>=1) {
            // These tests also reject a candidate unconditionally in admit().
            // A full diagnostic peak list must not turn ordinary ambiguity
            // rejection into a receiver failure.
            std::size_t group_count=1;
            if(search.search_stream_phases && item.score<threshold())
                phase_groups(item.stream_symbol,0,phase_upper,group_count);
            if(group_count==1)candidate_limit_reached=true;
        }
    }
    void collect_scores(std::span<const Complex> scores,std::uint64_t index,std::uint64_t phase,std::size_t f,bool startup_guide=false) {
        for(std::size_t j=0;j<scores.size();++j)
            collect_score(scores[j].real(),scores[j].imag(),j,index,phase,f,startup_guide);
    }
    detail::PatternFftStartSelection select_starts(std::uint64_t index,PhaseGroup group,std::size_t count)const {
        if(!fft_workload->restricted_timing_active)return {{},0,count,true};
        const auto geometry=window_geometry(index,group);
        auto selected=detail::pattern_fft_select_starts(*acquisition_window,geometry.samples,geometry.index,
            geometry.lower,geometry.upper,geometry.step,component_hop?logical_hop_begin:next_start,
            component_hop?hop:count,bin_samples,timing_minimum_rate,timing_maximum_rate);
        if(!component_hop)return selected;
        // Select on the immutable original hop, then intersect and rebase.
        // Recomputing a smaller numeric envelope could change an endpoint.
        detail::PatternFftStartSelection result;result.full=false;
        const auto first=static_cast<std::size_t>(next_start-logical_hop_begin),end=first+count;
        for(std::size_t r=0;r<selected.range_count;++r) {
            const auto low=std::max(first,selected.ranges[r].first);
            const auto high=std::min(end,selected.ranges[r].first+selected.ranges[r].count);
            if(low<high) {
                result.ranges[result.range_count++]={low-first,high-low};
                result.selected_count+=high-low;
            }
        }
        return result;
    }
    template<class Action> static void visit_starts(const detail::PatternFftStartSelection& selected,
                                                   std::size_t count,Action action) {
        if(selected.full)for(std::size_t j=0;j<count;++j)action(j);
        else for(std::size_t r=0;r<selected.range_count;++r) {
            const auto range=selected.ranges[r];
            for(std::size_t j=range.first;j<range.first+range.count;++j)action(j);
        }
    }
    template<class Score> void collect_selected(const detail::PatternFftStartSelection& selected,
            std::size_t count,std::uint64_t index,std::uint64_t phase,std::size_t f,Score score,bool startup_guide=false) {
        std::size_t previous=0;
        visit_starts(selected,count,[&](std::size_t j) {
            if(!acquisition_prepaid){trials+=j-previous;fft_workload->skipped_start_trials+=j-previous;}
            const auto values=score(j);collect_score(values.real(),values.imag(),j,index,phase,f,startup_guide);
            previous=j+1;
        });
        if(!acquisition_prepaid){trials+=count-previous;fft_workload->skipped_start_trials+=count-previous;}
    }
    bool fft_batch_may_intersect(std::size_t count)const {
        if(!acquisition_window)return true;
        for(std::uint64_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
                if(fft_workload->restricted_timing_active) {
                    if(select_starts(index,groups[g],count).selected_count)return true;
                } else {
                    const auto geometry=window_geometry(index,groups[g]);
                    if(detail::pattern_fft_batch_may_intersect(*acquisition_window,geometry.samples,geometry.index,
                        geometry.lower,geometry.upper,next_start,count,bin_samples))return true;
                }
            }
        }
        return false;
    }
    std::uint64_t acquisition_trials(std::size_t count)const {
        std::uint64_t total=0;
        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t groups=0;
            (void)phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,groups);
            total+=static_cast<std::uint64_t>(count)*templates.size()*groups;
        }
        return total;
    }
    std::unique_ptr<detail::FftPrivateTemplate> private_acquisition_template(
            std::uint64_t index,std::uint64_t phase,std::size_t held,std::stop_token stop) {
        if(templates.size()<3 || acquisition_template_reuse()!=detail::PrivateTemplateReuse::bounded_interpolation)return {};
        const auto geometry=scoring_geometry();
        detail::FftSearchJob job{index,phase};
        for(std::size_t f=0;f<templates.size();++f) {
            job.clock_ratio=clock_ratio(f);
            if(!detail::FftPrivateTemplate::eligible(geometry,job))return {};
        }
        const auto bytes=detail::FftPrivateTemplate::required_bytes(geometry);
        if(bytes>std::numeric_limits<std::size_t>::max()-held || !cache_fits(budget,held+bytes))return {};
        auto cache=std::make_unique<detail::FftPrivateTemplate>(geometry,job,code,stop);
        if(cache->working_bytes()>bytes)throw Error("private template cache exceeds reservation");
        ++fft_workload->private_template_builds;
        fft_workload->private_template_peak_bytes=std::max(fft_workload->private_template_peak_bytes,cache->working_bytes());
        return cache;
    }
    template<class Scalar> bool score_native_whole_impl(std::size_t count,std::stop_token stop,bool startup_guide=false) {
        using Value=std::complex<Scalar>;
        using Plan=std::conditional_t<std::is_same_v<Scalar,float>,detail::search_fft::FloatPlan,detail::search_fft::DoublePlan>;
        auto& plan=[&]() -> auto& {if constexpr(std::is_same_v<Scalar,float>)return native_float_plan;else return native_double_plan;}();
        const auto buffer_bytes=std::is_same_v<Scalar,float>?2*transform*sizeof(Value):0;
        const auto plan_bytes=plan?0:(transform-1)*sizeof(Value)+sizeof(Plan);
        if(!cache_fits(budget,buffer_bytes+plan_bytes))return false;
        if(!plan)plan=std::make_unique<Plan>(transform,stop);
        std::vector<Value> input_storage,product_storage;
        std::span<Value> input,row;
        if constexpr(std::is_same_v<Scalar,float>) {
            input_storage.resize(transform);product_storage.resize(transform);
            input=input_storage;row=product_storage;
        }else {input=spectrum;row=product;}
        native_active_bytes=(input_storage.capacity()+product_storage.capacity())*sizeof(Value);
        struct ActiveScope {std::size_t& value;~ActiveScope(){value=0;}} active{native_active_bytes};
        if(native_active_bytes>buffer_bytes || !cache_fits(budget,native_active_bytes))
            throw Error("native FFT storage exceeds reservation");
        // Adjacent deferred phase groups can name the exact same UTC symbol.
        // Reuse scores only within this immutable broad-input batch; keep all
        // original collections, trial charges, phase identities and ordering.
        // One cohort, capped at 8 MiB and spare workspace, never a shifted-PCM
        // matrix. Restricted priors keep their original independent ranges.
        std::vector<Complex> repeated_scores;
        SymbolStreamAddress repeated_address{};bool repeated_valid=false;
        const auto repeated_count=static_cast<long double>(count)*templates.size();
        const auto repeated_bytes=repeated_count*sizeof(Complex);
        if(symbol_calendar && search.reuse_deferred_templates && search.search_stream_phases && !fft_workload->restricted_timing_active &&
           repeated_bytes<=8*1024*1024 && cache_fits(budget,native_active_bytes+static_cast<std::size_t>(repeated_bytes))) {
            repeated_scores.resize(static_cast<std::size_t>(repeated_count));
            native_active_bytes+=repeated_scores.capacity()*sizeof(Complex);
            if(!cache_fits(budget,native_active_bytes))throw Error("exact score cache exceeds reservation");
        }
        fft_workload->native_scratch_peak_bytes=std::max(fft_workload->native_scratch_peak_bytes,
            native_active_bytes+native_plan_bytes());
        const auto transform_row=[&](std::span<Value> values,bool inverse) {
            detail::search_fft::fft(values,inverse,*plan,stop);
            if constexpr(std::is_same_v<Scalar,float>)++fft_workload->native_fp32_transforms;
            else ++fft_workload->tabulated_fp64_transforms;
        };
        for(std::size_t i=0;i<transform;++i){if((i&4095U)==0)cancelled(stop);
            input[i]={static_cast<Scalar>(work[i].real()),static_cast<Scalar>(work[i].imag())};}
        transform_row(input,false);++fft_workload->input_transforms;
        if(tracking_reference.empty())tracking_reference_valid=false;
        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
                const auto selected=select_starts(index,groups[g],count);
                if(!selected.selected_count) {
                    const auto skipped=static_cast<std::uint64_t>(count)*templates.size();
                    if(!acquisition_prepaid){trials+=skipped;fft_workload->skipped_start_trials+=skipped;
                        fft_workload->skipped_template_jobs+=templates.size();}
                    continue;
                }
                stream_phase(groups[g].lower);
                const auto address=stream_address(index,groups[g].lower);
                const bool repeated=!repeated_scores.empty() && repeated_valid &&
                    address.epoch==repeated_address.epoch && address.ordinal==repeated_address.ordinal;
                if(!repeated)prepare_templates(index,stop);
                auto private_cache=repeated?nullptr:private_acquisition_template(index,groups[g].lower,native_active_bytes,stop);
                const auto before_cache=native_active_bytes;
                if(private_cache)native_active_bytes+=private_cache->working_bytes();
                struct CacheReservation {std::size_t& bytes;std::size_t before;
                    ~CacheReservation(){bytes=before;}} private_cache_scope{native_active_bytes,before_cache};
                for(std::size_t f=0;f<templates.size();++f) {
                    if(!acquisition_lane(f))continue;
                    if(private_cache)++fft_workload->private_template_lane_jobs;
                    ++fft_workload->template_jobs;
                    fft_workload->retained_start_trials+=selected.selected_count;
                    if(repeated) {
                        ++fft_workload->exact_reused_template_jobs;
                        fft_workload->scored_start_trials+=selected.selected_count;
                        collect_selected(selected,count,index,groups[g].lower,f,[&](std::size_t j){return repeated_scores[f*count+j];},startup_guide);
                        continue;
                    }
                    ++fft_workload->fft_jobs;
                    for(unsigned b=0;b<2;++b) {
                        std::fill(row.begin(),row.end(),Value{});double norm=0;bool safe=true;
                        const auto limit=detail::search_fft::float_component_limit(transform);
                        for(std::size_t i=0;i<length;++i) {
                            if((i&4095U)==0)cancelled(stop);
                            Complex original;
                            if(private_cache) {
                                const auto position=static_cast<long double>(i)*bin_samples+(bin_samples-1)/2.L;
                                if(!variable_clock || position*clock_ratio(f)<code.symbol_samples())
                                    original=private_cache->value(i,clock_ratio(f))[b]*std::polar(1.,
                                        tau*frequency_offset(f)*static_cast<double>(position)/config.sample_rate);
                            }else original=template_value(i,index,b,f);
                            if constexpr(std::is_same_v<Scalar,float>)safe=safe && std::isfinite(original.real()) &&
                                std::isfinite(original.imag()) && std::abs(original.real())<=limit && std::abs(original.imag())<=limit;
                            const Value value{static_cast<Scalar>(original.real()),static_cast<Scalar>(original.imag())};
                            row[length-1-i]=std::conj(value);norm+=std::norm(Complex{value.real(),value.imag()});
                        }
                        if(safe) {
                            transform_row(row,false);
                            for(std::size_t i=0;i<transform;++i){if((i&4095U)==0)cancelled(stop);row[i]*=input[i];}
                            transform_row(row,true);
                            if constexpr(std::is_same_v<Scalar,float>)safe=detail::search_fft::finite(row);
                        }
                        if(!safe) {
                            // Retry this unpublished bit from original operands.
                            ++fft_workload->arithmetic_fallback_jobs;
                            spectrum=work;pattern_fft(spectrum,false,stop);norm=0;
                            ++fft_workload->input_transforms;
                            ++fft_workload->fp32_retry_fp64_transforms;
                            std::fill(product.begin(),product.end(),Complex{});
                            for(std::size_t i=0;i<length;++i){if((i&4095U)==0)cancelled(stop);
                                const auto value=template_value(i,index,b,f);product[length-1-i]=std::conj(value);norm+=std::norm(value);}
                            pattern_fft(product,false,stop);
                            ++fft_workload->fp32_retry_fp64_transforms;
                            for(std::size_t i=0;i<transform;++i)product[i]*=spectrum[i];
                            pattern_fft(product,true,stop);
                            ++fft_workload->fp32_retry_fp64_transforms;
                        }
                        visit_starts(selected,count,[&](std::size_t j) {
                            const auto z=row[length-1+j];const Complex dot=safe?Complex{z.real(),z.imag()}:product[length-1+j];
                            const auto score=pattern_evidence(dot,energy_prefix[j+length]-energy_prefix[j],
                                norm,evidence_count(length),noise_condition,real_rank,false,{});
                            if(b==0)reference[j]={score,0};else reference[j].imag(score);
                        });
                    }
                    fft_workload->scored_start_trials+=selected.selected_count;
                    if(!repeated_scores.empty())std::copy_n(reference.begin(),count,repeated_scores.begin()+f*count);
                    collect_selected(selected,count,index,groups[g].lower,f,[&](std::size_t j){return reference[j];},startup_guide);
                }
                repeated_address=address;repeated_valid=true;
            }
        }
        return true;
    }
    bool score_native_whole(std::size_t count,std::stop_token stop,bool startup_guide=false) {
        if(!streamed_templates || sample_fit || drift_sections!=1 || differential_window || search.worker_threads!=1)return false;
        // Preserve every direct/partition crossover. Never expand a narrow bank
        // merely to use a transform or a different arithmetic representation.
        if(fft_workload->restricted_timing_active && search.restricted_direct_search)
            for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
                std::size_t groups_count=0;
                const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,groups_count);
                for(std::size_t g=0;g<groups_count;++g){const auto selected=select_starts(index,groups[g],count);
                    if(detail::pattern_fft_direct_eligible(scoring_geometry(),true,selected.selected_count,true))return false;}
            }
        if(acquisition_arithmetic()==SearchArithmetic::fp32) {
            if(score_native_whole_impl<float>(count,stop,startup_guide))return true;
            // Insufficient native-float scratch must not select the old wide
            // buffer float recurrence, whose accumulated phase error is larger.
            // Rebuild original operands before the exact memory fallback.
            fp32_exact_fallback=true;drop_template_cache();templates_valid=false;
            ++fft_workload->arithmetic_fallback_jobs;
        }
        return score_native_whole_impl<double>(count,stop,startup_guide);
    }
    bool score_bounded(std::size_t count,std::size_t observed,std::stop_token stop,bool startup_guide=false) {
        if(!fft_workload->restricted_timing_active || !search.partitioned_timing_search ||
           !streamed_templates || sample_fit || drift_sections>1 || differential_window)return false;
        detail::FftSearchBatch batch;batch.geometry=scoring_geometry();batch.starts=count;
        batch.retain_floor=search.retain_score;
        batch.observations=std::span(work).first(observed+length-1);
        batch.energy_prefix=std::span(energy_prefix).first(observed+length);batch.first_bin=next_start;
        constexpr auto direct_limit=detail::partitioned_paired::integer_direct_start_limit;
        const auto inspect=[&](std::uint64_t index,PhaseGroup group) {
            const auto selected=select_starts(index,group,count);
            batch.start_ranges=std::span(selected.ranges).first(selected.range_count);
            detail::FftSearchJob job{index,group.lower,0,frequency_offset(0)};
            job.clock_ratio=clock_ratio(0);job.range_count=selected.range_count;
            if(selected.full)return false;
            if(!selected.selected_count || (search.restricted_direct_search && selected.selected_count<=direct_limit))return true;
            return detail::partitioned_paired::choose(batch,job,templates.size(),transform,2*transform).has_value();
        };
        // Decide the complete cohort before overwriting either FFT buffer.
        // A mixed full-FFT fallback must retain its shared input spectrum;
        // never keep an extra broad spectrum merely to mix execution methods.
        for(std::uint64_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t groups=0;
            const auto phases=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,groups);
            for(std::size_t g=0;g<groups;++g)if(!inspect(index,phases[g])) {
                ++fft_workload->partitioned_fallbacks;return false;
            }
        }
        drop_scoring();
        if(tracking_reference.empty())tracking_reference_valid=false;
        std::optional<detail::FftQuantizedDirect> quantized;
        const bool arithmetic_available=config.search_arithmetic!=SearchArithmetic::fp64 &&
            cache_fits(budget,detail::partitioned_paired::direct_arithmetic_stack_bytes);
        struct ArithmeticScope {
            std::size_t& reservation;
            ~ArithmeticScope(){reservation=0;}
        } arithmetic_scope{arithmetic_reservation};
        if(arithmetic_available)arithmetic_reservation=detail::partitioned_paired::direct_arithmetic_stack_bytes;
        for(std::uint64_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t groups=0;
            const auto phases=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,groups);
            for(std::size_t g=0;g<groups;++g) {
                const auto selected=select_starts(index,phases[g],count);
                if(!selected.selected_count) {
                    if(!acquisition_prepaid) {
                        const auto skipped=static_cast<std::uint64_t>(count)*templates.size();
                        trials+=skipped;fft_workload->skipped_start_trials+=skipped;
                        fft_workload->skipped_template_jobs+=templates.size();
                    }
                    continue;
                }
                batch.start_ranges=std::span(selected.ranges).first(selected.range_count);
                detail::FftSearchJob job{index,phases[g].lower,0,frequency_offset(0)};
                job.clock_ratio=clock_ratio(0);job.range_count=selected.range_count;
                const bool direct=search.restricted_direct_search&&selected.selected_count<=direct_limit;
                if(direct && arithmetic_available && search_arithmetic_is_int8(config.search_arithmetic) && !quantized) {
                    const auto packed_bytes=detail::FftQuantizedDirect::required_bytes(batch.observations.size(),length);
                    if(packed_bytes<=std::numeric_limits<std::size_t>::max()-arithmetic_reservation &&
                       cache_fits(budget,arithmetic_reservation+packed_bytes)) {
                        quantized.emplace(batch.observations.size(),length);
                        if(quantized->working_bytes()>packed_bytes)throw Error("integer search cache exceeds workspace reservation");
                        arithmetic_reservation+=packed_bytes;batch.quantized_direct=&*quantized;
                    }
                }
                detail::partitioned_paired::Work work_count;
                std::optional<detail::partitioned_paired::Context> context;
                const auto previous_reservation=arithmetic_reservation;
                struct ContextReservation {std::size_t& current;std::size_t previous;
                    ~ContextReservation(){current=previous;}} context_reservation{arithmetic_reservation,previous_reservation};
                if(!direct) {
                    const auto requirement=detail::partitioned_paired::choose(batch,job,templates.size(),transform,2*transform);
                    if(!requirement)throw Error("bounded pattern plan changed after preflight");
                    const auto extra=detail::partitioned_paired::native_bytes(*requirement,acquisition_arithmetic());
                    const auto admitted=extra<=std::numeric_limits<std::size_t>::max()-arithmetic_reservation &&
                        cache_fits(budget,arithmetic_reservation+extra)?extra:0;
                    arithmetic_reservation+=admitted;
                    context=detail::partitioned_paired::prepare(batch,*requirement,spectrum,product,&work_count,stop,admitted);
                    fft_workload->native_scratch_peak_bytes=std::max(fft_workload->native_scratch_peak_bytes,admitted);
                    if(!context)throw Error("bounded pattern scratch disagrees with preflight");
                }
                stream_phase(phases[g].lower);
                auto private_cache=((direct && arithmetic_available) || (context && context->execution==SearchArithmetic::fp32))?
                    private_acquisition_template(index,phases[g].lower,arithmetic_reservation,stop):nullptr;
                if(private_cache)arithmetic_reservation+=private_cache->working_bytes();
                batch.private_template=private_cache.get();
                struct PrivateReferenceScope {detail::FftSearchBatch& batch;
                    ~PrivateReferenceScope(){batch.private_template=nullptr;}} private_reference_scope{batch};
                for(std::size_t f=0;f<templates.size();++f) {
                    if(!acquisition_lane(f))continue;
                    if(private_cache)++fft_workload->private_template_lane_jobs;
                    job.frequency_index=f;job.frequency_hz=frequency_offset(f);job.clock_ratio=clock_ratio(f);
                    const auto previous_int8_dots=work_count.int8_dots;
                    const auto success=direct?detail::partitioned_paired::score_direct(batch,job,code,reference,
                        arithmetic_available?std::span(spectrum):std::span<Complex>{},
                        arithmetic_available?std::span(product):std::span<Complex>{},&work_count,stop):
                        detail::partitioned_paired::score_job(*context,job,code,reference,&work_count,stop);
                    if(!success)throw Error("bounded pattern job disagrees with preflight");
                    ++fft_workload->template_jobs;
                    if(work_count.int8_dots>previous_int8_dots)++fft_workload->int8_jobs;
                    if(direct)++fft_workload->direct_jobs;else ++fft_workload->partitioned_jobs;
                    fft_workload->retained_start_trials+=selected.selected_count;
                    fft_workload->scored_start_trials+=selected.selected_count;
                    collect_selected(selected,count,index,phases[g].lower,f,[&](std::size_t j){return reference[j];},startup_guide);
                }
                fft_workload->partitioned_input_transforms+=work_count.input_transforms;
                fft_workload->int8_dots+=work_count.int8_dots;
                fft_workload->certified_rejects+=work_count.certified_rejects;
                fft_workload->exact_refines+=work_count.exact_refines;
                fft_workload->fp32_dots+=work_count.fp32_dots;
                fft_workload->arithmetic_fallback_jobs+=work_count.precision_fallback_jobs;
                fft_workload->native_fp32_transforms+=work_count.native_fp32_transforms;
                fft_workload->tabulated_fp64_transforms+=work_count.tabulated_fp64_transforms;
                if(context)fft_workload->partitioned_transform_points+=context->geometry.transform*
                    (work_count.input_transforms+work_count.template_transforms+work_count.inverse_transforms);
            }
        }
        return true;
    }
    void score_parallel(std::size_t count,std::stop_token stop,bool startup_guide=false) {
        auto& storage=scoring.front();
        auto& scoring_jobs=storage.jobs;auto& scoring_templates=storage.templates;
        auto& scoring_outputs=storage.outputs;auto& scoring_workspaces=storage.workspaces;
        std::size_t queued=0;
        const bool reuse_templates=reuse_single_template();
        // Cache eviction originally materialized these ordinary transform
        // rows on the next hop. Keep the same retained footprint for shared
        // receiver-bank accounting even when workers generate private rows.
        if(!streamed_templates && cached_templates.empty() && !reuse_templates)
            for(auto& row:templates)for(auto& values:row)values.resize(transform);
        detail::FftSearchBatch batch;
        batch.geometry=scoring_geometry();
        batch.nominal_reference=storage.nominal_reference;
        batch.spectrum=spectrum;batch.carrier_square=work;batch.energy_prefix=energy_prefix;
        if(drift_sections>1 || (fft_workload->restricted_timing_active && search.restricted_direct_search && !sample_fit))batch.observations=work;
        batch.first_bin=next_start;
        batch.starts=count;batch.score_stride=hop;
        // The original shared input transform is complete. These two fixed,
        // already-accounted buffers are dead until joined acquisition returns.
        // No table survives this call or competes with receiver admission.
        if(!fft_workload->restricted_timing_active && !sample_fit && drift_sections==1 && !differential_window &&
           acquisition_arithmetic()==SearchArithmetic::fp64 && batch.observations.empty() &&
           !batch.observation_skip && work.size()==transform && reference.size()==transform &&
           std::fegetround()==FE_TONEAREST) {
            batch.carrier_square={};
            batch.double_tables=detail::search_fft::prepare_double_tables(
                std::span(work).first(transform-1),std::span(reference).first(transform-1),stop);
        }
        detail::PatternFftStartSelection selected{{},0,count,true};
        const auto flush=[&] {
            if(!queued)return;
            batch.prepared=std::span<const detail::FftPreparedTemplate>(scoring_templates).first(queued);
            fft_workload->template_jobs+=queued;
            std::vector<detail::FftSearchJob> unique_jobs;
            std::vector<std::size_t> output_job,canonical_jobs;
            const auto previous_reservation=arithmetic_reservation;
            struct DuplicateReservation {std::size_t& bytes;std::size_t before;
                ~DuplicateReservation(){bytes=before;}} duplicate_reservation{arithmetic_reservation,previous_reservation};
            const auto slots=(static_cast<long double>(search.initial_stream_symbols)+1)*templates.size();
            const auto extra=static_cast<long double>(queued)*(sizeof(detail::FftSearchJob)+sizeof(std::size_t))+
                static_cast<long double>(slots)*sizeof(std::size_t);
            if(symbol_calendar && search.reuse_deferred_templates && search.search_stream_phases && !fft_workload->restricted_timing_active &&
               extra<=8*1024*1024 && cache_fits(budget,arithmetic_reservation+static_cast<std::size_t>(extra))) {
                unique_jobs.reserve(queued);output_job.resize(queued);
                canonical_jobs.assign(static_cast<std::size_t>(slots),std::numeric_limits<std::size_t>::max());
                const auto first=symbol_calendar->first_rank(config.stream_epoch,0);
                for(std::size_t j=0;j<queued;++j) {
                    const auto& descriptor=scoring_jobs[j];
                    const auto rank=symbol_calendar->first_rank(config.stream_epoch,descriptor.phase)-first+descriptor.symbol;
                    auto& destination=canonical_jobs.at(static_cast<std::size_t>(rank)*templates.size()+descriptor.frequency_index);
                    if(destination==std::numeric_limits<std::size_t>::max()) {
                        destination=unique_jobs.size();unique_jobs.push_back(descriptor);
                    }
                    output_job[j]=destination;
                }
                arithmetic_reservation+=unique_jobs.capacity()*sizeof(detail::FftSearchJob)+
                    (output_job.capacity()+canonical_jobs.capacity())*sizeof(std::size_t);
                if(!cache_fits(budget,arithmetic_reservation))throw Error("parallel exact reuse exceeds reservation");
                fft_workload->exact_reused_template_jobs+=queued-unique_jobs.size();
            }
            const auto jobs=unique_jobs.empty()?std::span<const detail::FftSearchJob>(scoring_jobs).first(queued):
                std::span<const detail::FftSearchJob>(unique_jobs);
            const auto exact=exact_initial_rows && exact_initial_rows->valid?
                std::span<const detail::FftExactRawReference>(exact_initial_rows->references).first(exact_initial_rows->count):
                std::span<const detail::FftExactRawReference>{};
            detail::execute_fft_search_cpu_exact(batch,jobs,
                std::span(scoring_outputs).first(jobs.size()*hop),scoring_workspaces,exact,stop);
            if(batch.double_tables.maximum)for(const auto& job:jobs)
                fft_workload->tabulated_fp64_transforms+=
                    job.prepared_template==std::numeric_limits<std::uint64_t>::max()?4:2;
            // Trials, tie-breaking and peak replacement use the original
            // stream/phase/frequency/start order regardless of backend order.
            std::size_t collected_unique=0;
            for(std::size_t job=0;job<queued;++job) {
                const auto& descriptor=scoring_jobs[job];
                const auto output=output_job.empty()?job:output_job[job];
                const auto direct=detail::pattern_fft_direct_eligible(batch.geometry,
                    descriptor.prepared_template==std::numeric_limits<std::uint64_t>::max(),
                    selected.selected_count,!batch.observations.empty());
                if(output==collected_unique) {
                    ++collected_unique;
                    if(direct)++fft_workload->direct_jobs;else ++fft_workload->fft_jobs;
                }
                fft_workload->scored_start_trials+=selected.selected_count;
                collect_selected(selected,count,descriptor.symbol,descriptor.phase,
                        static_cast<std::size_t>(descriptor.frequency_index),[&](std::size_t start) {
                    const auto& score=scoring_outputs[output*hop+start];
                    return Complex{score.zero,score.one};
                },startup_guide);
            }
            queued=0;
        };
        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
                // A restricted group's immutable range span remains live
                // through its synchronous dispatch and ordered collection.
                if(fft_workload->restricted_timing_active) {
                    flush();selected=select_starts(index,groups[g],count);
                    batch.start_ranges=std::span(selected.ranges).first(selected.range_count);
                }
                if(!selected.selected_count) {
                    const auto skipped=static_cast<std::uint64_t>(count)*templates.size();
                    if(!acquisition_prepaid) {
                        trials+=skipped;fft_workload->skipped_start_trials+=skipped;
                        fft_workload->skipped_template_jobs+=templates.size();
                    }
                    continue;
                }
                stream_phase(groups[g].lower);
                if(!cached_templates.empty() || reuse_templates)prepare_templates(index,stop);
                for(std::size_t f=0;f<templates.size();++f) {
                    if(!acquisition_lane(f))continue;
                    auto& job=scoring_jobs[queued];
                    job={index,groups[g].lower,f,frequency_offset(f)};
                    job.clock_ratio=clock_ratio(f);
                    // Drift jobs retain their original full-hop scorer. Its
                    // backend does not accept restricted ranges; the ordered
                    // collection below still admits only selected starts.
                    if(!selected.full && drift_sections==1)job.range_count=selected.range_count;
                    fft_workload->retained_start_trials+=selected.selected_count;
                    if(!cached_templates.empty() || reuse_templates) {
                        const auto& rows=cached_templates.empty()?templates:template_bank(index).rows;
                        const auto& energies=cached_templates.empty()?template_energy:template_bank(index).energy;
                        const auto& squares=cached_templates.empty()?template_square:template_bank(index).square;
                        scoring_templates[queued]={{rows[f][0],rows[f][1]},energies[f],squares[f]};
                        job.prepared_template=queued;
                    }
                    if(++queued==scoring_jobs.size())flush();
                }
                if(fft_workload->restricted_timing_active)flush();
            }
        }
        if(queued)flush();
    }
    void score_drift_serial(std::size_t count,std::stop_token stop,bool startup_guide=false) {
        detail::FftSearchBatch batch;
        batch.geometry=scoring_geometry();batch.spectrum=spectrum;batch.observations=work;
        batch.energy_prefix=energy_prefix;batch.starts=count;batch.first_bin=next_start;
        if(tracking_reference.empty())tracking_reference_valid=false;
        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
                stream_phase(groups[g].lower);
                for(std::size_t f=0;f<templates.size();++f) {
                    if(!acquisition_lane(f))continue;
                    detail::FftSearchJob job{index,groups[g].lower,f,frequency_offset(f)};
                    job.clock_ratio=clock_ratio(f);
                    ++fft_workload->template_jobs;
                    ++fft_workload->fft_jobs;fft_workload->retained_start_trials+=count;
                    detail::execute_drift_search_job(batch,job,drift_scores,product,drift_scratch,code,stop,differential_scratch);
                    for(std::size_t j=0;j<count;++j)
                        collect_score(drift_scores[j].zero,drift_scores[j].one,j,index,groups[g].lower,f,startup_guide);
                    fft_workload->scored_start_trials+=count;
                }
            }
        }
    }
    bool score_frequency_axis(std::size_t count,std::stop_token stop,bool startup_guide=false) {
        if(!acquisition_mask.empty() || !frequency_axis_selected || drift_sections!=4 || differential_window || sample_fit)return false;
        detail::FftSearchBatch original;original.geometry=scoring_geometry();
        // The supported CZT implementation currently uses native FP64 at all
        // policy settings. Do not silently route Default/FP32 back to broad FFT.
        original.geometry.search_arithmetic=static_cast<std::uint32_t>(SearchArithmetic::fp64);
        original.observations=work;original.energy_prefix=energy_prefix;
        original.first_bin=next_start;original.starts=count;original.score_stride=count;
        original.retain_floor=search.retain_score;original.refine_threshold=threshold();
        original.refine_threshold_max=threshold_at(static_cast<long double>(trials)+acquisition_trials(count));
        const auto job_bytes=templates.size()*sizeof(detail::FftSearchJob);
        if(!cache_fits(budget,job_bytes))return false;
        std::vector<detail::FftSearchJob> jobs(templates.size());
        for(std::size_t f=0;f<jobs.size();++f) {
            jobs[f]={0,0,f,frequency_offset(f)};jobs[f].clock_ratio=clock_ratio(f);
        }
        bool spectrum_ready=false;
        for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
                const auto selected=select_starts(index,groups[g],count);
                if(!selected.selected_count) {
                    if(!acquisition_prepaid) {
                        const auto skipped=static_cast<std::uint64_t>(count)*jobs.size();
                        trials+=skipped;fft_workload->skipped_start_trials+=skipped;
                        fft_workload->skipped_template_jobs+=jobs.size();
                    }
                    continue;
                }
                stream_phase(groups[g].lower);
                for(auto& job:jobs){job.symbol=index;job.phase=groups[g].lower;}
                const std::array<detail::FftStartRange,1> full_range{{{0,count}}};
                const auto ranges=selected.full?std::span<const detail::FftStartRange>(full_range):
                    std::span<const detail::FftStartRange>(selected.ranges).first(selected.range_count);
                std::array<detail::frequency_axis::Requirements,64> requirements{};
                const auto descriptor_bytes=sizeof(requirements)+sizeof(full_range);
                const auto used=std::max(working_bytes(),fixed_reservation+cache_reservation+scoring_reservation+acquisition_mask.capacity());
                const auto output_bytes=selected.selected_count*jobs.size()*sizeof(detail::FftSearchScore);
                bool eligible=used<budget && descriptor_bytes<budget-used &&
                    job_bytes<budget-used-descriptor_bytes && output_bytes<budget-used-descriptor_bytes-job_bytes;
                auto fallback_status=detail::frequency_axis::Status::workspace;
                std::size_t ri=0;
                std::size_t scratch=0;
                if(eligible)for(const auto& range:ranges) {
                    auto part=detail::frequency_axis::choose_geometry(original.geometry,jobs,range.count,range.count,
                        budget-used-descriptor_bytes-job_bytes-output_bytes+range.count*jobs.size()*sizeof(detail::FftSearchScore),
                        detail::search_concurrency(search.worker_threads));
                    if(!part.supported()){eligible=false;fallback_status=part.status;break;}
                    scratch=std::max(scratch,part.scratch_bytes);requirements[ri++]=part;
                }
                eligible=eligible && cache_fits(budget,descriptor_bytes+job_bytes+output_bytes+scratch);
                if(eligible) {
                    const auto before=arithmetic_reservation;
                    struct Reservation {std::size_t& current;std::size_t before;~Reservation(){current=before;}} reservation{arithmetic_reservation,before};
                    arithmetic_reservation+=descriptor_bytes+job_bytes+output_bytes+scratch;
                    std::vector<detail::FftSearchScore> outputs(selected.selected_count*jobs.size());
                    std::size_t offset=0;
                    for(std::size_t r=0;r<ranges.size();++r) {
                        const auto& range=ranges[r];detail::FftSearchBatch batch;
                        batch.geometry=original.geometry;batch.observations=original.observations.subspan(range.first);
                        batch.energy_prefix=original.energy_prefix.subspan(range.first);
                        batch.first_bin=next_start+range.first;batch.starts=batch.score_stride=range.count;
                        batch.retain_floor=original.retain_floor;batch.refine_threshold=original.refine_threshold;
                        batch.refine_threshold_max=original.refine_threshold_max;
                        detail::frequency_axis::Work measured;
                        if(!detail::frequency_axis::execute(batch,jobs,requirements[r],
                            std::span(outputs).subspan(offset,requirements[r].score_count),code,&measured,stop))
                            throw Error("frequency-axis execution left its preflight domain");
                        ++fft_workload->frequency_axis_batches;
                        fft_workload->frequency_axis_transforms+=measured.forward_transforms+measured.inverse_transforms;
                        fft_workload->frequency_axis_refinements+=measured.refined_cells;
                        fft_workload->frequency_axis_peak_bytes=std::max(fft_workload->frequency_axis_peak_bytes,
                            descriptor_bytes+job_bytes+output_bytes+measured.peak_bytes-requirements[r].score_bytes);
                        offset+=requirements[r].score_count;
                    }
                    // Preserve original job/start order even across disjoint
                    // arrival cells. Only compact selected scores are stored.
                    for(std::size_t f=0;f<jobs.size();++f) {
                        collect_selected(selected,count,index,groups[g].lower,f,[&](std::size_t j) {
                            std::size_t base=0;
                            for(const auto& range:ranges) {
                                if(j>=range.first && j-range.first<range.count) {
                                    const auto& score=outputs[base+f*range.count+j-range.first];
                                    return Complex{score.zero,score.one};
                                }
                                base+=jobs.size()*range.count;
                            }
                            throw Error("frequency-axis selected start has no score");
                        },startup_guide);
                    }
                } else {
                    // A measured crossover or workspace bound can retain the
                    // existing FFT operator for this cohort. It scores the same
                    // admitted starts and keeps the original trial accounting.
                    ++fft_workload->frequency_axis_fallback_batches;
                    if(fallback_status==detail::frequency_axis::Status::crossover)++fft_workload->frequency_axis_crossover_fallbacks;
                    else if(fallback_status==detail::frequency_axis::Status::workspace || fallback_status==detail::frequency_axis::Status::score_limit)++fft_workload->frequency_axis_workspace_fallbacks;
                    fft_workload->frequency_axis_fallback_starts+=count*jobs.size();
                    if(!spectrum_ready){spectrum=work;pattern_fft(spectrum,false,stop,SearchArithmetic::fp64);++fft_workload->input_transforms;spectrum_ready=true;}
                    original.spectrum=spectrum;
                    for(std::size_t f=0;f<jobs.size();++f) {
                        detail::execute_drift_search_job(original,jobs[f],drift_scores,product,drift_scratch,code,stop,differential_scratch);
                        ++fft_workload->fft_jobs;
                        collect_selected(selected,count,index,groups[g].lower,f,[&](std::size_t j) {
                            return Complex{drift_scores[j].zero,drift_scores[j].one};
                        },startup_guide);
                    }
                }
                fft_workload->template_jobs+=jobs.size();
                fft_workload->retained_start_trials+=selected.selected_count*jobs.size();
                fft_workload->scored_start_trials+=selected.selected_count*jobs.size();
            }
        }
        return true;
    }
    void continue_observed_tracks(std::stop_token stop) {
        if(acquisition_suspended && next_start>acquisition_last_start){continue_tracks(stop);return;}
        if(long_symbol && !fft_workload->restricted_timing_active){continue_tracks(stop);return;}
        if(!fft_workload->restricted_timing_active || !search.early_qualified_fft || tracks.empty())return;
        if(component_hop) {
            const auto first=logical_hop_begin+(*timing_components)[timing_component_index].first;
            if(first>=3)continue_tracks(stop,false,first-3);
            return;
        }
        if(continuation_schedule!=next_start) {
            continuation_schedule=next_start;earliest_unscored_start=next_start;
            const auto& window=*acquisition_window;
            const auto offset=static_cast<long double>(source_start(search.initial_stream_symbols-1,phase_upper))/timing_minimum_rate;
            const auto physical_upper=search.search_stream_phases?canonical_phase_cell_upper(config,phase_upper):phase_upper;
            const auto upper=(packed_calendar || burst_plan)?packed_latest_start_samples:
                window.epoch_origin_samples+window.phase_scale*physical_upper+window.half_width_samples+offset;
            const auto magnitude=std::max({1.L,std::abs(window.epoch_origin_samples),
                window.phase_scale*phase_upper,std::abs(upper),std::abs(offset),
                static_cast<long double>(next_start)*bin_samples});
            const auto end=std::ceil((upper+4*bin_samples+
                128*std::numeric_limits<double>::epsilon()*magnitude)/bin_samples)+1;
            // Uncertain arithmetic keeps the old conservative frontier.
            if(std::isfinite(end)&&magnitude<1e15L&&end*bin_samples<1e15L&&
                end-next_start<std::numeric_limits<std::size_t>::max()) {
                earliest_unscored_start=std::numeric_limits<std::uint64_t>::max();
                if(end>next_start) {
                    const auto count=static_cast<std::size_t>(end-next_start);
                    for(std::uint64_t index=0;index<search.initial_stream_symbols;++index) {
                        std::size_t group_count=0;
                        const auto groups=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,
                            phase_upper,group_count);
                        for(std::size_t g=0;g<group_count;++g) {
                            const auto selected=select_starts(index,groups[g],count);
                            if(selected.full)earliest_unscored_start=next_start;
                            else if(selected.range_count)earliest_unscored_start=std::min(earliest_unscored_start,
                                next_start+selected.ranges[0].first);
                        }
                    }
                }
            }
        }
        // Two-bin refinements of an unscored acquisition alternative still
        // compete before continuation. Empty future unions cannot delay bits
        // or observed absence behind an unrelated full FFT hop.
        if(earliest_unscored_start==std::numeric_limits<std::uint64_t>::max())continue_tracks(stop);
        else if(earliest_unscored_start>=3)continue_tracks(stop,false,earliest_unscored_start-3);
    }
    bool prepare_timing_components() {
        timing_component_count=timing_component_index=0;
        if(final_component_replay || !search.component_qualified_search || !competing_frequencies() ||
           next_start>std::numeric_limits<std::uint64_t>::max()-hop)return false;
        const auto radius=std::max<std::uint64_t>(1,code.symbol_samples()/2);
        detail::PatternFftComponents components;
        for(std::uint64_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t groups=0;
            const auto phases=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,
                phase_upper,groups);
            for(std::size_t g=0;g<groups;++g)
                components.include(select_starts(index,phases[g],hop),bin_samples,radius);
        }
        // Every eligible component produces at most one peak. Splitting
        // cannot evade the original logical-hop candidate capacity.
        if(!components.eligible(bin_samples,radius,search.candidate_limit,competing_frequencies()))return false;
        // The range schedule persists across pushes, but unrestricted searches
        // never use it. Keep the original full-capacity reservation and allocate
        // the backing only after an eligible component schedule is known.
        if(!timing_components) {
            const auto used=working_bytes();
            if(sizeof(TimingComponents)>budget || used>budget-sizeof(TimingComponents))return false;
            try {timing_components=std::make_unique<TimingComponents>();}
            catch(const std::bad_alloc&) {return false;}
        }
        if(components.count>std::tuple_size_v<TimingComponents>)throw Error("pattern timing component capacity overflow");
        timing_component_count=static_cast<std::uint16_t>(components.count);
        std::copy_n(components.ranges.begin(),components.count,timing_components->begin());
        logical_hop_begin=next_start;logical_hop_end=next_start+hop;
        component_hop=true;acquisition_prepaid=false;
        return true;
    }
    void checkpoint_early_hop() {
        const auto begin=component_hop?logical_hop_begin:next_start;
        // Only one executed logical hop can precede its original observation
        // frontier. Preparing the next component schedule must not overwrite it.
        if(bins-length+1>=begin+hop)return;
        if(early_eof_pending && early_eof_begin==begin)return;
        if(early_eof_pending && bins-length+1<early_eof_end)
            throw Error("overlapping unobserved acquisition checkpoints");
        if(diagnostic_tail) {
            if(tracks.empty())diagnostic_tail->checkpoint(history);
            else diagnostic_tail->release_checkpoint();
        }
        early_eof_pending=true;early_eof_replayable=tracks.empty();
        early_eof_begin=begin;early_eof_end=begin+hop;
        early_trials_before=trials;early_history_before=history_insertions;
    }
    void prepay_component_trials() {
        if(acquisition_prepaid)return;
        const auto full=acquisition_trials(hop);
        std::uint64_t retained=0;
        for(std::uint64_t index=0;index<search.initial_stream_symbols;++index) {
            std::size_t groups=0;
            const auto phases=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,
                phase_upper,groups);
            for(std::size_t g=0;g<groups;++g) {
                const auto geometry=window_geometry(index,phases[g]);
                const auto selected=detail::pattern_fft_select_starts(*acquisition_window,geometry.samples,geometry.index,
                    geometry.lower,geometry.upper,geometry.step,logical_hop_begin,hop,bin_samples,
                    timing_minimum_rate,timing_maximum_rate);
                retained+=static_cast<std::uint64_t>(selected.selected_count)*templates.size();
                if(!selected.selected_count)fft_workload->skipped_template_jobs+=templates.size();
            }
        }
        trials+=full;fft_workload->skipped_start_trials+=full-retained;acquisition_prepaid=true;
    }
    void advance_acquisition(std::size_t count,std::stop_token stop) {
        if(!component_hop){next_start+=count;continue_tracks(stop);return;}
        ++fft_workload->timing_components;
        const auto end=logical_hop_begin+(*timing_components)[timing_component_index].first+
            (*timing_components)[timing_component_index].count;
        next_start+=count;
        if(next_start>=end && ++timing_component_index==timing_component_count) {
            next_start=logical_hop_end;component_hop=acquisition_prepaid=false;
        }
        // Never consume later observations ahead of an unscored competitor.
        continue_observed_tracks(stop);
    }
    bool hold_prior_batch(std::size_t count) {
        if(!prior_dispatch)return false;
        prior_pending_count=count;
        ++fft_workload->deferred_original_batches;
        return true;
    }
    void finish_observed(std::stop_token stop) {
        struct FinalScope {
            bool& flag;bool previous;
            explicit FinalScope(bool& value):flag(value),previous(value){}
            ~FinalScope(){flag=previous;}
        } final_scope(final_component_replay);
        if(early_eof_pending && early_eof_replayable) {
            // Unknown EOF can end before the remaining timing components are
            // observed. The old finite-final path charges only its observed
            // batch. Replay an unpublished cold hop from retained PCM instead
            // of leaving a tentative candidate under the larger live charge.
            // No established output/state may be rolled back. Complete normal
            // hops and published receptions keep their live accounting.
            if(bins-early_eof_begin>ring.size())throw Error("partial component replay lost observed samples");
            tracks.clear();trials=early_trials_before;
            if(diagnostic_tail)diagnostic_tail->rollback(history,history_insertions-early_history_before);
            else {
                const auto appended=std::min<std::uint64_t>(history.size(),history_insertions-early_history_before);
                history.resize(history.size()-static_cast<std::size_t>(appended));
            }
            ++fft_workload->eof_replays;next_start=early_eof_begin;early_eof_pending=false;
            component_hop=acquisition_prepaid=false;
            scheduled_start=continuation_schedule=std::numeric_limits<std::uint64_t>::max();
            timing_component_count=timing_component_index=0;
            final_component_replay=true;
        }
        if(!component_hop || !acquisition_prepaid) {
            if(component_hop)next_start=logical_hop_begin;
            component_hop=acquisition_prepaid=false;
            scheduled_start=continuation_schedule=std::numeric_limits<std::uint64_t>::max();
            timing_component_count=timing_component_index=0;
            final_component_replay=true;
        }
        process(stop,true);
    }
    bool startup_guide_owner(std::size_t count,bool final,bool charged=false) const {
        // Only the original startup cohort and its immediately following full
        // hop have a complete, immutable proof without additional track state.
        if(final || !acquisition_mask.empty() || !long_symbol || !shaped || sample_fit || drift_sections!=4 || differential_window ||
           config.scramble || config.dsss || config.dsss_factor!=1 || legacy_public_pattern(config) ||
           config.fake_hopping || calendar_symbols() || burst_plan || packed_calendar ||
           search.search_stream_phases || search.initial_stream_symbols!=1 ||
           search.hypotheses.empty() || !competing_frequencies() ||
           fft_workload->restricted_timing_active || acquisition_window ||
           component_hop || acquisition_prepaid || early_eof_pending || final_component_replay ||
           search.candidate_limit<2 || tracks.size()!=1 || completed.size()!=1 ||
           (config.search_arithmetic!=SearchArithmetic::default_mode && config.search_arithmetic!=SearchArithmetic::fp64))return false;
        const auto initial=std::min(hop,std::max<std::size_t>(1,config.sample_rate/bin_samples));
        if(next_start!=initial || count!=hop || !initial || !count)return false;
        const auto expected=acquisition_trials(initial)+(charged?acquisition_trials(count):0);
        if(trials!=expected)return false;
        const auto& t=tracks.front();const auto& b=t.burst;
        if(!t.admitted || !t.established || t.index!=1 || b.stream_first_symbol!=0 ||
           b.first_stream_symbol!=1 || !b.bits.empty() || t.confirmed || t.pending_gap || t.gap_slots ||
           t.unconfirmed_symbols || t.absent_samples!=0 || t.pending_score!=0 || t.penalty!=0 ||
           latest.complete || latest.missing_slots || latest.bits.size()!=1 || latest.bits.front()>1 ||
           latest.first_stream_symbol!=0 || latest.stream_first_symbol!=0 ||
           latest.first_sample!=b.stream_first_sample || latest.stream_first_sample!=b.stream_first_sample ||
           latest.end_sample!=t.confirmed_end || b.end_sample!=t.confirmed_end || b.first_sample!=t.confirmed_end ||
           latest.frequency_hz!=b.frequency_hz || latest.stream_phase_samples!=t.phase_lower ||
           t.phase_lower!=t.phase_upper || t.phase_lower!=config.stream_phase_samples ||
           latest.score!=t.confirmed_score || latest.score!=t.total_score ||
           latest.support_samples!=t.confirmed_support || latest.support_samples!=t.total_support)return false;
        if(latest.first_sample%bin_samples || latest.first_sample/bin_samples>=initial)return false;
        const auto& span=completed.front();
        if(span.first!=latest.first_sample || span.end!=latest.end_sample || span.frequency!=latest.frequency_hz)return false;
        const auto radius=std::max<std::uint64_t>(1,code.symbol_samples()/2);
        if(latest.first_sample>=radius || latest.first_sample>std::numeric_limits<std::uint64_t>::max()-(radius-1) ||
           count>std::numeric_limits<std::uint64_t>::max()-next_start)return false;
        const auto last=(latest.first_sample+radius-1)/bin_samples;
        return next_start+count>last;
    }
    bool adopt_startup_guide(std::size_t count,bool final) {
        if(!startup_guide_owner(count,final,true) || peaks.size()!=2)return false;
        auto& t=tracks.front();const auto bit=latest.bits.front();
        const auto guide=peaks[bit];const auto& opposite=peaks[1-bit];
        const auto radius=std::max<std::uint64_t>(1,code.symbol_samples()/2);
        if(guide.bit!=bit || opposite.bit!=1-bit || opposite.score<0 ||
           !std::isfinite(guide.score) || !std::isfinite(opposite.score) ||
           guide.score<=latest.score || guide.score<=opposite.score || guide.score<threshold() ||
           guide.score-guide.alternative_score<1 || guide.stream_symbol!=0 ||
           guide.stream_phase_samples!=t.phase_lower || guide.first_sample<=latest.first_sample ||
           guide.first_sample-latest.first_sample>=radius || guide.first_sample%bin_samples ||
           guide.end_sample<guide.first_sample || guide.end_sample-guide.first_sample!=length*bin_samples ||
           guide.end_sample>sample || guide.end_sample<t.confirmed_end)return false;
        const auto f=static_cast<std::size_t>(guide.frequency_hypothesis);
        if(f>=templates.size() || f>=search.hypotheses.size() || f>=t.frequency_scores.size() ||
           guide.frequency_hz!=config.carrier_hz+frequency_offset(f))return false;
        const auto start=guide.first_sample/bin_samples;
        const auto advance=advance_bins(guide.stream_symbol,t.phase_lower,f);
        if(start>std::numeric_limits<std::uint64_t>::max()-advance)return false;
        const auto old_end=t.confirmed_end;
        const auto consumed=std::max(t.burst.end_sample,guide.end_sample);
        // This is the same published prefix. It emits no bit and lends no
        // reused score/support to future admission; measure excludes this fence.
        t.anchor_start=start;t.anchor_index=guide.stream_symbol;t.frequency=f;t.next=start+advance;
        t.burst.first_sample=t.burst.end_sample=t.confirmed_end=consumed;
        t.burst.frequency_hz=guide.frequency_hz;
        std::fill(t.frequency_scores.begin(),t.frequency_scores.end(),0.);
        t.frequency_scores[f]=guide.score;
        tracking_reference_valid=false;
        // Retain ownership over the contiguous union, including its new tail.
        completed.front().end=std::max(completed.front().end,guide.end_sample);
        audio::trace::record("pattern_startup_guide",[&](auto& fields) {
            fields.add("stream_first_sample",latest.stream_first_sample);fields.add("stream_first_symbol",latest.stream_first_symbol);
            fields.add("published_end",latest.end_sample);fields.add("old_consumed_end",old_end);
            fields.add("guide_first",guide.first_sample);fields.add("guide_end",guide.end_sample);
            fields.add("guide_lane",f);fields.add("guide_frequency",guide.frequency_hz);fields.add("guide_score",guide.score);
            fields.add("opposite_score",opposite.score);fields.add("threshold",threshold());fields.add("trials",trials);
            fields.add("public_score",t.confirmed_score);fields.add("public_support",t.confirmed_support);fields.add("newly_confirmed",0);
        });
        return true;
    }
    void process(std::stop_token stop,bool final=false) {
        if(prior_pending_count)return;
        if(acquisition_suspended && next_start>acquisition_last_start) {
            continue_tracks(stop,final);return;
        }
        // The first long-symbol pass needs only a small range of fully
        // observed starts. Later passes retain efficient bounded batches.
        // Start coordinates, rather than polling/chunk boundaries, schedule
        // acquisition, and each start still competes across the entire bank.
        const auto next_count=[&] {
            if(fft_workload->restricted_timing_active && search.early_qualified_fft) {
                if(component_hop) {
                    const auto& component=(*timing_components)[timing_component_index];
                    return static_cast<std::size_t>(logical_hop_begin+component.first+component.count-next_start)+4;
                }
                if(scheduled_start!=next_start) {
                    scheduled_start=next_start;scheduled_observed_starts=0;
                    if(prepare_timing_components()) {
                        const auto& component=(*timing_components)[0];
                        return component.first+component.count+4;
                    }
                    if(search.component_qualified_search)++fft_workload->component_fallbacks;
                    for(std::uint64_t index=0;index<search.initial_stream_symbols;++index) {
                        std::size_t groups=0;
                        const auto phases=phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,
                            phase_upper,groups);
                        for(std::size_t g=0;g<groups;++g) {
                            const auto selected=select_starts(index,phases[g],hop);
                            const auto end=selected.full?hop:selected.range_count?
                                selected.ranges[selected.range_count-1].first+selected.ranges[selected.range_count-1].count:0;
                            scheduled_observed_starts=std::max(scheduled_observed_starts,end);
                        }
                    }
                    // Empty batches have no acquisition latency to improve.
                    // Keep their cheap skip cadence instead of revisiting an
                    // empty phase union for every newly observed input bin.
                    if(!scheduled_observed_starts)scheduled_observed_starts=hop;
                }
                return scheduled_observed_starts;
            }
            return long_symbol && next_start==0?
                std::min(hop,std::max<std::size_t>(1,config.sample_rate/bin_samples)):hop;
        };
        while(bins>=length+next_start && (final || bins-length+1-next_start>=next_count())) {
            if(acquisition_suspended && next_start>acquisition_last_start)break;
            cancelled(stop);
            const auto observed=static_cast<std::size_t>(std::min<std::uint64_t>(hop,bins-length+1-next_start));
            const bool early=!final && fft_workload->restricted_timing_active && search.early_qualified_fft &&
                observed>=next_count() && observed<hop;
            // Preserve the original logical batch and its full trial charge.
            // Only excluded starts can extend beyond observed PCM. Every
            // selected correlation still ends within the actual observation.
            const auto count=component_hop?std::min(observed,static_cast<std::size_t>(logical_hop_begin+
                (*timing_components)[timing_component_index].first+(*timing_components)[timing_component_index].count-next_start)):
                early?hop:observed;
            const auto batch_origin=frequency_origin_hz;
            const auto* batch_phase_view=execution_phase_view;
            if(prior_dispatch && !acquisition_prepaid) {
                // A subset spends the full original catalogue once. An
                // adaptive translated catalogue additionally spends its own
                // full attempt, monotonically across reference changes.
                const auto full=acquisition_trials(count);
                if(full>UINT64_MAX-trials || ((frequency_origin_hz!=0 || execution_phase_view) && full>UINT64_MAX-shifted_trials))
                    throw Error("conditional acquisition trial count overflow");
                trials+=full;if(frequency_origin_hz!=0 || execution_phase_view)shifted_trials+=full;
                acquisition_prepaid=true;
            }
            if(!final && (early || component_hop))checkpoint_early_hop();
            if(component_hop)prepay_component_trials();
            if(!fft_batch_may_intersect(count)) {
                // Charge precisely the original acquisition alternatives.
                // Continue existing tracks even after this acquisition bank's
                // initial templates have passed their qualified UTC window.
                const auto skipped=acquisition_trials(count);
                if(!acquisition_prepaid) {
                    trials+=skipped;fft_workload->skipped_start_trials+=skipped;
                    fft_workload->skipped_template_jobs+=skipped/count;
                }
                ++fft_workload->skipped_batches;
                if(hold_prior_batch(count))return;
                advance_acquisition(count,stop);continue;
            }
            // Drained decisions already veto overlapping acquisition in
            // admit(). If one retained span rejects this entire batch for
            // every carrier, its transforms cannot yield an admissible peak.
            // Keep charging the same trials before continuation; only the
            // diagnostic history of these rejected overlaps is omitted.
            // Ready continuation can rotate the retained span list before
            // admission, so let such batches follow the ordinary path.
            const auto stable_spans=std::none_of(tracks.begin(),tracks.end(),[&](const Track& track) {
                return track.next+length+2<=bins;
            });
            const auto covered=long_symbol && stable_spans && std::any_of(completed.begin(),completed.end(),[&](const Completed& span) {
                const auto index=static_cast<std::size_t>(&span-completed.data());
                // A helper-born published alias cannot prove that every
                // original source alignment in this whole batch is duplicate.
                if(helper_overlap && helper_overlap->completed[index])return false;
                if((next_start+count-1)*bin_samples>=span.end ||
                   (next_start+length)*bin_samples<=span.first)return false;
                return competing_frequencies() ||
                    std::all_of(search.frequency_offsets_hz.begin(),search.frequency_offsets_hz.end(),[&](double offset) {
                        return std::abs(config.carrier_hz+offset+frequency_origin_hz-span.frequency)<=
                            static_cast<double>(config.sample_rate)/static_cast<double>(code.symbol_samples());
                    });
            });
            const bool startup_guide=covered && startup_guide_owner(count,final);
            if(covered && !startup_guide) {
                for(std::size_t index=0;index<search.initial_stream_symbols;++index) {
                    std::size_t groups=0;
                    (void)phase_groups(index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,groups);
                    if(!acquisition_prepaid)trials+=static_cast<std::uint64_t>(count)*templates.size()*groups;
                }
                if(hold_prior_batch(count))return;
                advance_acquisition(count,stop);continue;
            }
            ensure_execution_scratch();
            prepare_phase_window(next_start,observed+length-1,stop);
            std::fill(work.begin(),work.end(),Complex{});energy_prefix[0]=0;
            for(std::size_t i=0;i<observed+length-1;++i) {
                work[i]=at(next_start+i);energy_prefix[i+1]=energy_prefix[i]+std::norm(work[i]);
            }
            // Input is now materialized in work. FFT/template paths may reuse
            // spectrum/reference; no cached phase span may survive that reuse.
            invalidate_phase_cache();
            if(acquisition_arithmetic()==SearchArithmetic::fp32 &&
               !detail::search_fft::float_input_safe(std::span<const Complex>(work),transform)) {
                // Keep the requested mode but normalize every subsequently
                // prepared upload before using the range fallback.
                fp32_exact_fallback=true;drop_template_cache();templates_valid=false;
                ++fft_workload->arithmetic_fallback_jobs;
            }
            if(early)++fft_workload->early_batches;
            // Explicit cohort leases return this transient backing after a call.
            // Unleased calls still retain the full original candidate quota.
            if(peaks.capacity()<search.candidate_limit)peaks.reserve(search.candidate_limit);
            peaks.clear();
            if(startup_guide) {
                peaks.resize(2);
                for(unsigned bit=0;bit<2;++bit){peaks[bit].score=-1;peaks[bit].bit=bit;}
            }
            if(!score_frequency_axis(count,stop,startup_guide) && !score_bounded(count,observed,stop,startup_guide) && !score_native_whole(count,stop,startup_guide)) {
            if(acquisition_arithmetic()==SearchArithmetic::fp32) {
                // A mixed cohort may reject both bounded execution and the
                // native whole path. Original operands must be rebuilt before
                // shared FFT uploads; neither policy enables legacy wide FP32.
                fp32_exact_fallback=true;drop_template_cache();templates_valid=false;
                ++fft_workload->arithmetic_fallback_jobs;
            }
            prepare_scoring(stop);
            spectrum=work;pattern_fft(spectrum,false,stop,acquisition_arithmetic());++fft_workload->input_transforms;
            // The input transform no longer needs work. Reuse it for the
            // identical carrier Gram phase shared by every bit/frequency/
            // stream hypothesis at a start, without new allocations or a
            // phase recurrence that would change the numerical calculation.
            if(sample_fit && drift_sections<=1)for(std::size_t j=0;j<count;++j)work[j]=carrier_square(next_start+j);
            if(!scoring.empty())score_parallel(count,stop,startup_guide);
            else if(drift_sections>1)score_drift_serial(count,stop,startup_guide);
            else {
            // Serial acquisition reuses the fallback tracking-cache buffer.
            // A later measure must rebuild it before any timing refinement.
            if(tracking_reference.empty())tracking_reference_valid=false;
            for(std::size_t stream_index=0;stream_index<search.initial_stream_symbols;++stream_index) {
            std::size_t group_count=0;
            const auto groups=phase_groups(stream_index,search.search_stream_phases?0:config.stream_phase_samples,phase_upper,group_count);
            for(std::size_t g=0;g<group_count;++g) {
            const auto selected=select_starts(stream_index,groups[g],count);
            if(!selected.selected_count) {
                const auto skipped=static_cast<std::uint64_t>(count)*templates.size();
                if(!acquisition_prepaid) {
                    trials+=skipped;fft_workload->skipped_start_trials+=skipped;
                    fft_workload->skipped_template_jobs+=templates.size();
                }
                continue;
            }
            stream_phase(groups[g].lower);
            prepare_templates(stream_index,stop);
            const auto& rows=cached_templates.empty()?templates:template_bank(stream_index).rows;
            const auto& energies=cached_templates.empty()?template_energy:template_bank(stream_index).energy;
            const auto& squares=cached_templates.empty()?template_square:template_bank(stream_index).square;
            for(std::size_t f=0;f<templates.size();++f) {
                if(!acquisition_lane(f))continue;
                ++fft_workload->template_jobs;
                fft_workload->retained_start_trials+=selected.selected_count;
                const auto direct=fft_workload->restricted_timing_active && search.restricted_direct_search && streamed_templates &&
                    detail::pattern_fft_direct_eligible(scoring_geometry(),true,selected.selected_count,!sample_fit);
                if(direct)++fft_workload->direct_jobs;else ++fft_workload->fft_jobs;
                for(unsigned b=0;b<2;++b) {
                    auto norm=energies[f][b];auto square=squares[f][b];
                    if(streamed_templates) {
                        std::fill(product.begin(),product.end(),Complex{});norm=0;square={};
                        for(std::size_t i=0;i<length;++i) {
                            const auto value=arithmetic_value(template_value(i,stream_index,b,f));
                            if((i&4095U)==0)cancelled(stop);
                            product[direct?i:length-1-i]=std::conj(value);norm+=std::norm(value);
                            if(sample_fit)square+=value*value*carrier_square(i);
                        }
                        if(!direct) {
                            pattern_fft(product,false,stop,acquisition_arithmetic());
                            for(std::size_t i=0;i<transform;++i)product[i]=arithmetic_product(spectrum[i],product[i]);
                        }
                    } else for(std::size_t i=0;i<transform;++i)product[i]=arithmetic_product(spectrum[i],rows[f][b][i]);
                    if(!direct)pattern_fft(product,true,stop,acquisition_arithmetic());
                    visit_starts(selected,count,[&](std::size_t j) {
                        Complex dot{};
                        if(direct)for(std::size_t i=0;i<length;++i) {
                            if((i&4095U)==0)cancelled(stop);
                            dot=arithmetic_value(dot+arithmetic_product(work[j+i],product[i]));
                        }
                        else dot=product[length-1+j];
                        const auto score=pattern_evidence(dot,energy_prefix[j+length]-energy_prefix[j],
                            norm,evidence_count(length),noise_condition,real_rank,
                            sample_fit,sample_fit?square*work[j]:Complex{});
                        if(b==0)reference[j]={score,0};else reference[j].imag(score);
                    });
                }
                fft_workload->scored_start_trials+=selected.selected_count;
                collect_selected(selected,count,stream_index,groups[g].lower,f,[&](std::size_t j){return reference[j];},startup_guide);
            }
            }
            }
            }
            }
            if(startup_guide) {
                // Every original cell/trial is complete before changing any
                // private geometry. Refusal retains the original covered skip.
                (void)adopt_startup_guide(count,final);
                peaks.clear();advance_acquisition(count,stop);continue;
            }
            std::sort(peaks.begin(),peaks.end(),[](const auto& a,const auto& b){return a.first_sample<b.first_sample;});
            for(const auto& peak:peaks) {
                // Advance through this candidate's timing before overlap
                // arbitration, including the two-bin refinement tolerance.
                // Consuming later symbols first could promote a weak track
                // onto a suffix before its stronger prefix is considered.
                const auto start=peak.first_sample/bin_samples;
                continue_tracks(stop,false,start>std::numeric_limits<std::uint64_t>::max()-2?
                    std::numeric_limits<std::uint64_t>::max():start+2);
                frequency_origin_hz=batch_origin;
                execution_phase_view=batch_phase_view;
                auto checked=peak;
                double chain_score=checked.score;
                if(outer_presence_check(checked,0,stop,&chain_score))admit(checked,checked.frequency_hypothesis,chain_score,batch_origin,batch_phase_view,stop);
                else remember(checked);
            }
            if(hold_prior_batch(count))return;
            advance_acquisition(count,stop);
        }
        if(final)continue_tracks(stop,true);
    }
    void bin(Complex value,std::stop_token stop) {
        ring[static_cast<std::size_t>(bins%ring.size())]=value;++bins;
        if(phase_curve)phase_curve->mark_observed(sample<=UINT64_MAX-search.input_sample_origin?
            search.input_sample_origin+sample:UINT64_MAX);
        if(early_eof_pending && bins>=length && bins-length+1>=early_eof_end) {
            early_eof_pending=false;
            if(diagnostic_tail)diagnostic_tail->release_checkpoint();
        }
        const auto point=std::abs(previous_chip)>1e-20?value*std::conj(previous_chip)/std::abs(previous_chip):value;
        if(points.size()==2048){points[point_cursor]=point;point_cursor=(point_cursor+1)%points.size();}
        else points.push_back(point);
        previous_chip=value;
        if(!defer_acquisition)process(stop);
    }
    std::vector<Complex> chip_points()const {
        std::vector<Complex> result;result.reserve(points.size());
        for(std::size_t i=0;i<points.size();++i)result.push_back(points[(point_cursor+i)%points.size()]);
        return result;
    }
    const Track* active_track()const {
        const Track* best=nullptr;
        for(const auto& track:tracks)if(track.admitted&&(!best||track.total_score>best->total_score))best=&track;
        return best;
    }
    const PatternBurst& current_burst()const {
        const auto* track=active_track();return track?track->burst:latest;
    }
    PatternBurst provisional()const {
        const auto* track=active_track();
        if(!track)return latest;
        PatternBurst result=track->burst;result.bits.resize(track->confirmed);result.end_sample=track->confirmed_end;
        // A provisional observation never claims the physical stream ended.
        result.complete=false;
        return result;
    }
    void suspend_acquisition() {
        if(!burst_plan || acquisition_suspended)return;
        auto last=static_cast<long double>(sample);
        if(search.start_offset_seconds) {
            last=(static_cast<long double>(*search.start_offset_seconds)+search.start_uncertainty_seconds)*config.sample_rate;
            last+=config.stream_phase_samples+
                static_cast<long double>(source_start(search.initial_stream_symbols-1,config.stream_phase_samples))/timing_minimum_rate;
            if(acquisition_window)last=packed_latest_start_samples;
        }
        const auto bound=std::max(0.L,std::ceil(last/bin_samples)+4);
        if(!std::isfinite(bound) || bound>std::numeric_limits<std::uint64_t>::max()-length-hop-4)
            throw Error("Symbol_Burst acquisition fence exceeds sample coordinates");
        acquisition_last_start=static_cast<std::uint64_t>(bound);
        // Exclusion separately requires next_start>acquisition_last_start:
        // every selected start has therefore already finished its whole fit.
        // The trailing capture guard dates the latest possible waveform
        // support, rather than adding the scorer's FFT processing lookahead.
        acquisition_capture_end=symbol_schedule_detail::multiply(
            symbol_schedule_detail::add(acquisition_last_start,length+4),bin_samples);
        acquisition_capture_end=symbol_schedule_detail::add(acquisition_capture_end,
            symbol_schedule_detail::add(static_cast<std::uint64_t>(std::ceil(20.L*config.sample_rate*
                detail::original_physical_guard_factor(burst_policy->minimum_rate,burst_policy->maximum_rate))),
                burst_plan->padding_samples()));
        acquisition_suspended=true;
    }
    std::optional<std::uint64_t> burst_unobserved_end(std::uint64_t from) const {
        if(!burst_plan || burst_policy->capture_revoked || !acquisition_suspended || next_start<=acquisition_last_start ||
           from<acquisition_capture_end || tracks.empty())return std::nullopt;
        auto end=static_cast<long double>(std::numeric_limits<std::uint64_t>::max());
        for(const auto& track:tracks) {
            const auto radius=track_radius(track);
            const auto guard=20.L*config.sample_rate*detail::original_physical_guard_factor(
                burst_policy->minimum_rate,burst_policy->maximum_rate)+
                static_cast<long double>(burst_plan->padding_samples())/clock_ratio(track.frequency);
            const auto begin=(static_cast<long double>(track.next)-radius)*bin_samples-guard;
            if(track.index) {
                const auto previous=track_start(track,track.index-1);
                const auto previous_end=(static_cast<long double>(previous)+length+radius)*bin_samples+guard;
                if(static_cast<long double>(from)<previous_end)return std::nullopt;
            }
            if(static_cast<long double>(from)>=begin)return std::nullopt;
            end=std::min(end,std::floor(begin/bin_samples)*bin_samples);
        }
        if(end<=from || end>=std::numeric_limits<std::uint64_t>::max())return std::nullopt;
        return static_cast<std::uint64_t>(end);
    }
    std::size_t working_bytes()const {
        if(fallback)return wrapper_bytes()+fallback->working_bytes();
        std::size_t total=acquisition_mask.capacity()+sizeof(Impl)+sizeof(PatternReceiver)+code.working_bytes()+sizeof(PatternFftWork)+(timing_components?sizeof(TimingComponents):0);
        for(const auto* v:{&ring,&work,&spectrum,&product,&reference,&points})total+=v->capacity()*sizeof(Complex);
        total+=energy_prefix.capacity()*sizeof(double)+(history.capacity()+peaks.capacity())*sizeof(PatternEvidence)+
            drift_scratch.capacity()*sizeof(detail::FftDriftAccumulator)+drift_scores.capacity()*sizeof(detail::FftSearchScore)+
            differential_scratch.capacity()*sizeof(detail::DifferentialAccumulator)+
            tracks.capacity()*sizeof(Track)+bursts.capacity()*sizeof(PatternBurst)+completed.capacity()*sizeof(Completed)+
            templates.capacity()*sizeof(decltype(templates)::value_type)+
            template_energy.capacity()*sizeof(decltype(template_energy)::value_type)+
            template_square.capacity()*sizeof(decltype(template_square)::value_type)+
            (search.frequency_offsets_hz.capacity()+search.clock_errors_ppm.capacity())*sizeof(double)+
            search.hypotheses.capacity()*sizeof(PatternFrequencyRateHypothesis)+
            packed_phases.capacity()*sizeof(std::uint64_t)+(packed_calendar?packed_calendar->working_bytes():0)+
            (burst_plan?burst_plan->working_bytes()+burst_cursor->working_bytes()+sizeof(BurstPolicy):0);
        for(const auto& row:templates)for(const auto& v:row)total+=v.capacity()*sizeof(Complex);
        total+=cached_templates.capacity()*sizeof(CachedTemplates);
        if(exact_initial_rows)total+=sizeof(ExactInitialRows);
        total+=tracking_reference.capacity()*sizeof(decltype(tracking_reference)::value_type);
        for(const auto& bank:cached_templates) {
            total+=bank.rows.capacity()*sizeof(decltype(templates)::value_type)+
                bank.energy.capacity()*sizeof(decltype(template_energy)::value_type)+
                bank.square.capacity()*sizeof(decltype(template_square)::value_type);
            for(const auto& row:bank.rows)for(const auto& values:row)total+=values.capacity()*sizeof(Complex);
        }
        for(const auto& track:tracks)total+=track.burst.bits.capacity()+track.frequency_scores.capacity()*sizeof(double);
        for(const auto& burst:bursts)total+=burst.bits.capacity();
        return total+(helper_overlap?helper_overlap->working_bytes():0)+(diagnostic_tail?diagnostic_tail->working_bytes():0)+latest.bits.capacity()+scoring_reservation+(work.empty()?0:arithmetic_reservation)+native_active_bytes+native_plan_bytes();
    }
    std::size_t wrapper_bytes()const {
        return acquisition_mask.capacity()+sizeof(Impl)+sizeof(PatternReceiver)+code.working_bytes()+(fft_workload?sizeof(PatternFftWork):0)+(timing_components?sizeof(TimingComponents):0)+
            search.frequency_offsets_hz.capacity()*sizeof(double)+search.clock_errors_ppm.capacity()*sizeof(double)+
            search.hypotheses.capacity()*sizeof(PatternFrequencyRateHypothesis)+
            packed_phases.capacity()*sizeof(std::uint64_t)+(packed_calendar?packed_calendar->working_bytes():0)+
            (burst_plan?burst_plan->working_bytes()+burst_cursor->working_bytes()+sizeof(BurstPolicy):0);
    }
    std::size_t retained_bit_reservation(std::size_t fixed)const {
        // Match the setter's uniform per-vector capacity proof, including
        // its nonzero candidate allowance, for current or projected state.
        std::size_t retained=std::max<std::size_t>(1,latest.bits.capacity());
        for(const auto& track:tracks)retained=std::max(retained,track.burst.bits.capacity());
        for(const auto& burst:bursts)retained=std::max(retained,burst.bits.capacity());
        const auto maximum=std::numeric_limits<std::size_t>::max();
        if(search.track_limit>(maximum-2)/2)
            throw Error("pattern retained bit reservation exceeds address space");
        const auto slots=2*search.track_limit+2;
        if(retained>(maximum-fixed)/slots)
            throw Error("pattern retained bit reservation exceeds address space");
        return fixed+slots*retained;
    }
    void set_workspace_bytes(std::size_t bytes) {
        if(fallback) {
            const auto wrapper=wrapper_bytes();
            if(wrapper>=bytes)throw Error("pattern workspace cannot retain correlator control state");
            fallback->set_workspace_bytes(bytes-wrapper);budget=bytes;return;
        }
        if(!acquisition_mask.empty() && !cache_fits(bytes,0))clear_prior(!prior_pending_count && !prior_dispatch);
        if(!cache_fits(bytes,0)){native_float_plan.reset();native_double_plan.reset();}
        if(!scoring.empty() && !cache_fits(bytes,0))drop_scoring();
        if((!cached_templates.empty() || exact_initial_rows) && !cache_fits(bytes,0))drop_template_cache();
        if(bytes<fixed_reservation||working_bytes()>bytes)throw Error("pattern workspace cannot retain current correlation state");
        const auto limit=std::min(configured_bit_limit,(bytes-fixed_reservation)/(2*search.track_limit+2));
        if(!limit)throw Error("pattern workspace cannot retain bit candidates");
        const auto fits=[&](const Bytes& bits){return bits.capacity()<=limit;};
        if(!fits(latest.bits)||std::any_of(tracks.begin(),tracks.end(),[&](const auto& track){return !fits(track.burst.bits);})||
            std::any_of(bursts.begin(),bursts.end(),[&](const auto& burst){return !fits(burst.bits);}))
            throw Error("pattern workspace cannot retain existing bit candidates");
        search.bit_limit=limit;budget=bytes;
    }
};
std::uint64_t pattern_absence_samples(const Config& config) {
    const auto symbol=symbol_sample_count(config);
    const std::uint64_t target=pattern_absence_seconds*config.sample_rate;
    const auto count=std::max<std::uint64_t>(1,target/symbol+(target%symbol!=0));
    if(count>std::numeric_limits<std::uint64_t>::max()/symbol)throw Error("pattern absence duration overflow");
    return count*symbol;
}
std::uint64_t pattern_absence_capture_samples(const Config& config) {
    const auto eligible=pattern_absence_samples(config);
    if(config.symbol_burst.enabled()) {
        const SymbolBurstPlan plan(config);
        const auto slots=eligible/symbol_sample_count(config);
        const auto span=symbol_schedule_detail::add(
            symbol_schedule_detail::multiply(2,plan.padding_samples()),
            symbol_schedule_detail::multiply(config.symbol_burst.wait_symbols+plan.random_limit()+1,plan.symbol_samples()));
        const auto maximum=symbol_schedule_detail::add(std::max<std::uint64_t>(span,60ULL*config.sample_rate),30ULL*config.sample_rate);
        return symbol_schedule_detail::multiply(slots+1,maximum);
    }
    if(packed_scheduled_symbols(config)) {
        const ScheduledSymbolCalendar calendar(config);
        const auto slots=eligible/symbol_sample_count(config);
        // Waiting cover consumes source samples but is never a failed symbol.
        // Reserve enough complete keyed groups to score every required slot,
        // including an unfavorable initial placement and final group residue.
        const auto groups=symbol_schedule_detail::add(1,
            slots/calendar.slots_per_group()+(slots%calendar.slots_per_group()!=0));
        const auto group=symbol_schedule_detail::multiply(2ULL*config.sample_rate/5,
            config.scheduled_hopping->group_hops);
        return scheduled_physical_span_samples(config,symbol_schedule_detail::multiply(groups,group));
    }
    if(scheduled_bursts(config))return scheduled_physical_span_samples(config,eligible);
    if(deferred_symbols(config)) {
        const auto count=eligible/symbol_sample_count(config),symbol=symbol_sample_count(config);
        const FhssSymbolCalendar calendar(*config.fake_hopping,config.sample_rate,pattern_pulse_padding_samples(config),symbol);
        // Worst phase: wait one hop, then score all required eligible symbols.
        // This only sizes physical capture tails; the scorer still requires
        // ceil(6/T) fully observed failed symbols, excluding every planned gap.
        const auto hops=1+count/calendar.per_hop+(count%calendar.per_hop!=0);
        return calendar.boundary(hops,true);
    }
    return eligible;
}
PatternReceiver::PatternReceiver(Config c,std::size_t bytes,PatternSearch search):impl_(std::make_unique<Impl>(c,bytes,std::move(search),detail::NativePeakConstruction::claim())) {
    audio::trace::record("pattern_receiver_engine",[&](auto& f) {
        f.add("epoch",impl_->config.stream_epoch);f.add("keyed",impl_->config.data_key.has_value());
        f.add("sample_origin",impl_->search.input_sample_origin);
        f.add("symbol_samples",impl_->code.symbol_samples());f.add("chip_samples",impl_->code.chip_samples());
        f.add("engine",impl_->fallback?"PatternCorrelator fallback":"PatternReceiver FFT");
        if(!impl_->fallback) {
            f.add("sample_fit",impl_->sample_fit);f.add("real_rank",impl_->real_rank);
            f.add("bin_samples",impl_->bin_samples);f.add("noise_condition",impl_->noise_condition);
        }
    });
}
detail::NativePeakLease::NativePeakLease(detail::NativePeakWorkspace* workspace,PatternReceiver& receiver)
    :NativePeakLease(receiver.impl_->fallback?nullptr:workspace,receiver.impl_->peaks,
                     receiver.impl_->search.candidate_limit) {}
bool detail::NativePeakLease::adopt(detail::NativePeakWorkspace& workspace,PatternReceiver& receiver) {
    auto& s=*receiver.impl_;
    return !s.fallback && workspace.adopt(s.peaks,s.search.candidate_limit);
}
detail::NativePointLease::NativePointLease(detail::NativePointPool* pool,detail::NativePointOwner& owner,PatternReceiver& receiver)
    :NativePointLease(receiver.impl_->fallback?nullptr:pool,owner,receiver.impl_->points) {}
bool detail::NativePointLease::try_adopt(detail::NativePointPool& pool,detail::NativePointOwner& owner,PatternReceiver& receiver) {
    return !receiver.impl_->fallback && pool.try_adopt(owner,receiver.impl_->points);
}
std::size_t detail::NativePointLease::allocation_headroom(const detail::NativePointPool& pool,const detail::NativePointOwner& owner,const PatternReceiver& receiver) {
    return receiver.impl_->fallback?0:pool.loan_allocation_headroom(owner,receiver.impl_->points);
}
PatternReceiver::~PatternReceiver()=default;

std::optional<detail::NativeDiagnosticHistoryRequirements>
detail::NativeDiagnosticHistoryAccess::requirements(const PatternReceiver& receiver,std::size_t limit) {
    const auto& s=*receiver.impl_;
    if(s.fallback || s.diagnostic_tail || s.helper_overlap || !limit || limit>s.search.candidate_limit || s.finished ||
       s.sample || s.bins || s.partial || s.trials || s.history_insertions || !s.history.empty() ||
       !s.tracks.empty() || !s.bursts.empty() || !s.completed.empty() || s.early_eof_pending)return std::nullopt;
    const auto tail_bytes=limit*sizeof(PatternEvidence);
    const auto control_bytes=sizeof(NativeDiagnosticHistoryTail<PatternEvidence>);
    const auto working=s.working_bytes();
    const auto growth=control_bytes+tail_bytes;
    const auto bound=working-s.history.capacity()*sizeof(PatternEvidence)+growth;
    const auto reserved=s.fixed_reservation-s.search.candidate_limit*sizeof(PatternEvidence)+growth+tail_bytes;
    // Binding's new array coexists with the constructor's old array until swap.
    // Do not turn storage compaction into additional arithmetic/cache budget.
    // The original C-entry reservation already covers H+J+control for Live.
    return NativeDiagnosticHistoryRequirements{growth,working+growth,bound,tail_bytes,
        std::max(bound,s.retained_bit_reservation(std::max(s.fixed_reservation,reserved)))};
}
bool detail::NativeDiagnosticHistoryAccess::bind(PatternReceiver& receiver,std::size_t limit,std::size_t available_growth_bytes) {
    const auto required=requirements(receiver,limit);
    if(!required || required->binding_growth_bytes>available_growth_bytes)return false;
    auto& s=*receiver.impl_;
    if(required->binding_peak_bytes>s.budget || required->bound_reserved_bytes>s.budget)return false;
    auto tail=std::make_unique<NativeDiagnosticHistoryTail<PatternEvidence>>(s.search.candidate_limit,limit);
    std::vector<PatternEvidence> history;history.reserve(limit);
    // A library may reserve more than requested. Reject before committing if its
    // actual overlap or checkpoint reservation exceeds the promised headroom.
    const auto growth=sizeof(*tail)+history.capacity()*sizeof(PatternEvidence);
    if(growth>available_growth_bytes || growth>s.budget-s.working_bytes())return false;
    const auto reserved=std::max(s.fixed_reservation,
        s.fixed_reservation-s.search.candidate_limit*sizeof(PatternEvidence)+growth+
        required->checkpoint_growth_bytes);
    if(s.retained_bit_reservation(reserved)>s.budget)return false;
    s.history.swap(history);s.diagnostic_tail=std::move(tail);s.fixed_reservation=reserved;
    // Bit capacity and every detector/search choice remain exactly as constructed.
    return true;
}
detail::NativeDiagnosticHistoryAccounting
detail::NativeDiagnosticHistoryAccess::accounting(const PatternReceiver& receiver)noexcept {
    const auto& s=*receiver.impl_;
    NativeDiagnosticHistoryAccounting result;
    result.history_capacity_bytes=s.history.capacity()*sizeof(PatternEvidence);
    if(s.diagnostic_tail) {
        result.bound=true;result.logical_size=s.diagnostic_tail->logical_size();result.limit=s.diagnostic_tail->limit();
        result.checkpoint_bytes=s.diagnostic_tail->checkpoint_bytes();result.control_bytes=sizeof(*s.diagnostic_tail);
    }
    return result;
}

std::size_t PatternReceiver::symbol_burst_control_bytes(){return sizeof(SymbolBurstPlan)+sizeof(SymbolBurstCursor)+sizeof(Impl::BurstPolicy);}
PatternReceiver::PatternReceiver(PatternReceiver&&) noexcept=default;
PatternReceiver& PatternReceiver::operator=(PatternReceiver&&) noexcept=default;
void PatternReceiver::push(std::span<const float> input,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)throw Error("pattern capture already finished");
    if(s.fallback){s.fallback->push(input,stop);return;}
    if(!s.defer_acquisition && s.prior_pending_count)resolve_acquisition(false,stop);
    if(!s.defer_acquisition && !s.acquisition_mask.empty())s.clear_prior();
    if(!s.defer_acquisition && s.deferred_acquisition_frontier!=UINT64_MAX) {
        // Mixing the split seam with ordinary push must first finish the
        // original ready batch at its saved input fence.
        s.process(stop);s.deferred_acquisition_frontier=UINT64_MAX;
    }
    if(s.sample<s.unobserved_until) {
        const auto count=std::min<std::uint64_t>(input.size(),s.unobserved_until-s.sample);
        for(const auto value:input.first(count))if(!std::isfinite(value))throw Error("pattern input contains a nonfinite sample");
        s.sample+=count;input=input.subspan(count);
        if(input.empty())return;
    }
    Impl::ScoringScope scoring{s};
    if(!s.oscillator_valid) {
        s.oscillator=std::polar(1.,static_cast<double>(std::remainder(-static_cast<long double>(tau)*s.config.carrier_hz*s.sample/s.config.sample_rate,
                                                                   static_cast<long double>(tau))));
        s.oscillator_valid=true;
    }
    for(auto x:input) {
        if((s.sample&4095U)==0)cancelled(stop);
        if(!std::isfinite(x))throw Error("pattern input contains a nonfinite sample");
        s.sum+=static_cast<double>(x)*s.oscillator;s.oscillator*=s.rotation;++s.sample;
        if((s.sample&65535U)==0)s.oscillator/=std::abs(s.oscillator);
        if(++s.partial==s.bin_samples) {
            s.bin(s.sum/static_cast<double>(s.partial),stop);s.sum={};s.partial=0;
        }
    }
    if(!s.defer_acquisition || !acquisition_state().ready)s.continue_observed_tracks(stop);
}
PatternAcquisitionState PatternReceiver::acquisition_state()const noexcept {
    const auto& s=*impl_;PatternAcquisitionState result;
    result.observed_samples=s.sample;result.processing_frontier=s.sample;
    result.original_pairs=s.templates.size();result.bin_samples=s.bin_samples;
    result.admitted_clock_half_width_ppm=s.search.admitted_clock_half_width_ppm;
    result.admitted_clock_cell_half_width_ppm=s.search.admitted_clock_cell_half_width_ppm;
    result.frequency_origin_hz=s.acquisition_origin_hz;result.prior_active=!s.acquisition_mask.empty();
    result.prior_pending=s.prior_pending_count!=0;
    result.prior_accepted=result.prior_pending && s.link_matches(s.acquisition_origin_hz,s.acquisition_mask);
    result.selected_pairs=s.acquisition_mask.empty()?s.templates.size():
        static_cast<std::size_t>(std::count_if(s.acquisition_mask.begin(),s.acquisition_mask.end(),[](auto value){return value!=0;}));
    // Restricted/calendar scheduling has additional component/checkpoint
    // fences. Leave those original engines and their input path unchanged.
    if(s.fallback || s.finished || s.calendar_symbols() || s.acquisition_suspended ||
       !s.bin_samples || !s.length || !s.hop || s.ring.empty() || s.sample<s.unobserved_until ||
       s.component_hop || s.early_eof_pending ||
       (s.fft_workload->restricted_timing_active && s.search.early_qualified_fft))return result;
    const auto count=s.long_symbol && !s.next_start?
        std::min(s.hop,std::max<std::size_t>(1,s.config.sample_rate/s.bin_samples)):s.hop;
    if(s.next_start>UINT64_MAX-s.length || s.next_start+s.length>UINT64_MAX-(count-1))return result;
    const auto fence_bins=s.next_start+s.length+count-1;
    if(fence_bins>UINT64_MAX/s.bin_samples)return result;
    const auto fence_samples=fence_bins*s.bin_samples;
    result.supported=true;result.original_pairs=s.templates.size();result.ready=s.bins>=fence_bins;
    result.selected_pairs=s.acquisition_mask.empty()?s.templates.size():
        static_cast<std::size_t>(std::count_if(s.acquisition_mask.begin(),s.acquisition_mask.end(),[](auto value){return value!=0;}));
    result.prior_active=!s.acquisition_mask.empty();result.prior_pending=s.prior_pending_count!=0;
    result.prior_accepted=result.prior_pending && s.link_matches(s.acquisition_origin_hz,s.acquisition_mask);
    result.bin_samples=s.bin_samples;result.frequency_origin_hz=s.acquisition_origin_hz;
    result.admitted_clock_half_width_ppm=s.search.admitted_clock_half_width_ppm;
    result.admitted_clock_cell_half_width_ppm=s.search.admitted_clock_cell_half_width_ppm;
    result.audit_start_sample=s.next_start*s.bin_samples;
    if(s.deferred_acquisition_frontier!=UINT64_MAX)result.processing_frontier=s.deferred_acquisition_frontier;
    if(result.ready || fence_samples<=s.sample)return result;
    const auto room=fence_samples-s.sample;
    result.input_room_samples=static_cast<std::size_t>(std::min<std::uint64_t>(room,SIZE_MAX));
    return result;
}
std::span<const PatternFrequencyRateHypothesis> PatternReceiver::acquisition_hypotheses()const noexcept {
    return impl_->fallback?std::span<const PatternFrequencyRateHypothesis>{}:std::span<const PatternFrequencyRateHypothesis>(impl_->search.hypotheses);
}
bool PatternReceiver::set_acquisition_phase_view(const PatternPhaseCurve* curve)noexcept {
    auto& s=*impl_;
    if(s.prior_pending_count || s.prior_dispatch)return false;
    if(curve==s.phase_curve)return true;
    if(std::any_of(s.tracks.begin(),s.tracks.end(),[](const auto& track){return track.phase_view!=nullptr;}))return false;
    if(curve && (!s.phase_geometry_supported() || !curve->view_id() || curve->sample_rate()!=s.config.sample_rate ||
                 s.bin_samples>curve->maximum_bin_samples()))return false;
    s.phase_curve=curve;s.invalidate_phase_cache();
    if(curve)curve->mark_observed(s.sample<=UINT64_MAX-s.search.input_sample_origin?
        s.search.input_sample_origin+s.sample:UINT64_MAX);
    return true;
}
bool PatternReceiver::acquisition_phase_view_active()const noexcept {return impl_->fresh_phase_view()!=nullptr;}
bool PatternReceiver::set_acquisition_prior(double origin,std::span<const std::uint8_t> mask) {
    auto& s=*impl_;
    if(!std::isfinite(origin) || s.fallback || s.finished || s.calendar_symbols() || s.acquisition_suspended ||
       s.component_hop || s.early_eof_pending || s.prior_pending_count ||
       s.search.hypotheses.size()!=s.templates.size() || mask.size()!=s.templates.size() || mask.empty())return false;
    std::size_t selected=0;double maximum=0;
    for(std::size_t f=0;f<mask.size();++f) {
        if(mask[f]>1)return false;
        selected+=mask[f]!=0;maximum=std::max(maximum,std::abs(s.search.frequency_offsets_hz[f]+origin));
        // The original explicit pairs remain authoritative for LF timing.
        if(!mask[f] && std::none_of(s.search.hypotheses.begin(),s.search.hypotheses.end(),[&](const auto& h) {
            const auto i=static_cast<std::size_t>(&h-s.search.hypotheses.data());
            return mask[i] && h.clock_error_ppm==s.search.hypotheses[f].clock_error_ppm;
        }))return false;
    }
    // A complete catalogue can still need a measured RF translation. Keeping
    // its original LF pairs is alignment, with no acquisition-work saving.
    // A full unshifted mask adds no frequency assistance and stays declined.
    if(!selected || (selected==mask.size() && origin==0) || maximum>pattern_frequency_offset_limit(s.config) ||
       detail::pattern_observation_bin_samples(s.config,maximum)<s.bin_samples)return false;
    // This optional scalar mask is paid from the original receiver grant.
    // It cannot replace a mandatory payload/bit reservation or an older mask.
    // A joined caller can refresh an identical prior twice at one ready
    // fence. Retain its exact template/cache basis only when the normal
    // refresh would leave all acquisition and consume-once link state alone.
    // Changed origins, masks, schedules or incompatible new evidence still
    // take the original invalidating path below.
    if(!s.prior_dispatch && !s.acquisition_prepaid && !s.execution_phase_view && s.scoring.empty() && s.helper_overlap &&
       s.prior_schedule_saved && !s.search.early_qualified_fft && !s.search.component_qualified_search &&
       s.frequency_origin_hz==origin && s.acquisition_origin_hz==origin &&
       std::signbit(s.frequency_origin_hz)==std::signbit(origin) &&
       std::signbit(s.acquisition_origin_hz)==std::signbit(origin) &&
       s.acquisition_mask.size()==mask.size() && std::equal(mask.begin(),mask.end(),s.acquisition_mask.begin()) &&
       s.cache_fits(s.budget,0) && (s.prior_link_floor==s.accepted_serial || s.link_matches(origin,mask)))return true;
    if(!s.cache_fits(s.budget,mask.size()))return false;
    std::vector<std::uint8_t> copy;
    try {copy.assign(mask.begin(),mask.end());}catch(const std::bad_alloc&){return false;}
    if(!s.cache_fits(s.budget,copy.capacity()))return false;
    if(!s.ensure_helper_overlap())return false;
    const bool preserve_link=!s.acquisition_mask.empty() && s.link_matches(origin,mask);
    s.drop_scoring();s.drop_template_cache();s.templates_valid=s.tracking_reference_valid=false;
    s.acquisition_mask=std::move(copy);s.frequency_origin_hz=s.acquisition_origin_hz=origin;
    if(!preserve_link)s.prior_link_floor=s.accepted_serial;
    // Full original batches avoid the independently checkpointed timing
    // components. Their immutable start-window selection remains unchanged.
    if(!s.prior_schedule_saved) {
        s.prior_schedule_saved=true;s.prior_early_qualified=s.search.early_qualified_fft;
        s.prior_component_qualified=s.search.component_qualified_search;
    }
    s.search.early_qualified_fft=false;s.search.component_qualified_search=false;
    return true;
}
bool PatternReceiver::clear_acquisition_prior() {
    auto& s=*impl_;if(s.fallback || s.prior_pending_count)return false;
    s.clear_prior();return true;
}
std::size_t PatternReceiver::observe_acquisition(std::span<const float> input,std::stop_token stop) {
    cancelled(stop);auto& s=*impl_;const auto state=acquisition_state();
    if(!state.supported || state.ready)return 0;
    const auto count=std::min(input.size(),state.input_room_samples);
    const auto before=s.sample;
    if(count && s.deferred_acquisition_frontier==UINT64_MAX)s.deferred_acquisition_frontier=before;
    struct DeferredScope {
        bool& flag;bool previous;
        explicit DeferredScope(bool& value):flag(value),previous(value){flag=true;}
        ~DeferredScope(){flag=previous;}
    } deferred(s.defer_acquisition);
    push(input.first(count),stop);
    return count;
}
bool PatternReceiver::process_prior_acquisition(std::stop_token stop) {
    cancelled(stop);auto& s=*impl_;const auto state=acquisition_state();
    if(state.prior_pending)return state.prior_accepted;
    if(!state.supported || !state.ready || !state.prior_active)return false;
    s.prior_track_floor=s.track_serial;s.prior_acceptance_floor=s.accepted_serial;
    s.prior_candidate_before=s.candidate_limit_reached;
    const auto jobs=s.fft_workload->template_jobs;
    const auto* view=s.fresh_phase_view();
    struct CountScope {Impl& state;std::uint64_t before;bool phase;~CountScope(){
        const auto count=state.fft_workload->template_jobs-before;state.fft_workload->conditional_template_jobs+=count;
        if(phase)state.fft_workload->phase_view_template_jobs+=count;}} count_scope{s,jobs,view!=nullptr};
    struct ExecutionOriginScope {Impl& state;const PatternPhaseCurve* previous;
        ~ExecutionOriginScope(){state.frequency_origin_hz=state.acquisition_origin_hz;state.execution_phase_view=previous;}} origin_scope{s,s.execution_phase_view};
    s.execution_phase_view=view;
    struct PriorScope {bool& flag;explicit PriorScope(bool& value):flag(value){flag=true;}~PriorScope(){flag=false;}} prior(s.prior_dispatch);
    Impl::ScoringScope scoring{s};s.process(stop);
    return s.link_matches(s.acquisition_origin_hz,s.acquisition_mask);
}
void PatternReceiver::resolve_acquisition(bool conditional_link,std::stop_token stop) {
    cancelled(stop);auto& s=*impl_;
    if(!s.prior_pending_count) {
        if(!conditional_link)process_acquisition(stop);return;
    }
    const auto count=s.prior_pending_count;s.prior_pending_count=0;
    s.prior_link_floor=s.accepted_serial;
    Impl::ScoringScope scoring{s};
    if(conditional_link) {
        s.acquisition_prepaid=false;s.advance_acquisition(count,stop);
    } else {
        // Existing accepted and provisional tracks keep their own immutable
        // origins. Only new unaccepted candidates from this conditional pass
        // can be withdrawn before the exact original-window audit.
        std::erase_if(s.tracks,[&](const auto& track){return track.serial>s.prior_track_floor && !track.established;});
        if(s.accepted_serial==s.prior_acceptance_floor)s.candidate_limit_reached=s.prior_candidate_before;
        s.clear_prior(false);const auto jobs=s.fft_workload->template_jobs;
        struct RawScope {Impl& state;const PatternPhaseCurve* previous;~RawScope(){state.execution_phase_view=previous;}} raw_scope{s,s.execution_phase_view};
        s.execution_phase_view=nullptr;
        struct AuditCountScope {Impl& state;std::uint64_t before;~AuditCountScope(){state.fft_workload->original_audit_template_jobs+=state.fft_workload->template_jobs-before;}} audit_scope{s,jobs};
        s.process(stop);s.acquisition_prepaid=false;
        s.restore_prior_schedule();
    }
    s.deferred_acquisition_frontier=UINT64_MAX;s.continue_observed_tracks(stop);
}
void PatternReceiver::process_acquisition(std::stop_token stop) {
    cancelled(stop);auto& s=*impl_;
    if(s.prior_pending_count){resolve_acquisition(false,stop);return;}
    const bool conditional_prepay=s.prior_schedule_saved;
    if(!s.acquisition_mask.empty())s.clear_prior(false);
    if(s.fallback || s.finished || s.calendar_symbols() || !s.bin_samples)
        throw Error("split acquisition requires an ordinary native input geometry");
    const auto jobs=s.fft_workload->template_jobs;Impl::ScoringScope scoring{s};
    struct RawScope {Impl& state;const PatternPhaseCurve* previous;~RawScope(){state.execution_phase_view=previous;}} raw_scope{s,s.execution_phase_view};
    s.execution_phase_view=nullptr;
    struct AuditCountScope {Impl& state;std::uint64_t before;~AuditCountScope(){state.fft_workload->original_audit_template_jobs+=state.fft_workload->template_jobs-before;}} audit_scope{s,jobs};
    s.process(stop);s.deferred_acquisition_frontier=UINT64_MAX;
    if(conditional_prepay)s.acquisition_prepaid=false;
    s.restore_prior_schedule();
    s.continue_observed_tracks(stop);
}
bool PatternReceiver::set_helper_gap_protection(bool qualified)noexcept {
    auto& s=*impl_;
    if(s.fallback)return false;
    s.helper_gap_protection=qualified;
    if(qualified)for(auto& track:s.tracks)track.helper_gap_protected=true;
    return true;
}
bool PatternReceiver::phase_assistance_supported()const noexcept {
    const auto& s=*impl_;
    return !s.fallback && !s.sample_fit && !s.differential_window && !s.real_rank &&
        !(s.search.outer_presence_guard && s.config.dsss_factor>1 && s.drift_sections==1);
}
std::size_t PatternReceiver::phase_assistance_bin_samples()const noexcept {
    return phase_assistance_supported()?impl_->bin_samples:0;
}
bool PatternReceiver::suspend_helper_acquisition() {
    auto& s=*impl_;
    if(s.fallback)return s.fallback->suspend_helper_acquisition();
    if(s.calendar_symbols() || s.finished || !s.next_start)return false;
    if(!s.acquisition_suspended) {
        s.acquisition_last_start=s.next_start?s.next_start-1:0;
        s.acquisition_suspended=true;
    }
    return true;
}
bool PatternReceiver::push_phase_assisted(std::span<const float> input,
        std::span<const PatternPhaseKnot> knots,std::uint64_t maximum_gap_samples,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);if(s.finished)throw Error("pattern capture already finished");
    if(!phase_assistance_supported() || s.phase_curve || !maximum_gap_samples || knots.size()<2 ||
       s.deferred_acquisition_frontier!=UINT64_MAX || !s.acquisition_mask.empty() ||
       s.sample<s.unobserved_until || input.size()>std::numeric_limits<std::uint64_t>::max()-s.sample)return false;
    for(std::size_t i=0;i<knots.size();++i) {
        if(!std::isfinite(knots[i].radians) || std::abs(knots[i].radians)>1e12 ||
           (i && (knots[i].sample<=knots[i-1].sample ||
           knots[i].sample-knots[i-1].sample>maximum_gap_samples)))return false;
    }
    const auto complete=(input.size()+s.partial)/s.bin_samples;
    const auto first_center=static_cast<long double>(s.sample-s.partial)+(s.bin_samples-1)/2.L;
    const auto last_center=first_center+(complete?complete-1:0)*static_cast<long double>(s.bin_samples);
    if(complete && (first_center<knots.front().sample || last_center>knots.back().sample))return false;
    // All model checks precede mutation. A rejected optional model cannot
    // consume PCM or poison the ordinary raw-input fallback.
    Impl::ScoringScope scoring{s};
    if(!s.oscillator_valid) {
        s.oscillator=std::polar(1.,static_cast<double>(std::remainder(-static_cast<long double>(tau)*s.config.carrier_hz*s.sample/s.config.sample_rate,
                                                                   static_cast<long double>(tau))));
        s.oscillator_valid=true;
    }
    std::size_t segment=0;
    for(auto x:input) {
        if((s.sample&4095U)==0)cancelled(stop);
        if(!std::isfinite(x))throw Error("pattern input contains a nonfinite sample");
        s.sum+=static_cast<double>(x)*s.oscillator;s.oscillator*=s.rotation;++s.sample;
        if((s.sample&65535U)==0)s.oscillator/=std::abs(s.oscillator);
        if(++s.partial==s.bin_samples) {
            const auto center=static_cast<long double>(s.sample)-(s.bin_samples+1)/2.L;
            while(segment+2<knots.size() && center>knots[segment+1].sample)++segment;
            const auto& a=knots[segment];const auto& b=knots[segment+1];
            const auto fraction=(center-a.sample)/static_cast<long double>(b.sample-a.sample);
            const auto phase=static_cast<double>(a.radians+fraction*(static_cast<long double>(b.radians)-a.radians));
            auto value=s.sum/static_cast<double>(s.partial);
            if(phase!=0)value*=std::polar(1.,-std::remainder(phase,tau));
            s.bin(value,stop);s.sum={};s.partial=0;
        }
    }
    s.continue_observed_tracks(stop);return true;
}
void PatternReceiver::push(std::span<const float> input,std::span<const std::complex<double>> projected,std::stop_token stop) {
    push(input,projected,stop,nullptr);
}
void PatternReceiver::push(std::span<const float> input,std::span<const std::complex<double>> projected,
                           std::stop_token stop,detail::CorrelationProjectionCache* cache) {
    auto& s=*impl_;cancelled(stop);if(s.finished)throw Error("pattern capture already finished");
    if(input.size()!=projected.size())throw Error("shared pattern projection length mismatch");
    if(s.fallback){s.fallback->push(input,stop,cache);return;}
    if(s.prior_pending_count)resolve_acquisition(false,stop);
    if(!s.acquisition_mask.empty())s.clear_prior();
    if(s.deferred_acquisition_frontier!=UINT64_MAX) {
        s.process(stop);s.deferred_acquisition_frontier=UINT64_MAX;
    }
    if(s.sample_fit || s.differential_window || s.fine_private_chip_guard() ||
       (s.search.outer_presence_guard && s.config.dsss_factor>1 && s.drift_sections==1)) {
        // Exact Gram whitening needs a known carrier phase convention. Shared
        // projections may have an arbitrary fixed rotation, so reconstruct
        // the observations from their raw samples.
        for(const auto value:projected)
            if(!std::isfinite(value.real())||!std::isfinite(value.imag()))
                throw Error("pattern input contains a nonfinite sample");
        push(input,stop);return;
    }
    if(s.sample<s.unobserved_until) {
        const auto count=std::min<std::uint64_t>(input.size(),s.unobserved_until-s.sample);
        for(std::size_t i=0;i<count;++i)if(!std::isfinite(input[i]) || !std::isfinite(projected[i].real()) || !std::isfinite(projected[i].imag()))
            throw Error("pattern input contains a nonfinite sample");
        s.sample+=count;input=input.subspan(count);projected=projected.subspan(count);
        if(input.empty())return;
    }
    s.oscillator_valid=false;
    Impl::ScoringScope scoring{s};
    for(std::size_t i=0;i<input.size();++i) {
        if((s.sample&4095U)==0)cancelled(stop);
        if(!std::isfinite(input[i])||!std::isfinite(projected[i].real())||!std::isfinite(projected[i].imag()))
            throw Error("pattern input contains a nonfinite sample");
        s.sum+=projected[i];++s.sample;
        if(++s.partial==s.bin_samples){s.bin(s.sum/static_cast<double>(s.partial),stop);s.sum={};s.partial=0;}
    }
    s.continue_observed_tracks(stop);
}
bool PatternReceiver::try_skip_unobserved(std::uint64_t count,std::stop_token stop) {
    cancelled(stop);
    if(impl_->finished)throw Error("pattern capture already finished");
    auto& s=*impl_;
    if(s.fallback)return s.fallback->try_skip_unobserved(count,stop);
    if(!s.burst_plan || count>std::numeric_limits<std::uint64_t>::max()-s.sample)return false;
    const auto end=s.sample+count;
    const auto limit=s.burst_unobserved_end(s.sample);
    if(!limit || end>*limit)return false;
    // The proof excludes all symbol windows and guards. Discard a projection
    // bin crossing the discontinuity; its samples are never a scored zero.
    const auto remainder=end%s.bin_samples;
    const auto tail=remainder?s.bin_samples-remainder:0;
    if(tail>std::numeric_limits<std::uint64_t>::max()-end)return false;
    s.sample=end;s.unobserved_until=end+tail;s.bins=s.unobserved_until/s.bin_samples;
    s.sum={};s.partial=0;s.oscillator_valid=false;
    s.next_start=std::max(s.next_start,s.bins);
    s.component_hop=s.acquisition_prepaid=s.early_eof_pending=false;
    if(s.diagnostic_tail)s.diagnostic_tail->release_checkpoint();
    return true;
}
std::optional<std::uint64_t> PatternReceiver::unobserved_end_from(std::uint64_t sample) const {
    return impl_->fallback?impl_->fallback->unobserved_end_from(sample):impl_->burst_unobserved_end(sample);
}
void PatternReceiver::suspend_fresh_acquisition() {
    if(impl_->fallback)impl_->fallback->suspend_fresh_acquisition();else impl_->suspend_acquisition();
}
bool PatternReceiver::symbol_burst_tracking_active()const {
    return impl_->fallback?impl_->fallback->symbol_burst_tracking_active():
        impl_->burst_plan && (std::any_of(impl_->tracks.begin(),impl_->tracks.end(),[&](const auto& track){
            return track.established || track.admitted || (impl_->acquisition_suspended && !track.burst.bits.empty());}) ||
            (impl_->acquisition_suspended && impl_->next_start<=impl_->acquisition_last_start));
}
std::optional<ScheduledCaptureEnvelope> PatternReceiver::scheduled_capture_envelope(std::uint64_t sample) const {
    return impl_->fallback?impl_->fallback->scheduled_capture_envelope(sample):std::nullopt;
}
void PatternReceiver::finish(std::stop_token stop) {
    auto& s=*impl_;if(s.finished)return;cancelled(stop);
    if(s.prior_pending_count)resolve_acquisition(false,stop);
    if(!s.acquisition_mask.empty())s.clear_prior();
    Impl::ScoringScope scoring{s};
    if(s.fallback)s.fallback->finish(stop);else s.finish_observed(stop);
    s.finished=true;
}
std::vector<PatternBurst> PatternReceiver::take_bursts(){
    auto& s=*impl_;if(s.fallback)return s.fallback->take_bursts();
    // The chunk limit bounds storage, not when an accepted symbol becomes
    // visible. Flush at consumer drains without claiming the stream ended.
    for(auto& track:s.tracks)s.publish(track,false,true,true);
    std::vector<PatternBurst> result;result.reserve(s.bursts.size());
    for(auto& burst:s.bursts)result.push_back(std::move(burst));
    s.bursts.clear();return result;
}
PatternBurst PatternReceiver::provisional()const{return impl_->fallback?impl_->fallback->provisional():impl_->provisional();}
std::optional<PatternSourceIdentity> PatternReceiver::source_identity(const PatternBurst& burst)const {
    return impl_->source_identity(burst.stream_first_symbol,burst.stream_phase_samples);
}
bool PatternReceiver::retains_stream(std::uint64_t first_sample,std::uint64_t first_symbol)const {
    const auto& s=*impl_;
    if(s.fallback)return s.fallback->retains_stream(first_sample,first_symbol);
    const auto same=[&](const auto& burst) {
        return burst.stream_first_sample==first_sample && burst.stream_first_symbol==first_symbol;
    };
    // Acquisition peaks have all been consumed at this boundary; future batches
    // advance past their starts. Diagnostic history/latest are not producers.
    return std::any_of(s.tracks.begin(),s.tracks.end(),[&](const auto& track){return same(track.burst);}) ||
        std::any_of(s.bursts.begin(),s.bursts.end(),same);
}
void PatternReceiver::process_pending(std::stop_token stop){if(impl_->fallback)impl_->fallback->process_pending(stop);}
std::uint64_t PatternReceiver::diagnostic_history_generation()const noexcept{return impl_->fallback?impl_->fallback->diagnostic_history_generation():0;}
std::uint64_t PatternReceiver::diagnostic_history_resolved_through()const noexcept{return impl_->fallback?impl_->fallback->diagnostic_history_resolved_through():0;}
std::uint64_t PatternReceiver::oldest_unresolved_diagnostic_event()const noexcept{return impl_->fallback?impl_->fallback->oldest_unresolved_diagnostic_event():0;}
bool PatternReceiver::deferred_pattern_history()const noexcept{return impl_->fallback && impl_->fallback->deferred_pattern_history();}
std::optional<std::uint64_t> PatternReceiver::delayed_processing_frontier()const noexcept {
    if(impl_->fallback)return impl_->fallback->delayed_processing_frontier();
    return impl_->deferred_acquisition_frontier==UINT64_MAX?std::nullopt:
        std::optional<std::uint64_t>(impl_->deferred_acquisition_frontier);
}
std::vector<PatternEvidence> PatternReceiver::candidates()const{return impl_->fallback?impl_->fallback->candidates():impl_->history;}
std::vector<PatternEvidence> PatternReceiver::candidates(std::size_t limit,std::stop_token stop)const {
    if(stop.stop_requested())throw Error("pattern diagnostic query cancelled");
    return impl_->fallback?impl_->fallback->candidates(limit,stop):candidates(limit);
}
std::vector<PatternEvidence> PatternReceiver::candidates(std::size_t limit)const{
    if(impl_->fallback)return impl_->fallback->candidates(limit);
    const auto& history=impl_->history;
    return {history.end()-static_cast<std::ptrdiff_t>(std::min(limit,history.size())),history.end()};
}
std::vector<Complex> PatternReceiver::take_chip_constellation(){if(impl_->fallback)return impl_->fallback->take_chip_constellation();auto result=impl_->chip_points();impl_->points.clear();impl_->point_cursor=0;return result;}
bool PatternReceiver::acquiring()const{return impl_->fallback?impl_->fallback->acquiring():!impl_->tracks.empty();}
bool PatternReceiver::synchronized()const{return impl_->fallback?impl_->fallback->synchronized():std::any_of(impl_->tracks.begin(),impl_->tracks.end(),[](const auto& track){return track.admitted;});}
std::uint64_t PatternReceiver::initial_fft_input_samples()const noexcept {
    const auto& s=*impl_;
    if(s.fallback || !s.bin_samples)return 0;
    // A full hop also bounds qualified/component scheduling, including its
    // four-bin refinement cover. Do not assume the long-symbol one-second
    // shortcut when a qualified arrival component can be wider.
    if(s.hop>UINT64_MAX-s.length || s.length+s.hop>UINT64_MAX-4)return UINT64_MAX;
    const auto bins=static_cast<std::uint64_t>(s.length)+s.hop+4;
    return bins>UINT64_MAX/s.bin_samples?UINT64_MAX:bins*s.bin_samples;
}
bool PatternReceiver::initial_search_complete()const {
    const auto& s=*impl_;
    if(s.fallback)return s.fallback->initial_search_complete();
    if(s.burst_plan && s.acquisition_suspended)return s.next_start>s.acquisition_last_start;
    if(!s.search.start_offset_seconds)return false;
    auto upper=(static_cast<long double>(*s.search.start_offset_seconds)+
        s.search.start_uncertainty_seconds)*s.config.sample_rate;
    const auto phase=s.phase_upper;
    const auto physical_phase=s.search.search_stream_phases?
        canonical_phase_cell_upper(s.config,phase):phase;
    const auto offset=static_cast<long double>(s.source_start(s.search.initial_stream_symbols-1,phase))/s.timing_minimum_rate;
    upper+=physical_phase+offset;
    if(s.acquisition_window) {
        const auto& window=*s.acquisition_window;
        upper=std::max(upper,window.epoch_origin_samples+window.phase_scale*physical_phase+
            window.half_width_samples+offset);
    }
    if(s.packed_calendar || s.burst_plan)upper=s.packed_latest_start_samples;
    // Cover the selected starts' two-bin refinements and precision margin
    // before retiring their acquisition context.
    auto magnitude=std::max({1.L,std::abs(upper),std::abs(offset),
        static_cast<long double>(s.next_start)*s.bin_samples});
    if(s.acquisition_window) {
        const auto& window=*s.acquisition_window;
        magnitude=std::max({magnitude,std::abs(window.epoch_origin_samples),
            std::abs(window.phase_scale*physical_phase),window.half_width_samples});
    }
    upper+=4.L*s.bin_samples+128*std::numeric_limits<double>::epsilon()*magnitude;
    // next_start advances only after a complete FFT acquisition batch. It
    // includes the actual stretched integration and batching lookahead;
    // merely consuming a nominal symbol's PCM is not sufficient.
    // Require at least the first scored start for an entirely past window.
    // Compare floating sample coordinates without integer products/casts.
    return std::isfinite(upper) && static_cast<long double>(s.next_start)*s.bin_samples>
        std::max(0.L,upper);
}
bool PatternReceiver::clock_windowed()const{return static_cast<bool>(impl_->fallback);}
bool PatternReceiver::local_clock_fallback()const{return impl_->local_search_fallback;}
bool PatternReceiver::candidate_limited()const{return impl_->candidate_limit_reached;}
bool PatternReceiver::drift_tolerant()const{return impl_->fallback?impl_->fallback->drift_tolerant():impl_->drift_sections>1;}
PatternFftWork PatternReceiver::fft_work()const {
    auto result=impl_->fft_workload?*impl_->fft_workload:PatternFftWork{};
    result.threshold_trials=impl_->trials;result.shifted_reference_trials=impl_->shifted_trials;
    if(search_arithmetic_is_int8(impl_->config.search_arithmetic))
        result.arithmetic_fallback_jobs=result.template_jobs-result.int8_jobs;
    return result;
}
std::size_t PatternReceiver::working_bytes()const {
    return impl_->working_bytes();
}
std::size_t PatternReceiver::reserved_workspace_bytes()const {
    if(impl_->fallback)return impl_->wrapper_bytes()+impl_->fallback->reserved_workspace_bytes();
    return std::max(impl_->working_bytes(),impl_->retained_bit_reservation(impl_->fixed_reservation));
}
std::optional<HelperReplayBudget> PatternReceiver::helper_replay_budget(std::size_t future_samples,
        const PatternSearch* reset_search)const {
    const auto& s=*impl_;
    if(s.fallback) {
        if(reset_search)return std::nullopt;
        auto result=s.fallback->helper_replay_budget(future_samples);
        if(result) {
            const auto wrapper=s.wrapper_bytes();
            if(result->workspace_bytes>SIZE_MAX-wrapper)return std::nullopt;
            result->workspace_bytes+=wrapper;
        }
        return result;
    }
    if(s.calendar_symbols() || !s.bin_samples || s.templates.empty())return std::nullopt;
    if(future_samples>UINT64_MAX-s.sample)return std::nullopt;
    auto minimum=UINT64_MAX;
    for(std::size_t f=0;f<s.templates.size();++f)minimum=std::min(minimum,s.symbol_bins(f));
    // Continuation may correct the preceding start by two bins at EACH bit.
    // Count the entire observed aperture, including unprocessed initial PCM.
    if(minimum<=2)return std::nullopt;
    const auto bins=(s.sample+future_samples)/s.bin_samples;
    const auto whole=bins/(minimum-2);
    if(whole>SIZE_MAX-2)return std::nullopt;
    const auto observed=static_cast<std::size_t>(whole)+2;
    auto capacity=std::size_t{1};
    // A later ordinary workspace loan may raise today's clipped bit limit.
    // Bound the physical aperture itself, independent of that optional loan.
    while(capacity<observed) {
        if(capacity>SIZE_MAX/2)return std::nullopt;
        capacity*=2;
    }
    capacity=std::max(capacity,s.latest.bits.capacity());
    for(const auto& track:s.tracks)capacity=std::max(capacity,track.burst.bits.capacity());
    for(const auto& burst:s.bursts)capacity=std::max(capacity,burst.bits.capacity());
    // Tracks, queued events, latest, realloc/copy and publish's three-event
    // temporary cover. Optional plans/caches retain their ordinary trim path.
    if(s.search.track_limit>(SIZE_MAX-6)/2)return std::nullopt;
    const auto slots=2*s.search.track_limit+6;
    const auto retained=std::max(s.working_bytes(),s.fixed_reservation);
    if(capacity>(SIZE_MAX-retained)/slots)return std::nullopt;
    HelperReplayBudget result{observed,retained+slots*capacity};
    result.steady_workspace_bytes=result.workspace_bytes;
    const auto default_reset_shape=[&](const PatternSearch& search) {
        return search.candidate_limit==2048 && search.track_limit==16 && search.initial_stream_symbols==4 &&
            search.chunk_bits==1024 && search.search_stream_phases && !search.compact_clock_search &&
            !search.couple_clock_to_carrier && !search.uncoupled_frequency_count &&
            search.clock_errors_ppm.size()==1 && search.clock_errors_ppm.front()==0 && !search.qualified_start_window &&
            !search.scheduled_capture && !search.hypotheses.empty();
    };
    const bool cold_native=s.config.receiver_strategy==ReceiverStrategy::complex_only &&
        s.config.oscillator_search && s.config.scramble && !s.config.dsss && s.config.dsss_factor==1 &&
        !s.config.fake_hopping && !s.config.scheduled_hopping && !s.config.symbol_burst.enabled() &&
        s.drift_sections==1 && !s.differential_window && !s.search.compact_clock_search &&
        s.code.symbol_samples()<60ULL*s.config.sample_rate && default_reset_shape(s.search);
    if(reset_search && (!cold_native || !default_reset_shape(*reset_search) ||
       reset_search->hypotheses!=s.search.hypotheses ||
       reset_search->hypotheses.capacity()>s.search.hypotheses.capacity() ||
       !reset_search->frequency_offsets_hz.empty() ||
       reset_search->prefer_streamed_templates!=s.search.prefer_streamed_templates ||
       reset_search->bit_limit!=s.configured_bit_limit))return std::nullopt;
    if(cold_native) {
        // For this SAME native geometry, F bounds the constructor's required
        // storage after removing optional per-frequency rows. With G=F+K,
        // full required+K either fits (so required<=F), or the constructor
        // streams those rows and without_rows<=F. It cannot reach the late
        // Correlator redirect. Do not replace G with the larger replay ceiling.
        const auto minimum_bits=2*s.search.track_limit+2;
        if(s.fixed_reservation>SIZE_MAX-minimum_bits)return std::nullopt;
        result.cold_constructor_bytes=s.fixed_reservation+minimum_bits;
        // Cover pristine C-entry history/new tail overlap and its one pending
        // checkpoint. Live may bind any diagnostic limit no greater than C.
        const auto history=s.search.candidate_limit*sizeof(PatternEvidence);
        const auto binding=sizeof(detail::NativeDiagnosticHistoryTail<PatternEvidence>)+history;
        auto reset=result.cold_constructor_bytes;
        const auto add=[&](std::size_t bytes){if(bytes>SIZE_MAX-reset)return false;reset+=bytes;return true;};
        if(!add(binding) || !add(history) || capacity>(SIZE_MAX-reset)/slots)return std::nullopt;
        reset+=slots*capacity;result.workspace_bytes=std::max(result.workspace_bytes,reset);
        // Ordinary reset temporarily holds proposed/declared searches and
        // constructor copies while the old row still exists. These vector
        // capacities also cover the Streaming wrapper's retained search copy.
        const auto vectors=s.search.frequency_offsets_hz.capacity()*sizeof(double)+
            s.search.clock_errors_ppm.capacity()*sizeof(double)+
            s.search.hypotheses.capacity()*sizeof(PatternFrequencyRateHypothesis);
        if(vectors>(SIZE_MAX-65536)/8)return std::nullopt;
        result.reset_overlap_bytes=65536+8*vectors;
    }
    return result;
}
std::size_t PatternReceiver::projection_cache_headroom(std::size_t input_samples)const {
    return impl_->fallback?impl_->fallback->projection_cache_headroom(input_samples):std::numeric_limits<std::size_t>::max();
}
std::optional<std::size_t> PatternReceiver::temporal_plan_workers()const {
    return impl_->fallback?impl_->fallback->temporal_plan_workers():std::nullopt;
}
void PatternReceiver::set_workspace_bytes(std::size_t bytes){impl_->set_workspace_bytes(bytes);}
void PatternReceiver::trim_optional_workspace(){
    if(impl_->fallback)impl_->fallback->trim_optional_workspace();
    else {
    if(!impl_->acquisition_mask.empty())impl_->clear_prior(!impl_->prior_pending_count && !impl_->prior_dispatch);
    if(impl_->exact_initial_rows || impl_->cached_templates.capacity() ||
            (!impl_->required_tracking_reference && impl_->tracking_reference.capacity()))
        impl_->drop_template_cache(); // Joined optional storage only; exact bounded recomputation remains.
    }
}
Diagnostics PatternReceiver::diagnostics()const {
    if(impl_->fallback)return impl_->fallback->diagnostics();
    const auto& s=*impl_;const auto& burst=s.current_burst();Diagnostics d;d.bit_rate=bit_rate(s.config);d.sample_offset=static_cast<std::size_t>(burst.first_sample);
    if(!burst.bits.empty())d.pattern_score=burst.score;
    d.constellation=s.chip_points();return d;
}
}

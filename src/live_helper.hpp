#pragma once
#include "datapump/live.hpp"
#include "datapump/channel.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/helper_reference_precision.hpp"
#include "datapump/helper_frontend.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/symbol_schedule.hpp"
#include "helper_link_state.hpp"
#include "helper_oscillator_model.hpp"
#include "receiver_oscillator_policy.hpp"
#include "helper_phase_chord.hpp"
#include "datapump/tuning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <numbers>

namespace datapump::live::detail {
// Joined current-owner pair totals, not independent per-column maxima or a
// claim that every owner uses one mask. A broad owner must not hide another
// owner's qualified narrowing. This call-scope fold owns no allocation.
struct RealtimeHelperWindowSummary {
    std::uint64_t original_pairs=0,selected_pairs=0;
    bool narrowed=false,clock_active=false,valid=true;
    void observe(const modem::PatternHelperFrequencyWindowState& window) noexcept {
        if(!valid)return;
        const auto original=static_cast<std::uint64_t>(window.original_pairs);
        const auto selected=static_cast<std::uint64_t>(window.selected_pairs);
        if(selected>original || original>UINT64_MAX-original_pairs || selected>UINT64_MAX-selected_pairs) {
            original_pairs=selected_pairs=0;narrowed=clock_active=valid=false;return;
        }
        original_pairs+=original;selected_pairs+=selected;
        const bool qualified=window.supported && window.active && selected && selected<original;
        narrowed|=qualified;clock_active|=qualified && window.clock_qualified;
    }
};
inline std::optional<modem::SampledSimulationChannel::PreconversionCapture> shared_helper_capture(
        const modem::Config& config,double expected) {
    if(!config.oscillator_search)return {};
    const auto& policy=*config.oscillator_search;
    if(policy.reference!=modem::OscillatorReference::shared_radio || policy.rf_shift_hz<=0)return {};
    const auto sign=policy.sideband==modem::OscillatorSideband::upper?1.L:-1.L;
    const auto epsilon=static_cast<long double>(policy.rf.accuracy_ppm)*policy.margin*1e-6L;
    const auto kd=sign*policy.rf_shift_hz+config.carrier_hz,kh=sign*policy.rf_shift_hz+expected;
    const auto half=helper::occupied_bandwidth(config)/2;
    const auto low=std::min(static_cast<long double>(config.carrier_hz)-std::abs(kd)*epsilon-half*(1+epsilon),
        static_cast<long double>(expected)-std::abs(kh)*epsilon-150*(1+epsilon));
    const auto high=std::max(static_cast<long double>(config.carrier_hz)+std::abs(kd)*epsilon+half*(1+epsilon),
        static_cast<long double>(expected)+std::abs(kh)*epsilon+150*(1+epsilon));
    const auto rate=std::ceil(std::max(std::abs(low),std::abs(high))/.42L)+64;
    if(!std::isfinite(low)||!std::isfinite(high)||!std::isfinite(rate)||rate>120000000)return {};
    return modem::SampledSimulationChannel::PreconversionCapture{static_cast<double>(low),
        static_cast<double>(high),static_cast<std::uint32_t>(std::max(64.L,rate))};
}


// Raw history deliberately retains measurements, not Metadata's string
// owner. Metadata already has its own reception publication owner. Keeping all
// four numeric/provenance records avoids borrowing extra payload workspace as
// additive receiver provenance grows.
struct HelperMeasurement {
    std::uint64_t sample=0,frequency_sample=0,phase_observation_sample=0,frequency_first_sample=0;
    double frequency_hz=0,phase_radians=0,frequency_uncertainty_hz=0,phase_uncertainty_radians=0;
    double dwell_start_seconds=0,dwell_uncertainty_seconds=0;
    double frequency_phase_diffusion_gain=0,frequency_process_uncertainty_hz=0;
    helper::PunctuationObservation punctuation;
    std::uint32_t sample_rate=0;
    helper::FrequencyFit frequency_fit=helper::FrequencyFit::unknown;
    bool frequency_valid=false,phase_valid=false,timing_valid=false,phase_observation_known=false;
    HelperMeasurement()=default;
    explicit HelperMeasurement(const helper::Reference& source) noexcept:
        sample(source.sample),frequency_sample(source.frequency_sample),
        phase_observation_sample(source.phase_observation_sample),frequency_first_sample(source.frequency_first_sample),
        frequency_hz(source.frequency_hz),phase_radians(source.phase_radians),
        frequency_uncertainty_hz(source.frequency_uncertainty_hz),phase_uncertainty_radians(source.phase_uncertainty_radians),
        dwell_start_seconds(source.dwell_start_seconds),dwell_uncertainty_seconds(source.dwell_uncertainty_seconds),
        frequency_phase_diffusion_gain(source.frequency_phase_diffusion_gain),
        frequency_process_uncertainty_hz(source.frequency_process_uncertainty_hz),punctuation(source.punctuation),
        sample_rate(source.sample_rate),frequency_fit(source.frequency_fit),frequency_valid(source.frequency_valid),
        phase_valid(source.phase_valid),timing_valid(source.timing_valid),phase_observation_known(source.phase_observation_known) {}
    helper::Reference reference()const noexcept {
        helper::Reference result;
        result.sample=sample;result.frequency_sample=frequency_sample;
        result.phase_observation_sample=phase_observation_sample;result.frequency_first_sample=frequency_first_sample;
        result.frequency_hz=frequency_hz;result.phase_radians=phase_radians;
        result.frequency_uncertainty_hz=frequency_uncertainty_hz;result.phase_uncertainty_radians=phase_uncertainty_radians;
        result.dwell_start_seconds=dwell_start_seconds;result.dwell_uncertainty_seconds=dwell_uncertainty_seconds;
        result.frequency_phase_diffusion_gain=frequency_phase_diffusion_gain;
        result.frequency_process_uncertainty_hz=frequency_process_uncertainty_hz;result.punctuation=punctuation;
        result.sample_rate=sample_rate;result.frequency_fit=frequency_fit;result.frequency_valid=frequency_valid;
        result.phase_valid=phase_valid;result.timing_valid=timing_valid;result.phase_observation_known=phase_observation_known;
        return result;
    }
};
static_assert(sizeof(HelperMeasurement)<sizeof(helper::Reference));

// A bounded, stack-only CURRENT measurement query. No predictive history or
// heap/control grant is added. The existing serialized receiver temporary
// allowance covers LinkState's explicit <=2KiB scratch. Persistent evolution
// or new frontend growth needs a separately funded owner before admission.
// This model is the PHYSICAL helper/audio clock. Keyed/calendar transmit-rate
// corrections can enlarge the ordinary DATA clock domain (B:600.09ppm versus
// physical audio300ppm). The returned data_residual_hz is diagnostic for an
// UNSTEERED physical data clock only; it MUST NOT replace that original data
// RF/LF domain or its separate correction union in acquisition masks.
inline helper::LinkResult helper_current_link_measurement(const modem::Config& config,double expected,
        const helper::Reference& reference,std::uint64_t captured_frontier,
        std::uint64_t capture_generation,int mono_image_sign=1) noexcept {
    helper::LinkResult declined;declined.reason=helper::LinkReason::invalid_model;
    if(!config.oscillator_search)return declined;
    const auto& policy=*config.oscillator_search;
    helper::LinkModel model;
    model.topology=policy.reference==modem::OscillatorReference::shared_radio?
        helper::LinkTopology::shared_radio:helper::LinkTopology::independent_audio;
    const auto accuracy=[&](double ppm) {
        if(ppm==0)return 0.;
        const auto infinity=std::numeric_limits<double>::infinity();
        const auto scaled=std::nextafter(ppm*policy.margin,infinity);
        return std::nextafter(scaled*std::nextafter(1e-6,infinity),infinity);
    };
    model.rf_accuracy_fraction=accuracy(policy.rf.accuracy_ppm);
    model.audio_accuracy_fraction=accuracy(policy.lf.accuracy_ppm);
    // The supplied presets declare no mean-frequency slew. Keep UNKNOWN:
    // fresh measurements may move; cross-time precision/prediction is absent.
    model.maximum_current_measurement_age_seconds=1;
    helper::LinkState current;if(!current.reset(model))return declined;
    helper::LinkMapping mapping;
    mapping.capture_generation=capture_generation;mapping.timebase_identity=capture_generation;
    mapping.reference_identity_known=true;mapping.reference_identity=0;
    mapping.sample_rate=reference.sample_rate;mapping.settled_first_sample=0;
    mapping.settled_end_sample=std::numeric_limits<std::uint64_t>::max();
    mapping.actual_rf_shift_hz=policy.rf_shift_hz;mapping.helper_audio_hz=expected;
    mapping.data_audio_hz=config.carrier_hz;
    mapping.sideband_sign=policy.sideband==modem::OscillatorSideband::upper?1:-1;
    mapping.mono_image_sign=mono_image_sign;
    // This app currently has a fixed physical mapping. Its fake-FHSS plan is
    // only a calendar/power/preview and is never used as actual LO provenance.
    mapping.provenance=helper::LinkMappingProvenance::fixed_configuration;
    mapping.phase_continuity_known=false;
    return current.observe({reference,mapping,{}},captured_frontier);
}
// This choice never changes provider provenance or makes phase a prerequisite
// for direct frequency assistance. The optional chord interval is conditional
// on a constant mean over its endpoint aperture AND the ordinary scored
// waveform plus the configured Wiener phase model; it grants no thermal-slew prediction or future precision lease.
struct HelperFrequencyInterval {
    double frequency_hz=0,uncertainty_hz=0;
    std::uint64_t first_sample=0,last_sample=0;
    bool phase_mean=false;
    const char* reason="fresh direct tone frequency; optional phase mean unavailable";
};
inline HelperFrequencyInterval helper_frequency_interval(const helper::Reference& reference,
        const helper::PhaseChord::Prediction* prediction,bool endpoint_accepted,bool raw_input_basis,
        std::uint64_t captured_frontier) noexcept {
    HelperFrequencyInterval result{reference.frequency_hz,reference.frequency_uncertainty_hz,
        reference.frequency_first_sample,reference.frequency_sample,false,
        "fresh direct tone frequency; optional phase mean unavailable"};
    if(!raw_input_basis) {
        result.reason="phase-view RF tightening needs the complete scored-support certificate; direct interval retained";
        return result;
    }
    if(!prediction || !endpoint_accepted || !reference.frequency_valid ||
       reference.frequency_fit==helper::FrequencyFit::unknown ||
       !reference.phase_valid || !reference.timing_valid || !reference.phase_observation_known ||
       reference.phase_observation_sample!=reference.sample || !reference.sample_rate ||
       reference.sample>=captured_frontier || captured_frontier-reference.sample>reference.sample_rate ||
       !std::isfinite(reference.phase_uncertainty_radians) || reference.phase_uncertainty_radians<0 ||
       reference.phase_uncertainty_radians>=.25 || !std::isfinite(reference.dwell_uncertainty_seconds) ||
       reference.dwell_uncertainty_seconds<0 || reference.dwell_uncertainty_seconds>.005 ||
       prediction->rate!=reference.sample_rate || prediction->last_sample!=reference.sample)return result;
    const auto mean=prediction->mean_frequency_interval();
    if(!mean)return result;
    const auto infinity=std::numeric_limits<double>::infinity();
    const auto raw_low=std::nextafter(reference.frequency_hz-reference.frequency_uncertainty_hz,-infinity);
    const auto raw_high=std::nextafter(reference.frequency_hz+reference.frequency_uncertainty_hz,infinity);
    // A fresh local measurement may move under UNKNOWN mean evolution. It
    // cannot be rejected merely because it left an older chord interval.
    if(mean->lower_hz>raw_high || mean->upper_hz<raw_low) {
        result.reason="current tone moved outside the conditional aperture mean; direct interval retained";
        return result;
    }
    const auto low=std::max(raw_low,mean->lower_hz),high=std::min(raw_high,mean->upper_hz);
    const auto center=low/2+high/2;
    const auto radius=std::nextafter(std::max(center-low,high-center),infinity);
    if(!std::isfinite(center) || !std::isfinite(radius) || radius<0 ||
       radius>=reference.frequency_uncertainty_hz) {
        result.reason="conditional aperture mean does not tighten the direct interval";return result;
    }
    result={center,radius,mean->first_sample,mean->last_sample,true,
        "fresh endpoint mean under the finite-aperture constant-mean/Wiener model; no prediction lease"};
    return result;
}
struct HelperTimingEvidence {
    std::optional<modem::PatternPeriodicTimingPrior> prior;
    std::uint64_t evidence_sample=0;
    const char* reason="punctuation timing is not independently qualified";
};
// The periodic anchor and the date that proves it are different coordinates.
// Captured ingress may guide frequency-only cold acquisition, but it cannot
// supply phase/punctuation evidence beyond this receiver's consumed input.
inline HelperTimingEvidence helper_timing_evidence(const helper::Reference& reference,
        std::uint64_t epoch,std::uint64_t origin,std::uint64_t observed_samples,std::uint32_t rate) {
    HelperTimingEvidence result;
    if(!rate || reference.sample_rate!=rate || !reference.timing_valid ||
       !std::isfinite(reference.dwell_start_seconds) ||
       !std::isfinite(reference.dwell_uncertainty_seconds) ||
       reference.dwell_uncertainty_seconds<0 || reference.dwell_uncertainty_seconds>=.005)return result;
    if(reference.sample<origin || reference.sample-origin>observed_samples) {
        result.reason="punctuation evidence is outside this receiver's consumed input";return result;
    }
    result.evidence_sample=reference.sample-origin;
    const auto observed=static_cast<long double>(reference.dwell_start_seconds)*rate-origin;
    if(!std::isfinite(observed) || observed<0 || observed>result.evidence_sample) {
        result.reason="punctuation anchor is outside its observed evidence coordinates";return result;
    }
    result.prior=modem::PatternPeriodicTimingPrior{observed,
        -static_cast<long double>(epoch%2)*rate,.4L*rate,
        static_cast<long double>(reference.dwell_uncertainty_seconds)*rate+1};
    result.reason="independently qualified causal punctuation; every periodic and clock alias retained";
    return result;
}
inline const char* helper_native_timing_reason(modem::PatternAcquisitionTimingStatus status)noexcept {
    using Status=modem::PatternAcquisitionTimingStatus;
    switch(status) {
    case Status::active:return "native timing assistance used independently qualified punctuation";
    case Status::unsupported_geometry:return "native timing declined for this ordinary receiver geometry";
    case Status::invalid_reference:return "timing awaits independently qualified punctuation within consumed input";
    case Status::unsupported_clock_domain:return "native timing retains unresolved clock-domain coverage";
    case Status::bounded_scoring_declined:return "native timing declined because bounded scoring was not eligible";
    case Status::numeric_selection_declined:return "native timing declined because its original numeric selection was not eligible";
    case Status::no_start_reduction:return "punctuation retains all original timing alternatives";
    case Status::not_requested:return "timing awaits qualified punctuation and eligible ordinary work";
    }
    return "native timing assistance unavailable";
}
// The tone is the reference even while no Robust message is being sent. Broad
// audits use new ordinary owners at a future input frontier; an empty/wrong
// epoch never causes its skipped historical search to be replayed.
inline std::uint64_t helper_audit_period(std::uint32_t rate,std::uint64_t symbol) noexcept {
    const auto symbols=symbol>UINT64_MAX/8?UINT64_MAX:8*symbol;
    return std::max<std::uint64_t>(6ULL*rate,symbols);
}
inline std::uint64_t helper_audit_deadline(std::uint64_t frontier,std::uint32_t rate,
                                         std::uint64_t symbol) noexcept {
    const auto period=helper_audit_period(rate,symbol);
    return frontier>UINT64_MAX-period?UINT64_MAX:frontier+period;
}

struct HelperBroadEpisode {
    std::uint64_t generation=0,origin=0,requested=0,committed=0,scored=0,retry_after=0,sample_limit=0;
    std::size_t workspace_argument_ceiling=0;
    std::uint32_t groups=0;
    bool active=false,admission_pending=false,periodic_slot=false;
    const char* reason="no broad coverage episode";
    void begin(std::uint64_t frontier,std::uint32_t families,const char* cause,bool partial=false) noexcept {
        ++generation;origin=frontier;groups=families;requested=committed=scored=sample_limit=0;
        workspace_argument_ceiling=0;
        active=admission_pending=true;periodic_slot=partial;reason=cause;
    }
    bool includes(std::size_t group)const noexcept {
        return group<32 && (groups&(std::uint32_t{1}<<group));
    }
    bool complete()const noexcept {
        return active && !admission_pending && requested && requested==committed && scored==committed;
    }
};

// Work credits bound optional quiet-link probes, not elapsed CPU time. One
// indivisible ordinary batch may overshoot its credit; that debt must be paid
// before another probe. Stored credit is capped at the previous packet, so a
// long payload cannot buy a later burst of exhaustive owners. Native template
// descriptors, compact projections and compact lane samples use separate jars.
struct HelperAuditTokens {
    static constexpr std::uint64_t divisor=8;
    std::uint64_t source=0,credit=0,debt=0,ceiling=1;
    std::uint8_t remainder=0;
    static std::uint64_t add(std::uint64_t a,std::uint64_t b)noexcept {
        return b>UINT64_MAX-a?UINT64_MAX:a+b;
    }
    void earn(std::uint64_t actual)noexcept {
        if(actual<source)return; // A different numeric lifetime cannot mint work.
        const auto delta=actual-source;source=actual;
        auto tokens=delta/divisor;
        remainder=static_cast<std::uint8_t>(remainder+delta%divisor);
        if(remainder>=divisor){remainder-=divisor;tokens=add(tokens,1);}
        const auto paid=std::min(tokens,debt);debt-=paid;tokens-=paid;
        credit=std::min(ceiling,add(credit,tokens));
    }
    bool ready()const noexcept{return credit && !debt;}
    void charge(std::uint64_t actual)noexcept {
        ceiling=std::max<std::uint64_t>(1,actual);
        const auto paid=std::min(credit,actual);credit-=paid;
        debt=add(debt,actual-paid);
    }
};

// Native quiet-link audits spend FFT-pair jobs, not descriptor visits or
// elapsed time. In particular, ONE initial token cannot buy an entire broad
// packet. An untouched constructor may learn the original packet bound; no
// PCM/scoring admission is allowed until that complete bound is prepaid.
struct HelperFftProbeTokens : HelperAuditTokens {
    std::uint64_t minimum_fft_jobs=0;
    HelperFftProbeTokens()noexcept {ceiling=UINT64_MAX;}
    void require_packet(std::uint64_t maximum_fft_pair_jobs)noexcept {
        if(!maximum_fft_pair_jobs)return;
        minimum_fft_jobs=std::max(minimum_fft_jobs,maximum_fft_pair_jobs);
        ceiling=minimum_fft_jobs;credit=std::min(credit,ceiling);
    }
    bool ready()const noexcept {
        return minimum_fft_jobs && credit>=minimum_fft_jobs && !debt;
    }
    bool may_learn_bound()const noexcept {return !minimum_fft_jobs && credit && !debt;}
    void charge(std::uint64_t actual_fft_jobs)noexcept {
        require_packet(actual_fft_jobs);
        HelperAuditTokens::charge(actual_fft_jobs);
        ceiling=minimum_fft_jobs;credit=std::min(credit,ceiling);
    }
};

// Optional acquisition CPU credit is measured in this worker's native CPU
// nanoseconds, not descriptors, wall time or a total-process CPU guarantee.
// One quantum can overrun its remaining credit; that measured excess becomes
// debt before any later work. No repeated polling can earn the same PCM twice.
struct HelperOptionalCpuCredit {
    std::uint64_t captured=0,credit=0,debt=0,used=0,overrun=0;
    bool clock_valid=true;
    static constexpr std::uint64_t cap=200000000,following_payload_burst=2000000;
    void earn(std::uint64_t frontier,std::uint32_t rate,double receiver_budget)noexcept {
        if(!clock_valid || !rate || frontier<captured || !std::isfinite(receiver_budget) || receiver_budget<=0)return;
        const auto frames=frontier-captured;captured=frontier;
        const auto ns=static_cast<long double>(frames)/rate*std::min(.2,receiver_budget)*1e9L;
        auto tokens=ns>=UINT64_MAX?UINT64_MAX:static_cast<std::uint64_t>(ns);
        const auto paid=std::min(tokens,debt);debt-=paid;tokens-=paid;
        credit=tokens>cap-credit?cap:credit+tokens;
    }
    bool charge(std::uint64_t before,std::uint64_t after,bool valid)noexcept {
        if(!valid || after<before) {clock_valid=false;return false;}
        const auto n=after-before;
        used=HelperAuditTokens::add(used,n);
        const auto paid=std::min(credit,n);credit-=paid;
        const auto excess=n-paid;debt=HelperAuditTokens::add(debt,excess);
        overrun=HelperAuditTokens::add(overrun,excess);return true;
    }
};

// This is a physical catalogue eligibility query, never a TX timestamp or a
// statement that there are no bits in retained PCM. ALL admissible short/long
// and keyed/plain families participate, including not-yet-born epoch rows.
inline std::optional<std::uint64_t> helper_startup_minimum_symbol(const Settings& value) {
    if(value.transfer.timestamp || value.helper.power_w<=0)return {};
    std::optional<std::uint64_t> minimum;
    const auto visit=[&](modem::Config base,bool keyed) {
        const auto profiles=value.transfer.automatic_receive_profiles?
            tuning::receive_profiles(base,value.transfer.receive_targets_db_hz,value.transfer.receive_pattern_mode,keyed):
            std::vector<modem::Config>{base};
        for(const auto& c:profiles) {
            if(!c.pattern_symbols || !c.oscillator_search || c.symbol_burst.enabled() || modem::scheduled_bursts(c) ||
               c.sample_rate!=value.transfer.modem.sample_rate)return false;
            const auto n=modem::symbol_sample_count(c);
            if(!n)return false;
            minimum=minimum?std::min(*minimum,n):n;
        }
        return true;
    };
    const auto family=[&](modem::Config c) {
        if(value.permits_plaintext()&&!visit(c,false))return false;
        if((value.transfer.key||!value.receive_keys.empty())&&!visit(c,true))return false;
        return true;
    };
    if(!family(value.transfer.modem) || (value.long_message_modem&&!family(*value.long_message_modem)))return {};
    return minimum;
}

// The optional tone fit may account for the public occupied data band, never
// decoded contents or the current frame. Include every receive profile and the
// full requested RF/audio-clock domain, even when the ordinary bank is limited.
inline std::optional<helper::OccupiedBand> helper_occupied_receive_band(const Settings& settings) noexcept {
    try {
        std::vector<modem::Config> profiles;
        const auto add=[&](bool keyed) {
            if(settings.transfer.automatic_receive_profiles) {
                auto family=tuning::receive_profiles(settings.transfer.modem,settings.transfer.receive_targets_db_hz,
                    settings.transfer.receive_pattern_mode,keyed);
                profiles.insert(profiles.end(),family.begin(),family.end());
            } else profiles.push_back(settings.transfer.modem);
        };
        if(settings.permits_plaintext())add(false);
        if(settings.transfer.key || !settings.receive_keys.empty())add(true);
        if(profiles.empty())return std::nullopt;
        if(settings.long_message_modem)profiles.push_back(*settings.long_message_modem);
        double lower=std::numeric_limits<double>::infinity(),upper=0;
        for(const auto& profile:profiles) {
            const auto half=helper::occupied_bandwidth(profile)/2;
            // The default local-clock expansion has a different coupled
            // policy. Until explicitly represented here, keep its raw fit.
            if(!profile.oscillator_search)return std::nullopt;
            const auto clock_policy=clock_sync::effective_policy(settings.transfer.clock_sync,
                transfer::shared_hop_calendar(profile));
            const auto search=modem::oscillator_pattern_search(profile,
                clock_policy?modem::utc_transmit_rate_limit(profile):0);
            const auto rf=search.frequency.requested_half_width_hz;
            const auto clock=search.requested_clock_half_width_ppm;
            const auto uncertainty=rf+(std::abs(profile.carrier_hz)+half)*clock*1e-6;
            const auto signed_low=profile.carrier_hz-half-uncertainty;
            const auto signed_high=profile.carrier_hz+half+uncertainty;
            const auto low=signed_low<=0 && signed_high>=0?0.:
                std::min(std::abs(signed_low),std::abs(signed_high));
            const auto high=std::max(std::abs(signed_low),std::abs(signed_high));
            if(!std::isfinite(low)||!std::isfinite(high)||high<=low)return std::nullopt;
            lower=std::min(lower,low);upper=std::max(upper,high);
        }
        if(!std::isfinite(lower)||upper<=lower)return std::nullopt;
        return helper::OccupiedBand{lower,upper};
    } catch(...) {
        return std::nullopt;
    }
}

inline long double helper_leading_seconds(std::uint64_t physical_origin,std::uint32_t rate,
                                         long double minimum_seconds=2.L) {
    if(!rate)throw Error("Helper Carrier lead has no source sample clock");
    if(!std::isfinite(minimum_seconds)||minimum_seconds<2.L)
        throw Error("invalid Helper Carrier lead duration");
    const auto epoch=physical_origin/rate;
    const auto origin=static_cast<long double>(physical_origin%rate)/rate;
    const auto parity=static_cast<long double>(epoch%2);
    const auto count=(parity+origin-minimum_seconds)*2.5L,nearest=std::round(count);
    const auto snapped=std::abs(count-nearest)<=8*std::numeric_limits<long double>::epsilon()*
        std::max(1.L,std::abs(count))?nearest:count;
    const auto start=std::floor(snapped)*.4L-parity;
    return origin-start;
}
inline std::uint64_t helper_envelope_samples(long double seconds,std::uint32_t rate) {
    const auto samples=std::ceil(seconds*rate);
    if(!rate||!std::isfinite(samples)||samples<0||samples>=std::ldexp(1.L,64))
        throw Error("Helper Carrier envelope exceeds the source sample clock");
    return static_cast<std::uint64_t>(samples);
}
struct HelperReceiveEnvelopeBound {
    long double fft_seconds=0,absence_seconds=0,pulse_seconds=0;
    std::size_t profiles=0;
};
inline long double helper_outward_span_seconds(std::uint64_t samples,std::uint32_t rate) {
    if(!rate)throw Error("Helper Carrier span has no sample clock");
    const auto quotient=samples/rate,remainder=samples%rate;
    auto whole=static_cast<long double>(quotient);
    // uint64-to-long-double can round downward on platforms where long double
    // is binary64. Avoid an out-of-range cast when UINT64_MAX rounds to2^64.
    if(whole<std::ldexp(1.L,64) && static_cast<std::uint64_t>(whole)<quotient)
        whole=std::nextafter(whole,std::numeric_limits<long double>::infinity());
    if(!remainder)return whole;
    const auto fraction=std::nextafter(static_cast<long double>(remainder)/rate,
        std::numeric_limits<long double>::infinity());
    return std::nextafter(whole+fraction,std::numeric_limits<long double>::infinity());
}
inline long double helper_outward_duration_sum(long double first,long double second) {
    const auto sum=first+second;
    if(!std::isfinite(sum)||first<0||second<0)
        throw Error("Helper Carrier duration exceeds the numerical range");
    if(first==0)return second;
    if(second==0)return first;
    return std::nextafter(sum,std::numeric_limits<long double>::infinity());
}
inline std::uint64_t helper_profile_fft_span(const modem::Config& config) {
    modem::validate(config);
    const auto symbol=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto divisor=std::gcd(std::gcd(chip,symbol),std::max<std::uint64_t>(1,chip/2));
    const auto limit=std::numeric_limits<std::uint64_t>::max();
    if(!divisor || divisor>limit/5 || divisor>limit/4)
        throw Error("Helper Carrier receive-profile FFT bound exceeds the sample counter");
    const auto extent=std::max(symbol,4*divisor),margin=5*divisor;
    if(extent>(limit-margin)/4)
        throw Error("Helper Carrier receive-profile FFT bound exceeds the sample counter");
    // Native b is1 or a divisor of g. Its validated +/-10000ppm domain
    // provides r>=.5 (a deliberately weaker proof premise), so L fits
    // P=pow2(2*max(4,ceil(S/b))) and (L+hop+4)*b<=(P+5)*b
    // <4*max(S,4*g)+5*g. Long-symbol geometry only reduces that span.
    // Amplitude FFTs are<=S/64; a compact owner may later become native.
    return 4*extent+margin;
}
inline HelperReceiveEnvelopeBound helper_receive_envelope_bound(const Settings& settings) {
    HelperReceiveEnvelopeBound result;
    std::vector<std::optional<Crypto>> keys;
    std::vector<Bytes> fingerprints;
    if(settings.permits_plaintext())keys.emplace_back();
    const auto add_key=[&](const Crypto& key) {
        const auto fingerprint=key.mac(Bytes{'D','P','-','R','X','-','B','A','N','K'});
        if(std::find(fingerprints.begin(),fingerprints.end(),fingerprint)==fingerprints.end()) {
            fingerprints.push_back(fingerprint);keys.emplace_back(key);
        }
    };
    if(settings.transfer.key)add_key(*settings.transfer.key);
    for(const auto& key:settings.receive_keys)add_key(key);
    const auto add_profile=[&](const modem::Config& base,const modem::Config& row,
                               const std::optional<Crypto>& key) {
        auto options=settings.transfer;options.modem=row;options.key=key;
        if(base.fake_hopping&&key)
            options.modem.fake_hopping=transfer::transmit_hopping(*base.fake_hopping,options);
        const auto profile=transfer::seeded_config(options,options.timestamp);
        if(profile.sample_rate!=settings.transfer.modem.sample_rate)
            throw Error("Helper Carrier cannot reserve an unknown receive-profile sample clock");
        result.fft_seconds=std::max(result.fft_seconds,
            helper_outward_span_seconds(helper_profile_fft_span(profile),profile.sample_rate));
        result.absence_seconds=std::max(result.absence_seconds,
            helper_outward_span_seconds(modem::pattern_absence_capture_samples(profile),profile.sample_rate));
        const auto shoulder=helper_outward_span_seconds(modem::pattern_pulse_padding_samples(profile),profile.sample_rate);
        result.pulse_seconds=std::max(result.pulse_seconds,
            helper_outward_duration_sum(shoulder,shoulder));
        ++result.profiles;
    };
    // Match make_bank's plaintext/keyed profile families, independent of current
    // residency, epoch, selected backend, bin size or constructor grant. Include
    // both explicit local geometries as a conservative short/long union too.
    for(const auto& key:keys)for(const auto* base:std::array<const modem::Config*,2>{
            &settings.transfer.modem,settings.long_message_modem?&*settings.long_message_modem:nullptr}) {
        if(!base)continue;
        if(settings.transfer.automatic_receive_profiles) {
            const auto profiles=tuning::receive_profiles(*base,settings.transfer.receive_targets_db_hz,
                settings.transfer.receive_pattern_mode,key.has_value());
            for(const auto& profile:profiles)add_profile(*base,profile,key);
        }
        add_profile(*base,*base,key);
    }
    return result;
}
inline helper::PlacementBand helper_placement_band(const Settings&);
inline std::uint32_t helper_predicted_capture_sample_rate(const Settings&,const modem::Config&);
// Same centered-support geometry as the optional fixed Resampler, without
// allocating one during a source-envelope forecast. Include every intermediate
// stage's finite queue just as source_lookahead_seconds() does.
inline long double helper_converter_lookahead(std::uint32_t input,std::uint32_t output) {
    if(input==output)return 2.L/input;
    if(static_cast<std::uint64_t>(input)>4ULL*output) {
        auto middle=output;while(4ULL*middle<input)middle*=4;
        return helper_converter_lookahead(input,middle)+helper_converter_lookahead(middle,output)+256.L/middle;
    }
    const auto ratio=std::min(1.,static_cast<double>(output)/input);
    const auto radius=std::ceil(std::nextafter(48/ratio,std::numeric_limits<double>::infinity()));
    return (static_cast<long double>(radius)+2)/input;
}
inline double helper_physical_notch_support(std::uint32_t physical_rate,std::uint32_t logical_rate) {
    if(!physical_rate||!logical_rate)throw Error("physical helper support has no sample clock");
    const auto count=static_cast<std::size_t>(std::ceil(physical_rate*.04))|1U;
    if(count<3||count>4097)throw Error("physical helper filter exceeds its optional 4097-tap bound");
    // Complete actually observed Hilbert startup, both sides of the centered
    // physical notch, both resampler startup/production supports, one raw
    // grid-alignment step, and ceiling/+1 rounding of the eligible logical bin.
    const auto quantum=physical_rate/std::gcd(physical_rate,logical_rate);
    const auto support=(2.L*helper::AnalyticFrequencyTranslator::group_delay+count-1+quantum)/physical_rate+
        2*helper_converter_lookahead(physical_rate,logical_rate)+2.L/logical_rate;
    return std::nextafter(static_cast<double>(support),std::numeric_limits<double>::infinity());
}
inline helper::TonePrecisionPrediction helper_lead_precision_prediction(
    const Settings&,const modem::Config&,double baseline_lead_seconds,double maximum_source_capture_ratio);

// The acquisition owner supplies its actual aperture/warmup here when its
// descriptor is available. An absent descriptor asserts no acquisition
// precision, and cannot qualify presence, frequency, phase or timing.
inline helper::EnvelopeRequirements helper_envelope_baseline_requirements(const Settings& settings,
        const modem::Config& config,const HelperReceiveEnvelopeBound& receive,
        const std::optional<helper::EnvelopeRequirements>& acquisition={}) {
    auto result=acquisition.value_or(helper::EnvelopeRequirements{});
    if(!config.sample_rate)throw Error("Helper Carrier envelope has no source sample clock");
    // Reserve every allowed physical profile, including later backend/epoch
    // births, rather than a snapshot of currently admitted first FFT batches.
    const auto fft=std::max(receive.fft_seconds,
        helper_outward_span_seconds(helper_profile_fft_span(config),config.sample_rate));
    const auto absence=std::max(receive.absence_seconds,
        helper_outward_span_seconds(modem::pattern_absence_capture_samples(config),config.sample_rate));
    const auto shoulder=helper_outward_span_seconds(modem::pattern_pulse_padding_samples(config),config.sample_rate);
    const auto pulse=std::max(receive.pulse_seconds,helper_outward_duration_sum(shoulder,shoulder));
    const auto deficit=helper_outward_duration_sum(
        helper_outward_duration_sum(helper_outward_duration_sum(fft,absence),1.L),pulse);
    auto outward_deficit=static_cast<double>(deficit);
    if(static_cast<long double>(outward_deficit)<deficit)outward_deficit=std::nextafter(outward_deficit,
        std::numeric_limits<double>::infinity());
    result.receiver_buffer_deficit_seconds=std::max(result.receiver_buffer_deficit_seconds,outward_deficit);
    if(settings.simulation) {
        // The physical sampled channel's centered FIR has at most128 logical
        // samples of lookahead. This is actual transport support, not a fit.
        result.frontend_lookahead_seconds=std::max(result.frontend_lookahead_seconds,128./config.sample_rate);
        result.maximum_source_capture_ratio=std::max(result.maximum_source_capture_ratio,
            1+settings.simulation_clock_error_ppm*1e-6);
    }
    if(config.oscillator_search) {
        auto options=settings.transfer;options.modem=config;
        const auto search=modem::oscillator_pattern_search(config,
            transfer::effective_clock_sync(options)?modem::utc_transmit_rate_limit(config):0);
        result.maximum_source_capture_ratio=std::max(result.maximum_source_capture_ratio,
            1+search.requested_clock_half_width_ppm*1e-6);
    }
    if(settings.helper.power_w>0) {
        // Compute the existing aperture/latency baseline first. Prediction only
        // adds bounded airtime; it never changes the Robust source calendar.
        try {
            const auto raw_rate=helper_predicted_capture_sample_rate(settings,config);
            if(raw_rate)result.frequency_acquisition_seconds=std::max(
                result.frequency_acquisition_seconds,8192./raw_rate+.4);
            if(raw_rate) {
                const auto expected=helper::plan(settings.helper,helper_placement_band(settings),raw_rate,0,
                    modem::outer_dsss_transmit_gain(config)).frequency_hz;
                if(expected<=helper::auxiliary_pilot_spacing_hz+225 || expected+225>=config.sample_rate/2.)
                    result.frontend_lookahead_seconds=std::max(result.frontend_lookahead_seconds,
                        helper_physical_notch_support(raw_rate,config.sample_rate));
            }
        }catch(const Error&) {} // Optional forecast cannot remove the physical baseline.
        catch(const std::bad_alloc&) {}
        // Actual bounded PCM staging waits for the local pilot phase aperture.
        // Charge this transport delay to the genuine lead/drain baseline,
        // separately from the at-most36s precision extension.
        result.frontend_lookahead_seconds=static_cast<double>(
            helper_outward_duration_sum(result.frontend_lookahead_seconds,.4L));
    }
    return result;
}
inline helper::EnvelopeRequirements helper_envelope_requirements(const Settings& settings,
        const modem::Config& config,const HelperReceiveEnvelopeBound& receive,
        const std::optional<helper::EnvelopeRequirements>& acquisition={},
        helper::TonePrecisionPrediction* precision=nullptr) {
    auto result=helper_envelope_baseline_requirements(settings,config,receive,acquisition);
    if(settings.helper.power_w>0) {
        const auto baseline=helper::plan_envelope(config,result);
        auto forecast=helper_lead_precision_prediction(settings,config,
            static_cast<double>(baseline.lead_seconds),result.maximum_source_capture_ratio);
        // Capped airtime remains planned when this valid request cannot predict
        // precision; it grants no frequency/phase/clock authority. Invalid or
        // allocation-failed forecasts keep both flags false and yield safely.
        if(forecast.supported || forecast.extension_capped)
            result.frequency_precision_extension_seconds=forecast.extra_lead_seconds;
        if(precision)*precision=std::move(forecast);
    }
    return result;
}
// Configured link prediction only, including when the GUI populates the same
// nominal-noise model for hardware. It is never a measured RF reference.
// Fractions remain explicit descriptor inputs: no random/deferred calendar is
// silently treated as a measured average-power or usable-tone certificate.
inline helper::ReferenceLevel helper_configured_reference_level(const Settings& settings,
        const modem::Config& config,double payload_power_fraction=1,double usable_tone_fraction=1) {
    const auto nominal=settings.simulation_snr_db+10*std::log10(settings.transfer.modem.sample_rate/2.);
    return helper::reference_level(settings.helper,nominal,
        settings.simulation?modem::outer_dsss_transmit_gain(config):1.,
        payload_power_fraction,usable_tone_fraction);
}
// Move every observation date together when capture is cleared. Zero is the
// frequency component's same-as-phase sentinel, not an independently dated
// observation at the replacement bank's origin.
inline std::optional<helper::Reference> rebase_helper_reference(helper::Reference reference,
        std::uint64_t origin,std::uint64_t frontier,std::uint32_t rate) {
    if(!rate || reference.sample_rate!=rate || reference.sample<origin || reference.sample>frontier)
        return std::nullopt;
    if(reference.frequency_fit!=helper::FrequencyFit::unknown) {
        const auto end=reference.frequency_sample?reference.frequency_sample:reference.sample;
        if(reference.frequency_first_sample<origin || reference.frequency_first_sample>end || end>reference.sample)
            return std::nullopt; // An optional fit crossing Clear has no new-bank support certificate.
        reference.frequency_first_sample-=origin;
    }
    if(reference.frequency_sample) {
        if(reference.frequency_sample<=origin || reference.frequency_sample>reference.sample)
            return std::nullopt;
        reference.frequency_sample-=origin;
    }
    if(reference.phase_observation_known) {
        if(reference.phase_observation_sample>reference.sample)return std::nullopt;
        if(reference.phase_observation_sample<origin)reference.phase_observation_known=false;
        else reference.phase_observation_sample-=origin;
    }
    reference.sample-=origin;
    reference.dwell_start_seconds-=static_cast<double>(origin)/rate;
    // Actual marker provenance is retained in raw history. This boundary has
    // not yet certified integer/fraction/support rebasing, so never expose raw
    // marker coordinates under a changed logical origin.
    reference.punctuation.known=false;
    return reference;
}

// Loss of an optional phase measurement does not change the oscillator already
// used for a partially scored Robust symbol. Continue its last accepted affine
// model until ordinary payload tracking/absence scoring releases that receiver.
struct HelperPhaseContinuation {
    std::uint64_t sample=0;
    double phase=0,frequency_hz=0;
    double at(std::uint64_t at,std::uint32_t rate)const {
        if(!rate)throw Error("helper phase continuation has no sample clock");
        const auto delta=at>=sample?static_cast<long double>(at-sample):
            -static_cast<long double>(sample-at);
        return static_cast<double>(static_cast<long double>(phase)+
            2*std::numbers::pi_v<long double>*frequency_hz*delta/rate);
    }
};

// Both local transmit geometries share one helper frequency. Selecting a long
// message or editing the callsign cannot move the carrier into the other band.
inline double helper_occupied_bandwidth(const Settings& settings) {
    const auto occupied=[](const modem::Config& c) {
        return helper::occupied_bandwidth(c);
    };
    auto bandwidth=occupied(settings.transfer.modem);
    if(settings.long_message_modem)bandwidth=std::max(bandwidth,occupied(*settings.long_message_modem));
    return bandwidth;
}
inline helper::PlacementBand helper_placement_band(const modem::Config& config,double occupied_bandwidth) {
    auto band=helper::placement_band(config);
    band.gate_bandwidth_hz=std::max(band.gate_bandwidth_hz,occupied_bandwidth-band.waveform_bandwidth_hz);
    return band;
}
inline helper::PlacementBand helper_placement_band(const Settings& settings) {
    auto band=helper::placement_band(settings.transfer.modem);
    if(settings.long_message_modem) {
        const auto other=helper::placement_band(*settings.long_message_modem);
        if(other.carrier_hz!=band.carrier_hz)throw Error("helper profiles require a shared carrier frequency");
        const auto full=std::max(band.waveform_bandwidth_hz+band.gate_bandwidth_hz,
            other.waveform_bandwidth_hz+other.gate_bandwidth_hz);
        band.waveform_bandwidth_hz=std::max(band.waveform_bandwidth_hz,other.waveform_bandwidth_hz);
        band.gate_bandwidth_hz=full-band.waveform_bandwidth_hz;
    }
    return band;
}
inline helper::FrequencyPrecisionOptions helper_precision_options(const modem::Config& config) {
    helper::FrequencyPrecisionOptions result;
    // The strong estimator uses the configured link model without receiving
    // any optional weak-profile workspace or inventing a clock reference.
    if(config.oscillator_search) {
        result.phase_noise_radians_per_sqrt_second=modem::oscillator_effects(config).
            phase_noise_degrees_per_sqrt_second*std::numbers::pi/180;
        result.maximum_audio_clock_ppm=std::max(result.maximum_audio_clock_ppm,
            config.oscillator_search->lf.accuracy_ppm*config.oscillator_search->margin);
    }
    return result;
}
// Same conditional HF evolution assumptions as the dated retune predictor.
// Project using actual RF/sideband geometry. Shared radio has one common
// oscillator coefficient, whereas independent RF/audio contributions add.
// Presets declare accuracy/phase noise, not a calibrated acceleration PSD.
inline double helper_continuous_frequency_process(const Settings& settings,const modem::Config& config,double pilot_hz) {
    if(!config.oscillator_search)return 0;
    const auto& p=*config.oscillator_search;
    if(p.rf_shift_hz<0||p.rf_shift_hz>54e6)return 0;
    const bool shared=p.reference==modem::OscillatorReference::shared_radio&&p.rf_shift_hz>0;
    const auto utc=transfer::receiver_utc_rate_correction(config,settings.transfer);
    const auto rf=p.sideband==modem::OscillatorSideband::upper?p.rf_shift_hz:-p.rf_shift_hz;
    const auto qr=p.rf.accuracy_ppm>0&&p.rf_shift_hz>0?
        helper::operational_hf_rf_acceleration_psd_ppm2_per_second3*1e-12:0.;
    // The operational source process includes the unresolved UTC correction
    // coordinate, as in the persistent model's effective B domain. Shared
    // radio's crystal contribution is already in (RF+H); only its additional
    // UTC coordinate is independent of that common crystal.
    const auto source_active=utc>0||(!shared&&p.lf.accuracy_ppm>0);
    const auto qa=source_active?helper::operational_hf_audio_acceleration_psd_ppm2_per_second3*1e-12:0.;
    const auto coefficient=rf+(shared?pilot_hz:0.);
    return coefficient*coefficient*qr+pilot_hz*pilot_hz*qa;
}

// Model the format selected by the same helper audio/channel negotiation.
// Hardware runtime qualification still uses the actual negotiated raw format.
inline std::uint32_t helper_predicted_capture_sample_rate(const Settings& settings,
        const modem::Config& config) {
    if(!settings.simulation)return helper::minimum_output_rate(settings.helper,
        helper_placement_band(settings),config.sample_rate);
    const auto h=helper::plan(settings.helper,helper_placement_band(settings),120000000,0,
        modem::outer_dsss_transmit_gain(config)).frequency_hz;
    return modem::SampledSimulationChannel::planned_physical_sample_rate(config,settings.helper,
        helper_occupied_bandwidth(settings),shared_helper_capture(config,h));
}
// Shared TX/Planner inputs. Metadata is confined to the auxiliary and cannot
// qualify or interrupt the pure pilot. Callsigns use their actual symbol count; three
// time symbols conservatively cover current/future Unix timestamps (early test
// epochs may use fewer). The current software always punctuates helper.
inline helper::TonePrecisionRequest helper_tone_precision_request(const Settings& settings,
        const modem::Config& config,std::uint32_t physical_rate,double straight_cn0_db_hz,
        double baseline_lead_seconds,double maximum_source_capture_ratio=1) {
    helper::TonePrecisionRequest request;
    request.sample_rate=physical_rate;request.straight_cn0_db_hz=straight_cn0_db_hz;
    request.pilot_segmented=true;
    // The production route and Fake-FHSS calendar do not retune physical RF.
    // Continuous pilot phase therefore survives the400ms amplitude ramps.
    request.pilot_observation=helper::PilotObservationMode::continuous_phase;
    const auto projection=std::max<std::size_t>(16,std::lround(physical_rate*.02));
    // Exact symmetric-Hann ENBW times the nonoverlapping projection period;
    // this is information lost by the implemented window, not fitted inflation.
    request.pilot_estimator_efficiency=std::sqrt(1.5*projection/(projection-1));
    // The25-seed actual HF channel has usable support starting0.335..1.063s.
    // Keep its explicit1.2s startup budget separate from genuine baseline lead.
    // This conditional software budget cannot qualify an actual reference.
    request.pilot_discovery_budget_seconds=1.2;
    const auto pilot=helper::plan(settings.helper,helper_placement_band(settings),120000000,0,
        modem::outer_dsss_transmit_gain(config)).frequency_hz;
    request.frequency_acceleration_psd_hz2_per_second3=helper_continuous_frequency_process(settings,config,pilot);
    request.phase_noise_radians_per_sqrt_second=settings.simulation?
        settings.simulation_phase_noise_degrees_per_sqrt_second*std::numbers::pi/180:
        helper_precision_options(config).phase_noise_radians_per_sqrt_second;
    const auto& callsign=settings.helper.callsign;
    request.metadata_symbols=!settings.transfer.key && !callsign.empty()?
        helper::callsign_packet(callsign).count:settings.helper.timecode?3:0;
    request.punctuate=true;request.baseline_lead_seconds=baseline_lead_seconds;
    request.maximum_source_capture_ratio=maximum_source_capture_ratio;
    return request;
}
inline helper::TonePrecisionPrediction helper_lead_precision_prediction(const Settings& settings,
        const modem::Config& config,double baseline_lead_seconds,double maximum_source_capture_ratio) {
    helper::TonePrecisionPrediction result;
    try {
        const auto physical_rate=helper_predicted_capture_sample_rate(settings,config);
        const auto level=helper_configured_reference_level(settings,config);
        result=helper::predict_tone_precision(helper_tone_precision_request(settings,config,
            physical_rate,level.pilot_straight_cn0_db_hz,baseline_lead_seconds,maximum_source_capture_ratio));
    }catch(const Error& error) {
        try {result.reason=error.what();}catch(const std::bad_alloc&) {}
    }catch(const std::bad_alloc&) {} // Precision prediction yields before waveform functionality.
    return result;
}

// Pure actual-geometry planning. A wide search uses its ENGINEERING coarse
// seed width; the observed peak never supplies a tighter prior. Runtime must
// revalidate the same descriptor at the actual seed center before funding.
struct HelperOptionalPlan {
    helper::FrequencyWorkOptions options;
    helper::FrequencyWorkGeometry geometry;
    double helper_hz=0,observer_half_width_hz=0,coarse_seconds=0;
    bool coarse=false;
    helper::EnvelopeRequirements envelope()const {
        helper::EnvelopeRequirements r;
        r.frequency_acquisition_seconds=std::nextafter(coarse_seconds+geometry.startup_support_seconds,
            std::numeric_limits<double>::infinity());
        return r;
    }
};
inline std::optional<HelperOptionalPlan> helper_optional_plan(const Settings& value,std::uint32_t physical_rate,
                                                            bool unsteered_capture=true) {
    if(value.helper.power_w<=0 || !physical_rate || !unsteered_capture || !value.transfer.modem.oscillator_search)
        return {};
    const auto& c=value.transfer.modem;const auto& policy=*c.oscillator_search;
    HelperOptionalPlan p;
    p.helper_hz=helper::plan(value.helper,helper_placement_band(value),physical_rate,0,
        modem::outer_dsss_transmit_gain(c)).frequency_hz;
    const bool shared=policy.reference==modem::OscillatorReference::shared_radio && policy.rf_shift_hz>0;
    const auto side=policy.sideband==modem::OscillatorSideband::upper?1.L:-1.L;
    const auto radius=shared?std::abs(side*policy.rf_shift_hz+p.helper_hz)*policy.rf.accuracy_ppm*policy.margin*1e-6L:
        (std::abs(policy.rf_shift_hz)*policy.rf.accuracy_ppm+std::abs(p.helper_hz)*policy.lf.accuracy_ppm)*
            policy.margin*1e-6L;
    const auto physical_radius=std::nextafter(static_cast<double>(radius),std::numeric_limits<double>::infinity());
    if(!std::isfinite(physical_radius) || physical_radius<0)return {};
    p.coarse=shared && physical_radius>100;
    p.options.sample_rate=physical_rate;p.options.coherent_phase_model=true;
    p.options.workspace_grant=helper::Receiver::optional_frequency_workspace_required();
    if(p.coarse) {
        const helper::CoarseToneSearch::Config cfg{physical_rate,p.helper_hz,physical_radius,4096,3};
        p.observer_half_width_hz=std::min(100.,std::max(20.,helper::CoarseToneSearch::seed_half_width_bound(cfg)));
        p.coarse_seconds=std::nextafter(static_cast<double>(3.L*cfg.fft_size/physical_rate),
            std::numeric_limits<double>::infinity());
    } else {
        p.observer_half_width_hz=std::min(100.,std::max(20.,physical_radius));
        p.options.physical_prior_half_width_hz=physical_radius;
    }
    p.geometry=helper::Receiver::optional_frequency_geometry(p.helper_hz,p.observer_half_width_hz,p.options,
        helper_precision_options(c));
    if(!p.geometry.supported || !p.geometry.eligible || !p.geometry.required_bytes)return {};
    return p;
}


}

#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/scheduled_symbol_calendar.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/symbol_schedule.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace datapump::modem {
PatternAmplitudeChannel pattern_amplitude_channel(const PatternAmplitudeGeometry& g,std::size_t index,std::size_t count) {
    if(!count)count=g.channel_count;
    if(!g.supported || !count || index>=count)throw Error("invalid amplitude energy channel");
    const auto span=g.center_high_bins-g.center_low_bins;
    const auto low=g.center_low_bins+span*static_cast<double>(index)/count;
    const auto high=g.center_low_bins+span*static_cast<double>(index+1)/count;
    const auto first=std::max(g.first_bin,static_cast<std::size_t>(std::floor(low-g.spectral_margin_bins)));
    const auto last=std::min(g.first_bin+g.bin_count-1,static_cast<std::size_t>(std::ceil(high+g.spectral_margin_bins)));
    return {first,last-first+1,.5*(low+high)*g.bin_width_hz};
}
std::uint64_t pattern_amplitude_phase_cell_upper(const Config& c,const PatternAmplitudeGeometry& g,std::uint64_t phase) {
    if(!scheduled_bursts(c))return canonical_phase_cell_upper(c,phase);
    if(packed_scheduled_symbols(c))return phase;
    const auto limit=canonical_phase_extent(c)-1;
    return phase+std::min(g.receiver_phase_step_samples-1,limit-std::min(phase,limit));
}
PatternAmplitudeGeometry pattern_amplitude_geometry(const Config& c,const PatternSearch& search) {
    PatternAmplitudeGeometry result;
    const auto fail=[&](const char* reason){result.reason=reason;return result;};
    if(!c.pattern_symbols || c.spreading_mode!=SpreadingMode::pattern)
        return fail("amplitude decoder requires pattern-mode binary symbols");
    const bool scheduled=c.scheduled_hopping && c.scheduled_hopping->enabled() && !c.scheduled_hopping->continuous();
    if((c.dsss || c.dsss_factor>1) && (!scheduled || c.outer_dsss_version!=OuterDsssVersion::interleaved_v2))
        return fail("amplitude decoder requires scheduled interleaved outer DSSS or DSSS Off");
    if(c.fake_hopping && !scheduled)return fail("amplitude decoder FHSS calendar is not yet supported");
    if(scheduled && !c.data_key)return fail("scheduled amplitude decoder requires its dedicated keyed calendar");
    if(scheduled && legacy_public_pattern(c))return fail("scheduled amplitude decoder requires independent amplitude profiles, not the legacy public sign-mask diagnostic");
    const bool packed=scheduled && packed_scheduled_symbols(c);
    if(packed) {
        try {const ScheduledSymbolCalendar calendar(c);(void)calendar;}
        catch(const Error& error){return fail(error.what());}
    }
    if(!pattern_pulse_enabled(c))return fail("amplitude decoder requires a finite shaped pattern alphabet");
    const auto total=symbol_sample_count(c),chip=pattern_chip_samples(c);
    result.receiver_phase_step_samples=canonical_phase_step(c);
    // Dated ordinary amplitude acquisition must resolve intensity, whose
    // bandwidth can be twice the shaped complex envelope's. Binary refinement
    // retains all preceding half-chip representatives in the full lattice.
    if(!scheduled && search.amplitude_start_window)result.receiver_timing_subdivisions=2;
    long double flat_samples=0;
    if(scheduled) {
        try {
            const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
            flat_samples=map.minimum_flat_samples();
            if(c.fake_hopping) {
                const FhssPowerShape shape(*c.fake_hopping,c.sample_rate,pattern_pulse_padding_samples(c),total);
                if(shape.enabled)flat_samples=std::min(flat_samples,
                    std::floor(shape.period)-2*shape.quiet-2*shape.support-2);
            }
        } catch(const Error& error){return fail(error.what());}
        if(flat_samples<16)return fail("scheduled amplitude flat interval has fewer than16 samples");
        const auto cap=static_cast<std::uint64_t>(std::max(1.L,std::floor(flat_samples/8)));
        if(!packed && result.receiver_phase_step_samples>cap) {
            // Subdivide only at divisors of the original cipher-address cell.
            // This keeps all original representatives and cannot cross an
            // original epoch/ordinal boundary merely to fit a smaller FFT.
            const auto old=result.receiver_phase_step_samples;
            std::uint64_t step=1;
            for(std::uint64_t divisor=1;divisor<=old/divisor;++divisor)if(old%divisor==0) {
                if(divisor<=cap)step=std::max(step,divisor);
                if(old/divisor<=cap)step=std::max(step,old/divisor);
            }
            result.receiver_phase_step_samples=step;
        }
        // Intensity is the squared complex envelope and can vary twice as fast
        // as the underlying shaped chip. Use at least quarter-chip arrival
        // spacing, retaining every preceding half-chip representative by
        // binary refinement. This is a resolution policy, not a universal
        // sensitivity guarantee for shaped/limited private rows.
        const auto subdivisions=std::max(2.L,std::ceil(2*static_cast<long double>(chip)/flat_samples));
        if(subdivisions>=std::ldexp(1.L,64))return fail("scheduled timing subdivision would overflow");
        while(result.receiver_timing_subdivisions<subdivisions) {
            if(result.receiver_timing_subdivisions>std::numeric_limits<std::uint64_t>::max()/2)
                return fail("scheduled timing subdivision would overflow");
            result.receiver_timing_subdivisions*=2;
        }
    }
    if((!scheduled && total<16ULL*c.sample_rate) || pattern_chips_per_symbol(c)<64)
        return fail("amplitude decoder requires64 chips and ordinary reception requires a16-second symbol");
    if(!search.start_offset_seconds || !std::isfinite(*search.start_offset_seconds) ||
       !std::isfinite(search.start_uncertainty_seconds) || search.start_uncertainty_seconds<0)
        return fail("amplitude decoder requires a finite arrival window");
    double frequency=0,clock=0;
    if(search.admitted_frequency_half_width_hz) {
        if(!std::isfinite(*search.admitted_frequency_half_width_hz) || *search.admitted_frequency_half_width_hz<0)
            return fail("amplitude decoder declared frequency domain is invalid");
        frequency=*search.admitted_frequency_half_width_hz;
    }
    if(search.admitted_clock_half_width_ppm) {
        if(!std::isfinite(*search.admitted_clock_half_width_ppm) || *search.admitted_clock_half_width_ppm<0)
            return fail("amplitude decoder declared clock domain is invalid");
        clock=*search.admitted_clock_half_width_ppm;
    }
    if(c.oscillator_search) {
        const auto bank=oscillator_pattern_search(c);
        frequency=std::max(frequency,bank.frequency.requested_half_width_hz);
        clock=std::max(clock,bank.requested_clock_half_width_ppm);
    } else if(search.expand_clock_search)frequency=std::max(frequency,default_pattern_frequency_search(c).requested_half_width_hz);
    for(const auto& h:search.hypotheses) {
        if(!std::isfinite(h.frequency_offset_hz) || !std::isfinite(h.clock_error_ppm))
            return fail("amplitude decoder explicit frequency/clock domain is invalid");
        frequency=std::max(frequency,std::abs(h.frequency_offset_hz));
        clock=std::max(clock,std::abs(h.clock_error_ppm));
    }
    for(auto f:search.frequency_offsets_hz) {
        if(!std::isfinite(f))return fail("amplitude decoder explicit frequency domain is invalid");
        frequency=std::max(frequency,std::abs(f));
    }
    for(auto r:search.clock_errors_ppm) {
        if(!std::isfinite(r))return fail("amplitude decoder explicit clock domain is invalid");
        clock=std::max(clock,std::abs(r));
    }
    // Legacy independent-rate and carrier-coupled alternatives are resolved
    // before replacing the complex bank. Authoritative pairs already contain
    // their combined rates. Automatic expanded search retains both nominal
    // and coupled clocks when its original frequency bank has >5 positions.
    if(search.hypotheses.empty()) {
        double coupled=0;
        if(search.couple_clock_to_carrier) {
            if(search.frequency_offsets_hz.empty())coupled=frequency/c.carrier_hz*1e6;
            else for(std::size_t j=search.uncoupled_frequency_count;j<search.frequency_offsets_hz.size();++j)
                coupled=std::max(coupled,std::abs(search.frequency_offsets_hz[j]/c.carrier_hz)*1e6);
        } else if(!c.oscillator_search && search.expand_clock_search && search.frequency_offsets_hz.empty() &&
                  search.clock_errors_ppm==std::vector<double>{0} && default_pattern_frequency_search(c).count>5)
            coupled=default_pattern_frequency_search(c).requested_half_width_hz/c.carrier_hz*1e6;
        if(coupled>0)clock=( (1+clock*1e-6)*(1+coupled*1e-6)-1 )*1e6;
    }
    if(!std::isfinite(frequency) || !std::isfinite(clock) || clock>10000)
        return fail("amplitude decoder oscillator domain is invalid");
    if(!c.oscillator_search && !search.expand_clock_search && search.hypotheses.empty() && search.frequency_offsets_hz.empty())
        frequency=std::max(frequency,2*.25*c.sample_rate/static_cast<double>(total));
    result.admitted_frequency_half_width_hz=frequency;
    result.clock_half_width_ppm=clock;
    // Clock displacement is bounded over the ORIGINAL whole symbol. This
    // grid is independent of any capped coherent carrier/rate bank.
    // Paused source time still uses a continuous physical sample-clock map.
    // A rate error accumulates over wall airtime, including every quiet hop.
    const auto clock_span=scheduled?scheduled_physical_span_samples(c,total):total;
    const auto displacement=scheduled?std::min(chip/4.L,flat_samples/8):chip/4.L;
    const auto radius=static_cast<double>(1e6L*displacement/clock_span);
    if(clock<=radius)result.clock_hypotheses=1;
    else {
        const auto original_radius=.25e6*static_cast<double>(chip)/static_cast<double>(clock_span);
        auto half=std::ceil(clock/(2*original_radius));
        if(!(half>=1) || half>32)return fail("amplitude decoder full clock domain requires more than 65 lanes");
        auto spacing=clock/half;
        // Binary subdivision retains every preceding FP64 rate value exactly,
        // instead of replacing that lattice with a different uniform bank.
        while(spacing/2>radius) {
            half*=2;spacing/=2;
            if(half>32)return fail("amplitude decoder full clock domain requires more than 65 lanes");
        }
        result.clock_hypotheses=2*static_cast<std::size_t>(half)+1;
        result.clock_step_ppm=spacing;
    }
    // Chip-scale blocks reduce guard-bin noise and repeated template work.
    // Retain at least64 observations per symbol and at most two chips/block;
    // arbitrary larger averaging erases the useful private amplitude profile.
    // The cap bounds buffering without retaining whole-symbol PCM.
    const auto target=std::max<std::uint64_t>(16,chip);
    result.fft_samples=16;
    while(result.fft_samples<target && result.fft_samples<65536)result.fft_samples*=2;
    while(result.fft_samples>16 && (result.fft_samples>2*chip || total/result.fft_samples<64))result.fft_samples/=2;
    if(scheduled) {
        const auto& plan=*c.scheduled_hopping;plan.validate();
        // Complete independent real-PCM FFTs stay inside one active flat hop.
        // Bound EACH coverage-cell contribution, independently of long chips:
        // phase<=W/8, full timing cell<=W/4, and clock displacement<=W/8
        // per endpoint over the complete physical wall span. No hypothesis
        // coverage is removed; refinement increases the charged lane count.
        const auto cell=search.search_stream_phases?result.receiver_phase_step_samples-1:0;
        const auto timing=search.start_uncertainty_seconds>0?chip/(2.L*result.receiver_timing_subdivisions):0.L;
        const auto clock_radius=(result.clock_hypotheses>1?result.clock_step_ppm/2:clock)*1e-6L*clock_span;
        const auto available=std::floor((flat_samples-cell-timing-2*clock_radius)/(1+clock*1e-6));
        while(result.fft_samples>16 && result.fft_samples>available)result.fft_samples/=2;
        if(result.fft_samples>available)return fail("scheduled amplitude FFT and timing guards do not fit an active hop");
        // A physical FFT lattice may omit one window at each flat endpoint.
        // Require64 whole retained observations, not64 nominal source blocks.
        const auto source_group=static_cast<long double>(2ULL*c.sample_rate/5)*plan.group_hops;
        const auto groups=std::ceil(static_cast<long double>(total)/source_group)+1;
        const auto retained=[&] {
            if(packed) {
                // The joint calendar places the ENTIRE shaped bit in a flat
                // interval. Its waits are outside the bit, not missing energy.
                // Reserve eight FFT windows for both lattice endpoints, three
                // quarter boundaries and conservative fractional coverage.
                return std::floor(static_cast<long double>(total)/result.fft_samples)-8;
            }
            // The physical flat allowance already covers the canonical phase
            // cell. Within a certified affine group that phase offset cancels
            // against the source anchor, so quarters need no second cell loss.
            const auto lost_per_group=std::max(0.L,source_group-available)+2*result.fft_samples;
            return std::floor(std::max(0.L,static_cast<long double>(total)-groups*lost_per_group)/result.fft_samples)-3;
        };
        while(result.fft_samples>16 && retained()<64)result.fft_samples/=2;
        if(retained()<64)return fail("scheduled amplitude decoder requires64 retained whole FFT observations per symbol");
    }
    if(result.fft_samples>2*chip || total/result.fft_samples<64)
        return fail("amplitude decoder requires64 disjoint whole-symbol energy observations");
    result.block_seconds=static_cast<double>(result.fft_samples)/c.sample_rate;
    const auto width=static_cast<double>(c.sample_rate)/result.fft_samples;
    result.bin_width_hz=width;
    const auto waveform=(1+pattern_pulse_rolloff)*c.sample_rate/(2.*static_cast<double>(chip))*(1+clock*1e-6);
    const auto low=c.carrier_hz-frequency-waveform,high=c.carrier_hz+frequency+waveform;
    result.center_low_bins=(c.carrier_hz-frequency)/width;
    result.center_high_bins=(c.carrier_hz+frequency)/width;
    const double guard_bins=pattern_chips_per_symbol(c)<256?8:2;
    result.spectral_margin_bins=waveform/width+guard_bins;
    // Rectangular finite observations have spectral tails. Two extra bins
    // keep the full nominal waveform/uncertainty band and outperform four/eight
    // in bounded paired detection tests. Sampled shaped/limited tail loss was
    // up to 0.174 dB; this is experimental, not a universal 0.1 dB certificate.
    // Short rows retain the wider eight-bin construction: with very few
    // observations the conservative unknown-noise certificate can otherwise
    // fail to admit even an undistorted legitimate amplitude profile.
    // Every retained noise bin is charged to Gamma degrees/channel bandwidth.
    const auto first=std::floor(low/width)-guard_bins,last=std::ceil(high/width)+guard_bins;
    if(first<1 || last>=static_cast<double>(result.fft_samples/2))
        return fail("amplitude energy band with transition margins crosses DC or Nyquist");
    result.first_bin=static_cast<std::size_t>(first);
    result.bin_count=static_cast<std::size_t>(last-first+1);
    result.retained_low_hz=first*width;
    result.retained_high_hz=(last+1)*width;
    // Coarse carrier cells follow waveform/block bandwidth rather than the
    // whole-symbol coherent frequency resolution. Their union covers the
    // complete declared interval; overlapping guards need no independence
    // between channels because admission uses an explicit union bound.
    const auto intrinsic_bins=2*result.spectral_margin_bins+1;
    const auto cells=std::ceil((result.center_high_bins-result.center_low_bins)/intrinsic_bins);
    result.channel_count=static_cast<std::size_t>(std::clamp(cells,1.,32.));
    if(search.amplitude_channel_limit)result.channel_count=std::min(result.channel_count,search.amplitude_channel_limit);
    result.supported=true;result.reason=c.search_arithmetic==SearchArithmetic::fp64?
        "experimental orthogonal I/Q energy; native FP64 FFT; original FP64 envelope; wide moments; full carrier domain":
        "experimental orthogonal I/Q energy; native FP32 FFT with FP64 range fallback; FP32 envelope dots; FP64 shaping; wide moments; full carrier domain";
    if(scheduled)result.reason+="; flat-safe subdivided timing/phase/clock cells; split-symbol active-only FFT observations; physical timestamps and virtual-source quadrature";
    return result;
}
}

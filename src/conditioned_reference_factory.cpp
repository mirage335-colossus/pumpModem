#include "conditioned_reference_factory.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "estimate_cancellation.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace datapump::simulation::detail::conditioned_factory {
Result make_first_bit_request(const modem::PatternReceiver& ordinary,
        std::span<const std::uint8_t> selected,const SourceConditions& conditions,std::stop_token stop) {
    Result out;const auto fail=[&](const std::string& reason){out.reason=reason;return out;};
    estimate_detail::check(stop);
    modem::detail::ConditionedReferenceDescriptor descriptor;
    if(!ordinary.export_conditioned_reference(descriptor,conditioned_private::maximum_original_jobs,
            conditioned_private::maximum_raw_samples))return fail(descriptor.reason);
    const auto& c=descriptor.config;
    if(c.sample_rate!=14400||modem::symbol_sample_count(c)!=5120||c.dsss_factor!=10||
       c.outer_dsss_version!=modem::OuterDsssVersion::interleaved_v2||!modem::deferred_symbols(c)||
       !c.fake_hopping||c.symbol_burst.enabled()||modem::scheduled_bursts(c))
        return fail("Actual owner is outside the native deferred B 14400 Hz/5120-sample Interleave10 reference");
    if(descriptor.geometry.search_arithmetic!=static_cast<std::uint32_t>(modem::SearchArithmetic::fp64))
        return fail("Finite reference currently requires an explicitly selected ordinary FP64 owner; automatic-width production B remains a separate coverage gap");
    if(selected.size()!=descriptor.original_pairs||std::any_of(selected.begin(),selected.end(),[](auto v){return v>1;}))
        return fail("RF selection is not Boolean in the actual owner's original pair order");
    if(conditions.channel.phase_noise_degrees_per_sqrt_second!=0)
        return fail("Finite source reference is fixed-phase only; configured oscillator phase diffusion is not silently removed");
    if(conditions.frequency_correction_hz!=0)
        return fail("Pristine original-coordinate factory supports origin-zero RF masks only; a nonzero frozen NCO requires a separately translated original-statistical descriptor");
    if(!conditions.independent_prior||!conditions.prior_evidence_end_sample||
       !conditions.starts||conditions.starts>32||!std::isfinite(conditions.frequency_correction_hz)||
       !std::isfinite(conditions.raw_noise_variance)||conditions.raw_noise_variance<=0||
       !std::isfinite(conditions.signal_scale)||conditions.signal_scale<0||
       !conditions.maximum_rendered_samples||conditions.maximum_rendered_samples>131072)
        return fail("Finite source conditions or render cap are invalid");
    helper::PayloadFilter filter;
    try {if(conditions.payload_filter_hz)filter=helper::PayloadFilter(c.sample_rate,*conditions.payload_filter_hz);}
    catch(const Error& e){return fail(e.what());}
    const auto bin=descriptor.geometry.bin_samples,bins=descriptor.geometry.bins_per_symbol;
    if(!bin||!bins||bins>conditioned_private::maximum_raw_samples/bin||
       conditions.starts-1>conditioned_private::maximum_raw_samples/bin-bins)
        return fail("Complete clock-scaled first-symbol observation exceeds raw bound");
    const auto outputs=(bins+conditions.starts-1)*bin;
    if(2*filter.delay()>conditioned_private::maximum_raw_samples-outputs)
        return fail("Actual fixed FIR halo exceeds raw bound");
    const auto raw_count=outputs+2*filter.delay();
    conditioned_private::FirstBitRequest request;request.config=c;request.geometry=descriptor.geometry;
    request.original_jobs=descriptor.jobs;request.selected_original_cells.reserve(descriptor.jobs.size());
    for(const auto& job:descriptor.jobs) {
        if(job.frequency_index>=selected.size())return fail("Actual exported job names an invalid original pair");
        request.selected_original_cells.push_back(selected[static_cast<std::size_t>(job.frequency_index)]);
    }
    const auto selected_jobs=static_cast<std::size_t>(std::count(request.selected_original_cells.begin(),request.selected_original_cells.end(),1));
    if(!selected_jobs||selected_jobs>conditioned_private::maximum_selected_jobs)
        return fail("Full initial-symbol/phase/LF union exceeds selected-reference job bound");
    request.competing_frequencies=descriptor.competing_frequencies;
    request.original_acquisition_threshold=descriptor.original_acquisition_threshold;
    request.original_threshold_trials=descriptor.original_threshold_trials;
    request.starts=conditions.starts;request.trials=conditions.trials;
    request.signal_scale=conditions.signal_scale;request.raw_noise_variance=conditions.raw_noise_variance;
    request.conditioning.filter_hz=conditions.payload_filter_hz;
    request.conditioning.frequency_correction_hz=conditions.frequency_correction_hz;
    request.conditioning.prior_evidence_end_sample=conditions.prior_evidence_end_sample;
    request.conditioning.independent_prior=conditions.independent_prior;
    std::optional<std::uint64_t> shared_first;
    for(unsigned bit=0;bit<2;++bit) {
        estimate_detail::check(stop);
        // Use production framing/source, interpolation, fractional startup,
        // clock rate and carrier phase. The tiny built-in300dB channel noise
        // remains fixed in this source realization and is explicitly disclosed;
        // independent requested AWGN is added by the finite reference later.
        auto channel=conditions.channel;channel.snr_db=300;
        modem::SampledSimulationChannel sampler(c,channel);
        modem::StreamingTransmitter source(modem::RawBits{Bytes{static_cast<std::uint8_t>(bit)}},c,
            conditions.transmitter_workspace_bytes);
        const auto rate=1+static_cast<long double>(channel.clock_error_ppm)*1e-6L;
        if(!(rate>0))return fail("Source clock rate is invalid");
        const auto prefix=static_cast<long double>(modem::training_sample_count(c))+
            modem::pattern_pulse_padding_samples(c)+modem::scheduled_symbol_offset(c,c.stream_epoch,c.stream_phase_samples,0);
        const auto first_output=std::floor(static_cast<long double>(sampler.startup_offset_samples())+prefix/rate);
        if(!std::isfinite(first_output)||first_output<filter.delay()||first_output>std::numeric_limits<std::uint64_t>::max()-raw_count)
            return fail("Finite first-symbol source coordinates are invalid");
        const auto first=static_cast<std::uint64_t>(first_output)-filter.delay(),end=first+raw_count;
        if(end>conditions.maximum_rendered_samples)return fail("Production source settling/calendar/FIR support exceeds render cap");
        if(shared_first&&*shared_first!=first)return fail("Production bit alternatives do not share the same channel support");
        shared_first=first;request.source_pcm[bit].resize(raw_count);
        std::array<float,4096> pcm{};std::uint64_t emitted=0;
        while(emitted<end) {
            estimate_detail::check(stop);const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(pcm.size(),end-emitted));
            const auto n=sampler.read(source,std::span(pcm).first(count),stop);
            if(!n)return fail("Production channel ended before complete first-symbol FIR support");
            const auto lower=std::max(emitted,first),upper=std::min(emitted+n,end);
            if(lower<upper)std::copy_n(pcm.begin()+static_cast<std::ptrdiff_t>(lower-emitted),
                static_cast<std::size_t>(upper-lower),request.source_pcm[bit].begin()+static_cast<std::ptrdiff_t>(lower-first));
            emitted+=n;
        }
        out.work.source_raw_samples+=static_cast<std::size_t>(emitted);
        out.work.transmitter_workspace_bytes=std::max(out.work.transmitter_workspace_bytes,source.working_bytes());
        out.work.channel_workspace_bytes=std::max(out.work.channel_workspace_bytes,sampler.working_bytes());
    }
    request.conditioning.first_raw_sample=*shared_first;
    if(conditions.prior_evidence_end_sample>*shared_first)return fail("Helper evidence overlaps first-symbol raw FIR halo");
    out.work.source_sample_rate=c.sample_rate;out.work.detector_sample_rate=c.sample_rate;
    out.work.retained_raw_samples=2*raw_count;out.work.receiver_workspace_bytes=descriptor.pristine_workspace_bytes;
    out.work.descriptor_bytes=descriptor.descriptor_bytes;out.work.original_jobs=descriptor.jobs.size();
    out.work.selected_jobs=selected_jobs;out.work.original_starts=descriptor.original_starts;
    out.work.original_threshold_trials=descriptor.original_threshold_trials;
    out.request=std::move(request);
    out.reason="Actual pristine ordinary native B jobs, complete RF/LF/initial-phase union and original batch-end threshold. Production source/channel includes configured static CFO and audio-clock error, fixed seeded startup/carrier phase and retained300dB built-in source noise. Known-onset-cropped isolated first-bit reference at up to32 nearby starts; the full original batch-end threshold is conservative and does not model full arrival-search acquisition. Phase diffusion, helper leakage, full reception, completion and CPU calibration remain outside coverage.";
    return out;
}
}

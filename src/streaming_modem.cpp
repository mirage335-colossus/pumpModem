#include "datapump/streaming_modem.hpp"
#include "native_peak_workspace.hpp"
#include "native_point_workspace.hpp"
#include "native_diagnostic_history.hpp"
#include "datapump/pattern_code.hpp"
#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace datapump::modem {
namespace {
using Complex=std::complex<double>;
void cancelled(std::stop_token stop) {if(stop.stop_requested())throw Error("modem operation cancelled");}
}

struct StreamingTransmitter::Impl {
    std::unique_ptr<PatternTransmitter> pattern;
    std::unique_ptr<NoiseTransmitter> noise;
    TransmitTrace trace;
    std::uint64_t chips_per_symbol=1;
    std::vector<Complex> constellation;
    std::size_t constellation_begin=0,constellation_count=0,pending_count=0;
    std::uint64_t dropped=0;
    std::size_t initialize_constellation(const Config& config,std::size_t workspace) {
        const auto count=StreamingTransmitter::constellation_history_capacity(config);
        const auto fixed=sizeof(StreamingTransmitter)+sizeof(Impl)+trace.working_bytes();
        if(fixed>workspace || count>(workspace-fixed)/sizeof(Complex))
            throw Error("pattern transmitter workspace is too small");
        constellation.resize(count);
        const auto bytes=fixed+constellation.capacity()*sizeof(Complex);
        if(bytes>workspace)throw Error("pattern transmitter workspace is too small");
        return workspace-bytes;
    }
    Impl(Bytes wire,Config config,std::size_t workspace) {
        validate(config);
        const auto remaining=initialize_constellation(config,workspace);
        if(wire.size()>std::numeric_limits<std::size_t>::max()/8 ||
           wire.capacity()>remaining || wire.size()>(remaining-wire.capacity())/8)
            throw Error("pattern transmitter workspace is too small");
        config.memory_limit=remaining;
        Bytes bits;bits.reserve(wire.size()*8);
        for(const auto byte:wire)for(unsigned bit=0;bit<8;++bit)
            bits.push_back(static_cast<std::uint8_t>((byte>>(7-bit))&1U));
        pattern=std::make_unique<PatternTransmitter>(std::move(bits),config,config.stream_epoch);
        if(pattern->working_bytes()>remaining)throw Error("pattern transmitter workspace is too small");
    }
    Impl(RawBits input,Config config,std::size_t workspace,TransmitTrace capture):trace(std::move(capture)) {
        validate(config);
        if(input.bits.empty())throw Error("raw binary transmission requires at least one bit");
        if(trace.active) {
            trace.total_wire_bits=input.bits.size();trace.generated_bits=0;trace.generated_chips=0;trace.revision=1;
            trace.tone=config.spreading_mode==SpreadingMode::tone;
            trace.pattern_available=!trace.tone;trace.pattern_private=config.scramble;trace.dsss=config.dsss;
            trace.outer_dsss_factor=config.dsss_factor;
            trace.outer_dsss_version=config.dsss_factor>1?static_cast<unsigned>(config.outer_dsss_version):1;
            trace.wire_bits.assign(input.bits.begin(),input.bits.begin()+static_cast<std::ptrdiff_t>(
                std::min(input.bits.size(),TransmitTrace::bit_limit)));
        }
        chips_per_symbol=pattern_chips_per_symbol(config);
        const auto remaining=initialize_constellation(config,workspace);
        if(input.bits.capacity()>remaining)throw Error("pattern transmitter workspace is too small");
        config.memory_limit=remaining;
        pattern=std::make_unique<PatternTransmitter>(std::move(input.bits),config,config.stream_epoch,0,true,trace.active);
        if(pattern->working_bytes()>remaining)throw Error("pattern transmitter workspace is too small");
    }
    Impl(Noise input,Config config,std::size_t workspace,bool prepared=false) {
        trace.temporary_key=true;trace.pattern_available=true;
        trace.pattern_private=true;trace.dsss=config.dsss;trace.data_masked=true;
        trace.outer_dsss_factor=config.dsss_factor;
        trace.outer_dsss_version=static_cast<unsigned>(config.outer_dsss_version);
        chips_per_symbol=pattern_chips_per_symbol(config);
        const auto remaining=initialize_constellation(config,workspace);
        config.memory_limit=remaining;
        noise=prepared?std::make_unique<NoiseTransmitter>(config,NoiseTransmitter::PreparedTemporaryKeys{},input.symbols):
            std::make_unique<NoiseTransmitter>(config,input.symbols);
        if(noise->working_bytes()>remaining)throw Error("noise transmitter workspace is too small");
    }
    void retain_constellation(Complex value) {
        if(trace.active || trace.temporary_key) {
            ++trace.generated_chips;
            trace.generated_bits=static_cast<std::size_t>((trace.generated_chips-1)/chips_per_symbol+1);
            ++trace.revision;
        }
        if(constellation_count==constellation.size()) {
            constellation_begin=(constellation_begin+1)%constellation.size();--constellation_count;
        }
        constellation[(constellation_begin+constellation_count++)%constellation.size()]=value;
        if(pending_count<constellation.size())++pending_count;
        else if(dropped<std::numeric_limits<std::uint64_t>::max())++dropped;
    }
};
std::size_t StreamingTransmitter::constellation_history_capacity(const Config& config) {
    const auto chip=pattern_chip_samples(config),symbol=symbol_sample_count(config);
    const auto chips=symbol/chip+(symbol%chip!=0);
    const auto samples=config.sample_rate/constellation_frame_rate+(config.sample_rate%constellation_frame_rate!=0);
    const auto remainder=samples%symbol;
    // Every complete symbol contributes all of its chips. A leftover span
    // can straddle a shortened final chip, and its first chip may have begun
    // before the frame. Keep both boundary observations as well.
    const auto partial=remainder?std::min(chips,remainder/chip+(remainder%chip!=0)+1):0;
    return std::max(constellation_history_limit,static_cast<std::size_t>((samples/symbol)*chips+partial+1));
}
StreamingTransmitter::StreamingTransmitter(Bytes wire,Config c,std::size_t workspace):impl_(std::make_unique<Impl>(std::move(wire),c,workspace)){}
StreamingTransmitter::StreamingTransmitter(RawBits bits,Config c,std::size_t workspace,TransmitTrace trace):
    impl_(std::make_unique<Impl>(std::move(bits),c,workspace,std::move(trace))){}
StreamingTransmitter::StreamingTransmitter(Noise input,Config c,std::size_t workspace) {
    c.data_key.reset();
    OPENSSL_cleanse(c.spreading_seed.data(),c.spreading_seed.size());
    OPENSSL_cleanse(c.dsss_seed.data(),c.dsss_seed.size());
    c.spreading_mode=SpreadingMode::pattern;c.scramble=true;
    impl_=std::make_unique<Impl>(input,c,workspace);
}
Config StreamingTransmitter::prepare_noise_config(Config config) { return NoiseTransmitter::prepare_config(std::move(config)); }
StreamingTransmitter::StreamingTransmitter(Noise input,Config c,PreparedTemporaryKeys,std::size_t workspace):
    impl_(std::make_unique<Impl>(input,std::move(c),workspace,true)) {}
StreamingTransmitter::~StreamingTransmitter()=default;
StreamingTransmitter::StreamingTransmitter(StreamingTransmitter&&) noexcept=default;
StreamingTransmitter& StreamingTransmitter::operator=(StreamingTransmitter&&) noexcept=default;
void StreamingTransmitter::request_finish() { if(impl_->noise)impl_->noise->request_finish();else impl_->pattern->request_finish(); }
std::uint64_t StreamingTransmitter::original_total_samples()const {
    return impl_->noise?impl_->noise->original_total_samples():impl_->pattern->original_total_samples();
}
std::uint64_t StreamingTransmitter::original_source_total_samples()const {
    return impl_->noise?impl_->noise->original_source_total_samples():impl_->pattern->original_source_total_samples();
}
std::uint64_t StreamingTransmitter::physical_output_origin()const {
    return impl_->noise?impl_->noise->physical_output_origin():impl_->pattern->physical_output_origin();
}
std::uint64_t StreamingTransmitter::physical_valid_payload_end()const {
    return impl_->noise?impl_->noise->physical_valid_payload_end():impl_->pattern->physical_valid_payload_end();
}
std::uint64_t StreamingTransmitter::predicted_physical_valid_payload_end(std::uint64_t frontier)const {
    return impl_->noise?impl_->noise->predicted_physical_valid_payload_end(frontier):impl_->pattern->predicted_physical_valid_payload_end(frontier);
}
std::uint64_t StreamingTransmitter::predicted_physical_stop_end(std::uint64_t frontier)const {
    return impl_->noise?impl_->noise->predicted_physical_stop_end(frontier):impl_->pattern->predicted_physical_stop_end(frontier);
}
std::uint64_t StreamingTransmitter::source_total_samples()const {
    return impl_->noise?impl_->noise->source_total_samples():impl_->pattern->source_total_samples();
}
std::uint64_t StreamingTransmitter::source_samples_emitted()const {
    return impl_->noise?impl_->noise->source_samples_emitted():impl_->pattern->source_samples_emitted();
}
std::uint64_t StreamingTransmitter::source_valid_payload_end()const {
    return impl_->noise?impl_->noise->source_valid_payload_end():impl_->pattern->source_valid_payload_end();
}
std::uint64_t StreamingTransmitter::emitted_payload_epoch()const {
    return impl_->noise?impl_->noise->emitted_payload_epoch():impl_->pattern->emitted_payload_epoch();
}
bool StreamingTransmitter::finished()const{return impl_->noise?impl_->noise->finished():impl_->pattern->finished();}
std::uint64_t StreamingTransmitter::total_samples()const{return impl_->noise?impl_->noise->total_samples():impl_->pattern->total_samples();}
std::uint64_t StreamingTransmitter::samples_emitted()const{return impl_->noise?impl_->noise->samples_emitted():impl_->pattern->samples_emitted();}
std::size_t StreamingTransmitter::working_bytes()const{return sizeof(StreamingTransmitter)+sizeof(Impl)+
    impl_->constellation.capacity()*sizeof(Complex)+(impl_->noise?impl_->noise->working_bytes():impl_->pattern->working_bytes())+
    impl_->trace.working_bytes();}
std::vector<Complex> StreamingTransmitter::payload_constellation()const {
    const auto& s=*impl_;std::vector<Complex> result;result.reserve(s.constellation_count);
    for(std::size_t i=0;i<s.constellation_count;++i)result.push_back(s.constellation[(s.constellation_begin+i)%s.constellation.size()]);
    return result;
}
ConstellationBatch StreamingTransmitter::take_payload_constellation() {
    auto& s=*impl_;ConstellationBatch result;result.points.reserve(s.pending_count);result.dropped=s.dropped;
    for(std::size_t i=s.constellation_count-s.pending_count;i<s.constellation_count;++i)
        result.points.push_back(s.constellation[(s.constellation_begin+i)%s.constellation.size()]);
    s.pending_count=0;s.dropped=0;return result;
}
TransmitTrace StreamingTransmitter::transmit_trace()const {
    auto result=impl_->trace;
    const auto count=std::min(result.generated_bits,TransmitTrace::bit_limit);
    for(auto* bits:{&result.wire_plain_bits,&result.wire_bits,&result.data_key_bits})
        if(bits->size()>count)bits->resize(count);
    if(impl_->pattern)impl_->pattern->copy_transmit_trace(result,result.generated_chips);
    else if(impl_->noise)impl_->noise->copy_transmit_trace(result,result.generated_chips);
    return result;
}
std::size_t StreamingTransmitter::read(std::span<float> output,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);
    const auto observer=[&](Complex point){s.retain_constellation(point);};
    return s.noise?s.noise->read(output,stop,observer):s.pattern->read(output,stop,observer);
}
std::size_t StreamingTransmitter::read_analytic(std::span<Complex> output,std::stop_token stop) {
    auto& s=*impl_;cancelled(stop);
    const auto observer=[&](Complex point){s.retain_constellation(point);};
    return s.noise?s.noise->read_analytic(output,stop,observer):s.pattern->read_analytic(output,stop,observer);
}
std::optional<SymbolObservation> StreamingTransmitter::next_symbol(std::stop_token stop) {
    cancelled(stop);
    throw Error("pattern reception requires physical PCM or analytic samples, not despread symbol observations");
}
void StreamingTransmitter::preview_last(std::span<float> output)const {
    if(output.size()>2048)throw Error("streaming preview is limited to 2048 samples");
    std::array<Complex,2048> analytic{};preview_last_analytic(std::span(analytic).first(output.size()));
    for(std::size_t i=0;i<output.size();++i)output[i]=static_cast<float>(analytic[i].real());
}
void StreamingTransmitter::preview_last_analytic(std::span<Complex> output)const {
    if(output.size()>analytic_preview_limit)throw Error("analytic streaming preview exceeds its bounded history");
    if(impl_->noise)impl_->noise->preview_last_analytic(output);
    else impl_->pattern->preview_last_analytic(output);
}

struct StreamingReceiver::Impl {
    static Config dsp_config(Config config,std::size_t budget) {
        validate(config);
        if(budget<=sizeof(Impl))throw Error("pattern receiver workspace is too small");
        config.memory_limit=budget-sizeof(Impl);
        return config;
    }
    Config config;
    PatternSearch search;
    std::size_t workspace;
    PatternReceiver pattern;
    Impl(Config c,std::size_t budget,PatternSearch value):config(dsp_config(c,budget)),search(std::move(value)),workspace(budget),
        pattern(config,budget-sizeof(Impl),search) {
        validate(c);
        if(budget<=sizeof(Impl))throw Error("pattern receiver workspace is too small");
    }
};
StreamingReceiver::StreamingReceiver(Config c,std::size_t workspace,PatternSearch search):impl_(std::make_unique<Impl>(c,workspace,std::move(search))){}
StreamingReceiver::~StreamingReceiver()=default;
StreamingReceiver::StreamingReceiver(StreamingReceiver&&) noexcept=default;
StreamingReceiver& StreamingReceiver::operator=(StreamingReceiver&&) noexcept=default;
bool StreamingReceiver::try_skip_unobserved(std::uint64_t samples,std::stop_token stop) {
    return impl_->pattern.try_skip_unobserved(samples,stop);
}
std::optional<std::uint64_t> StreamingReceiver::unobserved_end_from(std::uint64_t local_sample)const {
    return impl_->pattern.unobserved_end_from(local_sample);
}
std::optional<ScheduledCaptureEnvelope> StreamingReceiver::scheduled_capture_envelope(std::uint64_t local_sample)const {
    return impl_->pattern.scheduled_capture_envelope(local_sample);
}
void StreamingReceiver::suspend_fresh_acquisition(){impl_->pattern.suspend_fresh_acquisition();}
bool StreamingReceiver::symbol_burst_tracking_active()const{return impl_->pattern.symbol_burst_tracking_active();}
bool StreamingReceiver::synchronized()const{return impl_->pattern.synchronized();}
bool StreamingReceiver::initial_search_complete()const{return impl_->pattern.initial_search_complete();}
std::uint64_t StreamingReceiver::initial_fft_input_samples()const noexcept{return impl_->pattern.initial_fft_input_samples();}
bool StreamingReceiver::clock_windowed()const{return impl_->pattern.clock_windowed();}
bool StreamingReceiver::local_clock_fallback()const{return impl_->pattern.local_clock_fallback();}
bool StreamingReceiver::candidate_limited()const{return impl_->pattern.candidate_limited();}
bool StreamingReceiver::acquiring()const{return impl_->pattern.acquiring();}
Diagnostics StreamingReceiver::diagnostics()const{return impl_->pattern.diagnostics();}
ConstellationBatch StreamingReceiver::take_payload_constellation(){return {impl_->pattern.take_chip_constellation(),0};}
std::size_t StreamingReceiver::working_bytes()const{return sizeof(StreamingReceiver)+sizeof(Impl)+impl_->pattern.working_bytes();}
std::size_t StreamingReceiver::reserved_workspace_bytes()const{return sizeof(StreamingReceiver)+sizeof(Impl)+impl_->pattern.reserved_workspace_bytes();}
std::optional<HelperReplayBudget> StreamingReceiver::helper_replay_budget(std::size_t future_samples,
        const PatternSearch* reset_search)const {
    auto result=impl_->pattern.helper_replay_budget(future_samples,reset_search);
    if(result) {
        const auto control=sizeof(StreamingReceiver)+sizeof(Impl);
        if(result->workspace_bytes>SIZE_MAX-control)return std::nullopt;
        result->workspace_bytes+=control;
        if(result->steady_workspace_bytes) {
            if(result->steady_workspace_bytes>SIZE_MAX-control)return std::nullopt;
            result->steady_workspace_bytes+=control;
        }
        if(result->cold_constructor_bytes) {
            // Impl passes constructor budget-sizeof(Impl) to PatternReceiver.
            // The outer StreamingReceiver shell is actual storage only.
            if(result->cold_constructor_bytes>SIZE_MAX-sizeof(Impl))return std::nullopt;
            result->cold_constructor_bytes+=sizeof(Impl);
        }
    }
    return result;
}
std::size_t StreamingReceiver::projection_cache_headroom(std::size_t input_samples)const{return impl_->pattern.projection_cache_headroom(input_samples);}
std::optional<std::size_t> StreamingReceiver::temporal_plan_workers()const{return impl_->pattern.temporal_plan_workers();}
void StreamingReceiver::trim_optional_workspace(){impl_->pattern.trim_optional_workspace();}
detail::NativePeakLease::NativePeakLease(detail::NativePeakWorkspace* workspace,StreamingReceiver& receiver)
    :NativePeakLease(workspace,receiver.impl_->pattern) {}
bool detail::NativePeakLease::adopt(detail::NativePeakWorkspace& workspace,StreamingReceiver& receiver) {
    return adopt(workspace,receiver.impl_->pattern);
}
detail::NativePointLease::NativePointLease(detail::NativePointPool* pool,detail::NativePointOwner& owner,StreamingReceiver& receiver)
    :NativePointLease(pool,owner,receiver.impl_->pattern) {}
bool detail::NativePointLease::try_adopt(detail::NativePointPool& pool,detail::NativePointOwner& owner,StreamingReceiver& receiver) {
    return try_adopt(pool,owner,receiver.impl_->pattern);
}
std::size_t detail::NativePointLease::allocation_headroom(const detail::NativePointPool& pool,const detail::NativePointOwner& owner,const StreamingReceiver& receiver) {
    return allocation_headroom(pool,owner,receiver.impl_->pattern);
}
std::optional<detail::NativeDiagnosticHistoryRequirements>
detail::NativeDiagnosticHistoryAccess::requirements(const StreamingReceiver& receiver,std::size_t limit) {
    return requirements(receiver.impl_->pattern,limit);
}
bool detail::NativeDiagnosticHistoryAccess::bind(StreamingReceiver& receiver,std::size_t limit,std::size_t available_growth_bytes) {
    return bind(receiver.impl_->pattern,limit,available_growth_bytes);
}
detail::NativeDiagnosticHistoryAccounting detail::NativeDiagnosticHistoryAccess::accounting(const StreamingReceiver& receiver)noexcept {
    return accounting(receiver.impl_->pattern);
}
void StreamingReceiver::set_workspace_bytes(std::size_t bytes) {
    if(bytes<working_bytes())throw Error("DSP workspace is smaller than retained receiver history");
    impl_->pattern.set_workspace_bytes(bytes-sizeof(Impl));impl_->workspace=bytes;
}
void StreamingReceiver::reset(){auto& s=*impl_;auto fresh=std::make_unique<Impl>(s.config,s.workspace,s.search);impl_=std::move(fresh);}
Bytes StreamingReceiver::push_symbols(std::span<const SymbolObservation>,std::stop_token stop) {
    cancelled(stop);throw Error("pattern acquisition requires measured PCM, not despread symbol observations");
}
Bytes StreamingReceiver::push(std::span<const float> samples,std::stop_token stop){impl_->pattern.push(samples,stop);return {};}
PatternAcquisitionState StreamingReceiver::acquisition_state()const noexcept{return impl_->pattern.acquisition_state();}
std::span<const PatternFrequencyRateHypothesis> StreamingReceiver::acquisition_hypotheses()const noexcept {
    return impl_->pattern.acquisition_hypotheses();
}
bool StreamingReceiver::set_acquisition_prior(double origin,std::span<const std::uint8_t> mask) {
    return impl_->pattern.set_acquisition_prior(origin,mask);
}
bool StreamingReceiver::clear_acquisition_prior(){return impl_->pattern.clear_acquisition_prior();}
bool StreamingReceiver::set_helper_gap_protection(bool qualified)noexcept {
    return impl_->pattern.set_helper_gap_protection(qualified);
}
std::size_t StreamingReceiver::observe_acquisition(std::span<const float> samples,std::stop_token stop){
    return impl_->pattern.observe_acquisition(samples,stop);
}
void StreamingReceiver::process_acquisition(std::stop_token stop){impl_->pattern.process_acquisition(stop);}
bool StreamingReceiver::set_acquisition_phase_view(const PatternPhaseCurve* curve)noexcept{return impl_->pattern.set_acquisition_phase_view(curve);}
bool StreamingReceiver::acquisition_phase_view_active()const noexcept{return impl_->pattern.acquisition_phase_view_active();}
bool StreamingReceiver::process_prior_acquisition(std::stop_token stop){return impl_->pattern.process_prior_acquisition(stop);}
void StreamingReceiver::resolve_acquisition(bool conditional_link,std::stop_token stop){impl_->pattern.resolve_acquisition(conditional_link,stop);}
PatternFftWork StreamingReceiver::fft_work()const{return impl_->pattern.fft_work();}
std::optional<PatternSourceIdentity> StreamingReceiver::source_identity(const PatternBurst& burst)const {
    return impl_->pattern.source_identity(burst);
}
bool StreamingReceiver::phase_assistance_supported()const noexcept{return impl_->pattern.phase_assistance_supported();}
std::size_t StreamingReceiver::phase_assistance_bin_samples()const noexcept{return impl_->pattern.phase_assistance_bin_samples();}
bool StreamingReceiver::suspend_helper_acquisition(){return impl_->pattern.suspend_helper_acquisition();}
bool StreamingReceiver::push_phase_assisted(std::span<const float> samples,std::span<const PatternPhaseKnot> knots,
        std::uint64_t maximum_gap_samples,std::stop_token stop) {
    return impl_->pattern.push_phase_assisted(samples,knots,maximum_gap_samples,stop);
}
Bytes StreamingReceiver::push(std::span<const float> samples,std::span<const Complex> projected,std::stop_token stop){impl_->pattern.push(samples,projected,stop);return {};}
Bytes StreamingReceiver::push(std::span<const float> samples,std::span<const Complex> projected,std::stop_token stop,
                              detail::CorrelationProjectionCache* cache){impl_->pattern.push(samples,projected,stop,cache);return {};}
PatternBurst StreamingReceiver::provisional_pattern()const{return impl_->pattern.provisional();}
std::vector<PatternBurst> StreamingReceiver::take_pattern_bursts(){return impl_->pattern.take_bursts();}
bool StreamingReceiver::retains_pattern_stream(std::uint64_t first_sample,std::uint64_t first_symbol)const {
    return impl_->pattern.retains_stream(first_sample,first_symbol);
}
void StreamingReceiver::process_pending(std::stop_token stop){impl_->pattern.process_pending(stop);}
std::uint64_t StreamingReceiver::diagnostic_history_generation()const noexcept{return impl_->pattern.diagnostic_history_generation();}
std::uint64_t StreamingReceiver::diagnostic_history_resolved_through()const noexcept{return impl_->pattern.diagnostic_history_resolved_through();}
std::uint64_t StreamingReceiver::oldest_unresolved_diagnostic_event()const noexcept{return impl_->pattern.oldest_unresolved_diagnostic_event();}
bool StreamingReceiver::deferred_pattern_history()const noexcept{return impl_->pattern.deferred_pattern_history();}
std::optional<std::uint64_t> StreamingReceiver::delayed_processing_frontier()const noexcept{return impl_->pattern.delayed_processing_frontier();}
std::vector<PatternEvidence> StreamingReceiver::pattern_candidates()const{return impl_->pattern.candidates();}
std::vector<PatternEvidence> StreamingReceiver::pattern_candidates(std::size_t limit,std::stop_token stop)const {
    return impl_->pattern.candidates(limit,stop);
}
std::vector<PatternEvidence> StreamingReceiver::pattern_candidates(std::size_t limit)const{return impl_->pattern.candidates(limit);}
Bytes StreamingReceiver::finish(std::stop_token stop){impl_->pattern.finish(stop);return {};}

SymbolObservation add_awgn(SymbolObservation observation,double sample_snr_db,std::mt19937_64& random) {
    if(!observation.sample_count || !std::isfinite(sample_snr_db) || sample_snr_db < -300 || sample_snr_db > 300)throw Error("invalid integrated AWGN configuration");
    const auto sigma=std::sqrt(2*nominal_signal_power*std::pow(10.,-sample_snr_db/10)/static_cast<double>(observation.sample_count));
    std::normal_distribution<double> normal(0,sigma);
    observation.value+=Complex{normal(random),normal(random)};return observation;
}
}

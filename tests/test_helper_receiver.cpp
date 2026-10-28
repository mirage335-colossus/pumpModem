#include "datapump/helper_receiver.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/helper_filter.hpp"
#include "datapump/resampler.hpp"
#include "../src/punctuation_clock.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <barrier>
#include <exception>
#include <thread>
#include <source_location>
#include <stdexcept>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <random>
#include <vector>
#include <string_view>

using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const std::source_location where=std::source_location::current()) {
    if(!condition)throw std::runtime_error(std::string(where.file_name())+":"+std::to_string(where.line())+" failed");
}
struct Result {std::vector<helper::Reference> updates;};
// Isolated auxiliary component, extracted from the actual two-tone Generator.
// The normalization preserves the historical component's amplitude/noise
// fixtures; this is explicitly not composite-pilot isolation coverage.
class AuxiliaryFixture {
    helper::Generator composite_,pilot_;
    double frequency_;
    bool paired_,legacy_metadata_;
public:
    AuxiliaryFixture(helper::Options options,double frequency,double amplitude,double data_gain,
                     std::uint64_t epoch,bool punctuate=true,std::optional<Crypto> key={},bool paired=false,
                     bool legacy_metadata=false)
        :composite_(options,{frequency+helper::auxiliary_pilot_spacing_hz,amplitude,data_gain,options.power_w,frequency},
                    epoch,punctuate&&!paired,key),
         pilot_(options,{frequency+helper::auxiliary_pilot_spacing_hz,amplitude,data_gain,options.power_w},
                epoch,false,key),frequency_(frequency),paired_(paired),legacy_metadata_(paired||legacy_metadata) {}
    std::complex<double> analytic(long double time,std::uint32_t rate) const {
        const auto extract=[&](long double at) {
            auto value=(composite_.mix_analytic({},at,rate)-pilot_.mix_analytic({},at,rate))/
                helper::auxiliary_amplitude_fraction;
            if(legacy_metadata_) {
                const auto local=std::fmod(at,.4L),edge=std::min(local,.4L-local);
                const auto smooth=[](long double position) {
                    const auto u=static_cast<double>(std::clamp(position,0.L,1.L));
                    return u*u*u*(10+u*(-15+6*u));
                };
                const auto gate=smooth(edge/.008L);
                if(gate>0)value*=smooth(edge/.004L)/gate;
            }
            return value;
        };
        auto value=extract(time);
        const auto local=std::fmod(time,.4L);
        // Retain literal C2 legacy fixtures while the production Generator
        // uses C3. Read its unchanged symbol endpoints, then interpolate them
        // independently on the original8ms metadata grid.
        if(legacy_metadata_&&local>=.332L&&local<.396L) {
            const auto position=(local-.332L)/.008L,index=std::floor(position);
            const auto u=static_cast<double>(position-index),h=u*u*u*(10+u*(-15+6*u));
            const auto begin=time-local+.332L+index*.008L,end=begin+.008L;
            const auto oscillator=[&](long double at) {
                return std::polar(1.,tau*static_cast<double>(std::fmod(frequency_*at,1.L)));
            };
            value=((1-h)*extract(begin)/oscillator(begin)+h*extract(end)/oscillator(end))*oscillator(time);
        }
        if(paired_) {
            const auto first=static_cast<double>(std::clamp((local-.236L)/.008L,0.L,1.L));
            const auto u=static_cast<double>(std::clamp((local-.276L)/.008L,0.L,1.L));
            value*=(1-2*first*first*first*(10+first*(-15+6*first)))*
                (1-2*u*u*u*(10+u*(-15+6*u)));
        }
        return value;
    }
    float mix(long double time,std::uint32_t rate) const {return static_cast<float>(analytic(time,rate).real());}
};
Result run(bool punctuation,std::string callsign,double frequency,double phase,double delay,
           std::size_t chunk,double noise=0,bool quantize=false,bool timecode=false,
           std::optional<Crypto> transmit_key={},std::vector<Crypto> keys={},double ppm=0,
           bool corrupt_metadata=false,std::uint64_t epoch=1700000000,
           std::optional<long double> repeated_dwell={},helper::FrequencyPrecisionOptions precision={}) {
    constexpr std::uint32_t rate=48000;
    helper::Options options;options.power_w=100;options.callsign=std::move(callsign);options.timecode=timecode;
    AuxiliaryFixture generator(options,frequency,.7,.1,epoch,punctuation,std::move(transmit_key));
    helper::Receiver receiver(2100,20,std::move(keys),std::nullopt,precision,
        helper::PunctuationFormat::auxiliary_single);
    Result out;std::vector<float> samples(chunk);
    std::mt19937 rng(519);std::normal_distribution<double> gaussian(0,noise);
    const auto count=static_cast<std::uint64_t>(3.2*rate);
    for(std::uint64_t first=0;first<count;first+=chunk) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(chunk,count-first));
        for(std::size_t j=0;j<n;++j) {
            const auto time=static_cast<long double>(first+j)/rate*(1+ppm*1e-6)-delay;
            const auto generator_time=repeated_dwell?*repeated_dwell+std::fmod(std::max(0.L,time),.4L):time;
            auto x=time<0?0.:std::real(generator.analytic(generator_time,rate)*std::polar(1.,phase));
            x+=gaussian(rng);
            if(corrupt_metadata&&std::fmod(std::max(0.L,time),.4L)>.316L)
                x+=.005*std::cos(tau*2173*static_cast<double>(time));
            if(quantize)x=std::round(x*32767)/32767;
            samples[j]=static_cast<float>(x);
        }
        receiver.push(std::span<const float>(samples).first(n),rate,first);
        if(auto update=receiver.take_update()) {
            check(update->sample<first+n);
            check((update->frequency_sample?update->frequency_sample:update->sample)<=update->sample);
            out.updates.push_back(std::move(*update));
        }
    }
    return out;
}
void metadata_has_no_timing_authority(const Result& result) {
    for(std::size_t i=0;i<result.updates.size();++i)if(result.updates[i].metadata) {
        const auto& publication=result.updates[i];
        if(!publication.timing_valid) {
            check(!publication.frequency_valid&&!publication.phase_valid&&
                !publication.phase_observation_known&&!publication.punctuation.known);
            continue; // Metadata-only per-dwell processing exposes no authority.
        }
        bool compared=false;
        for(std::size_t previous=i;previous>0;--previous) {
            const auto& tone=result.updates[previous-1];
            if(tone.metadata||!tone.timing_valid)continue;
            check(std::abs(publication.dwell_start_seconds-tone.dwell_start_seconds)<1e-12);
            check(publication.dwell_uncertainty_seconds==tone.dwell_uncertainty_seconds);
            compared=true;break;
        }
        check(compared);
    }
}
void clean_precision() {
    const auto result=run(true,"",2100.37,.81,.03719,137);
    check(!result.updates.empty());
    std::size_t timed=0;
    for(const auto& r:result.updates) {
        check(r.frequency_valid&&r.phase_valid);
        if(r.sample<48000)continue;
        check(std::abs(r.frequency_hz-2100.37)<.001);
        if(r.timing_valid) {
            ++timed;
            check(std::abs(std::remainder(r.dwell_start_seconds-.03719,.4))<.00015);
            const auto physical=static_cast<double>(r.sample)/r.sample_rate-.03719;
            const auto expected=tau*2100.37*physical+.81;
            check(std::abs(std::remainder(r.phase_radians-expected,tau))<.001);
        }
    }
    check(timed>5);
}
void paired_marker_identity_and_missing_return() {
    constexpr std::uint32_t rate=48000;
    constexpr double frequency=2100.37,phase=.81;
    const auto smooth=[](long double x) {
        const auto t=static_cast<double>(std::clamp(x,0.L,1.L));
        return t*t*t*(10+t*(-15+6*t));
    };
    // Begin between the two edges, so the first observed return must not be
    // published as the240ms marker. Independently compose the new envelope
    // around the actual tone/compact-metadata Generator with punctuation off.
    // This is a receiver discriminator, not a second metadata implementation.
    for(const unsigned shape:{0U,1U,2U})for(const auto chunk:{137U,4096U}) {
        const bool lose_return=shape==1,return_only=shape==2;
        constexpr long double source_origin=.260L;
        helper::Options options;options.power_w=100;options.callsign="N0CALL";
        AuxiliaryFixture source(options,frequency,.7,.1,1800000000,false,{},false,true);
        helper::Receiver receiver(frequency,20);
        std::vector<float> samples(chunk);
        std::size_t frequencies=0,known=0,metadata=0,timed=0,late_frequencies=0;
        const auto total=static_cast<std::uint64_t>(3.2*rate);
        for(std::uint64_t first=0;first<total;first+=samples.size()) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),total-first));
            for(std::size_t i=0;i<count;++i) {
                const auto time=source_origin+static_cast<long double>(first+i)/rate;
                const auto local=std::fmod(time,.4L);
                const auto first_edge=return_only?-1.:1-2*smooth((local-.236L)/.008L);
                // A missing companion after warmup preserves the first-edge
                // train and dwell notches. It must not make negative metadata
                // or a post-return raw fit into canonical positive evidence.
                const auto second_edge=lose_return&&time>=2.L?0.:2*smooth((local-.276L)/.008L);
                const auto envelope=first_edge+second_edge;
                const auto value=source.analytic(time,rate)*envelope*std::polar(1.,phase);
                samples[i]=static_cast<float>(std::round(value.real()*32767)/32767);
            }
            receiver.push(std::span(samples).first(count),rate,first);
            if(const auto update=receiver.take_update()) {
                ++frequencies;check(update->frequency_valid);
                const auto time=source_origin+static_cast<long double>(update->sample)/rate;
                const auto local=std::fmod(time,.4L);
                check(std::abs(update->frequency_hz-frequency)<.002);
                if(time>=2.L)++late_frequencies;
                if(update->punctuation.known) {
                    ++known;const auto& marker=update->punctuation;
                    const auto at=source_origin+(static_cast<long double>(marker.sample)+marker.fraction)/rate;
                    check(std::abs(std::remainder(at-.24L,.4L))<.00015L);
                    check(marker.first_sample<=marker.sample && marker.last_sample<=update->sample);
                }
                if(update->timing_valid)++timed;
                if(update->timing_valid && update->phase_valid && !update->metadata) {
                    // The actual canonical carrier phase is continuous across
                    // both punctuation edges. Retained pre-edge observations
                    // remain usable when optional post-return evidence fails.
                    check(std::abs(std::remainder(update->phase_radians-
                        tau*frequency*static_cast<double>(time)-phase,tau))<.001);
                    if(lose_return && time>=2.L)check(!(local>.284L && local<.332L));
                }
                if(update->metadata) {
                    ++metadata;check(update->metadata->callsign=="N0CALL");
                    check(!lose_return || time<2.L);
                }
            }
        }
        check(frequencies>10 && late_frequencies>5);
        if(return_only)check(!known && !timed && !metadata);
        else check(known>5 && metadata>0 && timed>5);
    }
}

void metadata() {
    // Includes a one-symbol ID, a two-character ID without spare CRC bits,
    // a normal callsign and the seven-symbol maximum.
    for(const auto callsign:{"K","AB","N0CALL/TEST","ABCDEFGHIJKLMNOPQRS"}) {
        const auto result=run(true,callsign,2100.37,.81,.03719,137,0,true);
        bool found=false;
        for(const auto& r:result.updates)if(r.metadata) {
            check(r.metadata->kind==helper::MetadataKind::callsign);
            check(r.metadata->callsign==callsign);check(!r.metadata->authenticated);found=true;
        }
        check(found);metadata_has_no_timing_authority(result);
    }
}
void metadata_carrier_images() {
    // The finite real-carrier projection of a reversal depends on carrier
    // phase. A scalar bin-envelope average either rejected these clean tones
    // or biased the dwell enough to move a metadata point by one radial cell.
    struct Case {std::uint32_t rate;double frequency,phase;};
    for(const auto test:{Case{32000,2450,0},Case{32000,2262.5,.81},
                         Case{48000,2262.5,2.2},Case{48000,2462.5,.81}}) {
        helper::Options options;options.power_w=1500;options.callsign="N0CALL";
        AuxiliaryFixture generator(options,test.frequency,.7,.1,1800000000,true);
        helper::Receiver receiver(test.frequency,20,{},std::nullopt,{},
            helper::PunctuationFormat::auxiliary_single);
        constexpr double delay=.03719;
        std::array<float,512> samples{};Result result;
        const auto total=static_cast<std::uint64_t>(3.2*test.rate);
        for(std::uint64_t first=0;first<total;first+=samples.size()) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),total-first));
            for(std::size_t i=0;i<count;++i) {
                const auto time=static_cast<long double>(first+i)/test.rate-delay;
                const auto value=time<0?0.:std::real(generator.analytic(time,test.rate)*
                    std::polar(1.,test.phase));
                samples[i]=static_cast<float>(std::round(value*32767)/32767);
            }
            receiver.push(std::span<const float>(samples).first(count),test.rate,first);
            if(auto update=receiver.take_update()) {
                check(update->sample<first+count);
                result.updates.push_back(std::move(*update));
            }
        }
        bool decoded=false;
        for(const auto& reference:result.updates)if(reference.metadata) {
            check(reference.timing_valid);
            check(std::abs(std::remainder(reference.dwell_start_seconds-delay,.4))<2e-6);
            check(reference.metadata->kind==helper::MetadataKind::callsign);
            check(reference.metadata->callsign=="N0CALL");decoded=true;
        }
        check(decoded);metadata_has_no_timing_authority(result);
    }
}
void two_tone_auxiliary_metadata() {
    constexpr std::uint32_t rate=48000;
    constexpr double expected_auxiliary=2100,expected_pilot=expected_auxiliary+helper::auxiliary_pilot_spacing_hz;
    helper::Options options;options.power_w=100;options.callsign="N0CALL";options.timecode=false;
    const auto required=helper::AuxiliaryMetadataReceiver::workspace_required(rate);
    check(required<512*1024);
    // The second prepaid0.84s ring increases explicit optional storage; a
    // default512KiB owner may decline96kHz without affecting the pilot.
    check(helper::AuxiliaryMetadataReceiver::workspace_required(96000)>512*1024);
    check(helper::AuxiliaryMetadataReceiver::workspace_required(96000)<1024*1024);
    bool declined=false;
    try {helper::AuxiliaryMetadataReceiver unfunded(rate,expected_auxiliary,expected_pilot,{}, {},required-1);}
    catch(const Error&) {declined=true;}
    check(declined);
    std::array<double,16> phases{};std::mt19937 random(8127);
    std::uniform_real_distribution<double> phase(-std::numbers::pi,std::numbers::pi);
    for(auto& value:phases)value=phase(random);
    struct Geometry {double expected,actual,delay,clock_ppm=0;};
    // Exact dwell/bin alignment previously hid the ramp in the diagnostic
    // notch's carrier image, although the raw ramp and reversal were clean.
    for(const auto geometry:{Geometry{2100,2100.37,.03719},Geometry{2100,2100.37,0},
                             Geometry{5762.5,5762.5,.03719},Geometry{5762.5,5762.5,0},
                             Geometry{5762.5,5762.5,.03719,-100},Geometry{5762.5,5762.5,.03719,100}}) {
    const auto auxiliary=geometry.actual,pilot=auxiliary+helper::auxiliary_pilot_spacing_hz;
    // Scale the actual source clock, including punctuation and metadata, while
    // retaining the nominal discovery centers. No known clock or tone truth
    // is supplied to the receiver; its measured two-tone spacing must suffice.
    const auto clock_ppm=geometry.clock_ppm;
    const auto clock_scale=1+clock_ppm*1e-6;
    for(const bool phase_jumps:{false,true}) {
    // Preserve the original eight cases; two additional clock cases exercise
    // both signs with unknown per-dwell phase without a Cartesian expansion.
    if(clock_ppm!=0&&!phase_jumps)continue;
    helper::Generator generator(options,{pilot,.7,.1,100,auxiliary},1800000000,true);
    helper::AuxiliaryMetadataReceiver receiver(rate,geometry.expected,
        geometry.expected+helper::auxiliary_pilot_spacing_hz,{}, {},required);
    std::array<float,137> input{};Result result;
    const auto total=static_cast<std::uint64_t>(3.2*rate);
    for(std::uint64_t first=0;first<total;first+=input.size()) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(input.size(),total-first));
        for(std::size_t i=0;i<count;++i) {
            const auto time=static_cast<long double>(first+i)/rate*clock_scale-geometry.delay;
            const auto dwell=static_cast<std::size_t>(std::floor(std::max(0.L,time)/.4L));
            input[i]=time<0?0:static_cast<float>(std::real(generator.mix_analytic({},time,rate)*
                std::polar(1.,phase_jumps?phases[dwell%phases.size()]:0.)));
        }
        receiver.push(std::span(input).first(count),first);receiver.advance();
        if(auto update=receiver.take_update()) {
            check(update->sample<first+count);
            check(update->phase_observation_sample<=update->sample);
            check(update->punctuation.last_sample<=update->sample);
            check(!update->frequency_valid&&!update->phase_valid&&!update->timing_valid);
            result.updates.push_back(std::move(*update));
        }
    }
    std::size_t decoded=0;std::int64_t last_decoded_dwell=-1;
    for(const auto& update:result.updates)if(update.metadata) {
        check(update.metadata->kind==helper::MetadataKind::callsign);
        check(update.metadata->callsign=="N0CALL"&&!update.metadata->authenticated);
        check(std::abs(update.frequency_hz-auxiliary*clock_scale)<.002);
        const auto source_dwell=std::llround((update.dwell_start_seconds*clock_scale-geometry.delay)/.4);
        check(source_dwell>last_decoded_dwell);last_decoded_dwell=source_dwell;
        ++decoded;
    }
    std::cout<<"two-tone auxiliary frequency="<<auxiliary<<" delay="<<geometry.delay
        <<" clock_ppm="<<clock_ppm<<" phase_jumps="<<phase_jumps<<" references="<<result.updates.size()
        <<" metadata="<<decoded<<" workspace_grant="<<required<<'\n';
    check(decoded>0);metadata_has_no_timing_authority(result);
    }
    }
}

void metadata_frozen_work() {
    constexpr std::uint32_t rate=48000;
    constexpr double auxiliary=5762.5,pilot=auxiliary+helper::auxiliary_pilot_spacing_hz;
    helper::Options options;options.power_w=100;options.callsign="N0CALL";options.timecode=false;
    helper::Generator generator(options,{pilot,.7,.1,100,auxiliary},1800000000,true);
    std::vector<float> source(static_cast<std::size_t>(rate)*84/100),overwrite(2*rate);
    for(std::size_t i=0;i<source.size();++i)
        source[i]=static_cast<float>(generator.mix_analytic({},static_cast<long double>(i)/rate,rate).real());
    const auto required=helper::AuxiliaryMetadataReceiver::workspace_required(rate);
    for(const bool reset_before_commit:{false,true}) {
        helper::AuxiliaryMetadataReceiver receiver(rate,auxiliary,pilot,{}, {},required);
        receiver.push(source,0);
        check(!receiver.take_update()&&!receiver.advance(4096)&&!receiver.commit());
        check(receiver.prepare()&&receiver.pending_samples()==source.size());
        std::barrier begin(2);std::exception_ptr failure;std::size_t chunks=0;
        std::jthread worker([&] {
            try {
                begin.arrive_and_wait();
                while(receiver.pending_samples()) {
                    const auto pending=receiver.pending_samples();
                    check(receiver.advance(4096));++chunks;
                    check(pending-receiver.pending_samples()<=4096);
                }
            } catch(...) {failure=std::current_exception();}
        });
        begin.arrive_and_wait();
        // Overwrite the entire mutable ring while the worker reads its old,
        // paid snapshot. The resulting callsign must still use those dates.
        receiver.push(overwrite,source.size());worker.join();
        if(failure)std::rethrow_exception(failure);
        check(!receiver.take_update()&&!receiver.take_punctuation_update().known);
        check(chunks==(source.size()+4095)/4096);
        constexpr std::uint64_t restart=200000;
        if(reset_before_commit)receiver.reset(restart);
        check(receiver.commit()!=reset_before_commit);
        const auto update=receiver.take_update();
        const auto punctuation=receiver.take_punctuation_update();
        if(reset_before_commit)check(!update&&!punctuation.known);
        else {
            check(update&&update->metadata&&update->metadata->callsign=="N0CALL");
            check(update->sample<source.size());
            check(punctuation.known&&punctuation.last_sample<source.size());
            check(!update->frequency_valid&&!update->phase_valid&&!update->timing_valid&&
                  !update->phase_observation_known&&!update->punctuation.known);
        }
        // A generation gap starts new causal support and allows recovery;
        // stale prepared metadata must never survive into the new capture.
        receiver.reset(restart);
        for(std::size_t first=0;first<source.size();first+=137) {
            receiver.push(std::span(source).subspan(first,std::min<std::size_t>(137,source.size()-first)),restart+first);
            receiver.advance();
        }
        const auto recovered=receiver.take_update();
        check(recovered&&recovered->metadata&&recovered->metadata->callsign=="N0CALL");
        check(recovered->sample>=restart&&recovered->sample<restart+source.size());
        check(!recovered->frequency_valid&&!recovered->phase_valid&&!recovered->timing_valid);
    }
    std::cout<<"frozen metadata snapshot/generation passed; workspace_grant="<<required<<'\n';
}

void auxiliary_punctuation_clock() {
    constexpr std::uint32_t rate=48000;
    constexpr double auxiliary=2100,pilot=auxiliary+helper::auxiliary_pilot_spacing_hz;
    helper::Options options;options.power_w=100;options.timecode=false; // No metadata to decode.
    helper::Generator generator(options,{pilot,.7,.1,100,auxiliary},1800000000,true);
    std::vector<float> source(static_cast<std::size_t>(4.8*rate));
    for(std::size_t i=0;i<source.size();++i)
        source[i]=static_cast<float>(generator.mix_analytic({},static_cast<long double>(i)/rate,rate).real());
    for(const auto ppm:{-550.,550.}) {
        audio::Resampler converter(rate,rate,true);converter.initialize_timing(0,ppm*1e-6);
        helper::AuxiliaryMetadataReceiver receiver(rate,auxiliary,pilot,{}, {},
            helper::AuxiliaryMetadataReceiver::workspace_required(rate));
        helper::PunctuationClock clock;const helper::PunctuationClockDomain domain{1,1,0,1,rate};clock.reset(domain);
        std::array<float,1031> output{};std::size_t input=0,markers=0;
        std::uint64_t captured=0;helper::PunctuationObservation first,last;
        while(!converter.finished()) {
            const auto count=std::min<std::size_t>(137,source.size()-input);
            const auto progress=converter.process(std::span(source).subspan(input,count),output,input+count==source.size());
            check(progress.consumed||progress.produced||converter.finished());input+=progress.consumed;
            if(!progress.produced)continue;
            receiver.push(std::span(output).first(progress.produced),captured);captured+=progress.produced;
            receiver.advance();check(!receiver.take_update());
            const auto marker=receiver.take_punctuation_update();if(!marker.known)continue;
            check(marker.last_sample<captured&&marker.first_sample<=marker.sample&&marker.sample<=marker.last_sample);
            check(marker.last_sample-marker.first_sample+1>=static_cast<std::uint64_t>(.09*rate));
            helper::Reference evidence;evidence.sample_rate=rate;evidence.sample=marker.last_sample;evidence.punctuation=marker;
            check(!evidence.frequency_valid&&!evidence.phase_valid&&!evidence.timing_valid);
            check(clock.observe(evidence,domain,captured)==helper::PunctuationClockReason::accepted);
            if(!markers++)first=marker;last=marker;
        }
        check(markers>=8);
        helper::PunctuationClockRequest request{first.sample+2,last.sample-2,captured,2ULL*rate,
            helper::PunctuationClockModel::constant_rate_over_requested_support,{}};
        const auto fit=clock.qualify({-600.09,600.09},request);
        std::cout<<"auxiliary actual Resampler clock="<<ppm<<"ppm markers="<<markers
            <<" measured=["<<fit.physical_audio_lower_ppm<<','<<fit.physical_audio_upper_ppm<<"] reason="
            <<helper::punctuation_clock_reason(fit.reason)<<'\n';
        check(fit.qualified_for_acquisition&&fit.physical_audio_lower_ppm<=ppm&&fit.physical_audio_upper_ppm>=ppm);
        check(fit.physical_audio_upper_ppm-fit.physical_audio_lower_ppm<100);
    }
}
void timecodes_and_corruption() {
    std::array<std::uint8_t,32> master{};
    const Crypto key(master);master[0]=1;const Crypto wrong(master);
    const auto clear=run(true,"",2100,.2,.034,137,0,true,true);
    const auto encrypted=run(true,"",2100,.2,.034,137,0,true,true,key,{key,wrong});
    for(const auto* result:{&clear,&encrypted}) {
        std::size_t decoded=0;
        for(const auto& r:result->updates)if(r.metadata) {
            ++decoded;const auto& m=*r.metadata;
            check(m.kind==(result==&clear?helper::MetadataKind::clear_time:helper::MetadataKind::encrypted_time));
            check(!m.authenticated);
            check(m.callsign.empty());
            const auto expected=1700000000.L+r.dwell_start_seconds-.034L;
            check(std::abs(static_cast<long double>(m.time_ticks)/65536-expected)<.0002);
        }
        check(decoded>0);metadata_has_no_timing_authority(*result);
    }
    // Length-preserving encryption cannot reliably reject a wrong key. It
    // never grants authenticity or time/grid authority, including when another
    // key in the vector happens to be correct: only the first key is selected.
    const auto unverified=run(true,"",2100,.2,.034,137,0,true,true,key,{wrong,key});
    bool timed=false;
    for(const auto& r:unverified.updates) {
        if(r.metadata)check(!r.metadata->authenticated);
        timed|=r.timing_valid;
    }
    check(timed);metadata_has_no_timing_authority(unverified);
    const auto no_key=run(true,"",2100,.2,.034,137,0,true,true,key);
    timed=false;
    for(const auto& r:no_key.updates) {check(!r.metadata);timed|=r.timing_valid;}
    check(timed);
    // Repeating a fixed physical dwell through the real Generator gives a
    // coherent fixture for very small timestamps after timing acquisition.
    for(const auto dwell:{0.L,.4L}) {
        const auto compact=run(true,"",2100,0,.034,137,0,true,true,{}, {},0,false,0,dwell);
        bool decoded=false;
        for(const auto& r:compact.updates)if(r.metadata) {
            check(r.metadata->kind==helper::MetadataKind::clear_time);
            check(r.metadata->time_ticks==static_cast<std::uint64_t>(std::round(dwell*65536)));
            check(!r.metadata->authenticated);decoded=true;
        }
        check(decoded);
    }
    const auto compact_encrypted=run(true,"",2100,0,.034,137,0,true,true,key,{key},0,false,0,0.L);
    bool decoded=false;
    for(const auto& r:compact_encrypted.updates)if(r.metadata) {
        check(r.metadata->kind==helper::MetadataKind::encrypted_time);
        check(r.metadata->time_ticks==0&&!r.metadata->authenticated);decoded=true;
    }
    check(decoded);
    const auto corrupted=run(true,"N0CALL",2100,.2,.034,137,0,true,false,{}, {},0,true);
    for(const auto& r:corrupted.updates)check(!r.metadata);
    helper::Receiver bounded(2100,20,std::vector<Crypto>(8,key));
}
void clock_skew() {
    const auto result=run(true,"",2100.37,.81,.03719,137,0,false,false,{}, {},50);
    bool timed=false;
    for(const auto& r:result.updates)if(r.sample>48000&&r.timing_valid) {
        timed=true;const auto time=static_cast<double>(r.sample)/r.sample_rate;
        const auto physical=time*1.00005-.03719;
        check(std::abs(r.frequency_hz-2100.37*1.00005)<.001);
        check(std::abs(std::remainder(r.phase_radians-tau*2100.37*physical-.81,tau))<.001);
        const auto boundary=(r.dwell_start_seconds*1.00005-.03719);
        check(std::abs(std::remainder(boundary,.4))<.00015);
    }
    check(timed);
}
void metadata_with_declared_process() {
    // A clean actual waveform can remain locally decodable under a much more
    // conservative declared oscillator model. Keep real PCM16 quantization
    // and deterministic additive noise in both matched controls. This does not
    // promise metadata decoding through an actual crystal-diffusion realization.
    constexpr double frequency=2100,phase=.2,delay=.034;
    constexpr double crystal_sigma=.5*std::numbers::pi/180;
    const auto observed=[&](double sigma) {
        helper::FrequencyPrecisionOptions precision;
        precision.phase_noise_radians_per_sqrt_second=sigma;
        return run(true,"",frequency,phase,delay,137,1e-6,true,true,{}, {},0,false,
            1700000000,{},precision);
    };
    const auto control=observed(0),modeled=observed(crystal_sigma);
    for(const auto* result:{&control,&modeled}) {
        std::size_t decoded=0;
        for(const auto& r:result->updates) {
            if(!r.phase_valid||!r.timing_valid)continue;
            const auto time=static_cast<double>(r.sample)/r.sample_rate-delay;
            const auto phase_error=std::abs(std::remainder(r.phase_radians-tau*frequency*time-phase,tau));
            check(phase_error<=r.phase_uncertainty_radians);
            check(std::abs(r.frequency_hz-frequency)<=r.frequency_uncertainty_hz);
            if(result==&modeled) {
                // The full public interval still contains the finite-aperture
                // process allowance even when optional metadata is decoded.
                check(r.phase_uncertainty_radians>=8*crystal_sigma*std::sqrt(.04));
                check(r.frequency_process_uncertainty_hz>0);
                check(r.frequency_uncertainty_hz>=r.frequency_process_uncertainty_hz);
            } else check(r.frequency_process_uncertainty_hz==0);
            if(!r.metadata)continue;
            ++decoded;
            check(r.metadata->kind==helper::MetadataKind::clear_time);
            check(!r.metadata->authenticated);
            const auto expected=1700000000.L+r.dwell_start_seconds-delay;
            check(std::abs(static_cast<long double>(r.metadata->time_ticks)/65536-expected)<.0002);
            if(result==&modeled) {
                check(r.phase_uncertainty_radians>.0002);
                check(r.frequency_uncertainty_hz>.002);
            }
        }
        check(decoded>0);metadata_has_no_timing_authority(*result);
    }
}
void frequency_drift() {
    for(const auto rate:{8000U,11025U,44100U,48000U})for(const auto drift:{0.,-.2,.2,-1.,1.}) {
        helper::Receiver receiver(2100,20);
        std::mt19937 rng(91);std::normal_distribution<double> noise(0,.0001);
        std::array<float,137> samples{};std::size_t accepted=0;
        for(std::uint64_t first=0;first<rate*2;first+=samples.size()) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),rate*2-first));
            for(std::size_t i=0;i<count;++i) {
                const auto time=static_cast<double>(first+i)/rate;
                samples[i]=static_cast<float>(.7*std::cos(tau*(2100.37*time+drift*time*time/2)+.81)+noise(rng));
            }
            receiver.push(std::span<const float>(samples).first(count),rate,first);
            if(const auto reference=receiver.take_update()) {
                ++accepted;check(reference->sample<first+count);
            check((reference->frequency_sample?reference->frequency_sample:reference->sample)<=reference->sample);
                const auto time=static_cast<double>(reference->sample)/rate;
                const auto phase_error=std::abs(std::remainder(reference->phase_radians-
                    tau*(2100.37*time+drift*time*time/2)-.81,tau));
                const auto frequency_error=std::abs(reference->frequency_hz-2100.37-drift*time);
                check(phase_error<=reference->phase_uncertainty_radians);
                check(frequency_error<=reference->frequency_uncertainty_hz);
            }
        }
        check(accepted>20);
    }
}
void fallback_and_noise() {
    const auto continuous=run(false,"",2100.21,.4,.017,109);
    check(!continuous.updates.empty());
    for(const auto& r:continuous.updates)check(!r.timing_valid&&!r.metadata);
    const auto modest_noise=run(false,"",2100.21,.4,.017,109,.005);
    check(!modest_noise.updates.empty());
    for(const auto& r:modest_noise.updates) {check(!r.metadata&&!r.timing_valid);check(std::abs(r.frequency_hz-2100.21)<.05);}
    const auto noisy=run(true,"N0CALL",2100.21,.4,.017,109,.2);
    for(const auto& r:noisy.updates)check(!r.metadata);
    helper::Receiver receiver(2100);
    std::mt19937 rng(91);std::normal_distribution<float> noise(0,.2);
    std::array<float,137> samples{};
    for(std::uint64_t first=0;first<100000;first+=samples.size()) {
        for(auto& sample:samples)sample=noise(rng);
        receiver.push(samples,48000,first);check(!receiver.take_update());
    }
}
// The following weak-tone fixtures explicitly fund the optional profile and
// drive its joined work seam. Their coherent Generator supplies the declared
// cross-dwell phase model; default strong Receiver fixtures remain unfunded.
class FundedFrequencyReceiver {
    helper::Receiver receiver_;
    std::uint32_t rate_;
public:
    FundedFrequencyReceiver(double expected,double width,std::uint32_t rate,std::vector<Crypto> keys={},
                            helper::FrequencyPrecisionOptions precision={})
        :receiver_(expected,width,std::move(keys),std::nullopt,precision),rate_(rate) {
        helper::FrequencyWorkOptions work;work.sample_rate=rate;work.coherent_phase_model=true;
        const auto geometry=helper::Receiver::optional_frequency_geometry(expected,width,work,precision);
        check(geometry.supported&&!geometry.eligible);
        work.workspace_grant=geometry.required_bytes;
        check(helper::Receiver::optional_frequency_geometry(expected,width,work,precision).eligible);
        check(receiver_.set_optional_frequency_work(work));
        const auto actual=receiver_.optional_frequency_work();
        check(actual.actual_bytes==work.workspace_grant&&
              actual.heap_bytes+actual.worker_scratch_bytes==actual.actual_bytes);
    }
    void push(std::span<const float> samples,std::uint32_t rate,std::uint64_t first) {
        check(rate==rate_);receiver_.push(samples,rate,first);
        if(receiver_.prepare_optional_frequency(7)) {
            while(!receiver_.advance_optional_frequency(64).complete) {}
            const auto actual=receiver_.optional_frequency_work();
            receiver_.commit_optional_frequency({7,actual.generation,0,helper::FrequencyPresenceState::unavailable});
        }
    }
    std::optional<helper::Reference> take_update() {return receiver_.take_update();}
};
struct LinkResult {
    std::size_t references=0,usable=0;
    double first_usable=0,max_error=0,max_uncertainty=0;
    std::optional<helper::Reference> last;
};
LinkResult realistic_link(double cn0_db_hz,double power,double amplitude,std::size_t chunk,unsigned seed) {
    constexpr std::uint32_t rate=48000;
    constexpr double expected=6380,frequency=6380.37,delay=.03719;
    helper::Options options;options.power_w=power;options.data_power_w=4;
    options.callsign="N0CALL";options.timecode=false;
    AuxiliaryFixture generator(options,frequency,amplitude,.1,1700000000,true,{},true);
    FundedFrequencyReceiver receiver(expected,20,rate);
    const auto noise_sigma=std::sqrt(amplitude*amplitude*rate/(4*std::pow(10.,cn0_db_hz/10)));
    std::mt19937 rng(seed);std::normal_distribution<double> noise(0,noise_sigma);
    std::vector<float> samples(chunk);LinkResult result;
    for(std::uint64_t first=0;first<rate*12;first+=chunk) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(chunk,rate*12-first));
        for(std::size_t i=0;i<count;++i) {
            const auto time=static_cast<long double>(first+i)/rate-delay;
            const auto helper=time<0?0.:generator.mix(time,rate);
            const auto payload=amplitude*std::sqrt(4/power)*std::cos(tau*1500*static_cast<double>(time));
            samples[i]=static_cast<float>(helper+payload+noise(rng));
        }
        receiver.push(std::span<const float>(samples).first(count),rate,first);
        if(auto reference=receiver.take_update()) {
            check(reference->sample<first+count);
            check((reference->frequency_sample?reference->frequency_sample:reference->sample)<=reference->sample);
            check(reference->frequency_valid&&!reference->phase_valid&&!reference->timing_valid&&!reference->metadata);
            ++result.references;result.last=reference;
            check(std::isfinite(reference->frequency_hz)&&std::isfinite(reference->frequency_uncertainty_hz)&&
                  reference->frequency_uncertainty_hz>=0&&
                  std::abs(reference->frequency_hz-frequency)<=reference->frequency_uncertainty_hz);
            if(reference->frequency_uncertainty_hz<=.1) {
                if(!result.usable)result.first_usable=static_cast<double>(reference->sample)/rate;
                ++result.usable;
                const auto error=std::abs(reference->frequency_hz-frequency);
                result.max_error=std::max(result.max_error,error);
                result.max_uncertainty=std::max(result.max_uncertainty,reference->frequency_uncertainty_hz);
                check(error<=reference->frequency_uncertainty_hz);
            }
        }
    }
    return result;
}
void realistic_frequency() {
    const auto first=realistic_link(30.02,40,.01,137,581);
    std::cout<<"realistic helper refs="<<first.references<<" usable="<<first.usable<<" first="<<first.first_usable
             <<" max_error="<<first.max_error<<" max_unc="<<first.max_uncertainty<<'\n';
    check(first.usable>0);check(first.first_usable<8);
    for(const auto seed:{582U,583U}) {
        const auto varied=realistic_link(30.02,40,.003,137,seed);
        check(varied.usable>0&&varied.first_usable<8);
    }
    const auto batched=realistic_link(30.02,40,.01,4096,581);
    check(first.last&&batched.last);
    check(first.last->sample==batched.last->sample);
    check(first.last->frequency_hz==batched.last->frequency_hz);
    check(first.last->frequency_uncertainty_hz==batched.last->frequency_uncertainty_hz);
    // The simulated first profile includes 6.02 dB of DSSS power backoff.
    // At its resulting 24 dB-Hz, acquisition may retain modulo-pi ambiguity;
    // publication must stay frequency-only and every usable narrow interval
    // must still cover the actual carrier (checked inside realistic_link).
    const auto reduced=realistic_link(24,40,.003,137,581);
    std::cout<<"24 dB-Hz helper refs="<<reduced.references<<" usable="<<reduced.usable
             <<" first="<<reduced.first_usable<<'\n';
    check(reduced.references>0&&reduced.usable>0&&reduced.first_usable<10);
    // Other noise draws may remain ambiguous; any admitted narrow interval
    // must cover the actual frequency rather than choosing a convenient alias.
    for(const auto seed:{582U,583U,584U,585U})
        (void)realistic_link(24,40,.003,137,seed);
    const auto second=realistic_link(15.76,1500,.01,137,581);
    std::cout<<"below nominal level refs="<<second.references<<" usable="<<second.usable<<'\n';
    // A true level below 6 dB can fluctuate above the observed-level floor;
    // do not turn that floor into a confidence claim on the true C/N0.
    check(second.usable==0);
}
void weak_timecode_frequency(std::optional<bool> only={}) {
    // Weak existence evidence must not turn unresolved dwell modulation into
    // false fine frequency precision. Narrow intervals, if any, must cover the
    // real carrier; this vector does not promise useful acquisition latency.
    constexpr std::uint32_t rate=48000;
    constexpr double expected=6380,frequency=6380.37,amplitude=.01,cn0=20.7609125906;
    const Crypto key{std::array<std::uint8_t,32>{}};
    for(const bool signal:{false,true}) {
        if(only&&signal!=*only)continue;
        helper::Options options;options.power_w=1500;options.timecode=true;
        AuxiliaryFixture generator(options,frequency,amplitude,0,1700000000,true,key,true);
        FundedFrequencyReceiver receiver(expected,20,rate,{key});
        const auto sigma=std::sqrt(amplitude*amplitude*rate/(4*std::pow(10.,cn0/10)));
        std::mt19937 random(5181);std::normal_distribution<double> noise(0,sigma);
        std::array<float,137> samples{};std::size_t references=0;
        for(std::uint64_t first=0;first<20*rate;first+=samples.size()) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),20*rate-first));
            for(std::size_t i=0;i<count;++i) {
                const auto time=static_cast<long double>(first+i)/rate-.03719L;
                samples[i]=static_cast<float>((signal&&time>=0?generator.mix(time,rate):0.)+noise(random));
            }
            receiver.push(std::span<const float>(samples).first(count),rate,first);
            if(const auto reference=receiver.take_update()) {
                ++references;
                check(reference->sample<first+count && reference->frequency_sample<=reference->sample);
                check(reference->frequency_valid && !reference->phase_valid && !reference->timing_valid && !reference->metadata);
                check(std::isfinite(reference->frequency_hz)&&std::isfinite(reference->frequency_uncertainty_hz)&&
                      reference->frequency_uncertainty_hz>=0);
                if(signal)check(std::abs(reference->frequency_hz-frequency)<=reference->frequency_uncertainty_hz);
                if(reference->frequency_uncertainty_hz<=.25) {
                    check(std::abs(reference->frequency_hz-frequency)<=reference->frequency_uncertainty_hz);
                }
            }
        }
        if(signal)check(references>0);else check(references==0);
    }
}
LinkResult shifted_helper_frequency(bool signal,std::size_t chunk) {
    constexpr std::uint32_t rate=48000;
    constexpr double expected=2262.5,offset=30,ppm=100,amplitude=.01,cn0=39.7403126773;
    constexpr double frequency=expected*(1+ppm*1e-6)+offset;
    const Crypto key{std::array<std::uint8_t,32>{}};
    helper::Options options;options.power_w=1500;options.timecode=true;
    AuxiliaryFixture generator(options,expected,amplitude,0,1800000000,true,key,true);
    helper::FrequencyPrecisionOptions precision;
    precision.phase_noise_radians_per_sqrt_second=.50249378105604448*std::numbers::pi/180;
    FundedFrequencyReceiver receiver(expected,100,rate,{key},precision);
    const auto sigma=std::sqrt(amplitude*amplitude*rate/(4*std::pow(10.,cn0/10)));
    std::mt19937 noise_random(581),phase_random(7181);
    std::normal_distribution<double> noise(0,sigma),phase_step(0,
        .50249378105604448*std::numbers::pi/180/std::sqrt(rate));
    double phase=.81;std::vector<float> samples(chunk);LinkResult result;
    for(std::uint64_t first=0;first<6*rate;first+=samples.size()) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),6*rate-first));
        for(std::size_t i=0;i<count;++i) {
            const auto time=static_cast<long double>(first+i)/rate;
            const auto source_time=time*(1+ppm*1e-6)-.03719L;phase+=phase_step(phase_random);
            const auto value=!signal||source_time<0?0.:std::real(generator.analytic(source_time,rate)*
                std::polar(1.,phase+tau*offset*static_cast<double>(time)));
            samples[i]=static_cast<float>(value+noise(noise_random));
        }
        receiver.push(std::span<const float>(samples).first(count),rate,first);
        if(auto reference=receiver.take_update()) {
            check(reference->sample<first+count && reference->frequency_sample<=reference->sample);
            check(reference->frequency_valid && !reference->phase_valid && !reference->timing_valid && !reference->metadata);
            ++result.references;result.last=reference;
            // This fixed drift/noise realization must retain the known mean
            // carrier within every published interval, including after retune.
            check(std::abs(reference->frequency_hz-frequency)<=reference->frequency_uncertainty_hz);
            if(reference->frequency_uncertainty_hz<=.1) {
                if(!result.usable)result.first_usable=static_cast<double>(reference->sample)/rate;
                ++result.usable;
            }
        }
    }
    return result;
}
void shifted_frequency() {
    // At this actual HF-helper C/N0, punctuation/metadata may be undecodable.
    // A 30 Hz offset must still permit measured frequency-only assistance.
    const auto result=shifted_helper_frequency(true,137);
    check(result.usable>0 && result.first_usable<3);
    const auto batched=shifted_helper_frequency(true,4096);
    check(batched.usable>0 && result.last && batched.last);
    check(result.last->sample==batched.last->sample);
    check(result.last->frequency_hz==batched.last->frequency_hz);
    check(result.last->frequency_uncertainty_hz==batched.last->frequency_uncertainty_hz);
    check(shifted_helper_frequency(false,137).references==0);
}
void chunks_and_gaps() {
    const auto a=run(true,"N0CALL",2100,.2,.034,1,0,true);
    const auto b=run(true,"N0CALL",2100,.2,.034,4096,0,true);
    check(!a.updates.empty()&&!b.updates.empty());
    const auto& x=a.updates.back();const auto& y=b.updates.back();
    check(x.sample==y.sample&&x.frequency_hz==y.frequency_hz&&x.phase_radians==y.phase_radians);
    helper::Receiver receiver(2100);std::array<float,4800> samples{};
    for(std::size_t i=0;i<samples.size();++i)samples[i]=static_cast<float>(.6*std::cos(tau*2100*i/48000));
    receiver.push(samples,48000,0);check(receiver.take_update().has_value());
    receiver.push(std::span<const float>(samples).first(1),48000,9000);check(!receiver.take_update());
}
}
int main(int argc,char** argv) {
    try {
        check(argc<=2);std::cout<<std::unitbuf;
        const std::string_view which=argc==2?argv[1]:"all";
        if(which=="all") {
            clean_precision();paired_marker_identity_and_missing_return();metadata();metadata_carrier_images();two_tone_auxiliary_metadata();metadata_frozen_work();auxiliary_punctuation_clock();timecodes_and_corruption();metadata_with_declared_process();clock_skew();frequency_drift();fallback_and_noise();realistic_frequency();weak_timecode_frequency();shifted_frequency();chunks_and_gaps();
        } else if(which=="strong") {
            clean_precision();paired_marker_identity_and_missing_return();metadata();metadata_carrier_images();two_tone_auxiliary_metadata();metadata_frozen_work();auxiliary_punctuation_clock();timecodes_and_corruption();metadata_with_declared_process();clock_skew();frequency_drift();fallback_and_noise();chunks_and_gaps();
        } else if(which=="pair")paired_marker_identity_and_missing_return();
        else if(which=="two_tone"){two_tone_auxiliary_metadata();metadata_frozen_work();auxiliary_punctuation_clock();}
        else if(which=="realistic")realistic_frequency();
        else if(which=="legacy30"||which=="legacy24") {
            const bool reduced=which=="legacy24";
            const auto result=realistic_link(reduced?24:30.02,40,reduced?.003:.01,137,581);
            std::cout<<"MODELED,"<<which<<",refs="<<result.references<<",usable="<<result.usable
                <<",first="<<result.first_usable<<",max_error="<<result.max_error<<",max_ci="<<result.max_uncertainty<<'\n';
            check(result.usable>0&&result.first_usable<(reduced?10:8));
        } else if(which=="weak_timecode")weak_timecode_frequency();
        else if(which=="weak_noise")weak_timecode_frequency(false);
        else if(which=="weak_signal")weak_timecode_frequency(true);
        else if(which=="shifted")shifted_frequency();
        else if(which=="metadata_process")metadata_with_declared_process();
        else throw Error("unknown helper receiver test selector");
        std::cout<<"helper precision receiver passed: "<<which<<'\n';return 0;
    }catch(const std::exception& error) {std::cerr<<"FAIL,"<<error.what()<<'\n';return 1;}
}

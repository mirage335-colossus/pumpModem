#include "datapump/helper_receiver.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <source_location>
#include <stdexcept>
#include <cmath>
#include <complex>
#include <iostream>
#include <numbers>
#include <random>
#include <vector>

using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool condition,const std::source_location where=std::source_location::current()) {
    if(!condition)throw std::runtime_error(std::string(where.file_name())+":"+std::to_string(where.line())+" failed");
}
struct Result {std::vector<helper::Reference> updates;};
Result run(bool punctuation,std::string callsign,double frequency,double phase,double delay,
           std::size_t chunk,double noise=0,bool quantize=false,bool timecode=false,
           std::optional<Crypto> transmit_key={},std::vector<Crypto> keys={},double ppm=0,
           bool corrupt_metadata=false,std::uint64_t epoch=1700000000,
           std::optional<long double> repeated_dwell={}) {
    constexpr std::uint32_t rate=48000;
    helper::Options options;options.power_w=100;options.callsign=std::move(callsign);options.timecode=timecode;
    helper::Plan plan{frequency,.7,.1,100};
    helper::Generator generator(options,plan,epoch,punctuation,std::move(transmit_key));
    helper::Receiver receiver(2100,20,std::move(keys));
    Result out;std::vector<float> samples(chunk);
    std::mt19937 rng(519);std::normal_distribution<double> gaussian(0,noise);
    const auto count=static_cast<std::uint64_t>(3.2*rate);
    for(std::uint64_t first=0;first<count;first+=chunk) {
        const auto n=static_cast<std::size_t>(std::min<std::uint64_t>(chunk,count-first));
        for(std::size_t j=0;j<n;++j) {
            const auto time=static_cast<long double>(first+j)/rate*(1+ppm*1e-6)-delay;
            const auto generator_time=repeated_dwell?*repeated_dwell+std::fmod(std::max(0.L,time),.4L):time;
            auto x=time<0?0.:std::real(generator.mix_analytic({},generator_time,rate)*std::polar(1.,phase));
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
    helper::Generator generator(options,{frequency,amplitude,.1,power},1700000000,true);
    helper::Receiver receiver(expected,20);
    const auto noise_sigma=std::sqrt(amplitude*amplitude*rate/(4*std::pow(10.,cn0_db_hz/10)));
    std::mt19937 rng(seed);std::normal_distribution<double> noise(0,noise_sigma);
    std::vector<float> samples(chunk);LinkResult result;
    for(std::uint64_t first=0;first<rate*12;first+=chunk) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(chunk,rate*12-first));
        for(std::size_t i=0;i<count;++i) {
            const auto time=static_cast<long double>(first+i)/rate-delay;
            const auto helper=time<0?0.:generator.mix(0,time,rate);
            const auto payload=amplitude*std::sqrt(4/power)*std::cos(tau*1500*static_cast<double>(time));
            samples[i]=static_cast<float>(helper+payload+noise(rng));
        }
        receiver.push(std::span<const float>(samples).first(count),rate,first);
        if(auto reference=receiver.take_update()) {
            check(reference->sample<first+count);
            check((reference->frequency_sample?reference->frequency_sample:reference->sample)<=reference->sample);
            check(reference->frequency_valid&&!reference->phase_valid&&!reference->timing_valid&&!reference->metadata);
            ++result.references;result.last=reference;
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
int main() {
    clean_precision();metadata();timecodes_and_corruption();clock_skew();frequency_drift();fallback_and_noise();realistic_frequency();chunks_and_gaps();
    std::cout<<"helper precision receiver passed\n";
}

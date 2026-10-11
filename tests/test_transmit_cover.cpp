#include "datapump/transfer.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/fhss_schedule.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <map>
#include <random>
#include "datapump/pattern_receiver.hpp"

using namespace datapump;
using Complex=std::complex<double>;
namespace {
void check(bool value,const char* message) { if(!value)throw Error(message); }
modem::Config configuration(unsigned factor,double duration=.537) {
    transfer::Options options;options.timestamp=1800000321;options.key.emplace(Bytes(32,0x65));
    auto& c=options.modem;c.sample_rate=1024;c.carrier_hz=256;c.bandwidth_hz=128./factor;
    c.scramble=c.dsss=true;c.dsss_factor=factor;c.integration_seconds=duration;
    c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;c.stream_phase_samples=313;
    return transfer::seeded_config(options,options.timestamp);
}
// Independent role-derivation and full-symbol coefficient reference. In
// particular, the permutation length is NEVER the observed section length.
modem::Config reference_cover(modem::Config c,StreamDomain domain) {
    Bytes root(32,0);
    const auto mix=[&](const Crypto& key,StreamPurpose purpose) {
        const auto bytes=key.stream(purpose,c.stream_epoch,0,32,domain);
        for(std::size_t i=0;i<32;++i)root[i]^=bytes[i];
    };
    mix(*c.data_key,StreamPurpose::Data);
    mix(Crypto(c.spreading_seed),StreamPurpose::Scrambler);mix(Crypto(c.dsss_seed),StreamPurpose::Dsss);
    c.data_key=Crypto(root);
    for(const auto purpose:{StreamPurpose::Scrambler,StreamPurpose::Dsss}) {
        const auto seed=c.data_key->stream(purpose,0,0,32);
        std::copy(seed.begin(),seed.end(),purpose==StreamPurpose::Scrambler?c.spreading_seed.begin():c.dsss_seed.begin());
    }
    return c;
}
unsigned dummy_bit(const modem::Config& c,std::uint64_t symbol) {
    const auto a=modem::symbol_stream_address(c.stream_epoch,c.stream_phase_samples,symbol,
        modem::symbol_sample_count(c),c.sample_rate);
    return (c.data_key->stream(StreamPurpose::Data,a.epoch,a.ordinal/8,1)[0]>>(7-a.ordinal%8))&1U;
}
void full_outer_cover() {
    for(const unsigned factor:{1U,10U,100U,1000U}) {
        auto c=configuration(factor);const auto symbol=modem::symbol_sample_count(c);
        const auto chip=modem::pattern_chip_samples(c),padding=modem::pattern_pulse_padding_samples(c);
        const auto training=modem::training_sample_count(c),suppression=modem::suppression_sample_count(c);
        const Bytes bits{0,1,0};
        modem::PatternTransmitter source(bits,c,c.stream_epoch),bare(bits,c,c.stream_epoch,0,false);
        std::vector<Complex> actual(source.total_samples()),payload(bare.total_samples());
        source.read_analytic(actual);bare.read_analytic(payload);
        check(source.total_samples()==training+payload.size()+suppression,"cover changed physical duration");
        const auto gain=std::sqrt(2*modem::nominal_signal_power)*modem::outer_dsss_transmit_gain(c);
        auto pre=reference_cover(c,StreamDomain::CoverPreambleV1);
        auto tail=reference_cover(c,StreamDomain::CoverSuppressionV1);
        check(pre.spreading_seed!=tail.spreading_seed && pre.dsss_seed!=c.dsss_seed,
              "cover reused a communication or adjacent-section root");
        modem::PatternCode pre_code(pre,c.stream_epoch),tail_code(tail,c.stream_epoch);
        // Sample every fractional chip location and boundary at a sparse stride.
        for(std::uint64_t i=0;i<actual.size();i+=7) {
            const auto oscillator=std::polar(1.,2*std::numbers::pi*c.carrier_hz*static_cast<double>(i)/c.sample_rate);
            if(i+padding<training) {
                Complex expected{};const auto local=static_cast<double>(i)-padding;
                for(std::uint64_t s=0;s<training/symbol;++s)
                    expected+=pre_code.shaped_value(s*pre_code.chips_per_symbol(),dummy_bit(pre,s),local-s*symbol);
                check(std::abs(actual[i]-oscillator*modem::pattern_limit_pcm(gain*expected))<1e-9,
                      "prefix is not ordinary full-symbol private/DSSS construction");
            }
            const auto tail_begin=training+payload.size();
            if(i>=tail_begin) {
                const auto offset=static_cast<double>(i-tail_begin)+padding;Complex expected{};
                const auto count=(suppression+2*padding+symbol-1)/symbol;
                for(std::uint64_t s=0;s<count;++s)
                    modem::pattern_pulse_each(offset-static_cast<double>(s*symbol),symbol,chip,[&](std::uint64_t k,double pulse) {
                        const auto first=k*chip,length=std::min(chip,symbol-first);
                        const auto center=static_cast<double>(s*symbol+first)+.5*length;
                        if(center>=0 && center<suppression-1)
                            expected+=pulse*tail_code.value(s*tail_code.chips_per_symbol()+k,dummy_bit(tail,s));
                    });
                check(std::abs(actual[i]-oscillator*modem::pattern_limit_pcm(gain*expected))<1e-9,
                      "suppression shortened the virtual symbol or bypassed outer DSSS");
            }
        }
        modem::PatternCode payload_code(c,c.stream_epoch);
        const auto cut=training+bits.size()*symbol+padding;
        const auto cover_end=actual.size()-padding-1;
        for(std::uint64_t i=cut-padding;i<cut+padding+3*c.sample_rate;i+=5) {
            Complex expected{};const auto coordinate=static_cast<double>(i)-padding;
            for(std::uint64_t bit=0;bit<bits.size();++bit)
                expected+=payload_code.shaped_value(bit*payload_code.chips_per_symbol(),bits[bit],coordinate-training-bit*symbol);
            const auto tail_coordinate=static_cast<double>(i)-cut;
            const auto begin=static_cast<std::uint64_t>(std::max(0.,std::floor((tail_coordinate-padding)/symbol)));
            const auto end=static_cast<std::uint64_t>(std::floor((tail_coordinate+padding)/symbol)+1);
            for(auto sym=begin;sym<end;++sym)
                modem::pattern_pulse_each(tail_coordinate-sym*symbol,symbol,chip,[&](std::uint64_t k,double pulse) {
                    const auto length=std::min(chip,symbol-k*chip);
                    const auto center=cut+sym*symbol+k*chip+.5*length;
                    if(center>=cut && center<cover_end)
                        expected+=pulse*tail_code.value(sym*tail_code.chips_per_symbol()+k,dummy_bit(tail,sym));
                });
            const auto oscillator=std::polar(1.,2*std::numbers::pi*c.carrier_hz*static_cast<double>(i)/c.sample_rate);
            check(std::abs(actual[i]-oscillator*modem::pattern_limit_pcm(gain*expected))<1e-9,
                "payload-cover handover is not a common coefficient shaper/limiter");
        }
        const auto rotation=std::polar(1.,2*std::numbers::pi*c.carrier_hz*static_cast<double>(training)/c.sample_rate);
        for(std::uint64_t i=2*padding;i+2*padding<payload.size();++i)
            check(std::abs(actual[training+i]-rotation*payload[i])<1e-9,"cover altered payload beyond additive prefix overlap");
        check(source.working_bytes()<=c.memory_limit,"full cover map escaped workspace accounting");
        auto small=c;small.memory_limit=source.working_bytes()-1;bool rejected=false;
        try {modem::PatternTransmitter unavailable(bits,small,c.stream_epoch);}catch(const Error&){rejected=true;}
        check(rejected,"one-byte-short workspace admitted a lazy cover map");
        check(std::abs(actual.front())<1e-12 && std::abs(actual.back())<1e-12,"burst edges lack complete pulse support");
    }
}
void finish_paths() {
    {auto c=configuration(10);modem::PatternTransmitter none(Bytes{0},c,c.stream_epoch);
     none.request_finish();std::array<float,13> pcm{};
     check(none.finished() && !none.total_samples() && none.read(pcm)==0,"finish before output emitted silence or private chips");}
    for(const unsigned factor:{1U,10U,100U,1000U})for(const bool shaped:{false,true}) {
        auto c=configuration(factor);c.pulse_shaping=shaped;
        const auto chip=modem::pattern_chip_samples(c),pad=modem::pattern_pulse_padding_samples(c);
        const auto train=modem::training_sample_count(c),symbol=modem::symbol_sample_count(c);
        const auto content=train+3*symbol+2*pad;
        for(const auto point:{1ULL,static_cast<unsigned long long>(train+pad+chip/2),
            static_cast<unsigned long long>(train+pad+symbol),static_cast<unsigned long long>(content+chip/2)}) {
            modem::PatternTransmitter whole(Bytes{0,1,0},c,c.stream_epoch),fragmented(Bytes{0,1,0},c,c.stream_epoch),reference(Bytes{0,1,0},c,c.stream_epoch);
            const auto original=whole.total_samples();const auto memory=whole.working_bytes();
            std::vector<Complex> original_pcm(original),before(point),same(point);reference.read_analytic(original_pcm);
            whole.read_analytic(before);fragmented.read_analytic(same);
            check(before==same,"equal source prefix differs before finish request");
            whole.request_finish();fragmented.request_finish();whole.request_finish();
            check(whole.original_total_samples()==original,"graceful stop lost immutable exposure extent");
            const auto bound=modem::transmit_stop_sample_count(c);
            check(whole.total_samples()<=point+bound && whole.total_samples()<=original,"finish waited for a whole symbol");
            if(shaped && point<train+3*symbol)
                check(whole.total_samples()==point+4*pad+3*c.sample_rate+1,
                    "graceful stop shortened the protected three seconds or shaping support");
            std::vector<Complex> tail(whole.total_samples()-point),pieces(tail.size());whole.read_analytic(tail);
            std::size_t offset=0;while(!fragmented.finished()) {
                const auto n=fragmented.read_analytic(std::span(pieces).subspan(offset,std::min<std::size_t>(13,pieces.size()-offset)));
                offset+=n;
                std::array<Complex,31> preview{};fragmented.preview_last_analytic(preview);
                const auto shown=std::min<std::uint64_t>(preview.size(),fragmented.samples_emitted());
                for(std::uint64_t j=0;j<shown;++j) {
                    const auto index=fragmented.samples_emitted()-shown+j;
                    const auto expected=index<point?original_pcm[index]:tail[index-point];
                    check(std::abs(preview[preview.size()-shown+j]-expected)<1e-9,"finish preview changed past PCM or stop state");
                }
            }
            for(std::size_t i=0;i<tail.size();++i)check(std::abs(tail[i]-pieces[i])<1e-9,"finish depends on output block size");
            if(!tail.empty())check(std::abs(tail.back())<1e-12,"finish ended before retained pulse support reached zero");
            check(whole.working_bytes()==memory && fragmented.working_bytes()==memory,"finish allocated duration-sized history");
            std::stop_source stop;stop.request_stop();bool rejected=false;
            try {reference.read_analytic(same,stop.get_token());}catch(const Error&){rejected=true;}
            check(rejected && reference.samples_emitted()==original,"hard abort advanced source state");
        }
    }
    auto c=configuration(1000,3600*4.);c.bandwidth_hz=.001;c.sample_rate=64;c.carrier_hz=16;
    c.stream_phase_samples=0;
    modem::PatternTransmitter long_bit(Bytes{0,1},c,c.stream_epoch);
    std::array<float,17> first{};long_bit.read(first);long_bit.request_finish();
    check(long_bit.total_samples()==first.size()+4*modem::pattern_pulse_padding_samples(c)+3*c.sample_rate+1,
        "long-symbol stop shortened useful cover to fit pulse support");
    check(long_bit.total_samples()<modem::symbol_sample_count(c),"four-hour bit finish waited for its endpoint");
    check(long_bit.working_bytes()<2*1024*1024,"long bit allocated whole-symbol PCM");
    std::cout<<"four-hour symbol: fine chip "<<modem::pattern_chip_samples(c)/double(c.sample_rate)
             <<" s; graceful tail "<<(long_bit.total_samples()-first.size())/double(c.sample_rate)<<" s; immediate abort retained\n";
}
void cancelled_partial_chip_lattice() {
    // Nonintegral final chip and a stop three samples past a symbol boundary.
    // Independent coefficient partition catches reanchoring cover at P.
    for(const unsigned factor:{1U,10U,100U,1000U}) {
        auto c=configuration(factor);const Bytes bits{0,1,0};
        const auto t=modem::symbol_sample_count(c),chip=modem::pattern_chip_samples(c);
        const auto h=modem::pattern_pulse_padding_samples(c),pre=modem::training_sample_count(c);
        check(t%chip!=0,"partial-chip handover fixture became integral");
        const auto p=pre+h+t+3,cut=p+h,origin=h+(p/t)*t;
        modem::PatternTransmitter tx(bits,c,c.stream_epoch);
        std::vector<Complex> first(p);tx.read_analytic(first);tx.request_finish();
        const auto end=tx.total_samples()-h-1;
        std::vector<Complex> tail(tx.total_samples()-p);tx.read_analytic(tail);
        modem::PatternCode payload(c,c.stream_epoch);auto cover=reference_cover(c,StreamDomain::CoverSuppressionV1);
        modem::PatternCode outer(cover,c.stream_epoch);
        const auto gain=std::sqrt(2*modem::nominal_signal_power)*modem::outer_dsss_transmit_gain(c);
        for(std::uint64_t at=p;at<tx.total_samples();at+=17) {
            Complex value{};
            for(std::uint64_t b=0;b<bits.size();++b)
                modem::pattern_pulse_each(double(at)-double(pre+h+b*t),t,chip,[&](auto k,double pulse) {
                    const auto center=pre+h+b*t+k*chip+.5L*std::min(chip,t-k*chip);
                    if(center<cut)value+=pulse*payload.value(b*payload.chips_per_symbol()+k,bits[b]);
                });
            const auto last=(tx.total_samples()-origin+h)/t+1;
            for(std::uint64_t b=0;b<last;++b)
                modem::pattern_pulse_each(double(at)-double(origin+b*t),t,chip,[&](auto k,double pulse) {
                    const auto center=origin+b*t+k*chip+.5L*std::min(chip,t-k*chip);
                    if(center>=cut && center<end)value+=pulse*outer.value(b*outer.chips_per_symbol()+k,dummy_bit(cover,b));
                });
            const auto carrier=std::polar(1.,2*std::numbers::pi*c.carrier_hz*double(at)/c.sample_rate);
            check(std::abs(tail[at-p]-carrier*modem::pattern_limit_pcm(gain*value))<1e-9,
                  "cancelled cover moved the original symbol/partial-chip lattice");
        }
    }
}
void finite_tuning() {
    for(const unsigned factor:{1U,10U,100U,1000U}) {
        auto c=configuration(factor);c.dsss=false; // GUI outer spreading is independent of the inner optional mask.
        auto plan=std::make_shared<modem::FhssPlan>();plan->channels=3;plan->dwell_seconds=.4;plan->shifts[0]=1e6;plan->shifts[1]=1.01e6;plan->shifts[2]=1.02e6;c.fake_hopping=plan;
        modem::StreamingTransmitter first(modem::Noise{3},c),second(modem::Noise{3},c);
        modem::PatternTransmitter message(Bytes{0,1,0},c,c.stream_epoch);
        check(first.total_samples()==message.total_samples(),"finite tuning changed shared burst duration");
        std::vector<Complex> a(first.total_samples()),b(a.size());first.read_analytic(a);second.read_analytic(b);
        check(a!=b,"independent tuning starts reused temporary roots");
        const auto trace=first.transmit_trace();
        check(trace.fhss && trace.fhss_epoch==c.stream_epoch && trace.fhss_available && !trace.fhss_key.empty(),
              "tuning bypassed dedicated continuous keyed FHSS schedule");
        check(trace.temporary_key && !trace.dsss && !trace.active && trace.source.empty() && trace.data_key_bits.empty(),
              "tuning disclosed a root or became communication payload");
    }
}
void encrypted_echoes() {
    for(const unsigned factor:{1U,10U,100U,1000U})for(unsigned model=0;model<4;++model) {
        auto c=configuration(factor);c.sample_rate=2048;c.carrier_hz=512;c.bandwidth_hz=512./factor;
        c.integration_seconds=std::max(.5,factor/32.);c.stream_phase_samples=0;c.dsss=factor>1;
        const Bytes bits{1,0,0,1};const auto duration=modem::symbol_sample_count(c);
        for(const bool suppress:{true,false}) {
            if(!suppress && (model!=1 || factor>10))continue;
            modem::PatternTransmitter tx(bits,c,c.stream_epoch,0,suppress);
            std::vector<float> pcm(tx.total_samples());tx.read(pcm);
            std::vector<std::pair<double,double>> taps{{0,1}};
            if(model==1)taps.push_back({3.,.2});
            if(model==2)taps={{0,1},{2.8,.17},{3.000244140625,.14},{3.2,-.10}};
            if(model==3)for(unsigned i=0;i<21;++i)taps.push_back({2.5+i*.04,.14*std::exp(-static_cast<double>(i)*.12)*(i%2?-1:1)});
            const auto absence=(1+static_cast<std::uint64_t>(std::ceil(6.*c.sample_rate/duration)))*duration;
            std::vector<float> received(pcm.size()+4*c.sample_rate+absence);
            for(const auto [delay,gain]:taps) {
                const auto coordinate=delay*c.sample_rate+.375;
                const auto first=static_cast<std::size_t>(coordinate);const auto f=coordinate-first;
                for(std::size_t i=0;i<pcm.size();++i) {
                    received[first+i]+=static_cast<float>(gain*(1-f)*pcm[i]);
                    received[first+i+1]+=static_cast<float>(gain*f*pcm[i]);
                }
            }
            std::mt19937_64 rng(3832);std::normal_distribution<double> noise(0,std::sqrt(modem::nominal_signal_power/1e5));
            for(auto& value:received)value+=static_cast<float>(noise(rng));
            modem::PatternSearch search;search.frequency_offsets_hz={0};search.clock_errors_ppm={0};search.initial_stream_symbols=1;
            if(suppress && factor==10 && model==2) {
                // The complete reflected waveform, including cover, also
                // reaches unrelated key and disjoint epoch alternatives.
                // This bounded rejection check does not calibrate a rolling
                // Live bank's aggregate false-accept probability.
                for(unsigned alternative=0;alternative<3;++alternative) {
                    auto other=c;
                    if(alternative==0) {
                        other.spreading_seed[0]^=0x53;other.dsss_seed[0]^=0x71;
                        other.data_key.emplace(Bytes(32,0x27));
                    // Neighboring canonical epochs can legitimately describe
                    // later payload bits. Keep these rejection alternatives
                    // outside every epoch touched by this complete capture.
                    } else other.stream_epoch+=alternative==1?-103:103;
                    modem::PatternReceiver unrelated(other,64*1024*1024,search);
                    const auto reject=[&] {
                        for(const auto& b:unrelated.take_bursts())
                            check(b.bits.empty() && !b.complete,"reflected full cover admitted an unrelated key/epoch stream");
                    };
                    for(std::size_t offset=0;offset<received.size();) {
                        const auto n=std::min<std::size_t>(317,received.size()-offset);
                        unrelated.push(std::span(received).subspan(offset,n));offset+=n;reject();
                    }
                    unrelated.finish();reject();
                }
            }
            modem::PatternReceiver rx(c,64*1024*1024,search);
            struct Stream{Bytes bits;std::uint64_t start=0;bool complete=false;};
            std::map<std::pair<std::uint64_t,std::uint64_t>,Stream> streams;
            const auto harvest=[&] {for(const auto& b:rx.take_bursts()) {
                auto [it,added]=streams.try_emplace({b.stream_first_sample,b.stream_first_symbol});auto& s=it->second;
                if(added)s.start=b.first_stream_symbol;
                check(!s.complete && b.first_stream_symbol==s.start+s.bits.size(),"echo test pending identity changed");
                s.bits.insert(s.bits.end(),b.missing_slots,modem::missing_pattern_bit);s.bits.insert(s.bits.end(),b.bits.begin(),b.bits.end());s.complete=b.complete;
            }};
            bool pending=false;
            for(std::size_t offset=0;offset<received.size();) {
                const auto count=std::min<std::size_t>(317,received.size()-offset);
                rx.push(std::span(received).subspan(offset,count));offset+=count;harvest();
                if(offset<pcm.size())for(const auto& [id,s]:streams)pending|=!s.complete;
            }
            rx.finish();harvest();std::size_t accepted=0,correct=0;
            for(const auto& [id,s]:streams){accepted+=s.bits.size();correct+=s.start==0 && s.bits==bits && s.complete;}
            if(suppress)check(pending && streams.size()==1 && accepted==bits.size() && correct==1,
                "encrypted full-DSSS echo/cover admitted another stream or lost payload/completion");
            else check(accepted>bits.size(),"private no-suppression control failed to expose delayed duplicate bits");
        }
    }
}

void production_boundaries() {
    transfer::Options options;options.modem=configuration(10);options.modem.scramble=options.modem.dsss=false;
    check(!modem::private_cover_enabled(options.modem),
        "outer factor alone incorrectly enabled the private cover construction");
    options.modem.data_key.reset(); // Unsupported diagnostic outer layer, without a communication key.
    bool rejected=false;try{transfer::binary_transmitter(Bytes{0,1},options);}catch(const Error&){rejected=true;}
    check(rejected,"production silently selected diagnostic DSSS cover");
    options.modem.dsss_factor=1;
    check(transfer::binary_transmitter(Bytes{0,1},options)!=nullptr,"ordinary unencrypted production was disabled");
}
void diagnostic_cover_compatibility() {
    // Frozen preceding candidate, independent of the new cover implementation.
    // This low-level mode has no Data or private Scrambler and is not an
    // encrypted GUI transmission. Preserve its established prefix/suppression
    // while full-bank false-acceptance of alternative cover remains unqualified.
    modem::Config c;c.spreading_factor=32;c.sample_rate=6000;c.bandwidth_hz=1000;
    c.carrier_hz=1500;c.stream_epoch=1789312671;c.dsss=true;c.dsss_seed[11]=139;
    for(std::size_t i=0;i<32;++i)c.spreading_seed[i]=3*i+7;
    modem::PatternTransmitter tx(Bytes{0,1},c,c.stream_epoch);
    std::vector<Complex> pcm(tx.total_samples());tx.read_analytic(pcm);
    const std::array<std::pair<std::size_t,Complex>,8> frozen{{
        {480,{.34077451905652295,-.036082084600082581}},
        {783,{.69006033962066304,-.045834646697645032}},
        {1234,{-.32742424691975808,-.81003834257240193}},
        {9001,{-.0063206590258019571,-.3781553590820253}},
        {15000,{.2878400547690686,.024419098156241908}},
        {20000,{.31132973239574618,-.11706377295397007}},
        {28001,{.14870519218678216,.29331108717884924}},
        {30000,{-.019054207700596515,-.099541602772456536}}
    }};
    for(const auto& [index,value]:frozen)
        check(std::abs(pcm.at(index)-value)<1e-10,"DSSS-only diagnostic cover changed");
}
}
int main() {
    try {full_outer_cover();encrypted_echoes();finish_paths();cancelled_partial_chip_lattice();finite_tuning();production_boundaries();diagnostic_cover_compatibility();std::cout<<"full virtual cover, shared boundaries and finish passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

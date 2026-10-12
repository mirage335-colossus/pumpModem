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
#include "../src/pattern_correlator_batch.hpp"

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
            if(shaped && point<train+3*symbol) {
                const auto valid_end=whole.total_samples()-modem::suppression_sample_count(c);
                check(valid_end==point+2*pad && valid_end<=content,
                    "timing endpoint omitted already-contributed valid pulse support");
            }
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
        c.bandwidth_hz=512./factor; // finite pulse support must fit the production400ms dwell.
        auto plan=std::make_shared<modem::FhssPlan>();plan->channels=3;plan->dwell_seconds=.4;plan->shifts[0]=1e6;plan->shifts[1]=1.01e6;plan->shifts[2]=1.02e6;c.fake_hopping=modem::resolve_fhss_power(*plan,c);
        modem::StreamingTransmitter first(modem::Noise{3},c),second(modem::Noise{3},c);
        modem::PatternTransmitter message(Bytes{0,1,0},c,c.stream_epoch);
        check(first.total_samples()==message.total_samples(),"finite tuning changed shared burst duration");
        std::vector<Complex> a(first.total_samples()),b(a.size());first.read_analytic(a);second.read_analytic(b);
        check(a!=b,"independent tuning starts reused temporary roots");
        const auto trace=first.transmit_trace();
        check(trace.fhss && trace.fhss_epoch==0 && trace.fhss_available && !trace.fhss_key.empty(),
              "tuning bypassed dedicated continuous keyed FHSS schedule");
        check(trace.temporary_key && !trace.dsss && !trace.active && trace.source.empty() && trace.data_key_bits.empty(),
              "tuning disclosed a root or became communication payload");
    }
}
// A bounded physical-damage fixture, separate from graph telemetry. The
// provider counter does NOT establish this erasure model. Keep the whole burst,
// channel noise, all streams, next-poll publication and observed absence.
void xrun_erasure_evidence() {
    for(const unsigned factor:{1U,10U,100U,1000U}) {
        auto c=configuration(factor);c.sample_rate=2048;c.carrier_hz=512;
        c.bandwidth_hz=512./factor;c.integration_seconds=std::max(2.,factor/32.);
        c.stream_phase_samples=0;c.dsss=factor>1;
        const Bytes bits{1,0,1};const auto symbol=modem::symbol_sample_count(c);
        const auto first=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c);
        modem::PatternTransmitter tx(bits,c,c.stream_epoch);
        std::vector<Complex> original(tx.total_samples());tx.read_analytic(original);
        const auto gap=static_cast<std::uint64_t>(std::ceil(512.*c.sample_rate/48000));
        const auto start=first+symbol+symbol/2+3;
        // Conditional, worst-carrier-phase matched projection loss. H is the
        // removed I/Q Gram matrix and G the original. The exact generalized
        // eigenvalue gives max removed matched energy; no independent-sample
        // assumption or thermal-noise resemblance is made.
        double cc=0,ss=0,cs=0,hc=0,hs=0,hcs=0;
        for(std::uint64_t i=first+symbol;i<first+2*symbol;++i) {
            const auto re=original[i].real(),im=original[i].imag();cc+=re*re;ss+=im*im;cs+=re*im;
            if(i>=start&&i<start+gap){hc+=re*re;hs+=im*im;hcs+=re*im;}
        }
        const auto determinant=cc*ss-cs*cs;
        const auto trace=(ss*hc+cc*hs-2*cs*hcs)/determinant;
        const auto det=(hc*hs-hcs*hcs)/determinant;
        const auto fraction=.5*(trace+std::sqrt(std::max(0.,trace*trace-4*det)));
        const auto loss=-20*std::log10(1-fraction);
        check(std::isfinite(loss)&&loss<1.,"bounded erasure lost excessive conditional matched energy");
        const auto absence=(1+static_cast<std::uint64_t>(std::ceil(6.*c.sample_rate/symbol)))*symbol;
        for(const bool erase:{false,true}) {
            std::vector<float> input(original.size()+absence);
            std::mt19937_64 rng(19437);std::normal_distribution<double> noise(0,.003);
            for(std::size_t i=0;i<input.size();++i) {
                const bool removed=erase&&i>=start&&i<start+gap;
                input[i]=static_cast<float>((i<original.size()&&!removed?original[i].real():0)+noise(rng));
            }
            modem::PatternSearch search;search.frequency_offsets_hz={0};search.clock_errors_ppm={0};
            search.initial_stream_symbols=1;search.start_offset_seconds=double(first)/c.sample_rate;
            search.start_uncertainty_seconds=.5/c.sample_rate;search.compact_clock_search=true;
            modem::PatternReceiver rx(c,32*1024*1024,search);
            std::map<std::pair<std::uint64_t,std::uint64_t>,Bytes> streams;
            std::size_t publications=0;bool complete=false;std::uint64_t consumed=0;
            const auto harvest=[&]{for(const auto& event:rx.take_bursts()) {
                auto& got=streams[{event.stream_first_sample,event.stream_first_symbol}];
                check(!event.missing_slots&&event.first_stream_symbol==got.size(),"xrun fixture moved bit identity");
                if(!event.bits.empty()) {++publications;check(event.bits.size()==1,"xrun fixture batched accepted bits");}
                got.insert(got.end(),event.bits.begin(),event.bits.end());
                if(event.complete)check(consumed>=first+bits.size()*symbol+6*c.sample_rate,
                    "xrun gap or source EOF manufactured physical completion");
                complete|=event.complete;
            }};
            while(consumed<input.size()) {
                const auto count=std::min<std::size_t>(113,input.size()-consumed);
                rx.push(std::span(input).subspan(consumed,count));consumed+=count;harvest();
            }
            rx.finish();harvest();
            check(streams.size()==1&&streams.begin()->second==bits&&complete&&publications==bits.size(),
                "xrun erasure caused extra/lost bits, publication or completion failure");
            std::cout<<"xrun fixture factor="<<factor<<" erase="<<erase<<" symbol_s="<<double(symbol)/c.sample_rate
                <<" gap_ms="<<1000.*gap/c.sample_rate<<" conditional_worst_phase_loss_db="<<loss
                <<" workspace="<<rx.working_bytes()<<'\n';
        }
    }
}
// Paired finite-interruption channel: delayed/stale final PCM followed by the
// same5ms quintic transition and1649-frame forward repair as the real callback
// fixture. Downstream DAC damage is not known from a graph counter, so this is
// an explicit channel model, separately from the production callback oracle.
void forward_repair_evidence() {
 for(const unsigned factor:{1U,10U,100U,1000U})for(const bool hopping:{false,true}) {
  auto c=configuration(factor);c.sample_rate=2048;c.carrier_hz=437;
  c.bandwidth_hz=512./factor;c.integration_seconds=std::max(12.8,factor/32.);
  c.stream_phase_samples=0;c.dsss=factor>1;
  if(hopping){auto p=std::make_shared<modem::FhssPlan>();p->channels=3;p->dwell_seconds=.4;
   p->shifts[0]=10;p->shifts[1]=20;p->shifts[2]=30;p->power_ramps=true;
   p->clock_error_seconds=.01;p->peer_clock_error_seconds=.01;p->region_seconds=.001;p->audio_error_seconds=.05;
   p->smooth_ramps=true;p->mandatory_ramps=true;p->ramp_seconds=1./175;c.fake_hopping=p;}
  const Bytes bits{1,0,1};const auto symbol=modem::symbol_sample_count(c);
  const auto first=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c);
  modem::PatternTransmitter tx(bits,c,c.stream_epoch);std::vector<Complex> original(tx.total_samples());tx.read_analytic(original);
  const auto absence=(1+static_cast<std::uint64_t>(std::ceil(6.*c.sample_rate/symbol)))*symbol;
  const auto interpolate=[&](double p){if(p<0 || p+1>=original.size())return Complex{};
   const auto i=static_cast<std::size_t>(p);const auto f=p-i;return original[i]*(1-f)+original[i+1]*f;};
  const auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));};
  for(const bool final_bit:{false,true})for(const int extremum:{0,-1,1}) {
   const auto start=first+(final_bit?2:1)*symbol+symbol/2+3;
   const double gap=.0343548633333333,skip=extremum?gap-extremum*.5/c.sample_rate:1649./48000,ramp=.005;
   std::vector<Complex> repaired(original.size());
   for(std::size_t i=0;i<repaired.size();++i) {
    const auto t=(static_cast<double>(i)-start)/c.sample_rate;
    repaired[i]=t<0?original[i]:t<gap?Complex{}:t<gap+ramp?
     interpolate(i-gap*c.sample_rate)*(1-smooth((t-gap)/ramp)):
     interpolate(i-(gap-skip)*c.sample_rate)*smooth((t-gap-ramp)/ramp);
   }
   // Conditional matched-I/Q projection; evaluate every carrier phase at1deg.
   // Noise covariance G is unchanged and includes pulse overlap. This reports
   // waveform damage, not detector threshold or full acquisition qualification.
   double cc=0,ss=0,cs=0,b00=0,b01=0,b10=0,b11=0,worst=0;
   const auto bit=final_bit?2:1;
   for(auto i=first+bit*symbol;i<first+(bit+1)*symbol;++i) {
    const auto a=original[i],b=repaired[i];cc+=a.real()*a.real();ss+=a.imag()*a.imag();cs+=a.real()*a.imag();
    b00+=a.real()*b.real();b01+=a.real()*b.imag();b10+=a.imag()*b.real();b11+=a.imag()*b.imag();
   }
   for(unsigned phase=0;phase<360;++phase) {const auto p=phase*std::numbers::pi/180.;const auto x=std::cos(p),y=std::sin(p);
    const auto v0=b00*x+b01*y,v1=b10*x+b11*y;
    const auto original_energy=cc*x*x+2*cs*x*y+ss*y*y;
    const auto projected=(ss*v0*v0-2*cs*v0*v1+cc*v1*v1)/(cc*ss-cs*cs);
    worst=std::max(worst,10*std::log10(original_energy/projected));
   }
   if(!extremum)check(std::isfinite(worst)&&worst<1.,"forward repair exceeded conditional one-dB fixture allowance");
   else check(std::isfinite(worst)&&worst<2.,"cumulative displacement extremum exceeded conditional two-dB allowance");
   for(const bool damage:{false,true}) {
    std::vector<float> input(original.size()+absence);std::mt19937_64 rng(19437);std::normal_distribution<double> noise(0,.003);
    for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>((i<original.size()?(damage?repaired[i]:original[i]).real():0)+noise(rng));
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.clock_errors_ppm={0};search.initial_stream_symbols=1;
    search.start_offset_seconds=double(first)/c.sample_rate;search.start_uncertainty_seconds=.5/c.sample_rate;search.compact_clock_search=true;
    modem::PatternReceiver rx(c,32*1024*1024,search);std::map<std::pair<std::uint64_t,std::uint64_t>,Bytes> streams;
    std::size_t publications=0;std::uint64_t consumed=0;bool complete=false;
    const auto harvest=[&]{for(const auto& event:rx.take_bursts()) {auto& got=streams[{event.stream_first_sample,event.stream_first_symbol}];
     check(!event.missing_slots&&event.first_stream_symbol==got.size(),"forward repair changed bit identity");
     if(!event.bits.empty()){++publications;check(event.bits.size()==1,"forward repair batched publication");}
     got.insert(got.end(),event.bits.begin(),event.bits.end());
     if(event.complete)check(consumed>=first+bits.size()*symbol+6*c.sample_rate,"repair manufactured observed absence");complete|=event.complete;
    }};
    while(consumed<input.size()){const auto count=std::min<std::size_t>(113,input.size()-consumed);rx.push(std::span(input).subspan(consumed,count));consumed+=count;harvest();}
    rx.finish();harvest();check(streams.size()==1&&streams.begin()->second==bits&&complete&&publications==bits.size(),"forward repair lost/added bits or physical completion");
   }
   std::cout<<"forward repair factor="<<factor<<" hopping="<<hopping<<" final_bit="<<final_bit
    <<" residual_extremum="<<extremum<<" symbol_s="<<double(symbol)/c.sample_rate<<" conditional_phase_grid_loss_db="<<worst<<'\n';
  }
 }
}
// Confirmed-date recovery adds two graph quanta before the existing shaped
// skip. Use48kHz analytic source to avoid attributing coarse6120Hz linear-
// interpolation attenuation to the production48kHz integer skip. Both the
// interrupted bit and EVERY later bit retain the residual displacement.
void confirmed_forward_repair_evidence() {
 for(const unsigned factor:{1U,10U,100U,1000U})for(const bool hopping:{false,true}) {
  auto c=configuration(factor);c.sample_rate=48000;c.carrier_hz=1500;
  c.bandwidth_hz=360./factor;c.integration_seconds=35.555556;
  c.stream_phase_samples=0;c.dsss=factor>1;
  if(hopping){auto p=std::make_shared<modem::FhssPlan>();p->channels=3;p->dwell_seconds=.4;
   p->shifts[0]=10;p->shifts[1]=20;p->shifts[2]=30;p->power_ramps=true;
   p->clock_error_seconds=.01;p->peer_clock_error_seconds=.01;p->region_seconds=.001;p->audio_error_seconds=.05;
   p->smooth_ramps=true;p->mandatory_ramps=true;p->ramp_seconds=1./175;c.fake_hopping=p;}
  const Bytes bits{1,0,1};const auto symbol=modem::symbol_sample_count(c);
  const auto first=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c);
  modem::PatternTransmitter tx(bits,c,c.stream_epoch);std::vector<Complex> original(tx.total_samples());tx.read_analytic(original);
  const auto interpolate=[&](double p){if(p<0 || p+1>=original.size())return Complex{};
   const auto i=static_cast<std::size_t>(p);const auto f=p-i;return original[i]*(1-f)+original[i+1]*f;};
  const auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*x*(10+x*(-15+6*x));};
  for(const int extremum:{-1,1})for(const bool final_bit:{false,true}) {
   const auto start=first+(final_bit?2:1)*symbol+symbol/2+3;
   constexpr double gap=.018062712,confirmation=1024./48000,ramp=.005;
   const auto residual=extremum*.625/6120.;double maximum_loss=0;
   for(unsigned bit=final_bit?2:1;bit<bits.size();++bit) {
    double cc=0,ss=0,cs=0,b00=0,b01=0,b10=0,b11=0;
    for(auto i=first+bit*symbol;i<first+(bit+1)*symbol;++i) {
     const auto t=(static_cast<double>(i)-start)/c.sample_rate;
     const auto b=t<0?original[i]:t<gap?Complex{}:t<gap+confirmation?
      interpolate(i-gap*c.sample_rate):t<gap+confirmation+ramp?
      interpolate(i-gap*c.sample_rate)*(1-smooth((t-gap-confirmation)/ramp)):
      interpolate(i-residual*c.sample_rate)*smooth((t-gap-confirmation-ramp)/ramp);
     const auto a=original[i];cc+=a.real()*a.real();ss+=a.imag()*a.imag();cs+=a.real()*a.imag();
     b00+=a.real()*b.real();b01+=a.real()*b.imag();b10+=a.imag()*b.real();b11+=a.imag()*b.imag();
    }
    for(unsigned phase=0;phase<360;++phase) {const auto p=phase*std::numbers::pi/180.;const auto x=std::cos(p),y=std::sin(p);
     const auto v0=b00*x+b01*y,v1=b10*x+b11*y;
     const auto energy=cc*x*x+2*cs*x*y+ss*y*y;
     const auto projected=(ss*v0*v0-2*cs*v0*v1+cc*v1*v1)/(cc*ss-cs*cs);
     maximum_loss=std::max(maximum_loss,10*std::log10(energy/projected));
    }
   }
   check(std::isfinite(maximum_loss)&&maximum_loss<2.,"confirmed repair exceeded conditional two-dB impairment allowance");
   std::cout<<"confirmed repair factor="<<factor<<" hopping="<<hopping<<" final_bit="<<final_bit
    <<" residual_us="<<residual*1e6<<" all_later_bits_conditional_loss_db="<<maximum_loss<<'\n';
  }
 }
}
void encrypted_echoes(bool hopping=false,unsigned models=4,bool ramp=true) {
    for(const unsigned factor:{1U,10U,100U,1000U})for(unsigned model=0;model<models;++model) {
        auto c=configuration(factor);c.sample_rate=2048;c.carrier_hz=512;c.bandwidth_hz=512./factor;
        c.integration_seconds=std::max(.5,factor/32.);c.stream_phase_samples=0;c.dsss=factor>1;
        if(hopping) {
            c.sample_rate=8192;c.carrier_hz=2048;c.bandwidth_hz=4096./factor;
            auto plan=std::make_shared<modem::FhssPlan>();plan->channels=3;plan->dwell_seconds=.4;
            plan->shifts[0]=10;plan->shifts[1]=20;plan->shifts[2]=30;plan->power_ramps=ramp;
            plan->clock_error_seconds=.001;plan->audio_error_seconds=.01;c.fake_hopping=plan;
        }
        const Bytes bits{1,0,0,1};const auto duration=modem::symbol_sample_count(c);
        for(const bool suppress:{true,false}) {
            if(!suppress && (hopping || model!=1 || factor>10))continue;
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
            if(suppress && !(pending && streams.size()==1 && accepted==bits.size() && correct==1)) {
                std::cerr<<"echo diagnostic hopping="<<hopping<<" factor="<<factor<<" model="<<model<<" pending="<<pending<<" streams="<<streams.size()<<" accepted="<<accepted<<" correct="<<correct<<'\n';
                for(const auto& [id,s]:streams){std::cerr<<"start="<<s.start<<" complete="<<s.complete<<" bits=";for(auto bit:s.bits)std::cerr<<unsigned(bit);std::cerr<<'\n';}
            }
            if(suppress)check(pending && streams.size()==1 && accepted==bits.size() && correct==1,
                "encrypted full-DSSS echo/cover admitted another stream or lost payload/completion");
            else check(accepted>bits.size(),"private no-suppression control failed to expose delayed duplicate bits");
        }
    }
}

void hop_power_boundaries() {
    for(const unsigned factor:{1U,10U,100U,1000U}) {
        auto c=configuration(factor,.8);c.sample_rate=8192;c.carrier_hz=2048;
        c.bandwidth_hz=4096./factor;c.training_seconds=2;
        auto plan=std::make_shared<modem::FhssPlan>();plan->channels=3;plan->dwell_seconds=.4;
        plan->shifts[0]=10;plan->shifts[1]=20;plan->shifts[2]=30;
        plan->power_ramps=true;c.fake_hopping=plan;
        const auto h=modem::pattern_pulse_padding_samples(c),symbol=modem::symbol_sample_count(c);
        modem::FhssPowerShape shape(*plan,c.sample_rate,h,symbol);
        check(std::abs(shape.quiet/c.sample_rate-.035L)<=1.L/c.sample_rate,
              "50 ms positive audio lag was treated as +/-50 ms");
        const Bytes bits{0,1,0,0,1,1,0,1};
        modem::PatternTransmitter tx(bits,c,c.stream_epoch),fragmented(bits,c,c.stream_epoch);
        std::vector<Complex> pcm(tx.total_samples()),pieces(pcm.size());tx.read_analytic(pcm);
        for(std::size_t i=0;i<pieces.size();) {
            const auto count=std::min<std::size_t>(1+(i*31)%433,pieces.size()-i);
            check(fragmented.read_analytic(std::span(pieces).subspan(i,count))==count,"hop source ended early");i+=count;
        }
        for(std::size_t i=0;i<pcm.size();++i)check(std::abs(pcm[i]-pieces[i])<1e-9,"hop power/schedule changed with input block boundaries");
        auto old=c;old.fake_hopping.reset();modem::PatternTransmitter reference(bits,old,c.stream_epoch);
        std::vector<Complex> original(reference.total_samples());reference.read_analytic(original);
        modem::FhssSchedule schedule(plan,*c.data_key,c.stream_epoch);
        std::size_t quiet_samples=0,unchanged=0,full_cover=0;
        const auto end=modem::training_sample_count(c)+bits.size()*symbol+2*h;
        for(std::size_t i=0;i<pcm.size();++i) {
            const auto shifted=static_cast<long double>(i)+shape.lag_center;
            const auto hop=static_cast<std::uint64_t>(std::floor(shifted/shape.period));
            const auto d=shifted-hop*shape.period;
            const bool quiet=(hop&&schedule.channel(hop-1)!=schedule.channel(hop)&&d<=shape.quiet)||
                (schedule.channel(hop)!=schedule.channel(hop+1)&&shape.period-d<=shape.quiet);
            if(quiet){check(std::abs(pcm[i])<1e-13,"nonzero output inside uncertain hop boundary");++quiet_samples;}
            if(!schedule.near_change(i,shape.affected_radius(),shape)) {
                if(i+2*h+1<original.size()) {
                    check(std::abs(pcm[i]-original[i])<1e-12,"hop ramp changed an unaffected coefficient or carrier phase");++unchanged;
                }
                if(i>=end&&i<pcm.size()-2*h-1)++full_cover;
            }
        }
        check(quiet_samples>0&&unchanged>1000,"hop test missed boundary or ordinary output");
        check(full_cover>=3*c.sample_rate,"hop gaps consumed the protected statistical cover duration");
        modem::PatternTransmitter stopped(bits,c,c.stream_epoch);
        std::vector<Complex> prefix(3333);stopped.read_analytic(prefix);stopped.request_finish();
        check((stopped.total_samples()-stopped.samples_emitted())<=modem::transmit_stop_sample_count(c),"hop stop exceeded timing controller reserve");
        std::vector<Complex> tail((stopped.total_samples()-stopped.samples_emitted()));stopped.read_analytic(tail);
        check(std::abs(tail.back())<1e-13,"hop cancellation truncated shaped shutdown");
        std::array<Complex,31> preview{};stopped.preview_last_analytic(preview);
        for(std::size_t i=0;i<preview.size();++i)check(std::abs(preview[i]-tail[tail.size()-preview.size()+i])<1e-9,
            "hop cancellation preview changed emitted pulse contributions");
        std::size_t stopped_cover=0;
        for(std::size_t i=prefix.size()+2*h;i+2*h+1<stopped.total_samples();++i)
            if(!schedule.near_change(i,shape.affected_radius(),shape))++stopped_cover;
        check(stopped_cover>=3*c.sample_rate,"hop cancellation consumed protected cover");
        check(stopped.working_bytes()<256*1024,"hop envelopes allocated a PCM symbol");
        auto impossible=*plan;impossible.audio_error_seconds=1;
        bool rejected=false;try{modem::FhssPowerShape bad(impossible,c.sample_rate,h,symbol);}catch(const Error&){rejected=true;}
        check(rejected,"impossible FHSS guard silently changed the waveform policy");
    }
}

void hop_arrival_timing() {
    transfer::Options o;o.modem=configuration(10,.8);o.modem.sample_rate=8192;
    o.modem.carrier_hz=2048;o.modem.bandwidth_hz=409.6;
    modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
    // Production Default counts BOTH endpoint clocks, even without GPS mode.
    auto default_plan=transfer::transmit_hopping(p,o);
    check(default_plan->clock_error_seconds==.01&&default_plan->peer_clock_error_seconds==.01&&
        default_plan->audio_error_seconds==.05&&default_plan->relative_clock_region_seconds()==.02,
        "default FHSS dropped an endpoint clock or changed causal audio lag");
    o.clock_sync=clock_sync::Policy{.001,.02,2.564};o.audio_timing_error_seconds=.01;
    const auto plan=transfer::transmit_hopping(p,o);
    check(plan->power_ramps&&plan->clock_error_seconds==.001&&plan->peer_clock_error_seconds==.001&&
        plan->region_seconds==.02&&plan->offset_seconds==2.564&&plan->audio_error_seconds==.01,
        "FHSS did not freeze all custom timing fields");
    const auto fs=o.modem.sample_rate;
    modem::FhssPowerShape shape(*plan,fs,modem::pattern_pulse_padding_samples(o.modem),modem::symbol_sample_count(o.modem));
    check(std::abs(shape.quiet/fs-.017L)<=1.L/fs&&std::abs(shape.lag_center/fs-.005L)<1e-12,
        "FHSS relative clock/region/causal audio formula changed");
    // Invert every RF arrival corner about RX hop N+O. Offset cancels; RX
    // capture audio is downstream of RF selection, so is not counted twice.
    for(const auto tx:{-.001L,.001L})for(const auto rx:{-.001L,.001L})
        for(const auto region:{-.01L,.01L})for(const auto lag:{0.L,.01L}) {
            const auto source=rx-tx-region-lag;
            check(std::abs(source*fs+shape.lag_center)<=shape.quiet,
                "FHSS gap missed a GPS/audio/propagation arrival corner");
        }
    auto key=*o.modem.data_key;
    modem::FhssSchedule schedule(plan,key,o.modem.stream_epoch);
    const auto offset=static_cast<long double>(fs)*plan->offset_seconds;
    const auto base=(o.modem.stream_epoch/2)*5+(o.modem.stream_epoch%2)*5/2;
    check(!schedule.arrival_ordinal(offset-.25L,fs)&&schedule.arrival_ordinal(offset,fs)==base,
        "FHSS nominal arrival did not preserve fractional offset");
    for(const std::uint64_t hop:{0ULL,1ULL,2ULL,3ULL,40ULL,100001ULL}) {
        const auto boundary=hop?(hop-(o.modem.stream_epoch%2?.5L:0.L))*shape.period:0.L;
        const auto time=offset+boundary+.25L;
        const auto arrival=schedule.arrival_ordinal(time,fs);
        check(arrival==base+hop,"propagation delay changed original TX hop index");
        modem::FhssSchedule reference(plan,key,o.modem.stream_epoch);
        check(schedule.channel(*arrival)==reference.channel(base+hop),"arrival preview reseeded the FHSS stream");
        if(hop)check(schedule.arrival_ordinal(offset+boundary-.25L,fs)==base+hop-1,
            "fractional arrival boundary advanced a hop early");
    }
    auto zero_offset=std::make_shared<modem::FhssPlan>(*plan);zero_offset->offset_seconds=0;
    auto c=o.modem;c.fake_hopping=plan;auto before=c;before.fake_hopping=zero_offset;
    modem::PatternTransmitter delayed(Bytes{0,1},c,c.stream_epoch),local(Bytes{0,1},before,c.stream_epoch);
    schedule.source_origin_samples=static_cast<long double>(c.stream_phase_samples)-modem::training_sample_count(c)-modem::pattern_pulse_padding_samples(c);
    check(delayed.total_samples()==local.total_samples(),"fixed propagation offset added transmit airtime");
    std::array<Complex,131> a{},b{};
    while(!delayed.finished()) {
        const auto count=delayed.read_analytic(a);check(local.read_analytic(b)==count,"offset changed TX block length");
        for(std::size_t i=0;i<count;++i)check(a[i]==b[i],"offset moved transmitted waveform or private addresses");
        modem::TransmitTrace trace;delayed.copy_transmit_trace(trace,1);
        const auto expected=schedule.arrival_ordinal(delayed.samples_emitted()-1,fs);
        check(trace.fhss_arrival_available==expected.has_value(),"arrival trace availability mismatch");
        if(expected)check(trace.fhss_arrival_hop==*expected&&trace.fhss_arrival_shift_hz==plan->shifts[schedule.channel(*expected)],
            "GUI arrival preview disagrees with keyed delayed schedule");
    }
    o.clock_sync->region_seconds=.4;
    const auto smooth=transfer::transmit_hopping(p,o);
    check(smooth->power_ramps&&smooth->smooth_ramps&&!smooth->remote_guard_covered,"wide propagation region prevented safe local ramps");
    const auto unsupported=modem::resolve_fhss_pulse_power(*smooth,o.modem);
    check(!unsupported->power_ramps&&!unsupported->power_fallback_reason.empty()&&unsupported->offset_seconds==2.564,
        "a full-dwell propagation region silently narrowed its FHSS guard or lost offset");
}

void deferred_calendar(unsigned factor=1) {
    for(const unsigned rate:{64U,1024U,6000U,48000U}) {
        modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.power_ramps=true;p.defer_symbols=true;
        p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
        p.clock_error_seconds=p.peer_clock_error_seconds=.01;p.audio_error_seconds=.05;
        const auto symbol=std::max(1U,rate/20),half=std::max(1U,rate/1000);
        modem::FhssSymbolCalendar cal(p,rate,half,symbol);
        for(const auto epoch:{0ULL,1ULL,1800000321ULL})for(const auto phase:{0U,rate/3,rate-1}) {
            const auto rank=cal.first_rank(epoch,phase);const auto tick=epoch*rate+phase;
            for(std::uint64_t i=0;i<100;++i) {
                const auto start=cal.slot(rank+i);
                check(start>=tick,"FHSS calendar moved a bit backwards");
                check(cal.first_rank(start/rate,start%rate)==rank+i,"FHSS calendar cannot restart midstream");
                check(cal.offset(start/rate,start%rate,37)==cal.slot(rank+i+37)-start,
                    "FHSS calendar restart changed later private addresses");
                check(cal.lower_bound(epoch,phase,start-tick)==i,"FHSS calendar inverse lost a deferred bit");
                if(i)check(start-cal.slot(rank+i-1)>=symbol,"FHSS calendar reused a symbol address");
                const auto hop=(rank+i)/cal.per_hop;
                check(start>=cal.boundary(hop,true)+cal.left &&
                    start+symbol<=cal.boundary(hop+1)-cal.right,"FHSS calendar clipped a complete shaped symbol");
            }
        }
    }
    {
        modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.power_ramps=p.defer_symbols=true;
        p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
        p.clock_error_seconds=p.peer_clock_error_seconds=p.audio_error_seconds=0;
        bool rejected=false;try{modem::FhssSymbolCalendar impossible(p,1003,8,385);}
        catch(const Error&){rejected=true;}
        check(rejected,"fractional dwell rounding admitted a one-sample-too-long symbol");
        const Crypto key(Bytes(32,0x3d));auto plan=std::make_shared<modem::FhssPlan>(p);
        plan->offset_seconds=2.564;
        modem::FhssSchedule hops(plan,key,1800000321);hops.source_origin_samples=-2037;hops.source_rate=1003;
        const auto origin=1800000321ULL*1003-2037;
        for(const auto sample:{0ULL,1ULL,500ULL,3000ULL,100000001ULL}) {
            const auto tick=origin+sample;
            const auto expected=(tick/2006)*5+(tick%2006)*5/2006;
            check(hops.ordinal(sample,1003)==expected,"UTC hop ordinal lost fractional dwell or phase");
            check(hops.arrival_ordinal(sample+static_cast<long double>(plan->offset_seconds)*1003,1003)==expected,"UTC arrival offset changed hop identity");
        }
    }
    transfer::Options o;o.timestamp=1800000321;o.key.emplace(Bytes(32,0x65));
    auto& c=o.modem;c.sample_rate=4096*factor;c.carrier_hz=1024*factor;c.bandwidth_hz=2048;c.integration_seconds=.0625;
    c.dsss_factor=factor;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
    c.scramble=true;
    modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
    c.fake_hopping=transfer::transmit_hopping(p,o);
    if(!c.fake_hopping->defer_symbols||!c.fake_hopping->power_ramps)throw Error("short FHSS bits were not deferred: "+c.fake_hopping->power_fallback_reason);
    c=transfer::seeded_config(o,o.timestamp);
    const Bytes bits{0,0,1,1,0,1,0,1};
    auto wire=bits;transfer::xor_binary_bits(wire,o);
    modem::PatternTransmitter tx(wire,c,o.timestamp),pieces(wire,c,o.timestamp);
    std::vector<float> pcm(tx.total_samples()),split(pcm.size());tx.read(pcm);
    std::size_t emitted=0;while(emitted<split.size())emitted+=pieces.read(std::span(split).subspan(emitted,std::min<std::size_t>(113,split.size()-emitted)));
    double block_error=0;for(std::size_t i=0;i<pcm.size();++i)block_error=std::max(block_error,std::abs(double(pcm[i])-split[i]));
    std::cout<<"deferred block peak error "<<block_error<<'\n';
    check(block_error<1e-6,"FHSS deferred PCM changed at a block boundary");
    const auto estimate=transfer::estimate_binary(bits,o);
    check(estimate.waveform_samples==pcm.size(),"FHSS deferred airtime estimate differs from TX");
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.start_offset_seconds=2;
    search.start_uncertainty_seconds=1;search.initial_stream_symbols=1;search.chunk_bits=1;
    modem::PatternReceiver rx(c,32*1024*1024,search);
    std::vector<float> observed=pcm;observed.resize(observed.size()+modem::pattern_absence_capture_samples(c)+c.sample_rate);
    Bytes received;bool complete=false;
    for(std::size_t first=0;first<observed.size();first+=113) {
        rx.push(std::span(observed).subspan(first,std::min<std::size_t>(113,observed.size()-first)));
        for(const auto& event:rx.take_bursts()) {
            check(!event.missing_slots,"scheduled FHSS wait became missing bits");
            auto plain=event.bits;auto decoding=o;decoding.modem.stream_phase_samples=event.stream_phase_samples;
            transfer::xor_binary_bits(plain,decoding,event.first_stream_symbol);
            received.insert(received.end(),plain.begin(),plain.end());complete|=event.complete;
        }
    }
    check(received==bits,"deferred FHSS did not preserve every logical payload bit");
    check(complete,"deferred FHSS did not observe complete eligible absence");
    // Six wall seconds across scheduled holes are NOT six eligible seconds.
    const auto prefix=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c);
    const auto last=prefix+modem::scheduled_symbol_offset(c,o.timestamp,c.stream_phase_samples,bits.size()-1)+modem::symbol_sample_count(c);
    modem::PatternReceiver early(c,32*1024*1024,search);
    const auto stop=std::min<std::size_t>(observed.size(),last+6*c.sample_rate);
    for(std::size_t i=0;i<stop;i+=113) {
        early.push(std::span(observed).subspan(i,std::min<std::size_t>(113,stop-i)));
        for(const auto& event:early.take_bursts())check(!event.complete,"scheduled holes manufactured six-second absence");
    }
    early.finish();for(const auto& event:early.take_bursts())check(!event.complete,"EOF completed a deferred stream");
    // Acquire a later bit using only its current UTC epoch, not the original
    // message epoch or a remembered hop phase. Search every canonical slot.
    constexpr std::size_t begin_bit=5;
    const auto address=modem::symbol_stream_address(c,o.timestamp,c.stream_phase_samples,begin_bit);
    auto mid_options=o;mid_options.timestamp=address.epoch;mid_options.modem.stream_phase_samples=0;
    const auto mid_config=transfer::seeded_config(mid_options,address.epoch);
    const auto cut=prefix+modem::scheduled_symbol_offset(c,o.timestamp,c.stream_phase_samples,begin_bit)-modem::pattern_pulse_padding_samples(c);
    auto mid_search=search;mid_search.search_stream_phases=true;mid_search.initial_stream_symbols=4;
    mid_search.start_offset_seconds=static_cast<double>(address.epoch-o.timestamp)+(static_cast<double>(prefix)-cut)/c.sample_rate;
    modem::PatternReceiver mid(mid_config,32*1024*1024,mid_search);Bytes suffix;
    auto uncached_search=mid_search;uncached_search.reuse_deferred_templates=false;
    modem::PatternReceiver uncached(mid_config,32*1024*1024,uncached_search);
    for(std::size_t first=cut;first<observed.size();first+=113) {
        const auto block=std::span(observed).subspan(first,std::min<std::size_t>(113,observed.size()-first));
        mid.push(block);uncached.push(block);
        const auto events=mid.take_bursts(),references=uncached.take_bursts();
        check(events.size()==references.size(),"exact FHSS cache changed next-poll event count");
        for(std::size_t e=0;e<events.size();++e) {
            const auto& event=events[e];const auto& reference=references[e];
            check(event.bits==reference.bits && event.complete==reference.complete &&
                event.missing_slots==reference.missing_slots && event.first_stream_symbol==reference.first_stream_symbol &&
                event.stream_phase_samples==reference.stream_phase_samples && event.end_sample==reference.end_sample,
                "exact FHSS cache changed decisions, identity or physical completion");
            auto plain=event.bits;auto context=mid_options;context.modem.stream_phase_samples=event.stream_phase_samples;
            transfer::xor_binary_bits(plain,context,event.first_stream_symbol);suffix.insert(suffix.end(),plain.begin(),plain.end());
        }
    }
    check(mid.fft_work().threshold_trials==uncached.fft_work().threshold_trials &&
        mid.fft_work().scored_start_trials==uncached.fft_work().scored_start_trials,
        "exact FHSS cache changed trial/threshold accounting");
    check(mid.working_bytes()<=32*1024*1024 && uncached.working_bytes()<=32*1024*1024,
        "deferred acquisition exceeded its workspace");
    check(suffix==Bytes(bits.begin()+begin_bit,bits.end()),"deferred UTC midstream acquisition changed logical bits");
    if(factor==10) {
        // The optional exact calendar cache may replace originally streamed
        // variable-clock rows. Shrinking must restore streaming, not allocate
        // those formerly omitted rows after the cache reservation disappears.
        auto pressure_search=mid_search;pressure_search.prefer_streamed_templates=true;
        pressure_search.clock_errors_ppm={0,10};
        auto control_search=pressure_search;control_search.reuse_deferred_templates=false;
        modem::PatternReceiver pressure(mid_config,32*1024*1024,pressure_search);
        modem::PatternReceiver control(mid_config,32*1024*1024,control_search);
        const auto allowance=control.reserved_workspace_bytes()+65536;
        check(pressure.working_bytes()>allowance,"calendar eviction fixture did not allocate an optional cache");
        pressure.set_workspace_bytes(allowance);control.set_workspace_bytes(allowance);
        for(std::size_t first=cut;first<observed.size();first+=113) {
            const auto block=std::span(observed).subspan(first,std::min<std::size_t>(113,observed.size()-first));
            pressure.push(block);control.push(block);
            check(pressure.working_bytes()<=allowance,"evicted calendar cache allocated unreserved transforms");
            const auto events=pressure.take_bursts(),references=control.take_bursts();
            check(events.size()==references.size(),"calendar cache eviction changed publication");
            for(std::size_t e=0;e<events.size();++e)check(events[e].bits==references[e].bits &&
                events[e].complete==references[e].complete && events[e].end_sample==references[e].end_sample,
                "calendar cache eviction changed decisions or completion");
        }
    }
    // Cancellation during a planned wait must retain every emitted pulse.
    modem::PatternTransmitter cancelled(wire,c,o.timestamp);
    const auto pause=prefix+modem::scheduled_symbol_offset(c,o.timestamp,c.stream_phase_samples,4)-8;
    std::vector<float> before(pause);cancelled.read(before);const auto memory=cancelled.working_bytes();cancelled.request_finish();
    std::array<Complex,31> preview{};cancelled.preview_last_analytic(preview);
    for(std::size_t i=0;i<preview.size();++i)check(std::abs(preview[i].real()-before[pause-preview.size()+i])<1e-6,
        "FHSS cancellation rewrote an emitted pulse");
    std::array<float,113> tail{};while(!cancelled.finished())cancelled.read(tail);
    check(cancelled.working_bytes()==memory,"FHSS cancellation expanded memory with waiting duration");
    modem::NoiseTransmitter tuning(c);check(tuning.total_samples()<=modem::NoiseTransmitter::sample_target,
        "FHSS waits exceeded the tuning source safety horizon");
}
void matched_hop_gaps(unsigned factor,bool long_symbol=false,bool broad=false,bool compact=false) {
    auto c=configuration(factor,long_symbol?60.:.5);
    c.sample_rate=broad?1440:long_symbol?1024:2048;c.carrier_hz=c.sample_rate/4.;
    c.bandwidth_hz=c.sample_rate/(2.*factor);c.stream_phase_samples=broad?65:313;
    if(broad)c.integration_seconds=512./1440.;
    c.dsss=factor>1;
    modem::FhssPlan request;request.channels=3;request.dwell_seconds=.4;
    request.shifts[0]=10;request.shifts[1]=20;request.shifts[2]=30;
    request.clock_error_seconds=request.peer_clock_error_seconds=.01;
    c.fake_hopping=modem::resolve_fhss_pulse_power(request,c);
    check(c.fake_hopping->power_ramps && c.fake_hopping->matched_gaps && !c.fake_hopping->defer_symbols,
        "matched-gap fixture did not select pulse puncturing");
    const auto h=modem::pattern_pulse_padding_samples(c),s=modem::symbol_sample_count(c);
    const auto pre=modem::training_sample_count(c);const Bytes bits=long_symbol?Bytes{1,0}:Bytes{1,0,0,1};
    modem::PatternTransmitter source(bits,c,c.stream_epoch),split(bits,c,c.stream_epoch);
    std::vector<Complex> analytic(source.total_samples()),blocked(analytic.size());source.read_analytic(analytic);
    for(std::size_t at=0;at<blocked.size();at+=113)split.read_analytic(std::span(blocked).subspan(at,std::min<std::size_t>(113,blocked.size()-at)));
    double block_error=0;for(std::size_t i=0;i<analytic.size();++i)block_error=std::max(block_error,std::abs(analytic[i]-blocked[i]));
    std::cout<<"matched block difference "<<block_error<<'\n';
    check(block_error<1e-7,"matched FHSS block split changed PCM beyond output rounding");
    const modem::FhssPowerShape shape(*c.fake_hopping,c.sample_rate,h,s);
    std::size_t quiet=0;double power=0;
    for(std::size_t i=0;i<analytic.size();++i) {
        power+=std::norm(analytic[i]);
        if(modem::fhss_utc_quiet(c.stream_epoch,static_cast<long double>(c.stream_phase_samples)+i-pre-h,
                c.sample_rate,shape.quiet,shape.lag_center)) {
            ++quiet;check(std::abs(analytic[i])<1e-12,"production FHSS emitted through an uncertain tuner boundary");
        }
    }
    check(quiet>analytic.size()/10 && power>1,"mandatory gate erased the full transmission or skipped guards");
    // Independent ungated coefficients plus exact pulse exclusion must equal
    // each receiver template, including fractional sample positions and tails.
    auto original=c;original.fake_hopping.reset();modem::PatternCode raw(original,c.stream_epoch),gated(c,c.stream_epoch);
    for(std::uint64_t symbol=0;symbol<2;++symbol)for(double at=-double(h);at<double(s+h);at+=17.375) {
        std::array<Complex,2> expected{};
        const auto address=modem::symbol_stream_address(c.stream_epoch,c.stream_phase_samples,symbol,s,c.sample_rate);
        modem::pattern_pulse_each(at,s,gated.chip_samples(),[&](auto k,double pulse) {
            const auto off=k*gated.chip_samples();
            const auto center=static_cast<long double>(address.sample_in_second)+off+.5L*std::min(gated.chip_samples(),s-off);
            if(modem::fhss_utc_quiet(address.epoch,center,c.sample_rate,shape.coefficient_radius(),shape.lag_center))return;
            for(unsigned bit=0;bit<2;++bit)expected[bit]+=pulse*raw.value(symbol*raw.chips_per_symbol()+k,bit);
        });
        const auto actual=gated.shaped_values(symbol*gated.chips_per_symbol(),at);
        for(unsigned bit=0;bit<2;++bit)check(std::abs(actual[bit]-expected[bit])<1e-10,"gated receiver template disagrees with pulse construction");
    }
    const auto absent=static_cast<std::uint64_t>(std::ceil(6.*c.sample_rate/s))*s;
    std::vector<float> pcm(analytic.size()+absent+2*c.sample_rate);
    for(std::size_t i=0;i<analytic.size();++i)pcm[i]=static_cast<float>(analytic[i].real());
    std::mt19937_64 rng(7183);std::normal_distribution<double> noise(0,1e-4);
    for(auto& x:pcm)x+=static_cast<float>(noise(rng));
    modem::PatternSearch search;search.frequency_offsets_hz={0};search.clock_errors_ppm={0};
    search.initial_stream_symbols=1;search.start_offset_seconds=double(pre+h)/c.sample_rate;
    search.start_uncertainty_seconds=broad&&!compact?.02:0;search.search_stream_phases=broad;
    search.compact_clock_search=long_symbol||compact;
    if(long_symbol) {
        search.search_stream_phases=true;
        search.qualified_start_window=modem::PatternStartWindow{
            static_cast<long double>(pre+h)-c.stream_phase_samples,1,.2L};
    }
    modem::PatternReceiver rx(c,32*1024*1024,search);
    std::map<std::pair<std::uint64_t,std::uint64_t>,Bytes> streams;bool complete=false;
    std::uint64_t completion=0;std::vector<std::size_t> publication;
    for(std::size_t at=0;at<pcm.size();at+=113) {
        rx.push(std::span(pcm).subspan(at,std::min<std::size_t>(113,pcm.size()-at)));
        for(const auto& event:rx.take_bursts()) {
            auto& got=streams[{event.stream_first_sample,event.stream_first_symbol}];
            check(!event.missing_slots && event.first_stream_symbol==got.size(),"matched gaps changed pending identity or lost a bit");
            got.insert(got.end(),event.bits.begin(),event.bits.end());
            check(got.size()<=bits.size() && std::equal(got.begin(),got.end(),bits.begin()),"matched cover admitted wrong or extra bits");
            if(!event.bits.empty())publication.push_back(at+113);
            if(event.complete){complete=true;completion=at+std::min<std::size_t>(113,pcm.size()-at);}
        }
    }
    check(complete && streams.size()==1 && streams.begin()->second==bits,"matched gap reception lost payload or physical completion");
    check(completion>=pre+h+bits.size()*s+absent,"hop silence manufactured observed absence");
    check(rx.working_bytes()<=32*1024*1024,"matched phase bank exceeded workspace");
    if(factor==10 && !long_symbol && !broad) {
        for(bool wrong_outer:{true,false}) {
            auto wrong=c;if(wrong_outer)wrong.dsss_seed[0]^=1;
            modem::PatternReceiver rejected(wrong,32*1024*1024,search);
            std::mt19937_64 finite_rng(1031);std::normal_distribution<float> finite_noise(0,.01f);
            for(std::size_t at=0;at<pcm.size();at+=113) {
                std::array<float,113> block{};const auto n=std::min<std::size_t>(block.size(),pcm.size()-at);
                for(std::size_t i=0;i<n;++i)block[i]=wrong_outer?pcm[at+i]:finite_noise(finite_rng);
                rejected.push(std::span(block).first(n));
                check(rejected.take_bursts().empty(),"gated wrong DSSS key/noise admitted a stream");
            }
        }
    }
    std::cout<<"matched FHSS factor="<<factor<<" long="<<long_symbol<<" broad="<<broad
        <<" compact="<<compact<<" seconds="<<analytic.size()/double(c.sample_rate)<<" quiet="<<quiet
        <<" power="<<power/analytic.size()<<" rx_bytes="<<rx.working_bytes()<<" publications="<<publication.size()<<'\n';
}
void omitted_pulse_evidence() {
    using namespace modem::detail;
    CorrelationChipEvidence evidence;
    // Two unrelated, strong chip phases separated by a deliberately zero
    // template cell. Zero template energy must not disable the colored guard.
    CorrelationFit a;a.cc=a.ss=100;a.xc=1000;a.energy=10001;a.count=1000;
    CorrelationFit b=a;b.xc=-1000;
    CorrelationFit gap;gap.energy=1;gap.count=1000;
    evidence.add_fit(a,0);evidence.add_fit(gap,1);evidence.add_fit(b,2);
    CorrelationFit total;total.cc=total.ss=200;total.energy=20003;total.count=3000;
    check(evidence.nested && evidence.colored_excess(total,20),
        "zero-dimensional FHSS pulse disabled structured wrong-code rejection");
    evidence.add_fit(gap,3);
    check(evidence.colored_excess(total,20),"last omitted pulse disabled colored guard");
    check(total.count==3000 && total.energy==20003,"gaps erased observed noise degrees");
    a.ss=0;CorrelationChipEvidence singular;singular.retain(a);
    check(!singular.nested,"near-singular nonzero evidence lost conservative fallback");
}
// Fixed seed9018 exposed a five-bit wrong-key weak chain in gated cover.
// Keep every stream/assertion; the waveform including its suppression passes
// through the same fractional, clock, frequency, phase and interference channel.
void matched_gap_channel_stress(bool smooth=false) {
 for(unsigned factor:{1U,10U,100U,1000U}){
  transfer::Options o;o.timestamp=1800000321;o.key.emplace(Bytes(32,0x65));auto&c=o.modem;c.sample_rate=2048;c.carrier_hz=512;c.bandwidth_hz=1024./factor;c.integration_seconds=factor<100?.5:4.;c.stream_phase_samples=313;c.scramble=true;c.dsss=factor>1;c.dsss_factor=factor;c.outer_dsss_version=modem::OuterDsssVersion::interleaved_v2;
  modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;c.fake_hopping=transfer::transmit_hopping(p,o);if(!smooth)c.fake_hopping=modem::resolve_fhss_pulse_power(*c.fake_hopping,c);c=transfer::seeded_config(o,o.timestamp);
  Bytes bits{1,0,0,1};modem::PatternTransmitter tx(bits,c,c.stream_epoch);std::vector<std::complex<double>> pcm(tx.total_samples());tx.read_analytic(pcm);
  auto s=modem::symbol_sample_count(c),h=modem::pattern_pulse_padding_samples(c),pre=modem::training_sample_count(c);
  for(int model:{0,1,2,3}){
   // Windowed-sinc fractional source samples, small clock/frequency boundaries,
   // phase diffusion, and a stationary narrow interferer. No changed RX threshold.
   const double ppm=model==0?200:model==1?-200:0,rate=1+ppm*1e-6,df=model==0?.125:model==1?-.125:0;
   const double start=.375;size_t n=static_cast<size_t>(std::ceil(pcm.size()/rate+start))+static_cast<size_t>(std::ceil(6.*c.sample_rate/s))*s+2*c.sample_rate;
   std::vector<float> x(n);std::mt19937_64 rng(9018);std::normal_distribution<double> gaussian(0,1);double phase=0;
   for(size_t i=0;i<n;++i){auto a=(i-start)*rate;std::complex<double> val{};
    if(a>=0&&a+1<pcm.size()){
     const auto at=static_cast<long long>(std::floor(a));double norm=0;
     const auto sinc=[](double v){return std::abs(v)<1e-12?1.:std::sin(std::numbers::pi*v)/(std::numbers::pi*v);};
     for(int k=-15;k<=16;++k){auto index=at+k;const auto d=a-index;auto w=sinc(d)*sinc(d/16);norm+=w;if(index>=0&&index<static_cast<long long>(pcm.size()))val+=w*pcm[index];}
     val/=norm;
    }
    phase+=gaussian(rng)*.25*std::numbers::pi/180/std::sqrt(c.sample_rate);
    x[i]=model==3?0:static_cast<float>((val*std::polar(1.,2*std::numbers::pi*(df-c.carrier_hz*(rate-1))*i/c.sample_rate+phase)).real());
    x[i]+=static_cast<float>(gaussian(rng)*.01+.0005*std::sin(2*std::numbers::pi*311*i/c.sample_rate));
   }
   auto rc=c;if(model==2){rc.spreading_seed[0]^=1;rc.dsss_seed[0]^=1;}
   modem::PatternSearch search;search.frequency_offsets_hz={-.125,0,.125};search.clock_errors_ppm={-200,0,200};search.initial_stream_symbols=1;search.start_offset_seconds=(pre+h)/(rate*c.sample_rate)+start/c.sample_rate;search.start_uncertainty_seconds=0;search.search_stream_phases=false;
   modem::PatternReceiver rx(rc,32*1024*1024,search);std::map<std::pair<uint64_t,uint64_t>,Bytes> streams;bool complete=false;size_t pub=0;
   for(size_t at=0;at<n;at+=113){rx.push(std::span(x).subspan(at,std::min<size_t>(113,n-at)));for(const auto&e:rx.take_bursts()){auto&got=streams[{e.stream_first_sample,e.stream_first_symbol}];if(e.missing_slots||e.first_stream_symbol!=got.size())throw Error("stress pending identity");got.insert(got.end(),e.bits.begin(),e.bits.end());pub+=!e.bits.empty();complete|=e.complete;}}
   if(model>=2){if(!streams.empty())throw Error("FHSS wrong key/noise admitted a stream");}
   else if(streams.size()!=1||streams.begin()->second!=bits||!complete)throw Error("FHSS fractional/clock/frequency stress lost bits");
   std::cout<<"factor="<<factor<<" model="<<model<<" pubs="<<pub<<" workspace="<<rx.working_bytes()<<'\n';
  }
 }
}
void smooth_envelope() {
    // Short scoring intervals retain unity gain for EVERY sample, including
    // source phases near a hop. A long finite pulse tail cannot force a mostly
    // muted bit merely because the tail would not fit inside one dwell.
    for(const auto rate:{1003U,2048U})for(const auto audio:{0.,.01,.05}) {
        modem::FhssPlan plan;plan.channels=2;plan.shifts[1]=1;plan.dwell_seconds=.4;
        plan.power_ramps=plan.smooth_ramps=true;plan.ramp_seconds=.01;
        plan.clock_error_seconds=.01;plan.peer_clock_error_seconds=.01;
        plan.region_seconds=.001;plan.audio_error_seconds=audio;
        const auto symbol=static_cast<std::uint64_t>(.12*rate);
        modem::FhssSymbolCalendar calendar(plan,rate,rate/10,symbol);
        modem::FhssPowerShape shape(plan,rate,rate/10,symbol);
        for(unsigned phase=0;phase<rate;phase+=17)for(unsigned bit=0;bit<7;++bit) {
            const auto start=calendar.offset(1800000321,phase,bit)+phase;
            for(std::uint64_t i=0;i<symbol;++i)
                check(shape.gain(1800000321,start+i,rate)==1.,"deferred short bit entered quiet interval or ramp");
        }
        // Numeric independent integration checks the advertised worst flat
        // power bound (not an assertion about a random template's energy).
        for(const auto duration:{.12,.3,.4,.5,1.2}) {
            double worst=1.;const auto samples=static_cast<unsigned>(duration*rate);
            for(unsigned phase=0;phase<2*rate;phase+=7) {
                double sum=0;for(unsigned i=0;i<samples;++i){const auto g=shape.gain(0,phase+i+.5,rate);sum+=g*g;}
                worst=std::min(worst,sum/samples);
            }
            check(worst+2./samples>=shape.minimum_flat_power_fraction(samples),"flat-power loss bound overstates retained energy");
        }
    }
    for(unsigned factor:{1U,10U,100U,1000U}) {
        auto c=configuration(factor,4.);c.sample_rate=2048;c.carrier_hz=512;
        c.bandwidth_hz=1024./factor;c.stream_phase_samples=313;c.dsss=factor>1;
        modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
        c.fake_hopping=modem::resolve_fhss_power(p,c);
        check(c.fake_hopping->smooth_ramps&&c.fake_hopping->power_ramps&&!c.fake_hopping->matched_gaps,
            "smooth envelope did not preserve full-template search");
        auto plain=c;plain.fake_hopping.reset();
        check(modem::canonical_phase_step(c)==modem::canonical_phase_step(plain),"smooth FHSS expanded private phase bank");
        modem::PatternCode a(c,c.stream_epoch),b(plain,c.stream_epoch);
        for(unsigned bit=0;bit<2;++bit)for(std::uint64_t j=0;j<a.chips_per_symbol();++j)
            check(a.value(j,bit)==b.value(j,bit),"smooth FHSS changed a private coefficient/address");
        modem::PatternTransmitter tx(Bytes{1,0},c,c.stream_epoch),split(Bytes{1,0},c,c.stream_epoch),raw(Bytes{1,0},plain,c.stream_epoch);
        std::vector<Complex> whole(tx.total_samples()),blocks(whole.size()),reference(raw.total_samples());
        tx.read_analytic(whole);raw.read_analytic(reference);
        for(std::size_t at=0;at<blocks.size();at+=113)split.read_analytic(std::span(blocks).subspan(at,std::min<std::size_t>(113,blocks.size()-at)));
        const auto pre=modem::training_sample_count(c),h=modem::pattern_pulse_padding_samples(c),sym=modem::symbol_sample_count(c);
        modem::FhssPowerShape shape(*c.fake_hopping,c.sample_rate,h,sym);
        std::size_t zeros=0;
        for(std::size_t i=0;i<whole.size();++i) {
            const auto gain=shape.gain(c.stream_epoch,static_cast<long double>(c.stream_phase_samples)+i-pre-h,c.sample_rate);
            check(gain>=0&&gain<=1,"envelope gain escaped unit interval");
            check(std::abs(whole[i]-blocks[i])<1e-7,"smooth envelope depends on output blocks");
            if(!gain){++zeros;check(whole[i]==Complex{},"smooth envelope did not mute exact hop interval");}
            if(i<pre+2*sym)check(std::abs(whole[i]-reference[i]*gain)<1e-7,"source envelope changed carrier/shaper before multiplication");
        }
        check(zeros>100,"quiet interval oracle exercised no samples");
        check(tx.working_bytes()<500000,"smooth envelope buffered a full long waveform");
    }
    auto c=configuration(1,120);c.bandwidth_hz=1;c.sample_rate=1024;c.carrier_hz=256;
    modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
    c.fake_hopping=modem::resolve_fhss_power(p,c);
    check(c.fake_hopping->power_ramps&&c.fake_hopping->ramp_seconds==.01,"long original pulse still blocked smooth FHSS");
    const auto coarse=modem::pattern_pulse_padding_samples(c);
    modem::FhssPowerShape shape(*c.fake_hopping,c.sample_rate,coarse,modem::symbol_sample_count(c));
    check(shape.support<=11,"narrow waveform expanded ramp to its original pulse support");
    modem::PatternTransmitter tx(Bytes{1},c,c.stream_epoch);std::vector<Complex> first(2*c.sample_rate);tx.read_analytic(first);
    tx.request_finish();check(tx.total_samples()>tx.samples_emitted(),"smooth cancellation truncated protected cover");
    // The remote guard fits in continuous seconds but not after rounding to
    // low-rate source samples. Select the explicit local guard before shaping.
    c.sample_rate=64;c.carrier_hz=16;c.stream_phase_samples=0;p.peer_clock_error_seconds=.01;p.region_seconds=.28;
    c.fake_hopping=modem::resolve_fhss_power(p,c);
    check(c.fake_hopping->power_ramps&&!c.fake_hopping->remote_guard_covered,
        "rounded remote guard blocked a feasible local envelope");
}
void qualified_cipher_phase_cells() {
    for(const bool compact:{false,true})for(const bool hopping:{false,true}) {
        auto c=configuration(compact?1000:10,compact?20.:.5);
        c.sample_rate=2048;c.carrier_hz=512;c.bandwidth_hz=1024./c.dsss_factor;c.stream_phase_samples=313;
        if(hopping){modem::FhssPlan p;p.channels=2;p.shifts[1]=1;p.dwell_seconds=.4;c.fake_hopping=modem::resolve_fhss_power(p,c);}
        const Bytes bits{1,0};modem::PatternTransmitter tx(bits,c,c.stream_epoch);
        std::vector<float> pcm(tx.total_samples());tx.read(pcm);
        const auto pre=modem::training_sample_count(c)+modem::pattern_pulse_padding_samples(c),s=modem::symbol_sample_count(c);
        pcm.resize(pcm.size()+static_cast<std::size_t>(std::ceil(6.*c.sample_rate/s))*s+2*c.sample_rate);
        modem::PatternSearch search;search.hypotheses={{0,0}};search.initial_stream_symbols=1;
        search.start_offset_seconds=double(pre)/c.sample_rate;search.start_uncertainty_seconds=0;
        search.search_stream_phases=true;search.compact_clock_search=compact;
        search.qualified_start_window=modem::PatternStartWindow{static_cast<long double>(pre)-313,1,.2};
        modem::PatternReceiver rx(c,32*1024*1024,search);Bytes got;bool complete=false;
        for(std::size_t at=0;at<pcm.size();at+=113){rx.push(std::span(pcm).subspan(at,std::min<std::size_t>(113,pcm.size()-at)));
            for(const auto& e:rx.take_bursts()){check(!e.missing_slots&&e.first_stream_symbol==got.size(),"qualified phase cell changed bit identity");got.insert(got.end(),e.bits.begin(),e.bits.end());complete|=e.complete;}}
        check(got==bits&&complete,"qualified cipher cell omitted a noncanonical physical phase");
        if(!compact)check(rx.fft_work().template_jobs<100,"qualified cipher cell multiplied template banks");
    }
}
void mandatory_gate_rejection() {
    auto c=configuration(1,120);c.bandwidth_hz=1;c.sample_rate=1024;c.carrier_hz=256;
    modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
    c.fake_hopping=modem::resolve_fhss_pulse_power(p,c);
    check(c.fake_hopping->mandatory_ramps && !c.fake_hopping->power_ramps && !c.fake_hopping->power_fallback_reason.empty(),
        "unusable pulse support was not identified");
    bool unresolved=false;transfer::Options production;production.key.emplace(Bytes(32,0x65));
    production.modem=c;production.modem.fake_hopping=std::make_shared<modem::FhssPlan>(p);
    try{transfer::binary_transmitter(Bytes{1},production);}catch(const Error& e){unresolved=std::string_view(e.what()).find("resolved mandatory power plan")!=std::string_view::npos;}
    check(unresolved,"unresolved production API plan bypassed mandatory ramps");
    auto malformed=std::make_shared<modem::FhssPlan>(p);malformed->mandatory_ramps=malformed->power_ramps=true;
    production.modem.fake_hopping=malformed;bool diagnostic=false;
    try{transfer::binary_transmitter(Bytes{1},production);}catch(const Error& e){diagnostic=std::string_view(e.what()).find("resolved mandatory power plan")!=std::string_view::npos;}
    check(diagnostic,"diagnostic TX-relative hop plan entered production");
    bool message=false,noise=false;
    try{modem::PatternTransmitter tx(Bytes{1},c,c.stream_epoch);}catch(const Error& e){message=std::string_view(e.what()).find("FHSS transmission blocked")!=std::string_view::npos;}
    try{modem::NoiseTransmitter tx(c);}catch(const Error& e){noise=std::string_view(e.what()).find("FHSS transmission blocked")!=std::string_view::npos;}
    check(message&&noise,"production emitted ungated audio on impossible FHSS geometry");
}
void production_boundaries() {
    {
        transfer::Options o;o.modem=configuration(10,.5);o.modem.sample_rate=2048;
        o.modem.carrier_hz=512;o.modem.bandwidth_hz=51.2;
        modem::FhssPlan p;p.channels=3;p.dwell_seconds=.4;p.shifts[0]=10;p.shifts[1]=20;p.shifts[2]=30;
        const auto smooth=transfer::transmit_hopping(p,o);
        check(smooth->power_ramps&&smooth->smooth_ramps&&!smooth->matched_gaps,"production did not select bounded smooth envelope");
        const auto resolved=modem::resolve_fhss_pulse_power(*smooth,o.modem);
        check(resolved->power_ramps&&resolved->matched_gaps&&!resolved->defer_symbols,
            "formerly mismatched partial-symbol geometry did not enable exact gated templates");
    }
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
int main(int argc,char** argv) {
    try {if(argc==2&&std::string_view(argv[1])=="--xrun") {finish_paths();xrun_erasure_evidence();forward_repair_evidence();confirmed_forward_repair_evidence();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--smooth") {smooth_envelope();qualified_cipher_phase_cells();matched_gap_channel_stress(true);return 0;}
        if(argc==2&&std::string_view(argv[1])=="--matched-stress") {matched_gap_channel_stress();return 0;}
        if(argc==2&&std::string_view(argv[1])=="--matched") {
            smooth_envelope();matched_gap_channel_stress(true);matched_gap_channel_stress();omitted_pulse_evidence();mandatory_gate_rejection();matched_hop_gaps(1);matched_hop_gaps(10);matched_hop_gaps(10,false,true);matched_hop_gaps(10,false,true,true);matched_hop_gaps(1,true);matched_hop_gaps(10,true);matched_hop_gaps(100,true);matched_hop_gaps(1000,true);return 0;}
        if(argc==2&&std::string_view(argv[1])=="--deferred") {deferred_calendar();deferred_calendar(10);return 0;}
        if(argc==2&&std::string_view(argv[1])=="--fhss-echo-reference") {encrypted_echoes(true,4,false);return 0;}
        if(argc==2&&std::string_view(argv[1])=="--fhss-echo") {encrypted_echoes(true);return 0;}
        smooth_envelope();qualified_cipher_phase_cells();matched_gap_channel_stress(true);matched_gap_channel_stress();omitted_pulse_evidence();mandatory_gate_rejection();matched_hop_gaps(1);matched_hop_gaps(10);matched_hop_gaps(10,false,true);matched_hop_gaps(10,false,true,true);matched_hop_gaps(1,true);matched_hop_gaps(10,true);matched_hop_gaps(100,true);matched_hop_gaps(1000,true);
        deferred_calendar();deferred_calendar(10);hop_arrival_timing();hop_power_boundaries();full_outer_cover();encrypted_echoes();encrypted_echoes(true,1);finish_paths();xrun_erasure_evidence();forward_repair_evidence();confirmed_forward_repair_evidence();cancelled_partial_chip_lattice();finite_tuning();production_boundaries();diagnostic_cover_compatibility();std::cout<<"full virtual cover, shared boundaries and finish passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

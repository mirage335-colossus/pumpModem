#include "datapump/channel.hpp"
#include "datapump/helper_filter.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <string_view>

using namespace datapump;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
modem::Config config(std::uint32_t rate=8000) {
    modem::Config c;c.sample_rate=rate;c.carrier_hz=rate==8000?1500:500;
    c.bandwidth_hz=rate==8000?1000:200;c.spreading_factor=64;c.stream_epoch=1800000000;
    return c;
}
modem::ChannelConfig model() {
    modem::ChannelConfig c;c.snr_db=20;c.clock_error_ppm=0;c.phase_noise_degrees_per_sqrt_second=0;c.seed=581;
    return c;
}
helper::Options helper_options(double power=1500) {
    helper::Options h;h.power_w=power;h.data_power_w=4;h.callsign="N0CALL";return h;
}
struct Observer {
    std::vector<float> samples;
    std::uint32_t rate=0;
    void operator()(std::span<const float> pcm,std::uint32_t frequency,std::uint64_t first) {
        check(first==samples.size(),"physical observer skipped or repeated PCM");
        check(!rate||rate==frequency,"physical observer changed its sample rate");rate=frequency;
        samples.insert(samples.end(),pcm.begin(),pcm.end());
    }
};
std::vector<float> capture(modem::SampledSimulationChannel& channel,modem::StreamingTransmitter& source,std::size_t block) {
    std::vector<float> result;std::vector<float> pcm(block);
    while(const auto count=channel.read(source,pcm))result.insert(result.end(),pcm.begin(),pcm.begin()+static_cast<std::ptrdiff_t>(count));
    return result;
}
void logical_contract_and_chunking() {
    const auto c=config();auto impairment=model();impairment.clock_error_ppm=137;impairment.frequency_offset_hz=2.3;
    impairment.phase_noise_degrees_per_sqrt_second=3.1;
    const auto h=helper_options();
    modem::SampledSimulationChannel one(c,impairment,h),many(c,impairment,h);
    check(one.physical_sample_rate()>2*c.sample_rate,"helper did not receive a separate physical sample clock");
    check(one.working_bytes()+one.auxiliary_workspace_bound<=one.workspace_bound,"physical channel consumed Robust workspace");
    Observer a,b;one.set_physical_observer(std::ref(a));many.set_physical_observer(std::ref(b));
    std::array<float,701> noise_a{},noise_b{};one.read_noise(noise_a);many.read_noise(noise_b);
    check(noise_a==noise_b,"helper physical idle noise depends on read size");
    one.begin_burst();many.begin_burst();
    modem::StreamingTransmitter first(modem::RawBits{{0,0,1}},c),second(modem::RawBits{{0,0,1}},c);
    const auto x=capture(one,first,1),y=capture(many,second,137);
    check(x==y&&a.samples==b.samples,"physical helper channel depends on logical read block boundaries");
    check(one.transmitted_samples()==first.total_samples(),"physical decimator omitted finite source PCM");
    check(one.received_samples()==x.size()+noise_a.size(),"physical route exposed physical counts as logical PCM");
    check(one.physical_received_samples()==a.samples.size(),"physical observer count differs from generated PCM");
    const auto factor=one.physical_sample_rate()/c.sample_rate;
    check(one.physical_received_samples()>=one.received_samples()*factor&&
        one.physical_received_samples()<=one.received_samples()*factor+128*factor,
        "physical lookahead escaped its bounded logical frontier");
    one.read_noise(noise_a);many.read_noise(noise_b);
    check(noise_a==noise_b&&a.samples==b.samples,"physical filter or oscillator lost continuity after EOF");
    check(!one.read(first,noise_a),"finite helper channel emitted source twice");
}
void helper_only_graceful_finish() {
    const auto c=config();auto h=helper_options();h.timecode=false;
    modem::SampledSimulationChannel channel(c,model(),h);channel.begin_burst(true);
    modem::StreamingTransmitter source(modem::Noise{},c);
    std::vector<float> before(c.sample_rate*5);check(channel.read(source,before)==before.size(),"helper-only source ended before stop");
    check(source.samples_emitted()>2*c.sample_rate&&source.samples_emitted()<4*c.sample_rate&&
          source.samples_emitted()>=channel.transmitted_samples()&&
          source.samples_emitted()-channel.transmitted_samples()<=2048,
          "helper-only lead advanced or delayed its source clock incorrectly");
    source.request_finish();
    const auto end=source.total_samples();
    check(end>channel.transmitted_samples(),"helper-only stop endpoint moved behind already-transmitted PCM");
    bool payload_done=false;std::array<float,137> pcm{};
    while(channel.read(source,pcm)) {
        if(channel.payload_finished()) {
            payload_done=true;
            check(channel.transmitted_samples()==end,"helper-only post advanced beyond its exact source endpoint");
        }
    }
    check(channel.transmitted_samples()==end&&source.finished(),"helper-only graceful stop omitted its protected source endpoint");
    check(payload_done&&channel.payload_end_sample()&&
        channel.received_samples()>=*channel.payload_end_sample()+2*c.sample_rate,
        "helper-only graceful stop omitted its separate post-source envelope");
}
void source_coordinates_and_envelope() {
    auto c=config(2000);c.stream_phase_samples=137;c.scramble=true;c.dsss=true;
    c.data_key.emplace(Bytes(32,0x53));c.spreading_seed[0]=43;c.dsss_seed[0]=97;
    auto impairment=model();impairment.clock_error_ppm=137;impairment.frequency_offset_hz=2.3;
    const auto h=helper_options(40);
    for(const auto envelope:{modem::SampledSimulationChannel::HelperEnvelope{},
                             modem::SampledSimulationChannel::HelperEnvelope{4.01,3.21}}) {
        modem::SampledSimulationChannel channel(c,impairment,h,0,envelope);
        modem::StreamingTransmitter source(modem::RawBits{{0,0,1}},c),reference(modem::RawBits{{0,0,1}},c);
        check(!channel.payload_begin_sample()&&!channel.payload_end_sample()&&!channel.payload_finished(),
              "unbound helper exposed a fabricated payload endpoint");
        Observer physical;channel.set_physical_observer(std::ref(physical));
        const auto expected_total=source.total_samples(),expected_origin=source.physical_output_origin();
        const auto rate=1+static_cast<long double>(impairment.clock_error_ppm)*1e-6L;
        std::array<float,137> pcm{};std::array<std::complex<double>,2048> actual{},expected{};
        std::uint64_t checked=0;bool lead=false,post=false,extended=false;
        std::uint64_t end=0;
        while(channel.read(source,pcm)) {
            check(channel.payload_begin_sample()&&channel.payload_end_sample(),"bound helper lost its source coordinates");
            check(source.total_samples()==expected_total&&source.physical_output_origin()==expected_origin,
                  "helper envelope changed source duration or keyed epoch");
            end=*channel.payload_end_sample();
            const auto progress=channel.transmitted_samples();
            const auto position=(static_cast<long double>(channel.received_samples())-channel.startup_offset_samples())*rate;
            const auto wanted=position<=0?0:position>=expected_total?expected_total:static_cast<std::uint64_t>(position);
            check(progress==wanted,"helper envelope inflated payload progress with lead, tail or interpolation lookahead");
            if(channel.received_samples()+128<*channel.payload_begin_sample()) {
                lead=true;check(!source.samples_emitted()&&!progress,"negative helper coordinate accessed its source");
            }
            const auto delta=static_cast<std::size_t>(source.samples_emitted()-checked);
            check(delta<=actual.size(),"source comparison lost its bounded analytic history");
            if(delta) {
                source.preview_last_analytic(std::span(actual).first(delta));
                check(reference.read_analytic(std::span(expected).first(delta))==delta,
                      "helper envelope changed the ordinary source sample count");
                check(std::equal(actual.begin(),actual.begin()+static_cast<std::ptrdiff_t>(delta),expected.begin()),
                      "helper envelope changed ordinary source PCM, wire coordinates or keyed timing");
                checked+=delta;
            }
            check(channel.payload_finished()==(channel.received_samples()>=end),
                  "helper source completion followed prefetch rather than the receiver frontier");
            if(channel.payload_finished()) {
                post=true;
                check(source.finished()&&progress==expected_total,"helper post did not preserve the complete source endpoint");
                if(!extended) {channel.extend_helper_tail(4.01);channel.extend_helper_tail(2);extended=true;}
            }
        }
        check(lead&&post&&checked==expected_total&&reference.finished(),
              "helper envelope failed to preserve the entire ordinary waveform between lead and post");
        check(source.emitted_payload_epoch()==reference.emitted_payload_epoch()&&
            source.source_valid_payload_end()==reference.source_valid_payload_end(),
            "helper envelope changed the payload bit endpoint or epoch");
        check(channel.received_samples()>=end+static_cast<std::uint64_t>(std::floor(4.4L*c.sample_rate/rate)),
              "helper tail extension shrank or failed to round complete dwells");
        // The physical observer includes real helper transmission throughout
        // both outside-source regions, not fabricated receiver absence.
        const auto physical_factor=physical.rate/c.sample_rate;
        const auto energy=[&](std::uint64_t begin,std::uint64_t finish) {
            double sum=0;const auto lo=begin*physical_factor,hi=finish*physical_factor;
            check(hi<=physical.samples.size(),"helper envelope energy window escaped captured PCM");
            for(auto i=lo;i<hi;++i)sum+=static_cast<double>(physical.samples[i])*physical.samples[i];
            return sum/static_cast<double>(hi-lo);
        };
        check(energy(c.sample_rate/2,3*c.sample_rate/2)>.1&&
              energy(end+c.sample_rate/2,end+3*c.sample_rate/2)>.1,
              "helper lead or post did not contain a real physical carrier");
        bool rejected=false;try {channel.extend_helper_tail(8);}catch(const Error&) {rejected=true;}
        check(rejected,"completed helper envelope was retroactively extended");
    }
}
void disabled_envelope_and_validation() {
    const auto c=config(2000);const auto impairment=model();
    modem::SampledSimulationChannel original(c,impairment),disabled(c,impairment,{},0,{23,37});
    original.begin_burst();disabled.begin_burst(false,{91,53});
    disabled.extend_helper_tail(std::numeric_limits<double>::quiet_NaN());
    modem::StreamingTransmitter a(modem::RawBits{{0,0,1}},c),b(modem::RawBits{{0,0,1}},c);
    check(capture(original,a,137)==capture(disabled,b,137),"disabled helper envelope changed ordinary PCM or its finite extent");
    check(original.received_samples()==disabled.received_samples()&&
        original.transmitted_samples()==disabled.transmitted_samples()&&
        original.payload_begin_sample()==disabled.payload_begin_sample()&&
        original.payload_end_sample()==disabled.payload_end_sample()&&original.payload_finished()&&disabled.payload_finished(),
        "disabled helper envelope changed ordinary timing or source progress");
    for(const auto duration:{-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::max()}) {
        bool rejected=false;try {modem::SampledSimulationChannel invalid(c,impairment,helper_options(),0,{duration,2});}
        catch(const Error&) {rejected=true;}
        check(rejected,"helper envelope accepted an invalid or overflowing lead");
        modem::SampledSimulationChannel channel(c,impairment,helper_options());
        rejected=false;try {channel.extend_helper_tail(duration);}catch(const Error&) {rejected=true;}
        check(rejected,"helper envelope accepted an invalid or overflowing tail extension");
    }
}
void payload_boundary_precedes_envelope_eof() {
    const auto c=config(2000);
    modem::SampledSimulationChannel channel(c,model(),helper_options(40));
    modem::StreamingTransmitter source(modem::RawBits{{0,0,1}},c);
    std::vector<float> pcm(static_cast<std::size_t>(source.total_samples()+10*c.sample_rate));
    const auto count=channel.read(source,pcm);
    check(channel.payload_end_sample()&&channel.payload_finished()&&count==*channel.payload_end_sample()&&count<pcm.size(),
          "large helper read concealed the committed source endpoint until envelope EOF");
    channel.extend_helper_tail(6.01);
    check(channel.read(source,pcm)>6*c.sample_rate,"payload boundary short read prevented real extended helper output");
    check(!channel.read(source,pcm),"completed helper envelope repeated its tail");
}
void late_extension_preserves_rendered_pcm() {
    const auto c=config(2000);const auto h=helper_options(40);const auto impairment=model();
    modem::SampledSimulationChannel reference(c,impairment,h),late(c,impairment,h);
    modem::StreamingTransmitter a(modem::RawBits{{0,0,1}},c),b(modem::RawBits{{0,0,1}},c);
    const auto expected=capture(reference,a,137);
    const auto target=expected.size()-20;
    std::vector<float> actual(target);std::size_t position=0;
    while(position<target) {
        const auto count=late.read(b,std::span(actual).subspan(position));
        check(count>0,"late-extension fixture reached EOF before its physical final ramp");position+=count;
    }
    check(late.payload_finished()&&late.received_samples()<reference.received_samples(),
          "late-extension fixture did not retain a logical post-source interval");
    bool rejected=false;try {late.extend_helper_tail(8);}catch(const Error&) {rejected=true;}
    check(rejected,"helper tail extension rewrote a final ramp already emitted in physical lookahead");
    const auto rest=capture(late,b,137);actual.insert(actual.end(),rest.begin(),rest.end());
    check(actual==expected,"rejected helper extension changed immutable physical PCM or its endpoint");
}
void unsupported_geometry_preserves_plain_channel() {
    auto c=config();c.carrier_hz=3400;
    // This shaped waveform occupies625Hz, so3400Hz has a flat preserved
    // upper edge at3712.5Hz. Do not reject it using the wider nominal rate.
    modem::SampledSimulationChannel preserved(c,model(),helper_options());
    check(preserved.physical_sample_rate()>c.sample_rate,
          "helper rejected a data passband preserved by the actual capture FIR");
    c.carrier_hz=3600;
    const auto upper=c.carrier_hz+helper::occupied_bandwidth(c)/2;
    check(upper>3900&&upper<c.sample_rate/2.,
          "negative fixture no longer places a valid waveform in the FIR transition");
    modem::SampledSimulationChannel ordinary(c,model());
    check(ordinary.physical_sample_rate()==c.sample_rate,"plain simulation changed unsupported helper geometry");
    bool rejected=false;
    try {modem::SampledSimulationChannel unsupported(c,model(),helper_options());}
    catch(const Error& error) {
        rejected=std::string_view(error.what())=="Helper Carrier simulation cannot preserve this near-Nyquist data passband";
    }
    check(rejected,"helper silently filtered a near-Nyquist Robust passband");
}
void adjacent_helpers_are_physical_mixtures() {
    const auto c=config();auto impairment=model();impairment.snr_db=100;
    for(const auto placement:{helper::Frequency::high,helper::Frequency::low,helper::Frequency::cochannel}) {
        auto h=helper_options(40);h.frequency=placement;h.callsign.clear();h.timecode=false;
        modem::SampledSimulationChannel channel(c,impairment,h);channel.begin_burst(true);
        Observer raw;channel.set_physical_observer(std::ref(raw));
        modem::StreamingTransmitter source(modem::Noise{32},c);
        const auto logical=capture(channel,source,137);
        const auto plan=helper::plan(h,c.carrier_hz,helper::occupied_bandwidth(c),channel.physical_sample_rate(),0,modem::outer_dsss_transmit_gain(c));
        if(placement==helper::Frequency::high)
            check(plan.frequency_hz==2262.5&&plan.frequency_hz+150<c.sample_rate/2.,
                  "High helper was not adjacent inside the logical capture passband");
        std::complex<double> sum{};
        for(std::size_t i=0;i<raw.samples.size();++i)
            sum+=static_cast<double>(raw.samples[i])*std::polar(1.,-2*std::numbers::pi*plan.frequency_hz*i/raw.rate);
        check(std::norm(sum)>raw.samples.size(),"adjacent helper missing from physical PCM");
        double energy=0;for(const auto sample:logical)energy+=static_cast<double>(sample)*sample;
        check(energy/logical.size()>.01,"simulator hid an in-band helper from logical PCM");
    }
}
void calibrated_data_band_noise() {
    for(const auto sample_rate:{2000U,8000U})for(const auto power:{1500.,4000000000.}) {
        const auto c=config(sample_rate);const auto impairment=model();const auto h=helper_options(power);
        modem::SampledSimulationChannel channel(c,impairment,h);
        const auto plan=helper::plan(h,c.carrier_hz,helper::occupied_bandwidth(c),channel.physical_sample_rate(),0,modem::outer_dsss_transmit_gain(c));
        Observer physical;channel.set_physical_observer(std::ref(physical));
        std::vector<float> noise(100000);channel.read_noise(noise);
        modem::SampledSimulationChannel original(c,impairment);
        std::vector<float> original_noise(physical.samples.size());original.read_noise(original_noise);
        const auto physical_scale=plan.data_gain*std::sqrt(static_cast<double>(physical.rate)/c.sample_rate);
        for(std::size_t i=0;i<physical.samples.size();++i) {
            const auto expected=static_cast<float>(original_noise[i]*physical_scale);
            const auto actual=physical.samples[i];
            check(std::abs(actual-expected)<=8*std::numeric_limits<float>::epsilon()*std::max(std::abs(actual),std::abs(expected)),
                  "raw physical AWGN changed its original in-band PSD normalization");
        }
        double sum=0,square=0;const auto values=std::span(noise).subspan(1000);
        for(const auto value:values) {sum+=value;square+=static_cast<double>(value)*value;}
        const auto variance=square/values.size()-std::pow(sum/values.size(),2);
        const auto expected=modem::nominal_signal_power*std::pow(10.,-impairment.snr_db/10)*plan.data_gain*plan.data_gain;
        check(std::abs(variance/expected-1)<.025,"physical AWGN lost its bounded logical variance calibration");
        // Match the data carrier over one chip. This checks the noise used by
        // a payload correlation, rather than renormalizing total filtered PCM.
        const auto width=static_cast<std::size_t>(std::llround(c.sample_rate/c.bandwidth_hz));
        const auto rotation=std::polar(1.,-2*std::numbers::pi*c.carrier_hz/c.sample_rate);
        double matched=0;std::size_t windows=0;
        for(std::size_t start=0;start+width<=values.size();start+=width) {
            std::complex<double> sum{},oscillator{1,0};
            for(std::size_t i=0;i<width;++i) {sum+=static_cast<double>(values[start+i])*oscillator;oscillator*=rotation;}
            matched+=std::norm(sum);++windows;
        }
        check(std::abs(matched/(windows*width*expected)-1)<.035,
              "physical AWGN changed the original matched in-data-band noise density");
    }
}
void physical_phase_noise_sidebands() {
    const auto c=config();auto h=helper_options(4000000000.);h.callsign.clear();h.timecode=false;
    std::array<double,2> relative_power{};
    for(std::size_t trial=0;trial<relative_power.size();++trial) {
        auto impairment=model();impairment.snr_db=100;
        impairment.phase_noise_degrees_per_sqrt_second=trial?.5:0;
        modem::SampledSimulationChannel channel(c,impairment,h);channel.begin_burst(true);
        const auto plan=helper::plan(h,c.carrier_hz,helper::occupied_bandwidth(c),channel.physical_sample_rate(),0,modem::outer_dsss_transmit_gain(c));
        modem::StreamingTransmitter source(modem::Noise{64},c);std::array<float,800> pcm{};
        double energy=0;std::size_t samples=0;
        for(unsigned block=0;block<100;++block) {
            const auto count=channel.read(source,pcm);
            for(std::size_t i=0;i<count;++i)energy+=static_cast<double>(pcm[i])*pcm[i];
            samples+=count;
        }
        relative_power[trial]=energy/samples/(modem::nominal_signal_power*plan.data_gain*plan.data_gain);
    }
    check(relative_power[0]<.001,"unmodulated helper exceeded its isolated logical leakage fixture");
    // A Wiener oscillator has real broadband wings. At a90dB helper ratio,
    // even modest phase diffusion can dominate the weak data band's AWGN.
    // Keeping these wings proves simulation does not secretly cancel them or
    // claim unconditional aggregate payload SNR preservation.
    check(relative_power[1]>.01&&relative_power[1]>100*relative_power[0],
          "physical helper phase-noise sidebands were hidden from Robust PCM");
}
void physical_frequency_and_helper_only_rejection() {
    const auto c=config();auto impairment=model();impairment.snr_db=100;
    impairment.clock_error_ppm=300;impairment.frequency_offset_hz=37;
    auto h=helper_options(4000000000.);h.callsign.clear();h.timecode=false;
    modem::SampledSimulationChannel channel(c,impairment,h);channel.begin_burst(true);
    Observer physical;channel.set_physical_observer(std::ref(physical));
    modem::StreamingTransmitter source(modem::Noise{32},c);const auto logical=capture(channel,source,137);
    const auto plan=helper::plan(h,c.carrier_hz,helper::occupied_bandwidth(c),channel.physical_sample_rate(),0,modem::outer_dsss_transmit_gain(c));
    const auto expected=plan.frequency_hz*(1+impairment.clock_error_ppm*1e-6)+impairment.frequency_offset_hz;
    double strongest=-1,best=0;
    const auto window=physical.rate/10;
    for(int cell=-12;cell<=12;++cell) {
        const auto frequency=expected+cell*.5;
        double score=0;
        const auto step=std::polar(1.,-2*std::numbers::pi*frequency/physical.rate);
        for(std::size_t start=0;start+window<physical.samples.size();start+=window) {
            std::complex<double> oscillator{1,0},sum{};
            for(std::size_t i=0;i<window;++i) {sum+=static_cast<double>(physical.samples[start+i])*oscillator;oscillator*=step;}
            score+=std::norm(sum);
        }
        if(score>strongest) {strongest=score;best=frequency;}
    }
    check(std::abs(best-expected)<.51,"physical helper did not share actual CFO and transmitter clock error");
    double energy=0;for(const auto value:logical)energy+=static_cast<double>(value)*value;
    const auto data_reference=modem::nominal_signal_power*plan.data_gain*plan.data_gain;
    check(energy/logical.size()<data_reference*.01,"bright helper leaked into decimated Robust PCM");
}
void payload_progress_and_completion() {
    for(const auto sample_rate:{2000U,8000U})for(const auto power:{1500.,4000000000.}) {
        const auto c=config(sample_rate);const auto impairment=model();const auto h=helper_options(power);
        modem::SampledSimulationChannel channel(c,impairment,h);
        modem::StreamingTransmitter source(modem::RawBits{{0,0,1}},c);
        modem::PatternSearch search;search.worker_threads=1;search.start_offset_seconds=0;search.start_uncertainty_seconds=6;
        const auto placement=helper::plan(h,c.carrier_hz,helper::occupied_bandwidth(c),channel.physical_sample_rate(),0,
            modem::outer_dsss_transmit_gain(c));
        helper::PayloadFilter filter;
        if(placement.frequency_hz+225<c.sample_rate/2.)filter=helper::PayloadFilter(c.sample_rate,placement.frequency_hz);
        modem::PatternReceiver receiver(c,16*1024*1024,search);
        std::map<std::pair<std::uint64_t,std::uint64_t>,Bytes> streams;
        std::vector<std::string> prefixes;std::size_t completions=0;std::uint64_t last_bit=0,completed=0;
        std::array<float,173> pcm{};
        const auto feed=[&](std::span<const float> block) {
            std::array<float,173> filtered{};
            const auto count=filter.push(block,filtered);
            receiver.push(std::span(filtered).first(count));
            for(auto& burst:receiver.take_bursts()) {
                auto& bits=streams[{burst.stream_first_sample,burst.stream_first_symbol}];
                for(const auto bit:burst.bits) {
                    bits.push_back(bit);std::string prefix;for(const auto value:bits)prefix+=value?'1':'0';prefixes.push_back(prefix);
                }
                if(!burst.bits.empty())last_bit=channel.received_samples();
                if(burst.complete) {++completions;completed=channel.received_samples();}
            }
        };
        while(const auto count=channel.read(source,pcm))feed(std::span(pcm).first(count));
        auto trailing=sample_rate*9ULL;
        while(trailing) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,pcm.size()));
            channel.read_noise(std::span(pcm).first(count));feed(std::span(pcm).first(count));trailing-=count;
        }
        check(streams.size()==1&&streams.begin()->second==Bytes({0,0,1})&&completions==1,
              "physical helper changed exact Robust 001 payload or completion");
        check(prefixes==std::vector<std::string>({"0","00","001"}),"physical helper changed accepted pending bit prefixes");
        check(completed>=last_bit+6*sample_rate,"physical helper manufactured early payload completion");
    }
}
void outer_dsss_helper_power_calibration() {
    auto c=config();c.bandwidth_hz=100;c.dsss_factor=10;c.pulse_shaping=false;c.scramble=true;c.dsss=true;
    c.data_key.emplace(Bytes(32,0x63));
    const auto inner=c.data_key->stream(StreamPurpose::Scrambler,0,0,c.spreading_seed.size());
    const auto outer=c.data_key->stream(StreamPurpose::Dsss,0,0,c.dsss_seed.size());
    std::copy(inner.begin(),inner.end(),c.spreading_seed.begin());
    std::copy(outer.begin(),outer.end(),c.dsss_seed.begin());
    auto impairment=model();impairment.snr_db=100;
    auto h=helper_options(40);h.callsign.clear();h.timecode=false;
    const auto expected_db=10*std::log10(h.power_w/h.data_power_w);
    std::vector<float> legacy_idle,legacy_physical_idle;
    std::array<double,2> helper_power{};
    std::uint32_t physical_rate=0;
    for(std::size_t version=0;version<2;++version) {
        c.outer_dsss_version=version?modem::OuterDsssVersion::interleaved_v2:modem::OuterDsssVersion::legacy_v1;
        modem::SampledSimulationChannel composite(c,impairment,h),helper_only(c,impairment,h);
        Observer mixed,only;composite.set_physical_observer(std::ref(mixed));helper_only.set_physical_observer(std::ref(only));
        std::vector<float> idle(2048),idle_only(idle.size());
        composite.read_noise(idle);helper_only.read_noise(idle_only);
        check(idle==idle_only && mixed.samples==only.samples,"paired power-calibration channels changed seeded idle AWGN");
        if(version) {
            check(idle==legacy_idle && mixed.samples==legacy_physical_idle,
                "outer DSSS helper calibration changed pre-burst AWGN or its physical sample clock");
            check(mixed.rate==physical_rate,"outer DSSS helper calibration changed the physical sample clock");
        } else {legacy_idle=idle;legacy_physical_idle=mixed.samples;physical_rate=mixed.rate;}
        const auto origin=composite.received_samples();
        composite.begin_burst();helper_only.begin_burst(true);
        check(composite.startup_offset_samples()==helper_only.startup_offset_samples(),"paired power-calibration bursts have different startup coordinates");
        modem::StreamingTransmitter payload(modem::RawBits{{0,0,1,1}},c),suppressed(modem::RawBits{{0,0,1,1}},c);
        capture(composite,payload,137);capture(helper_only,suppressed,137);
        check(mixed.rate==only.rate && mixed.samples.size()==only.samples.size(),"paired power-calibration channels changed source duration");
        const auto end=static_cast<long double>(payload.total_samples())/c.sample_rate;
        double tone_energy=0,payload_energy=0;std::size_t count=0;
        for(std::size_t i=0;i<mixed.samples.size();++i) {
            const auto seconds=static_cast<long double>(i)/mixed.rate-
                (origin+composite.startup_offset_samples())/c.sample_rate;
            if(seconds<.02L || seconds>end-.02L)continue;
            const auto dwell=std::fmod(seconds,.4L);
            // Measure straight tone, excluding dwell ramps, punctuation, and
            // payload prefix/tails. Whole-dwell averages are not a power label.
            if(dwell<.04L || dwell>=.20L)continue;
            const auto tone=static_cast<double>(only.samples[i]);
            const auto data=static_cast<double>(mixed.samples[i])-tone;
            tone_energy+=tone*tone;payload_energy+=data*data;++count;
        }
        check(count>mixed.rate/2 && payload_energy>0,"power calibration did not cover enough active payload and clean helper tone");
        const auto actual_db=10*std::log10(tone_energy/payload_energy);
        check(std::abs(actual_db-expected_db)<.5,version?
            "V2 outer DSSS changed actual helper/data power ratio by its digital backoff":
            "legacy outer DSSS changed actual helper/data power ratio");
        helper_power[version]=tone_energy/count;
    }
    check(std::abs(helper_power[1]/helper_power[0]-.25)<.001,
        "V2 helper PCM did not follow the payload's half-amplitude backoff");
}
}
int main() {
    try {logical_contract_and_chunking();helper_only_graceful_finish();source_coordinates_and_envelope();disabled_envelope_and_validation();payload_boundary_precedes_envelope_eof();late_extension_preserves_rendered_pcm();unsupported_geometry_preserves_plain_channel();adjacent_helpers_are_physical_mixtures();calibrated_data_band_noise();physical_phase_noise_sidebands();physical_frequency_and_helper_only_rejection();payload_progress_and_completion();outer_dsss_helper_power_calibration();
        std::cout<<"physical helper simulation passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

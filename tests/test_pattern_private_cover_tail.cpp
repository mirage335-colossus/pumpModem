// Focused ordinary native-decoder regression, not normal Auto throughput proof.
// Both detector settings receive identical PCM. Expected bits are collector
// assertions only; no finish(), EOF completion or source length reaches RX.
#include "datapump/channel.hpp"
#include "datapump/transfer.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/tuning.hpp"
#include "../src/live_receiver_admission.hpp"
#include "../src/audio_trace.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <optional>
#include <random>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
using namespace datapump;
namespace {
constexpr std::size_t numeric_workspace=128*1024*1024;
void check(bool ok,const std::string& why){if(!ok)throw Error(why);}
transfer::Options options() {
    transfer::Options o;o.timestamp=1792888486;o.content_limit=1024*1024;
    o.dsp_workspace_bytes=256*1024*1024;
    auto& c=o.modem;c.sample_rate=1024;c.carrier_hz=256;c.bandwidth_hz=100;
    c.spreading_factor=64;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::complex_only;
    std::array<std::uint8_t,32> secret{};
    for(std::size_t i=0;i<secret.size();++i)secret[i]=static_cast<std::uint8_t>(i*11+7);
    o.key.emplace(secret);
    c.oscillator_search=modem::OscillatorSearchConfig{
        tuning::oscillator_model(tuning::parse_oscillator_preset("crystal")),
        tuning::oscillator_model(tuning::parse_oscillator_preset("ic-7100")),0,3};
    c.oscillator_search->reference=modem::OscillatorReference::independent_audio;
    while(modem::oscillator_pattern_search(c).hypotheses.size()<512 && c.oscillator_search->rf_shift_hz<1e8)
        c.oscillator_search->rf_shift_hz+=10000;
    return o;
}
bool trace_filter(const char* event)noexcept {
    const std::string_view name{event};
    return name=="trace_begin" || name=="pattern_receiver_engine" || name=="pattern_confirmation";
}
std::uint64_t number(const std::string& line,const char* key) {
    const auto needle='"'+std::string(key)+"\":";const auto at=line.find(needle);
    check(at!=std::string::npos,"confirmation trace omitted "+std::string(key));
    return std::stoull(line.substr(at+needle.size()));
}
struct Result {
    Bytes bits;bool complete=false;
    std::vector<std::pair<std::uint64_t,std::size_t>> progress;
};
Result receive(const transfer::Options& source,const std::vector<float>& pcm,const std::string& label,
        bool preceding,bool shared,std::uint64_t epoch,std::uint64_t origin,double capture_epoch,
        std::uint64_t expected_first,std::uint64_t expected_symbol,const Bytes& expected,bool exact,
        const std::filesystem::path& trace={}) {
    live::Settings value;value.simulation=true;value.full_duplex=true;
    value.transfer=source;value.transfer.timestamp=0;value.transfer.search_seconds=0;
    value.content_limit=1024*1024;value.dsp_workspace_bytes=256*1024*1024;
    value.transfer.dsp_workspace_bytes=value.dsp_workspace_bytes;
    value=live::detail::normalized_layout(value);
    const auto config=transfer::seeded_config(value.transfer,epoch);
    live::detail::ReceiverAdmissionContext context;context.sample_origin=origin;
    context.keyed=true;context.key_count=context.profile_count=1;
    context.capacity=live::receiver_workspace_capacity(value);
    context.control_margin=live::receiver_control_margin(config,true);
    context.prefer_streamed_templates=true;
    context.now=capture_epoch+static_cast<double>(origin)/config.sample_rate;
    auto prepared=live::detail::prepare_receiver_search(config,value,epoch,context);
    auto search=std::move(prepared.search);search.worker_threads=1;
    if(preceding)search.outer_presence_guard=false;
    const auto declared=modem::oscillator_pattern_search(config);
    check(search.hypotheses==declared.hypotheses,label+": original RF/LF catalogue changed");
    check(search.search_stream_phases,label+": original private phase search omitted");
    if(config.spreading_factor==64) {
        std::set<double> rates;for(const auto& h:search.hypotheses)rates.insert(h.clock_error_ppm);
        check(search.hypotheses.size()==513 && rates.size()==3 && search.initial_stream_symbols==4,
              label+": original 513-pair/3-clock/4-start geometry changed");
    }
    std::optional<audio::trace::Scope> tracing;
    if(!trace.empty())tracing.emplace(trace,false,&trace_filter);
    modem::StreamingReceiver receiver(config,numeric_workspace,search);
    const auto bounded=[&] {
        // StreamingReceiver's outer handle is outside its numeric constructor grant.
        check(receiver.working_bytes()<=numeric_workspace+sizeof(receiver) &&
              receiver.reserved_workspace_bytes()<=numeric_workspace+sizeof(receiver),
              label+": ordinary numerical workspace exceeded its original grant");
    };
    bounded();
    struct Stream {Bytes bits;bool complete=false;decltype(Result::progress) progress;};
    std::map<std::pair<std::uint64_t,std::uint64_t>,Stream> streams;
    for(std::size_t at=origin;at<pcm.size();) {
        const auto count=std::min<std::size_t>(163,pcm.size()-at);
        const auto input=std::span(pcm).subspan(at,count);
        if(shared) {
            std::array<std::complex<double>,163> projected{};
            for(std::size_t i=0;i<count;++i)projected[i]=static_cast<double>(input[i])*std::polar(1.,.73-
                2*std::numbers::pi*config.carrier_hz*static_cast<double>(at-origin+i)/config.sample_rate);
            receiver.push(input,std::span(projected).first(count));
        } else receiver.push(input);
        receiver.process_pending();at+=count;
        for(const auto& burst:receiver.take_pattern_bursts()) {
            auto& stream=streams[{burst.stream_first_sample,burst.stream_first_symbol}];
            check(!stream.complete,label+": reopened physical END");
            check(!burst.missing_slots && burst.first_stream_symbol==burst.stream_first_symbol+stream.bits.size(),
                  label+": lost exact stream positions during drain");
            stream.bits.insert(stream.bits.end(),burst.bits.begin(),burst.bits.end());
            stream.complete=burst.complete;
            if(!burst.bits.empty())stream.progress.emplace_back(at,stream.bits.size());
            const auto first=origin+burst.stream_first_sample;
            const auto distance=first>expected_first?first-expected_first:expected_first-first;
            const bool target=distance<=2 && burst.stream_first_symbol==expected_symbol;
            if(exact && target)check(stream.bits.size()<=expected.size() &&
                std::equal(stream.bits.begin(),stream.bits.end(),expected.begin()),
                label+": changed an accepted source prefix");
            if(burst.complete) {
                const auto h=modem::symbol_sample_count(config);
                const auto absence=(6ULL*config.sample_rate+h-1)/h*h;
                check(at>=origin+burst.end_sample && at-origin-burst.end_sample>=absence,
                      label+": END omitted fully observed absent symbols");
            }
        }
        bounded();
    }
    Result result;std::size_t targets=0;
    for(const auto& [identity,stream]:streams) {
        const auto first=origin+identity.first;
        const auto distance=first>expected_first?first-expected_first:expected_first-first;
        if(distance<=2 && identity.second==expected_symbol) {
            ++targets;result={stream.bits,stream.complete,stream.progress};
        }
    }
    check(targets==1,label+": lacked a unique actual-source row");
    if(exact)check(result.bits==expected && result.complete,label+": omitted entire physical wire or scored END");
    std::cout<<label<<": bits="<<result.bits.size()<<" complete="<<result.complete<<'\n';
    return result;
}
void same(const Result& a,const Result& b,const char* why) {
    check(a.bits==b.bits && a.progress==b.progress && a.complete==b.complete,why);
}
struct Capture {std::vector<float> mixed,without_helper;Bytes wire;std::uint64_t first=0;};
Capture cover_capture(const transfer::Options& o) {
    const auto config=transfer::seeded_config(o,o.timestamp);
    const auto training=modem::training_sample_count(config);
    modem::ChannelConfig effects;effects.snr_db=40;effects.clock_error_ppm=0;
    effects.frequency_offset_hz=0;effects.phase_noise_degrees_per_sqrt_second=0;effects.seed=5181;
    helper::Options helper;helper.power_w=1500;helper.data_power_w=4;helper.callsign.clear();
    modem::SampledSimulationChannel calibration(o.modem,effects,helper);
    calibration.set_helper_time_key(o.key);calibration.begin_burst();
    auto initial=transfer::binary_transmitter(Bytes{0,0,1},o);std::array<float,1> sample{};
    calibration.read(*initial,sample);
    const auto idle=8031ULL-training-static_cast<std::uint64_t>(std::ceil(calibration.startup_offset_samples()));
    modem::SampledSimulationChannel mixed(o.modem,effects,helper),only(o.modem,effects,helper),noise(o.modem,effects,helper);
    mixed.set_helper_time_key(o.key);only.set_helper_time_key(o.key);
    auto source=transfer::binary_transmitter(Bytes{0,0,1},o),suppressed=transfer::binary_transmitter(Bytes{0,0,1},o);
    Capture out;std::array<float,163> a{},b{},n{};
    for(std::uint64_t at=0;at<idle;) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(a.size(),idle-at));
        mixed.read_noise(std::span(a).first(count));only.read_noise(std::span(b).first(count));noise.read_noise(std::span(n).first(count));
        check(std::equal(a.begin(),a.begin()+count,b.begin()) && std::equal(a.begin(),a.begin()+count,n.begin()),
              "physical cover control changed its initial noise");
        out.mixed.insert(out.mixed.end(),a.begin(),a.begin()+count);
        out.without_helper.insert(out.without_helper.end(),a.begin(),a.begin()+count);at+=count;
    }
    mixed.begin_burst(false);only.begin_burst(true);noise.begin_burst(false);
    bool ended=false,extended=false;
    const auto tail=modem::pattern_absence_capture_samples(config)+config.sample_rate+128;
    while(out.mixed.size()<24000) {
        const auto capacity=std::min(a.size(),24000-out.mixed.size());std::size_t count=0;
        if(!ended) {
            count=mixed.read(*source,std::span(a).first(capacity));
            check(only.read(*suppressed,std::span(b).first(capacity))==count,"matched source durations changed");
            if(!count)ended=true;
            else {
                check(mixed.payload_begin_sample()==only.payload_begin_sample() &&
                      mixed.payload_end_sample()==only.payload_end_sample(),"matched source coordinates changed");
                out.first=*mixed.payload_begin_sample()+training;
                if(mixed.payload_finished() && !extended) {
                    mixed.extend_helper_tail(static_cast<double>(tail)/config.sample_rate);
                    only.extend_helper_tail(static_cast<double>(tail)/config.sample_rate);extended=true;
                }
            }
        }
        if(ended){count=capacity;mixed.read_noise(std::span(a).first(count));only.read_noise(std::span(b).first(count));}
        noise.read_noise(std::span(n).first(count));
        for(std::size_t i=0;i<count;++i) {
            out.mixed.push_back(a[i]);
            out.without_helper.push_back(static_cast<float>(static_cast<double>(a[i])-b[i]+n[i]));
        }
    }
    out.wire=source->transmit_trace().wire_bits;
    check(out.first==8031 && out.wire==Bytes({1,0,0}),"physical cover fixture changed its source");
    return out;
}
void cover_tail() {
    const auto o=options();const auto capture=cover_capture(o);
    constexpr double origin=1792888480.922322416;
    const auto before=receive(o,capture.mixed,"cover preceding",true,false,1792888485,4217,origin,
        capture.first,1,capture.wire,false);
    check(before.bits==Bytes({1,0,0,0,0,0}),"preceding detector did not reproduce exact extra-bit witness");
    const auto corrected=receive(o,capture.mixed,"cover guarded",false,false,1792888485,4217,origin,
        capture.first,1,capture.wire,true);
    receive(o,capture.without_helper,"cover helper component removed",false,false,1792888485,4217,origin,
        capture.first,1,capture.wire,true);
    const auto shared=receive(o,capture.mixed,"cover shared phase",false,true,1792888485,4217,origin,
        capture.first,1,capture.wire,true);
    same(corrected,shared,"raw/shared phase convention changed exact payload progress");
    receive(o,capture.mixed,"cover true epoch",false,false,1792888486,5201,origin,
        capture.first,0,capture.wire,true);
}
// Existing confirmation fields prove an accepted immutable birth identity
// precedes a later joint confirmation. No new production diagnostic field is needed.
void confirmation_proof(const std::filesystem::path& path,std::uint64_t first,bool weak) {
    std::ifstream file(path);check(file.is_open(),"cannot read ordinary confirmation evidence");
    std::string line;bool accepted=false,joint=false,engine=false,footer=false;
    while(std::getline(file,line)) {
        if(line.find("\"event\":\"pattern_receiver_engine\"")!=std::string::npos)
            engine=number(line,"bin_samples")==1 && number(line,"sample_fit")==0 && number(line,"real_rank")==1;
        if(line.find("\"event\":\"trace_end\"")!=std::string::npos)
            footer=number(line,"dropped")==0 && line.find("\"size_limited\":false")!=std::string::npos &&
                line.find("\"prior_io_failed\":false")!=std::string::npos;
        if(line.find("\"event\":\"pattern_confirmation\"")==std::string::npos || number(line,"stream_first_symbol")!=0)continue;
        const auto born=number(line,"input_sample_origin")+number(line,"stream_first_sample");
        const auto distance=born>first?born-first:first-born;if(distance>2)continue;
        check(number(line,"newly_confirmed")>0,"ordinary confirmation did not accept bits");
        if(weak) {
            const auto end_symbol=number(line,"chunk_first_symbol")+number(line,"confirmed");
            if(accepted && line.find("\"phase\":\"continuation\"")!=std::string::npos &&
               number(line,"joint_chain")==1 && end_symbol<=18)joint=true;
        } else check(number(line,"standalone")==1,"strong preservation control lost standalone confidence");
        accepted=true;
    }
    check(engine && footer && accepted,"ordinary confirmation evidence was incomplete or ineligible");
    if(weak)check(joint,"weak control lacked established joint confirmation before stronger final symbol");
}
void awgn_preservation(const std::filesystem::path& prefix,bool weak,bool impaired) {
    auto o=options();o.modem.spreading_factor=weak?50:16;o.modem.oscillator_search->rf_shift_hz=1e6;
    const auto config=transfer::seeded_config(o,o.timestamp);
    const auto h=modem::symbol_sample_count(config),chip=modem::pattern_chip_samples(config);
    const auto first=modem::training_sample_count(config)+128;
    check(chip==21 && (!weak || h==config.sample_rate),"preservation control geometry changed");
    Bytes expected=weak?Bytes{1,0,1,1,0,0,1,0,0,1,0,0,1,1,0,1,1,0,1}:Bytes{1,0,1,1,0,0,1,0};
    transfer::xor_binary_bits(expected,o);modem::PatternCode code(config,o.timestamp);
    const auto rate=impaired?1.00015:1.;const auto cfo=impaired?.125*config.sample_rate/static_cast<double>(h):0.;
    const auto sigma=std::sqrt(modem::nominal_signal_power*std::pow(10.,-(weak?-11.:40.)/10));
    std::mt19937_64 noise(5181),phase(5181^0xa0761d6478bd642fULL);
    std::normal_distribution<double> normal,phase_normal;
    std::vector<float> pcm(weak?40000:16000);double angle=.31;
    for(std::size_t i=0;i<pcm.size();++i) {
        // Retain the original high-SNR witness's cached draw ordering; the
        // separately preselected one-second control uses independent draws.
        if(impaired)angle+=(weak?phase_normal(phase):normal(phase))*.15*std::numbers::pi/180/std::sqrt(config.sample_rate);
        const auto source=(static_cast<double>(i)-first)*rate;std::complex<double> value{};
        if(source>=0 && source<expected.size()*h) {
            const auto index=static_cast<std::size_t>(source/h);const auto within=source-index*h;
            const auto gain=weak && index==18?8.:1.;
            value=gain*code.shaped_value(index*code.chips_per_symbol(),expected[index],within)*
                std::polar(1.,angle+2*std::numbers::pi*(config.carrier_hz*rate+cfo)*i/config.sample_rate);
        }
        pcm[i]=static_cast<float>(value.real()+sigma*normal(noise));
    }
    const auto name=weak?(impaired?"weak impaired":"weak nominal"):"strong off-grid";
    const auto off_path=prefix.string()+"-"+name+"-off.jsonl",on_path=prefix.string()+"-"+name+"-on.jsonl";
    const auto before=receive(o,pcm,std::string(name)+" preceding",true,false,o.timestamp,0,
        static_cast<double>(o.timestamp),first,0,expected,true,off_path);
    const auto after=receive(o,pcm,std::string(name)+" guarded",false,false,o.timestamp,0,
        static_cast<double>(o.timestamp),first,0,expected,true,on_path);
    same(before,after,"guard changed exact ordinary AWGN prefixes, next-poll progress or physical END");
    confirmation_proof(off_path,first,weak);confirmation_proof(on_path,first,weak);
}
}
int main()try {
    const auto prefix=std::filesystem::temp_directory_path()/("datapump-private-cover-tail-"+
        std::to_string(audio::trace::identity())+"-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    cover_tail();awgn_preservation(prefix,false,true);
    awgn_preservation(prefix,true,false);awgn_preservation(prefix,true,true);
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}

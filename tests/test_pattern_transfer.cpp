#include "datapump/transfer.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
transfer::Options options(bool keyed=false) {
    transfer::Options result;result.timestamp=1700000000;result.search_seconds=0;
    result.dsp_workspace_bytes=8*1024*1024;result.content_limit=65536;
    result.modem.sample_rate=8000;result.modem.bandwidth_hz=2000;result.modem.carrier_hz=1800;
    result.modem.spreading_factor=8;result.modem.pulse_shaping=false;
    if(keyed)result.key.emplace(Bytes(32,91));return result;
}
Message message() { Message sent;sent.data.assign(44,'e');sent.data.back()=0;return sent; }
struct Harness {
    transfer::Options value;
    modem::PatternReceiver physical;
    transfer::StreamReceiver source;
    std::vector<transfer::Received> ended;
    std::size_t pending_count=0;
    explicit Harness(transfer::Options options):value(std::move(options)),
        physical(transfer::seeded_config(value,value.timestamp),value.dsp_workspace_bytes,search()),source(value,value.timestamp){}
    static modem::PatternSearch search() {modem::PatternSearch result;result.frequency_offsets_hz={0};result.initial_stream_symbols=1;return result;}
    void harvest() {
        for(auto& burst:physical.take_bursts()) {
            const auto complete=burst.complete;auto decoded=source.push(std::move(burst),physical.diagnostics());
            if(complete)ended.push_back(std::move(decoded));
            else { ++pending_count;check(!decoded.stream_complete && !decoded.content_validated && decoded.content.message.data.empty(),"source codec must stay gated during physical reception"); }
        }
    }
    void feed(std::span<const float> pcm) {
        for(std::size_t first=0;first<pcm.size();) {const auto count=std::min<std::size_t>(997,pcm.size()-first);physical.push(pcm.subspan(first,count));first+=count;harvest();}
    }
    void silence(double seconds) {std::vector<float> samples(static_cast<std::size_t>(seconds*value.modem.sample_rate));feed(samples);}
};
void six_second_end_and_eof() {
    auto value=options();const auto sent=message();const auto pcm=transfer::transmit(sent,value);
    const auto tail=static_cast<std::size_t>(modem::suppression_sample_count(value.modem));
    check(tail==3*value.modem.sample_rate && tail<pcm.size(),"capture must include exactly three seconds of suppression noise");
    Harness receiver(value);receiver.feed(std::span(pcm).first(pcm.size()-tail));
    check(receiver.ended.empty(),"complete fixed codeword must not end physical stream");
    receiver.feed(std::span(pcm).last(tail));
    check(receiver.ended.empty(),"suppression noise is not a physical end marker");
    receiver.silence(2.75);check(receiver.ended.empty(),"5.75 seconds of symbol absence must not end physical stream");
    receiver.silence(.75);
    check(receiver.ended.size()==1,"six seconds of resolved absence must emit one stream end");
    check(receiver.ended.front().content_validated && receiver.ended.front().content.message.data==sent.data,"post-end compressed source must roundtrip sampled PCM");
    receiver.silence(1);check(receiver.ended.size()==1,"physical end event must not repeat on continuing silence");
    Harness eof(value);eof.feed(pcm);eof.physical.finish();eof.harvest();
    check(eof.ended.empty(),"capture EOF cannot synthesize six seconds of silence");
}
void pcm_symbol_loss() {
    const auto sent=message();
    for(const auto [keyed,spreading]:std::array<std::pair<bool,unsigned>,3>{{{false,8},{true,8},{false,16}}}) {
        auto value=options(keyed);value.compression=false;
        value.modem.spreading_factor=spreading;value.modem.pulse_shaping=spreading>=16;
        auto pcm=transfer::transmit(sent,value);
        const auto config=transfer::seeded_config(value,value.timestamp);
        const auto start=modem::training_sample_count(config)+modem::pattern_pulse_padding_samples(config);
        const auto symbol=modem::symbol_sample_count(config);
        const auto wire=transfer::message_wire_bits(sent,value);
        for(const auto position:std::array<std::size_t,4>{80,81,192+90,wire.size()-1})
            std::fill(pcm.begin()+static_cast<std::ptrdiff_t>(start+position*symbol),pcm.begin()+static_cast<std::ptrdiff_t>(start+(position+1)*symbol),0.f);
        Harness receiver(value);receiver.feed(pcm);receiver.silence(7);
        const auto found=std::find_if(receiver.ended.begin(),receiver.ended.end(),[&](const auto& result){return result.content_validated && result.content.message.data==sent.data;});
        if(found==receiver.ended.end())throw std::runtime_error(std::string(keyed?"keyed":"public")+" SF"+std::to_string(spreading)+" sampled lost-symbol stream failed"+(receiver.ended.empty()?" without end event":": "+receiver.ended.back().error));
        check(found->content.corrected_bytes>0 && found->missing_symbols>=3,"marker/data gap positions must engage RS without shifting bytes");
        const auto& stats=found->content.fec_stats;
        check(stats.data.corrected_bytes>0 && stats.data.repaired_bytes>0 && stats.data.erased_bytes>0 &&
              stats.parity.repaired_bytes>0 && stats.parity.erased_bytes>0,
              "blanked PCM data and final parity symbols must produce visible RS repairs");
        const auto& accuracy=found->content.pre_fec_accuracy;
        const auto data_bits=wire.size()/1216*interval_data_bytes(value.fec,keyed)*8;
        check(accuracy && accuracy->missing_data_bits>0 && accuracy->received_data_bits>0 &&
              accuracy->received_data_bits+accuracy->missing_data_bits==data_bits &&
              accuracy->corrected_data_bits<=accuracy->received_data_bits &&
              stats.data.missing_bits==accuracy->missing_data_bits && stats.parity.missing_bits>0,
              "sampled timed gaps must retain known-bit accuracy with explicit missing-data coverage");
        check(found->content.authenticated==keyed,"only keyed sampled stream authenticates");
    }
}
void sub_six_second_gap_is_one_stream() {
    auto value=options();value.compression=false;const auto sent=message();
    auto pcm=transfer::transmit(sent,value);const auto symbol=modem::symbol_sample_count(value.modem);
    const auto start=modem::training_sample_count(value.modem);
    // Gap in the parity tail is bounded and does not create a second stream.
    const auto first=start+(192+1000)*symbol;
    std::fill(pcm.begin()+static_cast<std::ptrdiff_t>(first),pcm.begin()+static_cast<std::ptrdiff_t>(first+8*symbol),0.f);
    Harness receiver(value);receiver.feed(pcm);receiver.silence(7);
    check(receiver.ended.size()==1 && receiver.ended.front().content_validated && receiver.ended.front().content.message.data==sent.data,"short weak run preserves one physical stream and recoverable coding interval");
}
void data_symbol_schedule_seeks() {
    auto value=options(true);
    // 6,000 one-chip symbols per second exercise both sides of the 512-byte
    // Data cache boundary before the next epoch, plus multiple epoch resets.
    value.modem.sample_rate=48000;value.modem.bandwidth_hz=12000;
    value.modem.carrier_hz=12000;value.modem.spreading_factor=1;
    value.modem.integration_seconds=0;
    check(modem::symbol_sample_count(value.modem)==8,
          "Data seek fixture must use 6,000 symbols per second");
    Bytes plain(13031);
    for(std::size_t i=0;i<plain.size();++i)plain[i]=static_cast<std::uint8_t>((i/3+i/17)%2);
    auto encrypted=plain;transfer::xor_binary_bits(encrypted,value);
    for(std::size_t second=0;second<3;++second) {
        const auto mask=value.key->stream(StreamPurpose::Data,value.timestamp+second,0,750);
        const auto first=second*6000,last=std::min(first+6000,plain.size());
        for(auto i=first;i<last;++i) {
            const auto local=i-first;
            const auto expected=plain[i]^((mask[local/8]>>(7-local%8))&1U);
            check(encrypted[i]==expected,
                  "Data masking must select the scheduled second and its MSB-first local bit");
        }
    }
    auto chunked=plain;
    const std::array<std::size_t,5> sizes{1,7,509,4093,601};
    for(std::size_t first=0,chunk=0;first<chunked.size();++chunk) {
        const auto count=std::min(sizes[chunk%sizes.size()],chunked.size()-first);
        transfer::xor_binary_bits(std::span(chunked).subspan(first,count),value,first);
        first+=count;
    }
    check(chunked==encrypted,"Data mask chunk boundaries must not change cache or epoch addressing");
    for(const std::size_t first:{7U,4095U,4096U,5999U,6000U,6001U,11999U,12000U}) {
        const auto count=std::min<std::size_t>(43,encrypted.size()-first);
        Bytes fragment(encrypted.begin()+static_cast<std::ptrdiff_t>(first),
                       encrypted.begin()+static_cast<std::ptrdiff_t>(first+count));
        transfer::xor_binary_bits(fragment,value,first);
        check(std::equal(fragment.begin(),fragment.end(),plain.begin()+static_cast<std::ptrdiff_t>(first)),
              "non-byte-aligned fragments must decrypt across cache and whole-second boundaries");
    }
}
void data_symbol_schedule_rebase() {
    for(const double seconds:{.3,4*3600+.5}) {
        auto value=options(true);value.modem.integration_seconds=seconds;value.modem.dsss=true;
        const auto samples=modem::symbol_sample_count(value.modem);
        const auto rate=static_cast<std::uint64_t>(value.modem.sample_rate);
        const Bytes plain{0,1,0,0,1,1,0,1,0,1,1,1,0};
        auto encrypted=plain;transfer::xor_binary_bits(encrypted,value);
        for(std::size_t i=0;i<plain.size();++i) {
            const auto start=static_cast<std::uint64_t>(i)*samples;
            const auto epoch=value.timestamp+start/rate,ordinal=(start%rate)/samples;
            const auto mask=value.key->stream(StreamPurpose::Data,epoch,ordinal/8,1);
            check(encrypted[i]==(plain[i]^((mask.front()>>(7-ordinal%8))&1U)),
                  "short and hours-long Data symbols must use their own start-second masks");
        }
        // A .3-second schedule first enters second 1 at phase .2; a
        // 4-hour+.5-second schedule enters its second symbol at phase .5.
        const std::size_t skipped=seconds<1?4:1;
        const auto start=static_cast<std::uint64_t>(skipped)*samples;
        auto later=value;later.timestamp+=start/rate;later.modem.stream_phase_samples=start%rate;
        check(later.modem.stream_phase_samples!=0,
              "rebasing fixture must require a fractional start phase");
        Bytes remaining(encrypted.begin()+static_cast<std::ptrdiff_t>(skipped),encrypted.end());
        transfer::xor_binary_bits(remaining,later);
        check(std::equal(remaining.begin(),remaining.end(),plain.begin()+static_cast<std::ptrdiff_t>(skipped)),
              "independent later-epoch Data decryption must not require the first message epoch");
        modem::PatternBurst burst;burst.bits.assign(encrypted.begin()+static_cast<std::ptrdiff_t>(skipped),encrypted.end());
        burst.stream_phase_samples=later.modem.stream_phase_samples;burst.score=100;burst.complete=true;
        auto context=value; // The evidence, rather than caller settings, supplies the recovered phase.
        const auto decoded=transfer::interpret_pattern(burst,context,later.timestamp);
        check(decoded.raw_bits==remaining,
              "interpret_pattern must use the phase recovered with the surviving symbol");
        const auto initial_config=transfer::seeded_config(value,value.timestamp);
        const auto later_config=transfer::seeded_config(later,later.timestamp);
        check(initial_config.spreading_seed==later_config.spreading_seed &&
              initial_config.dsss_seed==later_config.dsss_seed,
              "waveform purpose roots must be independent of the original message epoch");
        modem::PatternCode initial(initial_config,value.timestamp),rebased(later_config,later.timestamp);
        const auto chips=initial.chips_per_symbol();
        for(std::uint64_t symbol=0;symbol<3;++symbol)
            for(const auto local:std::array<std::uint64_t,3>{0,1,chips-1})
                check(initial.value((skipped+symbol)*chips+local,0)==rebased.value(symbol*chips+local,0),
                      "rebased pattern and Data streams must describe the same surviving symbols");
    }
}
}
int main(){try{data_symbol_schedule_seeks();data_symbol_schedule_rebase();six_second_end_and_eof();pcm_symbol_loss();sub_six_second_gap_is_one_stream();std::cout<<"pattern stream transfer passed\n";}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

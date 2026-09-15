#include "datapump/transfer.hpp"
#include "datapump/compression.hpp"
#include "datapump/channel.hpp"
#include "datapump/tuning.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_code.hpp"
#include <algorithm>
#include <iostream>
#include <cmath>
#include <array>
#include <limits>

using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
transfer::Options options(bool keyed=false) {
    transfer::Options result;result.timestamp=1700000000;result.search_seconds=1;
    result.dsp_workspace_bytes=8*1024*1024;result.content_limit=1024*1024;
    if(keyed){std::array<std::uint8_t,32> seed{};seed[0]=91;result.key=Crypto(seed);}
    result.modem=tuning::resolve(1200,40,tuning::PatternMode::auto_pattern,keyed).config;
    return result;
}
void exact_short_text() {
    Message message;message.data={'e'};
    for(bool keyed:{false,true}) {
        auto value=options(keyed);
        const auto bits=transfer::message_bits(message,value);
        check(bits.size()==3,"dictionary character must occupy exactly three meaningful bits");
        for(auto fec:{FecMode::off,FecMode::rs20,FecMode::rs60}) {
            value.fec=fec;value.compression=false;
            PacketLayout layout;
            const auto estimate=transfer::estimate(message,value,&layout);
            const auto content_samples=3*modem::symbol_sample_count(value.modem);
            const auto hardware_samples=modem::training_sample_count(value.modem)+2*modem::pattern_pulse_padding_samples(value.modem);
            check(estimate.waveform_samples==hardware_samples+content_samples,"short text adds hardware settling and pulse tails to its exact three-bit payload");
            check(std::abs(estimate.content_seconds-static_cast<double>(content_samples)/value.modem.sample_rate)<1e-12 &&
                  std::abs(estimate.total_seconds-estimate.content_seconds-static_cast<double>(hardware_samples)/value.modem.sample_rate)<1e-12,
                  "hardware settling airtime cannot be charged as meaningful content");
            check(layout.header_bytes==0 && layout.integrity_bytes==0,"short text inspection has no packet fields");
        }
        modem::ChannelConfig channel;channel.snr_db=6;channel.clock_error_ppm=100;channel.phase_noise_degrees_per_sqrt_second=.5;
        channel.receiver_timestamp=value.timestamp+1;
        const auto received=transfer::simulate(message,value,channel);
        check(received.raw_bits==bits,"blind PCM short-text reception preserves exact bits");
        check(received.packet.message.data==message.data,"dictionary interpretation uses detected exact endpoint");
        check(!received.packet_validated && !received.packet.authenticated,"pattern confidence does not claim packet validation or authentication");
    }
}
void raw_bits() {
    auto value=options();
    for(unsigned word=0;word<8;++word) {
        Bytes bits{static_cast<std::uint8_t>((word>>2)&1),static_cast<std::uint8_t>((word>>1)&1),static_cast<std::uint8_t>(word&1)};
        auto transmitter=transfer::binary_transmitter(bits,value);
        std::vector<float> pcm(137+static_cast<std::size_t>(transmitter->total_samples())+2*modem::symbol_sample_count(value.modem));
        std::size_t written=0;
        while(!transmitter->finished())written+=transmitter->read(std::span(pcm).subspan(137+written,std::min<std::size_t>(37,transmitter->total_samples()-written)));
        const auto received=transfer::receive(pcm,value);
        check(received.raw_bits==bits,"all eight three-bit messages decode without supplied bit count");
        if(word==1) {
            const auto payload_start=137+modem::training_sample_count(value.modem)+modem::pattern_pulse_padding_samples(value.modem);
            const auto without_prefix=transfer::receive(std::span<const float>(pcm).subspan(payload_start),value);
            check(without_prefix.raw_bits==bits,"a receiver that misses all hardware settling still acquires the exact payload");
        }
    }
}
void short_raw_interpretation() {
    const auto value=options();
    for(unsigned length=1;length<=4;++length)for(unsigned word=0;word<(1U<<length);++word) {
        modem::PatternBurst burst;burst.complete=true;burst.score=25.;
        for(unsigned bit=length;bit;--bit)burst.bits.push_back(static_cast<std::uint8_t>((word>>(bit-1))&1U));
        const auto received=transfer::interpret_pattern(burst,value,value.timestamp);
        check(received.raw_bits==burst.bits,"one- to four-bit patterns must retain exact leading zeros and endpoints");
        check(!received.packet_validated && !received.packet.authenticated,
              "raw short patterns cannot claim packet validation or authentication");
        Bytes decoded;
        if(length==3 && word<5)decoded={static_cast<std::uint8_t>(" etao"[word])};
        else if(length==4 && (word==10 || word==11))decoded={static_cast<std::uint8_t>(word==10?'i':'n')};
        check(received.packet.message.data==decoded,
              "only complete three- or four-bit dictionary tokens should decode into short text");
        if(length==3 && word==2)check(received.packet.message.data==Bytes{'t'} && received.raw_bits==Bytes({0,1,0}),
                                     "010 must retain its raw pattern while decoding as lowercase t");
    }
}
void packet_downstream() {
    auto value=options();value.fec=FecMode::off;Message message;message.data=Bytes(16,'e');
    const auto bits=transfer::message_bits(message,value);
    check(bits.size()%8==0 && bits.size()>128,"long content retains optional packet grammar");
    auto burst=modem::PatternBurst{};burst.bits=bits;burst.complete=true;
    auto decoded=transfer::interpret_pattern(burst,value,value.timestamp);
    check(decoded.packet_validated && decoded.packet.message.data==message.data,"packet parsing remains downstream of acquired bits");
    burst.bits.back()^=1;
    decoded=transfer::interpret_pattern(burst,value,value.timestamp);
    check(!decoded.packet_validated && decoded.raw_bits==burst.bits,"failed packet integrity cannot discard pattern-supported raw bits");
    value.content_limit=message.data.size();
    check(transfer::estimate(message,value).memory_supported,"encoded packet overhead uses its own workspace at the content limit");
    auto source=transfer::message_transmitter(message,value);
    check(source->total_samples()==modem::training_sample_count(value.modem)+2*modem::pattern_pulse_padding_samples(value.modem)+bits.size()*modem::symbol_sample_count(value.modem),"packet bit count remains exact at the content limit with separate hardware settling and pulse tails");
    message.repeatable=true;value.repeat_policy.minimum_payload_bytes=0;value.repeat_policy.maximum_seconds=.001;
    bool rejected=false;
    try{(void)transfer::message_transmitter(message,value);}catch(const Error&){rejected=true;}
    check(rejected,"pattern transmission retains the sender's repeatable airtime policy");
}
void public_late_symbol_interpretation() {
    const auto value=options();Message text;text.data={'e'};
    modem::PatternBurst burst;burst.bits=transfer::message_bits(text,value);
    burst.first_stream_symbol=7;burst.score=45;burst.complete=true;
    const auto received=transfer::interpret_pattern(burst,value,value.timestamp);
    check(received.packet.message.data==text.data,"public patterns do not require an absolute secret stream index to interpret short text");
    check(received.diagnostics.pattern_score==burst.score,"a completed burst retains its own evidence score after receiver tracks drain");
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
void long_symbol_estimate() {
    auto value=options();value.modem.integration_seconds=3600;
    const auto estimate=transfer::estimate_binary(Bytes{0,0,1},value);
    check(modem::training_sample_count(value.modem)==0 && estimate.content_seconds==10800 &&
          std::abs(estimate.total_seconds-estimate.content_seconds-
              2.*static_cast<double>(modem::pattern_pulse_padding_samples(value.modem))/value.modem.sample_rate)<1e-9 && estimate.memory_supported,
          "three hour-long symbols add only bounded pulse tails without a retained waveform");
}
void private_workspace_estimate() {
    for(const bool dsss:{false,true})for(const bool settling:{false,true}) {
        auto value=options(true);value.dsp_workspace_bytes=256*1024;value.modem.dsss=dsss;
        if(!settling)value.modem.integration_seconds=3600;
        const auto tiny=transfer::binary_transmitter(Bytes{0},value);
        const auto fixed=tiny->working_bytes()-1;
        const auto boundary=value.dsp_workspace_bytes/4-fixed;
        for(const int offset:{-1024,-256,256,1024}) {
            const auto size=static_cast<std::size_t>(static_cast<std::ptrdiff_t>(boundary)+offset);
            const Bytes bits(size,0);
            const auto estimate=transfer::estimate_binary(bits,value);
            bool admitted=false,exact_duration=true;
            try {
                const auto source=transfer::binary_transmitter(bits,value);
                admitted=source->working_bytes()<=value.dsp_workspace_bytes/4;
                exact_duration=source->total_samples()==estimate.waveform_samples;
            } catch(const Error&) {}
            check(exact_duration,"workspace estimation changed the exact private pattern duration");
            check(estimate.memory_supported==admitted,
                  "private workspace estimate omitted enabled preamble keystream caches");
        }
    }
}
void marked_packet_waveform() {
    Message message;message.kind=MessageKind::file;message.filename="boundary.bin";
    message.data=Bytes(300,'r');
    for(bool keyed:{false,true}) {
        auto value=options(keyed);value.compression=false;value.search_seconds=0;
        check(transfer::message_wire_bits(message,value).size()>transfer::message_bits(message,value).size(),
              "waveform fixture must cross at least one periodic recovery word");
        modem::ChannelConfig channel;channel.snr_db=30;channel.clock_error_ppm=0;
        channel.phase_noise_degrees_per_sqrt_second=0;
        const auto received=transfer::simulate(message,value,channel);
        check(received.packet_validated && received.packet.authenticated==keyed &&
              received.packet.message.data==message.data,
              "the unchanged constellation decoder must deliver a marker-bearing encrypted or clear packet");
    }
}
transfer::Options high_snr_options(bool keyed) {
    auto value=options(keyed);
    value.modem=tuning::resolve(12000,80,tuning::PatternMode::auto_pattern,keyed).config;
    value.automatic_receive_profiles=true;
    value.receive_targets_db_hz={80};
    return value;
}
modem::ChannelConfig high_snr_channel(const transfer::Options& value) {
    modem::ChannelConfig channel;
    // Channel noise occupies Fs/2; the planner's target is C/N0 in one hertz.
    channel.snr_db=80-10*std::log10(static_cast<double>(value.modem.sample_rate)/2);
    channel.delay_samples=137;
    channel.receiver_timestamp=value.timestamp+1;
    return channel;
}
void high_snr_short_patterns() {
    Message message;message.data={'e'};
    for(const bool keyed:{false,true}) {
        auto value=high_snr_options(keyed);
        value.modem.dsss=keyed;
        const auto bits=transfer::message_bits(message,value);
        const auto estimate=transfer::estimate(message,value);
        check(bits==Bytes({0,0,1}),"the fast profile must preserve the exact dictionary pattern");
        check(estimate.waveform_samples==modem::training_sample_count(value.modem)+
              2*modem::pattern_pulse_padding_samples(value.modem)+
              bits.size()*modem::symbol_sample_count(value.modem),
              "fast short text must retain exact bits with settling and pulse tails");
        auto channel=high_snr_channel(value);channel.seed=keyed?13:7;
        const auto received=transfer::simulate(message,value,channel);
        check(received.raw_bits==bits && received.packet.message.data==message.data,
              "fast public and private dictionary patterns must survive unsynchronized impaired PCM");
        check(!received.packet_validated && !received.packet.authenticated,
              "fast pattern confidence must not invent packet integrity or authentication");
    }

    auto value=high_snr_options(true);value.modem.dsss=true;
    const Bytes bits{0,1,0,0};
    auto transmitter=transfer::binary_transmitter(bits,value);
    modem::SampledSimulationChannel channel(transfer::seeded_config(value,value.timestamp),high_snr_channel(value));
    std::array<float,347> chunk{};std::vector<float> pcm;
    while(const auto count=channel.read(*transmitter,chunk))
        pcm.insert(pcm.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(count));
    std::vector<float> tail(2*modem::symbol_sample_count(value.modem));channel.read_noise(tail);
    pcm.insert(pcm.end(),tail.begin(),tail.end());
    ++value.timestamp;
    const auto received=transfer::receive(pcm,value);
    check(received.raw_bits==bits,"fast encrypted raw reception must preserve leading zeros and the exact endpoint");
    check(!received.packet_validated && !received.packet.authenticated,
          "short encrypted raw bits have no packet authentication tag");
}
void high_snr_marked_file() {
    Message message;message.kind=MessageKind::file;message.filename="fast-boundary.bin";
    for(unsigned i=0;i<300;++i)message.data.push_back(static_cast<std::uint8_t>((i*131+17)&255));
    for(const bool dsss:{false,true}) {
        auto value=high_snr_options(true);value.modem.dsss=dsss;value.compression=false;
        check(value.modem.scramble,"the fast encrypted plan must retain its keyed acquisition waveform");
        check(transfer::message_wire_bits(message,value).size()>transfer::message_bits(message,value).size(),
              "fast file reception must cross a masked periodic byte-boundary recovery word");
        auto channel=high_snr_channel(value);channel.seed=dsss?13:7;
        const auto received=transfer::simulate(message,value,channel);
        check(received.packet_validated && received.packet.authenticated &&
              received.packet.message.filename==message.filename && received.packet.message.data==message.data,
              "fast keyed file reception must preserve boundary recovery, encryption and authentication with clock drift");
    }
}
void centered_radio_packet() {
    Message message;message.kind=MessageKind::file;message.filename="radio.bin";
    message.id.fill(0x5c);
    for(unsigned i=0;i<16;++i)message.data.push_back(static_cast<std::uint8_t>(i*37));
    for(const double cn0:{40.,80.})for(const bool keyed:{false,true}) {
        auto value=options(keyed);value.compression=false;value.fec=FecMode::off;
        value.modem=tuning::resolve(3600,cn0,tuning::PatternMode::auto_pattern,keyed,1500).config;
        value.modem.dsss=keyed;
        check(value.modem.carrier_hz<value.modem.bandwidth_hz/2 && value.modem.pulse_shaping,
              "the radio fixture must exercise the shaped band below the old nominal carrier limit");
        if(cn0==80)check(value.modem.spreading_factor==16,
              "the strong radio fixture must exercise the GUI default's short automatic pattern");
        modem::ChannelConfig channel;
        // Keep each planner target in physical units regardless of
        // the internal sample clock. The sampled channel supplies an unknown
        // fractional start and carrier phase; acquisition also searches time.
        channel.snr_db=cn0-10*std::log10(static_cast<double>(value.modem.sample_rate)/2);
        channel.clock_error_ppm=keyed?-100:100;channel.delay_samples=137;
        channel.phase_noise_degrees_per_sqrt_second=.5;channel.seed=keyed?13:7;
        channel.receiver_timestamp=value.timestamp+1;
        auto expected=transfer::message_wire_bits(message,value);
        transfer::xor_binary_bits(expected,value); // Receive exposes decrypted bits, including recovery markers.
        const auto received=transfer::simulate(message,value,channel);
        if(received.raw_bits!=expected)
            throw Error(std::string(keyed?"private":"public")+" centered radio packet bits differ at "+
                std::to_string(cn0)+" dB-Hz: recovered "+
                std::to_string(received.raw_bits.size())+" bits, expected "+std::to_string(expected.size()));
        check(received.packet_validated &&
              received.packet.authenticated==keyed && received.packet.message.data==message.data &&
              received.packet.message.filename==message.filename,
              "3.6 kHz at 1500 Hz must acquire and validate public and private packets from impaired PCM at target C/N0");
    }
}
void high_snr_large_file_estimate() {
    auto value=high_snr_options(true);value.modem.dsss=true;
    value.compression=false;value.fec=FecMode::off;value.dsp_workspace_bytes=64*1024*1024;
    Message message;message.kind=MessageKind::file;message.filename="megabyte.bin";
    message.data=Bytes(value.content_limit,0xa5);
    for(const auto fec:{FecMode::off,FecMode::rs60}) {
        value.fec=fec;
        const auto fast=transfer::estimate(message,value);
        auto conservative=value;conservative.modem.spreading_factor=64;
        const auto slow=transfer::estimate(message,conservative);
        check(fast.memory_supported && !fast.batch_memory_supported,
              "megabyte files must remain streamable without retaining hours of fast-profile PCM");
        check(fast.packet_bytes==slow.packet_bytes && fast.content_bytes==message.data.size(),
              "faster automatic patterns must preserve the encrypted packet's content and overhead");
        check(fast.content_seconds*4<=slow.content_seconds+1e-9,
              "the high-C/N0 automatic profile must materially improve long-file airtime");
        check(std::abs(fast.total_seconds-static_cast<double>(fast.waveform_samples)/value.modem.sample_rate)<1e-9,
              "large-file estimates must use the fast profile's actual quantized PCM duration");
        // Check full-size bit storage and downstream grammar independently of
        // the shorter impaired-PCM file reception test above.
        modem::PatternBurst burst;burst.bits=transfer::message_wire_bits(message,value);
        burst.complete=true;burst.score=100;
        check(burst.bits.size()>transfer::packet_workspace_limit(value.content_limit),
              "the fixture must exceed the old packed-byte limit after bit and marker expansion");
        const auto received=transfer::interpret_pattern(std::move(burst),value,value.timestamp);
        check(received.packet_validated && received.packet.authenticated && received.packet.message.data==message.data,
              "full advertised file capacity must retain masked boundary recovery and authentication with every FEC expansion");
    }
}
void pattern_storage_limits() {
    auto value=high_snr_options(true);value.content_limit=1;value.compression=false;
    Message message;message.kind=MessageKind::file;message.filename="tiny.bin";
    for(const std::size_t size:{0U,1U}) {
        message.data=Bytes(size,0x5a);
        modem::PatternBurst burst;burst.bits=transfer::message_wire_bits(message,value);burst.complete=true;
        const auto received=transfer::interpret_pattern(std::move(burst),value,value.timestamp);
        check(received.packet_validated && received.packet.authenticated && received.packet.message.data==message.data,
              "independent pattern bit storage must preserve tiny content limits and packet overhead");
    }
    message.data.push_back(0);
    bool rejected=false;
    try{(void)transfer::message_wire_bits(message,value);}catch(const Error&){rejected=true;}
    check(rejected,"expanded pattern storage must not increase the admitted content limit");
    message.data.resize(1);
    // Exercise byte-to-bit overflow and the separate recovery-word expansion
    // before an allocation, using a one-byte message under an absurd quota.
    for(const auto limit:{(std::numeric_limits<std::size_t>::max()-65568)/8,
                          (Bytes{}.max_size()/8-65536)/8}) {
        value.content_limit=limit;rejected=false;
        try{(void)transfer::message_wire_bits(message,value);}catch(const Error&){rejected=true;}
        check(rejected,"pattern bit and recovery-word capacity arithmetic must reject address-space overflow");
    }
}
}
int main(){try{data_symbol_schedule_seeks();data_symbol_schedule_rebase();exact_short_text();raw_bits();short_raw_interpretation();packet_downstream();public_late_symbol_interpretation();long_symbol_estimate();private_workspace_estimate();marked_packet_waveform();high_snr_short_patterns();high_snr_marked_file();centered_radio_packet();high_snr_large_file_estimate();pattern_storage_limits();std::cout<<"pattern transfer tests passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

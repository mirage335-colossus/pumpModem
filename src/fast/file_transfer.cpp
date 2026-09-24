#include "datapump/fast/file_transfer.hpp"
#include "datapump/fast/codec.hpp"
#include "datapump/fast/compression.hpp"
#include "datapump/fast/attachment.hpp"
#include "datapump/fast/modem.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>

namespace datapump::fast {
namespace {
void put16(std::span<std::uint8_t> b,std::uint16_t n) {b[0]=static_cast<std::uint8_t>(n);b[1]=static_cast<std::uint8_t>(n>>8);}
void put32(std::span<std::uint8_t> b,std::uint32_t n) {for(unsigned i=0;i<4;++i)b[i]=static_cast<std::uint8_t>(n>>(8*i));}
std::uint16_t get16(std::span<const std::uint8_t> b) {return static_cast<std::uint16_t>(b[0]|b[1]<<8);}
std::uint32_t get32(std::span<const std::uint8_t> b) {std::uint32_t n=0;for(unsigned i=0;i<4;++i)n|=static_cast<std::uint32_t>(b[i])<<(8*i);return n;}
struct Writer {
    std::filesystem::path path;std::FILE* file=nullptr;std::uint32_t rate;std::uint64_t bytes=0;bool committed=false;
    Writer(const std::filesystem::path& p,std::uint32_t r):path(p),rate(r) {
#ifdef _WIN32
        file=_wfopen(path.c_str(),L"wbx");
#else
        file=std::fopen(path.c_str(),"wbx");
#endif
        if(!file)throw Error("Cannot create fast WAV (destination may exist)");
        std::array<std::uint8_t,44> header{};
        if(std::fwrite(header.data(),1,header.size(),file)!=header.size()) {std::fclose(file);file=nullptr;std::error_code ec;std::filesystem::remove(path,ec);throw Error("Cannot write fast WAV header");}
    }
    ~Writer(){if(file)std::fclose(file);if(!committed){std::error_code ec;std::filesystem::remove(path,ec);}}
    void write(std::span<const float> samples) {
        if(samples.size()>4096)throw Error("Fast WAV writer chunk exceeds local bound");
        if(samples.size()*2>std::numeric_limits<std::uint32_t>::max()-36-bytes)throw Error("Fast WAV exceeds the RIFF 4 GiB limit");
        std::array<std::uint8_t,8192> data{};
        for(std::size_t i=0;i<samples.size();++i) {
            if(!std::isfinite(samples[i]))throw Error("Non-finite fast audio sample");
            const auto n=static_cast<std::int16_t>(std::lround(std::clamp(samples[i],-1.f,1.f)*32767));
            put16(std::span(data).subspan(i*2,2),static_cast<std::uint16_t>(n));
        }
        const auto count=samples.size()*2;
        if(std::fwrite(data.data(),1,count,file)!=count)throw Error("Fast WAV output write failed");
        bytes+=count;
    }
    void finish() {
        std::array<std::uint8_t,44> h{};
        std::copy_n("RIFF",4,h.begin());put32(std::span(h).subspan(4),static_cast<std::uint32_t>(bytes+36));
        std::copy_n("WAVEfmt ",8,h.begin()+8);put32(std::span(h).subspan(16),16);
        put16(std::span(h).subspan(20),1);put16(std::span(h).subspan(22),1);
        put32(std::span(h).subspan(24),rate);put32(std::span(h).subspan(28),rate*2);
        put16(std::span(h).subspan(32),2);put16(std::span(h).subspan(34),16);
        std::copy_n("data",4,h.begin()+36);put32(std::span(h).subspan(40),static_cast<std::uint32_t>(bytes));
        if(std::fseek(file,0,SEEK_SET)||std::fwrite(h.data(),1,h.size(),file)!=h.size()||std::fflush(file))throw Error("Cannot finalize fast WAV");
        auto* output=file;file=nullptr;
        if(std::fclose(output))throw Error("Cannot close fast WAV output");
        committed=true;
    }
};
struct Reader {
    std::ifstream file;std::uint32_t rate=0;unsigned channels=0;std::uint64_t remaining=0;
    void exact(std::span<std::uint8_t> out) {file.read(reinterpret_cast<char*>(out.data()),static_cast<std::streamsize>(out.size()));if(static_cast<std::size_t>(file.gcount())!=out.size())throw Error("Truncated fast WAV container");}
    explicit Reader(const std::filesystem::path& path) {
        if(!std::filesystem::is_regular_file(path))throw Error("Fast WAV must be a readable regular file");
        file.open(path,std::ios::binary);
        if(!file)throw Error("Cannot open fast WAV");
        std::array<std::uint8_t,12> h{};exact(h);
        if(!std::equal(h.begin(),h.begin()+4,"RIFF")||!std::equal(h.begin()+8,h.end(),"WAVE"))throw Error("Expected a RIFF/WAVE file");
        const auto file_size=std::filesystem::file_size(path);const std::uint64_t extent=static_cast<std::uint64_t>(get32(std::span(h).subspan(4)))+8;
        if(extent>file_size||extent<12)throw Error("Invalid WAV container extent");
        std::uint64_t position=12;bool format=false;
        // RIFF is a local file format. Bounds are checked before every seek;
        // no container field determines a modem interval or source allocation.
        for(unsigned chunks=0;chunks<1024&&position+8<=extent;++chunks) {
            std::array<std::uint8_t,8> chunk{};exact(chunk);position+=8;
            const auto size=get32(std::span(chunk).subspan(4));const std::uint64_t padded=static_cast<std::uint64_t>(size)+(size&1);
            if(padded>extent-position)throw Error("WAV chunk exceeds local container");
            if(std::equal(chunk.begin(),chunk.begin()+4,"fmt ")) {
                if(format||size<16||size>64)throw Error("Unsupported WAV format chunk");
                std::array<std::uint8_t,64> f{};exact(std::span(f).first(size));
                channels=get16(std::span(f).subspan(2));rate=get32(std::span(f).subspan(4));
                if(get16(f)!=1||get16(std::span(f).subspan(14))!=16||channels<1||channels>2||
                   get16(std::span(f).subspan(12))!=channels*2||rate<44100||rate>192000||
                   get32(std::span(f).subspan(8))!=rate*channels*2)throw Error("Fast WAV requires 16-bit PCM, mono/stereo, 44100..192000 Hz");
                if(size&1)file.seekg(1,std::ios::cur);
                format=true;
            } else if(std::equal(chunk.begin(),chunk.begin()+4,"data")) {
                if(!format||size%(channels*2))throw Error("Invalid WAV sample data");
                remaining=size;return;
            } else file.seekg(static_cast<std::streamoff>(padded),std::ios::cur);
            if(!file)throw Error("Cannot seek WAV chunk");
            position+=padded;
        }
        throw Error("WAV has no bounded PCM data region");
    }
    std::size_t read(std::span<float> output,audio::ChannelMode mode) {
        const auto frames=static_cast<std::size_t>(std::min<std::uint64_t>({remaining/(channels*2),output.size(),4096}));
        std::array<std::uint8_t,16384> bytes{};exact(std::span(bytes).first(frames*channels*2));remaining-=frames*channels*2;
        for(std::size_t i=0;i<frames;++i) {
            const auto left=static_cast<std::int16_t>(get16(std::span(bytes).subspan(i*channels*2)));
            const auto right=channels==2?static_cast<std::int16_t>(get16(std::span(bytes).subspan(i*4+2))):left;
            output[i]=mode==audio::ChannelMode::left_mono?left/32768.f:
                mode==audio::ChannelMode::right_mono?right/32768.f:(left/65536.f+right/65536.f);
        }
        return frames;
    }
};
void check_settings(const Settings& s) {
    validate(s.profile);
    if(s.quota_bytes<65536||s.quota_bytes>256ULL*1024*1024)
        throw Error("Fast storage quota must be 64 KiB..256 MiB");
}
Snapshot transmit_source_wave(const Settings& s,PreparedXzSource input,const std::filesystem::path& wave,ProgressCallback progress,std::stop_token stop) {
    const auto estimate=estimate_transmission(s.profile,s.key.has_value(),input.encoded.size());
    std::uint64_t generated=0;
    const auto source_bytes=input.source_bytes;
    StreamEncoder codec(s.profile,s.key,byte_source(std::move(input.encoded)),SourceEncoding::xz);
    Transmitter modem(s.profile,[&](auto bits){return codec.next_interval(bits);});
    Writer writer(wave,s.profile.sample_rate);std::array<float,4096> block{};Snapshot result;result.transmitting=true;result.encrypted=s.key.has_value();result.estimated_seconds=estimate.seconds;
    const auto start=std::chrono::steady_clock::now();
    while(!stop.stop_requested()) {
        const auto n=modem.read(block);if(!n)break;writer.write(std::span(block).first(n));
        generated+=n;result.transmit_fraction=std::min(.999,static_cast<double>(generated)/static_cast<double>(estimate.samples));
        result.source_bytes=source_bytes;result.intervals=codec.intervals_emitted();++result.revision;
        if(progress)progress(result);
    }
    if(stop.stop_requested())throw Error("Fast WAV transmission cancelled");
    block.fill(0);auto silence=end_silence_samples(s.profile);
    while(silence){if(stop.stop_requested())throw Error("Fast WAV transmission cancelled");auto n=static_cast<std::size_t>(std::min<std::uint64_t>(silence,block.size()));writer.write(std::span(block).first(n));silence-=n;}
    writer.finish();result.transmit_fraction=1;result.transmitting=false;result.status="Fast waveform saved with observed-silence tail";
    result.elapsed_seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();return result;
}
}
Snapshot transmit_wave(const Settings& s,const std::filesystem::path& source,const std::filesystem::path& wave,ProgressCallback progress,std::stop_token stop) {
    check_settings(s);
    if(!std::filesystem::is_regular_file(source))throw Error("Fast source must be a readable regular file");
    if(std::filesystem::file_size(source)>s.quota_bytes)throw Error("Fast source exceeds local quota");
    return transmit_source_wave(s,prepare_xz_attachment(file_source(source),attachment::filename_from_path(source),s.quota_bytes,stop),wave,std::move(progress),stop);
}
Snapshot transmit_text_wave(const Settings& s,const std::string& text,const std::filesystem::path& wave,ProgressCallback progress,std::stop_token stop) {
    check_settings(s);
    if(text.size()>text_byte_limit||text.size()>s.quota_bytes)throw Error("Fast text exceeds the local 32768-byte limit");
    return transmit_source_wave(s,prepare_xz_source(byte_source(Bytes(text.begin(),text.end())),s.quota_bytes,stop),wave,std::move(progress),stop);
}
Snapshot receive_wave(const Settings& settings,const std::filesystem::path& wave,ProgressCallback progress,std::stop_token stop) {
    Reader input(wave);auto s=settings;s.profile.sample_rate=input.rate;check_settings(s);
    StreamDecoder codec(s.profile,s.key,s.quota_bytes,SourceEncoding::xz);Receiver modem(s.profile,[&](auto bits){codec.push_interval(bits);});
    Snapshot result;result.listening=true;result.encrypted=s.key.has_value();std::array<float,4096> block{};std::uint64_t samples=0;
    while(!stop.stop_requested()) {
        const auto n=input.read(block,audio::output_channels(s.mono,s.channel_mode));if(!n)break;modem.push(std::span(block).first(n));samples+=n;
        auto c=codec.snapshot();const auto& d=modem.progress();result.intervals=c.intervals;result.authenticated_groups=c.authenticated_groups;
        result.checksum_groups=c.checksum_groups;
        result.ldpc_frames=c.ldpc_frames;result.ldpc_failed_frames=c.ldpc_failed_frames;
        result.ldpc_iterations=c.ldpc_iterations;result.ldpc_changed_bits=c.ldpc_changed_bits;
        result.decoding_stopped=c.decoding_stopped;result.coding_cycles=c.coding_cycles;
        result.failed_cycles=c.failed_cycles;result.verified_bytes=c.verified_bytes;
        result.corrected_bytes=c.corrected_bytes;result.erased_bytes=c.erased_bytes;result.evm=d.evm;result.status=c.status;++result.revision;
        if(progress)progress(result);
        if(d.physical_complete)break;
    }
    modem.finish();result.physical_complete=!stop.stop_requested()&&modem.progress().physical_complete;
    codec.finish(result.physical_complete);const auto c=codec.snapshot();result.complete=c.complete;result.file=codec.result();result.source_bytes=c.source_bytes;
    result.encrypted=c.encrypted;result.authenticated=c.authenticated;result.authenticated_groups=c.authenticated_groups;result.checksum_groups=c.checksum_groups;
    result.decoding_stopped=c.decoding_stopped;result.coding_cycles=c.coding_cycles;
    result.failed_cycles=c.failed_cycles;result.verified_bytes=c.verified_bytes;
    result.cancelled=stop.stop_requested();result.status=c.status;result.listening=false;
    if(result.cancelled) {
        result.complete=false;result.authenticated=false;result.file.reset();
        result.status="Cancelled; transfer incomplete";
    }
    result.elapsed_seconds=static_cast<double>(samples)/input.rate;
    result.goodput_bps=result.elapsed_seconds>0?8.*static_cast<double>(result.source_bytes)/result.elapsed_seconds:0;
    if(!result.complete)result.error=result.status;
    return result;
}
}

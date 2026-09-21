#include "datapump/fast/compression.hpp"
#include <lzma.h>
#include <algorithm>
#include <array>
#include <limits>

namespace datapump::fast {
namespace {
constexpr std::uint64_t maximum_source=256ULL*1024*1024;
constexpr std::uint64_t decoder_memory=64ULL*1024*1024;
[[noreturn]] void fail(lzma_ret status) {
    if(status==LZMA_MEM_ERROR||status==LZMA_MEMLIMIT_ERROR)
        throw Error("Fast XZ compression memory limit exceeded");
    if(status==LZMA_BUF_ERROR)throw Error("Truncated Fast XZ source");
    throw Error("Invalid Fast XZ source or compression options");
}
struct Stream {
    lzma_stream value=LZMA_STREAM_INIT;
    ~Stream(){lzma_end(&value);}
};
struct Encoder {
    Stream stream;SourceReader reader;std::stop_token stop;
    std::array<std::uint8_t,16384> input{};
    std::uint64_t quota,read_bytes=0;
    bool eof=false,done=false;
    Encoder(SourceReader source,std::uint64_t limit,std::stop_token token):reader(std::move(source)),
        stop(token),quota(std::min(limit,maximum_source)) {
        if(!reader)throw Error("Missing Fast XZ source reader");
        lzma_options_lzma options{};
        if(lzma_lzma_preset(&options,6))throw Error("Fast XZ compression profile unavailable");
        options.dict_size=4U*1024*1024;
        lzma_filter filters[]={{LZMA_FILTER_LZMA2,&options},{LZMA_VLI_UNKNOWN,nullptr}};
        const auto status=lzma_stream_encoder(&stream.value,filters,LZMA_CHECK_CRC32);
        if(status!=LZMA_OK)fail(status);
    }
    std::size_t read(std::span<std::uint8_t> out) {
        if(done||out.empty())return 0;
        auto& state=stream.value;state.next_out=out.data();state.avail_out=out.size();
        while(state.avail_out&&!done) {
            if(stop.stop_requested())throw Error("Fast XZ compression cancelled");
            if(!state.avail_in&&!eof) {
                const auto count=reader(input);
                if(count>input.size())throw Error("Fast source reader exceeded its buffer");
                if(count>quota-read_bytes)throw Error("Fast source exceeds local storage quota");
                read_bytes+=count;state.next_in=input.data();state.avail_in=count;eof=count==0;
            }
            const auto old_in=state.avail_in,old_out=state.avail_out;
            const auto status=lzma_code(&state,eof?LZMA_FINISH:LZMA_RUN);
            if(status==LZMA_STREAM_END)done=true;
            else if(status!=LZMA_OK)fail(status);
            else if(old_in==state.avail_in&&old_out==state.avail_out)
                throw Error("Fast XZ encoder made no progress");
        }
        return out.size()-state.avail_out;
    }
};
}
SourceReader xz_source(SourceReader reader,std::uint64_t quota,std::stop_token stop) {
    auto encoder=std::make_shared<Encoder>(std::move(reader),quota,stop);
    return [encoder](std::span<std::uint8_t> out){return encoder->read(out);};
}
PreparedXzSource prepare_xz_source(SourceReader source,std::uint64_t quota,std::stop_token stop) {
    quota=std::min(quota,maximum_source);
    const auto bound=xz_size_bound(quota);
    PreparedXzSource result;
    auto reader=xz_source([&](std::span<std::uint8_t> out) {
        const auto n=source(out);
        if(n>out.size()||n>quota-result.source_bytes)throw Error("Fast source exceeds local storage quota");
        result.source_bytes+=n;return n;
    },quota,stop);
    std::array<std::uint8_t,16384> chunk{};
    while(const auto n=reader(chunk)) {
        if(n>bound-result.encoded.size())throw Error("Fast XZ source exceeds bounded encoding storage");
        const auto required=result.encoded.size()+n;
        if(required>result.encoded.capacity())
            result.encoded.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(bound,
                std::max<std::uint64_t>(required,result.encoded.capacity()*2))));
        result.encoded.insert(result.encoded.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(n));
    }
    return result;
}
Bytes decode_xz(std::span<const std::uint8_t> source,std::uint64_t quota) {
    const auto limit=std::min<std::uint64_t>(quota,maximum_source);
    Stream stream;auto& state=stream.value;
    const auto initialized=lzma_stream_decoder(&state,decoder_memory,LZMA_TELL_UNSUPPORTED_CHECK);
    if(initialized!=LZMA_OK)fail(initialized);
    state.next_in=source.data();state.avail_in=source.size();
    Bytes result;std::array<std::uint8_t,16384> chunk{};
    for(;;) {
        const auto remaining=limit-result.size();
        const auto capacity=static_cast<std::size_t>(std::min<std::uint64_t>(chunk.size(),remaining+1));
        state.next_out=chunk.data();state.avail_out=capacity;
        const auto old_in=state.avail_in;
        const auto status=lzma_code(&state,LZMA_FINISH);
        const auto produced=capacity-state.avail_out;
        if(produced>remaining)throw Error("Fast XZ output exceeds local source quota");
        if(result.size()+produced>result.capacity())
            result.reserve(static_cast<std::size_t>(std::min<std::uint64_t>(limit,
                std::max<std::uint64_t>(result.size()+produced,result.capacity()*2))));
        result.insert(result.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(produced));
        if(status==LZMA_STREAM_END) {
            if(state.avail_in)throw Error("Trailing bytes after the Fast XZ source");
            if(lzma_get_check(&state)!=LZMA_CHECK_CRC32)throw Error("Fast XZ source requires CRC32");
            return result;
        }
        if(status!=LZMA_OK)fail(status);
        if(old_in==state.avail_in&&!produced)throw Error("Truncated Fast XZ source");
    }
}
std::uint64_t xz_size_bound(std::uint64_t bytes) {
    if(bytes>maximum_source)throw Error("Fast source exceeds 256 MiB memory budget");
    // The liblzma single-call bound is not valid for our streaming encoder.
    // This pinned LZMA2 encoder emits <= input bytes + 6 bytes per chunk.
    // Without SYNC/FULL_FLUSH every nonfinal chunk consumes >= 1024 bytes:
    // its 65536-(OPTS+1)=61439 output threshold exceeds 53*1024+5
    // (RC_SYMBOLS_MAX per consumed byte, plus range flush); its other
    // threshold is almost 2 MiB of input. One final chunk may be shorter.
    // 128 bytes cover our one-block XZ header/check/index/footer, padding
    // and LZMA2 end marker. See docs/fast-xz.md for the pinned-source proof.
    return bytes+6*(bytes/1024+1)+128;
}
TransmitEstimate estimate_xz_transmission(const Profile& profile,bool encrypted,SourceReader source,
                                         std::uint64_t quota,std::stop_token stop) {
    std::uint64_t source_bytes=0,encoded_bytes=0;
    auto reader=xz_source([&](std::span<std::uint8_t> out) {
        const auto n=source(out);source_bytes+=n;return n;
    },quota,stop);
    std::array<std::uint8_t,16384> chunk{};
    while(const auto n=reader(chunk))encoded_bytes+=n;
    auto result=estimate_transmission(profile,encrypted,encoded_bytes);
    result.source_bps=8.*static_cast<double>(source_bytes)/result.seconds;
    return result;
}
}

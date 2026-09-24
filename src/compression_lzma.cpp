#include "datapump/compression.hpp"
#include <lzma.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace datapump::compression {
namespace {
constexpr std::size_t allocator_allowance=64U*1024U;

struct Profile {
    lzma_options_lzma options{};
    lzma_filter filters[2]{};
    Profile() {
        if(lzma_lzma_preset(&options,9U|LZMA_PRESET_EXTREME))
            throw Error("The bundled LZMA2 encoder does not support its fixed profile");
        // Fixed local profile; no received length determines dictionary size.
        options.dict_size=static_cast<std::uint32_t>(lzma2_dictionary_bytes);
        filters[0]={LZMA_FILTER_LZMA2,&options};
        filters[1]={LZMA_VLI_UNKNOWN,nullptr};
    }
    Profile(const Profile&)=delete;
    Profile& operator=(const Profile&)=delete;
};

std::size_t workspace(bool encoding) {
    Profile profile;
    const auto bytes=encoding?lzma_raw_encoder_memusage(profile.filters):
                              lzma_raw_decoder_memusage(profile.filters);
    if(bytes>std::numeric_limits<std::size_t>::max()-allocator_allowance)
        throw Error("Invalid LZMA2 workspace requirement");
    return static_cast<std::size_t>(bytes)+allocator_allowance;
}

// liblzma's estimates are checked before initialization, while this allocator
// enforces the supplied limit on actual simultaneously allocated scratch,
// including the bookkeeping below. No C callback can throw across the C API.
struct Budget {
    struct alignas(std::max_align_t) Allocation { std::size_t size; };
    std::size_t limit;
    std::size_t used=0;
    static void* allocate(void* opaque,std::size_t count,std::size_t size) noexcept {
        auto& budget=*static_cast<Budget*>(opaque);
        if(size&&count>std::numeric_limits<std::size_t>::max()/size)return nullptr;
        const auto bytes=count*size;
        if(bytes>std::numeric_limits<std::size_t>::max()-sizeof(Allocation))return nullptr;
        const auto total=bytes+sizeof(Allocation);
        if(total>budget.limit-budget.used)return nullptr;
        auto* allocation=static_cast<Allocation*>(std::malloc(total));
        if(!allocation)return nullptr;
        allocation->size=total;budget.used+=total;
        return allocation+1;
    }
    static void release(void* opaque,void* pointer) noexcept {
        if(!pointer)return;
        auto& budget=*static_cast<Budget*>(opaque);
        auto* allocation=static_cast<Allocation*>(pointer)-1;
        budget.used-=allocation->size;
        std::free(allocation);
    }
};

[[noreturn]] void fail(lzma_ret status) {
    if(status==LZMA_MEM_ERROR||status==LZMA_MEMLIMIT_ERROR)
        throw Error("LZMA2 compression workspace limit or available memory exhausted");
    if(status==LZMA_DATA_ERROR||status==LZMA_FORMAT_ERROR||status==LZMA_OPTIONS_ERROR)
        throw Error("Invalid raw LZMA2 stream or options");
    if(status==LZMA_BUF_ERROR)throw Error("Truncated raw LZMA2 stream");
    throw Error("LZMA2 codec failed");
}

struct Stream {
    Budget budget;
    lzma_allocator allocator;
    lzma_stream value=LZMA_STREAM_INIT;
    explicit Stream(std::size_t limit):budget{limit},
        allocator{Budget::allocate,Budget::release,&budget} { value.allocator=&allocator; }
    ~Stream() { lzma_end(&value); }
    Stream(const Stream&)=delete;
    Stream& operator=(const Stream&)=delete;
};

void check_workspace(std::size_t limit,bool encoding) {
    if(workspace(encoding)>limit)throw Error("LZMA2 compression workspace limit exceeded");
}

Lzma2Decoded process(std::span<const std::uint8_t> input,std::size_t output_limit,
                     std::size_t workspace_limit,bool encoding) {
    output_limit=std::min(output_limit,Bytes{}.max_size());
    const auto scratch=std::min(workspace_limit,encoding?long_encoder_limit:long_decoder_limit);
    check_workspace(scratch,encoding);
    Profile profile;Stream stream(scratch);
    const auto initialized=encoding?lzma_raw_encoder(&stream.value,profile.filters):
                                    lzma_raw_decoder(&stream.value,profile.filters);
    if(initialized!=LZMA_OK)fail(initialized);
    stream.value.next_in=input.data();stream.value.avail_in=input.size();
    Lzma2Decoded result;std::array<std::uint8_t,16384> chunk{};
    for(;;) {
        const auto remaining=output_limit-result.data.size();
        // One spare output byte detects expansion beyond an exactly full quota.
        const auto capacity=remaining<chunk.size()?remaining+1:chunk.size();
        stream.value.next_out=chunk.data();stream.value.avail_out=capacity;
        const auto old_in=stream.value.avail_in;
        const auto status=lzma_code(&stream.value,LZMA_FINISH);
        const auto produced=capacity-stream.value.avail_out;
        if(produced>remaining)throw Error("LZMA2 output limit exceeded");
        result.data.insert(result.data.end(),chunk.begin(),chunk.begin()+static_cast<std::ptrdiff_t>(produced));
        if(status==LZMA_STREAM_END) {
            if(encoding && stream.value.avail_in)throw Error("LZMA2 encoder did not consume its input");
            result.consumed_bytes=input.size()-stream.value.avail_in;return result;
        }
        if(status!=LZMA_OK && status!=LZMA_BUF_ERROR)fail(status);
        if(status==LZMA_BUF_ERROR || (old_in==stream.value.avail_in && !produced))
            throw Error(encoding?"LZMA2 encoder made no progress":"Truncated raw LZMA2 stream");
    }
}
}

std::size_t lzma2_encoder_workspace() { return workspace(true); }
std::size_t lzma2_decoder_workspace() { return workspace(false); }
Bytes encode_lzma2(std::span<const std::uint8_t> input,std::size_t output_limit,
                   std::size_t workspace_limit) try {
    return process(input,output_limit,workspace_limit,true).data;
} catch(const std::bad_alloc&) { throw Error("Not enough memory for LZMA2 encoding"); }
  catch(const std::length_error&) { throw Error("LZMA2 encoded output is too large"); }
Lzma2Decoded decode_lzma2(std::span<const std::uint8_t> encoded,std::size_t output_limit,
                        std::size_t workspace_limit) try {
    return process(encoded,output_limit,workspace_limit,false);
} catch(const std::bad_alloc&) { throw Error("Not enough memory for LZMA2 decoding"); }
  catch(const std::length_error&) { throw Error("LZMA2 decoded output is too large"); }
}

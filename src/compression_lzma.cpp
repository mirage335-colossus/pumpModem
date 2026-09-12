#include "datapump/compression.hpp"
#include <lzma.h>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

namespace datapump::compression {
namespace {
constexpr std::size_t maximum_history=64U*1024U*1024U;
constexpr std::size_t allocator_allowance=64U*1024U;
constexpr std::size_t maximum_preview=65536;

struct Profile {
    lzma_options_lzma options{};
    lzma_filter filters[2]{};
    explicit Profile(std::size_t original_size) {
        if(lzma_lzma_preset(&options,9U|LZMA_PRESET_EXTREME))
            throw Error("The bundled LZMA2 encoder does not support its fixed profile");
        // History contains only earlier bytes from this message, never a
        // built-in dictionary. Both endpoints derive its size identically.
        options.dict_size=static_cast<std::uint32_t>(
            std::clamp<std::size_t>(original_size,4096,maximum_history));
        filters[0]={LZMA_FILTER_LZMA2,&options};
        filters[1]={LZMA_VLI_UNKNOWN,nullptr};
    }
    Profile(const Profile&)=delete;
    Profile& operator=(const Profile&)=delete;
};

std::size_t workspace(std::size_t original_size,bool encoding) {
    Profile profile(original_size);
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

void check_workspace(std::size_t original_size,std::size_t limit,bool encoding) {
    if(workspace(original_size,encoding)>limit)
        throw Error("LZMA2 compression workspace limit exceeded");
}

Bytes decode(std::span<const std::uint8_t> encoded,std::size_t original_size,
             std::size_t output_limit,std::size_t workspace_limit,bool preview) {
    if(!preview&&original_size>output_limit)throw Error("LZMA2 decoded output limit exceeded");
    const auto capacity=preview?std::min({original_size,output_limit,maximum_preview}):original_size;
    if(capacity>Bytes().max_size())throw Error("LZMA2 decoded output is too large");
    const auto scratch=std::min(workspace_limit,long_decoder_limit);
    check_workspace(original_size,scratch,false);
    if(preview&&!capacity)return {};
    Profile profile(original_size);
    Stream stream(scratch);
    const auto initialized=lzma_raw_decoder(&stream.value,profile.filters);
    if(initialized!=LZMA_OK)fail(initialized);
    Bytes output(capacity);
    stream.value.next_in=encoded.data();stream.value.avail_in=encoded.size();
    std::uint8_t extra{};
    for(;;) {
        const auto produced=static_cast<std::size_t>(stream.value.total_out);
        const auto remaining=capacity-produced;
        stream.value.next_out=remaining?output.data()+produced:&extra;
        stream.value.avail_out=remaining?remaining:1;
        const auto old_in=stream.value.avail_in;
        const auto old_out=stream.value.total_out;
        const auto status=lzma_code(&stream.value,LZMA_FINISH);
        if(stream.value.total_out>original_size)throw Error("LZMA2 output exceeds its declared length");
        if(status==LZMA_STREAM_END) {
            if(stream.value.avail_in)throw Error("Trailing bytes after raw LZMA2 stream");
            if(stream.value.total_out!=original_size)throw Error("LZMA2 output length mismatch");
            output.resize(static_cast<std::size_t>(stream.value.total_out));
            return output;
        }
        if(status!=LZMA_OK&&status!=LZMA_BUF_ERROR)fail(status);
        // A preview is only the successfully decoded prefix. It is never
        // validation of bytes that follow the requested output boundary.
        if(preview&&stream.value.total_out==capacity&&capacity<original_size)return output;
        if(status==LZMA_BUF_ERROR||(old_in==stream.value.avail_in&&old_out==stream.value.total_out)) {
            if(!preview)throw Error("Truncated raw LZMA2 stream");
            output.resize(static_cast<std::size_t>(stream.value.total_out));
            return output;
        }
    }
}
}

std::size_t long_encoder_workspace(std::size_t original_size) { return workspace(original_size,true); }
std::size_t long_decoder_workspace(std::size_t original_size) { return workspace(original_size,false); }

std::optional<Bytes> encode_long(std::span<const std::uint8_t> input,std::size_t workspace_limit) try {
    if(input.size()<2)return std::nullopt;
    const auto scratch=std::min(workspace_limit,long_encoder_limit);
    check_workspace(input.size(),scratch,true);
    Profile profile(input.size());
    Stream stream(scratch);
    const auto initialized=lzma_raw_encoder(&stream.value,profile.filters);
    if(initialized!=LZMA_OK)fail(initialized);
    Bytes output(input.size()-1);
    stream.value.next_in=input.data();stream.value.avail_in=input.size();
    stream.value.next_out=output.data();stream.value.avail_out=output.size();
    const auto status=lzma_code(&stream.value,LZMA_FINISH);
    if(status==LZMA_STREAM_END) {
        if(stream.value.avail_in)throw Error("LZMA2 encoder did not consume the input");
        output.resize(static_cast<std::size_t>(stream.value.total_out));
        return output;
    }
    // Appended output cannot become shorter. Discard an expanding candidate
    // without allocating more than input.size()-1 output bytes.
    if((status==LZMA_OK||status==LZMA_BUF_ERROR)&&!stream.value.avail_out)return std::nullopt;
    fail(status);
} catch(const std::bad_alloc&) { throw Error("Not enough memory for LZMA2 encoding"); }
  catch(const std::length_error&) { throw Error("LZMA2 encoded output is too large"); }

Bytes decode_long(std::span<const std::uint8_t> encoded,std::size_t original_size,
                  std::size_t output_limit,std::size_t workspace_limit) try {
    return decode(encoded,original_size,output_limit,workspace_limit,false);
} catch(const std::bad_alloc&) { throw Error("Not enough memory for LZMA2 decoding"); }
  catch(const std::length_error&) { throw Error("LZMA2 decoded output is too large"); }

Bytes preview_long(std::span<const std::uint8_t> encoded,std::size_t original_size,
                   std::size_t maximum_output,std::size_t workspace_limit) try {
    return decode(encoded,original_size,maximum_output,workspace_limit,true);
} catch(const std::bad_alloc&) { throw Error("Not enough memory for LZMA2 preview"); }
  catch(const std::length_error&) { throw Error("LZMA2 preview output is too large"); }
}

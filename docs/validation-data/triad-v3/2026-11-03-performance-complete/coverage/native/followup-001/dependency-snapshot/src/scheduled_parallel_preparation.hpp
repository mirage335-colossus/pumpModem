#pragma once

#include "scheduled_acquisition_tile.hpp"
#include <algorithm>
#include <charconv>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace datapump::modem::detail::scheduled_parallel_preparation {

// Larger NUMERIC fragments retain the baseline transform extent. Physical
// producer/reset/processing fences remain their original <=512 samples.
inline scheduled_acquisition_tile::Shape shape(std::size_t span) {
    namespace tile=scheduled_acquisition_tile;
    const tile::Shape original{span,512};const auto transform=original.transform_size();
    std::size_t maximum=2048;
    if(const auto* mode=std::getenv("DATAPUMP_SCHED_LAZY_FRAGMENT")) {
        const std::string_view value(mode);
        if(value=="512")maximum=512;else if(value=="1024")maximum=1024;else if(value=="2048")maximum=2048;
        else {
            // Explicit bounded diagnostic pieces can fill a transform without
            // the artificial next-power-of-two fragment doubling. Automatic
            // selection remains separate until complete-cost crossover data.
            std::size_t fragment=0;
            const auto parsed=std::from_chars(value.data(),value.data()+value.size(),fragment);
            if(parsed.ec==std::errc{} && parsed.ptr==value.data()+value.size() &&
               fragment>2048 && fragment<=tile::maximum_fragment_samples && fragment%512==0)
                return {span,fragment};
            throw Error("implicit numeric fragment requires 512,1024,2048 or a512-sample multiple through524288");
        }
    }
    for(auto fragment=maximum;fragment>=512;fragment/=2) {
        const tile::Shape candidate{span,fragment};
        if(candidate.transform_size()==transform)return candidate;
    }
    return original;
}

// Diagnostic-only until complete execution establishes a benefit. Precision
// and algorithm/family width remain separate. Unsupported arithmetic keeps the
// original wide accumulator; FP64 force must never pass a true permission.
inline bool native_float_accumulation_requested() {
    const auto* mode=std::getenv("DATAPUMP_SCHED_NATIVE_ACCUMULATOR");
    if(!mode || std::string_view(mode)=="0")return false;
    if(std::string_view(mode)!="1")throw Error("native accumulator selector requires 0 or 1");
    return scheduled_float_spectral_accumulator::environment();
}

// One owner partitions its ACTUAL loan before dispatch. These reservations
// include the complete live screen chain and a first immutable native Plan;
// retained caches use only the unused portion of the same partition.
inline std::size_t scratch_bytes(std::size_t span,std::size_t lanes,std::size_t fragment,bool native_float_accumulation=false) {
    namespace tile=scheduled_acquisition_tile;
    const auto transform=tile::Shape{span,fragment}.transform_size();
    // Existing conservative complete-chain reservation, changing ONLY the
    // twelve accumulator planes whose execution/storage is selected below.
    auto bytes=tile::multiply_size((native_float_accumulation?36:48)*transform,sizeof(std::complex<double>));
    if(native_float_accumulation)bytes=tile::add_size(bytes,tile::multiply_size(12*transform,sizeof(std::complex<float>)));
    bytes=tile::add_size(bytes,tile::multiply_size(tile::add_size(span,fragment),96));
    bytes=tile::add_size(bytes,tile::multiply_size(lanes,sizeof(tile::Value)+sizeof(scheduled_acquisition_certificate::ReferenceErrors)));
    return tile::add_size(bytes,65536);
}
inline std::size_t plan_bytes(std::size_t span,std::size_t fragment) {
    namespace tile=scheduled_acquisition_tile;
    return tile::add_size(tile::multiply_size(tile::Shape{span,fragment}.transform_size(),sizeof(std::complex<double>)),16384);
}
inline std::size_t affordable_workers(std::size_t available,std::size_t context,
        std::size_t scratch,std::size_t plan,std::size_t groups,std::size_t requested) {
    namespace tile=scheduled_acquisition_tile;
    const auto each=tile::add_size(context,tile::add_size(scratch,plan));
    return each?std::min({groups,requested,available/each}):0;
}

} // namespace datapump::modem::detail::scheduled_parallel_preparation

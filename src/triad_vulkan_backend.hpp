#pragma once

#include "datapump/triad_acceleration.hpp"
#include "triad_bulk_peaks.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace datapump::triad::v3::receiver_detail {

// Small deterministic validation entry point using the same native device,
// bounded allocation and exact peak pipeline as production frequency search.
std::vector<CoarseBulkPeaks> vulkan_select_peak_rows(const VulkanReceiverOptions&,
    std::span<const double> scores, std::size_t arrivals, std::size_t separation);

inline constexpr std::string_view vulkan_rotate_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Observation { vec2 observation[]; };
layout(std430,set=0,binding=1) readonly buffer Frequencies { double frequency[]; };
#ifdef HALF_STORAGE
layout(std430,set=0,binding=2) writeonly buffer Output { uint output_values[]; };
#define STORE(value) packHalf2x16(value)
#else
layout(std430,set=0,binding=2) writeonly buffer Output { vec2 output_values[]; };
#define STORE(value) value
#endif
layout(push_constant) uniform Parameters { uint rows; uint nfft; uint frequencies; uint unused; double rate; } p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if (i >= p.nfft*p.frequencies) return;
    uint row=i%p.nfft, f=i/p.nfft;
    if (row>=p.rows) { output_values[i]=STORE(vec2(0)); return; }
    precise double cycles=-frequency[f]*(double(row)/p.rate);
    float angle=float((cycles-floor(cycles))*6.283185307179586476925286766559);
    vec2 rotation=vec2(cos(angle),sin(angle));
    vec2 value=observation[row];
    precise float real_value=value.x*rotation.x-value.y*rotation.y;
    precise float imag_value=value.y*rotation.x+value.x*rotation.y;
    output_values[i]=STORE(vec2(real_value,imag_value));
}
)glsl";

inline constexpr std::string_view vulkan_products_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
#ifdef HALF_STORAGE
layout(std430,set=0,binding=0) readonly buffer Bank { uint bank[]; };
layout(std430,set=0,binding=1) readonly buffer Observation { uint observation[]; };
layout(std430,set=0,binding=2) writeonly buffer Products { uint products[]; };
#define LOAD(value) unpackHalf2x16(value)
#define STORE(value) packHalf2x16(value)
#else
layout(std430,set=0,binding=0) readonly buffer Bank { vec2 bank[]; };
layout(std430,set=0,binding=1) readonly buffer Observation { vec2 observation[]; };
layout(std430,set=0,binding=2) writeonly buffer Products { vec2 products[]; };
#define LOAD(value) value
#define STORE(value) value
#endif
layout(push_constant) uniform Parameters { uint nfft; uint taps; uint frequencies; uint width; uint first_workspace; } p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.nfft*p.taps*p.frequencies*p.width) return;
    uint j=i%p.nfft, lane=i/p.nfft;
    uint tap=lane%p.taps, f=(lane/p.taps)%p.frequencies, w=lane/(p.taps*p.frequencies);
    vec2 a=LOAD(bank[((p.first_workspace+w)*p.taps+tap)*p.nfft+j]);
    vec2 b=LOAD(observation[f*p.nfft+j]);
    precise float real_value=a.x*b.x+a.y*b.y;
    precise float imag_value=a.x*b.y-a.y*b.x;
    products[i]=STORE(vec2(real_value,imag_value));
}
)glsl";

inline constexpr std::string_view vulkan_quadratic_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
#ifdef HALF_STORAGE
layout(std430,set=0,binding=0) readonly buffer Correlations { uint correlations[]; };
#define LOAD(value) unpackHalf2x16(value)
#else
layout(std430,set=0,binding=0) readonly buffer Correlations { vec2 correlations[]; };
#define LOAD(value) value
#endif
layout(std430,set=0,binding=1) readonly buffer Inverse { dvec4 coefficients[]; };
layout(std430,set=0,binding=2) writeonly buffer Scores { double scores[]; };
layout(push_constant) uniform Parameters {
    uint nfft; uint taps; uint frequencies; uint width; uint arrivals; uint first_workspace; double variance;
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.width*p.frequencies*p.arrivals) return;
    uint arrival=i%p.arrivals, pair=i/p.arrivals, w=pair/p.frequencies;
    uint base=pair*p.taps*p.nfft+arrival;
    dvec2 a=dvec2(LOAD(correlations[base]));
    dvec4 inverse=coefficients[p.first_workspace+w];
    precise double score=inverse.x*(a.x*a.x+a.y*a.y);
    if(p.taps==2u) {
        dvec2 b=dvec2(LOAD(correlations[base+p.nfft]));
        score+=inverse.y*(b.x*b.x+b.y*b.y);
        precise double real_product=a.x*inverse.z+a.y*inverse.w;
        precise double imag_product=a.x*inverse.w-a.y*inverse.z;
        score+=2.0*(real_product*b.x-imag_product*b.y);
    }
    scores[i]=score/p.variance;
}
)glsl";

// One workgroup covers one workspace/frequency pair. These are the exact
// linear-correlation samples at lag zero; no other arrival is implied.
inline constexpr std::string_view vulkan_direct_dot_shader = R"glsl(
#version 450
#extension GL_KHR_shader_subgroup_arithmetic : require
#extension GL_KHR_shader_subgroup_basic : require
#ifdef PACKED_DIRECT
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_EXT_shader_16bit_storage : require
#define Operand f16vec2
#define Storage f16vec2
#else
#define Operand vec2
#ifdef HALF_STORAGE
#define Storage uint
#else
#define Storage vec2
#endif
#endif
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Bank { Storage bank[]; };
layout(std430,set=0,binding=1) readonly buffer Observation { Storage observation[]; };
layout(std430,set=0,binding=2) writeonly buffer Correlations { vec2 correlation[]; };
layout(push_constant) uniform Parameters {
    uint rows; uint taps; uint frequencies; uint width; uint first_workspace;
} p;
shared vec2 partial[256];
void main() {
    uint pair=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    if(pair>=p.width*p.frequencies) return;
    uint f=pair%p.frequencies, w=p.first_workspace+pair/p.frequencies;
    uint lane=gl_LocalInvocationID.x;
    vec2 sum=vec2(0);
    for(uint row=lane;row<p.rows;row+=256u) {
#if defined(HALF_STORAGE) && !defined(PACKED_DIRECT)
        vec2 a=unpackHalf2x16(bank[w*p.rows+row]), b=unpackHalf2x16(observation[f*p.rows+row]);
#else
        Operand a=bank[w*p.rows+row], b=observation[f*p.rows+row];
#endif
        precise Operand term=fma(a.xx,b,a.yy*Operand(b.y,-b.x));
        sum+=vec2(term);
    }
    vec2 wave=subgroupAdd(sum);
    if(subgroupElect()) partial[gl_SubgroupID]=wave;
    barrier();
    if(lane==0u) {
        vec2 total=vec2(0);
        for(uint i=0u;i<gl_NumSubgroups;++i) total+=partial[i];
        correlation[pair]=total;
    }
}
)glsl";

} // namespace datapump::triad::v3::receiver_detail

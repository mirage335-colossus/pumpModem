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
layout(std430,set=0,binding=2) writeonly buffer Output { vec2 output_values[]; };
layout(push_constant) uniform Parameters { uint rows; uint nfft; uint frequencies; uint unused; double rate; } p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if (i >= p.nfft*p.frequencies) return;
    uint row=i%p.nfft, f=i/p.nfft;
    if (row>=p.rows) { output_values[i]=vec2(0); return; }
    precise double cycles=-frequency[f]*(double(row)/p.rate);
    float angle=float((cycles-floor(cycles))*6.283185307179586476925286766559);
    vec2 rotation=vec2(cos(angle),sin(angle));
    vec2 value=observation[row];
    precise float real_value=value.x*rotation.x-value.y*rotation.y;
    precise float imag_value=value.y*rotation.x+value.x*rotation.y;
    output_values[i]=vec2(real_value,imag_value);
}
)glsl";

inline constexpr std::string_view vulkan_products_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Bank { vec2 bank[]; };
layout(std430,set=0,binding=1) readonly buffer Observation { vec2 observation[]; };
layout(std430,set=0,binding=2) writeonly buffer Products { vec2 products[]; };
layout(push_constant) uniform Parameters { uint nfft; uint taps; uint frequencies; uint width; uint first_workspace; } p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.nfft*p.taps*p.frequencies*p.width) return;
    uint j=i%p.nfft, lane=i/p.nfft;
    uint tap=lane%p.taps, f=(lane/p.taps)%p.frequencies, w=lane/(p.taps*p.frequencies);
    vec2 a=bank[((p.first_workspace+w)*p.taps+tap)*p.nfft+j];
    vec2 b=observation[f*p.nfft+j];
    precise float real_value=a.x*b.x+a.y*b.y;
    precise float imag_value=a.x*b.y-a.y*b.x;
    products[i]=vec2(real_value,imag_value);
}
)glsl";

inline constexpr std::string_view vulkan_quadratic_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Correlations { vec2 correlations[]; };
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
    dvec2 a=dvec2(correlations[base]);
    dvec4 inverse=coefficients[p.first_workspace+w];
    precise double score=inverse.x*(a.x*a.x+a.y*a.y);
    if(p.taps==2u) {
        dvec2 b=dvec2(correlations[base+p.nfft]);
        score+=inverse.y*(b.x*b.x+b.y*b.y);
        precise double real_product=a.x*inverse.z+a.y*inverse.w;
        precise double imag_product=a.x*inverse.w-a.y*inverse.z;
        score+=2.0*(real_product*b.x-imag_product*b.y);
    }
    scores[i]=score/p.variance;
}
)glsl";

} // namespace datapump::triad::v3::receiver_detail

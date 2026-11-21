#pragma once

#include "datapump/triad_acceleration.hpp"
#include "triad_bulk_peaks.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace datapump::triad::v3::receiver_detail {

// Small deterministic validation entry point using the same native device,
// bounded allocation and exact peak pipeline as production frequency search.
std::vector<CoarseBulkPeaks> vulkan_select_peak_rows(const VulkanReceiverOptions&,
    std::span<const double> scores, std::size_t arrivals, std::size_t separation);

// Bounded device self-check of the core's fine-stage generalizations
// (test_triad_vulkan --core), on a backend device of these options. It
// checks the shader registry's family ranges; a fine-stage context with a
// descriptor pool beyond the default, whose six-binding pipelines run and
// whose work leaves every coarse counter unchanged; and FP64 twiddle-table
// VkFFT plans against a host DFT, with their quoted table charge released
// with the plans. It throws on any failed check. Without FP64/INT64 shaders
// fp64_plans is false and the FP64 plans are not checked.
struct VulkanCoreCheck {
    std::size_t built_in_kinds = 0, stage_kinds = 0;
    std::size_t pipelines = 0, bindings = 0, product_rows = 0;
    double product_error = 0;
    bool fp64_plans = false;
    std::size_t fft_rows = 0, fft_batches = 0, fp64_lut_bytes = 0, fp32_lut_bytes = 0;
    std::uint64_t fft_row_stages = 0;
    // Largest error relative to the reference's RMS: the forward transform
    // against the host DFT, and the normalized round trip against its input.
    double forward_error = 0, round_trip_error = 0;
};
VulkanCoreCheck vulkan_core_self_check(const VulkanReceiverOptions&, std::stop_token);

inline constexpr std::string_view vulkan_rotate_shader = R"glsl(
#version 450
#ifndef FIXED_PHASE
#extension GL_ARB_gpu_shader_fp64 : require
#endif
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Observation { vec2 observation[]; };
#ifdef FIXED_PHASE
layout(std430,set=0,binding=1) readonly buffer Frequencies { uvec2 phase_step[]; };
#else
layout(std430,set=0,binding=1) readonly buffer Frequencies { double frequency[]; };
#endif
#ifdef HALF_STORAGE
layout(std430,set=0,binding=2) writeonly buffer Output { uint output_values[]; };
#define STORE(value) packHalf2x16(value)
#else
layout(std430,set=0,binding=2) writeonly buffer Output { vec2 output_values[]; };
#define STORE(value) value
#endif
layout(push_constant) uniform Parameters {
    uint rows; uint nfft; uint frequencies; uint unused;
#ifndef FIXED_PHASE
    double rate;
#endif
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if (i >= p.nfft*p.frequencies) return;
    uint row=i%p.nfft, f=i/p.nfft;
    if (row>=p.rows) { output_values[i]=STORE(vec2(0)); return; }
#ifdef FIXED_PHASE
    // Multiply the Q64 step by the sample index modulo one turn. Only the
    // high word is needed for FP32 trig. No 64-bit shader feature is used.
    uint upper, lower;
    umulExtended(phase_step[f].x,row,upper,lower);
    uint phase=upper+phase_step[f].y*row;
    float angle=float(int(phase))*1.4629180792671597e-9;
#else
    precise double cycles=-frequency[f]*(double(row)/p.rate);
    float angle=float((cycles-floor(cycles))*6.283185307179586476925286766559);
#endif
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

// Changing-channel segmented timing, the Vulkan form of the CUDA scorer in
// triad_tensor_peaks.cu. A block's FP32 time column is copied into the
// zero-padded forward FFT input, exactly as the native float cast pads it.
inline constexpr std::string_view vulkan_segmented_column_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Source { vec2 source[]; };
layout(std430,set=0,binding=1) writeonly buffer Column { vec2 column[]; };
layout(push_constant) uniform Parameters { uint first; uint rows; uint nfft; uint unused; } p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.nfft) return;
    column[i]=i<p.rows?source[p.first+i]:vec2(0);
}
)glsl";

// Products for a tile of residual frequencies. Each frequency is a whole-bin
// shift of one lattice phase's observation spectrum, times the conjugated
// template spectrum, with the two separately rounded products of the native
// complex64 path. Spectra are [block][phase][row]; nfft is a power of two.
inline constexpr std::string_view vulkan_segmented_product_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Observation { vec2 observation[]; };
layout(std430,set=0,binding=1) readonly buffer Column { vec2 column[]; };
layout(std430,set=0,binding=2) writeonly buffer Products { vec2 products[]; };
layout(push_constant) uniform Parameters {
    uint nfft; uint phases; uint radius; uint bin_stride; uint first_frequency; uint width; uint block_offset;
    uint unused;
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.nfft*p.width) return;
    uint row=i/p.nfft, j=i&(p.nfft-1u);
    // Floor division of the signed bin offset, using nonnegative operands.
    int frequency=(int(p.first_frequency+row)-int(p.radius))*int(p.bin_stride);
    int phases=int(p.phases);
    int shift=frequency>=0?frequency/phases:-((phases-1-frequency)/phases);
    uint phase=uint(frequency-shift*phases);
    vec2 a=observation[p.block_offset+phase*p.nfft+((uint(shift)+j)&(p.nfft-1u))];
    vec2 b=column[j];
    precise float real_value=a.x*b.x+a.y*b.y;
    precise float imag_value=a.y*b.x-a.x*b.y;
    products[i]=vec2(real_value,imag_value);
}
)glsl";

// Ranks a tile of normalized inverse transforms. Each workgroup row owns a
// residue class of the tile's frequencies and keeps their maximum per lag in
// its own partial slot, so no atomics are needed and every slot has one
// writer per dispatch. The tracking DP reads the previous block's states and
// writes the next ones, in the CUDA kernel's FP64 rank and FP32 state
// arithmetic. A nonfinite rank or state stores NaN, which the finish kernel
// propagates into the published rank for host validation.
inline constexpr std::string_view vulkan_segmented_ranks_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=128) in;
layout(std430,set=0,binding=0) readonly buffer Correlations { vec2 correlations[]; };
layout(std430,set=0,binding=1) buffer Partial { double partial[]; };
layout(std430,set=0,binding=2) buffer States { float states[]; };
layout(push_constant) uniform Parameters {
    uint nfft; uint arrivals; uint frequencies; uint first_frequency; uint width; uint hop;
    uint previous; uint next; double energy; double noise_power;
} p;
// log(2) rounded to FP32, as the native and CUDA trackers use it.
const float hop_penalty=0.693147182464599609375;
void main() {
    uint lag=gl_GlobalInvocationID.x;
    if(lag>=p.arrivals) return;
    uint group=gl_WorkGroupID.y, groups=gl_NumWorkGroups.y;
    double maximum=0.0;
    bool invalid=false;
    for(uint row=group;row<p.width;row+=groups) {
        vec2 c=correlations[row*p.nfft+lag];
        double re=double(c.x), im=double(c.y);
        precise double value=(re*re+im*im)/p.energy;
        invalid=invalid||isnan(value)||isinf(value);
        if(value>maximum) maximum=value;
        if(p.hop==0u) continue;
        uint index=p.first_frequency+row;
        uint at=index*p.arrivals+lag;
        float best=states[p.previous+at];
        for(uint offset=1u;offset<=p.hop;++offset) {
            if(index>=offset) {
                precise float left=states[p.previous+at-offset*p.arrivals]-hop_penalty;
                if(left>best) best=left;
            }
            if(offset<p.frequencies-index) {
                precise float right=states[p.previous+at+offset*p.arrivals]-hop_penalty;
                if(right>best) best=right;
            }
        }
        precise float state=float(value/p.noise_power)+best;
        invalid=invalid||isnan(state)||isinf(state);
        states[p.next+at]=state;
    }
    uint slot=group*p.arrivals+lag;
    double prior=partial[slot];
    if(invalid||isnan(prior)) partial[slot]=packDouble2x32(uvec2(0u,0x7ff80000u));
    else if(maximum>prior) partial[slot]=maximum;
}
)glsl";

// Mode 1 adds each lag's block maximum over all partial slots to its fixed
// rank, in block order, and clears the slots. Mode 2 writes the linked rank
// from the final tracking states. Ranks are [fixed lag][linked lag].
inline constexpr std::string_view vulkan_segmented_finish_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) buffer Partial { double partial[]; };
layout(std430,set=0,binding=1) buffer Ranks { double ranks[]; };
layout(std430,set=0,binding=2) readonly buffer States { float states[]; };
layout(push_constant) uniform Parameters { uint arrivals; uint groups; uint frequencies; uint mode; uint state; } p;
void main() {
    uint lag=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(lag>=p.arrivals) return;
    if((p.mode&1u)!=0u) {
        double best=0.0;
        bool invalid=false;
        for(uint group=0u;group<p.groups;++group) {
            double value=partial[group*p.arrivals+lag];
            invalid=invalid||isnan(value);
            if(value>best) best=value;
            partial[group*p.arrivals+lag]=0.0;
        }
        precise double total=ranks[lag]+best;
        ranks[lag]=invalid?packDouble2x32(uvec2(0u,0x7ff80000u)):total;
    }
    if((p.mode&2u)!=0u) {
        double maximum=0.0;
        for(uint index=0u;index<p.frequencies;++index) {
            double state=double(states[p.state+index*p.arrivals+lag]);
            if(state>maximum) maximum=state;
        }
        ranks[p.arrivals+lag]=maximum;
    }
}
)glsl";

} // namespace datapump::triad::v3::receiver_detail

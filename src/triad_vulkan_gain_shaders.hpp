#pragma once

#include <cstdint>
#include <string_view>

namespace datapump::triad::v3::receiver_detail::vulkan {

// GLSL compute sources of the signed-lag gain direct rank kernels, by index
// from this family's first ShaderKind (triad_vulkan_core.hpp): a source for
// every index below gain_shader_count and none from it on, which the core's
// device self-check verifies. The unfused form: block windows and their
// forward transforms, frequency products and their inverse transforms, the
// FP32 per-lag log-mean-exp, and the FP64 block-order sum
// (docs/triad-gpu-optimization.md, "Signed-lag ranks").
enum class GainShader : std::uint16_t { columns, product, rank, finish };
inline constexpr std::uint16_t gain_shader_count = 4;

// A block of a batch (GainBlock, std430 layout in the columns and rank
// kernels): its earliest observation row, first template row, rows, outputs
// and shift run, then each bit's FP32 correlation gain g = cs / scale, with
// the packing's power-of-two scales folded in, and its log-determinant.

// Columns kernel. Rows [block][2 + chunks][tile] of zero-padded FP32
// inputs: both bits' template columns, then the native overlap-save
// observation windows, chunk c holding the outputs [c C, c C + C) of the
// block's shift range with its rows - 1 trailing rows. Padded blocks of a
// short batch are zero.
inline constexpr std::string_view vulkan_gain_columns_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
struct GainBlock {
    uint earliest; uint first; uint rows; uint outputs; uint run; uint unused0; uint unused1; uint unused2;
    vec2 gain; vec2 logdet;
};
layout(std430,set=0,binding=0) readonly buffer Observation { vec2 observation[]; };
layout(std430,set=0,binding=1) readonly buffer Columns { vec2 columns[]; };
layout(std430,set=0,binding=2) readonly buffer Blocks { GainBlock blocks[]; };
layout(std430,set=0,binding=3) writeonly buffer Window { vec2 window[]; };
layout(push_constant) uniform Parameters {
    uint tile; uint chunks; uint capacity; uint kernel_rows; uint first_block; uint count; uint batch; uint unused;
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    uint rows_per_block=2u+p.chunks;
    if(i>=p.batch*rows_per_block*p.tile) return;
    uint k=i%p.tile, row=i/p.tile, r=row%rows_per_block, b=row/rows_per_block;
    vec2 value=vec2(0);
    if(b<p.count) {
        GainBlock block=blocks[p.first_block+b];
        if(r<2u) {
            if(k<block.rows) value=columns[r*p.kernel_rows+block.first+k];
        } else {
            uint start=(r-2u)*p.capacity;
            if(start<block.outputs) {
                uint outputs=min(p.capacity,block.outputs-start);
                if(k<outputs+block.rows-1u) value=observation[block.earliest+start+k];
            }
        }
    }
    window[i]=value;
}
)glsl";

// Product kernel. Products [block][chunk][bit][frequency][tile]: the
// observation spectrum shifted by the frequency's whole bins times the
// conjugated template spectrum, with the two separately rounded products of
// the native complex path (segmented_product with one phase).
inline constexpr std::string_view vulkan_gain_product_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Window { vec2 window[]; };
layout(std430,set=0,binding=1) writeonly buffer Products { vec2 products[]; };
layout(push_constant) uniform Parameters {
    uint tile; uint chunks; uint frequencies; uint radius; uint unit_bin; uint batch;
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.batch*p.chunks*2u*p.frequencies*p.tile) return;
    uint k=i%p.tile, row=i/p.tile, f=row%p.frequencies, q=row/p.frequencies;
    uint bit=q%2u, c=(q/2u)%p.chunks, b=q/2u/p.chunks;
    int bin=(int(f)-int(p.radius))*int(p.unit_bin);
    uint source=uint(int(k)+bin)&(p.tile-1u);
    uint base=b*(2u+p.chunks)*p.tile;
    vec2 y=window[base+(2u+c)*p.tile+source];
    vec2 t=window[base+bit*p.tile+k];
    precise float real_value=y.x*t.x+y.y*t.y;
    precise float imag_value=y.y*t.x-y.x*t.y;
    products[i]=vec2(real_value,imag_value);
}
)glsl";

// Rank kernel. One thread per (block, bit, lag): the native chunk and
// offset of the lag's relative shift, the FP32 emission
// e = (g re)^2 + (g im)^2 - logdet of every frequency's normalized
// correlation, and the two-pass FP32
// log-mean-exp max + log(sum exp(e - max) / frequencies). A nonfinite
// emission or result stores NaN, which the finish kernel and the host
// certification propagate. Unsampled blocks write the batch rows; audit
// blocks write their own rows after them.
inline constexpr std::string_view vulkan_gain_rank_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
struct GainBlock {
    uint earliest; uint first; uint rows; uint outputs; uint run; uint unused0; uint unused1; uint unused2;
    vec2 gain; vec2 logdet;
};
layout(std430,set=0,binding=0) readonly buffer Products { vec2 products[]; };
layout(std430,set=0,binding=1) readonly buffer Blocks { GainBlock blocks[]; };
layout(std430,set=0,binding=2) readonly buffer Shifts { int shifts[]; };
layout(std430,set=0,binding=3) writeonly buffer Partial { float partial[]; };
layout(push_constant) uniform Parameters {
    uint tile; uint chunks; uint capacity; uint frequencies; uint lags; uint first_block; uint count; uint row;
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.count*2u*p.lags) return;
    uint lag=i%p.lags, bit=(i/p.lags)%2u, b=i/p.lags/2u;
    GainBlock block=blocks[p.first_block+b];
    uint at=uint(shifts[block.run*p.lags+lag]);
    uint c=at/p.capacity, j=at-c*p.capacity;
    uint base=(((b*p.chunks+c)*2u+bit)*p.frequencies)*p.tile+j;
    float g=block.gain[bit], logdet=block.logdet[bit];
    float maximum=uintBitsToFloat(0xff800000u);
    bool invalid=false;
    for(uint f=0u;f<p.frequencies;++f) {
        vec2 r=products[base+f*p.tile];
        precise float a=g*r.x, s=g*r.y;
        precise float e=a*a+s*s-logdet;
        invalid=invalid||isnan(e)||isinf(e);
        maximum=max(maximum,e);
    }
    float sum=0.0;
    for(uint f=0u;f<p.frequencies;++f) {
        vec2 r=products[base+f*p.tile];
        precise float a=g*r.x, s=g*r.y;
        precise float e=a*a+s*s-logdet;
        sum+=exp(e-maximum);
    }
    precise float value=maximum+log(sum/float(p.frequencies));
    invalid=invalid||isnan(value)||isinf(value);
    partial[(p.row+b)*2u*p.lags+bit*p.lags+lag]=invalid?uintBitsToFloat(0x7fc00000u):value;
}
)glsl";

// Finish kernel. One thread per (bit, lag): adds the batch's unsampled
// block values to the FP64 totals one block at a time, in block order, so
// the sum does not depend on the batch width.
inline constexpr std::string_view vulkan_gain_finish_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Partial { float partial[]; };
layout(std430,set=0,binding=1) buffer Totals { double totals[]; };
layout(push_constant) uniform Parameters { uint cells; uint count; } p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.cells) return;
    double total=totals[i];
    for(uint b=0u;b<p.count;++b) {
        precise double next=total+double(partial[b*p.cells+i]);
        total=next;
    }
    totals[i]=total;
}
)glsl";

constexpr std::string_view gain_shader_source(std::uint16_t index) noexcept {
    switch (static_cast<GainShader>(index)) {
    case GainShader::columns: return vulkan_gain_columns_shader;
    case GainShader::product: return vulkan_gain_product_shader;
    case GainShader::rank: return vulkan_gain_rank_shader;
    case GainShader::finish: return vulkan_gain_finish_shader;
    }
    return {};
}

} // namespace datapump::triad::v3::receiver_detail::vulkan

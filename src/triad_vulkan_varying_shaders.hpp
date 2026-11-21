#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace datapump::triad::v3::receiver_detail::vulkan {

// GLSL compute sources of the varying statistics kernels, by index from this
// family's first ShaderKind (triad_vulkan_core.hpp): a source for every
// index below varying_shader_count and none from it on, which the core's
// device self-check verifies.
//
// Every kernel is FP64 and mirrors one native step of the varying scorer
// (src/triad_receiver_fine.cpp) on the same host-computed tables: the
// projection's chirp, kernel spectrum, retained indices and unitary scale
// (DftPlan and Projection), and the half-bin rotation the projected tracker
// forms by repeated multiplication. Complex products and sums are written
// out as the native std::complex<double> forms and marked precise, so no
// fused multiply-add changes their rounding; only the VkFFT transforms and
// the order of the mode sums differ from the native arithmetic.
//
// Data layout of one segment of blocks of one length: transform columns
// [block][column][nfft], where a block's columns are its affine template
// columns, then for each tracked atom its four columns unrotated and then
// rotated by half a bin. Staging holds the host's templates and projected
// observations (VaryingStagingLayout); tables holds doubles; results holds
// the affine items' 20 sums and the atoms' 8-value cells.
enum class VaryingShader : std::uint16_t { pack, kernel, affine, independent };
inline constexpr std::uint16_t varying_shader_count = 4;

// Templates (rotated where the column asks) times the chirp, zero-padded to
// the transform length: DftPlan::transform's input. A power-of-two length
// has no chirp and no padding.
inline constexpr std::string_view vulkan_varying_pack_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Staging { dvec2 staging[]; };
layout(std430,set=0,binding=1) readonly buffer Tables { double tables[]; };
layout(std430,set=0,binding=2) writeonly buffer Data { dvec2 data[]; };
layout(push_constant) uniform Parameters {
    uint blocks; uint fft_columns; uint affine_columns; uint rows; uint nfft; uint staging_first;
    uint block_values; uint chirp; uint phase; uint bluestein;
} p;
dvec2 table(uint offset, uint i) { return dvec2(tables[offset+2u*i], tables[offset+2u*i+1u]); }
dvec2 cmul(dvec2 a, dvec2 b) {
    precise double re=a.x*b.x-a.y*b.y;
    precise double im=a.x*b.y+a.y*b.x;
    return dvec2(re,im);
}
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.blocks*p.fft_columns*p.nfft) return;
    uint row=i%p.nfft, column=(i/p.nfft)%p.fft_columns, block=i/(p.nfft*p.fft_columns);
    if(row>=p.rows) { data[i]=dvec2(0.0); return; }
    uint source=column;
    bool rotate=false;
    if(column>=p.affine_columns) {
        uint c=column-p.affine_columns;
        source=p.affine_columns+(c/8u)*4u+(c&3u);
        rotate=((c/4u)&1u)!=0u;
    }
    dvec2 value=staging[p.staging_first+block*p.block_values+source*p.rows+row];
    if(rotate) value=cmul(value,table(p.phase,row));
    if(p.bluestein!=0u) value=cmul(value,table(p.chirp,row));
    data[i]=value;
}
)glsl";

// The Bluestein product with the host FFT of the convolution kernel.
inline constexpr std::string_view vulkan_varying_kernel_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Staging { dvec2 staging[]; };
layout(std430,set=0,binding=1) readonly buffer Tables { double tables[]; };
layout(std430,set=0,binding=2) buffer Data { dvec2 data[]; };
layout(push_constant) uniform Parameters { uint values; uint nfft; uint kernel; uint unused; } p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.values) return;
    uint k=i%p.nfft;
    dvec2 a=data[i];
    dvec2 b=dvec2(tables[p.kernel+2u*k],tables[p.kernel+2u*k+1u]);
    precise double re=a.x*b.x-a.y*b.y;
    precise double im=a.x*b.y+a.y*b.x;
    data[i]=dvec2(re,im);
}
)glsl";

// One workgroup per (block, affine item): the item's projected modes, as
// Projection::apply gathers them (transform times chirp, times the unitary
// scale), and their correlations with the projected observation and Gram,
// as projected_statistics sums them before the variance division. Results
// per item: correlations [bit][tap], then Gram [bit][(0,0),(0,1),(1,1)].
inline constexpr std::string_view vulkan_varying_affine_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=64) in;
layout(std430,set=0,binding=0) readonly buffer Staging { dvec2 staging[]; };
layout(std430,set=0,binding=1) readonly buffer Tables { double tables[]; };
layout(std430,set=0,binding=2) readonly buffer Data { dvec2 data[]; };
layout(std430,set=0,binding=3) writeonly buffer Results { double results[]; };
layout(push_constant) uniform Parameters {
    uint blocks; uint items; uint fft_columns; uint nfft; uint modes; uint projected; uint mode_stride;
    uint first_block; uint indices; uint chirp; uint bluestein; uint item_table; uint scale; uint output_offset;
} p;
shared double partial[64*20];
dvec2 cmul(dvec2 a, dvec2 b) {
    precise double re=a.x*b.x-a.y*b.y;
    precise double im=a.x*b.y+a.y*b.x;
    return dvec2(re,im);
}
dvec2 conj_mul(dvec2 a, dvec2 b) {
    precise double re=a.x*b.x+a.y*b.y;
    precise double im=a.x*b.y-a.y*b.x;
    return dvec2(re,im);
}
void main() {
    uint group=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    uint lane=gl_LocalInvocationID.x;
    if(group>=p.blocks*p.items) return;
    uint block=group/p.items, item=group%p.items;
    uint column=uint(tables[p.item_table+2u*item]);
    uint taps=uint(tables[p.item_table+2u*item+1u]);
    double scale=tables[p.scale];
    double sums[20];
    for(uint k=0u;k<20u;++k) sums[k]=0.0;
    uint first=(block*p.fft_columns+column)*p.nfft;
    for(uint j=lane;j<p.modes;j+=64u) {
        uint index=uint(tables[p.indices+j]);
        dvec2 y=staging[p.projected+(p.first_block+block)*p.mode_stride+j];
        dvec2 chirp=dvec2(1.0,0.0);
        if(p.bluestein!=0u) chirp=dvec2(tables[p.chirp+2u*index],tables[p.chirp+2u*index+1u]);
        for(uint bit=0u;bit<2u;++bit) {
            dvec2 a[2];
            for(uint tap=0u;tap<taps;++tap) {
                dvec2 v=data[first+(bit*taps+tap)*p.nfft+index];
                if(p.bluestein!=0u) v=cmul(v,chirp);
                precise double re=v.x*scale;
                precise double im=v.y*scale;
                a[tap]=dvec2(re,im);
            }
            for(uint tap=0u;tap<taps;++tap) {
                dvec2 c=conj_mul(a[tap],y);
                uint at=2u*(bit*2u+tap);
                precise double re=sums[at]+c.x;
                precise double im=sums[at+1u]+c.y;
                sums[at]=re; sums[at+1u]=im;
            }
            precise double d0=sums[8u+6u*bit]+(a[0].x*a[0].x+a[0].y*a[0].y);
            sums[8u+6u*bit]=d0;
            if(taps==2u) {
                dvec2 g=conj_mul(a[0],a[1]);
                precise double gr=sums[8u+6u*bit+2u]+g.x;
                precise double gi=sums[8u+6u*bit+3u]+g.y;
                precise double d1=sums[8u+6u*bit+4u]+(a[1].x*a[1].x+a[1].y*a[1].y);
                sums[8u+6u*bit+2u]=gr; sums[8u+6u*bit+3u]=gi; sums[8u+6u*bit+4u]=d1;
            }
        }
    }
    for(uint k=0u;k<20u;++k) partial[lane*20u+k]=sums[k];
    barrier();
    for(uint width=32u;width>0u;width/=2u) {
        if(lane<width)
            for(uint k=0u;k<20u;++k) {
                precise double v=partial[lane*20u+k]+partial[(lane+width)*20u+k];
                partial[lane*20u+k]=v;
            }
        barrier();
    }
    if(lane==0u)
        for(uint k=0u;k<20u;++k) results[p.output_offset+((p.first_block+block)*p.items+item)*20u+k]=partial[k];
}
)glsl";

// One workgroup per (block, tracked atom, frequency state): both bits'
// direct and echo correlations, diagonals and cross term over the retained
// modes of the full transform of the state's half-bin parity, shifted by its
// whole bins, as visit_projected_tracks sums them. Cells are [block][atom][state][bit] of
// eight values: correlations, diagonals, cross.
inline constexpr std::string_view vulkan_varying_independent_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=64) in;
layout(std430,set=0,binding=0) readonly buffer Staging { dvec2 staging[]; };
layout(std430,set=0,binding=1) readonly buffer Tables { double tables[]; };
layout(std430,set=0,binding=2) readonly buffer Data { dvec2 data[]; };
layout(std430,set=0,binding=3) writeonly buffer Results { double results[]; };
layout(push_constant) uniform Parameters {
    uint blocks; uint atoms; uint states; uint fft_columns; uint affine_columns; uint nfft; uint rows; uint modes;
    uint projected; uint mode_stride; uint first_block; uint indices; uint chirp; uint bluestein; uint bins;
    uint scale; uint output_offset;
} p;
shared double partial[64*16];
dvec2 cmul(dvec2 a, dvec2 b) {
    precise double re=a.x*b.x-a.y*b.y;
    precise double im=a.x*b.y+a.y*b.x;
    return dvec2(re,im);
}
dvec2 conj_mul(dvec2 a, dvec2 b) {
    precise double re=a.x*b.x+a.y*b.y;
    precise double im=a.x*b.y-a.y*b.x;
    return dvec2(re,im);
}
dvec2 spectrum(uint first, uint bin, double scale) {
    dvec2 v=data[first+bin];
    if(p.bluestein!=0u) v=cmul(v,dvec2(tables[p.chirp+2u*bin],tables[p.chirp+2u*bin+1u]));
    precise double re=v.x*scale;
    precise double im=v.y*scale;
    return dvec2(re,im);
}
void main() {
    uint group=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    uint lane=gl_LocalInvocationID.x;
    if(group>=p.blocks*p.atoms*p.states) return;
    uint state=group%p.states, atom=(group/p.states)%p.atoms, block=group/(p.states*p.atoms);
    // The host splits each state's signed half-bin into its parity and
    // whole-bin shift, as the native visitor does.
    uint half_bin=uint(tables[p.bins+2u*state]);
    int shift=int(tables[p.bins+2u*state+1u]);
    double scale=tables[p.scale];
    uint base=(block*p.fft_columns+p.affine_columns+atom*8u+half_bin*4u)*p.nfft;
    double sums[16];
    for(uint k=0u;k<16u;++k) sums[k]=0.0;
    int rows=int(p.rows);
    for(uint j=lane;j<p.modes;j+=64u) {
        int shifted=int(tables[p.indices+j])-shift;
        if(shifted<0) shifted+=rows;
        if(shifted>=rows) shifted-=rows;
        uint at=uint(shifted);
        dvec2 y=staging[p.projected+(p.first_block+block)*p.mode_stride+j];
        for(uint bit=0u;bit<2u;++bit) {
            dvec2 first=spectrum(base+(2u*bit)*p.nfft,at,scale);
            dvec2 echo=spectrum(base+(2u*bit+1u)*p.nfft,at,scale);
            dvec2 c0=conj_mul(first,y), c1=conj_mul(echo,y), x=conj_mul(first,echo);
            uint o=8u*bit;
            precise double s0=sums[o]+c0.x, s1=sums[o+1u]+c0.y, s2=sums[o+2u]+c1.x, s3=sums[o+3u]+c1.y;
            precise double s4=sums[o+4u]+(first.x*first.x+first.y*first.y);
            precise double s5=sums[o+5u]+(echo.x*echo.x+echo.y*echo.y);
            precise double s6=sums[o+6u]+x.x, s7=sums[o+7u]+x.y;
            sums[o]=s0; sums[o+1u]=s1; sums[o+2u]=s2; sums[o+3u]=s3;
            sums[o+4u]=s4; sums[o+5u]=s5; sums[o+6u]=s6; sums[o+7u]=s7;
        }
    }
    for(uint k=0u;k<16u;++k) partial[lane*16u+k]=sums[k];
    barrier();
    for(uint width=32u;width>0u;width/=2u) {
        if(lane<width)
            for(uint k=0u;k<16u;++k) {
                precise double v=partial[lane*16u+k]+partial[(lane+width)*16u+k];
                partial[lane*16u+k]=v;
            }
        barrier();
    }
    if(lane==0u)
        for(uint k=0u;k<16u;++k)
            results[p.output_offset+(((p.first_block+block)*p.atoms+atom)*p.states+state)*16u+k]=partial[k];
}
)glsl";

inline constexpr std::array<std::string_view, varying_shader_count> varying_shader_sources{
    vulkan_varying_pack_shader, vulkan_varying_kernel_shader, vulkan_varying_affine_shader,
    vulkan_varying_independent_shader};
constexpr std::string_view varying_shader_source(std::uint16_t index) noexcept {
    return index < varying_shader_count ? varying_shader_sources[index] : std::string_view{};
}

} // namespace datapump::triad::v3::receiver_detail::vulkan

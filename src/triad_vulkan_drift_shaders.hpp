#pragma once

#include <cstdint>
#include <string_view>

namespace datapump::triad::v3::receiver_detail::vulkan {

// GLSL compute sources of the drift proposal kernels, by index from this
// family's first ShaderKind (triad_vulkan_core.hpp): a source for every
// index below drift_shader_count and none from it on, which the core's
// device self-check verifies. The push constant structures beside each
// source give its exact layout (src/triad_vulkan_drift.cpp).
enum DriftShader : std::uint16_t {
    drift_segment_column, drift_power, drift_ridge, drift_dechirp, drift_peaks, drift_peaks_merge
};
inline constexpr std::uint16_t drift_shader_count = 6;

// Peak workgroups and the bins each thread of one scans.
inline constexpr std::uint32_t drift_peak_threads = 128, drift_peak_bins_per_thread = 64;
// One partial of a peak workgroup: [bit][8]{rank, signed bin} then the two
// nominations, all FP64.
inline constexpr std::uint32_t drift_partial_doubles = 2 * 8 * 2 + 2;
// A merged slope: [bit][8]{rank, signed bin}; nominations follow all slopes.
inline constexpr std::uint32_t drift_frontier_doubles = 2 * 8 * 2;

// Zero-padded ridge segment columns [segment][column][fft_rows] of a chunk,
// copied from a lane's staged FP32 products [column][rows] exactly as the
// native float cast pads them. bounds holds each segment's first row.
struct DriftColumnPush { std::uint32_t rows, first_segment, chunk_segments, fft_rows, columns, unused[3]; };
inline constexpr std::string_view drift_segment_column_shader = R"glsl(
#version 450
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Staging { vec2 staging[]; };
layout(std430,set=0,binding=1) readonly buffer Bounds { uint bounds[]; };
layout(std430,set=0,binding=2) writeonly buffer Columns { vec2 columns[]; };
layout(push_constant) uniform Parameters {
    uint rows; uint first_segment; uint chunk_segments; uint fft_rows; uint columns; uint unused0; uint unused1; uint unused2;
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.chunk_segments*p.columns*p.fft_rows) return;
    uint row=i&(p.fft_rows-1u), transform=i/p.fft_rows;
    uint column=transform%p.columns, segment=p.first_segment+transform/p.columns;
    uint first=bounds[segment], count=bounds[segment+1u]-first;
    columns[i]=row<count?staging[column*p.rows+first+row]:vec2(0);
}
)glsl";

// The FP64 power table [bit][segment][bin] of a chunk's spectra in the
// native order and arithmetic: (0 + |X0|^2/E0) + |X1|^2/E1, each norm the
// FP64 x*x + y*y of the FP32 bin, each quotient correctly rounded, and a
// tap with energy <= 0 skipped. The device division is corrected by its
// exact FMA residual so the quotient does not depend on the driver's FP64
// division. A NaN or infinite power sets the call's flag, which the native
// definition of finite products cannot give.
struct DriftPowerPush {
    double energy[4];
    std::uint32_t segments, first_segment, chunk_segments, fft_rows, taps, unused[3];
};
inline constexpr std::string_view drift_power_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Columns { vec2 columns[]; };
layout(std430,set=0,binding=1) writeonly buffer Power { double power[]; };
layout(std430,set=0,binding=2) buffer Flag { uint flag[]; };
layout(push_constant) uniform Parameters {
    double energy0; double energy1; double energy2; double energy3;
    uint segments; uint first_segment; uint chunk_segments; uint fft_rows; uint taps;
    uint unused0; uint unused1; uint unused2;
} p;
double next_toward(double x, bool up) {
    uvec2 w=unpackDouble2x32(x);
    bool away=(x>0.0)==up;
    if(x==0.0) return up?packDouble2x32(uvec2(1u,0u)):packDouble2x32(uvec2(1u,0x80000000u));
    if(away) { w.x+=1u; if(w.x==0u) w.y+=1u; }
    else { if(w.x==0u) w.y-=1u; w.x-=1u; }
    return packDouble2x32(w);
}
// Round-to-nearest a/b for finite a >= 0 and b > 0.
double divide(double a, double b) {
    precise double q=a/b;
    if(isnan(q)||isinf(q)) return q;
    precise double r=fma(-q,b,a);
    if(r==0.0) return q;
    precise double c=q+r/b;
    precise double s=fma(-c,b,a);
    if(s!=0.0) {
        double n=next_toward(c,s>0.0);
        precise double t=fma(-n,b,a);
        if(abs(t)<abs(s)) c=n;
    }
    return c;
}
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.chunk_segments*p.fft_rows) return;
    uint bin=i&(p.fft_rows-1u), local=i/p.fft_rows;
    uint width=2u*p.taps;
    double energy[4]=double[4](p.energy0,p.energy1,p.energy2,p.energy3);
    for(uint bit=0u;bit<2u;++bit) {
        precise double value=0.0;
        for(uint tap=0u;tap<p.taps;++tap) {
            double e=energy[2u*bit+tap];
            if(e<=0.0) continue;
            vec2 x=columns[(local*width+bit*p.taps+tap)*p.fft_rows+bin];
            double re=double(x.x), im=double(x.y);
            precise double norm=re*re+im*im;
            value=value+divide(norm,e);
        }
        if(isnan(value)||isinf(value)) atomicOr(flag[0],1u);
        power[(bit*p.segments+p.first_segment+local)*p.fft_rows+bin]=value;
    }
}
)glsl";

// One workgroup per ridge slope. Each center sums both bits' rows over the
// segments in segment order in FP64, skipping a segment whose shifted bin
// leaves (-fft_rows/2, fft_rows/2); offsets are the host-rounded ones,
// clamped where every center skips. The record is each bit's first maximum
// by ascending center; NaN never wins. A record is {score0, score1,
// center index 0, center index 1}, the index -1 with score -inf for none.
struct DriftRidgePush { std::uint32_t segments, fft_rows, centers; std::int32_t bin_radius, bin_stride; std::uint32_t slopes, unused[2]; };
inline constexpr std::string_view drift_ridge_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Power { double power[]; };
layout(std430,set=0,binding=1) readonly buffer Offsets { int offsets[]; };
layout(std430,set=0,binding=2) writeonly buffer Records { double records[]; };
layout(push_constant) uniform Parameters {
    uint segments; uint fft_rows; uint centers; int bin_radius; int bin_stride; uint slopes; uint unused0; uint unused1;
} p;
shared double best_score[2][256];
shared int best_center[2][256];
void main() {
    uint slope=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x, lane=gl_LocalInvocationID.x;
    if(slope>=p.slopes) return;
    int half_rows=int(p.fft_rows/2u);
    double none=packDouble2x32(uvec2(0u,0xfff00000u));
    double score0=none, score1=none;
    int at0=-1, at1=-1;
    for(uint center=lane;center<p.centers;center+=256u) {
        precise double sum0=0.0, sum1=0.0;
        for(uint segment=0u;segment<p.segments;++segment) {
            int bin=offsets[slope*p.segments+segment]-p.bin_radius+int(center)*p.bin_stride;
            if(bin<=-half_rows||bin>=half_rows) continue;
            uint column=uint(bin<0?bin+int(p.fft_rows):bin);
            sum0=sum0+power[segment*p.fft_rows+column];
            sum1=sum1+power[(p.segments+segment)*p.fft_rows+column];
        }
        if(sum0>score0) { score0=sum0; at0=int(center); }
        if(sum1>score1) { score1=sum1; at1=int(center); }
    }
    best_score[0][lane]=score0; best_center[0][lane]=at0;
    best_score[1][lane]=score1; best_center[1][lane]=at1;
    barrier();
    for(uint stride=128u;stride>0u;stride>>=1u) {
        if(lane<stride)
            for(uint bit=0u;bit<2u;++bit) {
                double a=best_score[bit][lane], b=best_score[bit][lane+stride];
                int ia=best_center[bit][lane], ib=best_center[bit][lane+stride];
                if(ib>=0&&(b>a||(b==a&&(ia<0||ib<ia)))) {
                    best_score[bit][lane]=b; best_center[bit][lane]=ib;
                }
            }
        barrier();
    }
    if(lane==0u) {
        records[4u*slope]=best_score[0][0]; records[4u*slope+1u]=best_score[1][0];
        records[4u*slope+2u]=double(best_center[0][0]); records[4u*slope+3u]=double(best_center[1][0]);
    }
}
)glsl";

// The dechirped products [slot][column][nfft] of consecutive slopes,
// zero-padded past the proposal rows: staged round32(P * normalized) times
// exp(-i pi rate t^2), each slot's rate the host's slope * rate step. The
// phase is computed in FP64 turns at t = first_t + j * delta, reduced to a
// quarter turn and an angle of at most pi/4, and evaluated with FP32
// minimax sine and cosine and an FP32 complex product.
struct DriftDechirpPush {
    double delta, first_t;
    std::uint32_t rows, nfft, columns, slot_rows, first_rate, slots;
};
inline constexpr std::string_view drift_dechirp_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=256) in;
layout(std430,set=0,binding=0) readonly buffer Staging { vec2 staging[]; };
layout(std430,set=0,binding=1) readonly buffer Rates { double rates[]; };
layout(std430,set=0,binding=2) writeonly buffer Columns { vec2 columns[]; };
layout(push_constant) uniform Parameters {
    double delta; double first_t; uint rows; uint nfft; uint columns; uint slot_rows; uint first_rate; uint slots;
} p;
void main() {
    uint i=gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*256u;
    if(i>=p.slots*p.nfft) return;
    uint j=i&(p.nfft-1u), slot=i/p.nfft, base=slot*p.slot_rows;
    if(j>=p.rows) {
        for(uint column=0u;column<p.columns;++column) columns[base+column*p.nfft+j]=vec2(0);
        return;
    }
    precise double t=p.first_t+double(j)*p.delta;
    precise double turns=-0.5*rates[p.first_rate+slot]*t*t;
    double u=fract(turns);
    double quarter=floor(u*4.0+0.5);
    precise double v=u-quarter*0.25;
    precise float x=float(v*6.283185307179586lf);
    precise float z=x*x;
    precise float s=((-1.9515295891e-4*z+8.3321608736e-3)*z-1.6666654611e-1)*z*x+x;
    precise float c=((2.443315711809948e-5*z-1.388731625493765e-3)*z+4.166664568298827e-2)*z*z-0.5*z+1.0;
    uint q=uint(quarter)&3u;
    vec2 e=q==0u?vec2(c,s):q==1u?vec2(-s,c):q==2u?vec2(-c,-s):vec2(s,-c);
    for(uint column=0u;column<p.columns;++column) {
        vec2 a=staging[column*p.rows+j];
        precise float real_value=a.x*e.x-a.y*e.y;
        precise float imag_value=a.x*e.y+a.y*e.x;
        columns[base+column*p.nfft+j]=vec2(real_value,imag_value);
    }
}
)glsl";

// Each workgroup scans a contiguous run of a slope's public bins, by
// ascending signed bin in each thread, with the native FP64 rank
// |F0|^2 + |F1|^2 and, when nominating, the native normalized power (a tap
// with energy > 0 only, correctly rounded quotients). It keeps each bit's
// stable top eight (greater rank first, equal ranks by lower signed bin;
// NaN and -inf never enter) and its largest nomination, merges its threads'
// lists, and writes one partial. One dispatch scans consecutive slots
// [slot][column][nfft], groups workgroups each, into consecutive partials
// from partial_slot. A NaN or infinite rank sets the call's flag.
struct DriftPeaksPush {
    double energy[4];
    std::uint32_t nfft, taps, radius, bins, groups, nominate, slot_rows, partial_slot, slots, unused[3];
};
inline constexpr std::string_view drift_peaks_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=128) in;
layout(std430,set=0,binding=0) readonly buffer Columns { vec2 columns[]; };
layout(std430,set=0,binding=1) writeonly buffer Partial { double partial[]; };
layout(std430,set=0,binding=2) buffer Flag { uint flag[]; };
layout(push_constant) uniform Parameters {
    double energy0; double energy1; double energy2; double energy3;
    uint nfft; uint taps; uint radius; uint bins; uint groups; uint nominate; uint slot_rows; uint partial_slot;
    uint slots; uint unused0; uint unused1; uint unused2;
} p;
shared double shared_rank[2][128][8];
shared int shared_bin[2][128][8];
shared double shared_nomination[2][128];
double next_toward(double x, bool up) {
    uvec2 w=unpackDouble2x32(x);
    bool away=(x>0.0)==up;
    if(x==0.0) return up?packDouble2x32(uvec2(1u,0u)):packDouble2x32(uvec2(1u,0x80000000u));
    if(away) { w.x+=1u; if(w.x==0u) w.y+=1u; }
    else { if(w.x==0u) w.y-=1u; w.x-=1u; }
    return packDouble2x32(w);
}
double divide(double a, double b) {
    precise double q=a/b;
    if(isnan(q)||isinf(q)) return q;
    precise double r=fma(-q,b,a);
    if(r==0.0) return q;
    precise double c=q+r/b;
    precise double s=fma(-c,b,a);
    if(s!=0.0) {
        double n=next_toward(c,s>0.0);
        precise double t=fma(-n,b,a);
        if(abs(t)<abs(s)) c=n;
    }
    return c;
}
double rank_list[2][8];
int bin_list[2][8];
bool before(double rank, int bin, double other_rank, int other_bin) {
    return rank>other_rank||(rank==other_rank&&bin<other_bin);
}
void insert(uint bit, double rank, int bin) {
    double none=packDouble2x32(uvec2(0u,0xfff00000u));
    if(!(rank>none)) return;
    if(!before(rank,bin,rank_list[bit][7],bin_list[bit][7])) return;
    int at=7;
    while(at>0&&before(rank,bin,rank_list[bit][at-1],bin_list[bit][at-1])) {
        rank_list[bit][at]=rank_list[bit][at-1]; bin_list[bit][at]=bin_list[bit][at-1];
        --at;
    }
    rank_list[bit][at]=rank; bin_list[bit][at]=bin;
}
void main() {
    uint lane=gl_LocalInvocationID.x;
    uint linear=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    uint slot=linear/p.groups, group=linear%p.groups;
    bool scanning=slot<p.slots, nonfinite=false;
    uint slot_offset=slot*p.slot_rows;
    double none=packDouble2x32(uvec2(0u,0xfff00000u));
    double nomination[2]=double[2](none,none);
    for(uint bit=0u;bit<2u;++bit)
        for(uint k=0u;k<8u;++k) { rank_list[bit][k]=none; bin_list[bit][k]=0; }
    double energy[4]=double[4](p.energy0,p.energy1,p.energy2,p.energy3);
    if(scanning) {
        uint first=group*128u*64u;
        for(uint step=0u;step<64u;++step) {
            uint b=first+step*128u+lane;
            if(b>=p.bins) break;
            int signed_bin=int(b)-int(p.radius);
            uint offset=uint(signed_bin<0?signed_bin+int(p.nfft):signed_bin);
            for(uint bit=0u;bit<2u;++bit) {
                uint column=bit*p.taps;
                vec2 r0=columns[slot_offset+column*p.nfft+offset];
                double a=double(r0.x), b0=double(r0.y);
                precise double n0=a*a+b0*b0;
                precise double n1=0.0;
                if(p.taps==2u) {
                    vec2 r1=columns[slot_offset+(column+1u)*p.nfft+offset];
                    double c=double(r1.x), d=double(r1.y);
                    n1=c*c+d*d;
                }
                precise double rank=n0+n1;
                nonfinite=nonfinite||isnan(rank)||isinf(rank);
                insert(bit,rank,signed_bin);
                if(p.nominate!=0u) {
                    precise double normalized=0.0;
                    if(energy[2u*bit]>0.0) normalized=normalized+divide(n0,energy[2u*bit]);
                    if(p.taps==2u&&energy[2u*bit+1u]>0.0) normalized=normalized+divide(n1,energy[2u*bit+1u]);
                    if(normalized>nomination[bit]) nomination[bit]=normalized;
                }
            }
        }
    }
    for(uint bit=0u;bit<2u;++bit) {
        for(uint k=0u;k<8u;++k) { shared_rank[bit][lane][k]=rank_list[bit][k]; shared_bin[bit][lane][k]=bin_list[bit][k]; }
        shared_nomination[bit][lane]=nomination[bit];
    }
    barrier();
    for(uint stride=64u;stride>0u;stride>>=1u) {
        if(lane<stride)
            for(uint bit=0u;bit<2u;++bit) {
                for(uint k=0u;k<8u;++k)
                    insert(bit,shared_rank[bit][lane+stride][k],shared_bin[bit][lane+stride][k]);
                for(uint k=0u;k<8u;++k) { shared_rank[bit][lane][k]=rank_list[bit][k]; shared_bin[bit][lane][k]=bin_list[bit][k]; }
                double other=shared_nomination[bit][lane+stride];
                if(other>nomination[bit]) nomination[bit]=other;
                shared_nomination[bit][lane]=nomination[bit];
            }
        barrier();
    }
    if(nonfinite) atomicOr(flag[0],1u);
    if(lane==0u&&scanning) {
        uint base=((p.partial_slot+slot)*p.groups+group)*34u;
        for(uint bit=0u;bit<2u;++bit) {
            for(uint k=0u;k<8u;++k) {
                partial[base+(bit*8u+k)*2u]=rank_list[bit][k];
                partial[base+(bit*8u+k)*2u+1u]=double(bin_list[bit][k]);
            }
            partial[base+32u+bit]=nomination[bit];
        }
    }
}
)glsl";

// One thread per (slope, bit) merges a slope's workgroup partials into its
// stable top eight and largest nomination, in the same order, and writes
// them at the slope's place in the call's frontier.
struct DriftMergePush { std::uint32_t groups, slopes, output_slope, capacity; };
inline constexpr std::string_view drift_peaks_merge_shader = R"glsl(
#version 450
#extension GL_ARB_gpu_shader_fp64 : require
layout(local_size_x=64) in;
layout(std430,set=0,binding=0) readonly buffer Partial { double partial[]; };
layout(std430,set=0,binding=1) writeonly buffer Frontier { double frontier[]; };
layout(push_constant) uniform Parameters { uint groups; uint slopes; uint output_slope; uint capacity; } p;
bool before(double rank, int bin, double other_rank, int other_bin) {
    return rank>other_rank||(rank==other_rank&&bin<other_bin);
}
void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=2u*p.slopes) return;
    uint slope=i/2u, bit=i%2u;
    double none=packDouble2x32(uvec2(0u,0xfff00000u));
    double rank_list[8];
    int bin_list[8];
    for(uint k=0u;k<8u;++k) { rank_list[k]=none; bin_list[k]=0; }
    double nomination=none;
    for(uint group=0u;group<p.groups;++group) {
        uint base=(slope*p.groups+group)*34u;
        for(uint k=0u;k<8u;++k) {
            double rank=partial[base+(bit*8u+k)*2u];
            int bin=int(partial[base+(bit*8u+k)*2u+1u]);
            if(!(rank>none)) break;
            if(!before(rank,bin,rank_list[7],bin_list[7])) break;
            int at=7;
            while(at>0&&before(rank,bin,rank_list[at-1],bin_list[at-1])) {
                rank_list[at]=rank_list[at-1]; bin_list[at]=bin_list[at-1];
                --at;
            }
            rank_list[at]=rank; bin_list[at]=bin;
        }
        double other=partial[base+32u+bit];
        if(other>nomination) nomination=other;
    }
    uint out_base=((p.output_slope+slope)*2u+bit)*16u;
    for(uint k=0u;k<8u;++k) {
        frontier[out_base+2u*k]=rank_list[k];
        frontier[out_base+2u*k+1u]=double(bin_list[k]);
    }
    frontier[p.capacity*32u+(p.output_slope+slope)*2u+bit]=nomination;
}
)glsl";

constexpr std::string_view drift_shader_source(std::uint16_t index) noexcept {
    switch (index) {
    case drift_segment_column: return drift_segment_column_shader;
    case drift_power: return drift_power_shader;
    case drift_ridge: return drift_ridge_shader;
    case drift_dechirp: return drift_dechirp_shader;
    case drift_peaks: return drift_peaks_shader;
    case drift_peaks_merge: return drift_peaks_merge_shader;
    default: return {};
    }
}

} // namespace datapump::triad::v3::receiver_detail::vulkan

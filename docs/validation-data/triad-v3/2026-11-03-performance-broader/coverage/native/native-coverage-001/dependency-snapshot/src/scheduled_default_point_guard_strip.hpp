#pragma once
// Native-center subtraction only. Unknown requests use the same-chord raw-row
// fallback. Neither the result nor the stability policy is an Original bound.
#include "scheduled_default_point_guard_archive.hpp"

namespace datapump::modem::detail::scheduled_default_point_guard_archive {
struct Stripped {fft::InputRow row{};long double energy=0;std::uint64_t count=0;bool known=false;};
inline fft::InputRow edge_planes(const EdgeRow& e) {
    const auto& p=e.projection;const auto a=1-e.t,b=e.t;
    const fft::Wide y{p.xc,p.xs},k{p.cc-p.ss,2*p.cs},trace{p.cc+p.ss,0};
    fft::InputRow row{};row[0]=a*y;row[1]=b*y;
    const std::array<double,3> weights{a*a,2*a*b,b*b};
    for(unsigned j=0;j<3;++j){row[2+j]=weights[j]*k;row[5+j]=weights[j]*trace;}
    return row;
}
// Both inputs must come from the SAME archived absolute cell and identity.
// Explicit layout validates physical/t ownership even for standalone callers.
// All deleted rows must be present in the disjoint endpoint union. Native rank
// remains caller-assessed by point::contract: accept only two/zero_fields.
inline Stripped strip(Archive::View input,const EdgeCell& edges,std::uint64_t first,std::uint64_t end,
        const Layout& layout,std::size_t absolute_cell) {
    Stripped out;
    if(!scheduled_acquisition_certificate::environment<double>()||
       std::numeric_limits<long double>::digits<=std::numeric_limits<double>::digits||
       !input.row||!valid_edges(edges,layout,absolute_cell)||input.count!=edges.count||
       !std::isfinite(input.energy)||input.energy<0||first<edges.first||end>edges.end||first>end||end-first<=2)return out;
    // At most four unique endpoint records exist. Reject larger removal before
    // iterating; no unsigned underflow or overlapping double-deletion.
    const auto left=first-edges.first,right=edges.end-end;
    if(left>4||right>4||left+right>edges.head_count+edges.tail_count)return out;
    std::array<const EdgeRow*,4> removed{};unsigned n=0;
    for(auto p=edges.first;p<first;++p){const auto* row=find_edge(edges,p);if(!row)return out;removed[n++]=row;}
    for(auto p=end;p<edges.end;++p){const auto* row=find_edge(edges,p);if(!row)return out;removed[n++]=row;}
    if(input.count-n!=end-first)return out;
    std::array<long double,16> sums{},mass{};
    for(unsigned k=0;k<8;++k) {
        const auto z=(*input.row)[k];if(!std::isfinite(z.real())||!std::isfinite(z.imag())||(k>=5&&z.imag()!=0))return out;
        sums[2*k]=z.real();sums[2*k+1]=z.imag();mass[2*k]=std::abs(sums[2*k]);mass[2*k+1]=std::abs(sums[2*k+1]);
    }
    auto energy=input.energy,energy_mass=std::abs(input.energy);
    for(unsigned i=0;i<n;++i) {
        const auto row=edge_planes(*removed[i]);
        for(unsigned k=0;k<8;++k) {
            if(!std::isfinite(row[k].real())||!std::isfinite(row[k].imag()))return out;
            sums[2*k]-=static_cast<long double>(row[k].real());sums[2*k+1]-=static_cast<long double>(row[k].imag());
            mass[2*k]+=std::abs(static_cast<long double>(row[k].real()));mass[2*k+1]+=std::abs(static_cast<long double>(row[k].imag()));
        }
        energy-=static_cast<long double>(removed[i]->projection.energy);energy_mass+=removed[i]->projection.energy;
    }
    if(!std::isfinite(energy)||!std::isfinite(energy_mass)||energy<0)return out;
    // Cancellation of positive energy/trace sums is a numerical fallback,
    // not a rounding certificate. Never clamp negative or unstable residues.
    if(energy_mass>0&&energy<=64*std::numeric_limits<long double>::epsilon()*energy_mass)return out;
    for(unsigned k=0;k<8;++k) {
        const auto re=sums[2*k],im=sums[2*k+1];
        if(!std::isfinite(re)||!std::isfinite(im))return out;
        if(k>=5&&(re<0||(mass[2*k]>0&&re<=64*std::numeric_limits<double>::epsilon()*mass[2*k])))return out;
        out.row[k]={static_cast<double>(re),static_cast<double>(im)};
        if(!std::isfinite(out.row[k].real())||!std::isfinite(out.row[k].imag()))return Stripped{};
    }
    if(energy==0&&(out.row[0]!=fft::Wide{}||out.row[1]!=fft::Wide{}))return Stripped{};
    out.energy=energy;out.count=end-first;out.known=true;return out;
}
} // namespace datapump::modem::detail::scheduled_default_point_guard_archive

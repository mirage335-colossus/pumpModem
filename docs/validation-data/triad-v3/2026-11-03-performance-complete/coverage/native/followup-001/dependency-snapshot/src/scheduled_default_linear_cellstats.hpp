#pragma once
// Experimental native centers for ONE endpoint-chord template. No Original
// error, mask/owner proof, detector decision or score certificate is produced.
#include "linear_grid_moments.hpp"
#include "scheduled_cold_colored_source_rows.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace datapump::modem::detail::scheduled_default_linear_cellstats {
namespace grid=scheduled_linear_grid;
namespace fft=scheduled_linear_grid_fft;
namespace cert=scheduled_acquisition_certificate;
using Wide=fft::Wide;
inline constexpr std::uint64_t maximum_exact_count=0x1fffffffffffffULL;

// Same eight center planes as grid::Moments, without error/interval/mass
// preparation. Source/grid coordinates, observed support and the physical
// carrier/prefix association are caller-owned. No interpolated noise samples.
class Moments {
    std::size_t current_=0;
    bool have_=false,finished_=false,seen_=false;
    std::uint64_t last_physical_=0,current_count_=0;
    std::array<Wide,8> sum_{},correction_{};
    long double current_energy_=0;
    double gain_upper_=0;
    std::vector<fft::InputRow> rows_;
    std::vector<long double> energy_;
    std::vector<std::uint64_t> count_;
    static void add_scalar(double x,double& sum,double& correction) {
        const auto next=sum+x;
        correction+=(std::abs(sum)>=std::abs(x)?(sum-next)+x:(x-next)+sum);
        sum=next;
    }
    void add_plane(std::size_t plane,Wide value) {
        fft::require(cert::finite(value),"center linear moment product overflow");
        auto re=sum_[plane].real(),im=sum_[plane].imag();
        auto cr=correction_[plane].real(),ci=correction_[plane].imag();
        add_scalar(value.real(),re,cr);add_scalar(value.imag(),im,ci);
        sum_[plane]={re,im};correction_[plane]={cr,ci};
    }
    void flush() {
        if(!have_)return;
        for(std::size_t plane=0;plane<8;++plane) {
            rows_[current_][plane]=sum_[plane]+correction_[plane];
            fft::require(cert::finite(rows_[current_][plane]),"center linear moment sum overflow");
        }
        energy_[current_]=current_energy_;count_[current_]=current_count_;
        sum_={};correction_={};current_energy_=0;current_count_=0;
    }
public:
    static std::size_t reservation(std::size_t cells) {
        return fft::add(sizeof(Moments)+1024,fft::mul(cells,
            sizeof(fft::InputRow)+sizeof(long double)+sizeof(std::uint64_t)));
    }
    Moments(std::size_t cells,std::size_t budget) {
        fft::require(cert::environment<double>() && cells && reservation(cells)<=budget,
            "center linear moments exceed environment/workspace contract");
        gain_upper_=scheduled_cold_colored::smooth_gain_upper();
        fft::require(std::isfinite(gain_upper_) && gain_upper_>=1,"center linear gain cap invalid");
        rows_.resize(cells);energy_.resize(cells);count_.resize(cells);
        fft::require(working_bytes()<=budget,"center linear actual moments exceed workspace");
    }
    Moments(const Moments&)=delete;
    Moments& operator=(const Moments&)=delete;
    ~Moments(){if(!rows_.empty())OPENSSL_cleanse(rows_.data(),rows_.size()*sizeof(rows_[0]));}
    std::size_t working_bytes()const {
        return sizeof(*this)+1024+rows_.capacity()*sizeof(rows_[0])+
            energy_.capacity()*sizeof(energy_[0])+count_.capacity()*sizeof(count_[0]);
    }
    // localu is a RELATIVE grid coordinate. The qualified RN environment must
    // remain unchanged from construction through finish. Missing/zero-gain
    // rows advance continuity, but contribute no fields, energy or count.
    void add(std::uint64_t physical,long double localu,const CorrelationProjection& p,
             double gain,bool observed=true) {
        fft::require(!finished_ && std::isfinite(localu) && localu>=0 && localu<rows_.size(),
            "center linear coordinate outside borrowed extent");
        fft::require(!seen_ || physical>last_physical_,"center linear repeated/reversed physical sample");
        const auto cell=static_cast<std::size_t>(std::floor(localu));
        const auto t=static_cast<double>(localu-cell);
        fft::require(t>=0 && t<1 && std::isfinite(gain) && gain>=0 && gain<=gain_upper_,
            "center linear weight/gain invalid");
        if(have_)fft::require(cell>=current_,"center linear grid rewound");
        seen_=true;last_physical_=physical;
        if(!have_ || cell!=current_){flush();current_=cell;have_=true;}
        if(!observed || gain<=0)return;
        fft::require(std::isfinite(p.xc) && std::isfinite(p.xs) && std::isfinite(p.cc) &&
            std::isfinite(p.ss) && std::isfinite(p.cs) && std::isfinite(p.energy) && p.energy>=0,
            "center linear physical projection invalid");
        fft::require(p.cc>=0 && p.ss>=0 && p.cc<=4 && p.ss<=4 && std::abs(p.cs)<=4 &&
            std::abs(p.xc)<=0x1p128 && std::abs(p.xs)<=0x1p128 && p.energy<=0x1p256,
            "center linear projection outside physical producer bounds");
        const auto a=1-t,b=t,g2=gain*gain;
        const Wide y{p.xc*gain,p.xs*gain},k{p.cc-p.ss,2*p.cs},trace{p.cc+p.ss,0};
        add_plane(0,a*y);add_plane(1,b*y);
        const std::array<double,3> weights{a*a,2*a*b,b*b};
        for(unsigned j=0;j<3;++j) {
            add_plane(2+j,weights[j]*g2*k);add_plane(5+j,weights[j]*g2*trace);
        }
        current_energy_+=p.energy;
        fft::require(std::isfinite(current_energy_),"center linear energy overflow");
        fft::require(current_count_!=UINT64_MAX,"center linear sample count overflow");
        ++current_count_;
    }
    void finish() {
        if(finished_)return;
        fft::require(cert::environment<double>(),"center linear environment changed");
        flush();finished_=true;
    }
    std::span<const fft::InputRow> rows()const {
        fft::require(finished_,"center linear moments not sealed");return rows_;
    }
    std::span<const long double> energy()const {
        fft::require(finished_,"center linear energy not sealed");return energy_;
    }
    std::span<const std::uint64_t> count()const {
        fft::require(finished_,"center linear count not sealed");return count_;
    }
};

enum class NativeRank {empty,zero_fields,two,refine};
struct CellStats {
    std::array<CorrelationFit,2> fits{};
    std::array<NativeRank,2> rank{NativeRank::refine,NativeRank::refine};
    // valid means finite native centers only. It is NOT an Original/rank proof.
    bool valid=false;
    bool needs_refinement=true;
};
inline NativeRank native_rank(const CorrelationFit& fit) {
    if(!fit.count)return NativeRank::empty;
    if(CorrelationChipEvidence::zero_basis(fit))return NativeRank::zero_fields;
    const auto trace=fit.cc+fit.ss;
    const auto determinant=fit.cc*fit.ss-fit.cs*fit.cs;
    const auto cutoff=1e-12*trace*trace; // SAME per-chip branch association.
    if(!std::isfinite(trace) || !std::isfinite(determinant) || !std::isfinite(cutoff) ||
       trace<=1e-30 || fit.cc<=0 || fit.ss<=0)return NativeRank::refine;
    if(fit.count<=1 || determinant<=cutoff)return NativeRank::refine;
    // Finite fields/rank determinant alone do not make the executed quadratic
    // usable: huge finite dots can produce inf-inf in Original projection.
    const auto a=fit.ss*fit.xc,b=fit.cc*fit.xs,c=2*fit.cs;
    const auto aa=a*fit.xc,bb=b*fit.xs,cx=c*fit.xc,cross=cx*fit.xs;
    const auto sum=aa+bb,numerator=sum-cross,projected=numerator/determinant;
    for(const auto value:{a,b,c,aa,bb,cx,cross,sum,numerator,projected})
        if(!std::isfinite(value))return NativeRank::refine;
    return NativeRank::two;
}
template<bool AssessRank> inline CellStats contract_impl(const fft::InputRow& input,const grid::Cell& cell,
                          double energy,std::uint64_t count) {
    CellStats result;
    if(!cert::environment<double>() || !cell.supported || !std::isfinite(energy) ||
       energy<0 || count>maximum_exact_count)return result;
    // Quiet is an explicit support exclusion. Never infer it from private q=0.
    if(!cell.observed) {
        if(count || energy!=0)return result;
        result.rank={NativeRank::empty,NativeRank::empty};
        result.valid=true;result.needs_refinement=false;return result;
    }
    for(const auto value:input)if(!cert::finite(value))return result;
    for(unsigned plane=5;plane<8;++plane)if(input[plane].imag()!=0)return result;
    for(unsigned bit=0;bit<2;++bit)
        if(!cert::finite(cell.left[bit]) || !cert::finite(cell.right[bit]))return result;
    if(!count) {
        if(energy!=0 || std::any_of(input.begin(),input.end(),[](auto v){return v!=Wide{};}))return result;
        result.rank={NativeRank::empty,NativeRank::empty};
        result.valid=true;result.needs_refinement=false;return result;
    }
    // Plane3/6 ALREADY contain2ab. Image products have no conjugation; the
    // trace cross coefficient is real(L*conj(R)). Preserve grid native order.
    const auto coefficients=grid::row(cell);fft::Output products{};
    for(unsigned bit=0;bit<2;++bit)for(unsigned plane=0;plane<8;++plane) {
        const auto at=8*bit+plane;
        if(!cert::finite(coefficients[at]))return result;
        products[at]=input[plane]*coefficients[at];
        if(!cert::finite(products[at]))return result;
    }
    result.fits=grid::fit(products,energy,count,true);
    // Only the diagnostic entry point consumes per-cell rank probes. Event
    // guards assess the AGGREGATED chip; an unassessed center is not a rank proof.
    result.needs_refinement=!AssessRank;
    for(unsigned bit=0;bit<2;++bit) {
        const auto& f=result.fits[bit];
        if(!std::isfinite(f.xc) || !std::isfinite(f.xs) || !std::isfinite(f.cc) ||
           !std::isfinite(f.ss) || !std::isfinite(f.cs) || f.cc<0 || f.ss<0)return CellStats{};
        if constexpr(AssessRank) {
            result.rank[bit]=native_rank(f);
            result.needs_refinement=result.needs_refinement || result.rank[bit]==NativeRank::refine;
        }
    }
    result.valid=true;return result;
}
// Exact same fits and validity as contract(). Nonempty rank/refinement fields
// deliberately remain unassessed; no event consumer may use them as evidence.
inline CellStats contract_centers(const fft::InputRow& input,const grid::Cell& cell,
                                 double energy,std::uint64_t count) {
    return contract_impl<false>(input,cell,energy,count);
}
// Preserve the existing rank-aware diagnostic API and its execution order.
inline CellStats contract(const fft::InputRow& input,const grid::Cell& cell,
                          double energy,std::uint64_t count) {
    return contract_impl<true>(input,cell,energy,count);
}
} // namespace datapump::modem::detail::scheduled_default_linear_cellstats

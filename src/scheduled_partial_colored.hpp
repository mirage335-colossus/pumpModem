#pragma once
// A partial scan can ONLY reject the unchanged ORIGINAL colored statistic.
// All source owners/ranks/nesting are first proved independently of PCM dots;
// selected complete cells supply a chronological lower projected-energy bound.
// This is NOT a Summary, receiver Fit, admission or observed-absence event.
#include "scheduled_cold_colored_certificate.hpp"
#include <optional>
#include <vector>

namespace datapump::modem::detail::scheduled_partial_colored {
namespace c=scheduled_acquisition_certificate;
namespace original=scheduled_cold_colored;
using c::Interval;
inline constexpr std::uint64_t exact_integer_limit=0x1fffffffffffffULL;
struct Gram {Interval cc,ss,cs;};
struct Basis {Interval real,imag;};
struct Witness {std::uint64_t physical_sample=0;Basis basis;};
struct FiniteProjectionDomain {
    // Already validated ORIGINAL float PCM, bounded real basis rows and
    // prefix convention. These bounds ensure every omitted projection and
    // its nonnegative clamp/sum is finite; rank alone does not prove that.
    double maximum_template_magnitude=0,maximum_original_carrier_magnitude=0;
    std::size_t original_prefix_samples=0;
    bool finite_original_float_pcm=false;
};
inline bool finite(const Gram& g){return g.cc.bounded()&&g.ss.bounded()&&g.cs.bounded();}
// Intervals enclose ORIGINAL executed Gram fields, including prefix/restart,
// covariance reconstruction and source/ramp/membership error. Exact-zero must
// be proved from every original basis row, never a rounded small trace.
inline std::optional<unsigned> rank(std::uint64_t n,const Gram& g,bool exact_zero=false){
    if(n>exact_integer_limit||!finite(g)||!c::environment<double>())return {};
    if(!n||exact_zero){
        if(g.cc.lo>0||g.cc.hi<0||g.ss.lo>0||g.ss.hi<0||g.cs.lo>0||g.cs.hi<0)return {};
        return 0;
    }
    const auto trace=c::plus(g.cc,g.ss);if(!trace.bounded()||trace.lo<=1e-30)return {};
    if(n==1)return 1;
    const auto det=c::minus(c::times(g.cc,g.ss),c::times(g.cs,g.cs));
    const auto cutoff=c::times(c::times(Interval::point(1e-12),trace),trace);
    if(!det.bounded()||!cutoff.bounded()||det.lo<=cutoff.hi)return {};
    return 2;
}
// A caller may prove the mathematical full-cell Gram eigenvalue lower
// bound without constructing its physical orientation (common rotations do
// not change eigenvalues). This still proves the ORIGINAL executed branch,
// including all original entry/prefix/restart error; it is not a proxy rank.
inline bool rank2_eigen_lower(std::uint64_t n,double mathematical_eigen_lower,
        double trace_upper,double entry_radius){
    if(n<2||n>exact_integer_limit||!c::environment<double>()||
       !std::isfinite(mathematical_eigen_lower)||mathematical_eigen_lower<=0||
       !std::isfinite(trace_upper)||trace_upper<0||!std::isfinite(entry_radius)||entry_radius<0)return false;
    const auto eigen=c::down(mathematical_eigen_lower-c::mul(2,entry_radius));if(!(eigen>0))return false;
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto field_trace=c::add(trace_upper,c::mul(2,entry_radius));
    const auto trace_hi=c::add(c::mul(c::add(1,u),field_trace),eta);
    const auto trace_lo=c::down(c::down(c::down(2*eigen)*(1-u))-eta);
    if(!(trace_lo>1e-30)||!std::isfinite(trace_hi))return false;
    // Conservative ORIGINAL two products/subtraction rounding. PSD gives
    // |cc*ss|+|cs*cs|<=trace^2/2; the larger gamma8 bound retains slack.
    const auto round=c::add(c::mul(c::gamma(8,u),c::mul(field_trace,field_trace)),16*eta);
    const auto det_lo=c::down(c::down(eigen*eigen)-round);
    const auto cutoff=c::times(c::times(Interval::point(1e-12),{0,trace_hi}),{0,trace_hi});
    return cutoff.bounded()&&det_lo>cutoff.hi;
}
// Two distinct observed basis rows are witnesses, not two samples invented
// from a proxy. Let G=sum(v*v') over ALL original mathematical basis rows,
// trace(G)<=trace_upper. Every ORIGINAL executed Gram entry differs from G by
// <=entry_radius, including original prefix/cancellation/restart rounding.
// Then G>=W, lambda_min(G)>=det(W)/trace(W), and Weyl costs<=2entry_radius.
inline bool rank2_witness(std::uint64_t n,Witness wa,Witness wb,double trace_upper,double entry_radius){
    const auto a=wa.basis,b=wb.basis;
    if(wa.physical_sample==wb.physical_sample||n<2||n>exact_integer_limit||!c::environment<double>()||
       !a.real.bounded()||!a.imag.bounded()||!b.real.bounded()||!b.imag.bounded()||
       !std::isfinite(trace_upper)||trace_upper<0||!std::isfinite(entry_radius)||entry_radius<0)return false;
    const Gram w{c::plus(c::square(a.real),c::square(b.real)),c::plus(c::square(a.imag),c::square(b.imag)),
        c::plus(c::times(a.real,a.imag),c::times(b.real,b.imag))};
    const auto trace=c::plus(w.cc,w.ss),det=c::minus(c::times(w.cc,w.ss),c::times(w.cs,w.cs));
    if(!trace.bounded()||trace.hi<=0||!det.bounded()||det.lo<=0)return false;
    return rank2_eigen_lower(n,c::down(det.lo/trace.hi),trace_upper,entry_radius);
}

class NestingBuilder;
class AllCells {
    friend class NestingBuilder;
    struct Cell {std::uint64_t count=0,prefix_count=0,prefix_rank=0;unsigned rank=0;};
    std::vector<Cell> cells_;
    std::uint64_t count_=0,rank_=0,identity_=0;
    bool valid_=false;
public:
    AllCells()=default;
    AllCells(AllCells&& other)noexcept:cells_(std::move(other.cells_)),count_(other.count_),rank_(other.rank_),
        identity_(other.identity_),valid_(other.valid_){other.valid_=false;other.count_=other.rank_=0;}
    AllCells& operator=(AllCells&&)=delete;
    AllCells(const AllCells&)=delete;AllCells& operator=(const AllCells&)=delete;
    bool valid()const{return valid_;}std::uint64_t count()const{return count_;}
    std::uint64_t rank()const{return rank_;}std::uint64_t identity()const{return identity_;}
    std::size_t owners()const{return cells_.size();}
    std::size_t working_bytes()const{return sizeof(*this)+cells_.capacity()*sizeof(Cell);}
    bool prefix(std::uint64_t owner,std::uint64_t count,std::uint64_t rank)const{return valid_&&owner<cells_.size()&&
        cells_[owner].prefix_count==count&&cells_[owner].prefix_rank==rank;}
    bool contains(std::uint64_t owner,std::uint64_t count,unsigned rank)const{return valid_&&owner<cells_.size()&&
        cells_[owner].count==count&&cells_[owner].rank==rank;}
};
// Enumerate EVERY relative source owner0..K-1, explicitly including zero-count
// cells. K comes from original symbol/chip geometry. Expected count comes from
// the exact ORIGINAL usable mask, independently of selected cells. Context
// identity must bind capture generation, key/epoch/symbol, phase/rate/frequency,
// calendar, gain, original carrier contract and observation/crop boundaries.
class NestingBuilder {
    AllCells proof_;std::uint64_t expected_count_;std::size_t expected_owners_;bool valid_=true;
    void append(std::uint64_t owner,std::uint64_t n,std::optional<unsigned> r){
        if(!valid_||!r||owner!=proof_.cells_.size()||owner>=expected_owners_||n>1048576||n>exact_integer_limit-proof_.count_||
           *r>2||*r>n){valid_=false;return;}
        proof_.count_+=n;proof_.rank_+=*r;proof_.cells_.push_back({n,proof_.count_,proof_.rank_,*r});
    }
public:
    NestingBuilder(std::uint64_t expected_count,std::size_t owners,std::size_t byte_limit,std::uint64_t identity,
                   FiniteProjectionDomain domain)
        :expected_count_(expected_count),expected_owners_(owners){
        proof_.identity_=identity;
        if(!domain.finite_original_float_pcm||!std::isfinite(domain.maximum_template_magnitude)||
           domain.maximum_template_magnitude<=0||domain.maximum_template_magnitude>32||
           !std::isfinite(domain.maximum_original_carrier_magnitude)||domain.maximum_original_carrier_magnitude<=0||
           domain.maximum_original_carrier_magnitude>2||
           !domain.original_prefix_samples||domain.original_prefix_samples>512||expected_count>exact_integer_limit||!owners||owners>1048576||byte_limit<sizeof(AllCells)||
           owners>(byte_limit-sizeof(AllCells))/sizeof(AllCells::Cell)){valid_=false;return;}
        proof_.cells_.reserve(owners);if(proof_.working_bytes()>byte_limit)valid_=false;
    }
    void gram(std::uint64_t owner,std::uint64_t n,Gram g,bool exact_zero=false){
        const auto trace=c::plus(g.cc,g.ss);
        if(!trace.bounded()||trace.hi>1e12){valid_=false;return;}
        append(owner,n,rank(n,g,exact_zero));
    }
    void witness(std::uint64_t owner,std::uint64_t n,Witness a,Witness b,double trace_upper,double entry_radius){
        if(trace_upper>1e12){valid_=false;return;}
        append(owner,n,rank2_witness(n,a,b,trace_upper,entry_radius)?std::optional<unsigned>{2}:std::nullopt);}
    void eigen(std::uint64_t owner,std::uint64_t n,double lower,double trace_upper,double entry_radius){
        if(trace_upper>1e12){valid_=false;return;}
        append(owner,n,rank2_eigen_lower(n,lower,trace_upper,entry_radius)?std::optional<unsigned>{2}:std::nullopt);
    }
    AllCells finish(){proof_.valid_=valid_&&proof_.cells_.size()==expected_owners_&&proof_.count_==expected_count_;
        return std::move(proof_);}
};
// AllCells must outlive every borrowing Partial and remain immutable. Moving
// the proof explicitly invalidates its source, so an old borrow becomes unknown.
class Partial {
    const AllCells* all_;std::uint64_t identity_,last_=0,rank_=0,count_=0;std::size_t selected_=0;bool valid_=true;
    Interval projected_;enum class Mode{unset,cells,prefix};Mode mode_=Mode::unset;
public:
    Partial(const AllCells& all,std::uint64_t identity):all_(&all),identity_(identity){valid_=all.valid()&&all.identity()==identity;}
    void complete_cell(std::uint64_t owner,const original::FitIntervals& f){
        if(!valid_)return;if(mode_==Mode::prefix){valid_=false;return;}mode_=Mode::cells;const auto p=original::projection(f);
        if((selected_&&owner<=last_)||f.count_lower!=f.count||f.count_upper!=f.count||!p.valid||!p.nested||
           !all_->contains(owner,f.count,p.rank)||f.count>all_->count()-count_){valid_=false;return;}
        projected_=c::plus(projected_,p.energy);rank_+=p.rank;count_+=f.count;last_=owner;++selected_;
        valid_=projected_.bounded();
    }
    // Producer contract: this is the ACTUAL chronological original sum of
    // complete owners0..owner, each joined exactly once, after every physical
    // fragment/halo for owner. It is not a block snapshot or fabricated full
    // Summary. Zero-count gaps are included in the preflight prefix identity.
    void certified_prefix(std::uint64_t owner,const original::Summary& prefix){
        if(!valid_)return;
        if(mode_==Mode::cells||(mode_==Mode::prefix&&owner<=last_)||!prefix.valid||!prefix.nested||
           !prefix.owner_set_certified||!prefix.projected.bounded()||prefix.projected.hi<0||
           !all_->prefix(owner,prefix.count,prefix.rank)){valid_=false;return;}
        mode_=Mode::prefix;projected_={std::max(0.,prefix.projected.lo),prefix.projected.hi};rank_=prefix.rank;count_=prefix.count;
        last_=owner;selected_=static_cast<std::size_t>(owner)+1;
    }
    bool valid()const{return valid_&&all_->valid()&&all_->identity()==identity_;}
    const AllCells& all()const{return *all_;}Interval projected()const{return projected_;}
    std::uint64_t selected_rank()const{return rank_;}std::uint64_t count()const{return count_;}
    std::size_t owners()const{return selected_;}
};
struct Result {bool colored=false;double score_upper=c::infinity;};
inline Result bound(const original::FitIntervals& whole,const Partial& part,double threshold,
                    const original::MathContract& math,double weak_energy_scale=1){
    Result out;
    if(!part.valid()||!original::finite(whole)||!c::environment<double>()||!math.correctly_rounded_sqrt||
       !std::isfinite(math.log_error)||math.log_error<0||!std::isfinite(threshold)||threshold<0||
       !std::isfinite(weak_energy_scale)||weak_energy_scale<0||whole.count_lower!=whole.count||whole.count_upper!=whole.count||
       whole.count!=part.all().count()||part.count()>whole.count||part.selected_rank()>part.all().rank())return out;
    const auto rank=part.all().rank();const auto coherent=original::projection(whole);
    if(!coherent.valid||rank<=coherent.rank||rank<=2||rank>=whole.count||whole.energy.lo<=1e-30||
       part.projected().lo<=1e-30)return out;
    const auto r=Interval::point(static_cast<double>(rank-coherent.rank));
    const auto d=Interval::point(static_cast<double>(whole.count-rank));
    // Exact proven rank: preserve ORIGINAL expression association and outward
    // rounding. No unproved FP64 monotonic-rank simplification is used.
    const auto delta=c::times(c::plus(c::times(Interval::point(4),Interval::point(threshold)),
        Interval::point(32)),Interval::point(std::max(1.,weak_energy_scale)));
    const auto lower=c::minus(d,c::times(Interval::point(2),original::sqrt_interval(c::times(d,Interval::point(30)),math)));
    if(!lower.bounded()||lower.lo<=0)return out;
    const auto upper=c::plus(c::plus(c::plus(r,delta),c::times(Interval::point(2),
        original::sqrt_interval(c::times(c::plus(r,c::times(Interval::point(2),delta)),Interval::point(30)),math))),Interval::point(60));
    const auto ratio=c::over(c::over(upper,r),c::over(lower,d));if(!ratio.bounded())return out;
    const Interval limit{std::max(2.,ratio.lo),std::max(2.,ratio.hi)};
    // Original colored_excess clamps FULL projected sum to E; chip score does
    // NOT. Preserve these different denominators. Chronological subset sums
    // are lower bounds because inserting nonnegative RN additions is monotone.
    const auto pguard=std::max(0.,std::min(part.projected().lo,whole.energy.lo));
    const Interval projected{pguard,whole.energy.hi};const auto lack=c::minus(projected,coherent.energy);
    const Interval positive{std::max(0.,lack.lo),std::max(0.,lack.hi)};
    const auto residual=c::minus(whole.energy,projected);const auto left=c::times(positive,d);
    const auto right=c::times(c::times(limit,residual),r);
    if(!left.bounded()||!right.bounded()||!(residual.hi<=0||left.lo>right.hi))return out;
    out.colored=true;
    const auto explained=original::explained(whole);if(!explained.bounded())return out;
    constexpr double cap=1.-1e-15;
    const auto fraction=std::clamp(c::divide(explained.hi,part.projected().lo),0.,cap);
    // -log(1-f)<=f/(1-f); original libm error and final product remain owned.
    const auto log_upper=c::add(c::divide(fraction,c::down(1-fraction)),math.log_error);
    out.score_upper=c::mul(.5*static_cast<double>(rank-2),log_upper);
    return out;
}
} // namespace datapump::modem::detail::scheduled_partial_colored

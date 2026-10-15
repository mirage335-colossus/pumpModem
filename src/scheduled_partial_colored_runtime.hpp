#pragma once
// Rejection-only adapter for the existing per-source-owner colored statistic.
// No PCM dots, scores, admissions or absence are created by the all-owner pass.
#include "scheduled_partial_colored.hpp"
#include "scheduled_polynomial_chip_runtime.hpp"
#include <memory>

namespace datapump::modem::detail::scheduled_partial_colored_runtime {
namespace p=scheduled_partial_colored;
namespace c=scheduled_acquisition_certificate;
namespace color=scheduled_cold_colored;
namespace poly=scheduled_polynomial_chip;
namespace tile=scheduled_acquisition_tile;
using Complex=std::complex<double>;
using c::Interval;
struct ComplexInterval {Interval real,imag;};
inline ComplexInterval product(ComplexInterval a,ComplexInterval b){return {
    c::minus(c::times(a.real,b.real),c::times(a.imag,b.imag)),
    c::plus(c::times(a.real,b.imag),c::times(a.imag,b.real))};}
inline ComplexInterval point(Complex a){return {Interval::point(a.real()),Interval::point(a.imag())};}
inline ComplexInterval source_row(const color::SourceRows& s,std::size_t i,unsigned bit){
    const auto e=s.geometry[i].residual;
    return {Interval::disk(s.rows[i][bit].real(),e),Interval::disk(s.rows[i][bit].imag(),e)};
}
inline Interval norm_interval(Complex z){
    const auto q=c::plus(c::square(Interval::point(z.real())),c::square(Interval::point(z.imag())));
    if(!q.bounded()||q.hi<0)return {-c::infinity,c::infinity};
    return {q.lo<=0?0:c::down(std::sqrt(q.lo)),c::root(q.hi)};
}
// Reuses the established conservative Original-prefix/processing-carrier
// allowance, generalized to the actual (possibly partial) cell count. Template
// displacement is already enclosed row by row, not charged twice here.
inline double original_gram_radius(std::uint64_t count,double q,double carrier_radius,
        double carrier_magnitude,std::size_t prefix){
    if(count>1048576||!prefix||prefix>512||!std::isfinite(q)||q<0||q>32||
       !std::isfinite(carrier_radius)||carrier_radius<0||!std::isfinite(carrier_magnitude)||carrier_magnitude<0||carrier_magnitude>2)return c::infinity;
    const auto n=static_cast<std::size_t>(count)+2;
    const auto rho=c::add(c::gamma(128*n+128*prefix+128,std::numeric_limits<double>::epsilon()/2),
        c::mul(4*prefix,c::gamma(8*prefix+8,std::numeric_limits<double>::epsilon()/2)));
    const auto length=static_cast<double>(n),q2=c::mul(q,q);
    const auto rounded=c::mul(c::mul(32*rho,length),q2);
    const auto carrier=c::mul(c::mul(length,q2),c::add(c::mul(2*carrier_magnitude,carrier_radius),c::mul(carrier_radius,carrier_radius)));
    const auto tiny=c::mul(c::mul(128*length*(prefix+1)+128,std::numeric_limits<double>::denorm_min()),c::add(1,c::add(q,q2)));
    return c::add(c::mul(c::add(1,c::gamma(12*n+32,std::numeric_limits<double>::epsilon()/2)),c::add(rounded,carrier)),tiny);
}
struct Counters {
    std::uint64_t canonical_cells=0,direct_gram_cells=0,direct_gram_rows=0,zero_cells=0,
        rank_fallbacks=0,missing_fences=0,prefix_checks=0,stopped_after_owner=0;
};
// Each fence is the ACTUAL original carrier recurrence fence, with its actual
// stored seed. Provider blocks and processing virtual fences are not substitutes.
class CarrierFences {
public:
    struct Fence {std::uint64_t first=0;std::size_t count=0;Complex seed{};double minimum=0,maximum=0,adjacent_error=0;};
    static constexpr std::size_t maximum_fences=16384;
private:
    std::vector<Fence> fences_;Complex step_{};Interval step_norm_{};bool valid_=true;
public:
    explicit CarrierFences(Complex step):step_(step),step_norm_(norm_interval(step)){
        valid_=c::environment<double>()&&c::finite(step)&&step_norm_.bounded()&&step_norm_.lo>0&&step_norm_.hi<=2;
        if(valid_)fences_.reserve(maximum_fences); // one charged allocation; no old+new reallocation peak
    }
    static std::size_t reservation(){return sizeof(CarrierFences)+maximum_fences*sizeof(Fence)+128;}
    std::size_t working_bytes()const{return sizeof(*this)+fences_.capacity()*sizeof(Fence);}
    bool valid()const{return valid_;}Complex step()const{return step_;}
    double maximum()const{double value=0;for(const auto& f:fences_)value=std::max(value,f.maximum);return value;}
    bool add(std::uint64_t first,std::size_t count,Complex seed){
        if(!valid_)return false;
        if(!count||count>512||first>std::numeric_limits<std::uint64_t>::max()-count||!c::finite(seed))return valid_=false;
        if(!fences_.empty()&&first==fences_.back().first){if(count!=fences_.back().count||seed!=fences_.back().seed)return valid_=false;return true;}
        if((!fences_.empty()&&first<fences_.back().first+fences_.back().count)||fences_.size()==maximum_fences)return valid_=false;
        const auto seed_norm=norm_interval(seed);if(!seed_norm.bounded()||seed_norm.lo<=0)return valid_=false;
        // The norm bounds use the actual stored step and seed, not ideal trig.
        double lower=1,upper=1,growth=1;
        const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
        const auto one_step=c::add(1,c::gamma(8,u));
        for(std::size_t k=0;k<count;++k){lower=c::down(lower*std::min(1.,step_norm_.lo));upper=c::mul(upper,std::max(1.,step_norm_.hi));
            growth=c::mul(growth,std::max(1.,c::mul(one_step,step_norm_.hi)));}
        const auto relative=c::mul(c::gamma(16*count,u),c::mul(seed_norm.hi,upper));
        const auto underflow=c::mul(c::mul(8*eta,count),growth);
        const auto rho=c::add(relative,underflow);
        const auto minimum=std::max(0.,c::down(c::down(seed_norm.lo*lower)-rho));
        const auto maximum=c::add(c::mul(seed_norm.hi,upper),rho);
        const auto adjacent=c::add(c::mul(c::gamma(8,u),c::mul(maximum,step_norm_.hi)),8*eta);
        if(!(minimum>0)||!std::isfinite(maximum)||maximum>2||!std::isfinite(adjacent))return valid_=false;
        fences_.push_back({first,count,seed,minimum,maximum,adjacent});return true;
    }
    const Fence* find(std::uint64_t physical)const{
        if(!valid_)return nullptr;
        auto it=std::upper_bound(fences_.begin(),fences_.end(),physical,[](auto p,const auto& f){return p<f.first;});
        if(it==fences_.begin())return nullptr;--it;return physical-it->first<it->count?&*it:nullptr;
    }
    bool covers(std::uint64_t first,std::uint64_t end)const{
        if(first>end)return false;
        while(first<end){const auto* f=find(first);if(!f)return false;first=std::min(end,f->first+f->count);}return true;
    }
    const Fence* adjacent(std::uint64_t first)const{
        const auto* f=find(first);return f&&first<f->first+f->count-1?f:nullptr;
    }
    std::optional<Complex> oscillator(std::uint64_t physical)const{
        const auto* f=find(physical);if(!f)return {};auto o=f->seed;
        for(auto i=f->first;i<physical;++i)o*=step_;
        if(!c::finite(o)||c::norm(o)>2)return {};return o;
    }
};

// The expensive source-only witness selection is cached with the actual
// immutable SourceModels. It contains no PCM, physical phase or candidate axes.
class SourceWitnesses {
public:
    struct Option {std::size_t first=0;double lower=0;};
    struct Cell {std::array<std::array<Option,3>,2> option{};std::array<double,2> qmax{};};
private:
    const poly::SourceModels* model_identity_=nullptr;const color::SourceRows* source_identity_=nullptr;
    color::SourceRows::Key key_{};Complex step_{};long double delta_=0;
    std::uint64_t revision_=0;unsigned mode_=0;std::size_t row_count_=0;bool valid_=false;
    std::vector<Cell> cells_;
    template<class S> static std::uint64_t revision(const S& s){if constexpr(requires{s.geometry_revision;})return s.geometry_revision;else return 0;}
    template<class S> static unsigned mode(const S& s){if constexpr(requires{s.geometry_mode;})return static_cast<unsigned>(s.geometry_mode);else return 0;}
public:
    SourceWitnesses(const color::SourceRows& source,const poly::SourceModels& models,Complex step,std::stop_token stop={})
      :model_identity_(&models),source_identity_(&source),key_(source.key),step_(step),delta_(source.geometry_delta),
       revision_(revision(source)),mode_(mode(source)),row_count_(source.rows.size()){
        if(!models.matches(source)||!c::environment<double>()||!c::finite(step))return;
        cells_.resize(models.cells.size());
        for(std::size_t k=0;k<models.cells.size();++k){tile::check_stop(stop);const auto& cell=models.cells[k];auto& out=cells_[k];
            for(auto i=cell.first;i<cell.end;++i)if(source.mask[i]&&source.geometry[i].owner==cell.owner){
                if(source.geometry[i].membership||!std::isfinite(source.geometry[i].residual)||source.geometry[i].residual<0)return;
                for(unsigned bit=0;bit<2;++bit)out.qmax[bit]=std::max(out.qmax[bit],c::add(source.norms[i][bit],source.geometry[i].residual));
            }
            if(!cell.full)continue;
            for(unsigned bit=0;bit<2;++bit){std::vector<Option> candidates;candidates.reserve(33);
                for(auto i=cell.first;i+1<cell.end;++i){const auto a=source_row(source,i,bit),b=product(source_row(source,i+1,bit),point(step));
                    const auto cc=c::plus(c::square(a.real),c::square(b.real)),ss=c::plus(c::square(a.imag),c::square(b.imag));
                    const auto cs=c::plus(c::times(a.real,a.imag),c::times(b.real,b.imag));
                    const auto trace=c::plus(cc,ss),det=c::minus(c::times(cc,ss),c::square(cs));
                    if(trace.bounded()&&trace.hi>0&&det.bounded()&&det.lo>0)candidates.push_back({i,c::down(det.lo/trace.hi)});
                }
                std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.lower>b.lower;});
                std::size_t used=0;for(const auto& candidate:candidates){bool separate=true;
                    for(std::size_t t=0;t<used;++t)if(candidate.first+1>=out.option[bit][t].first&&out.option[bit][t].first+1>=candidate.first)separate=false;
                    if(separate)out.option[bit][used++]=candidate;if(used==3)break;}
            }
        }
        valid_=true;
    }
    ~SourceWitnesses(){if(!cells_.empty())OPENSSL_cleanse(cells_.data(),cells_.size()*sizeof(Cell));}
    bool matches(const color::SourceRows& source,const poly::SourceModels& models,Complex step)const{return valid_&&models.matches(source)&&
        model_identity_==&models&&source_identity_==&source&&key_==source.key&&step_==step&&delta_==source.geometry_delta&&
        revision_==revision(source)&&mode_==mode(source)&&row_count_==source.rows.size();}
    const Cell& cell(std::size_t k)const{return cells_.at(k);}
    static std::size_t reservation(std::uint64_t symbol_samples){if(!symbol_samples||symbol_samples>color::SourceRows::maximum_rows)return std::numeric_limits<std::size_t>::max();return tile::add_size(sizeof(SourceWitnesses)+128,
        tile::multiply_size(2*static_cast<std::size_t>(symbol_samples/34+2),sizeof(Cell)));}
    std::size_t working_bytes()const{return sizeof(*this)+cells_.capacity()*sizeof(Cell);}
};
struct Request {
    std::array<color::FitIntervals,2> whole{};
    color::MathContract math{};
    std::uint64_t identity=0,symbol_samples=0,observed_begin=0,observed_end=0;
    // Raw recurrence metadata is not PCM retention/presence. The caller must
    // separately supply the actual immutable retained capture extent.
    std::uint64_t available_begin=0,available_end=0;
    long double physical_translation=0; // actual physical = source physical + this
    double threshold=c::infinity,carrier_radius=c::infinity,carrier_magnitude=2;
    std::size_t original_prefix=0,proof_byte_limit=0;
    bool finite_original_float_pcm=false;
};
struct Reply {
    bool preflight_supported=false,rejected=false;
    std::array<double,2> upper{c::infinity,c::infinity};
    Counters counters{};
};
struct Proofs {std::array<std::unique_ptr<p::AllCells>,2> all;Counters counters;};
inline Proofs preflight(const color::SourceRows& source,const poly::SourceModels& models,
        const SourceWitnesses& witnesses,const CarrierFences& carrier,const Request& request,std::stop_token stop={}){
    Proofs out;
    if(!witnesses.matches(source,models,carrier.step())||!carrier.valid()||!request.finite_original_float_pcm||
       !request.math.correctly_rounded_sqrt||!c::environment<double>()||!request.symbol_samples||
       request.symbol_samples>color::SourceRows::maximum_rows||request.observed_begin>request.observed_end||
       request.available_begin>request.available_end||
       !std::isfinite(request.physical_translation)||std::trunc(request.physical_translation)!=request.physical_translation||
       !std::isfinite(request.threshold)||request.threshold<0||!std::isfinite(request.carrier_radius)||request.carrier_radius<0||
       !std::isfinite(request.carrier_magnitude)||request.carrier_magnitude<carrier.maximum()||request.carrier_magnitude>2||
       !color::finite(request.whole[0])||!color::finite(request.whole[1])||
       request.whole[0].count!=request.whole[1].count)return out;
    const auto owners=static_cast<std::size_t>((request.symbol_samples+33)/34);
    double maximum_q=0;for(std::size_t i=0;i<models.cells.size();++i)for(auto q:witnesses.cell(i).qmax)maximum_q=std::max(maximum_q,q);
    if(maximum_q>32||!std::isfinite(maximum_q))return out;
    const p::FiniteProjectionDomain domain{std::max(maximum_q,std::numeric_limits<double>::min()),2,request.original_prefix,true};
    p::NestingBuilder first(request.whole[0].count,owners,request.proof_byte_limit,request.identity,domain);
    p::NestingBuilder second(request.whole[1].count,owners,request.proof_byte_limit,request.identity,domain);
    std::array<p::NestingBuilder*,2> builders{&first,&second};
    std::size_t cursor=0;
    for(std::size_t owner=0;owner<owners;++owner){tile::check_stop(stop);
        if(cursor==models.cells.size()||models.cells[cursor].owner>owner){for(auto* b:builders)b->gram(owner,0,{},true);++out.counters.zero_cells;continue;}
        const auto& cell=models.cells[cursor];const auto& cached=witnesses.cell(cursor++);
        if(cell.owner!=owner)return {};
        const auto real=static_cast<long double>(cell.physical)+request.physical_translation;
        const bool complete=cell.full&&real>=request.observed_begin&&real+34<=request.observed_end&&
            real>=request.available_begin&&real+34<=request.available_end&&real>=0&&
            real+34<0x1p52L&&std::trunc(real)==real&&
            carrier.covers(static_cast<std::uint64_t>(real),static_cast<std::uint64_t>(real)+34);
        std::array<bool,2> proven{};
        if(complete){const auto physical=static_cast<std::uint64_t>(real);
            for(unsigned bit=0;bit<2;++bit){const auto q=cached.qmax[bit],trace=c::mul(34,c::mul(c::mul(q,q),4));
                const auto entry=original_gram_radius(34,q,request.carrier_radius,request.carrier_magnitude,request.original_prefix);
                for(const auto& option:cached.option[bit])if(option.lower>0){const auto p0=physical+option.first-cell.first;
                    const auto* f=carrier.adjacent(p0);if(!f)continue;
                    const auto e=c::mul(q,f->adjacent_error),v=c::mul(c::mul(q,f->maximum),c::norm(carrier.step()));
                    const auto loss=c::add(c::mul(c::mul(2,v),e),c::mul(e,e));
                    const auto lower=c::down(c::down(c::down(f->minimum*f->minimum)*option.lower)-loss);
                    if(p::rank2_eigen_lower(34,lower,trace,entry)){builders[bit]->eigen(owner,34,lower,trace,entry);proven[bit]=true;break;}
                }
            }
            if(proven[0]&&proven[1]){++out.counters.canonical_cells;continue;}
        }
        // Only failed/cropped/ramped/split cells pay this Gram-only scan. It
        // joins every physical fragment before deciding the owner's rank.
        ++out.counters.direct_gram_cells;std::array<p::Gram,2> gram{};std::array<bool,2> zero{true,true};std::uint64_t count=0;
        for(std::size_t run_index=cell.run;run_index<source.runs.size();++run_index){const auto& run=source.runs[run_index];if(run.first>=cell.end)break;
            const auto begin=std::max(cell.first,run.first),end=std::min(cell.end,run.first+run.count);
            for(auto i=begin;i<end;++i){if((i&255U)==0)tile::check_stop(stop);
                if(!source.mask[i]||source.geometry[i].owner!=owner)continue;
                const auto raw=static_cast<long double>(run.physical_offset)+(i-run.first)+request.physical_translation;
                if(raw<request.observed_begin||raw>=request.observed_end)continue;
                if(raw<request.available_begin||raw>=request.available_end){++out.counters.missing_fences;return out;}
                if(raw<0||raw>=0x1p52L||std::trunc(raw)!=raw)return {};
                const auto o=carrier.oscillator(static_cast<std::uint64_t>(raw));if(!o){++out.counters.missing_fences;return out;}
                ++count;++out.counters.direct_gram_rows;
                for(unsigned bit=0;bit<2;++bit)if(!proven[bit]){const auto q=source_row(source,i,bit);const auto v=product(q,point(*o));
                    gram[bit].cc=c::plus(gram[bit].cc,c::square(v.real));gram[bit].ss=c::plus(gram[bit].ss,c::square(v.imag));
                    gram[bit].cs=c::plus(gram[bit].cs,c::times(v.real,v.imag));
                    zero[bit]=zero[bit]&&source.rows[i][bit]==Complex{}&&source.geometry[i].residual==0;
                }
            }
        }
        for(unsigned bit=0;bit<2;++bit)if(!proven[bit]){const auto e=original_gram_radius(count,cached.qmax[bit],request.carrier_radius,request.carrier_magnitude,request.original_prefix);
            if(!std::isfinite(e))return {};
            if(count&&!zero[bit]){gram[bit].cc=c::plus(gram[bit].cc,{-e,e});gram[bit].ss=c::plus(gram[bit].ss,{-e,e});gram[bit].cs=c::plus(gram[bit].cs,{-e,e});}
            else gram[bit]={};
            if(!p::rank(count,gram[bit],zero[bit]))++out.counters.rank_fallbacks;
            builders[bit]->gram(owner,count,gram[bit],zero[bit]);
        }
    }
    if(cursor!=models.cells.size())return {};
    out.all[0]=std::make_unique<p::AllCells>(first.finish());out.all[1]=std::make_unique<p::AllCells>(second.finish());
    if(!out.all[0]->valid()||!out.all[1]->valid()){out.all={};return out;}return out;
}
// Borrowed SourceWitnesses/cache lifetime is outside this transient session.
// Request identity is event-specific; only current charged event may stop.
class Session {
    Request request_;Proofs proofs_;std::array<std::unique_ptr<p::Partial>,2> partial_;Reply* reply_=nullptr;
public:
    Session(const color::SourceRows& source,const poly::SourceModels& models,const SourceWitnesses& witnesses,
        const CarrierFences& carrier,Request request,Reply& reply,std::stop_token stop={})
        :request_(std::move(request)),proofs_(preflight(source,models,witnesses,carrier,request_,stop)),reply_(&reply){
        reply={};reply.counters=proofs_.counters;
        if(proofs_.all[0]&&proofs_.all[1]){for(unsigned bit=0;bit<2;++bit)partial_[bit]=std::make_unique<p::Partial>(*proofs_.all[bit],request_.identity);reply.preflight_supported=true;}
    }
    bool supported()const{return reply_&&reply_->preflight_supported;}
    bool completed_prefix(std::uint64_t owner,const std::array<color::Summary,2>& actual_prefix){
        if(!supported()||reply_->rejected)return reply_&&reply_->rejected;
        ++reply_->counters.prefix_checks;
        for(unsigned bit=0;bit<2;++bit){partial_[bit]->certified_prefix(owner,actual_prefix[bit]);
            reply_->upper[bit]=p::bound(request_.whole[bit],*partial_[bit],request_.threshold,request_.math).score_upper;}
        if(reply_->upper[0]<request_.threshold&&reply_->upper[1]<request_.threshold){reply_->rejected=true;reply_->counters.stopped_after_owner=owner;return true;}return false;
    }
    static std::size_t transient_reservation(std::uint64_t symbol_samples){
        const auto owners=static_cast<std::size_t>((symbol_samples+33)/34);
        return tile::add_size(CarrierFences::reservation()+sizeof(Session)+8192,tile::multiply_size(2*owners,64));
    }
};
} // namespace datapump::modem::detail::scheduled_partial_colored_runtime

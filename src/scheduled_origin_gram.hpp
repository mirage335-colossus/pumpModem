#pragma once
// PCM-independent, physical-origin Gram reuse. This stores requested prefix
// checkpoints, NOT whole-symbol PCM or one shifted input matrix. Every exponent
// is a physical offset; source-time compaction never pauses the carrier.
#include "scheduled_acquisition_certificate.hpp"
#include "scheduled_acquisition_tile.hpp"
#include "scheduled_origin_gram_contract.hpp"
#include <openssl/crypto.h>
#include <optional>
#include <cstdlib>
#include <string_view>

namespace datapump::modem::detail::scheduled_origin_gram {
namespace c=scheduled_acquisition_certificate;
namespace tile=scheduled_acquisition_tile;
using Complex=std::complex<double>;using c::Interval;
struct Ball {Complex center{};double radius=0;bool valid=true;};
inline double upper(Ball z){return z.valid?c::add(c::norm(z.center),z.radius):c::infinity;}

inline Ball product(Ball a,Ball b){
    if(!a.valid||!b.valid||!c::finite(a.center)||!c::finite(b.center)||a.radius<0||b.radius<0)return {{},0,false};
    const auto center=a.center*b.center;
    const auto radius=c::add(c::complex_preproduct_radius(a.center,b.center),c::add(
        c::add(c::mul(c::norm(a.center),b.radius),c::mul(c::norm(b.center),a.radius)),c::mul(a.radius,b.radius)));
    return {center,radius,c::finite(center)&&std::isfinite(radius)};
}
inline Ball inverse_point(Complex z){
    if(!c::finite(z))return {{},0,false};
    const auto den=c::plus(c::square(Interval::point(z.real())),c::square(Interval::point(z.imag())));
    if(!den.bounded()||den.lo<=0)return {{},0,false};
    const auto re=c::over(Interval::point(z.real()),den),im=c::over(Interval::point(-z.imag()),den);
    if(!re.bounded()||!im.bounded())return {{},0,false};
    const Complex center{re.lo+.5*(re.hi-re.lo),im.lo+.5*(im.hi-im.lo)};
    const auto er=std::max(std::abs(c::down(re.lo-center.real())),std::abs(c::up(re.hi-center.real())));
    const auto ei=std::max(std::abs(c::down(im.lo-center.imag())),std::abs(c::up(im.hi-center.imag())));
    const auto radius=c::root(c::add(c::mul(er,er),c::mul(ei,ei)));
    return {center,radius,c::finite(center)&&std::isfinite(radius)};
}
inline double separation(Complex point,Ball enclosure){
    if(!c::finite(point)||!enclosure.valid)return c::infinity;
    const auto re=c::minus(Interval::point(point.real()),Interval::point(enclosure.center.real()));
    const auto im=c::minus(Interval::point(point.imag()),Interval::point(enclosure.center.imag()));
    const auto er=std::max(std::abs(re.lo),std::abs(re.hi)),ei=std::max(std::abs(im.lo),std::abs(im.hi));
    return c::add(c::root(c::add(c::mul(er,er),c::mul(ei,ei))),enclosure.radius);
}
inline Interval real(Ball z){return z.valid?Interval::disk(z.center.real(),z.radius):Interval{-c::infinity,c::infinity};}
inline Interval imag(Ball z){return z.valid?Interval::disk(z.center.imag(),z.radius):Interval{-c::infinity,c::infinity};}
inline Interval norm_square(Ball z){return c::plus(c::square(real(z)),c::square(imag(z)));}
// Exact stored-step powers, with numerical error disks. No ideal trig premise.
class Powers {
    std::array<Ball,41> forward_{},backward_{};Complex step_{};bool valid_=false;
public:
    static constexpr std::int64_t maximum_exponent=std::int64_t{1}<<40;
    explicit Powers(Complex step):step_(step){
        if(!c::environment<double>()||!c::finite(step)||c::norm(step)>2)return;
        forward_[0]={step,0,true};backward_[0]=inverse_point(step);
        if(!backward_[0].valid)return;
        for(std::size_t i=1;i<forward_.size();++i){forward_[i]=product(forward_[i-1],forward_[i-1]);
            backward_[i]=product(backward_[i-1],backward_[i-1]);}
        valid_=true;
    }
    bool valid()const{return valid_;}Complex step()const{return step_;}
    Ball at(std::int64_t exponent)const{
        if(!valid_||!c::environment<double>()||exponent>maximum_exponent||exponent<-maximum_exponent)return {{},0,false};
        const auto& powers=exponent<0?backward_:forward_;
        auto n=static_cast<std::uint64_t>(exponent<0?-exponent:exponent);Ball value{{1,0},0,true};
        for(std::size_t i=0;n;++i,n>>=1)if(n&1)value=product(value,powers[i]);
        return value;
    }
};

// Actual raw-seed/fence evidence for ONE immutable captured revision. R0 is
// an actual stored seed at n0. Later libm-produced seeds are measured against
// R0*w^(f-n0); no libm accuracy is assumed. The bound is absolute, avoiding a
// normalization by nearly unit powers. A source key/template cache never owns
// this capture-dependent object.
class RawReference {
    const Powers* powers_;std::uint64_t generation_=0,revision_=0,first_=0,last_=0;
    Complex seed_{},last_seed_{};double radius_=0,maximum_=0;bool valid_=true,initialized_=false;
    std::uint64_t last_first_=0;std::size_t last_count_=0;
    struct Coverage {std::uint64_t first=0,end=0;};
    std::array<Coverage,4096> coverage_{};std::size_t coverage_count_=0;
public:
    RawReference(const Powers& p,std::uint64_t generation):powers_(&p),generation_(generation),valid_(p.valid()){}
    bool append(std::uint64_t first,std::size_t count,Complex actual_seed){
        if(!valid_||!c::environment<double>()||revision_==std::numeric_limits<std::uint64_t>::max()||!count||count>512||!c::finite(actual_seed)||first>std::numeric_limits<std::uint64_t>::max()-count)return valid_=false;
        if(initialized_&&first==last_first_){
            if(count==last_count_&&actual_seed==last_seed_)return true;
            return valid_=false;
        }
        if(!initialized_){first_=first;seed_=actual_seed;initialized_=true;}
        if(first<last_||first<first_||first-first_>static_cast<std::uint64_t>(Powers::maximum_exponent)-count)return valid_=false;
        const auto offset=static_cast<std::int64_t>(first-first_);
        const auto predicted=product({seed_,0,true},powers_->at(offset));
        const auto defect=separation(actual_seed,predicted);
        const auto grow=std::max(1.,upper(powers_->at(static_cast<std::int64_t>(count))));
        const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
        const auto seednorm=c::norm(actual_seed),w=c::norm(powers_->step());
        const auto one_step=c::add(1,c::gamma(8,u));double underflow_growth=1;
        for(std::size_t k=0;k<count;++k)underflow_growth=c::mul(underflow_growth,std::max(1.,c::mul(one_step,w)));
        const auto recurrence=c::add(c::mul(c::gamma(16*count,u),c::mul(seednorm,grow)),c::mul(c::mul(8*eta,count),underflow_growth));
        radius_=std::max(radius_,c::add(c::mul(defect,grow),recurrence));
        maximum_=std::max(maximum_,c::add(c::mul(seednorm,grow),recurrence));
        last_first_=first;last_count_=count;last_seed_=actual_seed;last_=first+count;
        if(coverage_count_&&coverage_[coverage_count_-1].end==first)coverage_[coverage_count_-1].end=last_;
        else{if(coverage_count_==coverage_.size())return valid_=false;coverage_[coverage_count_++]={first,last_};}
        ++revision_;
        return valid_=predicted.valid&&std::isfinite(radius_)&&std::isfinite(maximum_)&&maximum_<=2;
    }
    bool valid()const{return valid_&&initialized_&&c::environment<double>();}
    std::size_t working_bytes()const{return sizeof(*this);}
    bool covers(std::uint64_t first,std::uint64_t end)const{
        if(!valid()||first>end)return false;if(first==end)return true;
        const auto begin=coverage_.begin(),finish=begin+coverage_count_;
        auto it=std::upper_bound(begin,finish,first,[](auto p,const auto& interval){return p<interval.first;});
        if(it==begin)return false;--it;return first>=it->first&&end<=it->end;
    }
    std::uint64_t generation()const{return generation_;}std::uint64_t revision()const{return revision_;}
    std::uint64_t first()const{return first_;}std::uint64_t end()const{return last_;}
    double radius()const{return radius_;}double maximum()const{return maximum_;}
    Complex step()const{return powers_->step();}
    Ball factor(std::int64_t origin)const{
        if(!valid()||origin<-Powers::maximum_exponent||origin>Powers::maximum_exponent||
           first_>static_cast<std::uint64_t>(Powers::maximum_exponent))return {{},0,false};
        const auto exponent=origin-static_cast<std::int64_t>(first_);
        return product({seed_,0,true},powers_->at(exponent));
    }
};

enum class Build { interval_reference, native_compensated };
inline Build requested_build(){const auto* text=std::getenv("DATAPUMP_SCHED_ORIGIN_GRAM_BULK");
    return text && std::string_view(text)=="1"?Build::native_compensated:Build::interval_reference;}

// Variants=3 supports the existing A/B/D endpoint+difference formulation:
// All variants MUST share the same EXACT selected mask, not merely count.
// Evaluate each independent row family, then let its existing certified
// combine operator form the fractional target. No new quadratic approximation.
template<std::size_t Variants=1> class Prefix {
    static_assert(Variants>=1&&Variants<=3);
public:
    using Pair=std::array<Complex,2>;using Row=std::array<Pair,Variants>;
    struct Field {Interval image_real,image_imag,trace,square;std::uint64_t nonzero=0;};
    using Fields=std::array<std::array<Field,2>,Variants>;
    struct Checkpoint {Fields fields;std::uint64_t selected=0;};
    using Output=std::array<std::array<Gram,2>,Variants>;
private:
    const Powers* powers_;std::uint64_t identity_=0,last_end_=0;std::size_t next_=0,limit_=0;
    std::vector<std::uint64_t> points_;std::vector<Checkpoint> checkpoints_;Checkpoint running_;
    struct Run {std::uint64_t first=0,end=0;};std::vector<Run> selected_runs_;std::size_t run_limit_=0;
    bool valid_=true,finished_=false;Build build_=Build::interval_reference;
    struct NativeSum {
        double sum=0,correction=0,mass=0,error=0;
        void add(double x)noexcept{const auto next=sum+x;
            correction+=std::abs(sum)>=std::abs(x)?(sum-next)+x:(x-next)+sum;sum=next;}
        Interval enclosure(std::uint64_t count)const {
            if(count>std::numeric_limits<std::size_t>::max()-8 || !std::isfinite(sum)||!std::isfinite(correction)||
               !std::isfinite(mass)||mass<0||!std::isfinite(error)||error<0)return {-c::infinity,c::infinity};
            const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
            const auto gamma=c::gamma(static_cast<std::size_t>(count)+8,u);
            const auto underflow=c::divide(c::mul(eta,static_cast<double>(count)+8),c::down(1-gamma));
            // We do NOT rely on a compensated-summation theorem. The ordinary
            // sum has the elementary gamma(N) absolute error; shifting its
            // center by the ACTUAL correction is charged in full.
            const auto center=sum+correction;
            const auto radius=c::add(error,c::add(c::add(c::mul(gamma,mass),underflow),
                c::add(std::abs(correction),c::ulp(center))));
            return Interval::disk(center,radius);
        }
    };
    using NativeField=std::array<NativeSum,4>;
    std::array<std::array<NativeField,2>,Variants> native_{};
    std::uint64_t native_rows_=0,native_batches_=0,native_null_batches_=0;
    static double mass_upper(double native,std::size_t count) {
        if(!std::isfinite(native)||native<0)return c::infinity;
        const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
        const auto gamma=c::gamma(count+8,u);
        return c::divide(c::add(native,c::mul(8*eta,static_cast<double>(count))),c::down(1-gamma));
    }
    void flush(std::uint64_t position){
        if(build_==Build::native_compensated && !c::environment<double>()){valid_=false;return;}
        while(next_<points_.size()&&points_[next_]<=position){auto value=running_;
            if(build_==Build::native_compensated)for(std::size_t variant=0;variant<Variants;++variant)for(unsigned bit=0;bit<2;++bit){
                auto& f=value.fields[variant][bit];const auto& n=native_[variant][bit];
                f.image_real=n[0].enclosure(running_.selected);f.image_imag=n[1].enclosure(running_.selected);
                f.trace=n[2].enclosure(running_.selected);f.square=n[3].enclosure(running_.selected);
                if(!f.image_real.bounded()||!f.image_imag.bounded()||!f.trace.bounded()||!f.square.bounded())valid_=false;
            }
            checkpoints_[next_++]=value;
        }
    }
    void append_native(std::uint64_t offset,std::span<const Row> rows,std::span<const std::uint8_t> mask,std::stop_token stop) {
        struct Mass {double q1=0,q2=0,z2=0;};
        const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
        const auto g8=c::gamma(8,u),g4=c::gamma(4,u);
        const auto fast_q_square=c::down(c::down(128.*128.*c::down(1-g4))-8*eta);
        std::size_t first=0;
        while(first<rows.size()) {
            tile::check_stop(stop);const auto physical=offset+first;
            auto count=std::min<std::size_t>(64,rows.size()-first);
            if(next_<points_.size()&&points_[next_]>physical)count=std::min<std::uint64_t>(count,points_[next_]-physical);
            if(!mask.empty()&&std::find_if(mask.begin()+first,mask.begin()+first+count,
                    [](std::uint8_t value){return value!=0;})==mask.begin()+first+count) {
                // Exact candidate mask, never global capture omission. The
                // next represented carrier starts at its ABSOLUTE exponent.
                ++native_batches_;++native_null_batches_;first+=count;last_end_=offset+first;flush(last_end_);continue;
            }
            const auto seed=powers_->at(static_cast<std::int64_t>(physical));
            const auto grow=std::max(1.,upper(powers_->at(static_cast<std::int64_t>(count))));
            const auto g=c::gamma(16*count,u),seednorm=c::norm(seed.center);
            const auto recurrence=c::add(c::mul(g,c::mul(seednorm,grow)),
                c::mul(c::mul(8*eta,static_cast<double>(count)),c::mul(grow,c::add(1,g))));
            const auto rho=c::add(c::mul(seed.radius,grow),recurrence);
            const auto maximum=c::add(c::mul(seednorm,grow),recurrence);
            if(!seed.valid||!std::isfinite(rho)||!std::isfinite(maximum))throw Error("native Gram canonical recurrence unavailable");
            auto oscillator=seed.center;std::array<std::array<Mass,2>,Variants> mass{};std::size_t selected=0;
            for(std::size_t j=0;j<count;++j) {
                const auto i=first+j;
                if(!mask.empty()&&mask[i]>1)throw Error("invalid native Gram mask");
                if(mask.empty()||mask[i]) {
                    if(running_.selected==0x1fffffffffffffULL)throw Error("native Gram count exceeds exact domain");
                    ++running_.selected;++selected;++native_rows_;
                    const auto at=offset+i;
                    if(!selected_runs_.empty()&&selected_runs_.back().end==at)++selected_runs_.back().end;
                    else {if(selected_runs_.size()==run_limit_)throw Error("native Gram selected run bound exceeded");selected_runs_.push_back({at,at+1});}
                    for(std::size_t variant=0;variant<Variants;++variant)for(unsigned bit=0;bit<2;++bit) {
                        const auto q=rows[i][variant][bit];if(!c::finite(q))throw Error("native Gram source is nonfinite");
                        const auto square=std::norm(q);
                        // Cheap interior domain proof; retain the original
                        // outward norm check at the only uncertain boundary.
                        if(square>fast_q_square && c::norm(q)>128)throw Error("native Gram source outside bounded domain");
                        const auto z=q*oscillator,image=z*z;const auto trace=std::norm(z);
                        if(!c::finite(z)||!c::finite(image)||!std::isfinite(trace)||!std::isfinite(square))throw Error("native Gram product overflow");
                        auto& n=native_[variant][bit];n[0].add(image.real());n[1].add(image.imag());n[2].add(trace);n[3].add(square);
                        if(q!=Complex{})++running_.fields[variant][bit].nonzero;
                        const auto q1=std::abs(q.real())+std::abs(q.imag()),z1=std::abs(z.real())+std::abs(z.imag());
                        auto& m=mass[variant][bit];m.q1+=q1;m.q2+=q1*q1;m.z2+=z1*z1;
                    }
                }
                oscillator*=powers_->step();
            }
            if(selected)for(std::size_t variant=0;variant<Variants;++variant)for(unsigned bit=0;bit<2;++bit) {
                const auto& m=mass[variant][bit];const auto Q1=mass_upper(m.q1,selected),Q2=mass_upper(m.q2,selected),Z2=mass_upper(m.z2,selected);
                // |RN(q*O)-q*w^v| <= a*|q|_1 +8eta. Cauchy then
                // encloses BOTH the image and trace construction differences.
                const auto a=c::add(rho,c::mul(g8,c::mul(2,maximum)));
                const auto e2=c::add(c::mul(c::mul(a,a),Q2),c::add(c::mul(c::mul(16*eta,a),Q1),c::mul(c::mul(64*eta,eta),static_cast<double>(selected))));
                const auto carrier=c::add(c::mul(2,c::root(c::mul(Z2,e2))),e2);
                const auto product_error=c::add(c::mul(g8,Z2),c::mul(16*eta,static_cast<double>(selected)));
                const auto construction=c::add(carrier,product_error);
                const auto native_mass=c::add(c::mul(c::add(1,g8),Z2),c::mul(16*eta,static_cast<double>(selected)));
                const auto square_error=c::add(c::mul(g4,Q2),c::mul(8*eta,static_cast<double>(selected)));
                const auto square_mass=c::add(Q2,square_error);
                auto& n=native_[variant][bit];for(unsigned field=0;field<4;++field) {
                    n[field].mass=c::add(n[field].mass,field==3?square_mass:native_mass);
                    n[field].error=c::add(n[field].error,field==3?square_error:construction);
                    if(!std::isfinite(n[field].mass)||!std::isfinite(n[field].error)||!std::isfinite(n[field].sum)||!std::isfinite(n[field].correction))
                        throw Error("native Gram aggregate certificate overflow");
                }
            }
            ++native_batches_;first+=count;last_end_=offset+first;flush(last_end_);
        }
    }
public:
    static constexpr std::size_t maximum_checkpoints=2*tile::maximum_start_span+2;
    static std::size_t reservation(std::size_t checkpoints,std::size_t runs=4096){if(!checkpoints||checkpoints>maximum_checkpoints||!runs||runs>65536)return SIZE_MAX;
        return tile::add_size(tile::add_size(sizeof(Prefix)+8192,tile::multiply_size(2*runs,sizeof(Run))),tile::multiply_size(2*checkpoints,sizeof(std::uint64_t)+sizeof(Checkpoint)));}
    Prefix(const Powers& powers,std::uint64_t template_identity,std::span<const std::uint64_t> checkpoints,std::size_t byte_limit,std::size_t run_limit=4096,Build construction=Build::interval_reference)
        :powers_(&powers),identity_(template_identity),limit_(byte_limit),run_limit_(run_limit),build_(construction){
        if(build_!=Build::interval_reference&&build_!=Build::native_compensated)throw Error("invalid Gram prefix construction");
        if(!powers.valid()||checkpoints.empty()||checkpoints.size()>maximum_checkpoints||reservation(checkpoints.size(),run_limit)>byte_limit)
            throw Error("origin Gram prefix workspace or power contract unavailable");
        for(std::size_t i=0;i<checkpoints.size();++i)if(checkpoints[i]>static_cast<std::uint64_t>(Powers::maximum_exponent)||
            (i&&checkpoints[i]<=checkpoints[i-1]))throw Error("origin Gram checkpoints must be actual ordered unique physical offsets");
        points_.assign(checkpoints.begin(),checkpoints.end());checkpoints_.resize(checkpoints.size());selected_runs_.reserve(run_limit_);
        if(working_bytes()>limit_)throw Error("origin Gram prefix capacities exceeded workspace");
    }
    ~Prefix(){if(!checkpoints_.empty())OPENSSL_cleanse(checkpoints_.data(),checkpoints_.size()*sizeof(Checkpoint));OPENSSL_cleanse(&running_,sizeof(running_));OPENSSL_cleanse(&native_,sizeof(native_));}
    std::size_t working_bytes()const{return sizeof(*this)+points_.capacity()*sizeof(std::uint64_t)+checkpoints_.capacity()*sizeof(Checkpoint)+selected_runs_.capacity()*sizeof(Run);}
    std::uint64_t identity()const{return identity_;}bool valid()const{return valid_&&finished_;}
    void invalidate()noexcept{valid_=false;}
    Build construction()const noexcept{return build_;}
    std::uint64_t native_rows()const noexcept{return native_rows_;}
    std::uint64_t interval_rows()const noexcept{return build_==Build::interval_reference?running_.selected:0;}
    std::uint64_t native_batches()const noexcept{return native_batches_;}
    std::uint64_t native_null_batches()const noexcept{return native_null_batches_;}
    void append(std::uint64_t physical_offset,std::span<const Row> rows,std::span<const std::uint8_t> mask={},std::stop_token stop={}){
        bool committed=false;struct FailClosed {bool& valid;bool& committed;~FailClosed(){if(!committed)valid=false;}} failed{valid_,committed};
        if(finished_||!valid_||!c::environment<double>()||physical_offset<last_end_||physical_offset>static_cast<std::uint64_t>(Powers::maximum_exponent)||rows.size()>static_cast<std::uint64_t>(Powers::maximum_exponent)-physical_offset||
           (!mask.empty()&&mask.size()!=rows.size()))throw Error("origin Gram physical fragments reverse or exceed bound");
        tile::check_stop(stop);flush(physical_offset);last_end_=physical_offset;
        if(build_==Build::native_compensated){append_native(physical_offset,rows,mask,stop);committed=valid_;return;}
        auto oscillator=powers_->at(static_cast<std::int64_t>(physical_offset));
        if(!oscillator.valid){valid_=false;return;}
        for(std::size_t i=0;i<rows.size();++i){if((i&255U)==0)tile::check_stop(stop);
            if(!mask.empty()&&mask[i]>1)throw Error("invalid origin Gram template mask");
            if(mask.empty()||mask[i]){
                if(running_.selected==0x1fffffffffffffULL)throw Error("origin Gram count exceeds exact domain");
                ++running_.selected; // q==0 is still an observed noise degree.
                const auto physical=physical_offset+i;
                if(!selected_runs_.empty()&&selected_runs_.back().end==physical)++selected_runs_.back().end;
                else{if(selected_runs_.size()==run_limit_)throw Error("origin Gram selected-mask run limit exceeded");selected_runs_.push_back({physical,physical+1});}
                for(std::size_t variant=0;variant<Variants;++variant)for(unsigned bit=0;bit<2;++bit){
                    const auto q=rows[i][variant][bit];if(!c::finite(q)||c::norm(q)>128)throw Error("origin Gram source row outside bounded domain");
                    const auto basis=product({q,0,true},oscillator),image=product(basis,basis);
                    auto& f=running_.fields[variant][bit];if(q!=Complex{})++f.nonzero;
                    f.image_real=c::plus(f.image_real,real(image));f.image_imag=c::plus(f.image_imag,imag(image));
                    f.trace=c::plus(f.trace,norm_square(basis));f.square=c::plus(f.square,norm_square({q,0,true}));
                    if(!f.image_real.bounded()||!f.image_imag.bounded()||!f.trace.bounded()||!f.square.bounded()){valid_=false;return;}
                }
            }
            oscillator=product(oscillator,{powers_->step(),0,true});if(!oscillator.valid){valid_=false;return;}
            last_end_=physical_offset+i+1;flush(last_end_);
        }
        committed=true;
    }
    void finish(std::uint64_t physical_end,std::stop_token stop={}){
        bool committed=false;struct FailClosed {bool& valid;bool& committed;~FailClosed(){if(!committed)valid=false;}} failed{valid_,committed};
        tile::check_stop(stop);if(finished_||physical_end<last_end_||physical_end>static_cast<std::uint64_t>(Powers::maximum_exponent))throw Error("invalid origin Gram final physical endpoint");
        flush(physical_end);finished_=true;valid_=valid_&&next_==points_.size();committed=true;
    }
    Output query(std::uint64_t first,std::uint64_t end,std::int64_t origin,const RawReference& reference,
            bool every_selected_observation_present,double processing_radius=0,double additional_entry_radius=0)const{
        Output result{};
        if(!valid()||!reference.valid()||reference.step()!=powers_->step()||!every_selected_observation_present||first>end||
           !std::isfinite(processing_radius)||processing_radius<0||!std::isfinite(additional_entry_radius)||additional_entry_radius<0)return result;
        const auto lookup=[&](auto p)->const Checkpoint*{const auto it=std::lower_bound(points_.begin(),points_.end(),p);
            return it!=points_.end()&&*it==p?&checkpoints_[it-points_.begin()]:nullptr;};
        const auto* a=lookup(first);const auto* b=lookup(end);if(!a||!b||a->selected>b->selected)return result;
        // PCM presence and ACTUAL original-fence certification are different
        // proofs. Check every selected physical run, including native q==0.
        if(origin<-Powers::maximum_exponent||origin>Powers::maximum_exponent)return result;
        for(const auto& run:selected_runs_){const auto lo=std::max(first,run.first),hi=std::min(end,run.end);if(lo>=hi)continue;
            const auto physical_first=origin+static_cast<std::int64_t>(lo),physical_end=origin+static_cast<std::int64_t>(hi);
            if(physical_first<0||physical_end<physical_first||!reference.covers(static_cast<std::uint64_t>(physical_first),static_cast<std::uint64_t>(physical_end)))return result;
        }
        const auto factor=reference.factor(origin),factor_square=product(factor,factor);if(!factor.valid||!factor_square.valid)return result;
        const auto factor_norm=upper(factor);const auto factor_norm2=norm_square(factor);
        const auto scalar=[](Interval x){return x.lo+.5*(x.hi-x.lo);};
        const auto radius=[](double center,Interval x){return std::max(std::abs(c::down(x.lo-center)),std::abs(c::up(x.hi-center)));};
        for(std::size_t variant=0;variant<Variants;++variant)for(unsigned bit=0;bit<2;++bit){
            const auto& fa=a->fields[variant][bit];const auto& fb=b->fields[variant][bit];
            if(fa.nonzero>fb.nonzero)continue;
            if(fa.nonzero==fb.nonzero){auto& out=result[variant][bit];out.radius=additional_entry_radius;
                out.count=b->selected-a->selected;out.template_identity=identity_;out.capture_generation=reference.generation();out.capture_revision=reference.revision();out.variant=variant;out.supported=true;continue;}
            const auto kr=c::minus(fb.image_real,fa.image_real),ki=c::minus(fb.image_imag,fa.image_imag);
            auto tr=c::minus(fb.trace,fa.trace),sq=c::minus(fb.square,fa.square);
            tr.lo=std::max(0.,tr.lo);sq.lo=std::max(0.,sq.lo);
            if(!kr.bounded()||!ki.bounded()||!tr.bounded()||!sq.bounded()||tr.hi<0||sq.hi<0)continue;
            const auto ar=real(factor_square),ai=imag(factor_square);
            const auto image_real=c::minus(c::times(ar,kr),c::times(ai,ki));
            const auto image_imag=c::plus(c::times(ar,ki),c::times(ai,kr));
            const auto trace=c::times(factor_norm2,tr);
            // |Oraw-C|<=d. C(o+v)=a(o)u_v. Cauchy bounds
            // Σ|q|²|u| by sqrt(Tcanonical*S0), without another prefix plane.
            const auto raw_error=c::add(c::mul(c::mul(2*reference.radius(),factor_norm),c::root(c::mul(tr.hi,sq.hi))),
                c::mul(c::mul(reference.radius(),reference.radius()),sq.hi));
            // This uses actual RAW magnitude, preserving the raw-error ×
            // processing-error cross term. Original q/prefix error is separate.
            const auto processing_error=c::mul(c::add(c::mul(2*reference.maximum(),processing_radius),c::mul(processing_radius,processing_radius)),sq.hi);
            const auto error=c::add(additional_entry_radius,c::add(raw_error,processing_error));
            auto cc=c::times(Interval::point(.5),c::plus(trace,image_real));
            auto ss=c::times(Interval::point(.5),c::minus(trace,image_real));
            auto cs=c::times(Interval::point(.5),image_imag);
            cc=c::plus(cc,{-error,error});ss=c::plus(ss,{-error,error});cs=c::plus(cs,{-error,error});
            if(!cc.bounded()||!ss.bounded()||!cs.bounded())continue;
            auto& out=result[variant][bit];out.cc=scalar(cc);out.ss=scalar(ss);out.cs=scalar(cs);
            out.radius=std::max({radius(out.cc,cc),radius(out.ss,ss),radius(out.cs,cs)});
            out.count=b->selected-a->selected;out.template_identity=identity_;out.capture_generation=reference.generation();out.capture_revision=reference.revision();
            out.variant=variant;out.supported=std::isfinite(out.cc)&&std::isfinite(out.ss)&&std::isfinite(out.cs)&&std::isfinite(out.radius);
        }
        return result;
    }
};
// Bounded stack packing of the existing A/B/D spans, never whole-symbol rows.
inline void append_three(Prefix<3>& prefix,std::uint64_t offset,
        std::span<const std::array<Complex,2>> a,std::span<const std::array<Complex,2>> b,
        std::span<const std::array<Complex,2>> d,std::span<const std::uint8_t> mask,std::stop_token stop={}) {
    if(a.size()!=b.size()||a.size()!=d.size()||(!mask.empty()&&mask.size()!=a.size())) {
        prefix.invalidate();throw Error("origin Gram A/B/D component shapes differ");
    }
    std::array<Prefix<3>::Row,64> packed;
    struct Clean {decltype(packed)& rows;~Clean(){OPENSSL_cleanse(rows.data(),sizeof(rows));}} clean{packed};
    try {for(std::size_t first=0;first<a.size();first+=packed.size()) {
        const auto count=std::min(packed.size(),a.size()-first);
        for(std::size_t j=0;j<count;++j)packed[j]={a[first+j],b[first+j],d[first+j]};
        prefix.append(tile::add_coordinate(offset,first),std::span<const Prefix<3>::Row>(packed).first(count),
            mask.empty()?std::span<const std::uint8_t>{}:mask.subspan(first,count),stop);
    }}catch(...){prefix.invalidate();throw;}
}
} // namespace datapump::modem::detail::scheduled_origin_gram

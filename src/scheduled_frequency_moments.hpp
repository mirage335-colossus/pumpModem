#pragma once
// Isolated proposal: shared coarse-frequency FFT moments, certified resolved
// fine-frequency input VIEW. The production tile only needs InputLike::add;
// this prototype includes its otherwise unchanged frozen-v2 adapter copy.
#include "scheduled_acquisition_tile.hpp"
#include <numbers>
#include "scheduled_moment_synthesis.hpp"

namespace datapump::modem::detail::scheduled_frequency_moments {
namespace tile=scheduled_acquisition_tile;
namespace cert=scheduled_acquisition_certificate;
using Complex=std::complex<double>;
inline constexpr unsigned maximum_degree=6;

struct Work {
    std::uint64_t shared_forward_transforms=0,resolved_forward_transforms=0;
    std::uint64_t synthesized_products=0,sampled_relation_samples=0;
    std::size_t working_bytes=0;
};
inline double absolute(Complex value){return cert::norm(value);}
inline void compensated(double value,double& sum,double& correction) {
    const auto next=sum+value;
    correction+=std::abs(sum)>=std::abs(value)?(sum-next)+value:(value-next)+sum;
    sum=next;
}
inline double difference_radius(Complex a,Complex b) {
    const auto re=cert::minus(cert::Interval::point(a.real()),cert::Interval::point(b.real()));
    const auto im=cert::minus(cert::Interval::point(a.imag()),cert::Interval::point(b.imag()));
    const auto r=std::max(std::abs(re.lo),std::abs(re.hi)),i=std::max(std::abs(im.lo),std::abs(im.hi));
    return cert::root(cert::add(cert::mul(r,r),cert::mul(i,i)));
}
template<class T> double scalar_product_radius(double scalar_norm,double vector_norm,std::size_t size) {
    if(!cert::environment<T>())return cert::infinity;
    const auto u=std::numeric_limits<T>::epsilon()/2,eta=std::numeric_limits<T>::denorm_min();
    return cert::add(cert::mul(cert::mul(cert::root(2),cert::gamma(2,u)),cert::mul(scalar_norm,vector_norm)),
        cert::mul(cert::mul(cert::root(2),cert::add(3,cert::mul(2,u))),cert::mul(eta,cert::root(size))));
}

template<class T> class MomentInput {
public:
    using Native=std::complex<T>;
    const tile::Plan<T>& plan;
    const unsigned degree;
    const double center_sample,radius_samples;
    const tile::PreparedInput<T> base;
    // Integer/half-integer local sample divided by a power of two is exact.
    // Actual rounded powers define the model; sampled relation also encloses
    // their difference from the ideal physical exponential.
    std::vector<std::array<double,maximum_degree+1>> powers;
    std::array<std::vector<Native>,2*maximum_degree> extra;
    std::array<cert::Operand,2*(maximum_degree+1)> certificates{};
    std::array<long double,2*(maximum_degree+1)> norms{};
    Work work;
    MomentInput(const tile::Plan<T>& p,tile::Input input,unsigned order,std::size_t budget,
                std::stop_token stop={})
        :plan(p),degree(order),center_sample(.5*(input.pcm.size()-1)),
         radius_samples(std::bit_ceil(std::max<std::size_t>(1,input.pcm.size()/2))),
         base(p,input,budget,stop) {
        if(order>maximum_degree)throw Error("frequency moment degree exceeds bound");
        auto estimate=tile::add_size(sizeof(*this),base.working_bytes());
        estimate=tile::add_size(estimate,tile::multiply_size(2*degree*plan.size,sizeof(Native)));
        estimate=tile::add_size(estimate,tile::multiply_size(input.pcm.size(),sizeof(powers[0])));
        if(estimate>budget)throw Error("frequency moments exceed workspace");
        try {
            tile::check_stop(stop);powers.resize(input.pcm.size());
            for(std::size_t i=0;i<powers.size();++i) {
                powers[i][0]=1;const auto v=(static_cast<double>(i)-center_sample)/radius_samples;
                for(unsigned k=1;k<=degree;++k)powers[i][k]=powers[i][k-1]*v;
            }
            for(unsigned kind=0;kind<2;++kind) {
                certificates[index(kind,0)]=base.certificates[kind];norms[index(kind,0)]=base.norms[kind];
                for(unsigned k=1;k<=degree;++k) {
                    auto& values=extra[kind*maximum_degree+k-1];values.resize(plan.size);
                    auto& proof=certificates[index(kind,k)];double peak=0;
                    for(std::size_t i=0;i<input.pcm.size();++i) {
                        if((i&255U)==0)tile::check_stop(stop);
                        if(!tile::present(input,i))continue;
                        const auto oc=input.oscillator[i];const auto x=static_cast<double>(input.pcm[i]);
                        const auto original=kind==0?x*oc:oc*oc;
                        const auto power=powers[i][k];const auto before=original*power;
                        values[i]=tile::cast<T>(before);
                        const auto first=kind==0?cert::complex_preproduct_radius({x,0},oc):cert::complex_preproduct_radius(oc,oc);
                        const auto preparation=cert::add(cert::mul(std::abs(power),first),
                            cert::complex_preproduct_radius(original,{power,0}));
                        proof.observe(before,values[i],preparation);
                        norms[index(kind,k)]+=static_cast<long double>(std::norm(before));
                        peak=std::max({peak,std::abs(before.real()),std::abs(before.imag())});
                    }
                    tile::range<T>(peak,plan.size);tile::transform(values,false,plan,stop);
                    proof.finish(plan.certificate,std::span<const Native>(values));
                    norms[index(kind,k)]=std::sqrt(norms[index(kind,k)]);
                }
            }
            work.shared_forward_transforms=3+2*degree;work.working_bytes=working_bytes();
            if(work.working_bytes>budget)throw Error("frequency moment capacity exceeds workspace");
        }catch(...){clean();throw;}
    }
    static constexpr unsigned index(unsigned kind,unsigned k){return kind*(maximum_degree+1)+k;}
    std::span<const Native> spectrum(unsigned kind,unsigned k)const {
        if(kind>1 || k>degree)throw Error("invalid frequency moment plane");
        return k?std::span<const Native>(extra[kind*maximum_degree+k-1]):std::span<const Native>(base.spectra[kind]);
    }
    MomentInput(const MomentInput&)=delete;MomentInput& operator=(const MomentInput&)=delete;
    ~MomentInput(){clean();}
    void clean(){for(auto& values:extra)tile::wipe(values);}
    std::size_t working_bytes()const {
        auto bytes=tile::add_size(sizeof(*this),base.working_bytes());
        bytes=tile::add_size(bytes,tile::multiply_size(powers.capacity(),sizeof(powers[0])));
        for(const auto& values:extra)bytes=tile::add_size(bytes,tile::multiply_size(values.capacity(),sizeof(Native)));
        return bytes;
    }
};

struct Coefficients {
    unsigned degree=0;
    std::uint64_t physical_first=0,phase_origin=0;
    std::size_t input_samples=0;
    double delta_hz=0,sample_rate=0,center_sample=0,radius_samples=0;
    std::array<Complex,maximum_degree+1> dot{},square{};
};
struct TaylorTail {double angle=0,dot=0,square=0;};
inline TaylorTail taylor_tail(std::size_t samples,std::uint32_t sample_rate,double delta_hz,unsigned degree) {
    if(!samples || !sample_rate || !std::isfinite(delta_hz) || degree>maximum_degree)
        return {cert::infinity,cert::infinity,cert::infinity};
    // Full INPUT window, including timing translations. The exact-real phase
    // Taylor derivative has unit magnitude; remainder needs no exp(angle).
    const auto radius=cert::divide(.5*static_cast<double>(samples-1),sample_rate);
    const auto tau=2*0x1.921fb54442d19p+1;
    const auto angle=cert::mul(cert::mul(tau,std::abs(delta_hz)),radius);
    double dot=1,square=1,factorial=1;
    for(unsigned k=1;k<=degree+1;++k) {
        dot=cert::mul(dot,angle);square=cert::mul(square,cert::mul(2,angle));factorial*=k;
    }
    return {angle,cert::divide(dot,factorial),cert::divide(square,factorial)};
}
template<class T> Coefficients coefficients(const MomentInput<T>& input,double delta_hz,std::uint32_t sample_rate,
                                           std::uint64_t original_phase_origin) {
    if(!sample_rate || !std::isfinite(delta_hz))throw Error("invalid fine frequency");
    Coefficients result;result.degree=input.degree;result.physical_first=input.base.original.physical_first;
    result.phase_origin=original_phase_origin;result.input_samples=input.base.original.pcm.size();
    result.delta_hz=delta_hz;result.sample_rate=sample_rate;result.center_sample=input.center_sample;
    result.radius_samples=input.radius_samples;
    // Subtract integer coordinates BEFORE conversion, preserving a small exact
    // capture-time displacement even when the absolute UTC sample is enormous.
    const auto first=result.physical_first;
    const auto distance=first>=original_phase_origin?static_cast<long double>(first-original_phase_origin):
        -static_cast<long double>(original_phase_origin-first);
    const long double tau=2*std::numbers::pi_v<long double>;
    const auto phase=tau*static_cast<long double>(delta_hz)*(distance+input.center_sample)/sample_rate;
    const auto beta=tau*static_cast<long double>(delta_hz)*input.radius_samples/sample_rate;
    result.dot[0]=std::polar(1.,static_cast<double>(std::remainder(phase,tau)));
    result.square[0]=std::polar(1.,static_cast<double>(std::remainder(2*phase,tau)));
    for(unsigned k=1;k<=input.degree;++k) {
        result.dot[k]=result.dot[k-1]*Complex{0,static_cast<double>(beta)}/static_cast<double>(k);
        result.square[k]=result.square[k-1]*Complex{0,static_cast<double>(2*beta)}/static_cast<double>(k);
    }
    // These are executed coefficient centers, not a libm/trig certificate.
    // sampled_remainder below encloses actual target-vs-model discrepancies;
    // a faster analytic relation must include phase and original recurrence.
    return result;
}
inline bool same(const Coefficients& a,const Coefficients& b) {
    return a.degree==b.degree && a.physical_first==b.physical_first && a.phase_origin==b.phase_origin &&
        a.input_samples==b.input_samples && a.delta_hz==b.delta_hz && a.sample_rate==b.sample_rate &&
        a.center_sample==b.center_sample && a.radius_samples==b.radius_samples && a.dot==b.dot && a.square==b.square;
}
struct ModelRemainder {
    // L2 ORIGINAL target-operand minus represented polynomial-model operand.
    // Inverse/FFT/casting/synthesis errors are separate and added by ResolvedInput.
    std::array<double,3> radius{cert::infinity,cert::infinity,cert::infinity};
    Coefficients model;
    const void* moment_owner=nullptr;
    const void* target_oscillator=nullptr;
    bool valid=false,sampled=false;
};
struct ComplexInterval {cert::Interval re,im;};
inline ComplexInterval multiply(ComplexInterval a,ComplexInterval b) {
    return {cert::minus(cert::times(a.re,b.re),cert::times(a.im,b.im)),
            cert::plus(cert::times(a.re,b.im),cert::times(a.im,b.re))};
}
inline ComplexInterval point(Complex z){return {cert::Interval::point(z.real()),cert::Interval::point(z.imag())};}
inline double residual_square(ComplexInterval a,ComplexInterval b) {
    const auto re=cert::minus(a.re,b.re),im=cert::minus(a.im,b.im);
    const auto er=std::max(std::abs(re.lo),std::abs(re.hi)),ei=std::max(std::abs(im.lo),std::abs(im.hi));
    return cert::add(cert::mul(er,er),cert::mul(ei,ei));
}
template<class T> void compatible(const MomentInput<T>& input,const Coefficients& c,const tile::Input& target,
                                  bool oscillator_required) {
    const auto& base=input.base.original;
    if(c.degree!=input.degree || c.input_samples!=base.pcm.size() || c.physical_first!=base.physical_first ||
       c.center_sample!=input.center_sample || c.radius_samples!=input.radius_samples ||
       target.physical_first!=base.physical_first || target.pcm.data()!=base.pcm.data() ||
       target.pcm.size()!=base.pcm.size() || target.observed.data()!=base.observed.data() ||
       target.observed.size()!=base.observed.size() ||
       (oscillator_required && target.oscillator.size()!=base.pcm.size()))
        throw Error("incompatible fine-frequency input identity");
}
template<class T> ModelRemainder sampled_remainder(const MomentInput<T>& input,const Coefficients& c,
                                                  tile::Input target,std::stop_token stop={}) {
    compatible(input,c,target,true);ModelRemainder result;result.model=c;result.moment_owner=&input;
    result.sampled=true;result.target_oscillator=target.oscillator.data();
    std::array<double,3> squares{};
    for(std::size_t i=0;i<target.pcm.size();++i) {
        if((i&255U)==0)tile::check_stop(stop);
        if(!tile::finite(target.oscillator[i]))throw Error("nonfinite original fine carrier");
        if(!tile::present(target,i))continue;
        const auto x=cert::Interval::point(target.pcm[i]);const auto oc=point(input.base.original.oscillator[i]);
        const auto ot=point(target.oscillator[i]);
        const std::array<ComplexInterval,2> base{{{cert::times(x,oc.re),cert::times(x,oc.im)},multiply(oc,oc)}};
        const std::array<ComplexInterval,2> original{{{cert::times(x,ot.re),cert::times(x,ot.im)},multiply(ot,ot)}};
        for(unsigned kind=0;kind<2;++kind) {
            ComplexInterval model{};
            for(unsigned k=0;k<=input.degree;++k) {
                const auto power=cert::Interval::point(input.powers[i][k]);
                const ComplexInterval moment{cert::times(base[kind].re,power),cert::times(base[kind].im,power)};
                const auto term=multiply(moment,point(kind==0?c.dot[k]:c.square[k]));
                model.re=cert::plus(model.re,term.re);model.im=cert::plus(model.im,term.im);
            }
            squares[kind]=cert::add(squares[kind],residual_square(original[kind],model));
        }
        const auto target_norm=cert::plus(cert::square(ot.re),cert::square(ot.im));
        const auto base_norm=cert::plus(cert::square(oc.re),cert::square(oc.im));
        squares[2]=cert::add(squares[2],residual_square({target_norm,{}},{base_norm,{}}));
    }
    for(unsigned kind=0;kind<3;++kind)result.radius[kind]=cert::root(squares[kind]);
    result.valid=cert::environment<double>() && std::all_of(result.radius.begin(),result.radius.end(),
        [](double value){return std::isfinite(value) && value>=0;});return result;
}
// Fast analytic alternative requires an independently justified caller proof.
// It must include full-window Taylor remainder, coefficient/center phase error,
// original target/base recurrence and trace-norm discrepancies. Unknown defaults
// remain invalid; neither equal nominal frequency nor GPSDO supplies this proof.
template<class T> ModelRemainder bind_certified_remainder(const MomentInput<T>& input,const Coefficients& c,
                                                         std::array<double,3> radii,bool proved) {
    ModelRemainder result;result.model=c;result.radius=radii;result.moment_owner=&input;
    result.valid=proved && cert::environment<double>() && std::all_of(radii.begin(),radii.end(),
        [](double value){return std::isfinite(value) && value>=0;});return result;
}

template<class T> class ResolvedInput {
    using Native=std::complex<T>;
    std::array<std::vector<Native>,2> owned_;
    std::vector<Complex> before_; // One reused, charged pre-cast buffer.
public:
    const tile::Plan<T>& plan;
    const tile::Input original; // Actual target oscillator, NEVER mislabeled center.
    std::array<std::span<const Native>,3> spectra;
    std::array<cert::Operand,3> certificates{};
    std::array<long double,3> norms{};
    std::span<const long double> energy;
    std::span<const std::uint64_t> count;
    std::span<const cert::Interval> certified_energy;
    Work work;
    ResolvedInput(const MomentInput<T>& input,const Coefficients& c,tile::Input target,
                  const ModelRemainder& remainder,std::size_t budget,std::stop_token stop={})
        :plan(input.plan),original(target),energy(input.base.energy),count(input.base.count),
         certified_energy(input.base.certified_energy) {
        compatible(input,c,target,remainder.sampled);
        if(remainder.moment_owner!=&input || !same(remainder.model,c) ||
           (remainder.sampled && remainder.target_oscillator!=target.oscillator.data()))
            throw Error("fine-frequency remainder identity mismatch");
        bool exact_zero=c.delta_hz==0 && target.oscillator.data()==input.base.original.oscillator.data() &&
            target.oscillator.size()==input.base.original.oscillator.size() && c.dot[0]==Complex{1,0} && c.square[0]==Complex{1,0};
        for(unsigned k=1;k<=input.degree;++k)exact_zero=exact_zero && c.dot[k]==Complex{} && c.square[k]==Complex{};
        if(exact_zero) {
            tile::check_stop(stop);
            if(sizeof(*this)>budget)throw Error("resolved fine input exceeds workspace");
            for(unsigned kind=0;kind<3;++kind) {
                spectra[kind]=input.base.spectra[kind];certificates[kind]=input.base.certificates[kind];norms[kind]=input.base.norms[kind];
            }
            work.working_bytes=working_bytes();return;
        }
        auto estimate=tile::add_size(sizeof(*this),tile::multiply_size(2*plan.size,sizeof(Native)));
        estimate=tile::add_size(estimate,tile::multiply_size(plan.size,sizeof(Complex)));
        if(estimate>budget)throw Error("resolved fine input exceeds workspace");
        try {
            tile::check_stop(stop);before_.resize(plan.size);
            for(unsigned kind=0;kind<2;++kind) {
                owned_[kind].resize(plan.size);cert::Operand cast_proof;
                double propagation=0,products=0,magnitude=0;
                std::array<Native,maximum_degree+1> native_coefficient{};
                for(unsigned k=0;k<=input.degree;++k) {
                    const auto coefficient=kind==0?c.dot[k]:c.square[k];
                    native_coefficient[k]=tile::cast<T>(coefficient);
                    const Complex executed{native_coefficient[k].real(),native_coefficient[k].imag()};
                    const auto& moment=input.certificates[MomentInput<T>::index(kind,k)];
                    const auto cast_error=difference_radius(coefficient,executed);
                    propagation=cert::add(propagation,cert::add(cert::mul(absolute(coefficient),moment.forward_radius),
                        cert::mul(cast_error,moment.norm_upper)));
                    const auto product_error=scalar_product_radius<T>(absolute(executed),moment.norm_upper,plan.size);
                    products=cert::add(products,product_error);
                    magnitude=cert::add(magnitude,cert::add(cert::mul(absolute(executed),moment.norm_upper),product_error));
                    norms[kind]+=absolute(coefficient)*input.norms[MomentInput<T>::index(kind,k)];
                }
                std::array<std::span<const Native>,maximum_degree+1> moment_planes{};
                for(unsigned k=0;k<=input.degree;++k)moment_planes[k]=input.spectrum(kind,k);
                const auto synthesis=scheduled_moment_synthesis::synthesize<T>(
                    std::span<const std::span<const Native>>(moment_planes).first(input.degree+1),
                    std::span<const Native>(native_coefficient).first(input.degree+1),
                    owned_[kind],before_,stop);
                for(std::size_t i=0;i<plan.size;++i) {
                    if((i&255U)==0)tile::check_stop(stop);
                    cast_proof.observe(before_[i],owned_[kind][i],0.);
                }
                // Conservative operation-path bound for native products plus
                // compensated FP64 additions/final addition; no exact-sum or
                // libm accuracy assumption. Packing radius is measured outward.
                const auto u=std::numeric_limits<double>::epsilon()/2;
                const auto reduction=cert::add(cert::mul(cert::gamma(32*(input.degree+1)+64,u),magnitude),
                    cert::mul(cert::mul(32*(input.degree+1)+64,std::numeric_limits<double>::denorm_min()),
                              cert::root(2*static_cast<double>(plan.size))));
                auto& proof=certificates[kind];proof.norm_upper=cert::root(cast_proof.native_square_upper);
                proof.forward_radius=remainder.valid && synthesis.arithmetic_eligible?cert::add(cert::add(propagation,products),
                    cert::add(cert::add(reduction,cert::root(cast_proof.radius_square_upper)),
                        cert::mul(cert::root(plan.size),remainder.radius[kind]))):cert::infinity;
                spectra[kind]=owned_[kind];
            }
            spectra[2]=input.base.spectra[2];certificates[2]=input.base.certificates[2];norms[2]=input.base.norms[2];
            certificates[2].forward_radius=remainder.valid?cert::add(certificates[2].forward_radius,
                cert::mul(cert::root(plan.size),remainder.radius[2])):cert::infinity;
            work.synthesized_products=2*(input.degree+1)*plan.size;
            work.sampled_relation_samples=remainder.sampled?target.pcm.size():0;
            work.working_bytes=working_bytes();
            if(work.working_bytes>budget)throw Error("resolved fine input capacity exceeds workspace");
        }catch(...){clean();throw;}
    }
    ResolvedInput(const ResolvedInput&)=delete;ResolvedInput& operator=(const ResolvedInput&)=delete;
    ~ResolvedInput(){clean();}
    void clean(){for(auto& values:owned_)tile::wipe(values);tile::wipe(before_);}
    std::size_t working_bytes()const {
        auto bytes=sizeof(*this);
        for(const auto& values:owned_)bytes=tile::add_size(bytes,tile::multiply_size(values.capacity(),sizeof(Native)));
        return tile::add_size(bytes,tile::multiply_size(before_.capacity(),sizeof(Complex)));
    }
};
} // namespace datapump::modem::detail::scheduled_frequency_moments

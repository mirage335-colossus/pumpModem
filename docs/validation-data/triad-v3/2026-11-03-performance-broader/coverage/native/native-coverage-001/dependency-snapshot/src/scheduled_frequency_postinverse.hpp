#pragma once
// Bounded SOURCE PROTOTYPE. Per-fragment native inverse responses are contracted
// immediately into all admitted fine frequencies. No physical-span FFT, B*J
// response history, shifted-PCM matrix or detector statistic is introduced.
// Caller identity/support/cold-scope and full original refinement remain required.
#include "scheduled_frequency_moments.hpp"

namespace datapump::modem::detail::scheduled_frequency_postinverse {
namespace tile=scheduled_acquisition_tile;
namespace cert=scheduled_acquisition_certificate;
namespace fusion=scheduled_frequency_moments;
using Complex=std::complex<double>;
inline constexpr std::size_t maximum_targets=512;

struct Scope {
    unsigned drift_sections=1;
    bool differential=false,fp64_force=false;
    double symbol_seconds=0; // Qualified cold screen: caller must supply >=6.
};
struct Binding {
    tile::Input original; // Same PCM/observed identity; actual target carrier.
    fusion::Coefficients coefficients;
    fusion::ModelRemainder remainder; // Bound against these LITERAL coefficients.
};
struct Work {
    std::uint64_t fragments=0,inverses=0,spectral_products=0,wide_products=0,prefix_queries=0,energy_refinements=0;
    std::size_t working_bytes=0,transform=0,targets=0,starts=0;
};
struct Sum {
    double value=0,correction=0,mass=0;
    std::size_t terms=0;
    void add(double term) {
        if(!std::isfinite(term))throw Error("post-inverse contraction overflow");
        fusion::compensated(term,value,correction);
        mass=cert::add(mass,std::abs(term));++terms;
        if(!std::isfinite(value) || !std::isfinite(correction) || !std::isfinite(mass))
            throw Error("post-inverse reduction overflow");
    }
    double result()const{return value+correction;}
    double error()const {
        // Literal sum/correction operations: conservative dependency-path bound
        // against the exact sum of already-rounded product centers. Product
        // disks are charged separately. No exact-compensation assumption.
        const auto operations=tile::add_size(tile::multiply_size(terms,8),8);
        return cert::add(cert::mul(cert::gamma(operations,std::numeric_limits<double>::epsilon()/2),mass),
            cert::mul(static_cast<double>(operations),std::numeric_limits<double>::denorm_min()));
    }
};
struct Cell {
    std::array<Sum,5> fields; // dot.re, dot.im, Z.re, Z.im, real trace.
    double dot_radius=0,square_radius=0,trace_radius=0;
};

template<class T> class Streaming {
    using Native=std::complex<T>;
    const tile::Plan<T>& plan_;
    std::uint64_t origin_=0,last_end_=0;
    std::span<const std::size_t> starts_;
    std::size_t targets_=0;
    std::vector<Cell> cells_;
    std::vector<long double> energy_;
    std::vector<cert::Interval> certified_energy_;
    std::vector<std::uint64_t> count_;
    std::vector<Native> scratch_;
    bool finished_=false,failed_=false;
    Work work_;
    Cell& cell(std::size_t f,std::size_t j,unsigned bit){return cells_[2*(f*starts_.size()+j)+bit];}
    void clean(){tile::wipe(cells_);tile::wipe(energy_);tile::wipe(certified_energy_);tile::wipe(count_);tile::wipe(scratch_);}
    static double operand_norm(const cert::Operand& operand) {
        return cert::add(cert::root(operand.native_square_upper),cert::root(operand.radius_square_upper));
    }
    void contract(std::size_t f,std::size_t j,unsigned bit,unsigned kind,Complex center,
                  Complex coefficient,double response_radius) {
        auto& c=cell(f,j,bit);
        const auto product=coefficient*center;
        const auto error=cert::add(cert::mul(cert::norm(coefficient),response_radius),
            cert::complex_preproduct_radius(coefficient,center));
        if(kind==0){c.fields[0].add(product.real());c.fields[1].add(product.imag());c.dot_radius=cert::add(c.dot_radius,error);}
        else if(kind==1){c.fields[2].add(product.real());c.fields[3].add(product.imag());c.square_radius=cert::add(c.square_radius,error);}
        else {c.fields[4].add(center.real());c.trace_radius=cert::add(c.trace_radius,response_radius);}
        if(kind!=2)++work_.wide_products;
    }
public:
    static std::size_t reservation(const tile::Plan<T>& plan,std::size_t starts,std::size_t targets) {
        if(!targets || targets>maximum_targets || starts>tile::maximum_start_span)
            throw Error("post-inverse reservation exceeds bounded family");
        auto bytes=tile::add_size(sizeof(Streaming),tile::helper_reservation);
        bytes=tile::add_size(bytes,tile::multiply_size(2*tile::multiply_size(targets,starts),sizeof(Cell)));
        bytes=tile::add_size(bytes,tile::multiply_size(starts,sizeof(long double)+sizeof(cert::Interval)+sizeof(std::uint64_t)));
        if(starts)bytes=tile::add_size(bytes,tile::multiply_size(plan.size,sizeof(Native)));
        return bytes;
    }
    Streaming(const tile::Plan<T>& plan,std::uint64_t physical_origin,
              std::span<const std::size_t> starts,std::size_t targets,std::size_t budget,
              Scope scope={},std::stop_token stop={})
        :plan_(plan),origin_(physical_origin),starts_(starts),targets_(targets) {
        if(scope.drift_sections!=1 || scope.differential || scope.fp64_force ||
           !std::isfinite(scope.symbol_seconds) || scope.symbol_seconds<6)
            throw Error("post-inverse prototype requires coherent cold scope; FP64-force uses original per-frequency fallback");
        if(!targets || targets>maximum_targets || starts.size()>tile::maximum_start_span)
            throw Error("post-inverse family exceeds bounded shape");
        for(std::size_t j=0;j<starts.size();++j)
            if(starts[j]>=plan.shape.start_span || (j && starts[j]<=starts[j-1]))
                throw Error("post-inverse admitted timing starts invalid");
        tile::add_coordinate(origin_,plan.shape.start_span-1);
        const auto bytes=reservation(plan,starts.size(),targets);
        if(bytes>budget)throw Error("post-inverse family exceeds workspace");
        try {
            tile::check_stop(stop);cells_.resize(2*targets*starts.size());energy_.resize(starts.size());
            certified_energy_.resize(starts.size());count_.resize(starts.size());
            if(!starts.empty())scratch_.resize(plan.size);
            work_.working_bytes=working_bytes();work_.transform=plan.size;work_.targets=targets;work_.starts=starts.size();
            if(work_.working_bytes>budget)throw Error("post-inverse capacities exceed workspace");
        }catch(...){clean();throw;}
    }
    Streaming(const Streaming&)=delete;Streaming& operator=(const Streaming&)=delete;
    ~Streaming(){clean();}
    std::size_t working_bytes()const {
        auto bytes=tile::add_size(sizeof(*this),tile::helper_reservation);
        bytes=tile::add_size(bytes,tile::multiply_size(cells_.capacity(),sizeof(Cell)));
        bytes=tile::add_size(bytes,tile::multiply_size(energy_.capacity(),sizeof(long double)));
        bytes=tile::add_size(bytes,tile::multiply_size(certified_energy_.capacity(),sizeof(cert::Interval)));
        bytes=tile::add_size(bytes,tile::multiply_size(count_.capacity(),sizeof(std::uint64_t)));
        return tile::add_size(bytes,tile::multiply_size(scratch_.capacity(),sizeof(Native)));
    }
    const Work& work()const{return work_;}
    void add(const fusion::MomentInput<T>& input,const tile::PreparedFragment<T>& fragment,
             std::span<const Binding> targets,std::stop_token stop={}) {
        if(finished_ || failed_)throw Error("post-inverse family no longer writable");
        try {
            tile::check_stop(stop);
            if(targets.size()!=targets_ || input.plan.size!=plan_.size || fragment.plan.size!=plan_.size ||
               input.plan.shape.start_span!=plan_.shape.start_span || fragment.plan.shape.start_span!=plan_.shape.start_span ||
               input.plan.shape.maximum_fragment!=plan_.shape.maximum_fragment || fragment.plan.shape.maximum_fragment!=plan_.shape.maximum_fragment ||
               input.base.original.physical_first!=tile::add_coordinate(origin_,fragment.original.physical_offset) ||
               (work_.fragments && fragment.original.physical_offset<last_end_) || work_.fragments==tile::maximum_fragments)
                throw Error("post-inverse fragment identity/support incompatible");
            last_end_=tile::add_coordinate(fragment.original.physical_offset,fragment.original.rows.size());
            for(const auto& target:targets) {
                tile::check_stop(stop);
                fusion::compatible(input,target.coefficients,target.original,target.remainder.sampled);
                if(target.remainder.moment_owner!=&input || !fusion::same(target.remainder.model,target.coefficients) ||
                   (target.remainder.sampled && target.remainder.target_oscillator!=target.original.oscillator.data()))
                    throw Error("post-inverse original carrier relation identity changed");
                if(!std::isfinite(target.coefficients.delta_hz) || !(target.coefficients.sample_rate>0) ||
                   !std::isfinite(target.coefficients.sample_rate))throw Error("post-inverse coefficient metadata invalid");
                for(unsigned k=0;k<=input.degree;++k)
                    if(!tile::finite(target.coefficients.dot[k]) || !tile::finite(target.coefficients.square[k]))
                        throw Error("post-inverse coefficient nonfinite");
                if(target.remainder.valid)for(const auto radius:target.remainder.radius)
                    if(!std::isfinite(radius) || radius<0)throw Error("post-inverse valid carrier proof has invalid radius");
            }
            if(starts_.empty()){++work_.fragments;return;}
            // Each physical sample contributes only once per admitted start.
            // Overlapping input windows for different starts are harmless;
            // overlapping contributing template fragments are rejected above.
            for(std::size_t j=0;j<starts_.size();++j) {
                tile::check_stop(stop);
                long double energy=0;std::uint64_t count=0;cert::Interval energy_disk;bool refine=false;
                for(const auto run:fragment.runs) {
                    if((work_.prefix_queries&255U)==0)tile::check_stop(stop);
                    const auto first=starts_[j]+run.first,end=starts_[j]+run.end;
                    if(end>=input.base.energy.size())throw Error("post-inverse energy query outside original capture");
                    const auto selected=input.base.count[end]-input.base.count[first];
                    if(selected>std::numeric_limits<std::uint64_t>::max()-count)throw Error("post-inverse observed count overflow");
                    count+=selected;const auto delta=input.base.energy[end]-input.base.energy[first];
                    const auto error=8*std::numeric_limits<long double>::epsilon()*
                        (std::abs(input.base.energy[end])+std::abs(input.base.energy[first]));
                    refine|=delta<0 || error>1e-12L*std::max(1e-30L,std::abs(delta));energy+=delta;
                    energy_disk=cert::plus(energy_disk,cert::energy_slice(input.base.certified_energy[end],input.base.certified_energy[first]));
                    ++work_.prefix_queries;
                }
                if(refine) {
                    energy=0;energy_disk={};
                    for(const auto run:fragment.runs)for(auto i=run.first;i<run.end;++i) {
                        if((i&255U)==0)tile::check_stop(stop);
                        const auto sample=starts_[j]+i;
                        if(tile::present(input.base.original,sample)) {
                            const auto x=static_cast<long double>(input.base.original.pcm[sample]);energy+=x*x;
                            energy_disk=cert::plus(energy_disk,cert::pcm_square(input.base.original.pcm[sample]));
                        }
                    }
                    ++work_.energy_refinements;
                }
                if(count>std::numeric_limits<std::uint64_t>::max()-count_[j])throw Error("post-inverse observed count overflow");
                count_[j]+=count;energy_[j]+=energy;certified_energy_[j]=cert::plus(certified_energy_[j],energy_disk);
            }
            // One native P scratch. Responses are consumed immediately; no
            // per-fragment or whole-symbol PCM/response history is retained.
            for(unsigned bit=0;bit<2;++bit)for(unsigned kind=0;kind<3;++kind) {
                const auto degree=kind==2?0:input.degree;
                for(unsigned k=0;k<=degree;++k) {
                    tile::check_stop(stop);
                    const auto source=kind==2?std::span<const Native>(input.base.spectra[2]):input.spectrum(kind,k);
                    const auto& a=kind==2?input.base.certificates[2]:input.certificates[fusion::MomentInput<T>::index(kind,k)];
                    const auto& b=fragment.certificates[3*bit+kind];
                    double square=0,peak=0;
                    for(std::size_t i=0;i<plan_.size;++i) {
                        if((i&1023U)==0)tile::check_stop(stop);
                        scratch_[i]=source[i]*fragment.spectra[3*bit+kind][i];
                        const Complex executed{scratch_[i].real(),scratch_[i].imag()};
                        if(!tile::finite(executed))throw Error("post-inverse spectral product overflow");
                        square+=executed.real()*executed.real()+executed.imag()*executed.imag();
                        peak=std::max({peak,std::abs(executed.real()),std::abs(executed.imag())});
                    }
                    tile::range<T>(peak,plan_.size);
                    // Positive FP64 sum-of-squares: enclose its literal four
                    // operations per element once, instead of outward rounding
                    // every inner-loop addition. Underflow remains additive.
                    const auto operations=tile::add_size(tile::multiply_size(4,plan_.size),4);
                    const auto g=cert::gamma(operations,std::numeric_limits<double>::epsilon()/2);
                    const auto square_upper=std::isfinite(square) && g<1?cert::divide(
                        cert::add(cert::up(square),cert::mul(static_cast<double>(operations),std::numeric_limits<double>::denorm_min())),
                        cert::down(1-g)):cert::infinity;
                    const auto product_error=cert::product_radius_l1<T>(a.norm_upper,b.norm_upper,plan_.size);
                    const auto radius=cert::add(cert::convolution_radius(a,b,product_error,plan_.size),
                        cert::fft_radius<T>(plan_.certificate,cert::root(square_upper),0,true));
                    tile::transform(scratch_,true,plan_,stop);++work_.inverses;work_.spectral_products+=plan_.size;
                    for(std::size_t j=0;j<starts_.size();++j) {
                        if((j&63U)==0)tile::check_stop(stop);
                        const auto value=scratch_[plan_.shape.maximum_fragment-1+starts_[j]];
                        const Complex center{value.real(),value.imag()};
                        for(std::size_t f=0;f<targets_;++f) {
                            const auto coefficient=kind==0?targets[f].coefficients.dot[k]:
                                (kind==1?targets[f].coefficients.square[k]:Complex{1,0});
                            contract(f,j,bit,kind,center,coefficient,radius);
                        }
                    }
                }
                const auto qnorm=operand_norm(fragment.certificates[3*bit+kind]);
                for(std::size_t f=0;f<targets_;++f) {
                    tile::check_stop(stop);
                    const auto model=targets[f].remainder.valid?cert::mul(qnorm,targets[f].remainder.radius[kind]):cert::infinity;
                    for(std::size_t j=0;j<starts_.size();++j) {
                        auto& c=cell(f,j,bit);
                        if(kind==0)c.dot_radius=cert::add(c.dot_radius,model);
                        else if(kind==1)c.square_radius=cert::add(c.square_radius,model);
                        else c.trace_radius=cert::add(c.trace_radius,model);
                    }
                }
            }
            ++work_.fragments;
        }catch(...){failed_=true;throw;}
    }
    // Sink stores one prepared cold-screen value, never detector admission or
    // observed absence. Caller precharges its output and preserves event order.
    template<class Sink> void finish(Sink&& sink,std::stop_token stop={}) {
        if(finished_ || failed_)throw Error("post-inverse family cannot finish");
        try {
            tile::check_stop(stop);
            for(std::size_t f=0;f<targets_;++f)for(std::size_t j=0;j<starts_.size();++j) {
                tile::check_stop(stop);tile::Value value;value.physical_origin=tile::add_coordinate(origin_,starts_[j]);
                value.certificate.energy_lower=std::max(0.,certified_energy_[j].lo);
                value.certificate.energy_upper=std::max(0.,certified_energy_[j].hi);
                value.certificate.valid=plan_.certificate.valid && cert::environment<T>() &&
                    std::isfinite(value.certificate.energy_lower) && std::isfinite(value.certificate.energy_upper);
                for(unsigned bit=0;bit<2;++bit) {
                    const auto& c=cell(f,j,bit);auto& fit=value.fits[bit];
                    fit.xc=c.fields[0].result();fit.xs=c.fields[1].result();
                    const auto zr=c.fields[2].result(),zi=c.fields[3].result(),trace=c.fields[4].result();
                    fit.cc=.5*(trace+zr);fit.ss=.5*(trace-zr);fit.cs=.5*zi;
                    fit.energy=static_cast<double>(energy_[j]);fit.count=count_[j];
                    if(!fit.count)fit={};
                    if(!tile::finite(fit))throw Error("post-inverse fit overflow");
                    const auto dot=cert::add(c.dot_radius,cert::root(cert::add(cert::mul(c.fields[0].error(),c.fields[0].error()),
                        cert::mul(c.fields[1].error(),c.fields[1].error()))));
                    const auto z=cert::add(c.square_radius,cert::root(cert::add(cert::mul(c.fields[2].error(),c.fields[2].error()),
                        cert::mul(c.fields[3].error(),c.fields[3].error()))));
                    const auto er=cert::add(c.trace_radius,c.fields[4].error());
                    const auto reconstruction=cert::add(cert::add(cert::ulp(fit.cc),cert::ulp(fit.ss)),
                        cert::add(cert::mul(2,cert::ulp(fit.cs)),cert::mul(6,std::numeric_limits<double>::denorm_min())));
                    value.certificate.dot_radius[bit]=dot;
                    value.certificate.gram_radius[bit]=cert::add(cert::mul(.5,cert::add(z,er)),reconstruction);
                    value.certificate.valid=value.certificate.valid && std::isfinite(dot) && dot>=0 &&
                        std::isfinite(value.certificate.gram_radius[bit]) && value.certificate.gram_radius[bit]>=0;
                    value.requires_refinement[bit]=true; // It is ONLY an exclusion screen.
                }
                sink(f,j,value);
            }
            finished_=true;
        }catch(...){failed_=true;throw;}
    }
};
} // namespace datapump::modem::detail::scheduled_frequency_postinverse

#include "scheduled_frequency_postinverse.hpp"
#include <stdexcept>
// These predicates must execute in Release builds as well as Debug.
static std::size_t checked_predicates=0;
#define CHECK(...) do { ++checked_predicates; if(!(__VA_ARGS__)) throw std::runtime_error(#__VA_ARGS__); } while(false)
#include <iostream>
#include <memory>
#include <numbers>

namespace tile=datapump::modem::detail::scheduled_acquisition_tile;
namespace cert=datapump::modem::detail::scheduled_acquisition_certificate;
namespace fusion=datapump::modem::detail::scheduled_frequency_moments;
namespace post=datapump::modem::detail::scheduled_frequency_postinverse;
using Complex=std::complex<double>;
static constexpr std::size_t budget=32*1024*1024;
static constexpr post::Scope scope{1,false,false,6};
template<class F> static bool rejects(F&& f){try{f();}catch(const std::exception&){return true;}return false;}

static std::vector<Complex> carrier(std::uint64_t first,std::size_t count,double frequency) {
    std::vector<Complex> values(count);const double tau=2*std::numbers::pi;
    const auto step=std::polar(1.,tau*frequency/1024.);
    for(std::size_t i=0;i<count;) {
        const auto physical=first+i,block=physical-physical%512;
        auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block)*tau*frequency/1024.,static_cast<long double>(tau))));
        for(auto n=block;n<physical;++n)o*=step;
        do {values[i]=o;o*=step;++i;}while(i<count && (first+i)%512!=0);
    }
    return values;
}

template<class T> static void paired(unsigned seed) {
    constexpr unsigned order=3,fragments=4,targets=5;
    const std::array<std::size_t,4> starts{0,7,31,63};
    tile::Plan<T> plan({64,32});
    post::Streaming<T> streaming(plan,0,starts,targets,budget,scope);
    std::array<std::unique_ptr<tile::Accumulator<T>>,targets> preceding;
    std::array<std::vector<tile::Value>,targets> direct;
    for(unsigned f=0;f<targets;++f){preceding[f]=std::make_unique<tile::Accumulator<T>>(plan,0,starts,budget);direct[f].resize(starts.size());}
    for(unsigned b=0;b<fragments;++b) {
        const std::uint64_t physical_first=499+1473*b;
        std::vector<float> pcm(95);std::vector<std::uint8_t> observed(pcm.size(),1);
        for(unsigned i=0;i<pcm.size();++i){pcm[i]=static_cast<float>(.05*std::sin((i+physical_first)*(seed+1)*.173)+.017*std::cos(i*.053));
            if(i%29==17)observed[i]=0;}
        auto coarse=carrier(physical_first,pcm.size(),31.);
        tile::Input input{pcm,coarse,observed,physical_first};
        fusion::MomentInput<T> moment(plan,input,order,budget);
        std::vector<std::array<Complex,2>> rows(32);std::vector<std::uint8_t> mask(rows.size(),1);
        for(unsigned i=0;i<rows.size();++i) {
            const auto source=b*32+i+.375; // Fractional source rows, not interpolated by receiver operator.
            rows[i][0]=std::polar(.53+.19*std::sin(source*.071),source*.31);
            rows[i][1]=std::polar(.26+.16*std::cos(source*.093),source*.17+.63);
            if(i%19==7)mask[i]=0;
        }
        tile::Fragment fragment{rows,mask,physical_first};tile::PreparedFragment<T> prepared(plan,fragment,budget);
        std::array<std::vector<Complex>,targets> target_carriers;
        std::array<post::Binding,targets> bindings;
        for(unsigned f=0;f<targets;++f) {
            const double delta=.013*static_cast<double>(f);
            target_carriers[f]=carrier(physical_first,pcm.size(),31.+delta);
            tile::Input target{pcm,target_carriers[f],observed,physical_first};
            const auto coefficients=fusion::coefficients(moment,delta,1024,0);
            const auto remainder=fusion::sampled_remainder(moment,coefficients,target);
            bindings[f]={target,coefficients,remainder};
            fusion::ResolvedInput<T> resolved(moment,coefficients,target,remainder,budget);
            preceding[f]->add(resolved,prepared);
            tile::direct_add(target,fragment,0,starts,direct[f]);
        }
        streaming.add(moment,prepared,bindings);
    }
    std::array<std::vector<tile::Value>,targets> preceding_values,values;
    tile::TraceReuse<T> trace;
    for(unsigned f=0;f<targets;++f){const auto result=preceding[f]->finish({},f?&trace:nullptr);
        if(!f)trace=preceding[f]->trace_reuse();preceding_values[f].assign(result.begin(),result.end());values[f].resize(starts.size());}
    unsigned callbacks=0;
    streaming.finish([&](std::size_t f,std::size_t j,const tile::Value& value){values[f][j]=value;++callbacks;});
    CHECK(callbacks==targets*starts.size());
    CHECK(streaming.work().inverses==2*(2*(order+1)+1)*fragments);
    CHECK(streaming.work().spectral_products==streaming.work().inverses*plan.size);
    CHECK(streaming.working_bytes()<budget);
    for(unsigned f=0;f<targets;++f)for(unsigned j=0;j<starts.size();++j)for(unsigned bit=0;bit<2;++bit) {
        const auto& value=values[f][j];const auto& fit=value.fits[bit];const auto& original=direct[f][j].fits[bit];
        const auto& old=preceding_values[f][j];const auto& oldfit=old.fits[bit];
        CHECK(value.certificate.valid && old.certificate.valid && value.requires_refinement[bit]);
        CHECK(fit.count==original.count && fit.count==oldfit.count);CHECK(value.physical_origin==starts[j]);
        CHECK(value.certificate.energy_lower<=original.energy+1e-15 && value.certificate.energy_upper>=original.energy-1e-15);
        const auto reference=cert::scalar_reference_errors(fit.count,value.certificate.energy_upper,
            cert::up(original.cc+original.ss),0);
        const auto dot=std::hypot(fit.xc-original.xc,fit.xs-original.xs);
        const auto a=fit.cc-original.cc,b=fit.cs-original.cs,c=fit.ss-original.ss;
        const auto gram=.5*(std::abs(a+c)+std::hypot(a-c,2*b));
        CHECK(dot<=cert::add(value.certificate.dot_radius[bit],reference.dot));
        CHECK(gram<=cert::add(value.certificate.gram_radius[bit],reference.gram));
        CHECK(std::hypot(fit.xc-oldfit.xc,fit.xs-oldfit.xs)<=
            cert::add(value.certificate.dot_radius[bit],old.certificate.dot_radius[bit]));
    }
    CHECK(rejects([&]{streaming.finish([](auto,auto,const auto&){});}));
    std::cout<<(std::is_same_v<T,float>?"FP32":"FP64")<<" seed="<<seed<<" inverses="<<streaming.work().inverses
        <<" native_products="<<streaming.work().spectral_products<<" wide_products="<<streaming.work().wide_products
        <<" workspace="<<streaming.working_bytes()<<'\n';
}

static void lifecycle_and_energy() {
    tile::Plan<double> plan({1,8});const std::array<std::size_t,1> starts{0};
    std::vector<float> pcm(8,1e-4F);pcm[0]=1e20F;
    std::vector<Complex> oscillators(8,{1,0});
    tile::Input input{pcm,oscillators,{},0};fusion::MomentInput<double> moment(plan,input,0,budget);
    std::vector<std::array<Complex,2>> rows(8);
    for(unsigned i=0;i<8;++i)rows[i]={std::polar(.7,i*.3),std::polar(.4,i*.7)};
    std::vector<std::uint8_t> mask(8,1);mask[0]=0;tile::Fragment fragment{rows,mask,0};
    tile::PreparedFragment<double> prepared(plan,fragment,budget);
    const auto coefficients=fusion::coefficients(moment,0,1024,0);
    const auto remainder=fusion::sampled_remainder(moment,coefficients,input);
    const std::array<post::Binding,1> binding{{{input,coefficients,remainder}}};
    post::Streaming<double> energy(plan,0,starts,1,budget,scope);energy.add(moment,prepared,binding);
    tile::Value value;energy.finish([&](auto,auto,const auto& result){value=result;});
    long double expected=0;for(unsigned i=1;i<8;++i)expected+=static_cast<long double>(pcm[i])*pcm[i];
    CHECK(energy.work().energy_refinements==1);
    CHECK(value.fits[0].energy==static_cast<double>(expected) && value.fits[0].count==7);
    CHECK(value.certificate.energy_lower<=expected && value.certificate.energy_upper>=expected);
    CHECK(rejects([&]{energy.add(moment,prepared,binding);}));
    auto unknown=binding;unknown[0].remainder.valid=false;
    post::Streaming<double> unproved(plan,0,starts,1,budget,scope);unproved.add(moment,prepared,unknown);
    unsigned unknown_callbacks=0;
    unproved.finish([&](auto,auto,const auto& result){++unknown_callbacks;CHECK(!result.certificate.valid);});
    CHECK(unknown_callbacks==1);
    auto bad=binding;bad[0].remainder.radius[0]=-1;
    post::Streaming<double> poisoned(plan,0,starts,1,budget,scope);
    CHECK(rejects([&]{poisoned.add(moment,prepared,bad);}));
    CHECK(rejects([&]{poisoned.finish([](auto,auto,const auto&){});}));
    bad=binding;bad[0].remainder.moment_owner=nullptr;
    post::Streaming<double> ownership(plan,0,starts,1,budget,scope);
    CHECK(rejects([&]{ownership.add(moment,prepared,bad);}));
    post::Streaming<double> overlapping(plan,0,starts,1,budget,scope);overlapping.add(moment,prepared,binding);
    CHECK(rejects([&]{overlapping.add(moment,prepared,binding);}));
    std::stop_source stop;stop.request_stop();
    post::Streaming<double> cancelled_add(plan,0,starts,1,budget,scope);
    CHECK(rejects([&]{cancelled_add.add(moment,prepared,binding,stop.get_token());}));
    post::Streaming<double> empty(plan,0,{},1,budget,scope);bool cancelled_callback_invoked=false;
    CHECK(rejects([&]{empty.finish([&](auto,auto,const auto&){cancelled_callback_invoked=true;},stop.get_token());}));
    CHECK(!cancelled_callback_invoked);
    post::Sum reduction;reduction.add(1e20);reduction.add(1);reduction.add(-1e20);
    reduction.add(std::numeric_limits<double>::denorm_min());
    CHECK(std::abs(reduction.result()-1)<=reduction.error());
}

int main() {
    CHECK(cert::environment<double>());
    for(unsigned seed=0;seed<3;++seed){paired<float>(seed);paired<double>(seed);}
    lifecycle_and_energy();
    tile::Plan<float> plan({8,8});const std::array<std::size_t,2> starts{0,7};
    CHECK(rejects([&]{post::Streaming<float> s(plan,0,starts,1,1,scope);}));
    CHECK(rejects([&]{post::Streaming<float> s(plan,0,starts,1,budget,{1,false,true,6});}));
    CHECK(rejects([&]{post::Streaming<float> s(plan,0,starts,1,budget,{2,false,false,6});}));
    CHECK(rejects([&]{post::Streaming<float> s(plan,0,starts,1,budget,{1,true,false,6});}));
    CHECK(rejects([&]{post::Streaming<float> s(plan,0,starts,1,budget,{});}));
    std::stop_source cancelled;cancelled.request_stop();
    CHECK(rejects([&]{post::Streaming<float> s(plan,0,starts,1,budget,scope,cancelled.get_token());}));
    std::cout<<"streaming post-inverse family source tests passed\n";
    std::cout<<"checked_predicates="<<checked_predicates<<'\n';
}

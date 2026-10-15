#include "../src/scheduled_partial_colored_runtime.hpp"
#include <iostream>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace r=d::scheduled_partial_colored_runtime;
namespace c=d::scheduled_acquisition_certificate;
namespace color=d::scheduled_cold_colored;
namespace poly=d::scheduled_polynomial_chip;
#define CHECK(x) do{if(!(x))throw std::runtime_error("check failed: " #x);}while(false)
constexpr std::size_t C=34,N=8*C;
using Complex=std::complex<double>;
struct Fixture {
    color::SourceRows source{{0,0,0,0,0},0,N};
    Complex step=std::polar(1.,2*3.14159265358979323846*1500/6120.);
    Fixture(){for(std::size_t i=0;i<N;++i){
        const auto physical=i<73?1000+i:3000+i-73;
        const auto gain=std::min({1.,double(i+1)/9,double(N-i)/9});
        source.append(physical,{gain*std::polar(.7+.08*std::sin(.13*i),.11*i),
            gain*std::polar(.8+.07*std::cos(.17*i),-.09*i)},i,gain,(i>=2&&i/C!=4)?1:0);
    }source.seal();CHECK(source.prepare_integral_geometry(N,C,true));}
    r::CarrierFences fences(bool missing=false)const{
        r::CarrierFences result(step);std::uint64_t last=~std::uint64_t{};
        for(const auto& run:source.runs){const auto begin=(run.physical_offset/512)*512,end=run.physical_offset+run.count;
            for(auto first=begin;first<end;first+=512){if(first==last)continue;last=first;if(missing&&first==3072)continue;
                CHECK(result.add(first,512,std::polar(1.,double(std::remainder(static_cast<long double>(first)*2*3.14159265358979323846L*1500/6120.,2*3.14159265358979323846L)))));}}
        return result;
    }
    r::Request request(const r::CarrierFences& carrier)const{
        r::Request q;q.identity=0x51c341;q.symbol_samples=N;q.observed_begin=1003;q.observed_end=3000+N-73-2;
        q.available_begin=0;q.available_end=q.observed_end;
        q.threshold=50;q.carrier_radius=1e-6;q.carrier_magnitude=2;q.original_prefix=512;
        q.finite_original_float_pcm=true;q.math={1e-12,true};q.proof_byte_limit=128*1024;
        d::CorrelationFit whole;
        for(const auto& run:source.runs)for(std::size_t k=0;k<run.count;++k){const auto i=run.first+k,p=run.physical_offset+k;
            if(!source.mask[i]||p<q.observed_begin||p>=q.observed_end)continue;
            auto o=carrier.oscillator(p);CHECK(o);const auto v=source.rows[i][0]*(*o);
            whole.cc+=v.real()*v.real();whole.ss+=v.imag()*v.imag();whole.cs+=v.real()*v.imag();whole.energy+=1;++whole.count;
        }
        for(auto& fit:q.whole)fit=color::enclose(whole,0,0,0);return q;
    }
};
void ordinary_partial_owners(){Fixture f;poly::SourceModels model(f.source,N);r::SourceWitnesses witness(f.source,model,f.step);
    auto fences=f.fences();auto request=f.request(fences);auto proof=r::preflight(f.source,model,witness,fences,request);
    CHECK(proof.all[0]&&proof.all[1]);CHECK(proof.all[0]->valid()&&proof.all[1]->valid());
    CHECK(proof.all[0]->count()==request.whole[0].count);CHECK(proof.all[1]->count()==request.whole[1].count);
    CHECK(proof.counters.canonical_cells>0);CHECK(proof.counters.direct_gram_cells>=3); // ramp crop, cross-run owner, final crop
    CHECK(proof.counters.direct_gram_rows<request.whole[0].count/2);CHECK(proof.counters.zero_cells>=1);
    std::array<d::CorrelationChipEvidence,2> exact;
    for(const auto& run:f.source.runs)for(std::size_t k=0;k<run.count;++k){const auto i=run.first+k,p=run.physical_offset+k;
        if(!f.source.mask[i]||p<request.observed_begin||p>=request.observed_end)continue;
        auto o=fences.oscillator(p);CHECK(o);
        for(unsigned bit=0;bit<2;++bit){const auto q=f.source.rows[i][bit]*(*o);d::CorrelationFit fit;
            fit.cc=q.real()*q.real();fit.ss=q.imag()*q.imag();fit.cs=q.real()*q.imag();fit.energy=1;fit.count=1;
            exact[bit].add_fit(fit,i/C);}
    }
    for(unsigned bit=0;bit<2;++bit){const auto last=d::CorrelationChipEvidence::projection(exact[bit].cell);
        CHECK(exact[bit].nested);CHECK(proof.all[bit]->rank()==exact[bit].projected_rank+last.second);}
    auto huge=request;huge.physical_translation=0x1p64L;huge.observed_begin=0;huge.observed_end=~std::uint64_t{};
    huge.available_begin=0;huge.available_end=~std::uint64_t{};CHECK(!r::preflight(f.source,model,witness,fences,huge).all[0]);
    auto wrong=request;++wrong.whole[0].count;++wrong.whole[1].count;CHECK(!r::preflight(f.source,model,witness,fences,wrong).all[0]);
    auto missing=f.fences(true);CHECK(!r::preflight(f.source,model,witness,missing,request).all[0]);
    auto retired=request;retired.available_begin=1004;CHECK(!r::preflight(f.source,model,witness,fences,retired).all[0]);
    auto unproduced=request;--unproduced.available_end;CHECK(!r::preflight(f.source,model,witness,fences,unproduced).all[0]);
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{(void)r::preflight(f.source,model,witness,fences,request,stop.get_token());}catch(const std::exception&){cancelled=true;}CHECK(cancelled);
    f.source.invalidate_geometry();CHECK(!witness.matches(f.source,model,f.step));CHECK(f.source.prepare_integral_geometry(N,C,true));
    CHECK(!witness.matches(f.source,model,f.step));
}
void carrier_fence_contract(){const auto step=std::polar(1.,.31);r::CarrierFences f(step);CHECK(f.add(100,512,{.6,.8}));
    CHECK(f.add(100,512,{.6,.8}));CHECK(f.adjacent(100));CHECK(!f.adjacent(611));CHECK(f.covers(110,612));CHECK(!f.covers(110,613));
    auto o=Complex{.6,.8};for(std::size_t k=0;k<512;++k){const auto* a=f.find(100+k);CHECK(a);
        const auto norm=std::abs(o);CHECK(norm>=a->minimum&&norm<=a->maximum);CHECK(f.oscillator(100+k)==std::optional<Complex>(o));o*=step;}
    r::CarrierFences bad(step);CHECK(!bad.add(100,513,{1,0}));CHECK(!bad.valid());
}
// Actual SourceRows + raw physical carrier + float PCM + chronological
// Original per-owner fits. This is a certificate fixture, not private-pattern
// calibration or an acquisition-bank sensitivity measurement.
void actual_prefix_rejection(bool correct_signal){
    constexpr std::size_t owners=256,samples=owners*C;
    color::SourceRows source({0,0,0,0,0},0,samples);
    for(std::size_t i=0;i<samples;++i){const auto owner=i/C;
        const auto physical=i<4097?1000+i:20000+i-4097;
        source.append(physical,{std::polar(.7+.1*std::sin(double(owner)),.73*owner),
            std::polar(.8+.1*std::cos(double(owner)),.91*owner)},i,1,1);}
    source.seal();CHECK(source.prepare_integral_geometry(samples,C,true));poly::SourceModels models(source,samples);
    const auto step=std::polar(1.,.31);r::CarrierFences carrier(step);std::uint64_t last=~std::uint64_t{};
    for(const auto& run:source.runs)for(auto first=(run.physical_offset/512)*512;first<run.physical_offset+run.count;first+=512){
        if(first==last)continue;last=first;CHECK(carrier.add(first,512,std::polar(1.,.31*first)));}
    r::SourceWitnesses witnesses(source,models,step);std::vector<std::array<d::CorrelationFit,2>> cells(owners);
    std::array<d::CorrelationFit,2> whole{};std::array<d::CorrelationChipEvidence,2> original{};
    for(const auto& run:source.runs)for(std::size_t k=0;k<run.count;++k){const auto i=run.first+k,physical=run.physical_offset+k,owner=i/C;
        const auto o=carrier.oscillator(physical);CHECK(o);
        const auto sign=correct_signal?1.:(owner&1?-1.:1.);
        const float x=static_cast<float>(sign*(source.rows[i][0]*(*o)).real());
        for(unsigned bit=0;bit<2;++bit){const auto q=source.rows[i][bit]*(*o);auto& f=cells[owner][bit];
            f.xc+=double(x)*q.real();f.xs+=double(x)*q.imag();f.cc+=q.real()*q.real();f.ss+=q.imag()*q.imag();
            f.cs+=q.real()*q.imag();f.energy+=double(x)*x;++f.count;}
    }
    const auto merge=[](auto& a,const auto& b){a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;};
    for(std::size_t owner=0;owner<owners;++owner)for(unsigned bit=0;bit<2;++bit){merge(whole[bit],cells[owner][bit]);original[bit].add_fit(cells[owner][bit],owner);}
    r::Request request;request.identity=17;request.symbol_samples=samples;request.observed_begin=1000;request.observed_end=20000+samples-4097;
    request.available_begin=0;request.available_end=request.observed_end;request.threshold=50;request.carrier_radius=1e-6;
    request.carrier_magnitude=2;request.original_prefix=512;request.finite_original_float_pcm=true;request.math={1e-12,true};request.proof_byte_limit=128*1024;
    for(unsigned bit=0;bit<2;++bit)request.whole[bit]=color::enclose(whole[bit],0,0,0);
    r::Reply reply;r::Session session(source,models,witnesses,carrier,request,reply);CHECK(session.supported());
    std::array<color::Summary,2> cumulative;for(auto& bit:cumulative)bit.owner_set_certified=true;
    for(std::size_t owner=0;owner<owners;++owner){for(unsigned bit=0;bit<2;++bit)cumulative[bit].retain(color::enclose(cells[owner][bit],0,0,0));
        const bool stopped=session.completed_prefix(owner,cumulative);
        for(unsigned bit=0;bit<2;++bit)if(std::isfinite(reply.upper[bit]))CHECK(reply.upper[bit]+1e-10>=original[bit].score(whole[bit]));
        if(stopped)break;
    }
    if(correct_signal){CHECK(original[0].score(whole[0])>=request.threshold);CHECK(!reply.rejected);}
    else{CHECK(original[0].colored_excess(whole[0],request.threshold));CHECK(original[1].colored_excess(whole[1],request.threshold));
        CHECK(reply.rejected);CHECK(reply.counters.stopped_after_owner<owners-1);CHECK(reply.counters.prefix_checks<owners);}
}
int main(){try{if(!c::coordinate_environment()){std::cout<<"partial runtime unsupported arithmetic environment\n";return 0;}
    ordinary_partial_owners();carrier_fence_contract();actual_prefix_rejection(false);actual_prefix_rejection(true);std::cout<<"partial colored runtime tests PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "scheduled_linear_family.hpp"
#include "coarse_pulse_cells.hpp"
#include "datapump/tuning.hpp"
#include "datapump/transfer.hpp"
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace datapump;using namespace datapump::modem;
namespace d=datapump::modem::detail;namespace family=d::scheduled_linear_family;
namespace grid=d::scheduled_linear_grid;namespace coarse=d::scheduled_coarse_pulse_cells;
namespace cert=d::scheduled_acquisition_certificate;
using C=std::complex<double>;
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
constexpr std::size_t budget=32*1024*1024;
template<class F>void rejects(F&&f){bool threw=false;try{f();}catch(const Error&){threw=true;}CHECK(threw);}
struct Fixture {
    family::Geometry g;std::vector<float>pcm;std::vector<C>oscillator;std::vector<std::uint8_t>forced;
    Fixture(long double rate){forced.assign(9,0);forced.back()=1;g={.375L,4.25L/rate,3,512,65,9,4,7,forced};
        pcm.resize(g.capture_end);oscillator.resize(g.capture_end);
        for(std::size_t n=0;n<pcm.size();++n)pcm[n]=float(.19*std::sin(.073*n)+.13*std::cos(.047*n)+.017*std::sin(1.33*n));
        const auto rotation=std::polar(1.,.43);for(std::size_t first=0;first<oscillator.size();first+=64){auto o=std::polar(1.,std::remainder(first*.43,2*std::numbers::pi));
            for(std::size_t n=first;n<std::min(first+64,oscillator.size());++n){oscillator[n]=o;o*=rotation;}}
    }
    static grid::Pair node(std::size_t n){return {{{.3*std::sin(.13*n),.24*std::cos(.17*n)},{.27*std::cos(.23*n),-.31*std::sin(.11*n)}}};}
    static bool usable(std::size_t n){return n>=2&&n<63&&!(n>=7&&n<13);}
    static bool direct(std::size_t n){return n==2||n==14||n==62;}
    grid::Cell cell(std::size_t n,long double,long double)const{grid::Cell c;c.left=node(n);c.right=node(n+1);c.observed=usable(n);c.supported=!direct(n);c.residual={0,0};return c;}
    family::OriginalRow original(std::size_t j,std::uint64_t n)const{
        family::OriginalRow o;const auto u=(static_cast<long double>(n)-g.physical_base)/g.node_spacing-j*g.step-(forced[j]?.37L:0.L);
        if(u<0||u>=g.template_cells)return o;const auto at=static_cast<std::size_t>(std::floor(u));if(!usable(at))return o;
        o.observed=true;const auto t=static_cast<double>(u-at);const auto a=node(at),b=node(at+1);
        for(unsigned bit=0;bit<2;++bit){o.q[bit]=(1-t)*a[bit]+t*b[bit];if(direct(at))o.q[bit]+=.08*std::sin(4*t)*C{1.,bit?-.7:.7};}return o;
    }
    d::CorrelationProjection projection(std::uint64_t n)const{const auto x=double(pcm[n]);const auto o=oscillator[n];return {x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};}
    template<class Emit>void visit(std::uint64_t first,std::uint64_t end,Emit emit)const{for(auto n=first;n<end;++n)if(n%11)emit(n,projection(n));}
    std::array<d::CorrelationFit,2> oracle(std::size_t j)const{std::array<d::CorrelationFit,2>out;for(auto n=g.capture_first;n<g.capture_end;++n){if(!(n%11))continue;
            const auto q=original(j,n);if(q.observed)for(unsigned b=0;b<2;++b)out[b].add(projection(n),q.q[b],1);}return out;}
};
template<class T>void family_oracle(long double rate){Fixture f(rate);
    const auto result=family::run<T>(f.g,budget,[&](auto n,auto a,auto b){return f.cell(n,a,b);},
        [&](auto a,auto b,auto emit){f.visit(a,b,emit);},[&](auto j,auto n){return f.original(j,n);});
    CHECK(result.values.size()==f.g.outputs&&result.residual_square==0&&result.residual_l1==0);
    CHECK(result.work.original_cells==3&&result.work.original_visits>0&&result.work.transforms.partitions>1);
    CHECK(result.work.transforms.inverse_ffts==16&&result.work.peak_bytes<=budget);
    for(std::size_t j=0;j<f.g.outputs;++j){const auto expected=f.oracle(j);const auto&value=result.values[j];CHECK(value.certificate.valid);
        for(unsigned b=0;b<2;++b){const auto&a=value.fits[b],&e=expected[b];CHECK(a.count==e.count);
            CHECK(std::abs(a.xc-e.xc)<=value.certificate.dot_radius[b]);CHECK(std::abs(a.xs-e.xs)<=value.certificate.dot_radius[b]);
            CHECK(std::abs(a.cc-e.cc)<=value.certificate.gram_radius[b]);CHECK(std::abs(a.ss-e.ss)<=value.certificate.gram_radius[b]);CHECK(std::abs(a.cs-e.cs)<=value.certificate.gram_radius[b]);
            CHECK(e.energy>=value.certificate.energy_lower&&e.energy<=value.certificate.energy_upper);
            CHECK(std::abs(a.score()-e.score())<(std::is_same_v<T,float>?1e-4:1e-8));}}
}
void seams(){Fixture f(1);unsigned callbacks=0;
    rejects([&]{family::run<float>(f.g,1,[&](auto n,auto a,auto b){++callbacks;return f.cell(n,a,b);},[&](auto a,auto b,auto emit){f.visit(a,b,emit);},[&](auto j,auto n){return f.original(j,n);});});CHECK(!callbacks);
    std::stop_source source;rejects([&]{family::run<float>(f.g,budget,[&](auto n,auto a,auto b){return f.cell(n,a,b);},
        [&](auto a,auto b,auto emit){f.visit(a,b,[&](auto n,const auto&p){if(++callbacks==32)source.request_stop();emit(n,p);});},[&](auto j,auto n){return f.original(j,n);},source.get_token());});CHECK(source.stop_requested());
    grid::Moments m(4,true,65536);rejects([&]{m.add(0,0,{0,0,1e308,0,0,1},1e-200);});
    grid::Moments good(4,true,65536);good.add(0,.5,{1,0,1,0,0,1},1);good.finish();rejects([&]{grid::ObservationPrefix p(good,1);});
    d::scheduled_linear_grid_fft::Geometry g{2,2,1,2,true,false};d::scheduled_linear_grid_fft::StreamingEngine<float>fft(g,budget);
    std::array<d::scheduled_linear_grid_fft::InputRow,3>x{};std::array<d::scheduled_linear_grid_fft::TemplateRow,2>q{};
    std::array<d::scheduled_linear_grid_fft::InputRadius,3>xr{};std::array<d::scheduled_linear_grid_fft::TemplateRadius,2>qr{};xr[0][0]=-1;
    rejects([&]{fft.append(x,q,xr,qr);});
}
void actual_pulse(){for(const auto private_key:{false,true}){
    auto c=datapump::tuning::resolve(private_key?36:360,8,datapump::tuning::PatternMode::auto_keystream,private_key,1500,private_key?10:1,
        OuterDsssVersion::interleaved_v2,SearchArithmetic::default_mode).config;
    c.sample_rate=6120;if(private_key){datapump::transfer::Options options;options.modem=c;options.key.emplace(Bytes(32,0x67));c=datapump::transfer::seeded_config(options,1800000000);}
    PatternCode pattern(c,1800000000);pattern.set_stream_phase_samples(136);pattern.prepare_symbol(0);
    CHECK(pattern.chip_samples()==34);CHECK(pattern.symbol_samples()%pattern.chip_samples()==0);
    for(unsigned Q:{8U,16U,4U,2U})for(const auto amplitude:{.03,2.})for(std::uint64_t cell=32*Q;cell<36*Q;++cell){
        coarse::Request r{pattern.symbol_samples(),pattern.chip_samples(),cell,Q,amplitude,1e-12,true};
        const auto proof=coarse::certify(r,[&](auto k){return pattern.values(k);},[&](double x){return pattern.shaped_values(0,x);});CHECK(proof.cell.supported&&proof.endpoint_reads==2);
        const auto first=double(cell)*pattern.chip_samples()/Q,last=double(cell+1)*pattern.chip_samples()/Q;
        for(unsigned i=0;i<=31;++i){const auto t=double(i)/31,x=(1-t)*first+t*last;const auto raw=pattern.shaped_values(0,x);
            for(unsigned b=0;b<2;++b){const auto original=pattern_limit_pcm(amplitude*raw[b])/amplitude;
                const auto line=(1-t)*proof.cell.left[b]+t*proof.cell.right[b];CHECK(std::abs(original-line)<=proof.cell.residual[b]);}}}
}}
int main(){try{for(const auto r:{1.L,1.L-0.000037L,1.L+0.000051L}){family_oracle<float>(r);family_oracle<double>(r);}seams();actual_pulse();
    std::cout<<"linear family Original I/Q/Gram/count/certificate, nonunit/fractional, clipping/pulse/cancel/workspace passed\n";}
    catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

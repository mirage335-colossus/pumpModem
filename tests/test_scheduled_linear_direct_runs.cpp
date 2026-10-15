#include "scheduled_linear_family.hpp"
#define scheduled_linear_family scheduled_linear_family_cell_reference
#include "scheduled_linear_family_cell_reference.hpp"
#undef scheduled_linear_family
#include <bit>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace datapump;using namespace datapump::modem;
namespace d=datapump::modem::detail;namespace family=d::scheduled_linear_family;
namespace grid=d::scheduled_linear_grid;using C=std::complex<double>;
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
constexpr std::size_t budget=8*1024*1024;
struct Fixture {
    family::Geometry g;std::array<std::uint8_t,5>forced{0,0,0,0,1};std::vector<float>pcm;std::vector<C>osc;
    explicit Fixture(long double rate){g={.625L,1.75L/rate,3,512,98,5,2,13,forced};pcm.resize(g.capture_end);osc.resize(g.capture_end);
        for(std::size_t n=0;n<pcm.size();++n)pcm[n]=float(.19*std::sin(.073*n)+.13*std::cos(.047*n));
        const auto rotation=std::polar(1.,.43);for(std::size_t first=0;first<osc.size();first+=64){auto o=std::polar(1.,std::remainder(first*.43,2*std::numbers::pi));
            for(std::size_t n=first;n<std::min(first+64,osc.size());++n){osc[n]=o;o*=rotation;}}}
    static grid::Pair node(std::size_t n){return {{{.3*std::sin(.13*n),.24*std::cos(.17*n)},{.27*std::cos(.23*n),-.31*std::sin(.11*n)}}};}
    static bool direct(std::size_t n){return (n>=5&&n<12)||(n>=25&&n<34)||(n>=60&&n<69)||n>=89;}
    static bool usable(std::size_t n){return n>=5&&n<97&&!(n>=70&&n<76);}
    grid::Cell cell(std::size_t n,long double,long double)const{grid::Cell c;c.left=node(n);c.right=node(n+1);c.observed=usable(n);c.supported=!direct(n);c.residual={0,0};return c;}
    family::OriginalRow original(std::size_t j,std::uint64_t n)const{family::OriginalRow o;
        const auto u=(static_cast<long double>(n)-g.physical_base)/g.node_spacing-j*g.step-(forced[j]?.375L:0.L);
        if(u<0||u>=97)return o;const auto at=static_cast<std::size_t>(std::floor(u));if(!usable(at))return o;
        o.observed=true;const auto t=static_cast<double>(u-at);const auto a=node(at),b=node(at+1);
        for(unsigned bit=0;bit<2;++bit){o.q[bit]=(1-t)*a[bit]+t*b[bit];if(direct(at))o.q[bit]+=.08*std::sin(4*t)*C{1.,bit?-.7:.7};}return o;}
    d::CorrelationProjection projection(std::uint64_t n)const{const auto x=double(pcm[n]);const auto o=osc[n];return {x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x};}
    template<class Emit>void visit(std::uint64_t first,std::uint64_t end,Emit emit)const{for(auto n=first;n<end;++n)if(n%11)emit(n,projection(n));}
    auto expected_calls(std::size_t j)const{std::vector<std::uint64_t>out;for(auto n=g.capture_first;n<g.capture_end;++n){if(!(n%11))continue;
        const auto u=(static_cast<long double>(n)-g.physical_base)/g.node_spacing-j*g.step;
        if(u>=0&&u<g.template_cells&&(forced[j]||direct(static_cast<std::size_t>(std::floor(u)))))out.push_back(n);}return out;}
    std::array<d::CorrelationFit,2> oracle(std::size_t j)const{std::array<d::CorrelationFit,2>out;for(auto n=g.capture_first;n<g.capture_end;++n){if(!(n%11))continue;
        const auto q=original(j,n);if(q.observed)for(unsigned b=0;b<2;++b)out[b].add(projection(n),q.q[b],1);}return out;}
    auto old_counts()const{std::uint64_t calls=0,visits=0;for(std::size_t offset=0;offset<g.template_cells;offset+=g.fragment_cells){const auto actual=std::min(g.fragment_cells,g.template_cells-offset);
        for(std::size_t j=0;j<g.outputs;++j)for(std::size_t i=0;i<actual;++i)if(forced[j]||direct(offset+i)){
            ++calls;const auto at=offset+i+j*g.step;const auto lo=g.physical_base+at*g.node_spacing,hi=g.physical_base+(at+1)*g.node_spacing;
            const auto first=static_cast<std::uint64_t>(std::clamp(std::floor(lo)-2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)));
            const auto end=static_cast<std::uint64_t>(std::clamp(std::ceil(hi)+2,static_cast<long double>(g.capture_first),static_cast<long double>(g.capture_end)));
            for(auto n=first;n<end;++n)visits+=n%11!=0;}}
        return std::pair{calls,visits};}
};
template<class T>void check(long double rate){Fixture f(rate);std::array<std::vector<std::uint64_t>,5>calls;
    const auto out=family::run<T>(f.g,budget,[&](auto k,auto a,auto b){return f.cell(k,a,b);},
        [&](auto a,auto b,auto emit){f.visit(a,b,emit);},[&](auto j,auto n){CHECK(calls[j].empty()||calls[j].back()<n);calls[j].push_back(n);return f.original(j,n);});
    namespace ref=d::scheduled_linear_family_cell_reference;
    const ref::Geometry rg{f.g.physical_base,f.g.node_spacing,f.g.capture_first,f.g.capture_end,f.g.template_cells,f.g.outputs,f.g.step,f.g.fragment_cells,f.g.original_outputs};
    std::array<std::vector<std::uint64_t>,5>reference_calls;
    const auto preceding=ref::run<T>(rg,budget,[&](auto k,auto a,auto b){return f.cell(k,a,b);},
        [&](auto a,auto b,auto emit){f.visit(a,b,emit);},[&](auto j,auto n){reference_calls[j].push_back(n);return f.original(j,n);});
    const auto bits=[](double x){return std::bit_cast<std::uint64_t>(x);};
    CHECK(out.residual_square==preceding.residual_square&&out.residual_l1==preceding.residual_l1&&out.residual_gram==preceding.residual_gram);
    const auto old=f.old_counts();CHECK(out.work.original_run_cells==old.first);CHECK(out.work.original_runs<old.first/2);CHECK(out.work.original_visits<old.second);
    CHECK(out.work.original_runs>0&&out.work.original_usable>0&&out.work.peak_bytes<=budget);CHECK(out.work.transforms.inverse_ffts==16);
    for(std::size_t j=0;j<f.g.outputs;++j){CHECK(calls[j]==f.expected_calls(j)&&calls[j]==reference_calls[j]);const auto expected=f.oracle(j);const auto&value=out.values[j];CHECK(value.certificate.valid);
        const auto&oldvalue=preceding.values[j];CHECK(value.certificate.dot_radius==oldvalue.certificate.dot_radius&&value.certificate.gram_radius==oldvalue.certificate.gram_radius&&
            bits(value.certificate.energy_lower)==bits(oldvalue.certificate.energy_lower)&&bits(value.certificate.energy_upper)==bits(oldvalue.certificate.energy_upper)&&value.certificate.valid==oldvalue.certificate.valid);
        for(unsigned bit=0;bit<2;++bit){const auto&a=value.fits[bit],&e=expected[bit],&oldfit=oldvalue.fits[bit];CHECK(a.count==e.count&&a.count==oldfit.count);
            CHECK(bits(a.xc)==bits(oldfit.xc)&&bits(a.xs)==bits(oldfit.xs)&&bits(a.cc)==bits(oldfit.cc)&&bits(a.ss)==bits(oldfit.ss)&&bits(a.cs)==bits(oldfit.cs)&&bits(a.energy)==bits(oldfit.energy));
            CHECK(std::abs(a.xc-e.xc)<=value.certificate.dot_radius[bit]);CHECK(std::abs(a.xs-e.xs)<=value.certificate.dot_radius[bit]);
            CHECK(std::abs(a.cc-e.cc)<=value.certificate.gram_radius[bit]);CHECK(std::abs(a.ss-e.ss)<=value.certificate.gram_radius[bit]);CHECK(std::abs(a.cs-e.cs)<=value.certificate.gram_radius[bit]);
            CHECK(e.energy>=value.certificate.energy_lower&&e.energy<=value.certificate.energy_upper);}}
}
void cancellation(){Fixture f(1);std::stop_source source;std::uint64_t calls=0;bool caught=false;
    try{family::run<float>(f.g,budget,[&](auto,auto,auto){return grid::Cell{};},[&](auto a,auto b,auto emit){f.visit(a,b,emit);},
        [&](auto j,auto n){if(++calls==17)source.request_stop();return f.original(j,n);},source.get_token());}catch(const Error&){caught=true;}
    CHECK(caught&&source.stop_requested());}
int main(){try{for(const auto rate:{1.L,1.L-.000037L,1.L+.000051L}){check<float>(rate);check<double>(rate);}cancellation();
    std::cout<<"unsupported-cell runs preserve exact Original call identity/order, I/Q/Gram/counts, forced terminal, fractional/nonunit, budget and cancellation\n";
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

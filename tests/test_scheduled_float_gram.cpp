#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/scheduled_float_gram.hpp"
#include <cassert>
#include <iostream>
#include <random>

using namespace datapump::modem::detail;
using namespace datapump::modem::detail::scheduled_float_gram;
using W=std::complex<long double>;
int main() {
    std::mt19937_64 rng(0x6a4d2);std::uniform_real_distribution<double> random(-1,1);
    std::size_t pairs=0,accepted=0,rank_fallbacks=0,error_fallbacks=0,near_fallbacks=0;
    double largest_ratio=0;
    for(unsigned repeat=0;repeat<32;++repeat)for(const auto count:{2U,6U,17U,32U,128U})
        for(const auto omega:{0.,1e-9,3e-6,1e-5,.001,.1,.31,1.4})for(const double scale:{1e-10,1.,1e10}) {
            ScheduledSpanMoments m;m.energy=count;m.count=count;
            for(unsigned i=0;i<count;++i) {
                const auto carrier=std::polar(1.,omega*i),square=carrier*carrier;
                double n=1;
                for(unsigned j=0;j<3;++j){m.square[j]+=n*square;m.norm[j]+=n*std::norm(carrier);n*=i;}
            }
            const std::array<std::complex<double>,2> value{{scale*std::complex<double>{random(rng),random(rng)},scale*std::complex<double>{random(rng),random(rng)}}};
            const std::array<std::complex<double>,2> slope{{.005*value[0],scale*std::complex<double>{random(rng)/32,random(rng)/32}}};
            const auto result=evaluate(m,value,slope);++pairs;
            if(!result) {
                rank_fallbacks+=result.failure==Failure::rank;error_fallbacks+=result.failure==Failure::error;
                near_fallbacks+=omega<=1e-9;continue;
            }
            ++accepted;assert(omega>1e-9);
            std::array<CorrelationFit,2> original;
            assert(scheduled_affine_pair(m,value,slope,original));
            for(unsigned bit=0;bit<2;++bit) {
                const W v{value[bit].real(),value[bit].imag()},s{slope[bit].real(),slope[bit].imag()};
                std::array<W,3> c;for(unsigned j=0;j<3;++j)c[j]={m.square[j].real(),m.square[j].imag()};
                const long double trace=std::norm(v)*m.norm[0]+2*(v*std::conj(s)).real()*m.norm[1]+std::norm(s)*m.norm[2];
                const W square=v*v*c[0]+2.L*v*s*c[1]+s*s*c[2];
                const std::array<long double,3> oracle{(trace+square.real())/2,(trace-square.real())/2,square.imag()/2};
                const auto& g=result.gram[bit];const std::array<double,3> actual{g.cc,g.ss,g.cs};
                const std::array<double,3> ref{original[bit].cc,original[bit].ss,original[bit].cs};
                for(unsigned j=0;j<3;++j) {
                    const auto error=std::abs(static_cast<long double>(actual[j])-oracle[j]);
                    assert(error<=result.entry_error[bit]);
                    assert(std::abs(actual[j]-ref[j])<=result.entry_error[bit]);
                    largest_ratio=std::max(largest_ratio,static_cast<double>(error/result.entry_error[bit]));
                }
                assert(g.cc*g.ss-g.cs*g.cs>1e-10*(g.cc+g.ss)*(g.cc+g.ss));
                assert(result.lambda_lower[bit]>0 && result.maximum_relative_error<=1e-4);
                assert(g.energy==m.energy && g.count==m.count && g.xc==0 && g.xs==0);
            }
        }
    assert(accepted>100 && near_fallbacks>100 && rank_fallbacks>100);
    {
        ScheduledSpanMoments m;m.count=6;m.energy=6;m.norm={6,15,55};
        const std::array<std::complex<double>,2> v{{{1,0},{0,1}}},s{};
        assert(evaluate(m,v,s));
        m.norm[0]=std::numeric_limits<double>::infinity();assert(!evaluate(m,v,s));
        m.norm[0]=6;m.square[0]={6,0};assert(evaluate(m,v,s).failure==Failure::rank);
        m.square[0]={0,0};assert(!evaluate(m,v,s,0));
        const auto previous=std::fegetround();std::fesetround(FE_UPWARD);
        assert(evaluate(m,v,s).failure==Failure::environment);std::fesetround(previous);
    }
    std::cout<<"pairs="<<pairs<<" accepted="<<accepted<<" rank_fallbacks="<<rank_fallbacks
        <<" error_fallbacks="<<error_fallbacks<<" near_singular_fallbacks="<<near_fallbacks
        <<" maximum_observed_entry_error_over_bound="<<largest_ratio<<'\n';
}

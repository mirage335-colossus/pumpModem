#include "scheduled_frequency_piece.hpp"
#include "datapump/pattern_pulse.hpp"
#include <cstdlib>
#include <stdexcept>
#define CHECK(condition) do {if(!(condition))throw std::runtime_error("check failed: " #condition);} while(false)
#include <iostream>
#include <iomanip>
#include <map>
#include <random>
using namespace datapump::modem;
namespace d=datapump::modem::detail;
using C=std::complex<double>;
static constexpr double tau=2*std::numbers::pi;
std::vector<d::CorrelationProjection> projections(const std::vector<double>& x,std::uint64_t first,double f,unsigned fs) {
    std::vector<d::CorrelationProjection> out(x.size()+1);
    C oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(first)*tau*f/fs,static_cast<long double>(tau))));
    const C step=std::polar(1.,tau*f/fs);
    for(std::size_t i=0;i<x.size();++i) {
        const auto c=oscillator.real(),s=oscillator.imag();auto p=out[i];
        p.xc+=x[i]*c;p.xs+=x[i]*s;p.cc+=c*c;p.ss+=s*s;p.cs+=c*s;p.energy+=x[i]*x[i];out[i+1]=p;oscillator*=step;
    }
    return out;
}
void add(d::CorrelationFit& a,const d::CorrelationFit& b) {
    a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;
}
int main() {
    std::mt19937_64 rng(0x4ce7a130d921ULL);std::normal_distribution<double> normal;
    std::size_t accepted=0,rejected=0;double maxdot=0,maxgram=0,maxscore=0,maxouter=0,maxcertificate=0;
    for(unsigned fs:{6000U,6120U})for(double carrier:{.005,.5,1500.})for(std::size_t count:{8U,34U,128U})for(unsigned fixture=0;fixture<8;++fixture) {
        constexpr std::size_t block=512,left=73;const std::uint64_t first=fixture&1?1099511627776ULL:123456;
        std::vector<double> x(block);for(auto& v:x)v=normal(rng)*.17;
        if(fixture==4)for(auto& v:x)v=std::clamp(v*6,-.99,.99); // clipping/interference amplitude
        if(fixture==5)for(std::size_t i=0;i<block;++i)x[i]+=.8*std::cos(tau*1499.7*i/fs);
        std::array<std::array<C,33>,2> coefficients{};
        for(auto& row:coefficients)for(auto& c:row)c={normal(rng)*.65,normal(rng)*.65};
        const auto base=projections(x,first,carrier,fs);
        for(int side:{-1,0,1}) {
            const auto delta=side*.00044*fs/(tau*(count-1));const auto fine=carrier+delta;
            const auto target=projections(x,first,fine,fs);
            d::ScheduledFrequencyPiece piece(count);
            std::array<d::CorrelationFit,2> direct{};
            std::array<std::map<std::uint64_t,C>,1> pulse_sum;
            std::array<std::array<C,2>,128> phases{};
            std::array<std::array<double,17>,128> pulses{};
            constexpr std::uint64_t chip=34,symbol=32*chip+17,owner=12;
            const double offset=owner*chip+(fixture%3)*.137;
            // Each production piece is confined to one SOURCE-chip owner.
            const double rate=std::min(1.,(chip-.5)/(count-1));
            for(std::size_t n=0;n<count;++n) {
                const auto within=offset+n*rate;
                const auto gain=fixture==2 && n%11==0?0.:(fixture==3?.2+.8*n/(count-1):1.);
                if(gain==0){CHECK(piece.skip(n));continue;}
                const auto p=base[left+n+1]-base[left+n];
                const auto fp=target[left+n+1]-target[left+n];
                const auto lower=owner-8;
                pattern_pulse_each(within,symbol,chip,[&](std::uint64_t index,double weight) {
                    CHECK(index>=lower && index-lower<17);pulses[n][index-lower]=gain*weight;
                    pulse_sum[0][index]+=gain*weight*C{fp.xc,fp.xs};
                });
                for(unsigned bit=0;bit<2;++bit) {
                    const auto u=pattern_pulse_sum(within,symbol,chip,[&](std::uint64_t index){return coefficients[bit][index];});
                    phases[n][bit]=gain*pattern_limit_pcm(.2766992952647332*u)/.2766992952647332;
                    direct[bit].add(fp,phases[n][bit],1);
                }
                CHECK(piece.add(p,phases[n],pulses[n],true,n));
            }
            const auto result=piece.evaluate(d::scheduled_frequency_piece_phase(first,left,block,carrier,fine,fs),base.back().energy,block);
            if(!result.supported){++rejected;continue;}++accepted;
            maxcertificate=std::max(maxcertificate,result.maximum_relative_error);
            for(unsigned bit=0;bit<2;++bit) {
                const auto& a=result.fit[bit];const auto& b=direct[bit];CHECK(a.count==b.count);
                CHECK(std::abs(a.energy-b.energy)<=1e-11*std::max(1.,b.energy));
                const auto dot=std::hypot(a.xc-b.xc,a.xs-b.xs);
                const auto gram=std::max({std::abs(a.cc-b.cc),std::abs(a.ss-b.ss),std::abs(a.cs-b.cs)});
                const auto scale=std::max(1e-30,b.cc+b.ss);
                maxdot=std::max(maxdot,dot/std::sqrt(b.energy*scale));maxgram=std::max(maxgram,gram/scale);
                maxscore=std::max(maxscore,std::abs(a.score()-b.score()));
                CHECK(dot<=result.dot_error[bit]);CHECK(gram<=result.gram_error[bit]);
                CHECK(dot/std::sqrt(b.energy*scale)<1e-8 && gram/scale<1e-8);
                d::CorrelationChipEvidence ca,cb;ca.add_fit(a,owner);cb.add_fit(b,owner);
                CHECK(std::abs(ca.score(a)-cb.score(b))<1e-5);
                CHECK(ca.colored_excess(a,40)==cb.colored_excess(b,40));
            }
            d::CorrelationOuterEvidence oa,ob;
            const auto coefs=[&](std::uint64_t index){return std::array<C,2>{coefficients[0][index],coefficients[1][index]};};
            for(const auto& [index,value]:pulse_sum[0])ob.add(index,value,coefs);
            for(std::size_t j=0;j<17;++j)if(pulse_sum[0].contains(owner-8+j)) {
                const auto truth=pulse_sum[0].at(owner-8+j);
                maxouter=std::max(maxouter,std::abs(result.outer[j]-truth)/std::max(1.,std::abs(truth)));
                CHECK(std::abs(result.outer[j]-truth)<1e-8*std::max(1.,std::abs(truth)));
                CHECK(std::abs(result.outer[j]-truth)<=result.outer_error[j]);
                oa.add(owner-8+j,result.outer[j],coefs);
            }
            oa.finish(coefs);ob.finish(coefs);
            for(unsigned bit=0;bit<2;++bit) {
                CHECK(oa.contributing_terms[bit]==ob.contributing_terms[bit]);
                CHECK(oa.can_admit(bit,40)==ob.can_admit(bit,40));
                CHECK(std::abs(oa.score(bit)-ob.score(bit))<1e-5);
            }
        }
    }
    // Successive source chips retain complete outer pulse dots across many
    // separated physical captures. This is not a concatenated physical clock.
    std::array<std::array<C,257>,2> stream_coefficients{};
    for(auto& row:stream_coefficients)for(auto& c:row)c={normal(rng)*.6,normal(rng)*.6};
    const auto coefficients=[&](std::uint64_t index){return std::array<C,2>{stream_coefficients[0][index],stream_coefficients[1][index]};};
    d::CorrelationOuterEvidence outer_direct,outer_shared;
    std::array<d::CorrelationFit,2> whole_direct{},whole_shared{};
    std::array<d::CorrelationChipEvidence,2> chips_direct{},chips_shared{};
    std::array<std::array<d::CorrelationFit,2>,4> quarters_direct{},quarters_shared{};
    std::array<d::DifferentialAccumulator,2> differential_direct{},differential_shared{};
    constexpr unsigned fs=6120;constexpr std::uint64_t chip=34,symbol=256*chip+17;
    std::size_t stream_pieces=0;std::uint64_t physical=1099511627776ULL;
    for(std::uint64_t owner=0;owner<257;++owner) {
        std::array<d::CorrelationFit,2> local_direct{},local_shared{};
        const auto duration=owner==256?17U:34U;
        for(unsigned part=0;part<duration;part+=17) {
            constexpr std::size_t block=512,left=61,count=17;
            std::vector<double> x(block);for(auto& value:x)value=.1*normal(rng);
            // Interference, changing amplitude and noise-only portions share
            // precisely the same original physical captures in both paths.
            for(std::size_t n=0;n<block;++n)x[n]+=.03*std::cos(tau*1499.9*n/fs);
            const auto base=projections(x,physical,1500,fs),fine=projections(x,physical,1500.002,fs);
            d::ScheduledFrequencyPiece piece(count);
            const auto lower=owner>=8?owner-8:0;
            std::uint32_t valid=0;
            for(std::size_t n=0;n<count;++n) {
                const double within=owner*chip+part+n;
                const double gain=n==0 && owner%7==0?0.:1.;
                if(gain==0){CHECK(piece.skip(n));continue;}
                std::array<double,17> pulse{};
                pattern_pulse_each(within,symbol,chip,[&](std::uint64_t index,double weight){
                    CHECK(index>=lower && index-lower<17);pulse[index-lower]=gain*weight;valid|=1U<<(index-lower);
                    const auto fp=fine[left+n+1]-fine[left+n];
                    outer_direct.add(index,gain*weight*C{fp.xc,fp.xs},coefficients);
                });
                std::array<C,2> phase{};
                for(unsigned bit=0;bit<2;++bit) {
                    const auto source=pattern_pulse_sum(within,symbol,chip,[&](std::uint64_t index){return stream_coefficients[bit][index];});
                    phase[bit]=gain*pattern_limit_pcm(.2766992952647332*source)/.2766992952647332;
                    d::CorrelationFit f;f.add(fine[left+n+1]-fine[left+n],phase[bit],1);
                    add(local_direct[bit],f);add(whole_direct[bit],f);add(quarters_direct[std::min<std::uint64_t>(3,owner/64)][bit],f);
                    chips_direct[bit].add_fit(f,owner);
                }
                CHECK(piece.add(base[left+n+1]-base[left+n],phase,pulse,true,n));
            }
            const auto result=piece.evaluate(d::scheduled_frequency_piece_phase(physical,left,block,1500,1500.002,fs),base.back().energy,block);
            CHECK(result.supported);++stream_pieces;
            for(unsigned bit=0;bit<2;++bit) {
                add(local_shared[bit],result.fit[bit]);add(whole_shared[bit],result.fit[bit]);
                add(quarters_shared[std::min<std::uint64_t>(3,owner/64)][bit],result.fit[bit]);
                chips_shared[bit].add_fit(result.fit[bit],owner);
            }
            for(unsigned j=0;j<17;++j)if(valid&(1U<<j))outer_shared.add(lower+j,result.outer[j],coefficients);
            physical+=50000+owner%11; // fresh capture phase, no source advance during null
        }
        if(owner<256)for(unsigned bit=0;bit<2;++bit) {
            const auto whiten=[](const d::CorrelationFit& f){return d::differential_whiten(f.xc,f.xs,f.cc,f.ss,f.cs);};
            differential_direct[bit].add(whiten(local_direct[bit]),owner,256*chip,chip);
            differential_shared[bit].add(whiten(local_shared[bit]),owner,256*chip,chip);
        }
    }
    outer_direct.finish(coefficients);outer_shared.finish(coefficients);
    for(unsigned bit=0;bit<2;++bit) {
        CHECK(whole_direct[bit].count==whole_shared[bit].count);
        CHECK(std::abs(whole_direct[bit].score()-whole_shared[bit].score())<1e-7);
        CHECK(std::abs(chips_direct[bit].score(whole_direct[bit])-chips_shared[bit].score(whole_shared[bit]))<1e-7);
        CHECK(chips_direct[bit].colored_excess(whole_direct[bit],40)==chips_shared[bit].colored_excess(whole_shared[bit],40));
        CHECK(outer_direct.contributing_terms[bit]==outer_shared.contributing_terms[bit]);
        CHECK(std::abs(outer_direct.score(bit)-outer_shared.score(bit))<1e-7);
        CHECK(outer_direct.can_admit(bit,40)==outer_shared.can_admit(bit,40));
        CHECK(differential_direct[bit].pairs==128 && differential_shared[bit].pairs==128);
        CHECK(std::abs(differential_direct[bit].score()-differential_shared[bit].score())<1e-7);
        for(unsigned q=0;q<4;++q)CHECK(std::abs(quarters_direct[q][bit].explained()-quarters_shared[q][bit].explained())<1e-7);
        for(double threshold:{1.,3.,10.,40.})CHECK((whole_direct[bit].score()>=threshold)==(whole_shared[bit].score()>=threshold));
    }
    CHECK(stream_pieces==513);
    CHECK(accepted>100 && rejected>0);
    d::ScheduledFrequencyPiece bad(8);std::array<C,2> z{};std::array<double,17> p{};
    auto nonfinite=d::CorrelationProjection{};nonfinite.xc=std::numeric_limits<double>::infinity();CHECK(!bad.add(nonfinite,z,p,false,0));
    d::ScheduledFrequencyPiece empty(8);for(unsigned i=0;i<8;++i)CHECK(empty.skip(i));
    CHECK(!empty.evaluate(d::scheduled_frequency_piece_phase(0,0,8,1500,1500,6000),0,8).supported);
    CHECK(!d::scheduled_frequency_piece_phase(0,0,1024,1500,1500,6000).supported);
    std::cout<<std::setprecision(17)<<"{\"stream_pieces\":"<<stream_pieces<<",\"accepted\":"<<accepted<<",\"fallback\":"<<rejected<<",\"maximum_normalized_dot_error\":"<<maxdot<<",\"maximum_relative_gram_error\":"<<maxgram<<",\"maximum_score_error\":"<<maxscore<<",\"maximum_outer_error\":"<<maxouter<<",\"maximum_certificate\":"<<maxcertificate<<"}\n";
}

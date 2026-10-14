#include "scheduled_acquisition_quarters.hpp"
#include "scheduled_acquisition_tile.hpp"
#include "datapump/pattern_pulse.hpp"
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>
using namespace datapump::modem;
namespace d=datapump::modem::detail;
namespace tile=d::scheduled_acquisition_tile;
namespace cert=d::scheduled_acquisition_certificate;
namespace quarters=d::scheduled_acquisition_quarters;
using C=std::complex<double>;
#define CHECK(c) do{if(!(c))throw std::runtime_error("check failed: " #c);}while(false)
constexpr std::size_t budget=32*1024*1024;
void merge(d::CorrelationFit& a,const d::CorrelationFit& b) {
    a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;
}
d::CorrelationFit original(const tile::Input& input,const tile::Fragment& fragment,
                            std::size_t start,unsigned bit) {
    d::CorrelationFit f;
    for(std::size_t i=0;i<fragment.rows.size();++i) {
        const auto at=start+i;if(!tile::selected(fragment,i) || !tile::present(input,at))continue;
        const auto x=static_cast<double>(input.pcm[at]);const auto q=fragment.rows[i][bit]*input.oscillator[at];
        f.xc+=x*q.real();f.xs+=x*q.imag();f.cc+=q.real()*q.real();f.ss+=q.imag()*q.imag();
        f.cs+=q.real()*q.imag();f.energy+=x*x;++f.count;
    }
    return f;
}
void accumulate_original(d::CorrelationFit& f,const tile::Input& input,const tile::Fragment& fragment,
                         std::size_t start,unsigned bit) {
    // One uninterrupted ascending ORIGINAL sample reduction across fragments.
    for(std::size_t i=0;i<fragment.rows.size();++i) {
        const auto at=start+i;if(!tile::selected(fragment,i) || !tile::present(input,at))continue;
        const auto x=static_cast<double>(input.pcm[at]);const auto q=fragment.rows[i][bit]*input.oscillator[at];
        f.xc+=x*q.real();f.xs+=x*q.imag();f.cc+=q.real()*q.real();f.ss+=q.imag()*q.imag();
        f.cs+=q.real()*q.imag();f.energy+=x*x;++f.count;
    }
}
cert::ReferenceErrors reference(const d::CorrelationFit& f,const cert::FitCertificate& c,unsigned bit) {
    return cert::scalar_reference_errors(f.count,c.energy_upper,
        cert::add(cert::add(f.cc,f.ss),2*c.gram_radius[bit]),1e-8/std::max(1.,.5*(static_cast<double>(f.count)-2)));
}
void ownership() {
    for(const auto total:{1024ULL,1027ULL,1000000000001ULL}) {
        for(unsigned k=1;k<4;++k) {
            const auto edge=static_cast<long double>(d::drift_boundary(k,total,4));
            CHECK(quarters::owner(std::nextafter(edge,-std::numeric_limits<long double>::infinity()),total)==k-1);
            CHECK(quarters::owner(edge,total)==k);
            CHECK(quarters::owner(std::nextafter(edge,std::numeric_limits<long double>::infinity()),total)==k);
        }
        CHECK(quarters::owner(0,total)==0);CHECK(!quarters::owner(-.01L,total));CHECK(!quarters::owner(total,total));
    }
    const std::array<long double,6> positions{0,255.999L,256,513,770,std::numeric_limits<long double>::quiet_NaN()};
    const std::array<std::uint8_t,6> usable{1,1,1,1,1,0};std::array<std::uint8_t,6> owners{},mask{};
    CHECK(quarters::partition(positions,usable,1027,owners));
    CHECK((owners==std::array<std::uint8_t,6>{0,0,1,2,3,quarters::not_observed}));
    for(unsigned k=0;k<4;++k) {
        CHECK(quarters::mask(owners,k,mask));for(unsigned i=0;i<mask.size();++i)CHECK(mask[i]==(owners[i]==k));
    }
    CHECK(!quarters::partition(positions,{},1027,owners));CHECK(!quarters::mask(owners,4,mask));
    const std::array<std::uint8_t,1> corrupt{2};const std::array<long double,1> zero{0};std::array<std::uint8_t,1> out{};
    CHECK(!quarters::partition(zero,corrupt,1027,out));
}
void corner_bounds() {
    d::CorrelationFit f{1,.2,2,2,0,10,64};cert::FitCertificate c;
    c.valid=true;c.dot_radius={0,0};c.gram_radius={0,0};c.energy_lower=c.energy_upper=10;
    cert::ReferenceErrors r{0,0,0,0,true};
    const auto exact=quarters::explained(f,c,0,r);CHECK(exact.lo<=f.explained() && exact.hi>=f.explained());
    CHECK(std::isinf(quarters::explained(f,c,0,{}).hi));
    auto singular=f;singular.cc=singular.ss=singular.cs=1;
    const auto s=quarters::explained(singular,c,0,r);CHECK(s.lo==0 && s.hi>=singular.explained());
    auto straddle=c;straddle.gram_radius={3,3};const auto uncertain=quarters::explained(f,straddle,0,r);CHECK(uncertain.lo==0 && uncertain.hi>=f.explained());
    auto empty=f;empty.count=0;CHECK(quarters::explained(empty,c,0,r).hi==0);
    std::array<cert::Interval,4> terms{{{1,2},{3,4},{5,6},{7,8}}};const auto retained=quarters::retained(terms);
    CHECK(retained.lo<=1+3+5 && retained.hi>=2+4+6);
    terms[1]={0,cert::infinity};CHECK(std::isinf(quarters::retained(terms).hi));
    std::array<quarters::Quarter,4> q;
    for(auto& quarter:q) {quarter.fits={f,f};quarter.certificate=c;quarter.reference={r,r};}
    auto whole=f;whole.count=4*f.count;whole.xc*=4;whole.xs*=4;whole.cc*=4;whole.ss*=4;whole.cs*=4;whole.energy*=4;
    auto wc=c;wc.energy_lower=wc.energy_upper=40;const quarters::DriftReferenceErrors dr{0,true};
    CHECK(std::isfinite(quarters::combined_upper(whole,wc,0,r,q,dr)));
    auto negative=r;negative.dot=-1;CHECK(std::isinf(quarters::combined_upper(whole,wc,0,negative,q,dr)));
    negative=r;negative.gram=-1;CHECK(std::isinf(quarters::combined_upper(whole,wc,0,negative,q,dr)));
    negative=r;negative.energy=-1;CHECK(std::isinf(quarters::combined_upper(whole,wc,0,negative,q,dr)));
    auto bad=wc;bad.energy_lower=bad.energy_upper+1;CHECK(std::isinf(quarters::combined_upper(whole,bad,0,r,q,dr)));
    bad=wc;bad.dot_radius[0]=-1;CHECK(std::isinf(quarters::combined_upper(whole,bad,0,r,q,dr)));
    bad=wc;bad.gram_radius[0]=-1;CHECK(std::isinf(quarters::combined_upper(whole,bad,0,r,q,dr)));
    bad=wc;bad.energy_lower=bad.energy_upper=0;CHECK(std::isinf(quarters::combined_upper(whole,bad,0,r,q,dr)));
    bad=wc;bad.energy_lower=bad.energy_upper=1e-40;CHECK(std::isinf(quarters::combined_upper(whole,bad,0,r,q,dr)));
    bad=wc;bad.energy_lower=std::numeric_limits<double>::quiet_NaN();CHECK(std::isinf(quarters::combined_upper(whole,bad,0,r,q,dr)));
    auto long_count=whole;long_count.count=0x20000000000000ULL;CHECK(std::isinf(quarters::combined_upper(long_count,wc,0,r,q,dr)));
    for(auto& quarter:q) {quarter.fits[0].count=quarter.fits[1].count=2;}
    whole.count=8;CHECK(quarters::drift_upper(whole,wc,0,r,q,dr)==0);
    const auto merged=quarters::merge(q);CHECK(merged.certificate.valid && merged.fits[0].count==8);
    ++q[0].fits[1].count;CHECK(!quarters::merge(q).certificate.valid);

}
template<class T> void observations() {
    std::size_t noise_rejects=0,signal_refines=0,isolated_rejects=0;double max_excess=0;
    for(const auto total:{1024ULL,1027ULL})for(unsigned seed=0;seed<6;++seed)for(unsigned kind=0;kind<3;++kind) {
        const tile::Shape shape{37,128};tile::Plan<T> plan(shape);CHECK(plan.certificate.valid);
        const std::array<std::size_t,5> starts{0,1,9,18,36};
        tile::Accumulator<T> whole(plan,10000,starts,budget);
        std::array<std::unique_ptr<tile::Accumulator<T>>,4> accumulators;
        for(auto& p:accumulators)p=std::make_unique<tile::Accumulator<T>>(plan,10000,starts,budget);
        std::array<std::array<std::array<d::CorrelationFit,2>,4>,5> truth{};
        std::array<std::array<d::CorrelationFit,2>,5> chronological{};
        std::mt19937_64 rng(73129+seed);std::normal_distribution<double> random;
        const auto chip=total==1024?16ULL:13ULL;
        std::vector<std::array<C,2>> coefficients(total/chip+20);
        for(auto& row:coefficients)row={C{.2*random(rng),.2*random(rng)},C{.2*random(rng),.2*random(rng)}};
        for(unsigned part=0;part<8;++part) {
            const auto first=10000+40000ULL*part;
            std::vector<float> pcm(shape.start_span+shape.maximum_fragment-1);
            std::vector<C> oscillator(pcm.size());std::vector<std::array<C,2>> rows(128);
            std::vector<long double> source(rows.size());std::vector<std::uint8_t> usable(rows.size(),1),owner(rows.size()),mask(rows.size());
            std::vector<std::uint8_t> observed(pcm.size(),1);
            for(std::size_t i=0;i<rows.size();++i) {
                // Original source coordinates across physically separated bursts;
                // finite pulse evaluation is independent of quarter ownership.
                source[i]=128.L*part+i+.3125L;
                if(source[i]>=total)usable[i]=0;
                const auto q=static_cast<double>(source[i]/chip)-.5;
                for(std::size_t k=0;k<coefficients.size();++k) {
                    const auto weight=pattern_pulse(q-static_cast<double>(k));
                    rows[i][0]+=coefficients[k][0]*weight;rows[i][1]+=coefficients[k][1]*weight;
                }
                for(auto& z:rows[i])if(std::abs(z)>.992)z*=.992/std::abs(z);
                // Public guard/ramp modulation belongs to both candidate bases.
                const auto gain=i%29==0?0.:i%7==0?.4:1.;if(gain==0)usable[i]=0;
                rows[i][0]*=gain;rows[i][1]*=gain;
            }
            CHECK(quarters::partition(source,usable,total,owner));
            for(std::size_t i=0;i<pcm.size();++i) {
                oscillator[i]=std::polar(1.+i*1e-13,static_cast<double>(first+i)*.732);
                pcm[i]=static_cast<float>(.2*random(rng));if(i%31==0)observed[i]=0;
                if(i<rows.size() && usable[i]) {
                    const auto k=*quarters::owner(source[i],total);
                    const auto phase=std::polar(1.,k*std::numbers::pi/2);
                    const auto amplitude=kind==1?3.:kind==2 && k==0?3.:0.;
                    pcm[i]+=static_cast<float>(amplitude*(rows[i][0]*oscillator[i]*phase).real());
                }
            }
            const tile::Input input{pcm,oscillator,observed,first};tile::PreparedInput<T> pi(plan,input,budget);
            const tile::Fragment full{rows,usable,40000ULL*part};tile::PreparedFragment<T> pf(plan,full,budget);whole.add(pi,pf);
            for(unsigned at=0;at<starts.size();++at)for(unsigned bit=0;bit<2;++bit)
                accumulate_original(chronological[at][bit],input,full,starts[at],bit);
            for(unsigned k=0;k<4;++k) {
                CHECK(quarters::mask(owner,k,mask));
                const tile::Fragment f{rows,mask,40000ULL*part};tile::PreparedFragment<T> prepared(plan,f,budget);
                accumulators[k]->add(pi,prepared);
                for(unsigned at=0;at<starts.size();++at)for(unsigned bit=0;bit<2;++bit)
                    accumulate_original(truth[at][k][bit],input,f,starts[at],bit);
            }
        }
        const auto wf=whole.finish();std::array<std::vector<tile::Value>,4> qf;
        for(unsigned k=0;k<4;++k) {const auto finished=accumulators[k]->finish();qf[k].assign(finished.begin(),finished.end());}
        for(unsigned at=0;at<starts.size();++at) {
            std::array<quarters::Quarter,4> q;
            for(unsigned k=0;k<4;++k) {
                q[k].fits=qf[k][at].fits;q[k].certificate=qf[k][at].certificate;
                for(unsigned bit=0;bit<2;++bit)q[k].reference[bit]=reference(q[k].fits[bit],q[k].certificate,bit);
            }
            const auto merged=quarters::merge(q);CHECK(merged.certificate.valid);
            for(unsigned bit=0;bit<2;++bit) {
                d::CorrelationFit w;std::array<double,4> projected{};double sum=0,largest=0;
                for(unsigned k=0;k<4;++k) {
                    merge(w,truth[at][k][bit]);projected[k]=truth[at][k][bit].explained();sum+=projected[k];largest=std::max(largest,projected[k]);
                    const auto e=quarters::explained(q[k].fits[bit],q[k].certificate,bit,q[k].reference[bit]);
                    CHECK(e.lo<=projected[k] && e.hi>=projected[k]);
                }
                CHECK(w.count==chronological[at][bit].count);w=chronological[at][bit];
                const auto retained=sum-largest;
                const auto full_drift=d::drift_evidence(retained,w.energy,static_cast<double>(w.count),4,true);
                const auto fallback=d::drift_evidence(retained,w.energy,std::min(static_cast<double>(w.count),4.*w.count/chip),4,true);
                const auto current=d::combine_drift_evidence(w.score(),std::max(full_drift,fallback),4);
                const auto original_reference=reference(wf[at].fits[bit],wf[at].certificate,bit);
                const quarters::DriftReferenceErrors score_contract{1e-8,true};
                const auto upper=quarters::combined_upper(wf[at].fits[bit],wf[at].certificate,bit,original_reference,q,score_contract);
                CHECK(upper>=current);max_excess=std::max(max_excess,upper-current);
                const auto merged_reference=reference(merged.fits[bit],merged.certificate,bit);
                const auto merged_upper=quarters::combined_upper(merged.fits[bit],merged.certificate,bit,merged_reference,q,score_contract);
                CHECK(merged_upper>=current);CHECK(merged.fits[bit].count==w.count);
                CHECK(std::abs(C{merged.fits[bit].xc-w.xc,merged.fits[bit].xs-w.xs})<=
                    cert::add(merged.certificate.dot_radius[bit],merged_reference.dot));
                const auto gram_trace=(merged.fits[bit].cc-w.cc)+(merged.fits[bit].ss-w.ss);
                const auto gram_delta=std::hypot((merged.fits[bit].cc-w.cc)-(merged.fits[bit].ss-w.ss),2*(merged.fits[bit].cs-w.cs));
                CHECK(.5*(std::abs(gram_trace)+gram_delta)<=cert::add(merged.certificate.gram_radius[bit],merged_reference.gram));
                CHECK(std::isinf(quarters::combined_upper(wf[at].fits[bit],wf[at].certificate,bit,original_reference,q,{})));
                CHECK(std::isinf(quarters::combined_upper(wf[at].fits[bit],wf[at].certificate,bit,original_reference,q,score_contract,true)));
                auto wrong=q;++wrong[2].fits[bit].count;
                CHECK(std::isinf(quarters::combined_upper(wf[at].fits[bit],wf[at].certificate,bit,original_reference,wrong,score_contract)));
                if(kind==0 && upper<32)++noise_rejects;
                if(kind==1 && current>=32) {CHECK(upper>=32);++signal_refines;}
                if(kind==2 && upper<32)++isolated_rejects;
            }
        }
    }
    CHECK(noise_rejects>50);CHECK(signal_refines>0);CHECK(isolated_rejects>10);
    std::cout<<(std::is_same_v<T,float>?"FP32":"FP64")<<" noise rejects="<<noise_rejects<<" quarter-phase signal refines="<<signal_refines
        <<" isolated-quarter rejects="<<isolated_rejects<<" max conservative excess="<<max_excess<<'\n';
}
int main() {try {ownership();corner_bounds();observations<float>();observations<double>();
    std::cout<<"scheduled acquisition quarter certificate checks passed\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}

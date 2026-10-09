#include "datapump/channel.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "pattern_correlator_batch.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <string>
#include <vector>

using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using C=std::complex<double>;
using Clock=std::chrono::steady_clock;
constexpr long double tau=2*std::numbers::pi_v<long double>;
void require(bool b,const std::string& s){if(!b)throw Error(s);}
struct Args {
    std::string csv;
    std::uint64_t seed=719,waveform_seed=671;
    unsigned bit=0,trials=20000,verify=8;
    std::uint64_t symbol=1280;
    double ppm=0,offset=0,diffusion=0,interference=0,interference_frequency=17;
    double low=-10,high=20,seconds=110;
};
Args args(int argc,char**argv){
    Args a;
    for(int i=1;i<argc;i+=2){require(i+1<argc,"flag requires value");std::string f=argv[i],v=argv[i+1];
        if(f=="--csv")a.csv=v;
        else if(f=="--trials")a.trials=std::stoul(v);
        else if(f=="--bit")a.bit=std::stoul(v);
        else if(f=="--seed")a.seed=std::stoull(v);
        else if(f=="--waveform-seed")a.waveform_seed=std::stoull(v);
        else if(f=="--verify-pcm-seeds")a.verify=std::stoul(v);
        else if(f=="--symbol-samples")a.symbol=std::stoull(v);
        else if(f=="--clock-ppm")a.ppm=std::stod(v);
        else if(f=="--frequency-offset")a.offset=std::stod(v);
        else if(f=="--phase-diffusion")a.diffusion=std::stod(v);
        else if(f=="--interference-amplitude")a.interference=std::stod(v);
        else if(f=="--interference-frequency")a.interference_frequency=std::stod(v);
        else if(f=="--min-cn0")a.low=std::stod(v);
        else if(f=="--max-cn0")a.high=std::stod(v);
        else if(f=="--seconds-limit")a.seconds=std::stod(v);
        else throw Error("unsupported flag "+f);
    }
    require(!a.csv.empty()&&a.bit<2&&a.trials&&a.trials<=20000&&a.verify<=32,"invalid output/count");
    require(a.symbol>=1024&&a.symbol<=4096&&a.low<a.high&&a.seconds>0&&a.seconds<=120,"bounded geometry/range required");
    require(std::abs(a.ppm)<=100&&std::abs(a.offset)<=.1&&a.diffusion>=0&&a.diffusion<=1,"impairment outside conditional scope");
    return a;
}
struct Q {
    long double c=0,b=0,a=0;
    long double at(long double x)const{return (a*x+b)*x+c;}
    Q operator+(Q y)const{return {c+y.c,b+y.b,a+y.a};}
    Q operator-(Q y)const{return {c-y.c,b-y.b,a-y.a};}
    Q operator*(long double y)const{return {c*y,b*y,a*y};}
};
Q square(long double n,long double s){return {n*n,2*n*s,s*s};}
Q product(long double n,long double s,long double m,long double t){return {n*m,n*t+s*m,s*t};}
struct D {double n=0,s=0;};
struct Fit {
    CorrelationFit gram;
    D x,y;
};
struct Projection {Q q;unsigned rank=0;};
Projection projection(const Fit& f){
    const auto& g=f.gram;const auto trace=g.cc+g.ss;
    if(!g.count||trace<=1e-30)return {};
    const auto det=g.cc*g.ss-g.cs*g.cs;
    if(g.count>1&&det>1e-12*trace*trace)
        return {(square(f.x.n,f.x.s)*g.ss+square(f.y.n,f.y.s)*g.cc-product(f.x.n,f.x.s,f.y.n,f.y.s)*(2*g.cs))*(1/det),2};
    const auto angle=.5*std::atan2(2*g.cs,g.cc-g.ss);
    const auto lambda=.5*(trace+std::hypot(g.cc-g.ss,2*g.cs));
    const auto x=std::cos(angle),y=std::sin(angle);
    return {square(x*f.x.n+y*f.y.n,x*f.x.s+y*f.y.s)*(1/lambda),1};
}
struct PulseTerm {std::uint64_t chip;double value;};
struct Model {
    Config config;
    PatternSearch search;
    std::vector<float> signal;
    std::vector<std::array<C,2>> basis;
    std::vector<C> oscillator;
    std::vector<std::vector<PulseTerm>> pulse;
    std::vector<std::array<C,2>> coefficients;
    std::vector<std::size_t> cell;
    std::array<Fit,2> full;
    std::vector<std::array<Fit,2>> cells;
    std::uint64_t first=0,end=0,count=0;
    long double origin=0,rate=1;
    double frequency=0,threshold=-std::log(1e-10)+std::log(4.);
    PatternCorrelatorWork work;
    std::array<Q,2> deterministic_lof;
};
Model prepare(const Args& a){
    Model m;m.config.sample_rate=64;m.config.carrier_hz=16;m.config.bandwidth_hz=3.2;
    m.config.spreading_factor=32;m.config.dsss_factor=10;m.config.scramble=true;
    m.config.stream_epoch=1800000000;m.config.integration_seconds=static_cast<double>(a.symbol)/64;
    for(unsigned i=0;i<8&&symbol_sample_count(m.config)>a.symbol;++i)m.config.integration_seconds=std::nextafter(m.config.integration_seconds,0.);
    for(std::size_t i=0;i<m.config.spreading_seed.size();++i){m.config.spreading_seed[i]=3*i+7;m.config.dsss_seed[i]=13*i+29;}
    validate(m.config);require(pattern_chip_samples(m.config)==4&&symbol_sample_count(m.config)==a.symbol,"resolved geometry changed");
    m.rate=1+static_cast<long double>(a.ppm)*1e-6L;m.frequency=16*static_cast<double>(m.rate)+a.offset;
    ChannelConfig channel_config;channel_config.seed=a.waveform_seed;channel_config.snr_db=300;
    channel_config.clock_error_ppm=a.ppm;channel_config.frequency_offset_hz=a.offset;channel_config.phase_noise_degrees_per_sqrt_second=a.diffusion;
    SampledSimulationChannel channel(m.config,channel_config);
    StreamingTransmitter tx(RawBits{Bytes{static_cast<std::uint8_t>(a.bit)}},m.config,8*1024*1024);
    m.origin=channel.startup_offset_samples()+static_cast<long double>(training_sample_count(m.config)+pattern_pulse_padding_samples(m.config))/m.rate;
    m.origin=static_cast<long double>(static_cast<double>(m.origin/64))*64;
    m.first=static_cast<std::uint64_t>(std::ceil(m.origin));m.end=static_cast<std::uint64_t>(std::ceil(m.origin+a.symbol/m.rate));m.count=m.end-m.first;
    m.signal.resize(m.end);std::size_t pos=0;
    while(pos<m.end){auto n=channel.read(tx,std::span(m.signal).subspan(pos));require(n>0,"finite source ended before payload");pos+=n;}
    m.search.hypotheses={{m.frequency-16,a.ppm}};m.search.start_offset_seconds=static_cast<double>(m.origin/64);
    m.search.start_uncertainty_seconds=0;m.search.search_stream_phases=false;m.search.compact_clock_search=true;
    m.search.worker_threads=1;m.search.retain_score=0;m.search.chunk_bits=1;m.search.drift_tolerant=false;
    PatternCorrelator probe(m.config,m.search,8*1024*1024);m.work=probe.work();
    require(m.work.hypotheses==1&&m.work.phase_groups==1&&m.work.drift_sections==1&&!m.work.differential_window_samples,"conditional detector geometry changed");
    require(m.work.backend==(a.symbol%16?PatternCorrelationBackend::raw:PatternCorrelationBackend::pulse),"automatic backend changed");
    PatternCode pattern(m.config,m.config.stream_epoch);const auto chips=pattern.chips_per_symbol();
    m.coefficients.resize(chips);for(std::size_t j=0;j<chips;++j)m.coefficients[j]=pattern.values(j);
    m.basis.resize(m.count);m.oscillator.resize(m.count);m.pulse.resize(m.count);m.cell.resize(m.count);
    m.cells.resize(chips);
    C oscillator{};const auto step=std::polar(1.,static_cast<double>(tau*m.frequency/64));
    for(std::uint64_t n=m.first-m.first%32;n<m.end;++n){
        if(n%32==0)oscillator=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(n)*tau*m.frequency/64,tau)));
        if(n>=m.first){const auto i=n-m.first;const auto within=static_cast<double>((n-m.origin)*m.rate);
            const auto values=pattern.shaped_values(0,within);m.oscillator[i]=oscillator;
            m.cell[i]=CorrelationChipEvidence::index(n,m.origin,m.rate,4);
            require(m.cell[i]<m.cells.size(),"chip cell out of range");
            for(unsigned b=0;b<2;++b){const auto v=oscillator*values[b];m.basis[i][b]=v;
                for(auto* fit:{&m.full[b],&m.cells[m.cell[i]][b]}){fit->gram.cc+=v.real()*v.real();fit->gram.ss+=v.imag()*v.imag();fit->gram.cs+=v.real()*v.imag();++fit->gram.count;}}
            pattern_pulse_each(within,a.symbol,4,[&](std::uint64_t j,double v){m.pulse[i].push_back({j,v});});
        }oscillator*=step;
    }
    return m;
}
double cn0(long double alpha){return static_cast<double>(20*std::log10(alpha)+10*std::log10(nominal_signal_power*32));}
long double amplitude(double db){return std::pow(10.L,db/20.L)/std::sqrt(static_cast<long double>(nominal_signal_power)*32);}
struct Draw {
    std::array<Fit,2> full;
    std::array<Q,2> projected,coherent;
    std::array<std::uint64_t,2> rank{},coherent_rank{};
    std::array<bool,2> nested{true,true};
    Q energy;
    std::vector<double> noise;
    std::vector<C> pulse_signal,pulse_noise;
    std::array<Q,2> outer_numerator,outer_variance;
    bool outer_ready=false;
};
Draw draw(const Model& m,const Args& a,std::uint64_t seed,bool deterministic=false){
    Draw d;d.full=m.full;auto cells=m.cells;d.noise.resize(m.end);
    std::mt19937_64 random(seed);std::normal_distribution<double> normal;
    for(auto& n:d.noise)n=deterministic?0:normal(random);
    for(std::size_t i=0;i<m.count;++i){const auto n=m.first+i;
        const auto s=m.signal[n];const auto noise=d.noise[n]+a.interference*std::cos(static_cast<double>(tau*a.interference_frequency*n/64+.47L));
        d.noise[n]=noise;d.energy=d.energy+square(noise,s);
        for(unsigned b=0;b<2;++b){const auto v=m.basis[i][b];
            for(auto* f:{&d.full[b],&cells[m.cell[i]][b]}){f->x.n+=noise*v.real();f->x.s+=s*v.real();f->y.n+=noise*v.imag();f->y.s+=s*v.imag();}}
    }
    for(unsigned b=0;b<2;++b){const auto full=projection(d.full[b]);d.coherent[b]=full.q;d.coherent_rank[b]=full.rank;
        for(const auto& c:cells){const auto p=projection(c[b]);d.projected[b]=d.projected[b]+p.q;d.rank[b]+=p.rank;d.nested[b]=d.nested[b]&&p.rank>=std::min<std::uint64_t>(2,c[b].gram.count);}}
    return d;
}
double limit(const Model& m,const Draw& d,unsigned b){
    const auto r=static_cast<double>(d.rank[b]-d.coherent_rank[b]),degrees=static_cast<double>(m.count-d.rank[b]);
    const auto delta=4*m.threshold+32,lower=degrees-2*std::sqrt(degrees*30);
    if(r<=0||lower<=0)return std::numeric_limits<double>::infinity();
    const auto upper=r+delta+2*std::sqrt((r+2*delta)*30)+60;
    return std::max(2.,(upper/r)/(lower/degrees));
}
bool active(const Model&m,const Draw&d,unsigned b,long double alpha){
    if(!d.nested[b]||d.rank[b]<=d.coherent_rank[b]||d.rank[b]>=m.count)return false;
    const auto E=d.energy.at(alpha);if(E<=1e-30)return false;
    const auto P=std::clamp(d.projected[b].at(alpha),0.L,E),Qv=std::clamp(d.coherent[b].at(alpha),0.L,E);
    const auto residual=E-P,lack=std::max(0.L,P-Qv);
    return residual<=0||lack*(m.count-d.rank[b])>limit(m,d,b)*residual*(d.rank[b]-d.coherent_rank[b]);
}
void outer(const Model&m,Draw&d){
    if(d.outer_ready)return;d.outer_ready=true;const auto chips=m.coefficients.size();
    d.pulse_signal.resize(chips);d.pulse_noise.resize(chips);
    for(std::size_t i=0;i<m.count;++i)for(const auto p:m.pulse[i]){
        d.pulse_signal[p.chip]+=p.value*static_cast<double>(m.signal[m.first+i])*m.oscillator[i];
        d.pulse_noise[p.chip]+=p.value*d.noise[m.first+i]*m.oscillator[i];}
    for(unsigned b=0;b<2;++b){C ns{},ss{};
        for(std::size_t j=0;j<chips;++j){const auto c=m.coefficients[j][b],n=c*d.pulse_noise[j],s=c*d.pulse_signal[j];ns+=n;ss+=s;
            d.outer_variance[b]=d.outer_variance[b]+square(n.real(),s.real())+square(n.imag(),s.imag());}
        d.outer_numerator[b]=square(ns.real(),ss.real())+square(ns.imag(),ss.imag());}
}
bool outer_pass(const Model&m,const Draw&d,unsigned b,long double alpha){
    std::uint64_t count=0;
    for(std::size_t j=0;j<m.coefficients.size();++j)if(std::norm(m.coefficients[j][b])*std::norm(d.pulse_noise[j]+static_cast<double>(alpha)*d.pulse_signal[j])>0)++count;
    if(count<=m.threshold+CorrelationOuterEvidence::log_constant)return true; // impossible gate retains preceding detector
    return d.outer_numerator[b].at(alpha)>=(m.threshold+CorrelationOuterEvidence::log_constant)*d.outer_variance[b].at(alpha);
}
std::array<double,2> scores(const Draw&d,long double alpha){
    std::array<double,2> s{};const auto E=d.energy.at(alpha);
    for(unsigned b=0;b<2;++b){const auto& g=d.full[b].gram;const auto det=g.cc*g.ss-g.cs*g.cs;
        if(g.count>2&&E>1e-30&&det>1e-12*std::max(1.,g.cc*g.ss)){
            const auto fraction=std::clamp(d.coherent[b].at(alpha)/E,0.L,1.L-1e-15L);
            s[b]=static_cast<double>(-.5L*(g.count-2)*std::log1p(-fraction));}}
    return s;
}
bool accepted(const Model&m,const Args&a,Draw&d,long double alpha,bool guard){
    const auto s=scores(d,alpha);const auto b=s[1]>s[0]?1U:0U;
    if(b!=a.bit||s[b]<m.threshold||s[b]-s[1-b]<1)return false;
    if(!guard||!active(m,d,b,alpha))return true;
    outer(m,d);return outer_pass(m,d,b,alpha);
}
void roots(const Q&q,long double low,long double high,std::vector<long double>&out){
    const auto scale=std::max({std::abs(q.c),std::abs(q.b),std::abs(q.a),1e-100L});
    const auto put=[&](long double x){if(std::isfinite(x)&&x>low&&x<high)out.push_back(x);};
    if(std::abs(q.a)<1e-22L*scale){if(std::abs(q.b)>=1e-22L*scale)put(-q.c/q.b);return;}
    auto discriminant=q.b*q.b-4*q.a*q.c;if(discriminant<0)return;
    const auto v=-.5L*(q.b+std::copysign(std::sqrt(discriminant),q.b));
    put(v/q.a);if(v)put(q.c/v);else put(-q.b/(2*q.a));
}
struct Crossing {double value=std::numeric_limits<double>::quiet_NaN();bool bracketed=false,monotone=false;unsigned active_intervals=0;};
std::array<Crossing,2> crossings(const Model&m,const Args&a,Draw&d){
    const auto low=amplitude(a.low),high=amplitude(a.high);std::vector<long double> edges{low,high};
    roots(d.energy,low,high,edges);
    for(unsigned b=0;b<2;++b){const auto P=d.projected[b],Qv=d.coherent[b],E=d.energy;
        const auto threshold_fraction=-std::expm1(-2*m.threshold/(m.count-2));
        roots(Qv-E*threshold_fraction,low,high,edges);
        const auto margin=std::exp(2.L/(m.count-2));
        roots((E-d.coherent[1-b])-(E-Qv)*margin,low,high,edges);
        for(const auto q:{P,Qv,P-E,Qv-E,P-Qv})roots(q,low,high,edges);
        if(std::isfinite(limit(m,d,b))){const auto r=static_cast<long double>(d.rank[b]-d.coherent_rank[b]),degrees=static_cast<long double>(m.count-d.rank[b]);
            roots((P-Qv)*degrees-(E-P)*(limit(m,d,b)*r),low,high,edges);}}
    std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());
    bool any=false;for(std::size_t i=1;i<edges.size();++i){const auto x=(edges[i-1]+edges[i])/2;for(unsigned b=0;b<2;++b)any=any||active(m,d,b,x);}
    if(any){outer(m,d);
        for(unsigned b=0;b<2;++b){roots(d.outer_numerator[b]-d.outer_variance[b]*(m.threshold+CorrelationOuterEvidence::log_constant),low,high,edges);
            for(std::size_t j=0;j<d.pulse_signal.size();++j){const auto n=d.pulse_noise[j],s=d.pulse_signal[j];roots(square(n.real(),s.real())+square(n.imag(),s.imag()),low,high,edges);}}
        std::sort(edges.begin(),edges.end());edges.erase(std::unique(edges.begin(),edges.end()),edges.end());}
    std::array<Crossing,2> result;
    for(unsigned variant=0;variant<2;++variant){bool seen=false,rejected_after=false;auto& r=result[variant];
        if(accepted(m,a,d,low,variant)){seen=true;r.value=a.low;}
        for(std::size_t i=1;i<edges.size();++i){const auto left=edges[i-1],right=edges[i],mid=(left+right)/2;
            const bool pass=accepted(m,a,d,mid,variant);
            if(variant&&active(m,d,a.bit,mid))++r.active_intervals;
            if(pass&&!seen){long double lo=left,hi=mid;for(unsigned k=0;k<60;++k){const auto x=(lo+hi)/2;if(accepted(m,a,d,x,variant))hi=x;else lo=x;}
                r.value=cn0((lo+hi)/2);seen=true;}
            if(!pass&&seen)rejected_after=true;}
        for(std::size_t i=1;i+1<edges.size();++i){
            const bool before=accepted(m,a,d,(edges[i-1]+edges[i])/2,variant);
            const bool after=accepted(m,a,d,(edges[i]+edges[i+1])/2,variant);
            if(before==after&&accepted(m,a,d,edges[i],variant)!=before)rejected_after=true;
        }
        r.bracketed=seen&&r.value>a.low&&accepted(m,a,d,high,variant);r.monotone=r.bracketed&&!rejected_after;}
    return result;
}
struct Replay {PatternEvidence evidence;bool accepted=false;};
Replay replay(const Model&m,const Args&a,const Draw&d,double db,bool guard,bool raw,const Config*other=nullptr){
    auto search=m.search;search.outer_presence_guard=guard;
    PatternCorrelatorOptions options;options.raw_reference=raw;
    PatternCorrelator receiver(other?*other:m.config,search,8*1024*1024,options);
    auto work=receiver.work();require(work.hypotheses==1&&work.phase_groups==1&&work.drift_sections==1&&!work.differential_window_samples,"replay changed detector geometry");
    std::vector<float> pcm(m.end);const auto alpha=amplitude(db);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=static_cast<float>(alpha*m.signal[i]+d.noise[i]);
    Replay result;
    for(std::size_t pos=0;pos<pcm.size();){const auto n=std::min<std::size_t>(503,pcm.size()-pos);receiver.push(std::span(pcm).subspan(pos,n));pos+=n;
        for(const auto& event:receiver.take_bursts()){require(!event.complete,"one full symbol/EOF manufactured absence");
            if(!event.bits.empty()){require(event.bits.size()==1&&event.end_sample==m.end,"conditional publication endpoint changed");result.accepted=event.bits[0]==a.bit;}}}
    const auto candidates=receiver.candidates();require(candidates.size()==1,"conditional replay did not score exactly one full symbol");
    result.evidence=candidates[0];require(std::abs(result.evidence.admission_threshold-m.threshold)<1e-12,"paired threshold changed");
    require(guard?result.evidence.outer_presence_score>0:result.evidence.outer_presence_score==0,
        "guard replay did not exercise allocated outer evidence");
    receiver.finish();require(receiver.take_bursts().empty(),"EOF completed conditional capture");return result;
}
double replay_crossing(const Model&m,const Args&a,const Draw&d,double predicted,bool guard,bool raw){
    double low=predicted-.02,high=predicted+.02;
    require(!replay(m,a,d,low,guard,raw).accepted&&replay(m,a,d,high,guard,raw).accepted,"actual PCM crossing did not bracket model");
    for(unsigned i=0;i<23;++i){const auto mid=(low+high)/2;if(replay(m,a,d,mid,guard,raw).accepted)high=mid;else low=mid;}
    return (low+high)/2;
}
void positive_control(){
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=100;c.spreading_factor=64;
    c.scramble=true;c.dsss_factor=10;c.stream_epoch=1789312671;
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=13*i+29;}
    PatternTransmitter tx(Bytes{0,0,1},c,c.stream_epoch,0,false);
    std::vector<C> analytic(tx.total_samples());tx.read_analytic(analytic);
    const auto padding=pattern_pulse_padding_samples(c);const auto count=analytic.size()-2*padding;
    std::vector<float> pcm(137+count);std::mt19937_64 random(671);std::normal_distribution<double> noise(0,.05);
    for(auto& x:pcm)x=static_cast<float>(noise(random));
    for(std::size_t i=0;i<count;++i)pcm[137+i]+=static_cast<float>((analytic[padding+i]*std::polar(1.,.73)).real());
    c.dsss_seed[0]^=0x40;unsigned admitted[2]{};bool rejected=false;
    for(unsigned guard=0;guard<2;++guard){PatternSearch s;s.frequency_offsets_hz={0};s.compact_clock_search=true;
        s.start_offset_seconds=137./6000;s.start_uncertainty_seconds=.01;s.worker_threads=1;s.outer_presence_guard=guard;
        PatternCorrelator receiver(c,s,8*1024*1024);
        for(std::size_t pos=0;pos<pcm.size();){const auto n=std::min<std::size_t>(503,pcm.size()-pos);receiver.push(std::span(pcm).subspan(pos,n));pos+=n;
            for(const auto& e:receiver.take_bursts())admitted[guard]+=e.bits.size();}
        for(const auto& e:receiver.candidates())rejected=rejected||(guard&&e.score>=e.admission_threshold&&e.outer_presence_active&&e.outer_presence_score<e.admission_threshold);
    }
    require(admitted[0]>0&&admitted[1]==0&&rejected,"strong wrong-outer control did not exercise an actual guard rejection");
    std::cerr<<"positive_control_guard_off_accepted_bits="<<admitted[0]<<" guard_on_accepted_bits="<<admitted[1]<<" strong_rejection_verified=1\n";
}
int main(int argc,char**argv){try{
    const auto a=args(argc,argv);const auto begun=Clock::now();positive_control();const auto m=prepare(a);
    const auto deterministic=draw(m,a,0,true);
    const auto mismatch_polynomial=deterministic.projected[a.bit]-deterministic.coherent[a.bit];
    const auto maximum_mismatch=std::max({0.L,mismatch_polynomial.at(amplitude(a.low)),mismatch_polynomial.at(amplitude(a.high))});
    std::ofstream out(a.csv);require(bool(out),"cannot write claimed output");out<<std::setprecision(17);
    std::vector<std::pair<double,std::uint64_t>> population;
    out<<"seed,bit,guard_off_cn0,guard_on_cn0,off_bracketed,on_bracketed,off_monotone,on_monotone,active_intervals,mismatch_energy_at_upper,activation_allowance,activation_probability_bound,actual_raw_off_cn0,actual_raw_on_cn0,actual_auto_off_cn0,actual_auto_on_cn0,actual_maximum_root_error_db,sample_rate,carrier_hz,inner_bandwidth_hz,dsss_factor,chip_samples,symbol_samples,observation_samples,actual_origin_samples,frequency_offset_hz,clock_ppm,phase_diffusion,waveform_seed,interference_amplitude,interference_frequency,threshold,hypotheses,phase_groups,drift_sections,differential_window_samples,nested,noise_span_scope,qualified_cn0_low,qualified_cn0_high\n";
    for(unsigned i=0;i<a.trials;++i){require(std::chrono::duration<double>(Clock::now()-begun).count()<a.seconds,"bounded job limit reached; CSV is INCOMPLETE");
        const auto seed=a.seed+104729ULL*i;auto d=draw(m,a,seed);const auto cross=crossings(m,a,d);
        population.push_back({cross[0].value,seed});
        const auto mismatch=maximum_mismatch;
        const auto allowance=4*m.threshold+32;const auto epsilon=mismatch<=allowance?2*std::exp(-30.):std::numeric_limits<double>::quiet_NaN();
        std::array<double,4> actual;actual.fill(std::numeric_limits<double>::quiet_NaN());double maxerror=0;
        if(i<a.verify&&cross[0].bracketed&&cross[1].bracketed){for(unsigned k=0;k<4;++k){const bool guard=k%2,raw=k<2;actual[k]=replay_crossing(m,a,d,cross[guard].value,guard,raw);maxerror=std::max(maxerror,std::abs(actual[k]-cross[guard].value));}
            for(const auto delta:{-.1,.1})for(const bool guard:{false,true}){const auto db=cross[guard].value+delta;
                const auto r=replay(m,a,d,db,guard,true);require(r.accepted==accepted(m,a,d,amplitude(db),guard),"actual raw PCM decision disagreed with statistic");
                require(r.evidence.outer_presence_active==active(m,d,r.evidence.bit,amplitude(db)),"actual raw PCM activation disagreed with statistic");}}
        out<<seed<<','<<a.bit<<','<<cross[0].value<<','<<cross[1].value<<','<<cross[0].bracketed<<','<<cross[1].bracketed<<','<<cross[0].monotone<<','<<cross[1].monotone<<','<<cross[1].active_intervals<<','<<static_cast<double>(mismatch)<<','<<allowance<<','<<epsilon;
        for(auto v:actual)out<<','<<v;
        out<<','<<maxerror<<",64,16,3.2,10,4,"<<a.symbol<<','<<m.count<<','<<static_cast<double>(m.origin)<<','<<a.offset<<','<<a.ppm<<','<<a.diffusion<<','<<a.waveform_seed<<','<<a.interference<<','<<a.interference_frequency<<','<<m.threshold<<",1,1,1,0,"<<d.nested[a.bit]<<",conditional_fixed_waveform_real_AWGN_raw_fit,"<<a.low<<','<<a.high<<'\n';
        if((i+1)%1000==0){out.flush();std::cerr<<"draws="<<i+1<<" elapsed_seconds="<<std::chrono::duration<double>(Clock::now()-begun).count()<<'\n';}
    }
    if(a.verify){
        std::sort(population.begin(),population.end());
        std::ofstream verified(a.csv+".replay.csv");require(bool(verified),"cannot write quantile replay");verified<<std::setprecision(17);
        verified<<"p,seed,predicted_off_cn0,predicted_on_cn0,actual_raw_off_cn0,actual_raw_on_cn0,actual_auto_off_cn0,actual_auto_on_cn0,maximum_root_error_db\n";
        for(const double p:{.9,.99}){
            require(std::chrono::duration<double>(Clock::now()-begun).count()<a.seconds,"bounded quantile replay limit reached; INCOMPLETE");
            const auto index=static_cast<std::size_t>(std::ceil(p*population.size()))-1;const auto seed=population[index].second;
            auto d=draw(m,a,seed);const auto cross=crossings(m,a,d);require(cross[0].monotone&&cross[1].monotone,"quantile replay nonmonotone");
            verified<<p<<','<<seed<<','<<cross[0].value<<','<<cross[1].value;double maximum=0;
            for(unsigned k=0;k<4;++k){const bool guard=k%2,raw=k<2;const auto actual=replay_crossing(m,a,d,cross[guard].value,guard,raw);
                verified<<','<<actual;maximum=std::max(maximum,std::abs(actual-cross[guard].value));}
            verified<<','<<maximum<<'\n';
        }
    }
    std::cerr<<"COMPLETE conditional_draws="<<a.trials<<" elapsed_seconds="<<std::chrono::duration<double>(Clock::now()-begun).count()<<"; not full acquisition or physical-end qualification\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"INCOMPLETE: "<<e.what()<<'\n';return 1;}}

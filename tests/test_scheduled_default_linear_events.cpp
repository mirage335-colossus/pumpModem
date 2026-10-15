#include "scheduled_default_linear_events.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace datapump::modem;
namespace d=datapump::modem::detail;
namespace e=d::scheduled_default_linear_events;
namespace grid=d::scheduled_linear_grid;
using Complex=std::complex<double>;
void check(bool x,const char* message){if(!x)throw std::runtime_error(message);}
void close(double x,double y,const char* message){check(std::isfinite(x)&&std::isfinite(y)&&std::abs(x-y)<2e-9*std::max({1.,std::abs(x),std::abs(y)}),message);}
void fit_close(const d::CorrelationFit& a,const d::CorrelationFit& b){check(a.count==b.count,"count changed");
    close(a.xc,b.xc,"xc changed");close(a.xs,b.xs,"xs changed");close(a.cc,b.cc,"cc changed");
    close(a.ss,b.ss,"ss changed");close(a.cs,b.cs,"cs changed");close(a.energy,b.energy,"raw energy changed");}
void append_fit(d::CorrelationFit& a,const d::CorrelationFit& b){a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;}
struct Fixture {
    e::Geometry g;
    std::vector<d::CorrelationProjection> pcm;
    std::array<long double,8> observed{};
    std::uint64_t first_active=2,last_active=0;
    std::size_t chip=0,q=0;
    bool perturb=false;
    explicit Fixture(std::size_t c,unsigned subdivisions,bool changed,unsigned sections,bool differential):chip(c),q(subdivisions),perturb(changed) {
        g.physical_base=-.3125L;g.node_spacing=static_cast<long double>(chip)/q;
        g.chip_samples=chip;g.symbol_samples=64*chip;g.subdivisions=q;g.sections=sections;
        g.differential_width=differential?8*chip:0;g.outer=true;
        g.template_cells=64*q+42;g.outputs=8;g.origin_first=3;g.step=q/2;g.fragment_cells=37;
        last_active=64*q+41;g.capture_first=static_cast<std::uint64_t>(std::ceil(g.node_spacing*5));
        g.capture_end=static_cast<std::uint64_t>(std::ceil(g.physical_base+(g.template_cells+(g.origin_first+7)*g.step)*g.node_spacing));
        for(std::size_t j=0;j<8;++j)observed[j]=g.capture_first>0?3*chip/8.L:0;
        g.observed_source_begin=observed;pcm.resize(g.capture_end);
        Complex carrier;const auto step=std::polar(1.,.71);
        for(std::size_t n=0;n<pcm.size();++n){if(n%512==0)carrier=std::polar(1.,.71*n);
            const auto x=.3*std::sin(.113*n)+.17*std::cos(.301*n)+.09*std::sin(.017*n*n);
            pcm[n]={x*carrier.real(),x*carrier.imag(),carrier.real()*carrier.real(),carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),x*x};carrier*=step;}
    }
    long double node(std::uint64_t n)const{return (static_cast<long double>(n)-g.physical_base)/g.node_spacing;}
    std::uint64_t source_node(std::size_t k)const {
        const std::size_t a=16*q,b=24*q;
        if(k<2)return e::no_owner;k-=2;if(k<a)return k;k-=a;
        if(k<17)return e::no_owner;k-=17;if(k<b)return a+k;k-=b;
        if(k<23)return e::no_owner;k-=23;if(k<24*q)return a+b+k;return e::no_owner;
    }
    double gain(long double s)const {
        const auto cell=static_cast<std::uint64_t>(std::floor(s));
        const std::uint64_t group=cell<16*q?0:cell<40*q?16*q:40*q;
        const auto end=group+(group==0?16*q:24*q);const auto phase=s-group;
        const auto a=std::clamp(static_cast<double>(phase/q),0.,1.),b=std::clamp(static_cast<double>((end-s)/q),0.,1.);
        return a*a*(3-2*a)*b*b*(3-2*b);
    }
    e::Pair value(long double s)const {
        const auto x=static_cast<double>(s/q);
        return {Complex{.42*std::sin(.193*x)+.11,.31*std::cos(.117*x)},Complex{.29*std::cos(.139*x)-.08,.37*std::sin(.173*x)}};
    }
    e::Cell cell(std::size_t k)const {
        e::Cell c;const auto sn=source_node(k);if(sn==e::no_owner){c.chord.supported=true;c.chord.observed=false;return c;}
        c.source_node=sn;c.outer_gain_left=gain(sn);c.outer_gain_right=gain(sn+1);
        c.chord.observed=true;c.chord.supported=sn!=0&&sn!=16*q&&sn!=40*q&&sn+1!=64*q;
        c.chord.residual={0,0};const auto l=value(sn),r=value(sn+1);
        for(unsigned bit=0;bit<2;++bit){c.chord.left[bit]=c.outer_gain_left*l[bit];c.chord.right[bit]=c.outer_gain_right*r[bit];}
        // A chosen quiet global chord may still overlap Original observations
        // in another translated lane. It retains E/N and has explicit public
        // outer-zero metadata; private zero alone is never that proof.
        if(sn==7*q+1){c.chord.left=c.chord.right={};c.outer_zero=true;c.source_node=e::no_owner;}
        return c;
    }
    e::Row row(std::size_t j,std::uint64_t n,bool needq)const {
        e::Row r;if(n<g.capture_first||n>=g.capture_end)return r;
        const auto u=node(n)-j*g.step;if(u<0||u>=g.template_cells)return r;
        const auto k=static_cast<std::size_t>(std::floor(u));const auto sn=source_node(k);if(sn==e::no_owner)return r;
        const auto native=static_cast<long double>(sn)+(u-k);
        const auto change=perturb?(static_cast<long double>(j%3)-1)*.0003L:0;
        r.source=native*chip/q*(1+change)+.00013L*(static_cast<long double>(j%3)-1);
        r.gain=gain(native);if(r.source<0||r.source>=g.symbol_samples||r.gain<=0)return r;
        r.owner=static_cast<std::uint64_t>(std::floor(r.source/chip));r.observed=true;
        if(needq){r.q=value(native);for(auto& x:r.q)x*=r.gain;}return r;
    }
    e::Partition classify(std::size_t j,std::uint64_t first,std::uint64_t end)const {
        e::Partition out;if(first==end)return {true,false};
        // A pure oracle fixture exhaustively verifies the complete selected set.
        // The production callback instead proves this using Original pieces.
        for(auto n=first;n<end;++n){const auto r=row(j,n,false);if(!r.observed)return {};
            const e::Partition p{true,true,r.owner,e::window_at(g,r.source),e::section_at(g,r.source)};
            if(n==first)out=p;else if(p.owner!=out.owner||p.window!=out.window||p.section!=out.section)return {};}
        if((first/7)%11==0)return {}; // same chosen template despite forced partition
        return out;
    }
    e::Pair coefficients(std::uint64_t k)const{return {std::polar(.71,.17*k),std::polar(.63,-.23*k)};}
    void outer(e::EventStats& out,const e::Cell& c,const e::Row& row,const d::CorrelationProjection& p,double t)const {
        if(c.chord.supported&&c.outer_zero)return;
        const Complex z{p.xc,p.xs};
        if(c.chord.supported){const auto owner=c.source_node/q,phase=c.source_node%q;
            for(int d=-8;d<=8;++d){if(d<0&&owner<static_cast<std::uint64_t>(-d))continue;
                const auto k=d<0?owner-static_cast<std::uint64_t>(-d):owner+d;if(k>=64)continue;
                const auto l=c.outer_gain_left*pattern_pulse(static_cast<double>(phase)/q-d-.5);
                const auto r=c.outer_gain_right*pattern_pulse(static_cast<double>(phase+1)/q-d-.5);
                out.outer.add(k,((1-t)*l+t*r)*z,[&](auto k){return coefficients(k);});}
        }else pattern_pulse_each(static_cast<double>(row.source),g.symbol_samples,g.chip_samples,[&](auto k,double p){
            out.outer.add(k,(row.gain*p)*z,[&](auto k){return coefficients(k);});});
    }
    e::EventStats reference(std::size_t j)const {
        e::EventStats out;
        for(std::uint64_t n=g.capture_first;n<g.capture_end;++n){auto rowvalue=row(j,n,true);if(!rowvalue.observed)continue;
            const auto u=node(n)-j*g.step,k=std::floor(u);const auto c=cell(static_cast<std::size_t>(k));const auto t=static_cast<double>(u-k);
            if(c.chord.supported)for(unsigned b=0;b<2;++b)rowvalue.q[b]=(1-t)*c.chord.left[b]+t*c.chord.right[b];
            const auto section=e::section_at(g,rowvalue.source);const auto window=e::window_at(g,rowvalue.source);
            if(out.observed_start==e::no_owner)out.observed_start=n;
            for(unsigned b=0;b<2;++b){d::CorrelationFit f;f.add(pcm[n],rowvalue.q[b],1);append_fit(out.whole[b],f);out.chips[b].add_fit(f,rowvalue.owner);
                if(g.sections>1){auto& s=out.drift[b];while(s.section<section){if(e::full_rank(s.active))out.quarter_ranks[b]|=1U<<s.section;
                    const auto x=s.active.explained();s.explained_sum+=x;s.largest_explained=std::max(s.largest_explained,x);s.active={};++s.section;}append_fit(s.active,f);}
                if(g.differential_width){auto& w=out.differential[b];if(!w.initialized||w.window_index!=window){if(w.initialized){
                        w.expected_count=w.window_index*g.differential_width>=observed[j-g.origin_first]?w.active.count:0;w.finish(g.symbol_samples,g.differential_width);}
                        w.active={};w.initialized=true;w.window_index=window;}append_fit(w.active,f);}
            }
            if(g.outer)outer(out,c,rowvalue,pcm[n],t);
        }
        if(g.differential_width)for(auto& w:out.differential)if(w.initialized)
            w.expected_count=w.window_index*g.differential_width>=observed[j-g.origin_first]?w.active.count:0;
        if(g.outer)out.outer.finish([&](auto k){return coefficients(k);});out.valid=true;return out;
    }
};
void run(std::size_t chip,unsigned q,bool perturb,unsigned sections,bool differential) {
    Fixture f(chip,q,perturb,sections,differential);std::uint64_t visited=0;
    const auto visit=[&](auto a,auto b,auto emit){for(auto n=a;n<b;++n){++visited;emit(n,f.pcm[n]);}};
    const auto produce=[&](e::Geometry geometry){return e::run(geometry,e::reservation(geometry),[&](auto k,auto,auto){return f.cell(k);},visit,
        [&](auto j,auto a,auto b){return f.classify(j,a,b);},[&](auto j,auto n,bool need){return f.row(j,n,need);},[&](auto k){return f.coefficients(k);});};
    const auto result=produce(f.g);check(result.valid&&result.events.size()==8,"complete cohort failed");
    check(result.work.shared_input_rows>0&&result.work.cell_contractions>0&&result.work.direct_selected_rows>0&&
        result.work.source_rows<result.work.direct_selected_rows,"shared/chord/direct work not exercised");
    check(result.work.direct_input_rows==0&&result.work.projection_lookups>0&&visited==result.work.shared_input_rows,
        "row partitions replayed raw capture instead of shared projections");
    check(result.work.peak_bytes<=e::reservation(f.g),"workspace reservation failed");
    for(std::size_t j=0;j<8;++j){const auto oracle=f.reference(j+f.g.origin_first);const auto& actual=result.events[j];
        check(actual.observed_start==oracle.observed_start,"initial crop changed");
        for(unsigned b=0;b<2;++b){fit_close(actual.whole[b],oracle.whole[b]);fit_close(actual.chips[b].cell,oracle.chips[b].cell);
            close(actual.chips[b].projected_energy,oracle.chips[b].projected_energy,"chip projection changed");
            check(actual.chips[b].projected_rank==oracle.chips[b].projected_rank&&actual.chips[b].nested==oracle.chips[b].nested,"chip rank/nesting changed");
            if(sections>1){fit_close(actual.drift[b].active,oracle.drift[b].active);close(actual.drift[b].explained_sum,oracle.drift[b].explained_sum,"quarter projection changed");
                close(actual.drift[b].largest_explained,oracle.drift[b].largest_explained,"quarter maximum changed");check(actual.quarter_ranks[b]==oracle.quarter_ranks[b],"quarter rank changed");}
            if(differential){const auto& a=actual.differential[b];const auto& r=oracle.differential[b];
                fit_close(a.active,r.active);check(a.initialized==r.initialized&&a.window_index==r.window_index&&a.expected_count==r.expected_count,
                    "differential window completeness changed");
                check(a.evidence.pairs==r.evidence.pairs&&r.evidence.pairs>0&&a.evidence.pending==r.evidence.pending,
                    "differential paired accumulation missing");
                close(a.evidence.squared_magnitudes,r.evidence.squared_magnitudes,"differential mass changed");
                for(unsigned k=0;k<4;++k){close(a.evidence.sums[k].real(),r.evidence.sums[k].real(),"differential real projection changed");
                    close(a.evidence.sums[k].imag(),r.evidence.sums[k].imag(),"differential imag projection changed");}
                close(a.score(f.g.symbol_samples,f.g.differential_width),r.score(f.g.symbol_samples,f.g.differential_width),"differential evidence changed");}
            if(f.g.outer){close(actual.outer.dot[b].real(),oracle.outer.dot[b].real(),"independent outer dot real changed");
                close(actual.outer.dot[b].imag(),oracle.outer.dot[b].imag(),"independent outer dot imag changed");
                close(actual.outer.variance[b],oracle.outer.variance[b],"independent outer variance changed");
                check(actual.outer.contributing_terms[b]==oracle.outer.contributing_terms[b],"independent outer support changed");
                close(actual.outer.score(b),oracle.outer.score(b),"independent outer pulse evidence changed");}
        }
        for(double threshold:{0.,5.,27.,91.}){const auto a=e::scores(actual,f.g,threshold),b=e::scores(oracle,f.g,threshold);
            check(a.valid&&b.valid&&a.bit==b.bit&&a.outer_presence_active==b.outer_presence_active,"guard branch changed");
            close(a.score[0],b.score[0],"zero guard score changed");close(a.score[1],b.score[1],"one guard score changed");}
    }
    auto solo=f.g;solo.origin_first+=4;solo.outputs=1;solo.observed_source_begin=std::span(f.observed).subspan(4,1);
    const auto single=produce(solo);check(single.valid,"single lane failed");
    for(unsigned b=0;b<2;++b)fit_close(single.events[0].whole[b],result.events[4].whole[b]);
    bool denied=false;try{(void)e::run(f.g,e::reservation(f.g)-1,[&](auto k,auto,auto){return f.cell(k);},visit,
        [&](auto j,auto a,auto b){return f.classify(j,a,b);},[&](auto j,auto n,bool need){return f.row(j,n,need);},[&](auto k){return f.coefficients(k);});}catch(const datapump::Error&){denied=true;}
    check(denied,"workspace denial lost");std::stop_source stopped;stopped.request_stop();bool cancelled=false;
    try{(void)e::run(f.g,e::reservation(f.g),[&](auto k,auto,auto){return f.cell(k);},visit,
        [&](auto j,auto a,auto b){return f.classify(j,a,b);},[&](auto j,auto n,bool need){return f.row(j,n,need);},[&](auto k){return f.coefficients(k);},stopped.get_token());}catch(const datapump::Error&){cancelled=true;}
    check(cancelled,"cancellation lost");
    bool missing=false;try{(void)e::run(f.g,e::reservation(f.g),[&](auto k,auto,auto){return f.cell(k);},
        [&](auto a,auto b,auto emit){for(auto n=a;n<b;++n)if(n!=a)emit(n,f.pcm[n]);},
        [&](auto j,auto a,auto b){return f.classify(j,a,b);},[&](auto j,auto n,bool need){return f.row(j,n,need);},[&](auto k){return f.coefficients(k);});
    }catch(const datapump::Error&){missing=true;}check(missing,"missing physical projection silently accepted");
}
void active_colored_and_outer_branches() {
    e::Geometry g;g.symbol_samples=87040;g.chip_samples=34;g.subdivisions=2;
    g.outputs=g.template_cells=1;g.capture_end=1;g.outer=true;
    e::EventStats event;event.valid=true;
    for(unsigned bit=0;bit<2;++bit) {
        const auto explained=bit?10.:50.;event.whole[bit]={std::sqrt(explained*500),0,500,500,0,1000,87040};
        event.chips[bit].projected_energy=700;event.chips[bit].projected_rank=5120;
        event.outer.variance[bit]=100;event.outer.contributing_terms[bit]=1000;
    }
    check(event.chips[0].colored_excess(event.whole[0],30),"colored activation fixture inactive");
    const auto coherent=e::scores(event,g,30);
    check(coherent.valid&&coherent.outer_presence_active,"coherent colored/outer guard omitted");
    close(coherent.score[0],event.chips[0].score(event.whole[0]),"colored score formula changed");
    // complete() applies the explicit outer flag after every enabled score
    // reduction. An adapter may reject an unsupported geometry, not omit it.
    g.sections=4;
    check(e::scores(event,g,30).outer_presence_active,"quarter outer guard silently omitted");
    g.differential_width=136;
    check(e::scores(event,g,30).outer_presence_active,"differential outer guard silently omitted");
    event.whole[0].xc=std::sqrt(500.);event.whole[1].xc=0;
    auto& independent=event.differential[0].evidence;
    independent.pairs=256;independent.squared_magnitudes=256;
    independent.sums.fill(Complex{64,0});
    check(independent.score()>100,"positive differential score fixture inactive");
    const auto positive=e::scores(event,g,30);
    const auto prior=d::combine_drift_evidence(event.chips[0].score(event.whole[0]),0,4);
    close(positive.score[0],d::combine_differential_evidence(prior,independent.score(),true),
        "positive differential score or detector penalty omitted");
}
void outer_presence_cannot_promote_a_low_guarded_score() {
    e::Geometry g;g.symbol_samples=87040;g.chip_samples=34;g.subdivisions=2;
    g.outputs=g.template_cells=1;g.capture_end=1;g.outer=true;
    e::EventStats event;event.valid=true;
    for(unsigned bit=0;bit<2;++bit) {
        event.whole[bit]={std::sqrt((bit?1.:1.1)*500),0,500,500,0,1000,87040};
        event.chips[bit].projected_energy=bit?700:900;event.chips[bit].projected_rank=5120;
        event.outer.variance[bit]=100;event.outer.contributing_terms[bit]=1000;
        event.outer.dot[bit]={100,0};
    }
    constexpr double threshold=30;
    check(event.whole[0].score()>event.whole[1].score()&&event.whole[1].score()>threshold,
        "raw-score ordering fixture inactive");
    const auto result=e::scores(event,g,threshold);
    check(result.valid&&result.bit==1&&result.outer_presence_active&&result.outer_presence_score>threshold,
        "guarded winner/positive outer fixture inactive");
    const auto maximum=std::max(result.score[0],result.score[1]);
    check(maximum<threshold,"low guarded scores fixture inactive");
    const auto standalone=maximum>=threshold&&(!result.outer_presence_active||result.outer_presence_score>=threshold);
    check(!standalone,"outer AND gate promoted a rejected coherent candidate");
    check(e::below_rejection_floor(result,threshold),"active outer evidence blocked production early rejection");
    check(!e::below_rejection_floor(result,maximum),"strict rejection floor lost");
    check(!e::below_rejection_floor(result,0),"zero retention floor discarded a short chain");
    auto other=result;other.score[0]=threshold;
    check(!e::below_rejection_floor(other,threshold),"independent zero candidate discarded");
    other=result;other.score[1]=threshold;
    check(!e::below_rejection_floor(other,threshold),"independent one candidate discarded");
}
int main(){try{if(!d::scheduled_acquisition_certificate::environment<double>())return 77;
    run(34,2,false,1,false);run(34,4,true,1,false);run(8,4,true,4,false);run(34,4,true,4,true);
    active_colored_and_outer_branches();outer_presence_cannot_promote_a_low_guarded_score();
    std::cout<<"Default shared linear complete cohort fields/owners/ramps/crops/guards/cancellation checked\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

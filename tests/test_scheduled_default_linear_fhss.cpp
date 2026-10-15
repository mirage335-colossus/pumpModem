#include "scheduled_default_linear_runtime.hpp"
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace d=datapump::modem::detail;
namespace r=d::scheduled_default_linear_runtime;
namespace e=d::scheduled_default_linear_events;
namespace o=d::scheduled_original_partition;
using namespace datapump::modem;
using Complex=std::complex<double>;
void check(bool b,const char* m){if(!b)throw std::runtime_error(m);}
void close(double a,double b,const char* m){check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=3e-10*std::max({1.,std::abs(a),std::abs(b)}),m);}
struct Map {
    SchedHopLocation locate(SchedHopCoordinate tick)const {
        SchedHopLocation l;l.active=l.continuous=true;l.source=tick;
        l.physical_end=l.source_end=l.flat_end=l.fall_end=UINT64_MAX;return l;
    }
    SchedHopCoordinate source_to_physical(SchedHopCoordinate s,bool=false)const{return s;}
};
struct Fence {std::uint64_t original_first,original_count;};
const auto fences=[](auto first,auto end,auto consume){for(auto n=first;n<end;){const auto count=std::min<std::uint64_t>(137-n%137,end-n);consume(Fence{n,count});n+=count;}};
FhssPowerShape fhss(std::uint32_t fs) {
    FhssPlan p;p.channels=2;p.shifts[0]=30000000;p.shifts[1]=31000000;p.dwell_seconds=.4;
    p.power_ramps=p.mandatory_ramps=p.smooth_ramps=true;p.clock_error_seconds=p.peer_clock_error_seconds=.01;
    p.audio_error_seconds=.05;p.ramp_seconds=.006;return FhssPowerShape(p,fs,64,10000);
}
o::Event event(std::uint32_t fs,std::uint64_t chip,std::uint64_t count,long double rate=1,long double origin=0) {
    o::Event v;v.identity={1,2,3,1,0,0,1,1};v.symbol_samples=chip*count;v.chip_samples=chip;
    v.sample_rate=fs;v.epoch_sample=194ULL*fs;v.anchor={v.epoch_sample,0};v.rate=rate;v.origin=origin;
    v.begin=v.native_begin=static_cast<std::uint64_t>(std::max(0.L,std::ceil(origin)));
    v.end=v.native_end=static_cast<std::uint64_t>(std::ceil(origin+v.symbol_samples/rate));
    v.history_first=0;v.capture_end=v.end;v.left_guard=v.right_guard=1;v.fhss=fhss(fs);return v;
}
std::vector<o::Piece> reference_pieces(const o::Event& event,Map& map) {
    std::vector<o::Piece> out;fences(event.begin,event.end,[&](auto raw){auto at=raw.original_first;
        while(at<raw.original_first+raw.original_count){o::ProcessingFence f{raw.original_first+1,event.identity.capture,raw.original_first,raw.original_first+raw.original_count};
            auto p=o::make_piece(event,f,at,map);at=p.end;out.push_back(p);}});return out;
}
o::Evaluation expected(const o::Event& v,std::span<const o::Piece> pieces,std::uint64_t n) {
    if(n<v.begin||n>=v.end)return {};const auto* p=o::find_piece(pieces,n);check(p,"oracle Piece missing");
    const auto x=o::evaluate(v,*p,n);check(x.valid(),"independent Original FHSS oracle invalid");return x;
}
void masks(std::uint32_t fs,long double rate,long double origin) {
    auto v=event(fs,10000,1,rate,origin);Map map;r::Context c(1,0);c.add(0,v,map,fences);
    const auto pieces=reference_pieces(v,map);unsigned observed=0,quiet=0,positive_proofs=0,quiet_proofs=0,partitions=0;
    const auto q=[](long double s,double gain){return e::Pair{gain*Complex{.2+.1*std::sin(double(s)*.013),.3},gain*Complex{-.1,.2}};};
    for(auto n=v.begin;n<v.end;++n) {
        const auto a=expected(v,pieces,n);const auto b=c.row(0,n,true,q);
        check(a.current==b.observed,"FHSS exact usable mask changed");
        if(a.current){++observed;check(a.owner==b.owner&&a.within==b.source&&a.gain==b.gain,"FHSS source/gain/owner changed");}
        else ++quiet;
        if(n+9<=v.end) {
            const auto p=c.classify(0,n,n+9);
            if(!p.valid){++partitions;continue;}
            if(p.observed)++positive_proofs;else ++quiet_proofs;
            for(auto k=n;k<n+9;++k){const auto z=expected(v,pieces,k);
                check(z.current==p.observed,"FHSS endpoint-only proof hid a quiet/active interior");
                if(p.observed)check(z.owner==p.owner,"FHSS owner changed inside proved cell");}
        }
    }
    check(observed&&quiet&&positive_proofs&&quiet_proofs&&partitions,"FHSS mask fixture lost positive/quiet/partition coverage");
    bool bridge=false,flat=false;
    for(auto n=v.begin;n+512<v.end;++n) {
        const auto a=expected(v,pieces,n);if(!a.current)continue;
        const auto jump=static_cast<std::uint64_t>(std::ceil(2.L*fs/5/rate));
        if(n+jump>=v.end)continue;const auto b=expected(v,pieces,n+jump);if(!b.current)continue;
        bool middle=false;for(auto k=n+1;k<n+jump;++k)if(!expected(v,pieces,k).current){middle=true;break;}
        if(middle){check(!c.classify(0,n,n+jump+1).valid,"positive FHSS endpoints certified hidden quiet dwell");bridge=true;break;}
    }
    for(auto n=v.begin;n+65<v.end;++n) {
        const auto a=expected(v,pieces,n),b=expected(v,pieces,n+64);
        if(a.current&&b.current&&r::fhss_flat_span(v,a.physical,b.physical)) {
            const auto p=c.classify(0,n,n+65);check(p.valid&&p.observed,"exact flat FHSS proof failed long metadata cell");
            for(auto k=n;k<n+65;++k)check(v.fhss.gain(0,expected(v,pieces,k).physical.sample%(2ULL*fs)+expected(v,pieces,k).physical.fraction,fs)==1,"flat FHSS proof admitted nonunit gain");
            flat=true;break;
        }
    }
    check(bridge&&flat,"FHSS hidden-gap/flat fast-path fixture became vacuous");
}
void same_chord_fields(std::uint32_t fs,std::uint64_t chip) {
    const auto chips=chip==8?640ULL:256ULL;const auto v0=event(fs,chip,chips);auto v1=v0;v1.origin=chip/2.L;v1.begin=v1.native_begin=static_cast<std::uint64_t>(std::ceil(v1.origin));
    v1.end=v1.native_end=static_cast<std::uint64_t>(std::ceil(v1.origin+v1.symbol_samples));v1.capture_end=v1.end;
    Map map;r::Context context(1,0);context.add(0,v0,map,fences);context.add(1,v1,map,fences);
    const auto shape=[&](double s){return e::Pair{Complex{.19+.11*std::sin(s/chip*.71),.23*std::cos(s/chip*.17)},Complex{.25*std::cos(s/chip*.31),-.13-.08*std::sin(s/chip*.23)}};};
    const auto coefficients=[](std::uint64_t n){return e::Pair{std::polar(.7,.13*n),std::polar(.6,-.21*n)};};
    constexpr unsigned Q=4;const auto spacing=chip/static_cast<long double>(Q);
    e::Geometry g;g.node_spacing=spacing;g.capture_end=v1.end;g.symbol_samples=v0.symbol_samples;g.chip_samples=chip;
    g.template_cells=chips*Q;g.outputs=2;g.step=Q/2;g.subdivisions=Q;g.fragment_cells=37;g.outer=true;
    std::vector<d::CorrelationProjection> input(g.capture_end);
    for(std::uint64_t n=0;n<input.size();++n){const auto carrier=std::polar(1.,.731*n);const auto x=.17*std::sin(.113*n)+.11*std::cos(.307*n);
        input[n]={x*carrier.real(),x*carrier.imag(),carrier.real()*carrier.real(),carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),x*x};}
    const auto cell=[&](std::size_t,long double a,long double b){return context.cell(0,a,b,Q,.5,coefficients,shape);};
    // Independently check physical FHSS endpoint gain; a missing binding must
    // fail even if accelerated and direct same-chord computations agree.
    unsigned checked_endpoints=0;
    for(std::size_t k=0;k<g.template_cells;++k) {
        const auto c=cell(k,k*spacing,(k+1)*spacing);if(!c.chord.supported||c.outer_zero)continue;
        const auto a=SchedHopCoordinate{v0.epoch_sample,0}.add_signed(k*spacing);
        const auto b=SchedHopCoordinate{v0.epoch_sample,0}.add_signed((k+1)*spacing);
        const auto ga=v0.fhss.gain(0,a.sample%(2ULL*fs)+a.fraction,fs);
        const auto gb=v0.fhss.gain(0,b.sample%(2ULL*fs)+b.fraction,fs);
        close(c.outer_gain_left,ga,"public outer left FHSS gain lost");close(c.outer_gain_right,gb,"public outer right FHSS gain lost");
        const auto qa=shape(double(k*spacing)),qb=shape(double((k+1)*spacing));
        for(unsigned bit=0;bit<2;++bit){close(std::abs(c.chord.left[bit]-ga*pattern_limit_pcm(.5*qa[bit])/.5),0,"private left FHSS chord gain lost");
            close(std::abs(c.chord.right[bit]-gb*pattern_limit_pcm(.5*qb[bit])/.5),0,"private right FHSS chord gain lost");}
        ++checked_endpoints;
    }
    check(checked_endpoints>0,"FHSS endpoint gain fixture became empty");
    const auto row=[&](std::size_t j,std::uint64_t n,bool need){return context.row(j,n,need,[&](long double at,double gain){auto q=shape(double(at));for(auto& z:q)z=gain*pattern_limit_pcm(.5*z)/.5;return q;});};
    const auto result=e::run(g,e::reservation(g),cell,[&](auto a,auto b,auto emit){for(auto n=a;n<b;++n)emit(n,input[n]);},
        [&](auto j,auto a,auto b){
            auto part=context.classify(j,a,b);
            // On C8/Q4 every usable FHSS boundary aligns with this grid.
            // Force one metadata-unknown cell to test the required SAME-chord
            // direct fallback as well, without changing its observed rows.
            if(part.valid && part.observed && a<=1024 && b>1024)return e::Partition{};
            return part;
        },row,coefficients);
    check(result.valid&&result.work.shared_input_rows&&result.work.direct_selected_rows&&result.work.cell_contractions,"FHSS shared/direct event paths not exercised");
    for(std::size_t j=0;j<2;++j){std::array<d::CorrelationFit,2> exact{};std::uint64_t quiet=0;
        for(std::uint64_t n=0;n<input.size();++n){const auto r=row(j,n,true);if(!r.observed){++quiet;continue;}
            const auto u=n/spacing-j*g.step;if(u<0||u>=g.template_cells)continue;const auto k=static_cast<std::size_t>(std::floor(u));
            const auto c=cell(k,k*spacing,(k+1)*spacing);if(c.chord.supported&&!c.chord.observed)throw std::runtime_error("quiet FHSS cell omitted observed Original row");
            auto q=r.q;if(c.chord.supported){const auto t=static_cast<double>(u-k);for(unsigned bit=0;bit<2;++bit)q[bit]=(1-t)*c.chord.left[bit]+t*c.chord.right[bit];}
            for(unsigned bit=0;bit<2;++bit)exact[bit].add(input[n],q[bit],1);
        }
        check(quiet>0&&exact[0].count>2,"FHSS complete event lost quiet/usable rows");
        for(unsigned bit=0;bit<2;++bit){const auto& a=exact[bit];const auto& b=result.events[j].whole[bit];check(a.count==b.count,"FHSS full-event usable count changed");
            close(a.energy,b.energy,"FHSS raw E was gain-weighted or omitted");close(a.xc,b.xc,"FHSS same-chord xc changed");close(a.xs,b.xs,"FHSS same-chord xs changed");
            close(a.cc,b.cc,"FHSS same-chord cc changed");close(a.ss,b.ss,"FHSS same-chord ss changed");close(a.cs,b.cs,"FHSS same-chord cs changed");}
    }
}
int main(){try {if(!d::scheduled_sparse_owned_spans::environment())return 77;
    masks(6120,1,0);masks(6121,1.0000003L,.375L);masks(1001,.99L,17.25L);masks(1001,1.01L,.125L);
    same_chord_fields(6120,34);same_chord_fields(14400,8);
    std::cout<<"smooth FHSS exact masks/quiet interiors/lag/rational dwell/wrap/native same-chord fields checked\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

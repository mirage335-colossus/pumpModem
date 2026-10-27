#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <algorithm>
#include <complex>
#include <iostream>
#include <map>
#include <random>
#include <type_traits>
#include <string>
using namespace datapump;
namespace {
void check(bool v,const char* text){if(!v)throw Error(text);}
void geometry() {
    modem::PatternSearch search;search.admitted_clock_half_width_ppm=100;search.admitted_clock_cell_half_width_ppm=50;
    constexpr double coefficient=1000;
    for(const auto ppm:{-100.,0.,100.})for(const auto r:{-.5,-.25,0.,.25,.5})
        search.hypotheses.push_back({r-coefficient*ppm*1e-6,ppm});
    modem::detail::PatternHelperFrequencySelector selector;
    check(selector.initialize(search,{0,1,coefficient,50}),"complete LF/RF geometry declined");
    const auto storage=selector.working_bytes();check(selector.selected==15,"wide window omitted original cells");
    selector.update({0,.001,coefficient,50});check(selector.selected==3,"current reference failed to narrow all original LF groups");
    for(std::size_t i=0;i<search.hypotheses.size();++i)
        check(selector.keep(i)==(i%5==2),"affine cell selector changed an LF alternative or RF coordinate");
    selector.update({.125,0,coefficient,50});check(selector.selected==6,"RF boundary discarded an original tie");
    auto broad_cells=search;
    for(auto& pair:broad_cells.hypotheses)pair.frequency_offset_hz-=9000*pair.clock_error_ppm*1e-6;
    modem::detail::PatternHelperFrequencySelector inflated;
    check(inflated.initialize(broad_cells,{0,0,10000,50}) && inflated.selected==15,
        "selector dropped physical uncertainty inside an LF coverage cell");
    check(selector.working_bytes()==storage,"measurement refresh allocated new geometry");
    auto gap=search;gap.admitted_clock_cell_half_width_ppm=49;
    modem::detail::PatternHelperFrequencySelector invalid;
    check(!invalid.initialize(gap,{0,.001,coefficient,49}),"selector certified an actual clock coverage gap");
    modem::PatternSearch thin;thin.admitted_clock_half_width_ppm=100;thin.admitted_clock_cell_half_width_ppm=25;
    for(const auto ppm:{-100.,-50.,0.,50.,100.})thin.hypotheses.push_back({ppm*.01,ppm});
    modem::detail::PatternHelperFrequencySelector singleton;
    check(singleton.initialize(thin,{0,.001,coefficient,25}) && singleton.selected==thin.hypotheses.size(),
        "singleton RF/LF representatives were pruned by inventing independent RF coverage");
    // Independent clock information selects WHOLE intersecting original LF
    // cells, and uses that intersection (not the old full beta) in RF mapping.
    modem::PatternSearch dense;dense.admitted_clock_half_width_ppm=300;dense.admitted_clock_cell_half_width_ppm=37.5;
    for(int lf=-4;lf<=4;++lf)for(int rf=-20;rf<=20;++rf)
        dense.hypotheses.push_back({rf*.005-562.5*(lf*75.)*1e-6,lf*75.});
    modem::detail::PatternHelperFrequencySelector joint;
    check(joint.initialize(dense,{0,.001,562.5,37.5}),"complete dense clock domain declined");
    const auto ordinary=joint.selected;const auto bytes=joint.working_bytes();
    joint.update({0,.001,562.5,37.5,modem::PatternHelperClockInterval{99,101}});
    check(joint.selected>0 && joint.selected<=3 && joint.selected<ordinary,
        "qualified clock intersection retained full-LF-cell RF inflation");
    for(std::size_t i=0;i<joint.cells.size();++i)if(joint.keep(i))
        check(dense.hypotheses[i].clock_error_ppm==75,"clock interval selected a disjoint original LF cell");
    joint.update({0,.001,562.5,37.5,modem::PatternHelperClockInterval{112.5,112.5}});
    bool low=false,high=false;
    for(std::size_t i=0;i<joint.cells.size();++i)if(joint.keep(i)) {
        const auto ppm=dense.hypotheses[i].clock_error_ppm;
        check(ppm==75 || ppm==150,"clock boundary admitted an unrelated LF cell");low|=ppm==75;high|=ppm==150;
    }
    check(low && high,"clock boundary discarded one original LF-cell tie");
    check(!joint.accepts({0,.001,562.5,37.5,modem::PatternHelperClockInterval{301,302}}) &&
          !joint.accepts({0,.001,562.5,37.5,modem::PatternHelperClockInterval{2,1}}),
        "unavailable or malformed clock interval gained authority");
    joint.update({0,.001,562.5,37.5,modem::PatternHelperClockInterval{99,101}});
    auto reflected=dense;for(auto& pair:reflected.hypotheses)pair.frequency_offset_hz=-pair.frequency_offset_hz;
    modem::detail::PatternHelperFrequencySelector negative,shifted;
    check(negative.initialize(reflected,{0,.001,-562.5,37.5,modem::PatternHelperClockInterval{99,101}}) &&
          shifted.initialize(dense,{-.017,.001,562.5,37.5,modem::PatternHelperClockInterval{99,101}}),
        "signed/origin geometry initialization failed");
    for(std::size_t i=0;i<joint.cells.size();++i) {
        check(joint.keep(i)==negative.keep(i),"negative clock coefficient changed the reflected RF-cell union");
        check(joint.keep(i,.017)==shifted.keep(i),"nonzero accepted origin changed the translated RF-cell union");
    }
    joint.update({0,.001,562.5,37.5});
    check(!joint.window.clock_interval && joint.selected==ordinary && joint.working_bytes()==bytes,
        "unknown clock did not restore the exact frequency-only LF mask without allocation");
    std::cout<<"whole-LF cached affine geometry, clock intersection/ties/restoration and singleton decline passed\n";
}
modem::Config config() {
    modem::Config c;c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=8;c.spreading_factor=64;
    c.integration_seconds=8;c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;c.dsss=true;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);return c;
}
modem::PatternSearch search(const modem::Config& c,double origin,bool compact) {
    modem::PatternSearch s;s.compact_clock_search=compact;s.start_offset_seconds=origin;s.start_uncertainty_seconds=0;
    s.search_stream_phases=false;s.worker_threads=1;s.bit_limit=128;s.chunk_bits=1;s.track_limit=8;
    s.candidate_limit=128;s.drift_tolerant=false;s.retain_score=0;s.early_qualified_fft=false;s.component_qualified_search=false;
    s.admitted_clock_half_width_ppm=100;s.admitted_clock_cell_half_width_ppm=50;
    const auto step=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(const auto ppm:{0.,-100.,100.})for(const auto f:{-2*step,-step,0.,step,2*step})
        s.hypotheses.push_back({f-100*ppm*1e-6,ppm});return s;
}
struct Row {Bytes bits;bool complete=false,full_pending=false;std::uint64_t completion=0;};
using Rows=std::map<std::pair<std::uint64_t,std::uint64_t>,Row>;
template<class Receiver>void poll(Receiver& receiver,Rows& rows,const Bytes& expected,std::uint64_t at) {
    for(const auto& b:receiver.take_bursts()) {
        auto& row=rows[{b.stream_first_sample,b.stream_first_symbol}];check(!row.complete,"window refresh appended after physical END");
        if(b.first_stream_symbol-b.stream_first_symbol!=row.bits.size())continue;
        check(!b.missing_slots,"window refresh introduced a payload gap");
        row.bits.insert(row.bits.end(),b.bits.begin(),b.bits.end());row.complete=b.complete;
        row.full_pending|=!b.complete && row.bits==expected;if(b.complete)row.completion=at;
    }
}
template<class Receiver>void payload(bool compact,bool clock=false,std::size_t chunk=17) {
    const auto c=config();const Bytes bits{0,0,1,0,1,1,0,1,1,0,0,1,0,1,0,0,1,1,0,0,1,0,1,1};
    const auto S=modem::symbol_sample_count(c),delay=2*S+37,origin=delay+modem::pattern_pulse_padding_samples(c);
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);std::vector<std::complex<double>> analytic(source.total_samples());
    check(source.read_analytic(analytic)==analytic.size(),"source did not drain its entire waveform");const auto end=delay+analytic.size();
    std::vector<float> pcm(end+std::max<std::uint64_t>(3*S,8*c.sample_rate));
    std::mt19937 random(529);std::normal_distribution<float> noise(0,.025F);const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=noise(random)+(i>=delay&&i-delay<analytic.size()?static_cast<float>((phase*analytic[i-delay]).real()):0.F);
    const auto s=search(c,static_cast<double>(origin)/c.sample_rate,compact);
    auto make=[&](){if constexpr(std::is_same_v<Receiver,modem::PatternReceiver>)return Receiver(c,4*1024*1024,s);
        else return Receiver(c,s,4*1024*1024);};
    auto off=make(),on=make();Rows a,b;bool narrowed=false,lost=false,raw_restored=false,reinstalled=false;
    bool clock_narrowed=false,anchor_tested=false,clock_restored=false;
    std::uint64_t omitted=0,executed=0,restore_at=0;
    check(on.set_helper_frequency_window(0,1,100),"original ordinary bank declined the initial helper interval");
    const auto allocated=on.working_bytes();
    check(on.set_helper_frequency_window(0,1,100) && on.working_bytes()==allocated,"identical reference refresh allocated geometry");
    on.clear_helper_frequency_window();
    check(!on.helper_frequency_window().active && on.set_helper_frequency_window(0,1,100),
        "loss/clear permanently prevented current-window reattachment");
    check(!on.set_helper_frequency_window(0,-1,100) && on.helper_frequency_window().active,"invalid refresh changed an active reference");
    for(std::size_t at=0;at<pcm.size();) {
        const auto n=std::min(chunk,pcm.size()-at);const auto block=std::span(pcm).subspan(at,n);
        off.push(block);on.push(block);at+=n;poll(off,a,bits,at);poll(on,b,bits,at);
        if(!narrowed && at>=(compact?S:delay+3*S)) {
            check(on.set_helper_frequency_window(0,.001,100),"qualified improved precision could not update an existing bank");
            const auto state=on.helper_frequency_window();check(state.selected_pairs==3 && state.original_pairs==15,
                "existing bank retained all cold RF cells after precision improved");
            check(on.set_helper_frequency_window(0,.001,100),"identical joined refresh was refused");narrowed=true;
        }
        if(clock && narrowed && !clock_narrowed && at>=(compact?3*S/2:delay+4*S)) {
            check(on.set_helper_frequency_window(0,.001,100,modem::PatternHelperClockInterval{-1,1}),
                "independent clock interval could not update an existing full-LF bank");
            const auto state=on.helper_frequency_window();
            check(state.clock_qualified && state.clock_interval==modem::PatternHelperClockInterval{-1,1} && state.selected_pairs==1,
                "current clock interval did not narrow original cold RF/LF cells");clock_narrowed=true;
        }
        if(clock && reinstalled && !anchor_tested && at>=delay+7*S) {
            check(on.set_helper_frequency_window(0,.001,100,modem::PatternHelperClockInterval{99,100}),
                "a current interval disjoint from the established anchor was refused");
            check(on.helper_frequency_window().clock_qualified && on.helper_frequency_window().selected_pairs==1,
                "clock selector failed to isolate a whole original LF cell");anchor_tested=true;
        }
        if(clock && anchor_tested && !clock_restored && at>=delay+9*S) {
            check(on.set_helper_frequency_window(0,.001,100) && !on.helper_frequency_window().clock_qualified &&
                  on.helper_frequency_window().selected_pairs==3,"clock qualification loss did not restore full original LF immediately");
            clock_restored=true;
        }
        if(narrowed && !raw_restored && at>=delay+5*S) {
            on.clear_helper_frequency_window();raw_restored=true;restore_at=at;
        } else if(raw_restored && !reinstalled && at>restore_at) {
            check(on.set_helper_frequency_window(0,.001,100),"raw pushes after clear prevented current-window reattachment");
            check(!on.helper_frequency_window().clock_qualified,"frequency-only reattachment retained stale clock qualification");
            reinstalled=true;
        }
        if(!lost && at>=end-S/2) {
            check(on.synchronized(),"loss test reached final symbol without ordinary acquisition");
            const auto state=on.helper_frequency_window();omitted=state.skipped_continuation_fits;executed=state.continuation_fits;
            on.clear_helper_frequency_window();check(on.set_helper_acquisition_dispatch(false),"helper loss did not pause new cold acquisition");lost=true;
        }
    }
    const auto exact=[&](const auto& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.second.complete&&r.second.bits==bits;});};
    const auto x=exact(a),y=exact(b);
    check(narrowed&&raw_restored&&reinstalled&&lost&&(!clock || (clock_narrowed&&anchor_tested&&clock_restored))&&x!=a.end()&&y!=b.end(),"current helper window truncated the complete ordinary payload or physical END");
    check(x->first==y->first&&x->second.full_pending&&y->second.full_pending,"current helper window changed pending identity or delayed final-bit progress");
    check(y->second.completion+2>=end+6*c.sample_rate,"reference loss replaced fully scored physical absence");
    if constexpr(std::is_same_v<Receiver,modem::PatternReceiver>) {
        check(executed&&omitted,"existing native tracking did not omit actual competing fits");
        std::cout<<"native actual competing fits="<<executed<<" skipped="<<omitted<<'\n';
    } else {
        check(on.compact_acquisition_state().skipped_lane_samples,"compact window omitted no original cold lane work");
    }
    (void)allocated;std::cout<<(compact?"compact":"native")<<(clock?" clock":" frequency")<<" chunk="<<chunk<<" exact24bits/pending/physicalEND and final helper loss passed\n";
}
modem::Config continuation_config() {
    auto c=config();c.sample_rate=512;c.carrier_hz=128;c.bandwidth_hz=.5;c.integration_seconds=128;return c;
}
std::size_t continuation_budget(const modem::Config& c,const modem::PatternSearch& s,bool pulse) {
    if(pulse)return 8*1024*1024;
    // Preserve the real automatic backend selection. Fund the ordinary
    // segmented representation, but not the optional shared pulse kernels.
    modem::PatternCorrelator raw(c,s,8*1024*1024,{true,false});
    return raw.reserved_workspace_bytes()+24*1024;
}
void check_continuation_backend(const modem::PatternCorrelator& receiver,bool pulse) {
    const auto work=receiver.work();
    check(pulse?work.lattices>0:work.backend==modem::PatternCorrelationBackend::pulse_segments,
        "first-symbol continuation fixture selected the wrong ordinary backend");
}
void first_symbol_loss(bool pulse,bool first_cell,bool trim=false) {
    const auto c=continuation_config();const Bytes bits{0,1};
    const auto S=modem::symbol_sample_count(c);const std::uint64_t delay=37,origin=delay+modem::pattern_pulse_padding_samples(c);
    modem::PatternCode code(c,c.stream_epoch);const auto chip=code.chip_samples();
    check(S>=60ULL*c.sample_rate && chip>4,"continuation fixture lost its long-symbol/partial-cell shape");
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(source.total_samples());
    check(source.read_analytic(analytic)==analytic.size(),"continuation TX failed to drain");
    const auto end=delay+analytic.size();std::vector<float> pcm(end+std::max<std::uint64_t>(3*S,8*c.sample_rate));
    std::mt19937 random(529);std::normal_distribution<float> noise(0,.025F);const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<pcm.size();++i)pcm[i]=noise(random)+(i>=delay && i-delay<analytic.size()?
        static_cast<float>((phase*analytic[i-delay]).real()):0.F);
    const auto s=search(c,static_cast<double>(origin)/c.sample_rate,true);const auto budget=continuation_budget(c,s,pulse);
    modem::PatternCorrelator off(c,s,budget),on(c,s,budget);check_continuation_backend(on,pulse);Rows a,b;
    check(on.set_helper_frequency_window(0,.0001,0),"first-symbol helper window declined");
    const auto loss=origin+(first_cell?chip/4:S/3);bool lost=false;const auto allocated=on.working_bytes();
    for(std::size_t at=0;at<pcm.size();) {
        // Stop exactly at loss, including inside the first shared cell. The
        // receiver never receives a caller span from the future before loss.
        auto n=std::min<std::size_t>(733,pcm.size()-at);
        if(!lost)n=std::min<std::size_t>(n,loss-at);
        const auto block=std::span(pcm).subspan(at,n);off.push(block);on.push(block);at+=n;
        poll(off,a,bits,at);poll(on,b,bits,at);
        if(!lost && at==loss) {
            check(!on.synchronized() && std::all_of(b.begin(),b.end(),[](const auto& row){return row.second.bits.empty();}),
                "helper loss fixture already accepted its first data bit");
            if(trim) {
                on.trim_optional_workspace();
                check(!on.helper_acquisition_dispatch().permitted && !on.helper_frequency_window().active,
                    "workspace drop did not pause the conditional bank");
            } else {
                // This is Live's loss order: snapshot the current qualified
                // mask before releasing its geometry, then leave it paused.
                check(on.set_helper_acquisition_dispatch(false),"first-symbol dispatch pause declined");
                on.clear_helper_frequency_window();
                check(!on.helper_frequency_window().active && !on.helper_acquisition_dispatch().permitted,
                    "retained first-symbol cache kept stale frequency authority");
            }
            check(on.working_bytes()<=allocated,"first-symbol continuation allocated optional memory");lost=true;
        }
        if(lost)check(on.set_helper_acquisition_dispatch(false),"repeated loss refresh declined");
    }
    const auto exact=[&](const auto& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& row){
        return row.second.complete && row.second.bits==bits;});};
    const auto x=exact(a),y=exact(b);
    check(lost && x!=a.end() && y!=b.end(),"helper loss before the first decision discarded ordinary payload or END");
    check(x->first==y->first && x->second.full_pending && y->second.full_pending,
        "first-symbol continuation changed pending identity or exact final-bit progress");
    check(y->second.completion>=end+6*c.sample_rate,"first-symbol continuation bypassed physical absence scoring");
    std::cout<<(pulse?"shared pulse":"segmented")<<" first-symbol loss "<<(first_cell?"inside first cell":"after closed cells")
        <<(trim?" via workspace drop":"")<<" exact01/pending/physicalEND passed\n";
}
void rejected_continuation(bool pulse) {
    const auto c=continuation_config();const auto S=modem::symbol_sample_count(c);const std::uint64_t origin=73;
    modem::PatternCode code(c,c.stream_epoch);const auto chip=code.chip_samples();
    const auto s=search(c,static_cast<double>(origin)/c.sample_rate,true);
    modem::PatternCorrelator receiver(c,s,continuation_budget(c,s,pulse));check_continuation_backend(receiver,pulse);
    check(receiver.set_helper_frequency_window(0,.0001,0,modem::PatternHelperClockInterval{0,0}) &&
          receiver.helper_frequency_window().selected_pairs==1,"noise continuation did not isolate its existing original cell");
    const auto loss=origin+chip/4,terminal=origin+S,checkpoint=terminal+2*S+19;
    std::vector<float> zeros(checkpoint+S,0);std::size_t at=0;bool lost=false,checked=false;
    modem::PatternCompactAcquisitionState before;
    std::uint64_t before_cells=0;
    while(at<zeros.size()) {
        auto n=std::min<std::size_t>(733,zeros.size()-at);
        if(!lost)n=std::min<std::size_t>(n,loss-at);
        if(!checked)n=std::min<std::size_t>(n,checkpoint-at);
        receiver.push(std::span(zeros).subspan(at,n));at+=n;
        if(!lost && at==loss) {
            check(receiver.set_helper_acquisition_dispatch(false),"noise continuation pause declined");
            receiver.clear_helper_frequency_window();lost=true;
        }
        if(lost)check(receiver.set_helper_acquisition_dispatch(false),"repeated noise pause declined");
        check(receiver.take_bursts().empty(),"rejected continuation manufactured payload or END");
        if(!checked && at==checkpoint) {
            before=receiver.compact_acquisition_state();before_cells=receiver.work().cells;
            const auto expected=before.original_arrival_lanes*(checkpoint-origin)-S;
            check(before.skipped_lane_samples==expected,
                "rejected continuation observed a second symbol or missed same-block skipped samples");
            if(!pulse)check(before.accumulated_lane_samples==S,"rejected segmented continuation outlived its single original symbol");
            checked=true;
        }
    }
    const auto after=receiver.compact_acquisition_state();
    check(checked && after.projected_carrier_samples==before.projected_carrier_samples &&
          after.accumulated_lane_samples==before.accumulated_lane_samples && receiver.work().cells==before_cells,
        "repeated false refresh rearmed rejected cold work");
    check(after.observed_samples==zeros.size() && after.skipped_lane_samples==after.original_arrival_lanes*(zeros.size()-origin)-S,
        "rejected continuation lost sample coordinates or its one-symbol work bound");
    const auto retained=receiver.working_bytes();receiver.clear_helper_frequency_window();
    check(receiver.working_bytes()<retained && !receiver.helper_frequency_window().active,
        "empty paused closure failed to release its inactive cache");
    std::cout<<(pulse?"shared pulse":"segmented")<<" rejected first-cell continuation stayed within one symbol\n";
}
void paused_cache(bool pulse) {
    const auto c=continuation_config();const auto s=search(c,0,true);
    const auto budget=continuation_budget(c,s,pulse);
    modem::PatternCorrelator receiver(c,s,budget);check_continuation_backend(receiver,pulse);
    std::vector<std::uint8_t> mask(s.hypotheses.size());
    for(std::size_t i=0;i<mask.size();++i)mask[i]=i%5==2;
    if(!pulse) {
        modem::PatternCorrelator control(c,s,budget);
        check(control.compact_acquisition_state().supported &&
              control.set_compact_acquisition_prior(0,mask) && control.start_compact_acquisition_audit(),
            "paused-cache legacy control lacks a supported backend or valid per-clock mask");
    }
    check(receiver.set_helper_frequency_window(0,.0001,0),"paused-cache initial measurement declined");
    receiver.push(std::vector<float>(modem::pattern_chip_samples(c)/4));
    const auto retained=receiver.working_bytes();
    check(receiver.set_helper_acquisition_dispatch(false),"paused-cache dispatch withdrawal declined");
    receiver.clear_helper_frequency_window();
    check(!receiver.helper_frequency_window().active && receiver.working_bytes()==retained,
        "paused observed symbol lost its lookup cache or kept measurement authority");
    check(!receiver.set_compact_acquisition_prior(0,mask) && !receiver.start_compact_acquisition_audit(),
        "inactive frequency cache was mistaken for legacy acquisition authority");
    check(!receiver.set_helper_frequency_window(0,-1,0) && !receiver.helper_frequency_window().active,
        "invalid measurement reactivated cached frequency authority");
    check(receiver.set_helper_frequency_window(0,.0001,0) && receiver.helper_frequency_window().active &&
          receiver.working_bytes()==retained && receiver.set_helper_acquisition_dispatch(true),
        "fresh qualified measurement failed to reuse the paid cache");
    receiver.push(std::vector<float>(17));receiver.set_helper_acquisition_dispatch(false);
    receiver.clear_helper_frequency_window();const auto before_trim=receiver.working_bytes();
    receiver.trim_optional_workspace();
    check(!receiver.helper_frequency_window().active && !receiver.helper_acquisition_dispatch().permitted &&
          receiver.working_bytes()<before_trim && receiver.take_bursts().empty(),
        "ordinary pressure failed to release paused cache without inventing payload");
    std::cout<<"paused cache revoked authority, reused fresh measurement and yielded to pressure\n";
}
void continuation() {
    paused_cache(false);paused_cache(true);
    for(const bool pulse:{false,true}) {
        first_symbol_loss(pulse,true);first_symbol_loss(pulse,false);rejected_continuation(pulse);
    }
    first_symbol_loss(true,true,true);
}
}
int main(int argc,char** argv){try {
    const std::string mode=argc>1?argv[1]:"all";
    if(mode=="geometry" || mode=="all")geometry();
    if(mode=="native" || mode=="all")payload<modem::PatternReceiver>(false);
    if(mode=="compact" || mode=="all"){payload<modem::PatternCorrelator>(true);payload<modem::PatternCorrelator>(true,false,733);}
    if(mode=="clock" || mode=="all"){payload<modem::PatternReceiver>(false,true);payload<modem::PatternCorrelator>(true,true);payload<modem::PatternCorrelator>(true,true,733);}
    if(mode=="continuation" || mode=="all")continuation();
    if(mode!="geometry" && mode!="native" && mode!="compact" && mode!="clock" && mode!="continuation" && mode!="all")throw Error("unknown frequency-window test mode");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

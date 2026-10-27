#include "datapump/pattern_correlator.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/receiver_strategy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <random>
#include <string>

using namespace datapump;
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
modem::Config config(double seconds=8) {
    modem::Config c;c.sample_rate=256;c.carrier_hz=64;c.bandwidth_hz=8;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;
    c.integration_seconds=seconds;c.scramble=true;c.dsss=true;c.stream_epoch=1800000000;
    c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
    return c;
}
modem::PatternSearch search(const modem::Config& c,double origin=0) {
    modem::PatternSearch s;s.compact_clock_search=true;s.start_offset_seconds=origin;
    s.start_uncertainty_seconds=.125;s.search_stream_phases=true;
    s.worker_threads=1;s.candidate_limit=128;s.bit_limit=256;s.chunk_bits=1;
    s.track_limit=8;s.drift_tolerant=false;s.retain_score=0;
    s.admitted_clock_half_width_ppm=300;s.admitted_clock_cell_half_width_ppm=100;
    const auto spacing=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(const auto bin:{0,-1,1,-2,2,-3,3,-4,4})for(const auto ppm:{0.,-200.,200.})
        s.hypotheses.push_back({bin*spacing,ppm});
    return s;
}
std::vector<std::uint8_t> mask(const modem::PatternSearch& s) {
    std::vector<std::uint8_t> result(s.hypotheses.size());
    for(std::size_t i=0;i<result.size();++i)result[i]=s.hypotheses[i].frequency_offset_hz==0;
    return result;
}
std::vector<float> waveform(const modem::Config& c,const Bytes& bits,std::size_t delay,const modem::PatternPhaseCurve* curve=nullptr) {
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(static_cast<std::size_t>(source.total_samples()));source.read_analytic(analytic);
    std::vector<float> result(delay+analytic.size()+std::max<std::uint64_t>(3*modem::symbol_sample_count(c),7ULL*c.sample_rate));
    std::mt19937 random(529);std::normal_distribution<float> noise(0,.025F);
    const auto phase=std::polar(1.7,.83);
    for(std::size_t i=0;i<result.size();++i)
        result[i]=noise(random)+(i>=delay && i-delay<analytic.size()?static_cast<float>((phase*analytic[i-delay]*(curve?std::polar(1.,curve->value_at(i)):std::complex<double>{1,0})).real()):0.F);
    return result;
}
struct Seen {
    Bytes bits;bool complete=false;std::size_t polls=0;
};
using Identity=std::pair<std::uint64_t,std::uint64_t>;
template<class Receiver>void drain(Receiver& receiver,std::map<Identity,Seen>& rows,const Bytes& expected,bool allow_missing=false) {
    for(const auto& burst:receiver.take_bursts()) {
        if(burst.first_stream_symbol>expected.size())continue;
        auto& row=rows[{burst.stream_first_sample,burst.stream_first_symbol}];
        check(!row.complete,"compact helper published after physical END");
        check(allow_missing || !burst.missing_slots,"compact helper inserted a missing payload slot");
        const auto first=burst.first_stream_symbol-burst.stream_first_symbol;
        if(first!=row.bits.size())continue; // An independently drained alias is not this row.
        row.bits.insert(row.bits.end(),burst.missing_slots,modem::missing_pattern_bit);
        row.bits.insert(row.bits.end(),burst.bits.begin(),burst.bits.end());++row.polls;
        row.complete=burst.complete;
    }
}
void push(modem::PatternCorrelator& receiver,std::span<const float> input,std::size_t chunk=113) {
    for(std::size_t at=0;at<input.size();) {
        const auto count=std::min(chunk,input.size()-at);receiver.push(input.subspan(at,count));at+=count;
    }
}
void matched_payload(bool lose_near_final,bool long_symbol=false,bool pressure=false,bool pause_dispatch=false) {
    auto c=config(long_symbol?128:8);
    if(long_symbol){c.sample_rate=512;c.carrier_hz=128;c.bandwidth_hz=.5;}
    const Bytes bits=long_symbol?Bytes{0,1}:Bytes{0,0,1,1,0,1,0,0,1,0,1,1};
    constexpr std::size_t delay=137;const auto origin=delay+modem::pattern_pulse_padding_samples(c);
    auto s=search(c,static_cast<double>(origin)/c.sample_rate);
    // Resource-only fixture: cold planes are 4 Hz apart over [-16,+16] Hz,
    // inside this 256 Hz capture. No complete RF-coverage claim is made for
    // this deliberately sparse explicit catalogue.
    if(pressure)for(auto& pair:s.hypotheses)pair.frequency_offset_hz*=128;
    const auto selected=mask(s);auto pcm=waveform(c,bits,delay);
    if(pressure) {
        std::mt19937 random(761);std::normal_distribution<float> noise(0,.5F);
        for(auto& value:pcm)value+=noise(random);
    }
    const auto budget=[&] {
        if(!long_symbol)return std::size_t{4*1024*1024};
        modem::PatternCorrelator raw(c,s,8*1024*1024,{true,false});
        const auto initial=raw.reserved_workspace_bytes()+24*1024;
        modem::PatternCorrelator ordinary(c,s,initial);
        const auto promise=ordinary.helper_replay_budget(pcm.size());
        check(promise.has_value(),"long matched ordinary workspace promise unavailable");
        return std::max(initial,promise->workspace_bytes);
    }();
    std::size_t ordinary_peak=0;
    if(pressure) {
        modem::PatternCorrelator pilot(c,s,budget);std::map<Identity,Seen> pilot_rows;
        for(std::size_t at=0;at<pcm.size();) {
            const auto count=std::min<std::size_t>(113,pcm.size()-at);
            pilot.push(std::span(pcm).subspan(at,count));at+=count;drain(pilot,pilot_rows,bits);
        }
        check(std::any_of(pilot_rows.begin(),pilot_rows.end(),[&](const auto& item){return item.second.bits==bits && item.second.complete;}),
              "ordinary workspace pilot did not recover the full payload and physical END");
        ordinary_peak=pilot.work().peak_workspace_bytes;
        std::cerr<<"ordinary pressure pilot measured_peak="<<ordinary_peak<<" RF_centers=[-16,+16]Hz spacing=4Hz\n";
    }
    modem::PatternCorrelator off(c,s,budget),on(c,s,budget);
    if(long_symbol)check(on.work().backend==modem::PatternCorrelationBackend::pulse_segments,
        "128-second matched payload did not use ordinary compact segmented backend");
    check(on.compact_acquisition_state().supported,"ordinary compact capability absent");
    const auto original=on.compact_acquisition_hypotheses();
    check(original.size()==s.hypotheses.size(),"compact helper hid original pairs");
    check(on.set_compact_acquisition_prior(0,selected),"qualified compact prior declined");
    std::map<Identity,Seen> off_rows,on_rows;bool lost=false,reclaimed=false,pressure_armed=false;
    std::size_t common_loan=0,loan_frontier=0;
    if(pressure || long_symbol) {
        const auto promised=off.helper_replay_budget(pcm.size());
        std::cerr<<"memory initial long="<<long_symbol<<" pressure="<<pressure<<" grant="<<budget
            <<" off_reserved="<<off.reserved_workspace_bytes()<<" on_reserved="<<on.reserved_workspace_bytes()
            <<" ordinary_full_future_promise="<<(promised?promised->workspace_bytes:0)<<'\n';
    }
    for(std::size_t at=0;at<pcm.size();) {
        const auto count=std::min<std::size_t>(113,pcm.size()-at);
        const auto input=std::span(pcm).subspan(at,count);
        const auto consume=[&](auto& receiver,const char* side) {
            const auto before=receiver.compact_acquisition_state();
            const auto working=receiver.working_bytes(),reserved=receiver.reserved_workspace_bytes();
            try{receiver.push(input);}catch(const Error&) {
                const auto after=receiver.compact_acquisition_state();
                std::cerr<<"memory failure side="<<side<<" supplied_frontier="<<at<<" supplied_count="<<count
                    <<" observed="<<before.observed_samples<<"->"<<after.observed_samples
                    <<" working="<<working<<"->"<<receiver.working_bytes()
                    <<" reserved="<<reserved<<"->"<<receiver.reserved_workspace_bytes()
                    <<" loan="<<(pressure_armed?common_loan:budget)<<" loan_frontier="<<loan_frontier
                    <<" prior="<<before.prior_active<<"->"<<after.prior_active
                    <<" yielded="<<after.workspace_yielded<<" projected="<<after.projected_carrier_samples
                    <<" skipped="<<after.skipped_carrier_samples<<" active_pairs="<<after.active_pairs
                    <<" protected_pairs="<<after.protected_pairs<<'\n';throw;
            }
        };
        consume(off,"OFF");consume(on,"ON");at+=count;drain(off,off_rows,bits);drain(on,on_rows,bits);
        if(off.synchronized() && on.synchronized())check(!off.cold_acquisition_only() && !on.cold_acquisition_only(),
            "accepted payload was incorrectly certified cold for helper handoff");
        // This fixed frontier precedes the first payload admission. The
        // independently measured ordinary peak already funds every later
        // mandatory allocation; shrinking here tests yield inside a serial
        // completion whose cold projection banks were actually omitted.
        if(pressure && !pressure_armed && at>=origin+modem::symbol_sample_count(c)/2 &&
           on.compact_acquisition_state().skipped_carrier_samples>0) {
            check(at<origin+modem::symbol_sample_count(c) && !off.synchronized() && !on.synchronized(),
                  "pressure loan did not precede the first payload admission");
            common_loan=ordinary_peak;loan_frontier=at;
            off.set_workspace_bytes(common_loan);on.set_workspace_bytes(common_loan);pressure_armed=true;
            std::cerr<<"memory common loan="<<common_loan<<" arm_frontier="<<loan_frontier
                <<" off_working="<<off.working_bytes()<<" on_working="<<on.working_bytes()<<'\n';
            check(on.compact_acquisition_state().prior_active,"pilot-derived loan reclaimed helper before payload allocation");
        }
        if(pressure_armed && !reclaimed && !on.compact_acquisition_state().prior_active) {
            check(on.compact_acquisition_state().skipped_carrier_samples>0,
                  "compact pressure fixture reclaimed before omitting any cold bank");
            check(on.synchronized(),"compact control was not reclaimed at accepted payload allocation");
            reclaimed=true;off.set_workspace_bytes(budget);on.set_workspace_bytes(budget);
            std::cout<<"matched common pressure loan="<<common_loan<<" arm_frontier="<<loan_frontier<<" reclaim_frontier="<<at<<'\n';
        }
        if(lose_near_final && !lost && at>=origin+(bits.size()-1)*modem::symbol_sample_count(c)) {
            if(pause_dispatch) {
                check(!on.cold_acquisition_only(),"dispatch-loss fixture lacks an ordinary payload dependency");
                check(on.set_helper_acquisition_dispatch(false),"compact dispatch expiry declined");
                check(!on.helper_acquisition_dispatch().permitted,"compact dispatch remained enabled after expiry");
            } else check(on.clear_compact_acquisition_prior(),"helper loss did not restore broad compact acquisition");
            lost=true;
        }
        check(on.working_bytes()<=budget,"optional compact helper exceeded workspace");
    }
    const auto exact=[&](const auto& rows) {
        return std::find_if(rows.begin(),rows.end(),[&](const auto& item){return item.second.bits==bits && item.second.complete;});
    };
    const auto a=exact(off_rows),b=exact(on_rows);
    if(a==off_rows.end() || b==on_rows.end()) {
        for(const auto* rows:{&off_rows,&on_rows}) {for(const auto& [id,row]:*rows) {std::cerr<<id.first<<':'<<id.second<<' ';for(auto bit:row.bits)std::cerr<<unsigned(bit);std::cerr<<" end="<<row.complete<<'\n';}}
    }
    check(a!=off_rows.end() && b!=on_rows.end(),"matched compact helper changed exact final bits or physical completion");
    check(!pressure || reclaimed,"payload allocation never reclaimed optional compact assistance");
    check(a->first==b->first,"compact helper changed pending-row physical identity");
    check(b->second.polls>=bits.size(),"compact helper withheld per-bit progress");
    const auto before=off.compact_acquisition_state(),after=on.compact_acquisition_state();
    check(after.projected_carrier_samples<before.projected_carrier_samples,
          "compact helper retained cold carrier projection work");
    check(after.accumulated_lane_samples<before.accumulated_lane_samples,
          "compact helper retained cold fit accumulation work");
    check(after.skipped_carrier_samples && after.skipped_lane_samples,"compact skipped-work counters stayed empty");
    off.finish();on.finish();
    std::cout<<"matched bits="<<bits.size()<<" loss="<<lose_near_final<<" pressure="<<pressure
        <<" projected="<<before.projected_carrier_samples<<"->"<<after.projected_carrier_samples
        <<" symbol_seconds="<<static_cast<double>(modem::symbol_sample_count(c))/c.sample_rate
        <<" accumulated="<<before.accumulated_lane_samples<<"->"<<after.accumulated_lane_samples<<'\n';
}
void audit_and_reclaim() {
    const auto c=config();auto s=search(c);const auto selected=mask(s);
    modem::PatternCorrelator receiver(c,s,4*1024*1024);
    auto bad=selected;bad[1]=0;
    check(!receiver.set_compact_acquisition_prior(0,bad),"compact helper dropped an unresolved LF alternative");
    check(!receiver.set_compact_acquisition_prior(.1,selected),"unsupported compact frequency translation mutated state");
    std::vector<float> silence(static_cast<std::size_t>(modem::symbol_sample_count(c)/2));
    push(receiver,silence); // Every cold noise-only RF lane has a partial Fit.
    check(receiver.set_compact_acquisition_prior(0,selected),"compact prior could not narrow existing noise fits");
    const auto before=receiver.compact_acquisition_state();push(receiver,silence);
    const auto narrow=receiver.compact_acquisition_state();
    check(narrow.accumulated_lane_samples-before.accumulated_lane_samples<silence.size()*s.hypotheses.size()/2,
          "initial compact helper work waited for every cold symbol to finish");
    check(!receiver.initial_search_complete(),"address-only cold advancement falsely certified initial coverage");
    check(receiver.start_compact_acquisition_audit(),"compact broad audit refused");
    push(receiver,silence);
    check(receiver.compact_acquisition_state().audit_active,"compact audit closed before fresh full-symbol coverage");
    for(unsigned i=0;i<5 && receiver.compact_acquisition_state().audit_active;++i)push(receiver,silence);
    const auto audited=receiver.compact_acquisition_state();
    check(audited.audit_complete && !audited.audit_active && audited.audit_scored_lanes==audited.original_arrival_lanes,
          "compact audit did not score every original arrival cell");
    check(receiver.initial_search_complete(),"fresh original compact audit did not certify initial coverage");
    check(audited.broad_audit_full_symbols==audited.original_arrival_lanes && audited.broad_audit_accumulated_lane_samples,
          "compact audit actual-work accounting omitted original lanes");
    check(receiver.start_compact_acquisition_audit(),"repeat compact audit refused");push(receiver,silence);
    check(receiver.compact_acquisition_state().audit_active,"repeat audit did not retain original full-window obligation");
    receiver.trim_optional_workspace();
    check(receiver.compact_acquisition_state().workspace_yielded && !receiver.compact_acquisition_state().prior_active,
          "payload workspace pressure retained optional helper control");
    check(!receiver.set_compact_acquisition_prior(0,selected),"optional control reinstall starved an unfinished original broad audit");
    push(receiver,silence);check(receiver.take_bursts().empty(),"skipped cold samples became payload or physical END");
}
void logical_trial_parity() {
    const auto c=config();auto s=search(c);s.start_uncertainty_seconds=0;
    modem::PatternCorrelator off(c,s,4*1024*1024),on(c,s,4*1024*1024);
    check(on.set_compact_acquisition_prior(0,mask(s)),"logical trial fixture prior declined");
    std::size_t observed=0;
    for(unsigned index=0;index<3;++index) {
        // 27 current broad events fit the original compact history cap32.
        // Include the slowest original LF endpoint without completing a later
        // symbol; history retention is not changed to accommodate this test.
        const auto end=static_cast<std::size_t>((index+1)*modem::symbol_sample_count(c)+2);
        std::vector<float> silence(end-observed);push(off,silence);push(on,silence);observed=end;
        const auto broad=off.candidates(),narrow=on.candidates();std::size_t compared=0;
        std::vector<bool> used(broad.size());
        check(std::count_if(broad.begin(),broad.end(),[&](const auto& item){return item.stream_symbol==index && item.frequency_hz==c.carrier_hz;})==3,
              "logical trial fixture omitted a current original selected LF fit");
        for(const auto& candidate:narrow)if(candidate.stream_symbol==index) {
            const auto found=std::find_if(broad.begin(),broad.end(),[&](const auto& original) {
                return !used[static_cast<std::size_t>(&original-broad.data())] && original.frequency_hz==candidate.frequency_hz && original.first_sample==candidate.first_sample &&
                    original.end_sample==candidate.end_sample && original.stream_symbol==candidate.stream_symbol &&
                    original.stream_phase_samples==candidate.stream_phase_samples &&
                    original.admission_threshold==candidate.admission_threshold;
            });
            if(found==broad.end())std::cerr<<"trial diagnostic missing original: symbol="<<index<<" first="<<candidate.first_sample<<" end="<<candidate.end_sample<<" phase="<<candidate.stream_phase_samples<<'\n';
            else if(found->admission_threshold!=candidate.admission_threshold)std::cerr<<"trial diagnostic symbol="<<index<<" first="<<candidate.first_sample<<" end="<<candidate.end_sample<<" phase="<<candidate.stream_phase_samples<<" original_threshold="<<found->admission_threshold<<" conditional_threshold="<<candidate.admission_threshold<<" skipped_logical="<<on.compact_acquisition_state().skipped_logical_trials<<'\n';
            check(found!=broad.end() && found->score==candidate.score && found->alternative_score==candidate.alternative_score &&
                  found->admission_threshold==candidate.admission_threshold,
                  "conditional cold omission changed original selected-fit detector-choice trial penalties");
            used[static_cast<std::size_t>(found-broad.begin())]=true;++compared;
        }
        check(compared==3,"logical trial fixture omitted a current selected LF fit");
    }
    check(on.compact_acquisition_state().skipped_logical_trials>0,
          "conditionally omitted RF boundaries did not retain original logical trial penalties");
    check(off.take_bursts().empty() && on.take_bursts().empty(),"logical penalties supplied payload or absence completion");
}
void periodic_aliases() {
    const auto c=config();auto s=search(c);s.start_uncertainty_seconds=1;s.search_stream_phases=false;
    modem::PatternCorrelator receiver(c,s,4*1024*1024);
    std::vector<float> silence(1024);push(receiver,silence);
    check(receiver.set_compact_acquisition_prior(0,mask(s)),"periodic fixture frequency prior declined");
    const modem::PatternPeriodicTimingPrior prior{1024,0,.4L*c.sample_rate,1};
    check(receiver.set_compact_timing_prior(prior),"qualified periodic compact prior declined");
    const auto state=receiver.compact_acquisition_state();
    check(state.timing_prior_active && state.timing_narrowing_active &&
          state.timing_selected_arrival_lanes<state.original_arrival_lanes,
          "independently qualified punctuation did not remove any justified arrival work");
    check(state.active_arrival_lanes>3*state.selected_pairs,
          "periodic compact timing selected a nearest alias instead of their union");
    check(receiver.clear_compact_timing_prior(),"punctuation loss could not restore arrival coverage");
    const auto cleared=receiver.compact_acquisition_state();
    check(!cleared.timing_prior_active && !cleared.timing_narrowing_active &&
          cleared.active_arrival_lanes>state.active_arrival_lanes,
          "punctuation loss retained narrowed arrival coverage");
    auto shifted=c;shifted.stream_phase_samples=51;
    modem::PatternCorrelator explicit_phase(shifted,s,4*1024*1024),equivalent(c,s,4*1024*1024);
    push(explicit_phase,silence);push(equivalent,silence);
    auto translated=prior;translated.source_phase_samples=-51;
    check(explicit_phase.set_compact_acquisition_prior(0,mask(s)) && explicit_phase.set_compact_timing_prior(prior) &&
          equivalent.set_compact_acquisition_prior(0,mask(s)) && equivalent.set_compact_timing_prior(translated),
          "nonzero source-phase periodic setup failed");
    const auto shifted_state=explicit_phase.compact_acquisition_state(),equivalent_state=equivalent.compact_acquisition_state();
    check(shifted_state.timing_selected_arrival_lanes==equivalent_state.timing_selected_arrival_lanes &&
          shifted_state.active_arrival_lanes==equivalent_state.active_arrival_lanes,
          "periodic prior omitted nonzero original private source-phase translation");
    s.search_stream_phases=true;
    modem::PatternCorrelator ambiguous(c,s,4*1024*1024);push(ambiguous,silence);
    check(ambiguous.set_compact_acquisition_prior(0,mask(s)) && ambiguous.set_compact_timing_prior(prior),
          "ambiguous source-phase periodic prior setup failed");
    const auto unresolved=ambiguous.compact_acquisition_state();
    check(unresolved.timing_prior_active && !unresolved.timing_narrowing_active &&
          unresolved.timing_selected_arrival_lanes==unresolved.original_arrival_lanes,
          "punctuation prior removed unresolved canonical source-phase cell aliases");
}
void clock_domain_and_cold() {
    auto c=config();auto s=search(c);s.hypotheses={{0,99},{0,100},{0,101}};
    s.qualified_clock_interval=modem::PatternClockInterval{99,101,.5};
    check(modem::pattern_clock_interval_valid(s),"exactly touching qualified clock cells lost coverage");
    const auto good=s;s.qualified_clock_interval->cell_half_width_ppm=std::nextafter(.5,0.);
    check(!modem::pattern_clock_interval_valid(s),"qualified clock certificate bridged a true tiny cell gap");
    s=good;s.hypotheses.erase(s.hypotheses.begin()+1);
    check(!modem::pattern_clock_interval_valid(s),"qualified clock certificate bridged a missing interior cell");
    s=good;s.qualified_clock_interval=modem::PatternClockInterval{100,100,0};
    check(modem::pattern_clock_interval_valid(s),"qualified exact clock point declined");
    s.qualified_clock_interval=modem::PatternClockInterval{100,101,0};
    check(!modem::pattern_clock_interval_valid(s),"zero-radius clock points certified a continuous interval");
    s=good;
    modem::PatternCorrelator receiver(c,s,4*1024*1024);
    const auto state=receiver.compact_acquisition_state();
    check(state.qualified_clock_interval && state.qualified_clock_interval->lower_ppm==99 &&
          state.qualified_clock_interval->upper_ppm==101 && state.admitted_clock_half_width_ppm==300,
          "qualified nonzero clock domain rewrote ordinary baseline coverage metadata");
    check(receiver.cold_acquisition_only(),"cold compact owner was not cold before input");
    std::vector<float> silence(113);receiver.push(silence);
    check(receiver.cold_acquisition_only(),"unaccepted acquisition statistics prevented authorized cold handoff");
    auto burst=c;burst.symbol_burst.wait_symbols=1;bool rejected=false;
    try{modem::PatternCorrelator unsupported(burst,s,4*1024*1024);}
    catch(const Error& error){rejected=std::string(error.what()).find("continuous coherent")!=std::string::npos;}
    check(rejected,"qualified clock interval entered unsupported Burst geometry");
    auto amplitude=c;amplitude.receiver_strategy=modem::ReceiverStrategy::amplitude_only;
    const auto plan=modem::resolve_receiver_strategy(amplitude,s,4*1024*1024);
    check(!plan.supported,"qualified asymmetric clock domain entered an unsupported energy backend");
}
void feed_native(modem::PatternReceiver& receiver,std::span<const float> input) {
    while(!input.empty() || (receiver.acquisition_state().ready &&
          (receiver.acquisition_state().prior_pending || receiver.helper_acquisition_dispatch().permitted))) {
        const auto state=receiver.acquisition_state();check(state.supported,"native calendar split acquisition capability absent");
        if(state.ready && (state.prior_pending || receiver.helper_acquisition_dispatch().permitted)) {
            if(state.prior_active){receiver.process_prior_acquisition();receiver.resolve_acquisition(true);}
            else receiver.process_acquisition();
        } else {
            const auto count=receiver.observe_acquisition(input);check(count!=0,"native calendar observation made no progress");
            input=input.subspan(count);
        }
    }
}
void native_calendar(bool competing,bool pause_dispatch=false,bool large_span=false,bool ordinary_push=false) {
    auto c=config(.25);c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=3;hops->dwell_seconds=.4;
    hops->shifts[0]=1000000;hops->shifts[1]=1010000;hops->shifts[2]=1033000;
    hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
    c.fake_hopping=modem::resolve_fhss_power(*hops,c);
    check(c.fake_hopping->defer_symbols,"native calendar regression did not exercise real FHSS deferral");
    Bytes first{0,0,1,0,1,1,0,0};const Bytes second{1,1,0,0,1,0,1,1};
    if(large_span) {
        const auto pattern=first;for(unsigned i=1;i<4;++i)first.insert(first.end(),pattern.begin(),pattern.end());
    }
    constexpr std::size_t delay=137;const auto base=delay+modem::pattern_pulse_padding_samples(c);
    auto s=search(c,static_cast<double>(base)/c.sample_rate);s.compact_clock_search=false;
    // The one-span API test deliberately cannot drain within its call. Use
    // the ordinary default event batch (still capped by the SAME bit quota),
    // rather than requesting 32 one-bit events from an eight-event queue.
    // take_bursts() still flushes each accepted prefix at every earlier poll.
    if(large_span)s.chunk_bits=modem::PatternSearch{}.chunk_bits;
    s.search_stream_phases=false;s.early_qualified_fft=false;s.component_qualified_search=false;
    s.qualified_start_window=modem::PatternStartWindow{static_cast<long double>(base),1,competing?2.L*c.sample_rate:2.L};
    auto pcm=waveform(c,first,delay);
    modem::PatternTransmitter original(first,c,c.stream_epoch,0,false);
    const auto loss_at=delay+original.total_samples()-modem::symbol_sample_count(c)/2;
    // Deferred FHSS gaps never contribute absent-symbol duration. Use the
    // ordinary production capture-tail bound, plus pulse and FFT support.
    pcm.resize(delay+original.total_samples()+(competing?c.sample_rate:0)+
        modem::pattern_absence_capture_samples(c)+2*modem::symbol_sample_count(c)+
        2*modem::pattern_pulse_padding_samples(c));
    if(competing) {
        const auto later=waveform(c,second,delay+c.sample_rate);pcm.resize(std::max(pcm.size(),later.size()));
        for(std::size_t i=0;i<later.size();++i)pcm[i]+=2*later[i];
    }
    modem::PatternReceiver off(c,64*1024*1024,s),on(c,64*1024*1024,s);
    check(!on.clock_windowed() && on.acquisition_state().supported,"deferred FHSS was routed into an unsupported compact decoder");
    check(!on.set_acquisition_prior(.1,mask(s)),"calendar helper accepted unsupported translated reference");
    check(on.set_acquisition_prior(0,mask(s)),"origin-zero native calendar helper prior declined");
    check(!on.phase_assistance_supported(),"calendar helper falsely advertised phase assistance");
    std::map<Identity,Seen> off_rows,on_rows;bool loss=false;
    const auto collect=[](auto& receiver,auto& rows) {
        for(const auto& burst:receiver.take_bursts()) {
            auto& row=rows[{burst.stream_first_sample,burst.stream_first_symbol}];
            check(!row.complete,"calendar owner changed after physical END");
            if(burst.first_stream_symbol-burst.stream_first_symbol!=row.bits.size())continue;
            row.bits.insert(row.bits.end(),burst.missing_slots,modem::missing_pattern_bit);
            row.bits.insert(row.bits.end(),burst.bits.begin(),burst.bits.end());row.complete=burst.complete;++row.polls;
        }
    };
    std::size_t large_span_samples=0;
    for(std::size_t at=0;at<pcm.size();) {
        const auto count=large_span && loss?pcm.size()-at:std::min<std::size_t>(113,pcm.size()-at);
        const auto input=std::span(pcm).subspan(at,count);
        // OFF is the ordinary streaming control and retains its original
        // bounded push/drain cadence. Only ON tests a single large call.
        for(std::size_t baseline_at=0;baseline_at<input.size();) {
            const auto n=std::min<std::size_t>(113,input.size()-baseline_at);
            off.push(input.subspan(baseline_at,n));collect(off,off_rows);baseline_at+=n;
        }
        if(large_span && loss) {
            large_span_samples=count;
            if(ordinary_push)on.push(input);else feed_native(on,input);
        } else feed_native(on,input);
        at+=count;collect(on,on_rows);
        const auto accepted_prefix=std::any_of(on_rows.begin(),on_rows.end(),[&](const auto& item) {
            const auto& row=item.second;
            return !row.complete && !row.bits.empty() && row.bits.size()<first.size() &&
                std::equal(row.bits.begin(),row.bits.end(),first.begin());
        });
        if(!competing && !loss && (large_span?accepted_prefix:at>=loss_at)) {
            if(pause_dispatch) {
                check(!on.cold_acquisition_only(),"native dispatch-loss fixture lacks payload dependency");
                check(on.set_helper_acquisition_dispatch(false),"calendar dispatch expiry declined");
            } else check(on.clear_acquisition_prior(),"calendar helper loss could not restore original acquisition");
            loss=true;
        }
    }
    const auto& expected=competing?second:first;
    const auto find=[&](const auto& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& item){return item.second.complete && item.second.bits==expected;});};
    const auto a=find(off_rows),b=find(on_rows);
    if(a==off_rows.end() || b==on_rows.end())for(const auto* rows:{&off_rows,&on_rows})for(const auto& [id,row]:*rows) {
        std::cerr<<"calendar row "<<id.first<<':'<<id.second<<' ';for(auto bit:row.bits)std::cerr<<unsigned(bit);std::cerr<<" END="<<row.complete<<'\n';
    }
    check(a!=off_rows.end() && b!=on_rows.end(),"native calendar helper changed competing-start recovery or exact final bits/END");
    check(competing || loss,"native calendar fixture never lost the helper near its final symbol");
    if(large_span)check(large_span_samples>12*modem::symbol_sample_count(c),
        "paused accepted-track regression did not ingest a genuinely large payload/tail span");
    check(a->first==b->first,"native calendar helper changed ordinary pending-row identity");
    const auto baseline=off.fft_work(),assisted=on.fft_work();
    check(assisted.conditional_template_jobs>0 && assisted.template_jobs<baseline.template_jobs,
          "native calendar helper retained exhaustive expensive acquisition");
    std::cout<<"calendar competing="<<competing<<" exact="<<expected.size()<<" large_span="<<large_span_samples
        <<" ordinary_push="<<ordinary_push<<" jobs="<<baseline.template_jobs<<"->"<<assisted.template_jobs<<'\n';
}
// Negative lease tests use silence and actual work counters. They do not use
// known message length as a receiver completion or allocation input.
void dispatch_expiry_negative() {
    auto c=config(.25);c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=3;hops->dwell_seconds=.4;
    hops->shifts[0]=1000000;hops->shifts[1]=1010000;hops->shifts[2]=1033000;
    hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
    c.fake_hopping=modem::resolve_fhss_power(*hops,c);
    auto s=search(c);s.compact_clock_search=false;s.search_stream_phases=false;
    s.early_qualified_fft=false;s.component_qualified_search=false;
    modem::PatternReceiver cold(c,64*1024*1024,s);
    check(c.fake_hopping->defer_symbols && !cold.clock_windowed(),"expiry fixture lost native calendar B geometry");
    check(cold.set_acquisition_prior(0,mask(s)),"native expiry prior declined");
    check(cold.set_helper_acquisition_dispatch(false),"native cold dispatch pause declined");
    const auto before=cold.fft_work();
    std::vector<float> block(4096);std::size_t consumed=0;
    for(unsigned i=0;i<64;++i) {
        const auto n=cold.observe_acquisition(block);check(n==block.size(),"paused native fence blocked physical observation");consumed+=n;
        cold.process_prior_acquisition();cold.process_acquisition();
    }
    const auto after=cold.fft_work();
    check(after.template_jobs==before.template_jobs && after.threshold_trials==before.threshold_trials,
          "expired native source dispatched new acquisition jobs or detector trials");
    check(cold.acquisition_state().observed_samples==consumed && cold.helper_acquisition_dispatch().retired,
          "bounded native ring did not explicitly retire overwritten cold support");
    check(!cold.initial_search_complete() && !cold.set_helper_acquisition_dispatch(true),
          "overwritten unscored native support became coverage or historical replay");
    check(cold.take_bursts().empty() && cold.cold_acquisition_only(),"unscored native input became payload or END");

    modem::PatternReceiver pending(c,64*1024*1024,s);
    check(pending.set_acquisition_prior(0,mask(s)),"pending expiry prior declined");
    while(!pending.acquisition_state().ready) {
        const auto n=pending.observe_acquisition(block);check(n>0,"pending expiry could not fill first ordinary fence");
    }
    pending.process_prior_acquisition();check(pending.acquisition_state().prior_pending,"conditional receipt was not retained");
    const auto scored=pending.fft_work();
    check(pending.set_helper_acquisition_dispatch(false),"pending receipt expiry declined");
    check(pending.observe_acquisition(block)==0,"expiry let PCM overwrite a pending receipt");
    pending.process_prior_acquisition();pending.resolve_acquisition(false);
    const auto resolved=pending.fft_work();
    check(!pending.acquisition_state().prior_pending && resolved.template_jobs==scored.template_jobs &&
          resolved.original_audit_template_jobs==scored.original_audit_template_jobs,
          "expired scored receipt caused new broad historical work instead of one resolution");
    pending.resolve_acquisition(false);pending.process_prior_acquisition();
    check(pending.fft_work().template_jobs==scored.template_jobs,"resolved expired receipt dispatched twice");

    auto long_c=config(128);long_c.sample_rate=512;long_c.carrier_hz=128;long_c.bandwidth_hz=.5;
    auto long_s=search(long_c);long_s.start_uncertainty_seconds=0;long_s.search_stream_phases=false;
    // Keep the formerly failing 8 MiB shape explicit: it affords the ordinary
    // shared pulse lattices, whose legacy segmented-only mask API must decline.
    modem::PatternCorrelator compact(long_c,long_s,8*1024*1024);
    check(compact.work().lattices>0 && !compact.compact_acquisition_state().supported,
          "128-second expiry control no longer exercises the legacy unsupported pulse shape");
    check(!compact.set_compact_acquisition_prior(0,mask(long_s)),
          "legacy segmented-only prior unexpectedly accepted the pulse-lattice shape");
    // This is the actual manual receive API. It conditions the original bank
    // in place and retains the complete unresolved LF domain.
    check(compact.set_helper_frequency_window(0,.0001,0),"128-second manual frequency window declined");
    const auto frequency=compact.helper_frequency_window();
    check(frequency.supported && frequency.active && frequency.original_pairs==long_s.hypotheses.size() &&
          frequency.selected_pairs==3 && !frequency.clock_qualified &&
          frequency.unresolved_clock_cell_half_width_ppm==*long_s.admitted_clock_cell_half_width_ppm,
          "128-second manual window did not narrow RF while retaining all original clock cells");
    // The legacy regression still has its own supported, bounded ordinary
    // segmented constructor policy; changing APIs must not erase that coverage.
    modem::PatternCorrelator raw(long_c,long_s,8*1024*1024,{true,false});
    const auto legacy_budget=raw.reserved_workspace_bytes()+24*1024;
    modem::PatternCorrelator legacy(long_c,long_s,legacy_budget);
    check(legacy.work().backend==modem::PatternCorrelationBackend::pulse_segments &&
          legacy.compact_acquisition_state().supported && legacy.set_compact_acquisition_prior(0,mask(long_s)),
          "bounded 128-second legacy segmented prior declined");
    check(legacy.working_bytes()<=legacy_budget,"legacy 128-second helper exceeded its bounded constructor grant");
    check(compact.set_helper_acquisition_dispatch(false),"128-second dispatch pause declined");
    const auto compact_before=compact.compact_acquisition_state();
    std::vector<float> silence(static_cast<std::size_t>(2*modem::symbol_sample_count(long_c)+17));push(compact,silence);
    const auto compact_after=compact.compact_acquisition_state();
    check(compact_after.projected_carrier_samples==compact_before.projected_carrier_samples &&
          compact_after.accumulated_lane_samples==compact_before.accumulated_lane_samples,
          "expired 128-second compact source retained cold numeric work");
    check(compact_after.observed_samples==silence.size() && !compact.initial_search_complete() &&
          compact.take_bursts().empty(),"compact cold skipping manufactured coverage, payload or END");
    check(compact.set_helper_acquisition_dispatch(true),"still-bounded compact owner could not requalify at current frontier");
    const auto resumed=compact.compact_acquisition_state();push(compact,std::vector<float>(113));
    check(compact.compact_acquisition_state().broad_audit_full_symbols==resumed.broad_audit_full_symbols &&
          compact.take_bursts().empty(),"compact requalification replayed a crossed partial symbol as absence");
}
void phase_payload(bool long_symbol,bool zero_curve) {
    auto c=config(long_symbol?128:8);if(long_symbol){c.sample_rate=512;c.carrier_hz=128;c.bandwidth_hz=.5;}
    const Bytes bits=long_symbol?Bytes{0,1}:Bytes{0,0,1,1,0,1};constexpr std::size_t delay=137;
    const auto base=delay+modem::pattern_pulse_padding_samples(c);auto s=search(c,static_cast<double>(base)/c.sample_rate);
    modem::PatternPhaseCurve curve;check(curve.configure(41,c.sample_rate,0,0,32),"compact phase curve configure failed");
    const auto symbol=modem::symbol_sample_count(c);const auto slope=zero_curve?0.:.02*2*std::acos(-1.)/c.sample_rate;
    const auto change=64+symbol+symbol/3;
    const std::array<modem::PatternPhaseSegment,2> segments{{{64,0,slope},{change,static_cast<double>(change-64)*slope,-slope}}};
    check(curve.append(segments,0,static_cast<std::uint64_t>(base+(bits.size()+4)*symbol)),"qualified continuous phase curve append failed");
    const auto pcm=waveform(c,bits,delay,&curve);
    modem::PatternCorrelator raw(c,s,8*1024*1024,{true,false});
    const auto budget=long_symbol?raw.reserved_workspace_bytes()+24*1024:4*1024*1024;
    modem::PatternCorrelator optimized(c,s,budget);
    std::optional<modem::PatternCorrelator> unchanged;
    unchanged.emplace(c,s,8*1024*1024,modem::PatternCorrelatorOptions{true,false});
    check(raw.set_compact_acquisition_prior(0,mask(s)) && optimized.set_compact_acquisition_prior(0,mask(s)),"phase fixture frequency prior declined");
    if(unchanged)check(unchanged->set_compact_acquisition_prior(0,mask(s)),"zero-curve ordinary reference prior declined");
    check(raw.set_compact_phase_view(&curve) && optimized.set_compact_phase_view(&curve),"qualified compact phase view was not attached at birth");
    if(long_symbol)check(optimized.work().backend==modem::PatternCorrelationBackend::pulse_segments,"phase regression did not use segmented real-Gram backend");
    std::map<Identity,Seen> raw_rows,optimized_rows,unchanged_rows;bool closed=false,observed_active=false;
    double corrected_score=0,uncurved_score=0;
    const auto first_score=[](const auto& candidates) {
        double best=0;for(const auto& item:candidates)if(item.stream_symbol==0)best=std::max(best,item.score);return best;
    };
    for(std::size_t at=0;at<pcm.size();) {
        const auto count=std::min<std::size_t>(113,pcm.size()-at);const auto input=std::span(pcm).subspan(at,count);
        raw.push(input);optimized.push(input);at+=count;drain(raw,raw_rows,bits);drain(optimized,optimized_rows,bits);
        if(unchanged){unchanged->push(input);drain(*unchanged,unchanged_rows,bits,!zero_curve);}
        corrected_score=std::max(corrected_score,first_score(raw.candidates()));
        uncurved_score=std::max(uncurved_score,first_score(unchanged->candidates()));
        observed_active=observed_active || optimized.compact_phase_view_active();
        if(!closed && at>=base+(bits.size()-1)*symbol) {curve.close();closed=true;}
    }
    const auto exact=[&](const auto& rows){return std::any_of(rows.begin(),rows.end(),[&](const auto& item){return item.second.bits==bits && item.second.complete;});};
    check(exact(raw_rows) && exact(optimized_rows),"raw/segmented compact phase lost final bits or physical END after reference loss");
    if(zero_curve)check(exact(unchanged_rows),"zero phase view differed from the ordinary waveform's exact bits/END");
    check(observed_active && optimized.compact_acquisition_state().phase_projected_carrier_samples>0 &&
          !optimized.compact_phase_view_active(),"compact phase activation/loss did not reflect actual physical projection work");
    check(!optimized.set_compact_phase_view(nullptr),"compact phase loss changed the established affine basis");
    const auto compare=[](const auto& a,const auto& b) {
        check(a.size()==b.size(),"phase real-Gram optimization omitted completed fits");
        for(std::size_t i=0;i<a.size();++i)check(a[i].bit==b[i].bit && a[i].first_sample==b[i].first_sample &&
            a[i].end_sample==b[i].end_sample && a[i].stream_symbol==b[i].stream_symbol &&
            std::abs(a[i].score-b[i].score)<2e-7*std::max(1.,a[i].score) &&
            std::abs(a[i].alternative_score-b[i].alternative_score)<2e-7*std::max(1.,a[i].alternative_score),
            "phase physical oscillator Gram/moments changed original real-IQ evidence");
    };
    compare(raw.candidates(),optimized.candidates());
    if(zero_curve)compare(raw.candidates(),unchanged->candidates());
    // Observe scores while they are resident in bounded history, never as
    // completion authority or as a requirement that assistance always helps.
    std::cout<<"phase long="<<long_symbol<<" zero="<<zero_curve<<" exact="<<bits.size()
        <<" projected="<<optimized.compact_acquisition_state().phase_projected_carrier_samples
        <<" moments="<<optimized.compact_acquisition_state().phase_carrier_moment_updates
        <<" uncurved_exact="<<exact(unchanged_rows)<<" first_score="<<uncurved_score<<"->"<<corrected_score<<'\n';
}
void phase_coordinate_boundary() {
    const auto c=config();auto s=search(c);s.input_sample_origin=UINT64_MAX-17;
    modem::PatternPhaseCurve curve;check(curve.configure(91,c.sample_rate,0,0,32),"phase boundary curve configure failed");
    modem::PatternCorrelator receiver(c,s,4*1024*1024);
    check(receiver.set_compact_phase_view(&curve),"phase boundary view attach failed");
    const auto rejected=[&](std::size_t count) {
        const auto before=receiver.compact_acquisition_state();bool failed=false;
        try{receiver.push(std::vector<float>(count));}catch(const Error& error) {
            failed=std::string(error.what()).find("phase input coordinate overflow")!=std::string::npos;
        }
        const auto after=receiver.compact_acquisition_state();
        check(failed && before.observed_samples==after.observed_samples &&
              before.projected_carrier_samples==after.projected_carrier_samples &&
              before.accumulated_lane_samples==after.accumulated_lane_samples,
              "phase coordinate overflow consumed part of the caller span");
    };
    rejected(18);receiver.push(std::vector<float>(17));
    check(receiver.compact_acquisition_state().observed_samples==17,"phase input endpoint was rejected before UINT64_MAX");
    rejected(1);
}
void long_geometry() {
    auto c=config(128);c.sample_rate=512;c.carrier_hz=128;c.bandwidth_hz=.5;
    auto s=search(c);s.start_uncertainty_seconds=0;s.search_stream_phases=false;s.candidate_limit=4;s.track_limit=1;s.bit_limit=8;
    s.hypotheses.clear();const auto spacing=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(const auto bin:{0,-1,1,-2,2,-3,3,-4,4})for(const auto ppm:{0.,-.0001,.0001,-.0002,.0002})
        s.hypotheses.push_back({bin*spacing,ppm});
    s.admitted_clock_half_width_ppm=.00025;s.admitted_clock_cell_half_width_ppm=.00005;
    modem::PatternCorrelator raw(c,s,8*1024*1024,{true,false});
    const auto budget=raw.reserved_workspace_bytes()+24*1024;
    modem::PatternCorrelator receiver(c,s,budget);
    check(modem::symbol_sample_count(c)==128ULL*c.sample_rate,"128-second fixture geometry changed");
    check(receiver.work().backend==modem::PatternCorrelationBackend::pulse_segments,
          "128-second ordinary compact fixture lost segmented backend");
    check(receiver.compact_acquisition_state().supported && receiver.set_compact_acquisition_prior(0,mask(s)),
          "128-second compact symbols still exclude qualified helper narrowing");
    std::vector<float> silence(2048);push(receiver,silence);
    const auto state=receiver.compact_acquisition_state();
    check(state.selected_pairs==5 && state.active_arrival_lanes==5 && state.projected_carriers==1,
          "long compact helper removed clock ambiguity or retained cold RF work");
    check(state.skipped_carrier_samples==silence.size()*8 && state.accumulated_lane_samples<=silence.size()*5,
          "long compact helper actual projection/fit work was not reduced");
    check(receiver.working_bytes()<=budget,"long compact helper displaced ordinary workspace");
}
void shared_pulse_admission() {
    auto c=config(128);c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=1;c.dsss=false;
    auto make_search=[&](int pairs) {
        auto s=search(c,7);s.start_uncertainty_seconds=7;s.candidate_limit=128;s.track_limit=8;
        s.admitted_clock_half_width_ppm=.0003;s.admitted_clock_cell_half_width_ppm=.0003;
        s.hypotheses.clear();const auto spacing=.25*c.sample_rate/modem::symbol_sample_count(c);
        for(int f=-(pairs/2);f<=pairs/2;++f)s.hypotheses.push_back({f*spacing,(f+pairs/2)%2?.0003:-.0003});
        s.receiver_strategy_workspace_bytes=12*1024*1024;return s;
    };
    constexpr std::size_t grant=256*1024*1024;
    auto coarse=make_search(1513);
    {modem::PatternCorrelator ordinary(c,coarse,grant);
        check(ordinary.work().backend==modem::PatternCorrelationBackend::pulse_segments && ordinary.work().hypotheses==23451,
            "coarse control did not reproduce the long compact segmented geometry");}
    coarse.helper_wait_for_shared_pulse=true;
    std::size_t required=0;
    try {modem::PatternCorrelator unexpected(c,coarse,grant);throw Error("helper admitted the unaffordable coarse pulse bank");}
    catch(const modem::PatternCompactAdmissionPending& pending) {
        required=pending.required_bytes;
        check(pending.pairs==1513 && pending.hypotheses==23451 && pending.available_bytes==coarse.receiver_strategy_workspace_bytes && required>pending.available_bytes,
            "helper waiting outcome did not report the actual constructor geometry and byte shortfall");
    }
    auto narrow=make_search(57);narrow.helper_wait_for_shared_pulse=true;
    auto below=narrow;below.receiver_strategy_workspace_bytes=1;
    try {modem::PatternCorrelator unexpected(c,below,grant);throw Error("helper pulse gate ignored its cohort share");}
    catch(const modem::PatternCompactAdmissionPending& pending) {
        required=pending.required_bytes;
        check(pending.pairs==57 && pending.hypotheses==883 && required>1,
            "narrow pulse budget query changed the retained catalogue");
    }
    below.receiver_strategy_workspace_bytes=required-1;
    try {modem::PatternCorrelator unexpected(c,below,grant);throw Error("helper pulse gate rounded an insufficient byte share up");}
    catch(const modem::PatternCompactAdmissionPending& pending) {
        check(pending.required_bytes==required && pending.available_bytes==required-1,"pulse budget boundary changed its accounting");
    }
    narrow.receiver_strategy_workspace_bytes=required;
    {modem::PatternCorrelator exact(c,narrow,grant);
        check(exact.work().lattices>0 && exact.work().backend!=modem::PatternCorrelationBackend::pulse_segments && exact.work().hypotheses==883,
            "exact funded shared-pulse boundary selected a different catalogue or backend");}
    narrow.receiver_strategy_workspace_bytes=12*1024*1024;
    {modem::PatternCorrelator receiver(c,narrow,grant);
        check(receiver.work().lattices>0 && receiver.work().hypotheses==883 && receiver.working_bytes()<=grant,
            "measured narrow catalogue did not admit the ordinary shared-pulse backend within its share");}
    // The wrapper must propagate the typed outcome instead of silently
    // reporting a missing payload allocation. Isolate its original compact
    // factory: Auto may legitimately select a different backend for this
    // sparse diagnostic catalogue. Production Auto remains unchanged.
    coarse.receiver_strategy_workspace_bytes=12*1024*1024;
    auto wrapper_config=c;wrapper_config.receiver_strategy=modem::ReceiverStrategy::existing_only;
    try {modem::StreamingReceiver unexpected(wrapper_config,grant,coarse);throw Error("ordinary wrapper bypassed helper admission waiting");}
    catch(const modem::PatternCompactAdmissionPending& pending) {
        check(pending.pairs==1513 && pending.available_bytes==coarse.receiver_strategy_workspace_bytes,
            "ordinary wrapper lost the constructor waiting report");
    }
    std::cout<<"helper compact admission full pairs=1513/57 lanes=23451/883 pulse byte boundary="<<required<<" passed\n";
}
}
int main(int argc,char** argv) {
    try {
        const std::string mode=argc>1?argv[1]:"all";
        if(mode=="all" || mode=="payload"){matched_payload(false);matched_payload(true);}
        if(mode=="all" || mode=="audit")audit_and_reclaim();
        if(mode=="all" || mode=="long")long_geometry();
        if(mode=="all" || mode=="timing")periodic_aliases();
        if(mode=="all" || mode=="trials")logical_trial_parity();
        if(mode=="all" || mode=="pressure")matched_payload(false,false,true);
        if(mode=="long-payload")matched_payload(true,true);
        if(mode=="all" || mode=="clock")clock_domain_and_cold();
        if(mode=="calendar"){native_calendar(false);native_calendar(true);}
        if(mode=="dispatch") {
            shared_pulse_admission();
            dispatch_expiry_negative();native_calendar(false,true);
            native_calendar(false,true,true,false);native_calendar(false,true,true,true);
            matched_payload(true,false,false,true);matched_payload(true,true,false,true);
        }
        if(mode=="phase-raw"){phase_coordinate_boundary();phase_payload(false,true);phase_payload(false,false);}
        if(mode=="phase-long"){phase_payload(true,true);phase_payload(true,false);}
        if(mode!="all" && mode!="payload" && mode!="audit" && mode!="long" && mode!="timing" && mode!="long-payload" && mode!="trials" && mode!="pressure" && mode!="clock" && mode!="calendar" && mode!="dispatch" && mode!="phase-raw" && mode!="phase-long")throw Error("unknown compact helper test mode");
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

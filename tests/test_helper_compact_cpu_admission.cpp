#include "helper_compact_cpu_admission.hpp"
#include "helper_reference_search.hpp"
#include "datapump/transfer.hpp"
#include <iostream>
#include <limits>

using namespace datapump;
using namespace datapump::modem;
namespace {
void check(bool v,const char* message){if(!v)throw Error(message);}
Config geometry() {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=1;c.spreading_factor=64;
    c.integration_seconds=128;c.pattern_symbols=true;c.constellation_bits=1;c.scramble=true;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.data_key=Crypto(c.spreading_seed);
    OscillatorSearchConfig p;p.lf.accuracy_ppm=p.rf.accuracy_ppm=.0001;
    p.margin=3;p.rf_shift_hz=5.8e9;p.reference=OscillatorReference::independent_audio;
    c.oscillator_search=p;c.receiver_cpu_budget=.6;
    // Match the actual EME Fake-FCC waveform/Auto policy. Without this hop
    // plan the diagnostic legitimately permits frequency-axis/amplitude Auto
    // operators which are unavailable to the production continuous FHSS case.
    FhssPlan hopping;hopping.channels=75;hopping.dwell_seconds=.4;
    for(unsigned i=0;i<hopping.channels;++i)hopping.shifts[i]=5.8e9+25000*i;
    transfer::Options options;options.modem=c;options.key=c.data_key;options.audio_timing_error_seconds=0;
    c.fake_hopping=transfer::transmit_hopping(hopping,options);
    check(c.fake_hopping->smooth_ramps&&!deferred_symbols(c)&&!c.fake_hopping->matched_gaps,
        "actual long EME fixture lost its continuous Fake-FCC power policy");
    return c;
}
PatternSearch search(const Config& c,double uncertainty) {
    PatternSearch s;
    apply_helper_reference_search(helper_reference_search(c,uncertainty,2450.3125,0,
        utc_transmit_rate_limit(c),HelperReferenceClockRelation::common_corrected_source,{},
        PatternUtcClockGrid::complete_expanded_domain),s);
    s.compact_clock_search=s.helper_wait_for_shared_pulse=true;
    s.start_offset_seconds=7;s.start_uncertainty_seconds=7;
    s.receiver_bank_count=receiver_strategy_bank_count(c,6,1,1);s.worker_threads=1;
    return s;
}
void gates() {
    auto c=geometry();auto s=search(c,.07515);
    ReceiverStrategyPlan p;p.cpu_budget=c.receiver_cpu_budget;p.selected_processing_ratio=1;
    const auto wait=[&](bool enabled=true,bool fresh=true) {
        return detail::helper_compact_cpu_admission_wait(enabled,fresh,c,s,p);
    };
    check(wait(),"new manually enabled compact candidate was not deferred");
    check(!wait(false)&&!wait(true,false),"helper OFF or an existing owner was CPU gated");
    p.selected_processing_ratio=p.cpu_budget;check(!wait(),"budget equality deferred a candidate");
    p.selected_processing_ratio=.1;check(!wait(),"affordable measured catalogue did not reenter");
    for(auto value:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        p.selected_processing_ratio=value;check(!wait(),"unknown processing estimate gained wait authority");
    }
    p.selected_processing_ratio=1;p.supported=false;check(!wait(),"unsupported backend disguised as CPU wait");
    p.supported=true;p.backend=ReceiverBackend::amplitude_energy;check(!wait(),"amplitude backend CPU gated");
    p.backend=ReceiverBackend::existing_complex;
    for(auto policy:{ReceiverStrategy::existing_only,ReceiverStrategy::complex_only,ReceiverStrategy::amplitude_only}) {
        c.receiver_strategy=policy;check(!wait(),"forced backend policy changed");
    }
    c.receiver_strategy=ReceiverStrategy::automatic;s.helper_wait_for_shared_pulse=false;
    check(!wait(),"ordinary compact owner gained helper admission policy");
}
void model() {
    auto c=geometry();auto wide=search(c,.07515),narrow=search(c,.001);
    constexpr std::size_t bytes=64U*1024*1024;
    const auto w=resolve_receiver_strategy(c,wide,bytes),n=resolve_receiver_strategy(c,narrow,bytes);
    std::cout<<"wide backend "<<receiver_backend_name(w.backend)<<", supported "<<w.supported
        <<", estimate "<<w.selected_processing_ratio<<", reason "<<w.reason
        <<"\nnarrow backend "<<receiver_backend_name(n.backend)<<", supported "<<n.supported
        <<", estimate "<<n.selected_processing_ratio<<", reason "<<n.reason<<"\n";
    check(w.supported&&n.supported&&w.backend==ReceiverBackend::existing_complex&&
        n.backend==ReceiverBackend::existing_complex,"actual long compact model selected another operator");
    check(wide.hypotheses.size()>narrow.hypotheses.size()&&
        w.selected_processing_ratio>n.selected_processing_ratio,"residual catalogue did not reduce modeled work");
    check(detail::helper_compact_cpu_admission_wait(true,true,c,wide,w)&&
        !detail::helper_compact_cpu_admission_wait(true,true,c,narrow,n),
        "actual measured-catalogue work did not defer then reenter at the configured CPU objective");
    wide.worker_threads=8;const auto many=resolve_receiver_strategy(c,wide,bytes);
    check(many.backend==w.backend&&many.selected_processing_ratio==w.selected_processing_ratio,
        "serial pulse model credited unavailable search-worker speedup");
    const auto planned=plan_receiver_strategy(c,wide,bytes,512U*1024*1024,128,4096);
    check(planned.selected_processing_ratio==many.selected_processing_ratio,
        "Planner resolver disagrees with runtime serial compact policy");
    wide.helper_wait_for_shared_pulse=false;const auto ordinary_many=resolve_receiver_strategy(c,wide,bytes);
    wide.worker_threads=1;const auto ordinary_one=resolve_receiver_strategy(c,wide,bytes);
    check(ordinary_many.selected_processing_ratio<ordinary_one.selected_processing_ratio,
        "ordinary legacy worker pricing changed");
    std::cout<<"pairs "<<wide.hypotheses.size()<<" -> "<<narrow.hypotheses.size()
        <<", provisional serial CPU "<<w.selected_processing_ratio<<" -> "<<n.selected_processing_ratio
        <<", objective "<<c.receiver_cpu_budget<<"\n";
}
}
int main(){try{gates();model();std::cout<<"helper compact CPU admission gates passed\n";}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

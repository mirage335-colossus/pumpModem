// Regression for conditioned-input provenance, optional reclamation and immutable constructor coverage.
#include "live_helper_frontend.hpp"
#include "live_helper_score_frequency.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
using namespace datapump;
using namespace datapump::live::detail;
void require(bool p,const char* m){if(!p)throw std::runtime_error(m);}
HelperScoreFrequencyCertificate certificate(std::uint64_t a,std::uint64_t b,HelperFrequencyDomainStamp s,bool good=true,double u=.006){return {a,b,-u,u,s,good};}
void startup_and_capture_ahead() {
    for(const std::uint64_t start:{6580ULL,4000ULL*128+6580}) {
        HelperScorePublication p;require(p.history.configure(4000),"minimum configure failed");p.observe_authority({17,2},0);
        require(p.history.append(certificate(0,start,{17,2},false)),"unsupported prefix did not drain");
        require(!p.query(0,start).valid && p.history.next_sample()==0,"startup acquired authority");
        require(p.history.append(certificate(start,start+1104,{17,2})),"first qualified span failed");
        require(p.query(start,start+1104).valid && !p.query(start-1,start+1104).valid,"startup prefix poisoned or bridged first span");
        const auto first=start;auto at=start+1104;
        for(unsigned i=0;i<200;++i){require(p.history.append(certificate(at,at+4000,{17,2})),"bounded coalescing append failed");at+=4000;}
        const auto h=p.query(first,at);require(h.valid && h.lower_hz<=-.006 && h.upper_hz>=.006,"bounded full-run hull lost valid support");
    }
}
void reclamation_and_model_veto() {
    HelperScorePublication p;require(p.history.configure(4000),"publication configure failed");p.observe_authority({17,2},0);
    auto view=std::make_shared<HelperInputView>();view->budget_serial=17;view->logical_rate=4000;view->physical_rate=48000;p.view=view;
    constexpr std::uint64_t first=6580,end=7684;
    require(p.history.append(certificate(first,end,p.stamp())),"base append failed");
    p.refinement=std::make_unique<HelperScoreFrequencyProducer>();p.refinement->view=view;p.refinement->view_generation=2;
    require(p.refinement->history.configure(4000,48000),"refinement configure failed");
    require(p.refinement->history.append(certificate(first,end,p.stamp(),true,.001)),"fine append failed");
    require(p.query(first,end).valid && p.query(first,end).upper_hz==.001,"same-authority fine bound unavailable");
    HelperScoreFrequencyBasis basis{p.stamp(),4000,2000,1000,0,{},false};
    std::array<HelperScoreFrequencyLease,13> leases;
    for(auto& l:leases){l.valid=true;l.basis=basis;l.lower_hz=-.01;l.upper_hz=.01;l.radius_hz=.01;}
    p.release_refinement();
    const auto fallback=p.query(first,end);unsigned valid=0;
    for(const auto& l:leases)valid+=l.contains(fallback,basis);
    require(!p.refinement && valid==13 && fallback.lower_hz==-.006 && fallback.upper_hz==.006,"optional reclamation revoked minimum or used newest-only precision");
    // Explicit model rejection survives reclamation. Neither queued old tags nor
    // a new-generation current span can certify a cold owner's older aperture.
    p.observe_authority({17,3},100000);
    require(!p.query(first,end).valid,"veto preserved rejected old authority");
    require(!p.history.append(certificate(end,end+100,p.stamp()==HelperFrequencyDomainStamp{17,3}?HelperFrequencyDomainStamp{17,2}:p.stamp())),"queued old-generation block inherited authority");
    require(p.history.append(certificate(20000,21000,p.stamp())),"replacement support failed");
    require(p.query(20000,21000).valid && !p.query(first,21000).valid,"new model manufactured old support");
    // Phase-only absence changes no frequency stamp; no phase field is authority.
    const auto before=p.query(20000,21000);p.observe_authority(p.stamp(),100000);
    require(p.query(20000,21000).valid && p.query(20000,21000).lower_hz==before.lower_hz,"unchanged frequency authority was erased");
    p.observe_authority({18,1},200000);
    require(!p.query(20000,21000).valid,"provider replacement reused old support");
    p.observe_authority({},200000);require(!p.history.append(certificate(30000,31000,{})),"exhausted authority accepted a block");
    std::cout<<"reclamation conditional_receipts=13 minimum_valid="<<valid<<" optional_bytes_released="<<sizeof(HelperScoreFrequencyProducer)<<" no_payload_or_END_claim=1\n";
}
void real_gaps_and_fir_boundaries() {
    HelperScorePublication p;require(p.history.configure(4000),"gap configure failed");p.observe_authority({1,1},0);
    require(p.history.append(certificate(0,100,{1,1})),"gap first failed");
    require(p.history.append(certificate(100,200,{1,1},false)),"unsupported span was dropped");
    require(p.history.append(certificate(200,300,{1,1})),"gap recovery failed");
    require(!p.query(0,300).valid && !p.query(100,200).valid && p.query(200,300).valid,"real gap was bridged or current support lost");
    require(p.history.append(certificate(400,500,{1,1})),"unobserved date jump failed");
    require(!p.query(200,500).valid,"unobserved gap was relabelled");
    require(helper_score_hilbert_support(1024,400,512,0,1424),"whole FIR positive boundary failed");
    require(!helper_score_hilbert_support(1023,400,512,0,1424) && !helper_score_hilbert_support(1024,401,512,0,1424),"whole FIR endpoints manufactured support");
}
int minimum_checks(){try{startup_and_capture_ahead();reclamation_and_model_veto();real_gaps_and_fir_boundaries();
    std::cout<<"minimum_history_bytes="<<sizeof(HelperScoreMandatoryHistory)<<" publication_bytes="<<sizeof(HelperScorePublication)<<" optional_producer_bytes="<<sizeof(HelperScoreFrequencyProducer)<<" view_block_bytes="<<sizeof(HelperViewBlock)<<" per_block_authority_tag_bytes="<<2*sizeof(std::uint64_t)<<" mandatory_queue_tag_ceiling_delta_bytes="<<2*8*2*sizeof(std::uint64_t)<<" precision_settings_changed=0 PCM_added=0\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

#include "live_helper_constructor_radius.hpp"
#include "live_helper_data_parameter.hpp"
#include "helper_reference_search.hpp"
#include "receiver_oscillator_policy.hpp"
#include <set>
#include <iomanip>
void official_constructor_headroom() {
    modem::Config c;c.sample_rate=4000;c.carrier_hz=1000;c.bandwidth_hz=200;
    c.spreading_factor=16;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{0,0},{.1,0},1e8,1};
    transfer::Options options;options.modem=c;options.clock_sync.reset();
    const auto utc=transfer::receiver_utc_rate_correction(c,options);
    constexpr double first=.005435805107747341,later=.0055,center=0,H=2000;
    const auto radius=helper_score_constructor_radius(c,first);
    require(modem::symbol_sample_count(c)==640 && radius>=first+.78125 &&
        radius<=std::nextafter(first+.78125,INFINITY),"official additive native cell geometry changed");
    const auto build=[&](double u) {modem::PatternSearch search;
        auto r=modem::helper_reference_search(c,u,H,center,utc,modem::HelperReferenceClockRelation::common_corrected_source,
            false,modem::PatternUtcClockGrid::complete_expanded_domain);
        require(!r.original_prior_retained,"control unexpectedly used original broad prior");
        modem::apply_helper_reference_search(std::move(r),search);return search;};
    auto old_search=build(first),new_search=build(radius);
    const HelperScoreFrequencyBasis basis{{17,2},4000,H,c.carrier_hz,utc,c.oscillator_search,false};
    const auto old=HelperScoreFrequencyLease::freeze(basis,center,first,old_search,false);
    const auto widened=HelperScoreFrequencyLease::freeze(basis,center,radius,new_search,false);
    const HelperScoreFrequencyHull fresh{true,6580,9000,-later,later,basis.stamp};
    require(old.valid && !old.contains(fresh,basis),"old exact receipt did not reproduce widening lockout");
    require(widened.valid && widened.contains(fresh,basis),"actual widened builder receipt did not contain later hull");
    require(new_search.admitted_frequency_half_width_hz && *new_search.admitted_frequency_half_width_hz>=radius,
        "actual builder templates did not receive constructor radius");
    modem::detail::PatternHelperFrequencySelector selector;
    const modem::PatternHelperFrequencyWindow win{0,later,H-c.carrier_hz,
        *new_search.admitted_clock_cell_half_width_ppm,{}};
    require(selector.initialize(new_search,win) && selector.selected>0,"official selected cells unavailable");
    std::set<double> old_lf,new_lf,selected_lf;
    for(const auto& h:old_search.hypotheses)old_lf.insert(h.clock_error_ppm);
    for(std::size_t i=0;i<new_search.hypotheses.size();++i){new_lf.insert(new_search.hypotheses[i].clock_error_ppm);
        if(selector.keep(i))selected_lf.insert(new_search.hypotheses[i].clock_error_ppm);}
    require(old_lf==new_lf && new_lf==selected_lf,"headroom removed original LF/UTC alternatives");
    auto wrong=basis;wrong.helper_hz=-H;require(!widened.contains(fresh,wrong),"wrong gauge received authority");
    wrong=basis;wrong.stamp.model_generation++;require(!widened.contains(fresh,wrong),"old hull survived model veto");
    require(helper_score_constructor_radius(c,0)==0 && helper_score_constructor_radius(c,-1)==-1,
        "zero or invalid measured radius was normalized");
    bool rejected=false;try{(void)build(-1);}catch(...){rejected=true;}require(rejected,"builder accepted invalid radius");
    auto unsupported=c;unsupported.symbol_burst.wait_symbols=1;
    require(helper_score_constructor_radius(unsupported,first)==first,"scheduled source gained unproven constructor scope");
    auto split=c;auto hopping=std::make_shared<modem::FhssPlan>();hopping->defer_symbols=true;split.fake_hopping=hopping;
    require(helper_score_constructor_radius(split,first)==first,"split source gained unproven constructor scope");
    std::cout<<std::setprecision(17)<<"official_builder measured="<<first<<" later="<<later<<" constructor_radius="<<radius
        <<" old_contains=0 new_contains=1 old_pairs="<<old_search.hypotheses.size()<<" new_pairs="<<new_search.hypotheses.size()
        <<" selected="<<selector.selected<<" LF="<<new_lf.size()<<" selected_LF="<<selected_lf.size()<<" utc="<<utc<<'\n';
}
void data_constructor_high_uncertainty() {
    // Retained actual-RF startup counterexample: a small later widening must
    // remain reachable even when initial uncertainty exceeds one RF cell.
    modem::Config c;c.sample_rate=8000;c.carrier_hz=1500;c.bandwidth_hz=200;
    c.spreading_factor=128;c.pulse_shaping=false;c.receiver_strategy=modem::ReceiverStrategy::automatic;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{2,0},7e6,3};
    const DataParameterBound birth{-1.0923349386666628,1.1357071192015802,{2,1},DataParameterBound::Kind::bounded,false};
    DataParameterHull fresh;fresh.lower=-1.092470766395;fresh.upper=1.135843023673;
    fresh.stamp=birth.stamp;fresh.kind=DataParameterBound::Kind::bounded;
    require(fresh.lower<birth.lower && fresh.upper>birth.upper,"retained widening counterexample changed");
    modem::PatternSearch search;
    const auto receipt=data_parameter_search(c,0,birth,search,1368,1632);
    require(receipt && receipt->contains(fresh,c,0,birth.stamp),"high-uncertainty startup hull locked out");
    const auto center=std::midpoint(fresh.lower,fresh.upper);
    const auto radius=DataParameterBound::up(std::max(static_cast<long double>(center)-fresh.lower,
        static_cast<long double>(fresh.upper)-center));
    modem::detail::PatternHelperFrequencySelector selector;
    require(search.admitted_clock_cell_half_width_ppm && selector.initialize(search,
        {center,radius,0,*search.admitted_clock_cell_half_width_ppm,{}}),"DATA startup selector unavailable");
    modem::PatternSearchResolution resolution;resolution.clock_frequency_hz=0;
    const auto original=modem::oscillator_pattern_search(c,0,resolution,modem::PatternUtcClockGrid::complete_expanded_domain);
    std::set<double> original_lf,constructor_lf,selected_lf;
    for(const auto& h:original.hypotheses)original_lf.insert(h.clock_error_ppm);
    for(std::size_t i=0;i<search.hypotheses.size();++i){constructor_lf.insert(search.hypotheses[i].clock_error_ppm);
        if(selector.keep(i))selected_lf.insert(search.hypotheses[i].clock_error_ppm);}
    require(original_lf.size()>1 && original_lf==constructor_lf && original_lf==selected_lf,
        "DATA headroom pruned unresolved original LF alternatives");
    auto beyond=fresh;beyond.lower=std::nextafter(receipt->lower,-INFINITY);
    beyond.upper=std::nextafter(receipt->upper,INFINITY);
    require(!receipt->contains(beyond,c,0,birth.stamp),"DATA receipt accepted an outside hull");
    auto wrong=birth.stamp;++wrong.epoch;
    require(!receipt->contains(fresh,c,0,wrong),"DATA startup hull crossed model authority");
    std::cout<<"data_constructor high_uncertainty_startup_reachable=1 pairs="<<search.hypotheses.size()
        <<" selected="<<selector.selected<<" original_LF="<<original_lf.size()<<" outside_declined=1\n";
}
void queued_model_veto() {
    using Frontend=HelperInputFrontend;constexpr unsigned rate=8000,step=160;constexpr double pilot=2400;
    modem::Config c;c.sample_rate=rate;c.carrier_hz=1200;c.bandwidth_hz=36;c.spreading_factor=16;c.pulse_shaping=false;
    helper::Reference ref;ref.sample_rate=rate;ref.frequency_valid=true;ref.frequency_hz=pilot+.25;ref.frequency_uncertainty_hz=.01;
    const auto grant=Frontend::reservation_bound(rate,rate,pilot);
    Frontend control(grant,79),vetoed(grant,79);
    control.admit_realtime(c,pilot,ref,rate,rate/2.-10,0);vetoed.admit_realtime(c,pilot,ref,rate,rate/2.-10,0);
    std::array<float,step> input{};
    for(std::uint64_t at=0;at<5120;at+=step){
        require(control.track_frequency(ref,pilot)&&vetoed.track_frequency(ref,pilot),"queue frequency control failed");
        if(at==2720){helper::AnalyticFrequencyTranslator::PilotObservation phase{pilot,pilot+.25,.2,3000,.5,1600,4800,4800,pilot+.25};
            require(control.track_pilot(phase)&&vetoed.track_pilot(phase),"queue phase control failed");}
        for(std::size_t i=0;i<step;++i)input[i]=static_cast<float>(.02*std::cos(2*std::numbers::pi*(c.carrier_hz+.25)*(at+i)/rate));
        control.push(input,at,.02);vetoed.push(input,at,.02);
    }
    // A retrospective context veto happens after correction production but
    // before take. Later dependencies exceed the veto cutoff; cutoff alone is insufficient.
    vetoed.invalidate_frequency_certificates();
    auto a=control.take(UINT64_MAX,ref,5120),b=vetoed.take(UINT64_MAX,ref,5120);
    std::vector<float> ap,bp;std::size_t valid_before=0,valid_after=0;bool phase_before=false,phase_after=false;
    for(const auto& x:a){ap.insert(ap.end(),x.samples.begin(),x.samples.end());valid_before+=bool(x.corrected_rf_uncertainty_hz);phase_before|=x.common_phase_corrected;}
    for(const auto& x:b){bp.insert(bp.end(),x.samples.begin(),x.samples.end());valid_after+=bool(x.corrected_rf_uncertainty_hz);phase_after|=x.common_phase_corrected;
        require(!x.corrected_rf_uncertainty_hz,"queued old-model output relabeled after veto");
        require(x.applied_correction_hz.lower_hz==-.25 && x.applied_correction_hz.upper_hz==-.25,"veto changed applied q");}
    require(valid_before && !valid_after && !ap.empty() && ap==bp && a.front().first_sample==b.front().first_sample,
        "queue veto dropped/redated PCM or did not exercise a valid queued certificate");
    require(phase_before && phase_after,"frequency veto erased independent phase diagnostics");
    bool recovered=false;std::size_t unsupported=0;
    for(std::uint64_t at=5120;at<8000;at+=step){vetoed.push(input,at,.02);
        for(const auto& x:vetoed.take(UINT64_MAX,ref,at+step)){if(x.corrected_rf_uncertainty_hz)recovered=true;else ++unsupported;}}
    require(recovered && unsupported && vetoed.bytes()<=grant,"fresh whole-dependency recovery failed or revived old raw dependency");
    std::cout<<"queue_veto old_valid_blocks="<<valid_before<<" new_valid_blocks="<<valid_after<<" PCM_equal=1 phase_retained=1 fresh_recovered=1 unsupported_transition_blocks="<<unsupported<<" extra_storage_bytes=0\n";
}
void optional_rejection_survives_reclamation() {
    HelperScorePublication p;require(p.history.configure(4000),"rejection base configure failed");p.observe_authority({17,2},0);
    require(p.history.append(certificate(4000,5000,p.stamp())),"rejection base append failed");
    require(p.history.append(certificate(8000,9000,p.stamp())),"later rejection base append failed");
    HelperScoreRefinedHistory rejected;require(rejected.configure(4000,8000),"rejection refined configure failed");
    // This uses the actual unsupported representation produced by a disjoint
    // posterior veto. Missing optional coverage alone is tested by R1 above.
    require(rejected.append(certificate(4000,5000,p.stamp(),false)),"veto interval append failed");
    p.observe_rejections(rejected);p.release_refinement();
    require(!p.query(4000,5000).valid && p.query(8000,9000).valid,"reclamation erased a dated veto or invalidated unrelated support");
    std::cout<<"optional_rejection persisted_after_reclamation=1 unrelated_later_support=1 extra_storage_bytes=0\n";
}
void optional_refinement_preserves_constructor_support() {
    // Actual HF15k owner9: the produced corrected-M bound fits its immutable
    // constructor, but optional backward estimates became less informative.
    // This tests publication/lease selection, not PCM, RF truth or END scoring.
    constexpr double base_u=.08889253695824155,constructor_u=.09768159945824155;
    constexpr std::uint64_t first=1407,end=5338;
    HelperScorePublication p;require(p.history.configure(6120),"HF15k base configure failed");
    p.observe_authority({17,2},0);
    auto view=std::make_shared<HelperInputView>();view->budget_serial=17;
    view->logical_rate=6120;view->physical_rate=48960;p.view=view;
    require(p.history.append(certificate(first,end,p.stamp(),true,base_u)),"HF15k base append failed");
    modem::Config c;c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{100,0},3e7,3};
    c.oscillator_search->reference=modem::OscillatorReference::shared_radio;
    const HelperScoreFrequencyBasis basis{p.stamp(),6120,2662.5,1500,0,c.oscillator_search,false};
    modem::PatternSearch search;search.admitted_clock_half_width_ppm=300;
    search.admitted_clock_cell_half_width_ppm=37.5;
    const auto lease=HelperScoreFrequencyLease::freeze(basis,0,constructor_u,search);
    const auto original=p.query(first,end);
    require(original.valid && lease.valid && lease.contains(original,basis),"HF15k original constructor unsupported");
    const auto install=[&](double lo,double hi,bool supported=true) {
        p.refinement=std::make_unique<HelperScoreFrequencyProducer>();
        p.refinement->view=view;p.refinement->view_generation=p.view_generation;
        require(p.refinement->history.configure(6120,48960),"HF15k fine configure failed");
        require(p.refinement->history.append({first,end,lo,hi,p.stamp(),supported}),"HF15k fine append failed");
    };
    // The third estimate is narrower in width but extends beyond the original
    // upper endpoint: width alone must not confer subset authority.
    for(const auto bounds:std::array<std::array<double,2>,3>{{
            {-.12339753668532841,.11205039075451542},
            {-.88762836977642,.8462020911938715},{.08,.10}}}) {
        install(bounds[0],bounds[1]);
        const auto optional=p.refinement->history.query(first,end),chosen=p.query(first,end);
        require(optional.valid && !lease.contains(optional,basis),"HF15k widening control no longer exceeds constructor");
        require(chosen.valid && chosen.lower_hz==original.lower_hz && chosen.upper_hz==original.upper_hz &&
                chosen.support_first_sample==original.support_first_sample &&
                chosen.support_end_sample==original.support_end_sample && lease.contains(chosen,basis),
                "optional wider/recentered estimate revoked executed constructor support");
    }
    install(-.02,.03);const auto narrow=p.query(first,end);
    require(narrow.valid && narrow.lower_hz==-.02 && narrow.upper_hz==.03 && lease.contains(narrow,basis),
            "supported actual subset did not improve existing constructor");
    p.release_refinement();const auto reclaimed=p.query(first,end);
    require(reclaimed.valid && reclaimed.lower_hz==original.lower_hz && reclaimed.upper_hz==original.upper_hz &&
            lease.contains(reclaimed,basis),"optional reclaim lost mandatory constructor support");
    // A disjoint posterior is explicitly unsupported in the producer protocol.
    // Live persists that veto before optional allocation/copy/reclamation; do
    // the same here. It must never fall back to the earlier executed bound.
    install(.2,.3,false);p.observe_rejections(p.refinement->history);
    require(!p.query(first,end).valid && !lease.contains(p.query(first,end),basis),
            "explicit disjoint-posterior rejection fell back to old base");
    p.release_refinement();require(!p.query(first,end).valid,"reclamation erased disjoint-posterior veto");
    p.observe_authority({17,3},end);
    require(p.history.append(certificate(7000,8000,p.stamp(),true,.01)),"new authority support failed");
    require(!p.query(first,end).valid && p.query(7000,8000).valid,"new authority revived rejected old dates");
    HelperScoreRefinedHistory reset;require(reset.configure(6120,48960),"reset fine configure failed");
    require(reset.append(certificate(7000,8000,p.stamp(),true,.001)),"reset fine append failed");
    reset.invalidate_model();p.observe_rejections(reset);p.release_refinement();
    require(!p.query(7000,8000).valid,"explicit model reset disappeared on optional reclaim");
    std::cout<<"HF15k optional_wider_or_shifted=3 base_preserved=1 strict_subset=1 immutable_lease_retained=1"
        <<" disjoint_reset_reclaim_vetoes=1 extra_storage_bytes=0 no_PCM_or_END_claim=1\n";
}
// Numeric publication-protocol input only. The production publish body consumes
// the same dated frequency_interval contract from ToneTracker. These supplied
// intervals exercise no tone acquisition, PCM, physical confidence or END.
struct ScoreProtocolFrequencySource {
    double helper_hz=2662.5,lower=0,upper=0;
    std::uint64_t captured=0;
    helper::ToneFrequencyInterval frequency_interval(std::uint64_t first,std::uint64_t end)const noexcept {
        helper::ToneFrequencyInterval q;
        if(first>=end || end>captured)return q;
        q.valid=true;q.frequency_hz=helper_hz+std::midpoint(lower,upper);
        q.uncertainty_hz=std::max(q.frequency_hz-helper_hz-lower,upper-(q.frequency_hz-helper_hz));
        q.first_sample=first;q.end_sample=end;q.anchor_sample=first;
        q.evidence_first_sample=first;q.evidence_end_sample=end;q.captured_end_sample=captured;return q;
    }
};
void publish_score_protocol(HelperScoreRefinedHistory& history,helper::ToneTrackerSnapshot& model,
        double lower,double upper,std::uint64_t captured=20000) {
    model.sample=captured-1;model.frequency_context_end_sample=captured;model.captured_end_sample=captured;
    model.sample_rate=48960;model.frequency_hz=2662.5+std::midpoint(lower,upper);
    model.frequency_variance_hz2=(upper-lower)*(upper-lower)/36;
    history.publish(ScoreProtocolFrequencySource{2662.5,lower,upper,captured},model,2662.5,true);
}
void immutable_produced_descriptor() {
    constexpr HelperFrequencyDomainStamp stamp{17,2};
    constexpr std::uint64_t first=1407,end=3290;
    const auto prepare=[&](HelperScoreRefinedHistory& h,double u) {
        require(h.configure(6120,48960),"descriptor configure failed");
        require(h.append(certificate(first,end,stamp,true,u),10000,11000,0,0,true,11000),"descriptor append failed");
    };
    HelperScoreRefinedHistory h;prepare(h,.09);helper::ToneTrackerSnapshot model;
    for(const auto bounds:std::array<std::array<double,2>,2>{{{-.12,.11},{.08,.10}}}) {
        publish_score_protocol(h,model,bounds[0],bounds[1]);
        const auto q=h.query(first,end);
        require(q.valid && q.lower_hz==-.09 && q.upper_hz==.09,
            "overlapping wider or shifted posterior replaced immutable initial descriptor");
    }
    publish_score_protocol(h,model,.081,.085);const auto narrow=h.query(first,end);
    require(narrow.valid && narrow.lower_hz>.08 && narrow.upper_hz<.09 &&
        narrow.lower_hz<=.081 && narrow.upper_hz>=.085,"initial subset was not selected");
    // A later non-subset returns to INITIAL, not the narrowest earlier poll.
    publish_score_protocol(h,model,-.12,.11);
    require(h.query(first,end).lower_hz==-.09 && h.query(first,end).upper_hz==.09,
        "descriptor selected a minimum across earlier polls");
    require(h.append(certificate(end,end+100,stamp,true,.01),0,0,0,0,false,300000),"freeze trigger append failed");
    require(h.recent_size()==0 && h.query(first,end).lower_hz==-.09 && h.query(first,end).upper_hz==.09,
        "freezing retained the wider posterior instead of initial descriptor");

    // Every adjacent posterior overlaps its predecessor. The second is disjoint
    // from INITIAL and must revoke authority permanently, including after freeze.
    HelperScoreRefinedHistory walk;prepare(walk,.09);helper::ToneTrackerSnapshot walk_model;
    publish_score_protocol(walk,walk_model,.08,.10);
    publish_score_protocol(walk,walk_model,.095,.115);
    require(walk.diagnostics.contradictions==1 && !walk.query(first,end).valid,
        "overlapping posterior walk escaped immutable initial contradiction check");
    HelperScorePublication p;require(p.history.configure(6120),"walk base configure failed");p.observe_authority(stamp,0);
    require(p.history.append(certificate(first,end,stamp,true,.09)),"walk base append failed");
    p.observe_rejections(walk);p.release_refinement();
    require(!p.query(first,end).valid,"initial-disjoint veto disappeared on reclaim");
    publish_score_protocol(walk,walk_model,-.01,.01);
    require(!walk.query(first,end).valid,"later overlap revived vetoed descriptor");
    require(walk.append(certificate(end,end+100,stamp,true,.01),0,0,0,0,false,300000),"walk freeze trigger failed");
    require(!walk.query(first,end).valid,"frozen initial-disjoint veto revived");
    // The original current-posterior contradiction check also remains: both
    // intervals are inside INITIAL, but are disjoint from one another.
    HelperScoreRefinedHistory previous;prepare(previous,.09);helper::ToneTrackerSnapshot previous_model;
    publish_score_protocol(previous,previous_model,-.08,-.06);
    publish_score_protocol(previous,previous_model,.06,.08);
    require(previous.diagnostics.contradictions==1 && !previous.query(first,end).valid,
        "initial containment erased previous-posterior contradiction");
    HelperScoreRefinedHistory reset;prepare(reset,.09);reset.invalidate_model();
    publish_score_protocol(reset,previous_model,-.01,.01);
    require(!reset.query(first,end).valid,"model reset reused initial descriptor");
    std::cout<<"immutable_descriptor wider_shifted_fallback=2 strict_subset=1 initial_not_minimum_poll=1"
        <<" frozen_initial=1 initial_walk_veto=1 previous_disjoint_veto=1 reset_reclaim_veto=1 no_acquisition_or_END_claim=1\n";
}
void late_constructor_with_coalesced_minimum() {
    // Retained HF15k owner39 dimensions. All produced later bounds fit its
    // constructor. The mandatory16 buckets coalesce startup into that origin,
    // while the optional ledger must retain each descriptor's initial bound.
    constexpr std::uint32_t rate=6120,raw_rate=48960;
    constexpr std::uint64_t first=1407,origin=153412,end=246816;
    constexpr double startup_u=.08889253695824155,produced_u=.02167143409155625;
    constexpr double constructor_radius=.029811495620653283;
    HelperScorePublication p;require(p.history.configure(rate),"late minimum configure failed");p.observe_authority({17,2},0);
    auto view=std::make_shared<HelperInputView>();view->budget_serial=17;view->logical_rate=rate;view->physical_rate=raw_rate;p.view=view;
    p.refinement=std::make_unique<HelperScoreFrequencyProducer>();p.refinement->view=view;p.refinement->view_generation=2;
    require(p.refinement->history.configure(rate,raw_rate),"late producer configure failed");
    helper::ToneTrackerSnapshot model;
    for(auto at=first;at<end;) {
        const auto stop=std::min<std::uint64_t>(end,at+rate-at%rate);
        const auto u=at==first?startup_u:produced_u;
        const auto c=certificate(at,stop,p.stamp(),true,u);
        require(p.history.append(c),"late mandatory append failed");
        require(p.refinement->history.append(c,at*8,stop*8,0,0,true,stop*8),"late producer append failed");
        publish_score_protocol(p.refinement->history,model,-.03175551789384252,.03386726750806385,stop*8);
        at=stop;
    }
    const auto base=p.history.query(origin,end),fine=p.refinement->history.query(origin,end),selected=p.query(origin,end);
    require(base.valid && base.support_first_sample==first && base.lower_hz==-startup_u && base.upper_hz==startup_u,
        "late control did not exercise16-bucket startup coalescing");
    require(fine.valid && fine.lower_hz==-produced_u && fine.upper_hz==produced_u &&
        fine.support_first_sample==153000 && p.refinement->history.recent_size()>0,
        "late optional recent/frozen query lost original produced descriptors");
    modem::Config c;c.sample_rate=rate;c.carrier_hz=1500;c.bandwidth_hz=36;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{100,0},3e7,3};
    c.oscillator_search->reference=modem::OscillatorReference::shared_radio;
    const HelperScoreFrequencyBasis basis{p.stamp(),rate,2662.5,1500,0,c.oscillator_search,false};
    modem::PatternSearch search;search.admitted_clock_half_width_ppm=300;search.admitted_clock_cell_half_width_ppm=37.5;
    const auto lease=HelperScoreFrequencyLease::freeze(basis,0,constructor_radius,search);
    require(lease.valid && !lease.contains(base,basis) && lease.contains(selected,basis),
        "late actual lease remained locked out by optional widening/coarse minimum");
    p.release_refinement();require(p.query(origin,end).valid && !lease.contains(p.query(origin,end),basis),
        "optional reclaim manufactured fine authority from a coalesced minimum");
    auto wrong=basis;++wrong.stamp.model_generation;
    require(!lease.contains(selected,wrong),"late original descriptor crossed authority stamp");
    std::cout<<"HF15k_late_constructor origin="<<origin<<" constructor_radius="<<constructor_radius
        <<" produced_bound="<<produced_u<<" coarse_minimum="<<startup_u
        <<" original_recent_frozen_selected=1 lease_supported=1 reclaim_declines_fine=1 extra_storage_bytes=0"
        <<" no_PCM_acquisition_or_END_claim=1\n";
}
int main(){try{if(minimum_checks())return 1;official_constructor_headroom();data_constructor_high_uncertainty();queued_model_veto();optional_rejection_survives_reclamation();optional_refinement_preserves_constructor_support();immutable_produced_descriptor();late_constructor_with_coalesced_minimum();return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

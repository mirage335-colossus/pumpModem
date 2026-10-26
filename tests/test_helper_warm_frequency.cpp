// Controlled component authority; normal Live publication/CPU qualification is
// separate. Payload and physical END always use the ordinary Robust engine.
#define main helper_joint_component_main
#include "test_helper_joint_clock.cpp"
#undef main
#include "live_helper_continuation.hpp"
#include "pattern_continuation_prior.hpp"
#include <sstream>

namespace {
using Snapshot=live::detail::LiveHelperClockSnapshot;
using WarmResult=modem::PatternContinuationFrequencyResult;
struct Pending {Bytes bits;bool complete=false,full_pending=false;std::uint64_t completion=0;std::vector<std::pair<std::uint64_t,Bytes>> progress;};
struct WarmRun {std::map<Identity,Pending> rows;WarmResult work;std::uint64_t trials=0;double cpu=0,wall=0;};
Snapshot warm_snapshot(const modem::Config& c,std::uint64_t captured,double offset=0) {
    Snapshot s;s.domain={1,1,0,1,c.sample_rate};s.captured_end_sample=captured;s.capture_known=s.frequency_known=true;
    s.transport_recipe={audio::CaptureTransportKind::fixed_nominal_audio,c.sample_rate,c.sample_rate,0,0};
    s.expected_helper_hz=5275;
    h::Reference r;r.sample=captured-1;r.sample_rate=c.sample_rate;r.frequency_valid=true;
    r.frequency_fit=h::FrequencyFit::local_polynomial;r.frequency_hz=s.expected_helper_hz+s.expected_helper_hz*.0001+offset;
    r.frequency_uncertainty_hz=.005;r.frequency_first_sample=captured-std::min<std::uint64_t>(captured,c.sample_rate/10);
    s.frequency=live::detail::HelperMeasurement(r);return s;
}
h::ContinuationFrequencyRequest warm_request(const modem::PatternContinuationScoredSupport& support,std::uint64_t captured) {
    h::ContinuationFrequencyRequest r;r.scored=support;r.input_captured_end_sample=captured;
    r.source_relation=h::JointClockSourceRelation::common_corrected_robust_source;
    r.input_basis=h::JointClockInputBasis::unshifted_fixed_filter_and_nominal_resampler;
    r.model=h::ContinuationFrequencyModel::conditional_stationary_mean_configured_wiener;return r;
}
void add(WarmResult& a,const WarmResult& b) {
    a.windows+=b.windows;a.narrowed_windows+=b.narrowed_windows;a.anchor_timing_fits+=b.anchor_timing_fits;
    a.original_competing_fits+=b.original_competing_fits;a.executed_competing_fits+=b.executed_competing_fits;
    a.skipped_competing_fits+=b.skipped_competing_fits;a.first_failure_broad_retries+=b.first_failure_broad_retries;
    a.periodic_broad_windows+=b.periodic_broad_windows;a.logical_trials+=b.logical_trials;
}
void poll_warm(modem::PatternReceiver& receiver,WarmRun& result,std::uint64_t frontier,const Bytes& expected) {
    for(const auto& burst:receiver.take_bursts()) {
        auto& row=result.rows[{burst.stream_first_sample,burst.stream_first_symbol}];
        check(!row.complete,"warm reference appended after physical END");
        if(burst.first_stream_symbol-burst.stream_first_symbol!=row.bits.size())continue;
        check(!burst.missing_slots,"warm reference introduced a timed gap");
        row.bits.insert(row.bits.end(),burst.bits.begin(),burst.bits.end());row.complete=burst.complete;
        if(row.bits.size()<=expected.size() && std::equal(row.bits.begin(),row.bits.end(),expected.begin()))
            row.full_pending|=!row.complete && row.bits==expected;
        if(burst.complete)row.completion=frontier;
    }
    for(auto& [identity,row]:result.rows)row.progress.push_back({frontier,row.bits});
}
WarmRun receive_warm(const modem::Config& c,const modem::PatternSearch& search,std::span<const float> pcm,
        const Bytes& expected,bool enabled,std::uint64_t loss_at,double offset=0) {
    WarmRun result;modem::PatternReceiver receiver(c,64*1024*1024,search);receiver.set_helper_gap_protection(true);
    const auto snapshot=warm_snapshot(c,pcm.size(),offset);
    // A common cold acquisition mask is deliberately identical for OFF/ON.
    // The changed variable is ONLY the new fresh warm RF call-scope selector.
    const auto input=live::detail::live_helper_clock_frequency_input(c,snapshot,1,
        h::JointClockSourceRelation::common_corrected_robust_source,*search.admitted_clock_half_width_ppm);
    check(input.valid,"warm component has no independent current frequency authority");
    const auto proof=live::detail::helper_prior_mask(search.hypotheses,input.offset_hz,input.uncertainty_hz,
        c.carrier_hz-snapshot.expected_helper_hz,.125*c.sample_rate/modem::symbol_sample_count(c),
        *search.admitted_clock_cell_half_width_ppm,*search.admitted_clock_half_width_ppm,false);
    check(proof.admitted,"warm component ordinary all-LF prior failed");
    // Always use the SAME truthful cold prior; misleading-reference tests
    // apply their offset only after a stream is already established.
    const auto original=warm_snapshot(c,pcm.size());
    const auto original_input=live::detail::live_helper_clock_frequency_input(c,original,1,
        h::JointClockSourceRelation::common_corrected_robust_source,*search.admitted_clock_half_width_ppm);
    const auto cold=live::detail::helper_prior_mask(search.hypotheses,original_input.offset_hz,original_input.uncertainty_hz,
        c.carrier_hz-original.expected_helper_hz,.125*c.sample_rate/modem::symbol_sample_count(c),
        *search.admitted_clock_cell_half_width_ppm,*search.admitted_clock_half_width_ppm,false);
    check(receiver.set_acquisition_prior(0,std::span(cold.cells).first(search.hypotheses.size())),"warm cold prerequisite failed");
    const auto cpu=std::clock();const auto wall=std::chrono::steady_clock::now();bool lost=false;
    for(std::size_t at=0;at<pcm.size();) {
        const auto block=std::span(pcm).subspan(at,std::min<std::size_t>(113,pcm.size()-at));auto remaining=block;
        const auto ready_work=[&] {
            const auto state=receiver.acquisition_state();const auto permission=receiver.helper_acquisition_dispatch();
            return state.prior_pending || (state.ready && permission.permitted && !permission.retired);
        };
        while(!remaining.empty() || ready_work()) {
            const auto state=receiver.acquisition_state();check(state.supported,"warm ordinary native seam unavailable");
            const auto process=ready_work();
            const auto incoming=process?0:std::min(remaining.size(),state.input_room_samples);
            h::ContinuationFrequencyCertificate certificate;std::optional<modem::PatternContinuationFrequencyPrior> prior;
            if(enabled && !lost)if(const auto support=receiver.continuation_scored_support(incoming);support && support->ready_established_tracks) {
                check(certificate.build(c,warm_request(*support,pcm.size()),snapshot),"fresh whole-support warm proof unexpectedly declined");
                prior=certificate.prior();check(prior.has_value(),"warm builder omitted its immutable prior");
            }
            WarmResult measured;
            if(process) {
                if(state.prior_active) {
                    modem::PatternAcquisitionAssistance assistance;assistance.continuation=prior?&*prior:nullptr;
                    assistance.continuation_result=&measured;receiver.process_assisted_acquisition(assistance);add(result.work,measured);
                    if(prior){receiver.resolve_acquisition(true,*prior,measured);add(result.work,measured);}
                    else receiver.resolve_acquisition(true);
                } else receiver.process_acquisition();
            } else {
                const auto count=prior?receiver.observe_acquisition(remaining,*prior,measured):receiver.observe_acquisition(remaining);
                check(count,"warm observation stalled");add(result.work,measured);remaining=remaining.subspan(count);
            }
        }
        at+=block.size();poll_warm(receiver,result,at,expected);
        if(!lost && at>=loss_at){check(receiver.clear_acquisition_prior(),"warm loss discarded pending batch");receiver.set_helper_gap_protection(false);receiver.set_helper_acquisition_dispatch(false);lost=true;}
    }
    result.trials=receiver.fft_work().threshold_trials;
    result.cpu=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;
    result.wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();return result;
}
void guards() {
    const auto c=config();const auto search=::search(c,0);modem::PatternReceiver receiver(c,64*1024*1024,search);
    constexpr std::size_t captured=200000;auto snapshot=warm_snapshot(c,captured);
    const auto n=std::min<std::size_t>(113,receiver.acquisition_state().input_room_samples);
    const auto support=receiver.continuation_scored_support(n);check(bool(support),"warm prospective support unavailable");
    const auto q=warm_request(*support,captured);h::ContinuationFrequencyCertificate certificate;
    check(certificate.build(c,q,snapshot),"full native prospective support did not certify");
    const auto prior=certificate.prior();WarmResult result;
    check(bool(modem::detail::pattern_continuation_frequency_prior(*prior,*support,result)),"fresh warm core certificate rejected");
    auto changed=*support;++changed.input.last_sample;
    check(!modem::detail::pattern_continuation_frequency_prior(*prior,changed,result) &&
        result.status==modem::PatternContinuationFrequencyStatus::domain_changed,"warm stale aperture was reused");
    // Stale prospective support must run the ordinary observe path, not
    // silently pause accepted work or reuse the issued aperture. A matched
    // baseline sees the exact same input and original numerical state.
    modem::PatternReceiver ordinary(c,64*1024*1024,search);
    const std::vector<float> zeros(n+1,0);
    WarmResult stale;
    check(receiver.observe_acquisition(zeros,*prior,stale)==n+1 &&
        stale.status==modem::PatternContinuationFrequencyStatus::domain_changed,
        "warm stale prospective certificate did not decline at actual observe boundary");
    check(ordinary.observe_acquisition(zeros)==n+1 &&
        ordinary.acquisition_state().observed_samples==receiver.acquisition_state().observed_samples &&
        ordinary.fft_work().threshold_trials==receiver.fft_work().threshold_trials,
        "warm stale certificate changed ordinary input/trial progression");
    // The repaired expiry seam deliberately keeps ready cold input paused.
    // A stale warm prior must not reinstate its fence or refuse ordinary PCM.
    for(;;) {
        const auto state=receiver.acquisition_state();if(state.ready)break;
        const auto count=std::min<std::size_t>(1024,state.input_room_samples);check(count,"paused fixture stalled before ready fence");
        const std::vector<float> block(count,0);
        check(receiver.observe_acquisition(block)==count && ordinary.observe_acquisition(block)==count,
            "paused fixture diverged before dispatch expiry");
    }
    receiver.set_helper_acquisition_dispatch(false);ordinary.set_helper_acquisition_dispatch(false);
    const auto paused_before=receiver.acquisition_state().observed_samples;
    check(receiver.observe_acquisition(zeros,*prior,stale)==zeros.size() && ordinary.observe_acquisition(zeros)==zeros.size() &&
        receiver.acquisition_state().observed_samples==ordinary.acquisition_state().observed_samples &&
        receiver.acquisition_state().observed_samples==paused_before+zeros.size() &&
        receiver.fft_work().threshold_trials==ordinary.fft_work().threshold_trials,
        "warm stale prior blocked paused-ready observation or scored cold work");
    auto request=q;request.input_basis=h::JointClockInputBasis::unknown;
    check(!certificate.build(c,request,snapshot),"warm unknown transform basis certified");
    request=q;request.model=h::ContinuationFrequencyModel::unknown;
    check(!certificate.build(c,request,snapshot),"warm unknown thermal/process model certified");
    request=q;request.provider_phase_diffusion_radians_per_sqrt_second=1;
    check(!certificate.build(c,request,snapshot),"warm foreign observer sigma certified");
    auto process_config=c;process_config.oscillator_search->lf.phase_noise_degrees_per_sqrt_second=.5;
    auto process_request=q;process_request.provider_phase_diffusion_radians_per_sqrt_second=
        modem::oscillator_effects(process_config).phase_noise_degrees_per_sqrt_second*std::numbers::pi/180;
    auto process_snapshot=snapshot;process_snapshot.frequency.frequency_process_uncertainty_hz=.001;
    process_snapshot.frequency.frequency_phase_diffusion_gain=.01;
    check(certificate.build(process_config,process_request,process_snapshot) &&
        certificate.result().aperture_process_expansion_hz>0,
        "warm omitted configured Wiener expansion from complete support");
    const auto ordinary_expansion=certificate.result().aperture_process_expansion_hz;
    auto faster_pairs=search.hypotheses;for(auto& pair:faster_pairs)if(pair.clock_error_ppm==600)pair.clock_error_ppm=900;
    auto faster=process_request;faster.scored.input.domain.original_pairs=faster_pairs;
    // The builder may decline incomplete catalogue coverage, but if it issues
    // an RF proof the faster retained fits must never get a smaller expansion.
    const auto faster_ok=certificate.build(process_config,faster,process_snapshot);
    check(!faster_ok || certificate.result().aperture_process_expansion_hz>=ordinary_expansion,
        "warm ignored faster immutable original LF fits in process support");
    auto old=snapshot;old.frequency_observation_frontier=2;
    check(!certificate.build(c,q,old),"warm old SourceDate granted a future scoring lease");
    auto bad=snapshot;bad.transport_recipe.kind=audio::CaptureTransportKind::unknown;
    check(!certificate.build(c,q,bad),"warm inferred a transport witness from equal rates");
    // The original source input samples/fence have not advanced by queries.
    check(receiver.acquisition_state().observed_samples==paused_before+zeros.size(),"warm support queries mutated the receiver frontier");
    std::cout<<"warm support/source-date/process/basis guards passed\n";
}
void payload(bool loss,double offset=0) {
    const auto c=config();Bytes bits(32);for(std::size_t i=0;i<bits.size();++i)bits[i]=(i*73+(i>>1)+19)%7<3;
    const auto delay=4ULL*c.sample_rate+137;constexpr long double ratio=1.0001L;
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);std::vector<std::complex<double>> analytic(source.total_samples());
    check(source.read_analytic(analytic)==analytic.size(),"warm source failed complete waveform drain");
    const auto end=delay+static_cast<std::uint64_t>(std::ceil(analytic.size()/ratio));
    std::vector<float> pcm(end+modem::pattern_absence_capture_samples(c)+4*modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c));
    std::mt19937 random(1729);std::normal_distribution<float> noise(0,.02F);const auto phase=std::polar(2.,.83);
    for(std::size_t i=0;i<pcm.size();++i){std::complex<double> value{};
        if(i>=delay && i<end){const auto position=(i-delay)*ratio;const auto k=static_cast<std::size_t>(position);
            if(k<analytic.size()){const auto next=k+1<analytic.size()?analytic[k+1]:std::complex<double>{};value=analytic[k]+static_cast<double>(position-k)*(next-analytic[k]);}}
        pcm[i]=noise(random)+static_cast<float>((phase*value).real());}
    const auto s=search(c,delay+modem::pattern_pulse_padding_samples(c));
    const auto off=receive_warm(c,s,pcm,bits,false,loss?end-modem::symbol_sample_count(c)/2:UINT64_MAX);
    const auto on=receive_warm(c,s,pcm,bits,true,loss?end-modem::symbol_sample_count(c)/2:UINT64_MAX,offset);
    const auto find=[&](const auto& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& r){return r.second.complete && r.second.bits==bits;});};
    const auto a=find(off.rows),b=find(on.rows);check(a!=off.rows.end() && b!=on.rows.end(),"warm RF assistance truncated payload/physical END");
    check(a->first==b->first && a->second.full_pending && b->second.full_pending,"warm changed pending identity or withheld final bits");
    const auto progress=[&](const Pending& row){std::size_t previous=0;for(const auto& [at,prefix]:row.progress){
        check(prefix.size()>=previous && prefix.size()<=bits.size() && std::equal(prefix.begin(),prefix.end(),bits.begin()),"warm pending prefix changed/retracted");previous=prefix.size();}};
    progress(a->second);progress(b->second);
    check(b->second.completion+16>=end+6ULL*c.sample_rate,"warm helper loss implied END before ordinary absence");
    const auto calendar=modem::FhssSymbolCalendar(*c.fake_hopping,c.sample_rate,modem::pattern_pulse_padding_samples(c),modem::symbol_sample_count(c));
    const auto absent=static_cast<std::uint64_t>(std::ceil(6.L*c.sample_rate*(1-.00060009L)/modem::symbol_sample_count(c)));
    const auto scored=static_cast<long double>(delay+modem::pattern_pulse_padding_samples(c))+
        (calendar.offset(c.stream_epoch,0,bits.size()+absent-1)+modem::symbol_sample_count(c))/(1+.00060009L);
    check(static_cast<long double>(b->second.completion)+16>=scored,"warm counted unscored FHSS silence toward END");
    check(off.trials==on.trials,"warm omitted original logical trial penalties");
    check(on.work.narrowed_windows && on.work.anchor_timing_fits && on.work.skipped_competing_fits &&
        on.work.executed_competing_fits<on.work.original_competing_fits,"warm removed no actual numeric fits");
    if(!loss){check(on.work.first_failure_broad_retries && on.work.periodic_broad_windows,"warm transition/periodic broad audits not exercised");
        check(on.work.first_failure_broad_retries<absent,"warm absence caused a same-symbol broad storm");}
    std::cout<<"warm exact bits="<<bits.size()<<" loss="<<loss<<" misleading_offset="<<offset<<" original_fits="<<on.work.original_competing_fits
        <<" executed="<<on.work.executed_competing_fits<<" skipped="<<on.work.skipped_competing_fits<<" first_failure_retries="<<on.work.first_failure_broad_retries
        <<" periodic="<<on.work.periodic_broad_windows<<" logical_trials="<<on.trials<<" CPU="<<off.cpu<<"->"<<on.cpu<<" wall="<<off.wall<<"->"<<on.wall<<'\n';
}
}
int main(int argc,char** argv){try {
    if(argc==1)guards();else if(std::string_view(argv[1])=="--payload")payload(false);
    else if(std::string_view(argv[1])=="--loss")payload(true);
    else if(std::string_view(argv[1])=="--anchor")payload(false,2.);
    else check(false,"unknown warm-reference fixture mode");return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

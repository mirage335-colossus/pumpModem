#include "datapump/helper_carrier.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "../src/joint_clock_cells.hpp"
#include "live_helper_prior.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>
#include <map>
#include <random>

using namespace datapump;
namespace h=datapump::helper;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
modem::Config config() {
    modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
    c.oscillator_search=modem::OscillatorSearchConfig{{100,0},{1,0},30000000,3};
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=3;hops->dwell_seconds=.4;
    hops->shifts[0]=1000000;hops->shifts[1]=1010000;hops->shifts[2]=1033000;
    hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
    c.fake_hopping=modem::resolve_fhss_power(*hops,c);
    check(c.fake_hopping->defer_symbols && modem::symbol_sample_count(c)==5120,"joint-clock fixture lost original B calendar");
    return c;
}
modem::PatternSearch search(const modem::Config& c,std::size_t base) {
    modem::PatternSearch s;s.start_offset_seconds=static_cast<double>(base)/c.sample_rate;s.start_uncertainty_seconds=7;
    s.search_stream_phases=true;s.initial_stream_symbols=4;s.worker_threads=1;s.prefer_streamed_templates=true;
    s.candidate_limit=128;s.track_limit=8;s.bit_limit=256;s.chunk_bits=1;s.drift_tolerant=false;s.retain_score=0;
    s.early_qualified_fft=false;s.component_qualified_search=false;
    s.admitted_clock_half_width_ppm=600.09;s.admitted_clock_cell_half_width_ppm=100.09;
    const auto step=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(int f=-7;f<=7;++f)for(const auto ppm:{-600.,-400.,-200.,0.,200.,400.,600.})s.hypotheses.push_back({f*step,ppm});
    return s;
}
h::PunctuationClockDomain raw_domain(std::uint32_t rate=14400){return {1,1,2,1,rate};}
h::LinkMapping mapping(double helper_hz,std::uint32_t rate=14400) {
    h::LinkMapping m;m.capture_generation=1;m.timebase_identity=2;m.reference_identity=1;m.reference_identity_known=true;
    m.sample_rate=rate;m.settled_end_sample=10000000;m.actual_rf_shift_hz=30000000;
    m.helper_audio_hz=helper_hz;m.data_audio_hz=1500;m.provenance=h::LinkMappingProvenance::modelled_physical_capture;
    m.phase_segment=1;return m;
}
h::LinkModel frequency_policy() {
    h::LinkModel policy;policy.rf_accuracy_fraction=3e-6;policy.audio_accuracy_fraction=300e-6;
    policy.maximum_current_measurement_age_seconds=2;return policy;
}
h::Reference frequency(const h::LinkMapping& m,double residual,double radius,std::uint64_t at) {
    h::Reference r;r.sample=at;r.sample_rate=m.sample_rate;r.frequency_hz=m.helper_audio_hz+residual;
    r.frequency_valid=true;r.frequency_uncertainty_hz=radius;r.frequency_fit=h::FrequencyFit::local_polynomial;
    r.frequency_first_sample=at>100?at-100:0;return r;
}
std::vector<std::uint8_t> safe_legacy_mask(const modem::Config& c,const modem::PatternSearch& s,
        const h::LinkResult& observed,const h::LinkMapping& actual) {
    check(observed.authority!=h::LinkAuthority::declined,"legacy prior has no current observed frequency authority");
    const auto lower=observed.helper_residual_hz.lower,upper=observed.helper_residual_hz.upper;
    const auto center=lower/2+upper/2;
    const auto radius=std::max(live::detail::helper_prior_detail::sum(center,-lower).upper,
        live::detail::helper_prior_detail::sum(upper,-center).upper);
    const auto utc=live::detail::helper_utc_frequency_allowance(c,true,actual.helper_audio_hz);
    check(utc.valid,"legacy prior lost ordinary UTC frequency allowance");
    const auto allowance=live::detail::helper_prior_detail::sum(radius,utc.radius_hz).upper;
    const auto proof=live::detail::helper_prior_mask(s.hypotheses,center,allowance,
        actual.data_audio_hz-actual.helper_audio_hz,.125*c.sample_rate/modem::symbol_sample_count(c),
        *s.admitted_clock_cell_half_width_ppm,*s.admitted_clock_half_width_ppm,false);
    check(proof.admitted && proof.origin_hz==0 && proof.represented_rates==proof.rates &&
          proof.selected<s.hypotheses.size(),"production safe all-LF frequency prior did not narrow original RF cells");
    return {proof.cells.begin(),proof.cells.begin()+static_cast<std::ptrdiff_t>(s.hypotheses.size())};
}
h::PunctuationClock synthetic_clock(double audio_ppm,std::uint32_t rate=14400) {
    h::PunctuationClock clock;const auto ratio=1+static_cast<long double>(audio_ppm)*1e-6L;
    for(unsigned i=0;i<16;++i) {
        const auto position=(.24L+.4L*i)*rate/ratio;
        const auto sample=static_cast<std::uint64_t>(std::floor(position));
        h::Reference r;r.sample_rate=rate;r.sample=sample+101;
        r.punctuation={1,sample,sample-100,sample+100,static_cast<double>(position-sample),.01,true};
        check(clock.observe(r,raw_domain(rate),r.sample+1)==h::PunctuationClockReason::accepted,"synthetic actual marker rejected");
    }
    return clock;
}
h::PunctuationClockResult qualify(const h::PunctuationClock& clock,std::uint64_t first,std::uint64_t last,std::uint64_t captured) {
    h::PunctuationClockRequest request{first,last,captured,60ULL*14400,
        h::PunctuationClockModel::constant_rate_over_requested_support,0.};
    return clock.qualify({-300,300},request);
}
modem::PatternAcquisitionScoredSupport support(std::span<const modem::PatternFrequencyRateHypothesis> pairs,
        std::span<const std::uint8_t> mask) {
    modem::PatternAcquisitionScoredSupport s{10000,50000,48000};
    s.domain={pairs,mask,modem::detail::clock_prior_detail::mask_id(mask),0,0,5120,14400,0,.3515625,600.09,100.09};
    return s;
}
h::JointClockCellRequest request(modem::PatternAcquisitionScoredSupport support,std::uint64_t captured) {
    return {support,raw_domain(),support.first_sample,support.last_sample,captured,captured,true,
        h::JointClockInputBasis::unshifted_fixed_filter_and_nominal_resampler};
}
void cells_and_provenance() {
    const auto c=config();const auto s=search(c,0);std::vector<std::uint8_t> legacy(s.hypotheses.size(),1);
    auto issued=support(s.hypotheses,legacy);const auto clock=synthetic_clock(100);
    const auto fit=qualify(clock,issued.first_sample,issued.last_sample,100000);
    check(fit.qualified_for_acquisition && fit.physical_audio_lower_ppm<=100 && fit.physical_audio_upper_ppm>=100,
        "dated actual punctuation did not certify physical audio");
    for(const auto H:{5275.,-5275.}) {
        const auto m=mapping(H);const auto f=frequency(m,H*100e-6,.005,99999);
        h::JointClockCellCertificate certificate;
        check(certificate.build(c,request(issued,100000),fit,{f,m,{}},frequency_policy(),raw_domain()),"joint cell certificate rejected finite complete original catalogue");
        const auto prior=certificate.prior();check(prior.has_value(),"accepted builder omitted immutable certificate");
        modem::PatternAcquisitionClockResult accepted;
        check(modem::detail::pattern_fft_clock_prior(*prior,issued,accepted).has_value() &&
              accepted.status==modem::PatternAcquisitionClockStatus::active,"core rejected actual builder certificate");
        check(accepted.selected_clock_rates>=3 && accepted.selected_clock_rates<accepted.original_clock_rates &&
              accepted.selected_pairs<accepted.original_pairs,"joint clock did not retain unresolved UTC cells and reduce work");
        // Endpoint/cell-interior grid includes BOTH signs of actual c. This is
        // a coverage oracle, not receiver knowledge of transmitter clock truth.
        const auto C=modem::utc_transmit_rate_limit(c);
        for(const auto a:{fit.physical_audio_lower_ppm,100.,fit.physical_audio_upper_ppm})
            for(unsigned j=0;j<=40;++j)for(const auto hertz:{f.frequency_hz-H-f.frequency_uncertainty_hz,f.frequency_hz-H+f.frequency_uncertainty_hz}) {
                const long double correction=-C+2*static_cast<long double>(C)*j/40;
                const long double d=(1+static_cast<long double>(a)*1e-6L)*(1+correction)-1;
                const long double rf=hertz+c.carrier_hz*d-H*static_cast<long double>(a)*1e-6L;
                bool covered=false;
                for(std::size_t k=0;k<s.hypotheses.size();++k)if(prior->pair_mask()[k]) {
                    const auto& cell=s.hypotheses[k];
                    covered|=std::abs(static_cast<long double>(cell.clock_error_ppm)-d*1e6L)<=issued.domain.clock_cell_half_width_ppm &&
                        std::abs(static_cast<long double>(cell.frequency_offset_hz)-rf)<=issued.domain.frequency_cell_radius_hz;
                }
                check(covered,"joint RF/DATA-LF union omitted a feasible physical a/c boundary or interior point");
            }
        auto changed=issued;changed.domain.input_sample_origin=1;
        modem::PatternAcquisitionClockResult rejected;
        check(!modem::detail::pattern_fft_clock_prior(*prior,changed,rejected) &&
              rejected.status==modem::PatternAcquisitionClockStatus::domain_changed,"certificate crossed receiver input view");
        changed=issued;++changed.consumed_end_sample;
        check(!modem::detail::pattern_fft_clock_prior(*prior,changed,rejected) &&
              rejected.status==modem::PatternAcquisitionClockStatus::uncovered_scored_support,"certificate crossed batch fence");
        auto q=request(issued,100000);q.raw_domain.rf_gauge=2;
        check(!certificate.build(c,q,fit,{f,m,{}},frequency_policy(),raw_domain()),"clock certificate crossed actual retune gauge");
        q=request(issued,100000);q.raw_last_sample=fit.requested_last_sample+1;
        check(!certificate.build(c,q,fit,{f,m,{}},frequency_policy(),raw_domain()) &&
              certificate.result().reason==h::JointClockCellReason::uncovered_scored_support,"unobserved scored support was clipped to force clock qualification");
    }
    {
        const auto m=mapping(5275);const auto f=frequency(m,5275*100e-6,.005,99999);
        auto unknown=request(issued,100000);unknown.input_basis=h::JointClockInputBasis::unknown;
        h::JointClockCellCertificate declined;
        check(!declined.build(c,unknown,fit,{f,m,{}},frequency_policy(),raw_domain()) &&
              declined.result().reason==h::JointClockCellReason::unsupported_input_basis,
            "external transformed/unknown input basis silently inherited raw authority");
        auto overflow=issued;overflow.domain.input_sample_origin=UINT64_MAX-100;
        check(!declined.build(c,request(overflow,100000),fit,{f,m,{}},frequency_policy(),raw_domain()) &&
              declined.result().reason==h::JointClockCellReason::uncovered_scored_support,
            "absolute input support overflow produced a certificate");
        // Distinct RAW and decoder clocks are valid only via the explicitly
        // certified nominal, unshifted adapter and complete support expansion.
        const auto fast_clock=synthetic_clock(100,28800);
        const auto fast_fit=qualify(fast_clock,20000,100000,200000);
        const auto fast_mapping=mapping(5275,28800);
        const auto fast_frequency=frequency(fast_mapping,5275*100e-6,.005,199999);
        auto expanded=request(issued,100000);expanded.raw_domain=raw_domain(28800);
        expanded.raw_first_sample=20000;expanded.raw_last_sample=100000;expanded.raw_captured_end_sample=200000;
        h::JointClockCellCertificate nominal;
        check(nominal.build(c,expanded,fast_fit,{fast_frequency,fast_mapping,{}},frequency_policy(),raw_domain(28800)),
            "exact nominal resampler support changed physical clock or Hz units");
    }
    // Exact touching clock cells cover the whole interval; a genuine one-ulp
    // gap is not joined by a tolerance or nearest-lane heuristic.
    {
        auto exact=issued;exact.domain.clock_cell_half_width_ppm=100;
        const auto m=mapping(5275);const auto f=frequency(m,5275*100e-6,.005,99999);
        h::JointClockCellCertificate touching;
        check(touching.build(c,request(exact,100000),fit,{f,m,{}},frequency_policy(),raw_domain()),
            "exactly touching original DATA clock cells were treated as a gap");
        exact.domain.clock_cell_half_width_ppm=std::nextafter(100.,0.);
        h::JointClockCellCertificate gap;
        check(!gap.build(c,request(exact,100000),fit,{f,m,{}},frequency_policy(),raw_domain()) &&
              gap.result().reason==h::JointClockCellReason::uncovered_joint_cells,
            "real original DATA clock gap was silently joined");
        auto q=request(issued,100000);q.raw_captured_end_sample=fit.evidence_sample;
        h::JointClockCellCertificate future;
        check(!future.build(c,q,fit,{f,m,{}},frequency_policy(),raw_domain()),
            "marker evidence at exclusive captured end granted a clock mask");
    }
    // Remove a genuinely required original RF cell from the legacy safe mask.
    // The new certificate must decline, not blindly AND away its truth cell.
    const auto m=mapping(5275);const auto f=frequency(m,5275*100e-6,.005,99999);
    h::JointClockCellCertificate good;check(good.build(c,request(issued,100000),fit,{f,m,{}},frequency_policy(),raw_domain()),"mask-hole setup declined");
    const auto prior=good.prior();const auto selected=std::find(prior->pair_mask().begin(),prior->pair_mask().end(),1);
    check(selected!=prior->pair_mask().end(),"mask-hole setup selected no pair");
    legacy[static_cast<std::size_t>(selected-prior->pair_mask().begin())]=0;
    issued.domain.legacy_mask_id=modem::detail::clock_prior_detail::mask_id(legacy);
    h::JointClockCellCertificate hole;
    check(!hole.build(c,request(issued,100000),fit,{f,m,{}},frequency_policy(),raw_domain()) &&
          hole.result().reason==h::JointClockCellReason::legacy_frequency_mask_too_narrow,"joint mask silently dropped required RF cells");
    std::cout<<"joint cells preserve both UTC signs and helper coefficients; exact domain/support/hole declines passed\n";
}
struct Measured {
    h::PunctuationClock clock;
    h::Reference last;
    h::LinkMapping actual;
    h::LinkResult frequency;
};
Measured actual_helper(const modem::Config& c) {
    h::Options options;options.power_w=15000;options.data_power_w=4;options.timecode=false;
    const auto plan=h::plan(options,h::placement_band(c),c.sample_rate);
    h::Generator source(options,plan,c.stream_epoch,true);h::Receiver receiver(plan.frequency_hz,20);
    Measured measured;measured.actual=mapping(plan.frequency_hz);
    constexpr long double ratio=1.0001L;std::array<float,113> input{};std::size_t unique=0;
    for(std::uint64_t at=0;at<6ULL*c.sample_rate;) {
        const auto n=std::min<std::uint64_t>(input.size(),6ULL*c.sample_rate-at);
        for(std::size_t i=0;i<n;++i)input[i]=source.mix(0,ratio*(at+i)/c.sample_rate,c.sample_rate);
        receiver.push(std::span(input).first(n),c.sample_rate,at);at+=n;
        if(auto update=receiver.take_update()) {
            measured.last=*update;
            if(update->punctuation.known) {
                const auto result=measured.clock.observe(*update,raw_domain(),at);
                check(result==h::PunctuationClockReason::accepted || result==h::PunctuationClockReason::duplicate_marker,
                    "actual helper punctuation lost provenance");unique+=result==h::PunctuationClockReason::accepted;
            }
        }
    }
    check(unique>=8,"actual source did not expose enough independent markers");
    h::LinkModel policy;policy.rf_accuracy_fraction=3e-6;policy.audio_accuracy_fraction=300e-6;policy.maximum_current_measurement_age_seconds=2;
    h::LinkState link;check(link.reset(policy),"actual helper link policy rejected");
    measured.frequency=link.observe({measured.last,measured.actual,{}},6ULL*c.sample_rate);
    check(measured.frequency.authority!=h::LinkAuthority::declined,"actual raw helper frequency was declined");
    std::cout<<"actual clock markers="<<unique<<" H="<<plan.frequency_hz<<" CFO=["
        <<measured.frequency.helper_residual_hz.lower<<','<<measured.frequency.helper_residual_hz.upper<<"]\n";
    return measured;
}
struct Seen {
    Bytes bits;bool complete=false,full_pending=false;
    bool provenance_seen=false,provenance_consistent=true,helper_acquired=false,helper_conditioned_acquisition=false;
    std::uint64_t completion=0;std::size_t polls=0;
};
using Identity=std::pair<std::uint64_t,std::uint64_t>;
void poll(modem::PatternReceiver& receiver,std::map<Identity,Seen>& rows,std::uint64_t frontier,const Bytes& expected) {
    for(const auto& b:receiver.take_bursts()) {
        auto& row=rows[{b.stream_first_sample,b.stream_first_symbol}];check(!row.complete,"joint clock changed completed row");
        if(b.first_stream_symbol-b.stream_first_symbol!=row.bits.size())continue;
        check(!b.missing_slots,"joint clock manufactured a payload gap");
        if(row.provenance_seen)row.provenance_consistent=row.provenance_consistent &&
            row.helper_acquired==b.helper_acquired && row.helper_conditioned_acquisition==b.helper_conditioned_acquisition;
        else {
            row.provenance_seen=true;row.helper_acquired=b.helper_acquired;
            row.helper_conditioned_acquisition=b.helper_conditioned_acquisition;
        }
        row.bits.insert(row.bits.end(),b.bits.begin(),b.bits.end());row.complete=b.complete;++row.polls;
        // Wrong-key/epoch/noise candidates are allowed. The matched complete
        // source chain is checked below, with exact prefixes from its first bit.
        if(row.bits.size()<=expected.size() && std::equal(row.bits.begin(),row.bits.end(),expected.begin()))
            row.full_pending|=!row.complete && row.bits.size()==expected.size();
        if(b.complete)row.completion=frontier;
    }
}
struct Work {std::uint64_t active=0,declined=0,minimum_pairs=UINT64_MAX;double cpu=0,wall=0;};
void feed(modem::PatternReceiver& receiver,std::span<const float> input,bool assisted,bool loss,
          std::uint64_t captured,const modem::Config& c,const Measured& measured,Work& work) {
    const auto cpu=std::clock();const auto wall=std::chrono::steady_clock::now();
    while(!input.empty() || receiver.acquisition_state().ready) {
        const auto state=receiver.acquisition_state();check(state.supported,"joint native seam unavailable");
        if(state.ready) {
            bool used=false;
            if(assisted && !loss && state.prior_active)if(const auto support=receiver.acquisition_scored_support()) {
                constexpr std::uint64_t raw_captured=6ULL*14400;
                const auto clock=qualify(measured.clock,support->first_sample,support->last_sample,raw_captured);
                auto q=request(*support,captured);q.raw_captured_end_sample=raw_captured;
                h::JointClockCellCertificate certificate;
                if(certificate.build(c,q,clock,{measured.last,measured.actual,{}},frequency_policy(),raw_domain())) {
                    const auto prior=certificate.prior();modem::PatternAcquisitionClockResult result;
                    receiver.process_prior_acquisition(*prior,result);used=true;
                    check(result.completed && result.status==modem::PatternAcquisitionClockStatus::active,
                        "actual builder certificate was not executed synchronously");
                    work.active+=result.narrowed;work.minimum_pairs=std::min<std::uint64_t>(work.minimum_pairs,result.selected_pairs);
                } else ++work.declined;
            }
            if(!used){if(state.prior_active)receiver.process_prior_acquisition();else receiver.process_acquisition();}
            if(state.prior_active)receiver.resolve_acquisition(true);
        } else {
            const auto used=receiver.observe_acquisition(input);check(used,"joint clock observation stalled");input=input.subspan(used);
        }
    }
    work.cpu+=static_cast<double>(std::clock()-cpu)/CLOCKS_PER_SEC;
    work.wall+=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
}
void payload(double c_fraction,const Measured& measured) {
    const auto c=config();const Bytes bits{0,0,1,0,1,1,0,0,1,0,1,1};
    const auto delay=4ULL*c.sample_rate+137;
    const long double physical_audio=1.0001L,data_rate=physical_audio*(1+static_cast<long double>(c_fraction));
    modem::PatternTransmitter source(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(source.total_samples());source.read_analytic(analytic);
    const auto data_extent=static_cast<std::uint64_t>(std::ceil(analytic.size()/data_rate));
    const auto end=delay+data_extent;
    std::vector<float> pcm(end+modem::pattern_absence_capture_samples(c)+4*modem::symbol_sample_count(c)+2*modem::pattern_pulse_padding_samples(c));
    std::mt19937 random(1729);std::normal_distribution<float> noise(0,.02F);const auto phase=std::polar(2.,.83);
    for(std::size_t i=0;i<pcm.size();++i) {
        std::complex<double> value{};
        if(i>=delay && i<end) {
            const auto position=(i-delay)*data_rate;const auto k=static_cast<std::size_t>(position);
            if(k<analytic.size()){const auto next=k+1<analytic.size()?analytic[k+1]:std::complex<double>{};
                value=analytic[k]+static_cast<double>(position-k)*(next-analytic[k]);}
        }
        pcm[i]=noise(random)+static_cast<float>((phase*value).real());
    }
    const auto s=search(c,delay+modem::pattern_pulse_padding_samples(c));
    modem::PatternReceiver ordinary(c,64*1024*1024,s),assisted(c,64*1024*1024,s);
    const auto legacy=safe_legacy_mask(c,s,measured.frequency,measured.actual);
    check(ordinary.set_acquisition_prior(0,legacy)&&assisted.set_acquisition_prior(0,legacy),"joint original all-LF mask declined");
    std::map<Identity,Seen> baseline,conditioned;Work wb,wa;bool loss=false;
    // Captured PCM and actual helper observations may precede decoder work;
    // no phase or known-Tx timestamp is supplied by this finite replay.
    const auto captured=static_cast<std::uint64_t>(pcm.size());
    for(std::size_t at=0;at<pcm.size();) {
        const auto count=std::min<std::size_t>(113,pcm.size()-at);const auto block=std::span(pcm).subspan(at,count);
        feed(ordinary,block,false,loss,captured,c,measured,wb);feed(assisted,block,true,loss,captured,c,measured,wa);
        at+=count;poll(ordinary,baseline,at,bits);poll(assisted,conditioned,at,bits);
        if(!loss && at>=end-modem::symbol_sample_count(c)/2) {
            check(ordinary.clear_acquisition_prior()&&assisted.clear_acquisition_prior(),"helper loss discarded established ordinary owner");loss=true;
        }
    }
    const auto exact=[&](const auto& rows){return std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.second.complete&&row.second.bits==bits;});};
    const auto off=exact(baseline),on=exact(conditioned);
    check(off!=baseline.end() && on!=conditioned.end(),"joint clock truncated or changed complete ordinary payload");
    check(off->second.provenance_seen && on->second.provenance_seen &&
          off->second.provenance_consistent && on->second.provenance_consistent &&
          off->second.helper_conditioned_acquisition && on->second.helper_conditioned_acquisition &&
          !off->second.helper_acquired && !on->second.helper_acquired,
        "actual conditional calendar birth lost durable provenance or changed legacy overlap policy");
    check(off->first==on->first && off->second.full_pending && on->second.full_pending &&
          off->second.polls>=bits.size() && on->second.polls>=bits.size(),"joint clock changed row identity/final pending/per-bit progress");
    const auto bins=std::max(ordinary.acquisition_state().bin_samples,assisted.acquisition_state().bin_samples);
    check(on->second.completion+4*bins>=end+6ULL*c.sample_rate,"helper loss or source endpoint implied physical END");
    const auto completion_delta=off->second.completion>on->second.completion?off->second.completion-on->second.completion:on->second.completion-off->second.completion;
    check(completion_delta<=4*bins+113,"joint clock changed physical END beyond ordinary refinement/polling");
    check(wa.active && wa.minimum_pairs<legacy.size() && assisted.fft_work().template_jobs<ordinary.fft_work().template_jobs,
        "joint clock failed to remove actual expensive original-pair jobs");
    check(ordinary.fft_work().threshold_trials==assisted.fft_work().threshold_trials,
        "joint clock altered original acquisition trial penalties");
    std::cout<<"joint payload c="<<c_fraction<<" data_ppm="<<static_cast<double>((data_rate-1)*1e6L)
        <<" exact="<<bits.size()<<" active="<<wa.active<<" selected_pairs_min="<<wa.minimum_pairs<<'/'<<legacy.size()
        <<" template_jobs="<<ordinary.fft_work().template_jobs<<"->"<<assisted.fft_work().template_jobs
        <<" FFT_jobs="<<ordinary.fft_work().fft_jobs<<"->"<<assisted.fft_work().fft_jobs
        <<" CPU_seconds="<<wb.cpu<<"->"<<wa.cpu<<" wall_seconds="<<wb.wall<<"->"<<wa.wall
        <<" completion="<<off->second.completion<<"->"<<on->second.completion<<'\n';
}
}
int main(){try {
    cells_and_provenance();const auto measured=actual_helper(config());
    const auto C=modem::utc_transmit_rate_limit(config());payload(-C,measured);payload(C,measured);
    std::cout<<"joint RF/DATA clock certificate passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

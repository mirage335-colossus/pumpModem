#include "datapump/streaming_modem.hpp"
#include "datapump/channel.hpp"
#include "../src/native_diagnostic_history.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <map>
#include <numbers>
#include <random>
#include <tuple>

using namespace datapump;
namespace {
constexpr std::size_t budget=16*1024*1024;
void check(bool value,const char* message){if(!value)throw Error(message);}
struct Capture {
    modem::Config config;
    Bytes bits{0,0,1};
    std::vector<float> pcm;
    std::uint64_t begin=0,end=0;
};
double phase_wander(long double sample,std::uint32_t rate) {
    const auto seconds=sample/rate;
    return seconds<4?0:seconds<8?static_cast<double>((seconds-4)*.25):
        static_cast<double>(1-(seconds-8)*.1);
}
Capture capture(bool long_symbol,double offset=0,bool wander=false,bool outer=false) {
    Capture result;auto& c=result.config;
    c.sample_rate=long_symbol?256:2000;c.carrier_hz=long_symbol?64:500;
    c.bandwidth_hz=long_symbol?8:200;c.spreading_factor=128;
    if(outer){c.sample_rate=6000;c.carrier_hz=1300;c.bandwidth_hz=100;c.spreading_factor=64;c.dsss_factor=10;}
    c.scramble=true;c.pulse_shaping=false;c.stream_epoch=1800000041;
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=std::uint8_t(13*i+29);
    if(outer)for(std::size_t i=0;i<c.dsss_seed.size();++i)c.dsss_seed[i]=std::uint8_t(7*i+113);
    modem::StreamingTransmitter tx(modem::RawBits{result.bits},c,budget);
    std::vector<std::complex<double>> source(tx.total_samples());
    check(tx.read_analytic(source)==source.size(),"acquisition source PCM truncated");
    const auto prefix=wander?4*c.sample_rate:0;
    result.begin=modem::training_sample_count(c)+prefix;result.end=tx.physical_valid_payload_end()+prefix;
    const auto tail=std::max<std::uint64_t>(12*c.sample_rate,2*modem::symbol_sample_count(c));
    result.pcm.resize(prefix+source.size()+tail);
    std::mt19937_64 random(5181);std::normal_distribution<double> noise(0,outer?.001:.01);
    for(auto& value:result.pcm)value=float(noise(random));
    for(std::size_t i=0;i<source.size();++i)
        result.pcm[prefix+i]+=float(.08*std::real(source[i]*std::polar(1.,.73+2*std::numbers::pi*offset*i/c.sample_rate+
            (wander?phase_wander(prefix+i,c.sample_rate):0))));
    return result;
}
modem::PatternSearch search(const Capture& wave) {
    modem::PatternSearch s;s.worker_threads=1;s.initial_stream_symbols=1;s.chunk_bits=1;
    s.drift_tolerant=false;s.differential_window_seconds=0;
    s.start_offset_seconds=double(wave.begin)/wave.config.sample_rate;s.start_uncertainty_seconds=.01;
    s.early_qualified_fft=false;
    for(const auto f:std::array<double,7>{-2,-1,-.5,0,.5,1,2})
        for(const auto rate:std::array<double,3>{-100,0,100})s.hypotheses.push_back({f,rate});
    return s;
}
struct Result {
    std::vector<modem::PatternBurst> events;
    std::vector<modem::PatternEvidence> evidence;
    modem::PatternFftWork work;
    std::size_t held=0;
};
Result receive(const Capture& wave,std::size_t chunk,bool split,bool prior=false,double origin=0,bool expire=false,bool qualified_early=false,
               std::size_t workers=1,modem::PatternPhaseCurve* curve=nullptr,bool close_after_bit=false,
               bool expect_exact=true,bool cohort_link=false,bool full_prior=false) {
    auto s=search(wave);s.worker_threads=workers;
    if(qualified_early) {
        s.early_qualified_fft=true;
        s.qualified_start_window=modem::PatternStartWindow{static_cast<long double>(wave.begin),1,20};
    }
    if(!prior&&origin!=0)for(auto& h:s.hypotheses)h.frequency_offset_hz+=origin;
    modem::StreamingReceiver receiver(wave.config,budget,s);
    if(curve)check(receiver.set_acquisition_phase_view(curve),"bounded ordinary phase view declined native geometry");
    check(!receiver.clock_windowed(),"acquisition fixture did not select native executor");
    const auto initial=receiver.acquisition_state();
    if(!qualified_early)check(initial.supported&&initial.input_room_samples,"native acquisition seam unavailable");
    std::vector<std::uint8_t> mask;
    if(prior) {
        const auto catalogue=receiver.acquisition_hypotheses();
        check(catalogue.size()==s.hypotheses.size(),"original acquisition catalogue changed");
        mask.resize(catalogue.size());
        for(std::size_t i=0;i<catalogue.size();++i)mask[i]=full_prior || catalogue[i].frequency_offset_hz==0;
        check(receiver.set_acquisition_prior(origin,mask),"valid conditional prior was declined");
        if(full_prior)check(receiver.acquisition_state().selected_pairs==catalogue.size(),
                            "translated complete catalogue lost original frequency/clock alternatives");
    }
    Result result;
    const auto resolve=[&](bool accepted) {
        ++result.held;receiver.resolve_acquisition((accepted||cohort_link)&&!expire);
        if((accepted||cohort_link)&&!expire)return;
        // A fixed finite probation retries only fresh windows. Every missed
        // original window has already received its complete broad audit.
        prior=!accepted&&result.held<3&&receiver.set_acquisition_prior(origin,mask);
    };
    for(std::size_t at=0;at<wave.pcm.size();) {
        const auto count=std::min(chunk,wave.pcm.size()-at);
        const auto input=std::span(wave.pcm).subspan(at,count);
        if(!split)receiver.push(input);
        else for(std::size_t used=0;used<input.size();) {
            const auto state=receiver.acquisition_state();
            if(!state.supported&&qualified_early&&!prior) {
                receiver.push(input.subspan(used));used=input.size();continue;
            }
            check(state.supported,"acquisition seam changed geometry during capture");
            if(state.ready) {
                const auto before=receiver.fft_work();
                check(receiver.observe_acquisition(input.subspan(used))==0,"pending original batch consumed more PCM");
                if(prior) {
                    const bool accepted=receiver.process_prior_acquisition();
                    const auto held=receiver.acquisition_state();
                    check(held.ready&&held.prior_pending,"conditional pass released its original cursor");
                    check(receiver.observe_acquisition(input.subspan(used))==0,"conditional miss overwrote pending audit input");
                    // A genuine ordinary bit is the only local link support.
                    // A miss receives a same-window full original audit.
                    resolve(accepted);
                } else receiver.process_acquisition();
                const auto after=receiver.fft_work();
                check(after.threshold_trials>=before.threshold_trials,"conditional trial spending regressed");
                continue;
            }
            const auto consumed=receiver.observe_acquisition(input.subspan(used));
            check(consumed>0&&consumed<=input.size()-used,"acquisition seam made no input progress");
            used+=consumed;
        }
        at+=count;
        // Ready acquisition is processed before this ordinary progress poll.
        if(split&&receiver.acquisition_state().ready) {
            if(prior) {
                const bool accepted=receiver.process_prior_acquisition();
                resolve(accepted);
            } else receiver.process_acquisition();
        }
        for(auto& burst:receiver.take_pattern_bursts()) {
            check(at>=burst.end_sample,"split acquisition published an unobserved bit");
            if(burst.complete)check(at>=burst.end_sample+modem::pattern_absence_samples(wave.config),
                                    "split acquisition shortened scored physical END");
            if(curve && close_after_bit && !burst.bits.empty())curve->close();
            result.events.push_back(std::move(burst));
        }
        check(receiver.working_bytes()<=budget,"split acquisition exceeded numeric grant");
    }
    result.work=receiver.fft_work();
    result.evidence=receiver.pattern_candidates(2048);
    std::map<std::pair<std::uint64_t,std::uint64_t>,std::pair<Bytes,unsigned>> streams;
    for(const auto& event:result.events) {
        check(!event.missing_slots,"acquisition fixture introduced an unknown slot");
        auto& stream=streams[{event.stream_first_sample,event.stream_first_symbol}];
        stream.first.insert(stream.first.end(),event.bits.begin(),event.bits.end());stream.second+=event.complete;
    }
    const bool exact=std::any_of(streams.begin(),streams.end(),[&](const auto& item){return item.second.first==wave.bits&&item.second.second==1;});
    if(!exact && expect_exact) {
        std::cerr<<"capture Fs="<<wave.config.sample_rate<<" chunk="<<chunk<<" split="<<split<<" origin="<<origin<<" events="<<result.events.size()<<'\n';
        std::cerr<<"jobs="<<result.work.template_jobs<<" conditional="<<result.work.conditional_template_jobs<<" audits="<<result.work.original_audit_template_jobs
                 <<" trials="<<result.work.threshold_trials<<" shifted="<<result.work.shifted_reference_trials<<'\n';
        for(const auto& candidate:receiver.pattern_candidates(4))std::cerr<<"candidate "<<candidate.first_sample<<" f="<<candidate.frequency_hz<<" score="<<candidate.score
                                                                       <<" threshold="<<candidate.admission_threshold<<'\n';
        for(const auto& [identity,stream]:streams) {
            std::cerr<<"stream "<<identity.first<<':'<<identity.second<<" bits=";
            for(auto bit:stream.first)std::cerr<<unsigned(bit);
            std::cerr<<" completions="<<stream.second<<'\n';
        }
    }
    if(expect_exact)check(exact,"acquisition fixture did not deliver exact 001 and scored END");
    return result;
}
void original_equivalence() {
    for(const bool long_symbol:{false,true}) {
        const auto wave=capture(long_symbol);
        for(const std::size_t chunk:{37,257,4096}) {
            const auto ordinary=receive(wave,chunk,false),split=receive(wave,chunk,true);
            check(std::none_of(ordinary.events.begin(),ordinary.events.end(),[](const auto& b){return b.helper_acquired;}) &&
                  std::none_of(split.events.begin(),split.events.end(),[](const auto& b){return b.helper_acquired;}),
                  "ordinary acquisition invented helper provenance");
            check(ordinary.events.size()==split.events.size(),"observe/full-acquire changed event count");
            for(std::size_t i=0;i<ordinary.events.size();++i) {
                const auto& a=ordinary.events[i];const auto& b=split.events[i];
                check(std::tie(a.bits,a.first_sample,a.end_sample,a.complete,a.stream_first_sample,a.stream_first_symbol,
                               a.score,a.frequency_hz,a.support_samples)==
                      std::tie(b.bits,b.first_sample,b.end_sample,b.complete,b.stream_first_sample,b.stream_first_symbol,
                               b.score,b.frequency_hz,b.support_samples),"observe/full-acquire changed ordinary decisions");
            }
            check(std::tie(ordinary.work.template_jobs,ordinary.work.input_transforms,ordinary.work.threshold_trials)==
                  std::tie(split.work.template_jobs,split.work.input_transforms,split.work.threshold_trials),
                  "observe/full-acquire changed ordinary work or trials");
        }
    }
}
void conditional_work_and_fallback() {
    const auto wave=capture(false);
    const auto full=receive(wave,257,true),conditioned=receive(wave,257,true,true);
    check(conditioned.held&&conditioned.work.conditional_template_jobs,
          "conditional acquisition executed no measured jobs");
    std::cout<<"acquisition jobs ordinary="<<full.work.template_jobs<<" conditional_total="<<conditioned.work.template_jobs
             <<" narrow="<<conditioned.work.conditional_template_jobs<<" audits="<<conditioned.work.original_audit_template_jobs
             <<" ordinary_trials="<<full.work.threshold_trials<<" conditional_trials="<<conditioned.work.threshold_trials<<'\n';
    check(conditioned.work.template_jobs==conditioned.work.conditional_template_jobs+conditioned.work.original_audit_template_jobs,
          "conditional work counters omitted an acquisition path");
    check(conditioned.work.template_jobs<full.work.template_jobs,
          "conditional acquisition and all its audits did not reduce total original template jobs");
    const auto misleading=receive(wave,257,true,true,6);
    check(misleading.work.original_audit_template_jobs>0,
          "misleading helper did not execute a same-window original audit");
    check(misleading.work.threshold_trials==full.work.threshold_trials,
          "misleading shifted helper changed the original-origin trial counter");
    receive(capture(false,3),257,false,false,3);
    receive(capture(false,3),257,true,true,3);
    const auto expired=receive(capture(false,3),257,true,true,3,true);
    check(expired.work.original_audit_template_jobs>0,
          "accepted shifted track prevented an original-origin audit on expiry");
    // Original restricted/component scheduling owns its diagnostic journal.
    // Optional helper admission yields; ordinary qualified acquisition stays.
    {
        auto restricted=search(wave);restricted.early_qualified_fft=true;
        restricted.qualified_start_window=modem::PatternStartWindow{static_cast<long double>(wave.begin),1,20};
        modem::StreamingReceiver r(wave.config,budget,restricted);
        std::vector<std::uint8_t> m(restricted.hypotheses.size());
        for(std::size_t f=0;f<m.size();++f)m[f]=restricted.hypotheses[f].frequency_offset_hz==0;
        const auto bytes=r.working_bytes();
        check(!r.set_acquisition_prior(6,m) && r.working_bytes()==bytes,
              "helper borrowed an original restricted timing journal");
    }
    receive(wave,257,true,false,0,false,true);
    const auto fractional=receive(capture(false,.1),257,false,false,.1);
    check(fractional.work.shifted_reference_trials==0,
          "ordinary fractional carrier rounding applied a helper-only threshold or counter");
}
void original_clock_and_domain() {
    const auto wave=capture(false);auto s=search(wave);
    modem::StreamingReceiver receiver(wave.config,budget,s);
    const auto before=receiver.working_bytes();const auto catalogue=receiver.acquisition_hypotheses();
    std::vector<std::uint8_t> missing(catalogue.size());missing[0]=1;
    check(!receiver.set_acquisition_prior(0,missing),"helper prior discarded original LF clock alternatives");
    check(receiver.working_bytes()==before&&!receiver.acquisition_state().prior_active,"rejected helper mask mutated ordinary state");
    std::vector<std::uint8_t> all(catalogue.size(),1);
    check(!receiver.set_acquisition_prior(0,all),"untranslated full helper mask added no frequency assistance");
    check(receiver.working_bytes()==before&&!receiver.acquisition_state().prior_active,
          "rejected untranslated full mask mutated ordinary state");
    std::vector<std::uint8_t> selected(catalogue.size());
    for(std::size_t i=0;i<catalogue.size();++i)selected[i]=catalogue[i].frequency_offset_hz==0;
    check(!receiver.set_acquisition_prior(900,selected),"helper prior exceeded original projection/passband geometry");
    check(receiver.set_acquisition_prior(0,selected),"complete original clock mask declined");
    receiver.trim_optional_workspace();
    check(!receiver.acquisition_state().prior_active,"optional prior did not yield its storage");
}
void complete_catalogue_translation() {
    const auto wave=capture(false,3);
    const auto aligned=receive(wave,257,true,true,3,false,false,1,nullptr,false,true,false,true);
    check(aligned.work.conditional_template_jobs && aligned.work.shifted_reference_trials,
          "complete translated catalogue did not execute separately charged helper-domain work");
    check(std::any_of(aligned.events.begin(),aligned.events.end(),[&](const auto& event) {
        return !event.bits.empty() && event.frequency_hz==wave.config.carrier_hz+3;
    }),"complete translated catalogue did not retain its actual RF alignment");
    const auto audited=receive(wave,257,true,true,3,true,false,1,nullptr,false,true,false,true);
    check(audited.work.original_audit_template_jobs,
          "complete translated track suppressed its required original-origin audit");
    const auto plain=capture(false);
    const auto baseline=receive(plain,257,true);
    const auto misleading=receive(plain,257,true,true,6,false,false,1,nullptr,false,true,false,true);
    check(misleading.work.original_audit_template_jobs &&
          misleading.work.threshold_trials==baseline.work.threshold_trials,
          "misleading complete translation changed original-window fallback or its trial accounting");
    std::cout<<"full-catalogue alignment original_pairs="<<search(wave).hypotheses.size()
             <<" conditional_jobs="<<aligned.work.conditional_template_jobs
             <<" forced_audit_jobs="<<audited.work.original_audit_template_jobs<<'\n';
}
void original_clock_cell_metadata() {
    modem::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=200;c.spreading_factor=32;
    c.scramble=true;c.oscillator_search=modem::OscillatorSearchConfig{};
    auto& policy=*c.oscillator_search;policy.lf={100,.5};policy.rf={1,.05};policy.rf_shift_hz=30000000;policy.margin=3;
    const auto declared=modem::oscillator_pattern_search(c);
    check(declared.hypotheses.size()==233 && declared.clock_step_ppm/2>599,
          "thin original clock-cell fixture lost its clipped timing geometry");
    std::vector<double> rates;
    for(const auto& pair:declared.hypotheses)if(std::find(rates.begin(),rates.end(),pair.clock_error_ppm)==rates.end())
        rates.push_back(pair.clock_error_ppm);
    std::sort(rates.begin(),rates.end());
    check(rates==std::vector<double>{-300,0,300} && declared.clock_step_ppm/2>(rates[1]-rates[0])/2,
          "thin clock-cell radius was reduced to adjacent rate-center spacing");
    modem::PatternSearch automatic;automatic.initial_stream_symbols=1;automatic.drift_tolerant=false;
    automatic.differential_window_seconds=0;
    modem::StreamingReceiver generated(c,budget,automatic);
    const auto expected=std::optional<double>{declared.clock_step_ppm/2};
    check(generated.acquisition_state().admitted_clock_cell_half_width_ppm==expected &&
          std::equal(generated.acquisition_hypotheses().begin(),generated.acquisition_hypotheses().end(),
                     declared.hypotheses.begin(),declared.hypotheses.end()),
          "automatic original oscillator catalogue lost its timing-cell provenance");
    auto explicit_search=automatic;explicit_search.hypotheses=declared.hypotheses;
    explicit_search.admitted_clock_half_width_ppm=declared.requested_clock_half_width_ppm;
    explicit_search.admitted_clock_cell_half_width_ppm=*expected;
    modem::StreamingReceiver explicit_receiver(c,budget,explicit_search);
    const auto initial=explicit_receiver.acquisition_state();
    std::vector<std::uint8_t> full(declared.hypotheses.size(),1);
    check(explicit_receiver.set_acquisition_prior(2.17,full),"thin original catalogue could not retain full translated timing coverage");
    check(explicit_receiver.acquisition_state().admitted_clock_cell_half_width_ppm==expected &&
          explicit_receiver.acquisition_state().admitted_clock_half_width_ppm==initial.admitted_clock_half_width_ppm,
          "RF translation changed original timing-cell provenance");
    explicit_receiver.clear_acquisition_prior();explicit_receiver.trim_optional_workspace();
    check(explicit_receiver.acquisition_state().admitted_clock_cell_half_width_ppm==expected,
          "ordinary fallback or optional trimming changed original timing-cell provenance");
    explicit_search.admitted_clock_cell_half_width_ppm.reset();
    modem::StreamingReceiver unproven(c,budget,explicit_search);
    check(!unproven.acquisition_state().admitted_clock_cell_half_width_ppm,
          "explicit oscillator override invented timing-cell provenance");
    std::cout<<"original thin clock-cell pairs="<<declared.hypotheses.size()<<" rates="<<rates.size()
             <<" cell_half_width_ppm="<<*expected<<'\n';
}
void selected_parallel_preparation() {
    const auto wave=capture(false);
    const auto full=receive(wave,257,true,false,0,false,false,2);
    const auto conditioned=receive(wave,257,true,true,0,false,false,2);
    check(full.work.original_prepared_jobs_peak>0 && conditioned.work.conditional_prepared_jobs_peak>0,
          "parallel acquisition fixture prepared no measured jobs");
    check(conditioned.work.conditional_prepared_jobs_peak<full.work.original_prepared_jobs_peak,
          "conditional preparation retained the full original job matrix");
    check(conditioned.work.conditional_scoring_peak_bytes<full.work.original_scoring_peak_bytes,
          "conditional preparation did not reduce actual scoring allocation");
    check(conditioned.work.original_prepared_jobs_peak==full.work.original_prepared_jobs_peak,
          "same-window fallback did not retain original parallel preparation");
    std::cout<<"parallel prepared jobs ordinary="<<full.work.original_prepared_jobs_peak
             <<" conditional="<<conditioned.work.conditional_prepared_jobs_peak
             <<" scoring bytes ordinary="<<full.work.original_scoring_peak_bytes
             <<" conditional="<<conditioned.work.conditional_scoring_peak_bytes<<'\n';
}
void immutable_phase_curve() {
    modem::PatternPhaseCurve curve;
    check(curve.configure(1,256,0,4,32),"phase curve rejected finite configuration");
    check(!curve.configure(2,256,0,4,32),"phase curve reconfigured a live convention");
    const auto until=std::uint64_t{100000};
    const std::array first{modem::PatternPhaseSegment{128,0,0}};
    check(!curve.append(first,92,until),"phase model accepted evidence reaching the first PCM guard");
    check(curve.append(first,0,until),"phase model rejected guarded future evidence");
    curve.mark_observed(256);
    const std::array retrospective{modem::PatternPhaseSegment{288,0,.01}};
    check(!curve.append(retrospective,0,until),"phase model rewrote an observed or partial bin");
    const std::array pair{modem::PatternPhaseSegment{512,0,.01},modem::PatternPhaseSegment{768,2.56,.02}};
    auto invalid=pair;invalid[1].radians+=1;
    const auto remaining=curve.available_segments();
    check(!curve.append(invalid,0,until) && curve.available_segments()==remaining,
          "failed phase bridge partially changed immutable history");
    check(curve.append(pair,0,until),"continuous future phase bridge declined");
    const auto historical=curve.value_at(640),future=curve.value_at(1024);
    curve.close();
    check(!curve.qualified_until() && curve.value_at(640)==historical && curve.value_at(1024)==future,
          "helper loss changed accepted phase history or affine continuation");
    check(curve.requalify(300,until) && curve.value_at(640)==historical,
          "agreement qualification rewrote immutable phase weights");
    std::uint64_t next=1024;
    while(curve.available_segments()) {
        const modem::PatternPhaseSegment segment{next,curve.value_at(next),.02};
        check(curve.append(std::span(&segment,1),300,until),"bounded phase control did not fill its declared capacity");
        next+=256;
    }
    const modem::PatternPhaseSegment overflow{next,curve.value_at(next),.02};
    check(!curve.append(std::span(&overflow,1),300,until) && curve.value_at(640)==historical,
          "full phase control rewrote accepted history instead of declining optional work");
}
void phase_view_loss_and_raw_audit() {
    const auto wave=capture(true,0,true);
    modem::PatternPhaseCurve curve;
    check(curve.configure(7,wave.config.sample_rate,0,0,64),"phase fixture configuration failed");
    const auto until=static_cast<std::uint64_t>(wave.pcm.size()+1);
    const std::array first{modem::PatternPhaseSegment{256,0,0},
        modem::PatternPhaseSegment{1024,0,.25/wave.config.sample_rate}};
    const std::array tail{modem::PatternPhaseSegment{8ULL*wave.config.sample_rate,1,-.1/wave.config.sample_rate}};
    check(curve.append(first,0,until) && curve.append(tail,0,until),"phase fixture guarded model failed");
    const auto result=receive(wave,37,true,true,0,false,false,1,&curve,true);
    check(result.work.phase_view_template_jobs && result.work.phase_view_tracking_fits && result.work.phase_corrected_bins,
          "phase fixture performed no nonidentity ordinary acquisition/continuation work");
    check(result.work.original_audit_template_jobs && result.work.shifted_reference_trials,
          "phase loss skipped raw audits or helper-only trial spending");
    std::cout<<"phase-view jobs="<<result.work.phase_view_template_jobs<<" tracking fits="<<result.work.phase_view_tracking_fits
             <<" corrected bins="<<result.work.phase_corrected_bins<<" raw audits="<<result.work.original_audit_template_jobs<<'\n';
    // Merely retaining a dequalified view cannot rotate the raw fallback.
    const auto plain=capture(true);
    modem::PatternPhaseCurve closed;
    check(closed.configure(8,plain.config.sample_rate,0,0,64) && closed.append(first,0,until),
          "raw parity phase control failed");
    closed.close();
    const auto raw=receive(plain,37,true),bound=receive(plain,37,true,false,0,false,false,1,&closed);
    check(raw.events.size()==bound.events.size() && !bound.work.phase_view_template_jobs && !bound.work.phase_corrected_bins,
          "dequalified phase control changed original raw event count or observations");
    for(std::size_t i=0;i<raw.events.size();++i) {
        const auto& a=raw.events[i];const auto& b=bound.events[i];
        check(std::tie(a.bits,a.first_sample,a.end_sample,a.complete,a.score,a.frequency_hz,a.support_samples)==
              std::tie(b.bits,b.first_sample,b.end_sample,b.complete,b.score,b.frequency_hz,b.support_samples),
              "dequalified phase control changed an original raw decision");
    }
}
void phase_outer_guard() {
    const auto wave=capture(false,0,true,true);
    const auto model=[&](modem::PatternPhaseCurve& curve,std::uint64_t id) {
        const auto rate=wave.config.sample_rate;
        check(curve.configure(id,rate,0,0,64),"outer guard phase configuration failed");
        const std::array first{modem::PatternPhaseSegment{rate,0,0},
            modem::PatternPhaseSegment{4ULL*rate,0,.25/rate}};
        const std::array tail{modem::PatternPhaseSegment{8ULL*rate,1,-.1/rate}};
        const auto until=static_cast<std::uint64_t>(wave.pcm.size()+1);
        check(curve.append(first,0,until) && curve.append(tail,0,until),"outer guard phase model failed");
    };
    modem::PatternPhaseCurve correct;model(correct,9);
    // This positive ordinary receiver supplies the common-link evidence for
    // the paired cold wrong-outer-key row; neither gets expected payload bits.
    const auto good=receive(wave,37,true,true,0,false,false,1,&correct,false,true,true);
    check(good.work.phase_view_template_jobs && good.work.phase_corrected_bins && !good.work.original_audit_template_jobs,
          "outer guard positive fixture did not score through the nonidentity phase view");
    const auto guard_fits=std::count_if(good.evidence.begin(),good.evidence.end(),[](const auto& item){
        return item.score>=item.admission_threshold && item.outer_presence_score>0;
    });
    check(guard_fits>0,"phase view did not execute the independent outer-code covariance fit");
    auto wrong=wave;wrong.config.dsss_seed[0]^=0x40;
    modem::PatternPhaseCurve negative;model(negative,10);
    const auto rejected=receive(wrong,37,true,true,0,false,false,1,&negative,false,false,true);
    check(rejected.work.phase_view_template_jobs && rejected.work.phase_corrected_bins && !rejected.work.original_audit_template_jobs,
          "outer guard negative fixture silently used an original raw audit");
    check(std::none_of(rejected.events.begin(),rejected.events.end(),[](const auto& event){return !event.bits.empty() || event.complete;}),
          "phase view disabled ordinary wrong-outer-key rejection");
    // The non-quarter-rate carrier has nonzero real-PCM projection image in
    // the original six-sample native bin: phase rotates the guard gamma.
    modem::StreamingReceiver geometry(wave.config,budget,search(wave));
    const auto bin=geometry.acquisition_state().bin_samples;
    const auto omega=2*std::numbers::pi*wave.config.carrier_hz/wave.config.sample_rate;
    const auto image=std::abs(std::sin(bin*omega)/(bin*std::sin(omega)));
    check(bin>1 && image>.01,
          "outer guard phase fixture lost its nonzero projection covariance");
    std::cout<<"phase outer guard fits="<<guard_fits<<" jobs="<<rejected.work.phase_view_template_jobs
             <<" nonidentity bins="<<rejected.work.phase_corrected_bins<<" projection bin="<<bin<<" image="<<image<<'\n';
}
void observe_refresh_and_consumption(bool incompatible) {
    const auto wave=capture(true);auto s=search(wave);
    check(wave.begin==0,"long continuation fixture gained a source preamble");
    for(auto& h:s.hypotheses)h.frequency_offset_hz*=.25;
    modem::StreamingReceiver receiver(wave.config,budget,s);
    std::vector<std::uint8_t> mask(s.hypotheses.size());
    for(std::size_t f=0;f<mask.size();++f)mask[f]=s.hypotheses[f].frequency_offset_hz==0;
    check(receiver.set_acquisition_prior(0,mask),"long ordinary-link prior declined");
    unsigned batch=0,completions=0;std::size_t accepted=0;Bytes bits;
    for(std::size_t at=0;at<wave.pcm.size();) {
        const auto state=receiver.acquisition_state();
        check(state.supported,"long link refresh lost native geometry");
        if(!state.ready) {
            const auto count=std::min<std::size_t>(37,wave.pcm.size()-at);
            at+=receiver.observe_acquisition(std::span(wave.pcm).subspan(at,count));
        } else {
            // The native hop is half a payload symbol here: batch1 has no
            // second bit yet. Audit that miss before trying the next window.
            if(batch==2) {
                check(accepted>=2,"refresh fixture did not observe ordinary continuation before dispatch");
                check(receiver.set_acquisition_prior(incompatible?1:.001,mask),"atomic prior refresh declined valid geometry");
            } else check(receiver.set_acquisition_prior(0,mask),"compatible atomic prior refresh failed");
            const bool link=receiver.process_prior_acquisition();
            if(batch==0)check(link,"first ordinary long bit did not support its helper link");
            if(batch==2)check(link!=incompatible,"prior refresh lost compatible bits or inherited incompatible bits");
            if(batch==1 || batch==3)check(!link,"a consumed ordinary bit renewed a later empty batch");
            receiver.resolve_acquisition(link);
            if(!link)check(receiver.set_acquisition_prior(0,mask),"fresh-window ordinary-link prior declined");
            ++batch;
        }
        for(const auto& event:receiver.take_pattern_bursts()) {
            check(!event.missing_slots,"refresh altered ordinary unknown-slot accounting");
            bits.insert(bits.end(),event.bits.begin(),event.bits.end());accepted+=event.bits.size();
            if(event.complete) {
                ++completions;check(at>=event.end_sample+modem::pattern_absence_samples(wave.config),
                                   "refresh manufactured a physical END");
            }
        }
    }
    check(batch>=4&&bits==wave.bits&&completions==1,"atomic prior refresh changed complete ordinary long payload");
}
void source_identity_bounds() {
    static_assert(sizeof(void*)!=8 || sizeof(modem::PatternBurst)==112);
    static_assert(sizeof(modem::PatternSourceIdentity)<=40);
    modem::PatternSourceIdentity a{1800000003,1,16000,0,1900,6000},b{1800000003,0,16000,0,1900,6000};
    const auto disjoint=[&](const auto& x,std::uint64_t first_x,std::uint64_t end_x,
                            const auto& y,std::uint64_t first_y,std::uint64_t end_y) {
        return modem::pattern_source_identities_disjoint(x,first_x,end_x,1920,y,first_y,end_y,1920);
    };
    check(disjoint(a,2520,14175,b,13155,15090),"separate qualified source envelopes became ambiguous");
    check(!disjoint(a,2520,14175,a,2520,15090),"same source alignment escaped ordinary overlap veto");
    auto unknown=a;unknown.phase_upper=6000;
    check(!disjoint(unknown,2520,14175,b,13155,15090),"invalid phase cell was treated as independent");
    auto overflow=a;overflow.epoch=UINT64_MAX;
    check(!disjoint(overflow,2520,14175,b,13155,15090),"overflowing source epoch supplied overlap authority");
    auto converging=b;converging.first_symbol=20;converging.clock_half_width_ppm=900000;
    check(!disjoint(a,2520,1000000,converging,13155,1000000),
          "disjoint birth anchors concealed later overlapping source cones");
    modem::detail::NativeDiagnosticHistoryTail<int> tail(5,3);std::vector<int> history;
    for(int i=0;i<8;++i)tail.append(history,i);
    check(tail.logical_size()==5 && history==std::vector<int>({5,6,7}),"diagnostic FIFO setup changed");
    tail.checkpoint(history);
    check(!tail.rebind_materialized_limit(1),"helper discarded an active diagnostic checkpoint");
    tail.release_checkpoint();
    check(tail.rebind_materialized_limit(0),"diagnostic surrender failed");history.clear();
    for(int i=0;i<8;++i)tail.append(history,i);
    check(tail.logical_size()==5 && history.empty(),"zero materialization regrew or changed logical FIFO");
    tail.checkpoint(history);tail.append(history,9);tail.rollback(history,1);
    check(tail.logical_size()==4 && history.empty(),"zero-materialization rollback changed original FIFO occupancy");
}
void diagnostic_funding() {
    const auto wave=capture(false);auto s=search(wave);
    modem::StreamingReceiver r(wave.config,budget,s);
    const auto required=modem::detail::NativeDiagnosticHistoryAccess::requirements(r,128);
    check(required && modem::detail::NativeDiagnosticHistoryAccess::bind(r,128,required->binding_growth_bytes),
          "helper diagnostic funding could not bind ordinary128-entry tail");
    r.trim_optional_workspace();
    const auto before=r.working_bytes(),reserved=r.reserved_workspace_bytes();
    const auto initial=modem::detail::NativeDiagnosticHistoryAccess::accounting(r);
    std::vector<std::uint8_t> mask(s.hypotheses.size());
    for(std::size_t f=0;f<mask.size();++f)mask[f]=s.hypotheses[f].frequency_offset_hz==0;
    check(r.set_acquisition_prior(0,mask),"history-funded helper metadata declined valid ordinary geometry");
    const auto after=modem::detail::NativeDiagnosticHistoryAccess::accounting(r);
    check(r.working_bytes()<=before+mask.size() && r.reserved_workspace_bytes()==reserved,
          "helper metadata increased original mandatory or bank-visible persistent allocation");
    check(after.logical_size==initial.logical_size && after.limit<initial.limit &&
          after.history_capacity_bytes<initial.history_capacity_bytes && !after.checkpoint_bytes,
          "helper history compaction changed logical occupancy or retained the old heap");
    std::cout<<"diagnostic funding actual_before="<<before<<" actual_after="<<r.working_bytes()
             <<" reserved="<<reserved<<" history_before="<<initial.history_capacity_bytes
             <<" history_after="<<after.history_capacity_bytes<<" materialized_limit="<<after.limit<<'\n';
}
void helper_published_alias() {
    modem::Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=200;c.spreading_factor=32;
    c.scramble=true;c.stream_epoch=1800000003;
    c.oscillator_search=modem::OscillatorSearchConfig{{100,.5},{1,.05},3e7,3};
    for(std::size_t i=0;i<c.spreading_seed.size();++i)c.spreading_seed[i]=static_cast<std::uint8_t>(7+11*i);
    const Bytes payload{1,0,1,1,0,0,1,0};
    modem::StreamingTransmitter tx(modem::RawBits{payload},c,32*1024*1024);
    modem::ChannelConfig effects;effects.snr_db=35;effects.frequency_offset_hz=-.05;
    effects.clock_error_ppm=-300;effects.phase_noise_degrees_per_sqrt_second=0;effects.seed=5181;
    modem::SampledSimulationChannel channel(c,effects);std::array<float,137> pcm{};std::vector<float> recording;
    for(;;){const auto n=channel.read(tx,pcm);if(!n)break;recording.insert(recording.end(),pcm.begin(),pcm.begin()+n);}
    for(std::uint64_t left=10ULL*c.sample_rate;left;) {
        const auto n=std::min<std::uint64_t>(left,pcm.size());channel.read_noise(std::span(pcm).first(n));
        recording.insert(recording.end(),pcm.begin(),pcm.begin()+n);left-=n;
    }
    check(recording.size()==107970,"published-alias regression no longer uses the original actual PCM duration");
    const auto geometry=modem::oscillator_pattern_search(c);
    for(const bool assisted:{false,true}) {
        modem::PatternSearch options;options.hypotheses=geometry.hypotheses;options.worker_threads=1;
        options.admitted_clock_half_width_ppm=geometry.requested_clock_half_width_ppm;
        options.admitted_clock_cell_half_width_ppm=geometry.clock_step_ppm/2;
        options.initial_stream_symbols=2;options.chunk_bits=1;
        modem::StreamingReceiver r(c,32*1024*1024,options);
        std::vector<std::uint8_t> mask(options.hypotheses.size());std::size_t selected=0;
        for(std::size_t f=0;f<mask.size();++f)selected+=(mask[f]=std::abs(options.hypotheses[f].frequency_offset_hz)<.8);
        check(selected==3,"published-alias regression lost the original three-cell helper strip");
        if(assisted)check(r.set_acquisition_prior(0,mask),"published-alias prior was declined");
        struct Stream {Bytes bits;unsigned completions=0;bool helper=false;};
        std::map<std::pair<std::uint64_t,std::uint64_t>,Stream> streams;
        std::size_t at=0;const auto drain=[&] {
            for(const auto& b:r.take_pattern_bursts()) {
                auto& stream=streams[{b.stream_first_sample,b.stream_first_symbol}];
                stream.bits.insert(stream.bits.end(),b.bits.begin(),b.bits.end());stream.helper|=b.helper_acquired;
                if(b.complete) {
                    ++stream.completions;check(at>=b.end_sample+modem::pattern_absence_samples(c),
                                              "helper alias arbitration shortened physical END");
                }
                if(b.stream_first_sample==13155 && b.stream_first_symbol==0)
                    check(stream.bits.size()<=payload.size() && std::equal(stream.bits.begin(),stream.bits.end(),payload.begin()),
                          "helper arbitration changed a published true prefix");
            }
        };
        while(at<recording.size()) {
            if(assisted && r.acquisition_state().ready) {
                r.process_prior_acquisition();r.resolve_acquisition(true);drain();continue;
            }
            const auto n=std::min<std::size_t>(137,recording.size()-at);
            if(assisted)at+=r.observe_acquisition(std::span(recording).subspan(at,n));
            else {r.push(std::span(recording).subspan(at,n));at+=n;}
            drain();
        }
        if(assisted && r.acquisition_state().ready){r.process_prior_acquisition();r.resolve_acquisition(true);drain();}
        const auto found=streams.find({13155,0});
        check(found!=streams.end() && found->second.bits==payload && found->second.completions==1,
              "helper-published false alias still removed the true first bit");
        if(assisted) {
            const auto false_stream=streams.find({2520,1});
            check(false_stream!=streams.end() && false_stream->second.bits==Bytes({0,0,1,0,0,1}) &&
                  false_stream->second.completions==1 && false_stream->second.helper && found->second.helper,
                  "helper overlap repair retracted or merged an older published owner");
        } else check(!found->second.helper,"ordinary control acquired helper provenance");
    }
}

}
int main(){try {
    original_equivalence();conditional_work_and_fallback();original_clock_and_domain();complete_catalogue_translation();
    original_clock_cell_metadata();selected_parallel_preparation();
    source_identity_bounds();diagnostic_funding();helper_published_alias();
    observe_refresh_and_consumption(false);observe_refresh_and_consumption(true);
    immutable_phase_curve();phase_view_loss_and_raw_audit();phase_outer_guard();
    std::cout<<"ordinary split acquisition, conditional work and same-window fallback passed\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

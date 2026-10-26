#include "datapump/helper_receiver.hpp"
#include "datapump/helper_carrier.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <source_location>
#include <string_view>
using namespace datapump;
namespace {
constexpr double tau=2*std::numbers::pi;
void check(bool value,const std::source_location at=std::source_location::current()) {
    if(!value)throw Error(std::string(at.file_name())+":"+std::to_string(at.line()));
}
void pure_geometry() {
    helper::FrequencyWorkOptions options;options.sample_rate=24000;
    options.physical_prior_half_width_hz=1.7400009;options.coherent_phase_model=true;
    helper::FrequencyPrecisionOptions model;model.phase_noise_radians_per_sqrt_second=
        .0070710678118654753*std::numbers::pi/180;
    auto layout=helper::Receiver::optional_frequency_geometry(2450.3125,20,options,model);
    check(layout.supported&&!layout.eligible&&layout.decline==helper::FrequencyWorkDecline::grant_unavailable);
    check(layout.bin_samples==96&&layout.aperture_bins==2000&&layout.aperture_seconds==8.0);
    check(layout.startup_support_seconds>8.4&&layout.startup_support_seconds<8.41);
    check(layout.cold_staging_deadline_seconds==10&&layout.maximum_supported_fit_seconds==8.0);
    check(layout.maximum_support_age_seconds==1&&layout.required_bytes==helper::Receiver::optional_frequency_workspace_required());
    check(layout.heap_bytes+layout.worker_scratch_bytes==layout.required_bytes);
    options.workspace_grant=layout.required_bytes;
    check(helper::Receiver::optional_frequency_geometry(2450.3125,20,options,model).eligible);
    --options.workspace_grant;
    check(!helper::Receiver::optional_frequency_geometry(2450.3125,20,options,model).eligible);
    options.workspace_grant=layout.required_bytes;options.physical_prior_half_width_hz=20.1;
    check(!helper::Receiver::optional_frequency_geometry(2450.3125,20,options,model).supported);
    options.physical_prior_half_width_hz=1.7400009;options.sample_rate=44100;
    check(!helper::Receiver::optional_frequency_geometry(2450.3125,20,options,model).supported);
    options.sample_rate=24000;model.maximum_audio_clock_ppm=1001;
    check(!helper::Receiver::optional_frequency_geometry(2450.3125,20,options,model).supported);
    std::cout<<"PURE_GEOMETRY_PASS,total="<<layout.required_bytes<<'\n';
}
void default_and_grant() {
    for(const auto rate:{8000U,11025U,32000U,44100U,48000U}) {
        helper::Receiver baseline(2100,20),subject(2100,20);
        auto d=subject.optional_frequency_work();check(d.actual_bytes==0);
        helper::FrequencyWorkOptions o;o.sample_rate=rate;o.coherent_phase_model=true;
        o.workspace_grant=d.required_bytes-1;check(!subject.set_optional_frequency_work(o));
        o.workspace_grant=d.required_bytes;o.coherent_phase_model=false;check(!subject.set_optional_frequency_work(o));
        check(subject.optional_frequency_work().actual_bytes==0);
        o.coherent_phase_model=true;
        if(rate!=48000)check(!subject.set_optional_frequency_work(o));
        else check(subject.set_optional_frequency_work(o));
        std::array<float,137> input{};std::size_t publications=0;
        for(std::uint64_t first=0;first<rate/2;first+=input.size()) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(input.size(),rate/2-first));
            for(std::size_t i=0;i<count;++i)input[i]=static_cast<float>(.6*std::cos(tau*2100.37*(first+i)/rate+.81));
            const auto span=std::span<const float>(input).first(count);
            baseline.push(span,rate,first);subject.push(span,rate,first);
            const auto a=baseline.take_update(),b=subject.take_update();check(a.has_value()==b.has_value());
            if(a) {++publications;check(a->sample==b->sample&&a->frequency_hz==b->frequency_hz&&
                a->frequency_uncertainty_hz==b->frequency_uncertainty_hz&&a->phase_radians==b->phase_radians&&
                a->phase_observation_sample==b->phase_observation_sample&&a->frequency_first_sample==b->frequency_first_sample);}
        }
        check(publications>0);subject.release_optional_frequency_work();check(subject.optional_frequency_work().actual_bytes==0);
        std::cout<<"STRONG_DEFAULT,"<<rate<<",refs="<<publications<<'\n';
    }
}
class WeakStream {
public:
    static constexpr std::uint32_t rate=24000;
    static constexpr double expected=2450.3125,offset=.58,ppm=.0001,amplitude=.01;
    static constexpr double physical_sigma=.0070710678118654753*std::numbers::pi/180;
    helper::Options options;
    Crypto key{std::array<std::uint8_t,32>{}};
    helper::Generator generator;
    helper::Receiver receiver;
    std::mt19937_64 noise_rng{1},phase_rng{1^0xa0761d6478bd642fULL};
    std::normal_distribution<double> noise{0,std::sqrt(amplitude*amplitude*rate/(4*std::pow(10.,30.7609125906/10)))},
        phase_step{0,physical_sigma/std::sqrt(rate)};
    std::uint64_t next=0;double phase=.81;std::size_t accepted=0;double fit_seconds=0;
    WeakStream():options([]{helper::Options o;o.power_w=15000;o.data_power_w=4;o.timecode=true;return o;}()),
        generator(options,{expected,amplitude,0,15000},1800000000,true,key),
        receiver(expected,20,{key},{},helper::FrequencyPrecisionOptions{0,1000,physical_sigma}) {
        grant();
    }
    void grant() {
        helper::FrequencyWorkOptions o;o.sample_rate=rate;
        o.workspace_grant=helper::Receiver::optional_frequency_workspace_required();
        o.physical_prior_half_width_hz=1.7400009;o.coherent_phase_model=true;
        check(receiver.set_optional_frequency_work(o));
        const auto layout=helper::Receiver::optional_frequency_geometry(expected,20,o,
            helper::FrequencyPrecisionOptions{0,1000,physical_sigma});
        check(layout.eligible);fit_seconds=layout.aperture_seconds;
        const auto d=receiver.optional_frequency_work();check(d.actual_bytes==o.workspace_grant);
        check(d.heap_bytes+d.worker_scratch_bytes==d.actual_bytes);
        std::cout<<"WORKSPACE,heap="<<d.heap_bytes<<",scratch="<<d.worker_scratch_bytes<<",total="<<d.actual_bytes<<'\n';
    }
    void push_until(double seconds,bool signal=true) {
        const auto stop=static_cast<std::uint64_t>(std::llround(seconds*rate));std::array<float,137> input{};
        while(next<stop) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(input.size(),stop-next));
            for(std::size_t i=0;i<count;++i) {
                const auto time=static_cast<double>(next+i)/rate;
                const auto source=static_cast<long double>(time)*(1+ppm*1e-6)-.03719L;
                phase+=phase_step(phase_rng);
                const auto tone=!signal||source<0?0.:std::real(generator.mix_analytic({},source,rate)*
                    std::polar(1.,phase+tau*offset*time));
                input[i]=static_cast<float>(tone+noise(noise_rng));
            }
            receiver.push(std::span<const float>(input).first(count),rate,next);next+=count;
            if(const auto r=receiver.take_update())check(r->frequency_fit!=helper::FrequencyFit::masked_tone_profile);
        }
    }
    void complete(std::size_t batch=31) {
        for(;;) {
            const auto before=receiver.optional_frequency_work();
            const auto step=receiver.advance_optional_frequency(batch);
            const auto after=receiver.optional_frequency_work();
            check(step.processed_cells<=batch&&after.processed_cells>=before.processed_cells&&
                  after.processed_cells<=after.total_cells);
            check(step.processed_work_units<=after.maximum_work_units_per_advance&&
                  after.processed_work_units-before.processed_work_units==step.processed_work_units);
            if(const auto r=receiver.take_update())check(r->frequency_fit!=helper::FrequencyFit::masked_tone_profile);
            if(step.complete)break;
        }
    }
    helper::FrequencyPresenceCertificate context(helper::FrequencyPresenceState state=helper::FrequencyPresenceState::unavailable) {
        const auto d=receiver.optional_frequency_work();return {77,d.generation,next-1,state};
    }
    void consume() {
        const auto d=receiver.optional_frequency_work();const auto r=receiver.take_update();check(r.has_value());
        check(r->frequency_fit==helper::FrequencyFit::masked_tone_profile&&r->frequency_valid&&
              !r->phase_valid&&!r->timing_valid&&!r->phase_observation_known&&!r->metadata);
        check(r->sample==d.end_sample&&r->frequency_first_sample==d.first_sample&&r->sample<next);
        const auto truth=expected*(1+ppm*1e-6)+offset;
        check(std::abs(r->frequency_hz-truth)<=r->frequency_uncertainty_hz);
        check(d.recent_presence_qualified&&d.presence_snr_db>=6&&d.presence_noise_bandwidth_hz==10);
        check(d.presence_first_sample>=d.presence_interval_first_sample&&d.presence_end_sample<=d.presence_interval_end_sample&&
              d.presence_first_sample<=d.presence_end_sample&&d.presence_interval_end_sample==r->sample);
        ++accepted;std::cout<<"GLOBAL,first="<<r->frequency_first_sample<<",end="<<r->sample<<",ci="<<r->frequency_uncertainty_hz
            <<",presence_first="<<d.presence_first_sample<<",presence_end="<<d.presence_end_sample<<",snr="<<d.presence_snr_db<<'\n';
    }
};
void implicit_capture_reset() {
    for(const bool change_rate:{false,true}) {
        WeakStream s;s.push_until(s.fit_seconds);check(s.receiver.prepare_optional_frequency(77));
        const auto step=s.receiver.advance_optional_frequency(1);
        check(step.processed_cells<=1&&step.processed_work_units>0&&step.processed_work_units<=8);
        const auto frozen=s.receiver.optional_frequency_work();
        const std::array<float,1> sample{0};
        s.receiver.push(sample,change_rate?48000:WeakStream::rate,s.next+(change_rate?0:137));
        const auto invalidated=s.receiver.optional_frequency_work();
        check(invalidated.generation!=frozen.generation&&invalidated.first_sample==frozen.first_sample&&
              invalidated.end_sample==frozen.end_sample&&invalidated.processed_cells==frozen.processed_cells&&
              invalidated.processed_work_units==frozen.processed_work_units);
        s.complete();check(!s.receiver.commit_optional_frequency(s.context()));
        check(s.receiver.optional_frequency_work().decline==helper::FrequencyWorkDecline::generation_changed);
        std::cout<<"IMPLICIT_RESET,"<<(change_rate?"rate":"gap")<<",old_generation="<<frozen.generation
            <<",new_generation="<<invalidated.generation<<'\n';
    }
}
void lifecycle() {
    pure_geometry();default_and_grant();implicit_capture_reset();WeakStream s;
    s.push_until(s.fit_seconds);check(s.receiver.prepare_optional_frequency(77));
    const auto frozen=s.receiver.optional_frequency_work();check(frozen.look_ordinal==1&&frozen.opportunity_ordinal==1);
    const auto one=s.receiver.advance_optional_frequency(1);
    check(one.processed_cells<=1&&one.processed_work_units>0&&one.processed_work_units<=8&&!one.complete);
    check(!s.receiver.commit_optional_frequency(s.context()));
    s.push_until(s.fit_seconds+.4);const auto concurrent=s.receiver.optional_frequency_work();
    check(concurrent.first_sample==frozen.first_sample&&concurrent.end_sample==frozen.end_sample&&
          concurrent.look_ordinal==frozen.look_ordinal&&concurrent.opportunity_ordinal>frozen.opportunity_ordinal);
    s.complete();check(s.receiver.optional_frequency_work().state==helper::FrequencyWorkState::all_cells_complete);
    check(s.receiver.commit_optional_frequency(s.context()));s.consume();
    check(s.receiver.prepare_optional_frequency(77));s.complete();
    check(!s.receiver.commit_optional_frequency(s.context(helper::FrequencyPresenceState::absent)));
    check(s.receiver.optional_frequency_work().decline==helper::FrequencyWorkDecline::presence_unqualified);
    s.push_until(s.fit_seconds+.8);check(s.receiver.prepare_optional_frequency(77));s.complete();s.push_until(s.fit_seconds+2);
    check(!s.receiver.commit_optional_frequency(s.context()));
    check(s.receiver.optional_frequency_work().decline==helper::FrequencyWorkDecline::stale_support);
    check(s.receiver.prepare_optional_frequency(77));s.receiver.advance_optional_frequency(1);
    s.receiver.reset();s.complete();check(!s.receiver.commit_optional_frequency(s.context()));
    check(s.receiver.optional_frequency_work().decline==helper::FrequencyWorkDecline::generation_changed);
    const auto ordinal=s.receiver.optional_frequency_work().opportunity_ordinal;
    s.receiver.release_optional_frequency_work();check(s.receiver.optional_frequency_work().actual_bytes==0);
    check(s.receiver.optional_frequency_work().look_ordinal==ordinal);s.grant();
    s.push_until(2*s.fit_seconds+2.4);check(s.receiver.prepare_optional_frequency(77));s.complete();
    check(s.receiver.commit_optional_frequency(s.context()));s.consume();
    s.push_until(2*s.fit_seconds+4.4,false);check(s.receiver.prepare_optional_frequency(77));s.complete();
    // The last qualified reference is now older than one second. Its stale
    // cache must not overwrite the pending negative-prepass terminal result.
    const auto absent=s.receiver.optional_frequency_work();
    check(absent.state==helper::FrequencyWorkState::analysis_declined&&
          absent.decline==helper::FrequencyWorkDecline::presence_unqualified&&absent.processed_cells==0);
    check(!s.receiver.commit_optional_frequency(s.context()));
    check(!s.receiver.optional_frequency_work().recent_presence_qualified);
    s.receiver.clear_optional_frequency_history();check(!s.receiver.prepare_optional_frequency(77));
    check(s.receiver.optional_frequency_work().decline==helper::FrequencyWorkDecline::warming_aperture);
    std::cout<<"LIFECYCLE_PASS,accepted="<<s.accepted<<'\n';
}
}
int main(int argc,char** argv) {
    try {
        check(argc<=2);std::cout<<std::unitbuf;
        const std::string_view which=argc==2?argv[1]:"all";
        if(which=="all")lifecycle();
        else if(which=="geometry")pure_geometry();
        else if(which=="default")default_and_grant();
        else if(which=="implicit_reset")implicit_capture_reset();
        else throw Error("unknown optional frequency work test selector");
        return 0;
    }catch(const std::exception& error) {std::cerr<<"FAIL,"<<error.what()<<'\n';return 1;}
}

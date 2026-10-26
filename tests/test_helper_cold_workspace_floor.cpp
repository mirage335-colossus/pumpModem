// Private focused actual-class control. Compile this TU instead of a second
// pattern_receiver.o; link the proposed Streaming wrapper object and archive.
#include "../src/pattern_receiver.cpp"
#include "datapump/streaming_modem.hpp"
#include "datapump/transfer.hpp"
#include <iostream>

namespace datapump::modem {
struct PatternReceiverStartupGuideTestAccess {
    using State=PatternReceiver::Impl;
    static void check(bool value,const char* why){if(!value)throw Error(why);}
    static Config config(bool calendar) {
        Config c;c.pattern_symbols=true;c.constellation_bits=1;c.sample_rate=6000;
        c.carrier_hz=1500;c.bandwidth_hz=200;c.spreading_factor=32;c.scramble=true;
        c.stream_epoch=1800000000;c.receiver_strategy=ReceiverStrategy::complex_only;
        c.pulse_shaping=true;c.spreading_seed[3]=51;
        if(calendar) {
            c.sample_rate=14400;c.bandwidth_hz=360;c.spreading_factor=64;
            c.dsss=true;c.dsss_factor=10;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
            auto hops=std::make_shared<FhssPlan>();hops->channels=3;hops->dwell_seconds=.4;
            hops->shifts[0]=1000000;hops->shifts[1]=1010000;hops->shifts[2]=1033000;
            hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
            c.fake_hopping=resolve_fhss_power(*hops,c);
            check(c.fake_hopping->defer_symbols,"cold floor control lost deferred calendar");
        }
        return c;
    }
    static PatternSearch search() {
        PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=1;
        s.initial_stream_symbols=4;s.search_stream_phases=true;s.worker_threads=2;
        s.prefer_streamed_templates=true;s.release_idle_fft_scratch=true;
        s.candidate_limit=64;s.track_limit=4;s.bit_limit=4096;s.chunk_bits=1;
        s.drift_tolerant=false;s.differential_window_seconds=0;
        s.early_qualified_fft=false;s.component_qualified_search=false;
        for(const auto f:{-.5,0.,.5})for(const auto q:{-300.,0.,300.})s.hypotheses.push_back({f,q});
        return s;
    }
    static bool equal(const PatternColdWorkspaceFloor& a,const PatternColdWorkspaceFloor& b) {
        return a.workspace_argument_bytes==b.workspace_argument_bytes &&
            a.actual_workspace_bytes==b.actual_workspace_bytes && a.configured_bit_limit==b.configured_bit_limit;
    }
    static void quota_and_fill(bool calendar) {
        constexpr std::size_t grant=8*1024*1024;
        const auto c=config(calendar);const auto original=search();
        PatternReceiver r(c,grant,original);auto& s=*r.impl_;
        check(!s.fallback,"cold floor control selected fallback");
        r.trim_optional_workspace();
        const auto fixed=s.fixed_reservation,working=r.working_bytes(),reserved=r.reserved_workspace_bytes();
        const auto before=s.fft_workload?*s.fft_workload:PatternFftWork{};
        const auto floor=r.cold_workspace_floor();const auto again=r.cold_workspace_floor();
        check(floor && again && equal(*floor,*again),"cold floor was unavailable or unstable");
        check(floor->workspace_argument_bytes<=grant && floor->actual_workspace_bytes==floor->workspace_argument_bytes &&
            floor->configured_bit_limit==4096,"cold floor changed configured quota/argument convention");
        check(r.working_bytes()==working && r.reserved_workspace_bytes()==reserved && s.fixed_reservation==fixed &&
            s.sample==0 && s.trials==0 && s.search.worker_threads==original.worker_threads &&
            s.search.hypotheses==original.hypotheses && s.search.bit_limit==4096 &&
            r.fft_work().template_jobs==before.template_jobs,"pure floor query mutated decoder/layout/work");
        r.set_workspace_bytes(floor->workspace_argument_bytes);
        check(s.search.bit_limit==4096 && s.configured_bit_limit==4096 &&
            r.working_bytes()<=floor->actual_workspace_bytes,"certified floor clipped quota or actual backing");
        const std::array<float,23> partial{};
        check(r.observe_acquisition(partial)==partial.size(),"cold fill did not consume original PCM");
        const auto partial_floor=r.cold_workspace_floor();
        check(partial_floor && partial_floor->configured_bit_limit==4096 && s.search.bit_limit==4096 &&
            s.search.worker_threads==original.worker_threads && s.trials==0 && s.tracks.empty(),
            "partial-bin cold fill changed configured quota/workers or certificate eligibility");
        // Finish the original first input aperture without executing acquisition.
        const auto room=r.acquisition_state().input_room_samples;
        std::vector<float> zeros(room);
        check(r.observe_acquisition(zeros)==room && r.acquisition_state().ready,
            "floor changed original first-batch observation fence");
        check(r.cold_workspace_floor().has_value() && !r.acquiring() && s.trials==0,
            "unscored ready aperture manufactured an owner or lost cold eligibility");
        // Execution is a distinct original loan; this test makes no worker-floor
        // or throughput claim and does not execute a batch under the idle floor.
        r.set_workspace_bytes(grant);
        check(s.search.bit_limit==4096 && s.search.worker_threads==original.worker_threads,
            "ordinary execution loan did not restore original quota/worker policy");
        PatternReceiver boundary(c,grant,original);auto& b=*boundary.impl_;boundary.trim_optional_workspace();
        const auto f=boundary.cold_workspace_floor();check(f && f->workspace_argument_bytes>0,"boundary floor absent");
        bool rejected=false;try{boundary.set_workspace_bytes(f->workspace_argument_bytes-1);}catch(const Error&){rejected=true;}
        check(rejected || b.search.bit_limit<4096,"one byte below floor retained the certified quota unexpectedly");
        std::cout<<"cold floor calendar="<<calendar<<" Impl="<<sizeof(State)<<" fixed="<<fixed
                 <<" original_grant="<<grant<<" floor="<<floor->workspace_argument_bytes
                 <<" quota="<<floor->configured_bit_limit<<" worker_limit="<<original.worker_threads<<'\n';
    }
    static void closure_and_overflow() {
        PatternReceiver r(config(false),8*1024*1024,search());auto& s=*r.impl_;r.trim_optional_workspace();
        const auto expect=[&](bool absent,const char* why){check(r.cold_workspace_floor().has_value()!=absent,why);};
        expect(false,"pristine native control unavailable");
        s.tracks.emplace_back();expect(true,"provisional owner accepted as cold");s.tracks.clear();
        s.bursts.emplace_back();expect(true,"queued output accepted as cold");s.bursts.clear();
        s.completed.push_back({0,1,1500});expect(true,"published span accepted as cold");s.completed.clear();
        s.latest.bits={1};expect(true,"latest accepted prefix accepted as cold");s.latest.bits.clear();
        s.accepted_serial=1;expect(true,"historical accepted owner accepted as cold");s.accepted_serial=0;
        s.track_serial=1;expect(true,"historical provisional owner accepted as cold");s.track_serial=0;
        s.prior_pending_count=1;expect(true,"held acquisition accepted as cold");s.prior_pending_count=0;
        s.scoring.resize(1);expect(true,"live scoring owner accepted as cold");decltype(s.scoring)().swap(s.scoring);
        s.trials=1;expect(true,"scored acquisition accepted as cold");s.trials=0;
        s.early_eof_pending=true;expect(true,"pending physical scoring accepted as cold");s.early_eof_pending=false;
        PatternPhaseCurve curve;s.phase_curve=&curve;expect(true,"borrowed phase view accepted as raw cold");s.phase_curve=nullptr;
        const auto limit=s.configured_bit_limit;s.configured_bit_limit=SIZE_MAX;
        expect(true,"configured-quota overflow produced a certificate");s.configured_bit_limit=limit;
        const auto tracks=s.search.track_limit;s.search.track_limit=SIZE_MAX;
        expect(true,"bit-slot overflow produced a certificate");s.search.track_limit=tracks;
        expect(false,"refusal controls changed the baseline decoder");
        auto compact=search();compact.compact_clock_search=true;
        PatternReceiver fallback(config(false),8*1024*1024,compact);
        check(fallback.clock_windowed() && !fallback.cold_workspace_floor(),"compact backend received native floor");
    }
    static void normal_quota_observation() {
        constexpr std::size_t grant=8*1024*1024;
        auto original=search();original.track_limit=16;
        original.bit_limit=transfer::pattern_bit_limit(1024*1024);
        check(original.bit_limit==79847424,"normal content ceiling no longer matches its wire/storage contract");
        PatternReceiver r(config(false),grant,original);auto& s=*r.impl_;r.trim_optional_workspace();
        const auto effective=s.search.bit_limit,budget=s.budget;
        const auto full=r.cold_workspace_floor();const auto storage=r.cold_observation_storage();
        check(full && full->workspace_argument_bytes>grant && storage &&
              storage->actual_workspace_bytes<grant && storage->execution_workspace_argument_bytes==grant &&
              storage->configured_bit_limit==original.bit_limit && storage->effective_bit_limit==effective,
              "normal configured ceiling was allocated as idle observation storage");
        const auto room=r.acquisition_state().input_room_samples;
        std::vector<float> zeros(room);
        check(r.observe_acquisition(zeros)==room && r.acquisition_state().ready,
              "pristine observation did not stop at its original first scoring fence");
        check(r.cold_observation_storage().has_value() && s.budget==budget &&
              s.search.bit_limit==effective && s.configured_bit_limit==original.bit_limit && s.trials==0 &&
              s.search.worker_threads==original.worker_threads && r.working_bytes()<=storage->actual_workspace_bytes,
              "pristine observation changed G/quota/workers, allocated outside its bound or scored input");
        // Full original ordinary execution loan is a distinct obligation. No
        // idle bound is passed to the setter, and no clipped quota is installed.
        r.set_workspace_bytes(grant);r.process_acquisition();
        check(s.search.bit_limit==effective && s.configured_bit_limit==original.bit_limit &&
              s.budget==budget && !r.cold_observation_storage(),
              "first ordinary scored batch changed quota/G or remained pristine");
        std::cout<<"normal quota="<<original.bit_limit<<" effective="<<effective<<" originalG="<<grant
                 <<" observation="<<storage->actual_workspace_bytes<<" fullQfloor="<<full->workspace_argument_bytes<<'\n';
    }
    static void streaming_controls() {
        constexpr std::size_t grant=8*1024*1024;
        StreamingReceiver r(config(true),grant,search());r.trim_optional_workspace();
        const auto actual=r.working_bytes(),reserved=r.reserved_workspace_bytes();
        const auto work=r.fft_work();
        const auto f=r.cold_workspace_floor();const auto again=r.cold_workspace_floor();
        check(f && again && equal(*f,*again) && f->configured_bit_limit==4096 &&
            f->actual_workspace_bytes-f->workspace_argument_bytes==sizeof(StreamingReceiver),
            "Streaming floor omitted or duplicated its outer-shell convention");
        check(r.working_bytes()==actual && r.reserved_workspace_bytes()==reserved &&
            r.fft_work().threshold_trials==work.threshold_trials,"Streaming query mutated retained decoder");
        r.set_workspace_bytes(f->workspace_argument_bytes);
        check(r.working_bytes()<=f->actual_workspace_bytes,"Streaming setter rejected its certified argument");
        const std::array<float,11> input{};check(r.observe_acquisition(input)==input.size(),"Streaming cold floor lost PCM");
        check(r.cold_workspace_floor().has_value(),"Streaming partial fill lost native cold certificate");
    }
};
}
int main(){try {
    using T=datapump::modem::PatternReceiverStartupGuideTestAccess;
    T::quota_and_fill(false);T::quota_and_fill(true);T::closure_and_overflow();T::normal_quota_observation();T::streaming_controls();
    std::cout<<"cold configured-quota/native closure controls passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

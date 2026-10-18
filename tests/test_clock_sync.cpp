#include "datapump/clock_sync.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/utc_timing.hpp"
#include "datapump/resampler.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include <cmath>
#include <numbers>
#include <iostream>
#include <iomanip>
#include <stdexcept>
#include <source_location>
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F function,std::source_location where=std::source_location::current()) {
    try {function();}catch(const datapump::Error&){return;}
    throw std::runtime_error("unsafe timing accepted at "+std::string(where.file_name())+":"+
        std::to_string(where.line())+" in "+where.function_name());
}
void declared_clock_timing() {
    using namespace datapump::audio;
    constexpr auto mode=UtcTimeline::Prediction::timestamp_intersection;
    constexpr long double epoch=1800000000.L;
    constexpr double shared=.0001;
    for(const auto a:{0.,1e-10,.0001,.0003,.01}) {
        const ClockTimingPolicy policy{a,.001};
        const auto [lo,hi]=policy.seconds_per_nominal_second_bounds();
        require(lo<=1/(1.L+a) && hi>=1/(1.L-a),"declared clock inversion lost an endpoint");
        require(lo<=1 && hi>=1,"declared clock interval omits nominal rate");
        if(a==0)require(lo==1 && hi==1,"exact declared clock acquired fictional drift");
    }
    for(const auto a:{-1.,1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&]{ClockTimingPolicy{a,.001}.validate();});
    for(const auto slew:{-1.,.0011,std::numeric_limits<double>::infinity()})
        rejects([&]{ClockTimingPolicy{.0001,slew}.validate();});

    // Whole null frames are not a generic +/-2000 ppm open-loop reservation.
    // These are conditional policy bounds, not measured sound-card accuracy.
    UtcTimeline generic(48000,.001,mode,shared,true);
    UtcTimeline crystal(48000,.001,mode,shared,true,ClockTimingPolicy{.0001,.001});
    UtcTimeline disciplined(48000,.001,mode,shared,true,ClockTimingPolicy{1e-10,.001});
    const FrameTimestamp anchor{0,epoch,0,shared,0,TimingQuality::estimated,3};
    generic.observe(anchor);crystal.observe(anchor);disciplined.observe(anchor);
    for(const auto seconds:{40.L,80.L,400.L,4000.L}) {
        const auto old=generic.predict(seconds*48000),c=crystal.predict(seconds*48000),g=disciplined.predict(seconds*48000);
        require(!old.declared_clock_policy && c.declared_clock_policy && g.declared_clock_policy,
            "prediction does not expose declared versus generic clock coverage");
        require(c.uncertainty_seconds<old.uncertainty_seconds/15 &&
                g.uncertainty_seconds<c.uncertainty_seconds/15,
            "declared clock forecast retains generic long-frame pessimism");
        require(c.clock_extrapolation_seconds>=static_cast<double>(seconds*.0001L) &&
                c.clock_extrapolation_seconds<static_cast<double>(seconds*.000101L),
            "declared clock extrapolation contribution incorrect");
        require(c.quality==TimingQuality::estimated && g.quality==TimingQuality::estimated,
            "declared oscillator upgraded estimated provider metrology");
        const auto fresh=crystal.predict_without_history(seconds*48000);
        require(fresh.declared_clock_policy && fresh.uncertainty_seconds==c.uncertainty_seconds,
            "fresh preparation discarded declared clock policy");
    }

    // Cover signed constant offsets, query jitter, fractional frames, ring
    // wrapping, and backward/forward prediction at realistic absolute epochs.
    for(const auto fs:{8000U,48000U,192000U})for(const auto a:{0.,1e-10,.0001,.0003})
        for(const auto side:{-1.L,1.L}) {
            const ClockTimingPolicy policy{a,.001};
            const auto offset=side*a;
            const auto q=1/(1+offset);
            UtcTimeline timeline(fs,.001,mode,shared,true,policy);
            long double last_frame=0,last_elapsed=0;
            for(unsigned i=0;i<160;++i) {
                const auto elapsed=static_cast<long double>(i)*.075L;
                const auto jitter=static_cast<long double>(static_cast<int>(i%5)-2)*.000025L;
                const auto frame=fs*(1+offset)*elapsed;
                timeline.observe({frame,epoch+elapsed+jitter,elapsed,shared+static_cast<double>(std::abs(jitter)),
                    .0001,TimingQuality::estimated,2});
                last_frame=frame;last_elapsed=elapsed;
            }
            for(const auto horizon:{-.025L,.05L,40.L}) {
                const auto frame=last_frame+horizon*fs;
                const auto prediction=timeline.predict(frame);
                const auto truth=epoch+last_elapsed+horizon*q;
                require(std::abs(prediction.utc_seconds-truth)<=prediction.uncertainty_seconds,
                    "declared clock forecast omitted a valid constant-rate trajectory");
                const auto returned_q=static_cast<long double>(prediction.seconds_per_frame)*fs;
                const auto [lo,hi]=policy.seconds_per_nominal_second_bounds();
                require(std::abs(returned_q-lo)<=prediction.rate_uncertainty_fraction &&
                        std::abs(returned_q-hi)<=prediction.rate_uncertainty_fraction,
                    "declared rate metadata omits an admitted endpoint");
            }
        }

    // A good fit cannot turn a physical range into a claim of no future wander.
    // Integrate a bounded, smoothly varying physical rate, then take the other
    // endpoint for an unobserved future interval.
    UtcTimeline wander(48000,.001,mode,shared,true,ClockTimingPolicy{.0001,.001});
    long double frames=0,elapsed=0;
    wander.observe({frames,epoch,elapsed,shared,0,TimingQuality::bounded,1});
    for(unsigned i=1;i<=180;++i) {
        const auto offset=.0001L*std::sin(static_cast<long double>(i)*.05L);
        frames+=48000*.1L*(1+offset);elapsed+=.1L;
        wander.observe({frames,epoch+elapsed,elapsed,shared,0,TimingQuality::bounded,1});
    }
    for(const auto future_offset:{-.0001L,.0001L}) {
        const auto prediction=wander.predict(frames+48000*40);
        const auto truth=epoch+elapsed+40/(1+future_offset);
        require(std::abs(prediction.utc_seconds-truth)<=prediction.uncertainty_seconds,
            "recent rate fit averaged away allowed future wander");
    }

    // Wall-versus-steady slew has its own gate. It does not enlarge the sample
    // envelope when dated samples themselves retain the exact admitted UTC rate.
    UtcTimeline slew(48000,.001,mode,shared,true,ClockTimingPolicy{0,.001});
    for(unsigned i=0;i<=100;++i) {
        const auto steady=static_cast<long double>(i)*.1L;
        const auto utc=steady*1.0005L;
        slew.observe({utc*48000,epoch+utc,steady,shared,0,TimingQuality::estimated,7});
    }
    const auto before=slew.predict(48000*10.055L);
    require(before.clock_extrapolation_seconds==0,"system slew became permanent sample drift");
    rejects([&]{slew.observe({48000*10.105L,epoch+10.115L,10.1L,shared,0,TimingQuality::estimated,7});});
    require(slew.predict(48000*10.055L).utc_seconds==before.utc_seconds,
        "rejected wall-clock step mutated admitted timeline");

    // A selected GPSDO envelope cannot conceal a contradictory sound card.
    UtcTimeline contradiction(48000,.001,mode,.000001,true,ClockTimingPolicy{1e-10,.001});
    contradiction.observe({0,epoch,0,.000001,0,TimingQuality::estimated,1});
    rejects([&]{contradiction.observe({48000*100*1.0001L,epoch+100,100,.000001,0,TimingQuality::estimated,1});});
    // Adjacent observations can individually fit while the ORIGINAL interval
    // cannot. Preserve that constraint beyond the ring; never restart budgets.
    UtcTimeline accumulated(48000,.001,mode,.001,true,ClockTimingPolicy{0,.001});
    accumulated.observe({0,epoch,0,.001,0,TimingQuality::bounded,1});
    accumulated.observe({48000,epoch+1.0015L,1.0015L,.001,0,TimingQuality::bounded,1});
    rejects([&]{accumulated.observe({96000,epoch+2.003L,2.003L,.001,0,TimingQuality::bounded,1});});
    UtcTimeline fitted(48000,.001,UtcTimeline::Prediction::fitted,std::nullopt,false,ClockTimingPolicy{.0001,.001});
    fitted.observe({0,epoch,0,shared,0,TimingQuality::estimated,1});
    fitted.observe({48000*1.0001L,epoch+1,1,shared,0,TimingQuality::estimated,1});
    const auto prediction=fitted.predict(48000*41.0001L);
    const auto opposite_truth=epoch+1+40/(1-.0001L);
    require(std::abs(prediction.utc_seconds-opposite_truth)<=prediction.uncertainty_seconds,
        "fitted declared policy omitted opposite future rate endpoint");
}

void physical_waveform_rate() {
    using namespace datapump::audio;
    // Calibration can remove a physical hardware offset without permitting
    // that correction magnitude as residual error in the emitted waveform.
    for(const auto correction:{-.0003,-.0001,0.,.0001,.0003}) {
        const auto q=1.L+correction;
        const auto residual=physical_waveform_rate_interval(correction,q,q);
        require(residual.lower<=0 && residual.upper>=0 &&
                residual.upper-residual.lower<1e-15,
            "calibration correction confused with residual waveform rate");
        const auto raw=physical_waveform_rate_interval(0,q,q);
        const auto truth=1/q-1;
        require(raw.lower<=truth && raw.upper>=truth,"waveform-rate endpoint lost on conversion");
    }
    const ClockTimingPolicy policy{.0001,.001};
    const auto [lo,hi]=policy.seconds_per_nominal_second_bounds();
    for(const auto correction:{-.0001,0.,.0001}) {
        const auto residual=physical_waveform_rate_interval(correction,lo,hi);
        for(const auto q:{lo,(lo+hi)/2,hi}) {
            const auto truth=(1.L+correction)/q-1;
            require(residual.lower<=truth && residual.upper>=truth,"physical waveform interval is not outward rounded");
        }
    }
    rejects([]{physical_waveform_rate_interval(-1,1,1);});
    rejects([]{physical_waveform_rate_interval(0,0,1);});
    rejects([]{physical_waveform_rate_interval(0,2,1);});
    rejects([]{physical_waveform_rate_interval(std::numeric_limits<double>::infinity(),1,1);});
    rejects([]{physical_waveform_rate_interval(0,1,std::numeric_limits<long double>::infinity());});
}
void bounded_system_utc_phase() {
    using namespace datapump::audio;
    constexpr long double epoch=1800000000.L;
    constexpr double phase=.010,raw=.0002;
    // Two additions at this absolute epoch can round by one ULP in total.
    // Declare that synthetic clock-read error without changing raw Ui or C.
    const auto clock_read_roundoff=static_cast<double>(
        std::nextafter(epoch,std::numeric_limits<long double>::infinity())-epoch);
    for(const auto invalid:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&]{ClockTimingPolicy{0,.001,invalid}.validate();});
    // True sample presentation is exact. The host clock moves from -C to +C,
    // with either an independent steady clock or Linux's jointly slewed wall
    // and monotonic clocks. Neither trajectory is a physical DAC frequency error.
    for(const auto mode:{UtcTimeline::Prediction::fitted,UtcTimeline::Prediction::timestamp_intersection})
        for(const auto joint_slew:{false,true}) {
            UtcTimeline timeline(48000,.001,mode,
                mode==UtcTimeline::Prediction::timestamp_intersection?std::optional<double>(raw):std::nullopt,
                mode==UtcTimeline::Prediction::timestamp_intersection,ClockTimingPolicy{0,.001,phase});
            for(unsigned i=0;i<=200;++i) {
                const auto true_time=static_cast<long double>(i)*.1L;
                const auto host_phase=-static_cast<long double>(phase)+2*phase*i/200;
                timeline.observe({48000*true_time,epoch+true_time+host_phase,
                    true_time+(joint_slew?host_phase:0),raw,clock_read_roundoff,TimingQuality::estimated,4});
                const auto prediction=timeline.predict(48000*(true_time+.05L));
                const auto truth=epoch+true_time+.05L;
                require(std::abs(prediction.utc_seconds-truth)<=prediction.uncertainty_seconds,
                    "bounded system UTC phase rejected an exact physical sample clock");
                require(prediction.system_clock_phase_uncertainty_seconds==phase &&
                        prediction.provider_uncertainty_floor_seconds==raw,
                    "UTC phase/provider uncertainty decomposition missing");
                require(prediction.clock_extrapolation_seconds==0 && prediction.rate_uncertainty_fraction<1e-12,
                    "bounded system UTC phase became permanent DAC rate uncertainty");
                const auto seen=timeline.observation_uncertainty();
                require(seen.latest==raw && seen.maximum==raw,
                    "system UTC phase silently changed retained raw timestamp radii");
            }
            const auto last=timeline.predict(48000*20.05L);
            if(mode==UtcTimeline::Prediction::timestamp_intersection) {
                // Exact-rate observations at phases -C and +C intersect at Ui.
                // Their rounded absolute endpoints contribute at most half an
                // epoch ULP. Prediction expands the interval and its returned
                // radius separately; retain both explicit roundoff charges.
                constexpr auto epsilon=std::numeric_limits<long double>::epsilon();
                const auto final_utc=epoch+20.L+static_cast<long double>(phase);
                const auto final_frame=48000*20.05L;
                const auto prediction_roundoff=32*epsilon*
                    (std::abs(final_utc)+std::abs(final_frame)/48000+1);
                // Bound the 201 relative-coordinate updates: 32 units cover
                // the propagation charge and fewer than 32 scalar roundings
                // lie on each endpoint path. Other intersections only narrow it.
                constexpr auto operations=64.L*201;
                const auto relative_roundoff=operations*epsilon/(1-operations*epsilon)*
                    (4*(20.05L+1)+2);
                require(relative_roundoff<clock_read_roundoff/2,
                    "UTC phase fixture exceeds its relative-coordinate rounding budget");
                const auto return_roundoff=2*std::numeric_limits<double>::epsilon()*
                    (static_cast<long double>(raw)+phase);
                const auto arithmetic_budget=2*prediction_roundoff+clock_read_roundoff+return_roundoff;
                require(last.uncertainty_seconds<static_cast<long double>(raw)+arithmetic_budget,
                    "UTC phase intersection exceeds endpoint arithmetic bound");
                require(last.uncertainty_seconds<raw+phase,
                    "UTC phase intersection unnecessarily restores a full C floor");
            }
            // Both modes retain the original phase constraints after >64
            // observations. An endpoint outside the complete +/-C corridor is
            // not accepted just because each adjacent difference is small.
            rejects([&]{timeline.observe({48000*20.1L,epoch+20.1L+2*phase,
                20.1L+(joint_slew?2*phase:0),raw,0,TimingQuality::estimated,4});});
            require(timeline.predict(48000*20.05L).utc_seconds==last.utc_seconds,
                "rejected accumulated phase contradiction changed retained evidence");
        }

    // The phase envelope is bounded rather than a one-time monotonic slew.
    // Repeat within it for many history wraps; retain independent query jitter.
    UtcTimeline cycle(8000,.001,UtcTimeline::Prediction::timestamp_intersection,raw,true,
        ClockTimingPolicy{.0001,.001,phase});
    for(unsigned i=0;i<600;++i) {
        const auto true_time=static_cast<long double>(i)*.1L;
        const auto host_phase=phase*std::sin(true_time*.05L);
        const auto query_error=(static_cast<int>(i%3)-1)*.00005L;
        const auto radius=raw+static_cast<double>(std::abs(query_error));
        cycle.observe({8000*true_time*1.00005L,epoch+true_time+host_phase+query_error,
            true_time+host_phase,radius,.0001,TimingQuality::bounded,7});
        const auto prediction=cycle.predict(8000*(true_time+.1L)*1.00005L);
        require(std::abs(prediction.utc_seconds-(epoch+true_time+.1L))<=prediction.uncertainty_seconds,
            "clock wander/UTC phase/query jitter lost a valid true-UTC endpoint");
        require(prediction.rate_uncertainty_fraction<.0003,
            "system phase or query jitter permanently widened physical sample rate");
    }
    UtcTimeline step(48000,.001,UtcTimeline::Prediction::timestamp_intersection,raw,true,
        ClockTimingPolicy{0,.001,phase});
    step.observe({0,epoch,0,raw,0,TimingQuality::estimated,1});
    rejects([&]{step.observe({4800,epoch+.1L+.04L,.1L,raw,0,TimingQuality::estimated,1});});
    // The read-roundoff allowance cannot conceal a real 100 us clock step.
    // Raw Ui admits this displacement; the zero-slew wall/steady gate rejects it.
    for(const auto read_radius:{0.,clock_read_roundoff}) {
        UtcTimeline local(48000,.001,UtcTimeline::Prediction::timestamp_intersection,raw,true,
            ClockTimingPolicy{0,0,0});
        local.observe({0,epoch,0,raw,read_radius,TimingQuality::estimated,1});
        local.observe({48000,epoch+1,1,raw,read_radius,TimingQuality::estimated,1});
        const auto before=local.predict(96000);
        rejects([&]{
            try {local.observe({96000,epoch+2+.0001L,2,raw,read_radius,TimingQuality::estimated,1});}
            catch(const datapump::Error& error) {
                require(std::string(error.what())=="system clock stepped during UTC audio",
                    "small clock step was not rejected by the wall/steady continuity gate");
                throw;
            }
        });
        const auto after=local.predict(96000);
        require(after.utc_seconds==before.utc_seconds && after.uncertainty_seconds==before.uncertainty_seconds,
            "rejected small clock step changed retained UTC evidence");
        local.observe({96000,epoch+2,2,raw,read_radius,TimingQuality::estimated,1});
    }
    // A GPS phase allowance is not permission to conceal a sudden wall-clock
    // step or a displaced frame counter. Keep the trace worker's exact 10 ms,
    // 480-frame counterexamples: both were previously absorbed by C=10 ms.
    for(const auto frame_displacement:{false,true}) {
        UtcTimeline local(48000,.001,UtcTimeline::Prediction::timestamp_intersection,.0001,true,
            ClockTimingPolicy{0,.001,phase});
        local.observe({0,epoch,0,.0001,0,TimingQuality::estimated,1});
        rejects([&]{local.observe({48000*(frame_displacement?.21L:.2L),
            epoch+(frame_displacement?.2L:.21L),.2L,.0001,0,TimingQuality::estimated,1});});
        const auto unchanged=local.predict(0);
        require(std::abs(unchanged.utc_seconds-epoch)<=unchanged.uncertainty_seconds,
            "rejected local continuity event mutated original UTC evidence");
    }
    // A declared C=0 physical rate also remains an ORIGINAL stream constraint
    // when the caller omits the historical persistent-observations flag.
    // Each adjacent observation and the final 40.1-second jump fit the rawUi
    // envelope, but the full 80.1-second four-ppm discrepancy does not.
    // Its 120.4-us contradiction beyond 2*Ui also exceeds the explicit
    // large-epoch arithmetic allowance on binary53 long double.
    UtcTimeline original(48000,.001,UtcTimeline::Prediction::timestamp_intersection,.0001,false,
        ClockTimingPolicy{1e-10,.001,0});
    for(unsigned i=0;i<=400;++i) {
        const auto time=.1L*i;
        original.observe({48000*time*1.000004L,epoch+time,time,.0001,0,TimingQuality::estimated,8});
    }
    const FrameTimestamp future{48000*80.1L*1.000004L,epoch+80.1L,80.1L,.0001,0,TimingQuality::estimated,8};
    const FrameTimestamp previous{48000*40.L*1.000004L,epoch+40.L,40.L,.0001,0,TimingQuality::estimated,8};
    require(std::abs((future.frame-previous.frame)/48000-(future.utc_seconds-previous.utc_seconds))<2*.0001L,
        "history fixture final adjacent pair is already contradictory");
    const auto minimum_q=ClockTimingPolicy{1e-10,.001,0}.seconds_per_nominal_second_bounds().first;
    const auto original_gap=future.frame/48000*minimum_q-(future.utc_seconds-epoch)-2*.0001L;
    const auto numeric_guard=64*std::numeric_limits<long double>::epsilon()*(epoch+80.1L+1)+
        (std::nextafter(epoch,std::numeric_limits<long double>::infinity())-epoch);
    require(original_gap>numeric_guard,"history fixture contradiction is below numerical resolution");
    UtcTimeline adjacent(48000,.001,UtcTimeline::Prediction::timestamp_intersection,.0001,false,
        ClockTimingPolicy{1e-10,.001,0});
    adjacent.observe(previous);
    adjacent.observe(future);
    bool original_rejected=false;
    try {original.observe(future);}
    catch(const datapump::Error& error) {
        require(std::string(error.what())=="audio UTC observations contradict the clock-rate envelope",
            "original-history fixture failed for an unrelated timing reason");
        original_rejected=true;
    }
    require(original_rejected,"original UTC history accepted a contradictory physical clock");

    // Existing two-field policy initializers retain C=0 exactly, including
    // fitted diagnostics and the historical generic fallback.
    for(const auto mode:{UtcTimeline::Prediction::fitted,UtcTimeline::Prediction::timestamp_intersection}) {
        UtcTimeline implicit(48000,.001,mode,std::nullopt,false,ClockTimingPolicy{.0001,.001});
        UtcTimeline explicit_zero(48000,.001,mode,std::nullopt,false,ClockTimingPolicy{.0001,.001,0});
        for(unsigned i=0;i<100;++i) {
            FrameTimestamp value{4800.L*i,epoch+.1L*i,.1L*i,raw,0,TimingQuality::bounded,1};
            implicit.observe(value);explicit_zero.observe(value);
        }
        const auto a=implicit.predict(48000*10.3L),b=explicit_zero.predict(48000*10.3L);
        require(a.utc_seconds==b.utc_seconds && a.uncertainty_seconds==b.uncertainty_seconds &&
                a.seconds_per_frame==b.seconds_per_frame && a.rate_uncertainty_fraction==b.rate_uncertainty_fraction &&
                a.system_clock_phase_uncertainty_seconds==0 && b.system_clock_phase_uncertainty_seconds==0,
            "zero phase policy changed existing prediction arithmetic");
    }
}
void native_timestamp_intervals() {
    using namespace datapump::audio;
    constexpr auto mode=UtcTimeline::Prediction::timestamp_intersection;
    constexpr long double epoch=1800000000.L;
    constexpr double radius=.0130208333333333;
    // Exact reproduction: valid +10,+10,-10 ms pointer errors on a steady
    // clock inflate the old fitted radius beyond the 25 ms half-width of the
    // operator's causal 0..50 ms lag interval. Keep the old failure as evidence.
    UtcTimeline old(48000),native(48000,.001,mode);
    for(const auto [frame,time]:{std::pair{4320.L,.1L},std::pair{6720.L,.15L},std::pair{10080.L,.2L}}) {
        const FrameTimestamp stamp{frame,epoch+time,time,radius,1e-6,TimingQuality::estimated,7};
        old.observe(stamp);native.observe(stamp);
    }
    const auto desired=epoch+10080.L/48000;
    rejects([&]{(void)presentation_correction(old.predict(10080),desired,48000,.025);});
    const auto prediction=native.predict(10080);
    const auto corrected=presentation_correction(prediction,desired,48000,.025);
    require(std::abs(corrected.error_seconds)+prediction.uncertainty_seconds<.016,
        "valid native pointer intervals still inflate the UTC error");
    require(prediction.uncertainty_seconds>=radius && prediction.quality==TimingQuality::estimated,
        "native timestamp model erased provider uncertainty or upgraded quality");
    std::cout<<"Native UTC counterexample: old radius_ms="<<old.predict(10080).uncertainty_seconds*1000
        <<" new radius_ms="<<prediction.uncertainty_seconds*1000
        <<" center_error_ms="<<corrected.error_seconds*1000<<'\n';
    // Includes common codec offset (never averages down), fractional frames,
    // long epoch origins, clock envelope edges, arbitrary block cadence,
    // extrapolation, and multiple wraps of the bounded observation history.
    for(const auto fs:{8000U,48000U,192000U})for(const double ppm:{-2000.,-1000.,0.,1000.,2000.})
        for(const long double offset:{-.002L,0.L,.002L}) {
            UtcTimeline timeline(fs,.001,mode);long double elapsed=0;
            for(unsigned i=0;i<256;++i) {
                elapsed+=.05L+.003L*(i%5);
                const auto jitter=i%3==2?-.010L:.010L;
                const auto frame=elapsed*fs*(1-ppm*1e-6L)+jitter*fs+.375L;
                timeline.observe({frame,epoch+elapsed+offset,elapsed+offset,radius,1e-6,TimingQuality::estimated,12});
                for(const long double horizon:{-.025L,0.L,.05L,.25L}) {
                    const auto target=frame+horizon*fs;
                    const auto actual_scale=1/(1-ppm*1e-6L);
                    const auto actual=epoch+target*actual_scale/fs;
                    const auto p=timeline.predict(target);
                    require(std::abs(p.utc_seconds-actual)<=p.uncertainty_seconds,
                        "native prediction omitted an admitted timing corner");
                    require(p.uncertainty_seconds>=radius+.002*std::abs(horizon),
                        "native prediction lost provider floor or extrapolation allowance");
                    require(std::abs(p.seconds_per_frame*fs-actual_scale)<=p.rate_uncertainty_fraction+1e-15,
                        "native rate metadata lost an admitted clock envelope edge");
                }
            }
        }
    UtcTimeline contradiction(48000,.001,mode);
    // Each adjacent pair passes the existing continuity gate. Collectively
    // they cannot describe a clock in the admitted envelope.
    for(unsigned i=0;i<4;++i)contradiction.observe({4800.L+i*4896.L,epoch+.1L+i*.1L,
        .1L+i*.1L,.001,1e-6,TimingQuality::estimated,8});
    rejects([&]{(void)contradiction.predict(19488);});
    rejects([&]{native.observe({12480,epoch+.3L,.25L,radius,1e-6,TimingQuality::estimated,7});});
    try {
        (void)presentation_correction({epoch+.04L,.014,1./48000,TimingQuality::estimated},epoch,48000,.025);
        throw std::runtime_error("unsafe native playback accepted");
    } catch(const datapump::Error& e) {
        const std::string message=e.what();
        require(message.find("center error=")!=std::string::npos && message.find("timestamp radius=")!=std::string::npos &&
            message.find("allowed radius=25.000000 ms")!=std::string::npos,"UTC failure omitted numeric timing evidence");
    }
}
void native_query_uncertainty() {
    using namespace datapump::audio;
    constexpr auto mode=UtcTimeline::Prediction::timestamp_intersection;
    constexpr long double epoch=1800000000.L;
    constexpr double shared=.014375; // 8 kHz, 10 ms period, codec/query floor, one frame
    constexpr double query=.003440737362; // isolated slow query consistent with the user report
    constexpr long double center_error=.008709924L;
    UtcTimeline previous(8000,.001,mode),native(8000,.001,mode,shared);
    require(native.observation_uncertainty().maximum==0,"empty timeline invented query uncertainty");
    for(unsigned i=1;i<=16;++i) {
        const auto t=i*.05L;
        const auto bracket=i==3?query:0.;
        const FrameTimestamp observation{t*8000,epoch+t+center_error,t,shared+bracket/2,
            bracket+1e-6,TimingQuality::estimated,9};
        previous.observe(observation);native.observe(observation);
    }
    const auto frame=.8L*8000+817; // user's 102.125 ms prediction horizon
    const auto desired=epoch+frame/8000;
    const auto before=previous.predict(frame),after=native.predict(frame);
    rejects([&]{(void)presentation_correction(before,desired,8000,.025);});
    const auto correction=presentation_correction(after,desired,8000,.025);
    const auto epoch_ulp=std::nextafter(epoch,std::numeric_limits<long double>::infinity())-epoch;
    const auto arithmetic_roundoff=32*std::numeric_limits<long double>::epsilon()*
        (std::abs(epoch+.8L+center_error)+std::abs(frame)/8000+1);
    std::cout<<std::setprecision(std::numeric_limits<long double>::max_digits10);
    std::cout<<"Native query counterexample: previous radius_ms="<<before.uncertainty_seconds*1000
        <<" new radius_ms="<<after.uncertainty_seconds*1000
        <<" center_error_ms="<<correction.error_seconds*1000
        <<" previous_total_ms="<<(std::abs(before.utc_seconds-desired)+before.uncertainty_seconds)*1000
        <<" new_total_ms="<<(std::abs(after.utc_seconds-desired)+after.uncertainty_seconds)*1000<<'\n';
    std::cout<<"Native query arithmetic: epoch_ulp="<<epoch_ulp<<" R="<<arithmetic_roundoff
        <<" shared_floor="<<after.provider_uncertainty_floor_seconds
        <<" extrapolation="<<after.clock_extrapolation_seconds<<" phase="<<after.system_clock_phase_uncertainty_seconds
        <<" estimated="<<(after.quality==TimingQuality::estimated)
        <<" aggregate_below_original_cap="<<(std::abs(correction.error_seconds)+after.uncertainty_seconds<.0233)<<'\n';
    require(std::abs(correction.error_seconds)+after.uncertainty_seconds<.0233 &&
        after.uncertainty_seconds>=shared && after.quality==TimingQuality::estimated,
        "isolated slow query persisted as a shared native clock error");
    require(std::abs(after.utc_seconds-desired)<=after.uncertainty_seconds,
        "native interval omitted the compatible physical clock");
    const auto raw=native.observation_uncertainty();
    require(raw.latest==shared && raw.maximum==shared+query/2,
        "query diagnostic lost complete observation uncertainties");
    // No better evidence: the full interval, not just its shared floor, must
    // determine the bound. The decomposition must not force unsafe acceptance.
    UtcTimeline broad(8000,.001,mode,shared);
    for(unsigned i=1;i<=16;++i)broad.observe({i*400.L,epoch+i*.05L,i*.05L,.030,.031251,
        TimingQuality::estimated,9});
    require(broad.predict(6400).uncertainty_seconds>=.030,"wide queries were discarded from the intersection");
    rejects([&]{(void)presentation_correction(broad.predict(6400),epoch+.8L,8000,.025);});
    // Shared offsets do not average down. Include rate edges, fractional
    // frames, both prediction directions and several observation-ring wraps.
    for(const auto fs:{8000U,48000U,192000U})for(const double ppm:{-2000.,0.,2000.})
        for(const long double offset:{-static_cast<long double>(shared),static_cast<long double>(shared)}) {
            UtcTimeline timeline(fs,.001,mode,shared);
            for(unsigned i=1;i<=150;++i) {
                const auto t=i*.05L,position=t*fs*(1-ppm*1e-6L)+.375L;
                const auto bracket=i%13==0?query:0.;
                timeline.observe({position,epoch+t+offset,t,shared+bracket/2,
                    bracket+1e-6,TimingQuality::estimated,9});
                for(const long double horizon:{-.025L,0.L,.102125L,.25L}) {
                    const auto target=position+horizon*fs;
                    const auto truth=epoch+(target-.375L)/fs/(1-ppm*1e-6L);
                    const auto p=timeline.predict(target);
                    require(std::abs(p.utc_seconds-truth)<=p.uncertainty_seconds,
                        "split native uncertainty omitted common offset or rate corner");
                    require(p.uncertainty_seconds>=shared+.002*std::abs(horizon),
                        "split native uncertainty lost its shared provider floor");
                }
            }
        }
    rejects([&]{UtcTimeline invalid(8000,.001,mode,-1.);});
    rejects([&]{UtcTimeline invalid(8000,.001,mode,std::numeric_limits<double>::quiet_NaN());});
    rejects([&]{UtcTimeline invalid(8000,.001,mode,std::numeric_limits<double>::infinity());});
    rejects([&]{UtcTimeline invalid(8000,.001,UtcTimeline::Prediction::fitted,shared);});
    UtcTimeline missing(8000,.001,mode,shared);
    rejects([&]{missing.observe({400,epoch+.05L,.05L,shared-1e-6,1e-6,TimingQuality::estimated,9});});
    require(!missing.ready(),"invalid provider observation mutated empty timing state");
    rejects([&]{native.observe({6800,epoch+.85L+center_error,.85L,shared-1e-6,1e-6,TimingQuality::estimated,9});});
    const auto unchanged=native.predict(frame);
    require(unchanged.utc_seconds==after.utc_seconds && unchanged.uncertainty_seconds==after.uncertainty_seconds,
        "invalid provider observation changed an established timing prediction");
    for(unsigned i=17;i<=67;++i)native.observe({i*400.L,epoch+i*.05L+center_error,i*.05L,
        shared,1e-6,TimingQuality::estimated,9});
    require(native.observation_uncertainty().maximum==shared,
        "query diagnostic retained an evicted observation");
    UtcTimeline contradiction(48000,.001,mode,.001);
    for(unsigned i=0;i<4;++i)contradiction.observe({4800.L+i*4896.L,epoch+.1L+i*.1L,
        .1L+i*.1L,.001,1e-6,TimingQuality::estimated,8});
    rejects([&]{(void)contradiction.predict(19488);});
}
void native_frozen_source_timing() {
    using namespace datapump::audio;
    constexpr auto mode=UtcTimeline::Prediction::timestamp_intersection;
    constexpr long double epoch=1800000000.L;
    constexpr double raw_radius=.014375,shared=.004375,limit=.025;
    UtcTimeline preceding(8000,.001,mode,raw_radius);
    UtcTimeline native(8000,.001,mode,shared,true);
    const FrameTimestamp first{400,epoch+.05L-.010368093L,.05L-.010368093L,raw_radius,1e-6,TimingQuality::estimated,17};
    preceding.observe(first);native.observe(first);
    const auto source_origin=native.predict(400).utc_seconds;
    double worst=0;unsigned old_failures=0,new_failures=0;
    for(unsigned i=2;i<=1000;++i) {
        // Quantized pointer phases recur, without independent jitter or a
        // zero-mean assumption. The SOURCE origin never moves.
        const auto jitter=i%3?.010L:-.010L;
        const auto frame=i*400.L,time=i*.05L;
        const FrameTimestamp stamp{frame,epoch+time+jitter,time+jitter,raw_radius,1e-6,TimingQuality::estimated,17};
        preceding.observe(stamp);native.observe(stamp);
        for(const long double horizon:{-.025L,0.L,.153125L,3.2L}) {
            const auto target=frame+horizon*8000;
            const auto desired=source_origin+(target-400)/8000;
            const auto p=native.predict(target),old=preceding.predict(target);
            require(std::abs(p.utc_seconds-(epoch+target/8000))<=p.uncertainty_seconds,
                "persistent native interval omitted the physical device clock");
            const auto total=std::abs(p.utc_seconds-desired)+p.uncertainty_seconds;
            worst=std::max(worst,static_cast<double>(total));
            require(total<limit && p.uncertainty_seconds>=shared && p.quality==TimingQuality::estimated,
                "native interval moved the immutable source outside Audio50");
            new_failures+=total>limit;
            (void)presentation_correction(p,desired,8000,limit);
            old_failures+=std::abs(old.utc_seconds-desired)+old.uncertainty_seconds>limit;
        }
    }
    require(old_failures>100,"frozen-origin fixture did not retain preceding false-stop evidence");
    std::cout<<"Native frozen-origin history: previous failures="<<old_failures
        <<" new failures="<<new_failures<<" new worst_total_ms="<<worst*1000<<" source_origin_shift_ms=-10.368093\n";
    // A constant unknown period bias may not be averaged away. With no phase
    // diversity the interval stays broad, and an out-of-range clock rejects.
    for(const double bias:{-.010,.010}) {
        UtcTimeline t(8000,.001,mode,shared,true);
        for(unsigned i=1;i<=130;++i)t.observe({i*400.L,epoch+i*.05L+bias,i*.05L,raw_radius,1e-6,TimingQuality::estimated,17});
        require(t.predict(52000).uncertainty_seconds>=raw_radius-1e-8,
            "constant correlated pointer bias was averaged away");
    }
    // Full existing generic clock-envelope endpoints, including UTC slew,
    // shared offsets, fractional positions and extrapolation.
    for(const double drift:{1/(1+.002)-1,0.,1/(1-.002)-1})for(const double offset:{-shared,shared}) {
        UtcTimeline t(48000,.001,mode,shared,true);
        for(unsigned i=1;i<=300;++i) {
            const auto frame=i*2400.L+.375L;
            const auto truth=epoch+frame/48000*(1+drift);
            const auto jitter=i%3==0?.010L:-.010L;
            t.observe({frame,truth+offset+jitter,frame/48000*(1+drift)+offset+jitter,
                raw_radius,1e-6,TimingQuality::estimated,17});
            for(const long double horizon:{-.025L,0.L,.153125L,3.2L}) {
                const auto target=frame+horizon*48000;
                const auto p=t.predict(target);
                const auto actual=epoch+target/48000*(1+drift);
                require(std::abs(p.utc_seconds-actual)<=p.uncertainty_seconds,
                    "native rate propagation omitted a fractional/offset endpoint");
            }
        }
    }
    // Informative constraints survive history eviction; broad-but-compatible
    // later observations cannot reset either source origin or the rate budget.
    UtcTimeline persistent(8000,.001,mode,shared,true),recent(8000,.001,mode,shared);
    for(unsigned i=1;i<=140;++i) {
        const auto time=i*.001L;
        const auto radius=i<=2?shared:.030;
        const auto bias=i<=2?0.L:.010L;
        const FrameTimestamp value{i*8.L,epoch+time+bias,time+bias,radius,1e-6,TimingQuality::estimated,17};
        persistent.observe(value);recent.observe(value);
    }
    require(persistent.predict(1120).uncertainty_seconds<.005 && recent.predict(1120).uncertainty_seconds>.029,
        "persistent phase constraint was forgotten at ring wrap");
    require(persistent.predict_without_history(1120+48000).uncertainty_seconds>.042,
        "silence restart incorrectly retained refined phase evidence during qualification");
    // Continuous UTC slew is still admitted; GUI oscillator precision must not
    // silently replace the native provider's existing generic rate envelope.
    for(const double slew:{-.0005,.0005}) {
        UtcTimeline t(8000,.001,mode,shared,true);
        for(unsigned i=1;i<=160;++i) {
            const auto frame=i*400.L;
            const auto steady=frame/8000*1.0001L;
            const auto truth=epoch+steady*(1+slew);
            t.observe({frame,truth,steady,raw_radius,1e-6,TimingQuality::estimated,17});
            const auto p=t.predict(frame+1225);
            const auto future=epoch+(frame+1225)/8000*1.0001L*(1+slew);
            require(std::abs(p.utc_seconds-future)<=p.uncertainty_seconds,
                "native position bound discarded sound-card drift or continuous UTC slew");
        }
    }
    UtcTimeline duplicate(8000,.001,mode,shared,true);
    duplicate.observe({400,epoch+.05L,.05L,raw_radius,1e-6,TimingQuality::estimated,17});
    duplicate.observe({400,epoch+.07L,.07L,raw_radius,1e-6,TimingQuality::estimated,17});
    const auto unchanged=duplicate.predict(400);
    rejects([&]{duplicate.observe({400,epoch+.09L,.09L,raw_radius,1e-6,TimingQuality::estimated,17});});
    require(duplicate.predict(400).utc_seconds==unchanged.utc_seconds &&
        duplicate.predict(400).uncertainty_seconds==unchanged.uncertainty_seconds,
        "contradictory duplicate observation mutated the retained timeline");
    UtcTimeline wrong_clock(8000,.001,mode,shared,true);
    bool rejected=false;
    for(unsigned i=1;i<=1000 && !rejected;++i) {
        try {wrong_clock.observe({i*400.L,epoch+i*.05L*1.003L,i*.05L*1.003L,
            raw_radius,1e-6,TimingQuality::estimated,17});}catch(const datapump::Error&){rejected=true;}
    }
    require(rejected,"native rate envelope silently forgot a contradictory clock");
    rejects([&]{UtcTimeline bad(8000,.001,UtcTimeline::Prediction::fitted,shared,true);});
    // Screenshot's current point fits, but the full generic cone used at its
    // block end grows by0.1002ms. Do not call this a safe continuation.
    require(protected_timing_frontiers(.02494976,.025049963,.02499,.024999,limit)==ProtectedTimingAction::abort,
        "native block-end forecast ignored uncertainty growth");
    require(protected_timing_frontiers(.020,.0201,.02499,.02501,limit)==ProtectedTimingAction::finish,
        "native stop forecast spent the next-block reaction margin");
    // Recorded failure: full independent cover forecast crosses 25ms while
    // current PCM remains at16.145ms. Reserve valid pulse support, not a second
    // copy of the entire separately keyed3+s cover. Runtime cover checks remain.
    constexpr double observed=.016145462,cone=1./(1.-.002)-1.;
    require(protected_timing_frontiers(observed,observed+cone*.05,
        observed+cone*4.369402,observed+cone*4.419402,limit)==ProtectedTimingAction::finish,
        "recorded conservative full-cover stop was not reproduced");
    require(protected_timing_frontiers(observed,observed+cone*.05,
        observed+cone*.15,observed+cone*.20,limit)==ProtectedTimingAction::proceed,
        "independent cover incorrectly consumes valid-pulse timing reserve");
    require(protected_timing_frontiers(.0251,.0252,.0253,.0254,limit)==ProtectedTimingAction::abort,
        "independent cover exemption excused an actual current UTC violation");
    require(protected_timing_frontiers(.020,.0201,.023,.0231,limit)==ProtectedTimingAction::proceed,
        "native safe stop frontier was rejected");
}
void continuous_guard() {
    using namespace datapump::audio;
    require(protected_timing_action(.001,.001,.0003,3,.05,1e-6,.004)==ProtectedTimingAction::proceed,
        "safe protected tail spuriously stopped");
    require(protected_timing_action(.001,.001,.0003,3,.05,1e-6,.00291)==ProtectedTimingAction::finish,
        "controller spent the one-block protected-stop reaction margin");
    require(protected_timing_action(.001,.002,.0003,3,.05,1e-6,.00291)==ProtectedTimingAction::abort,
        "infeasible protected tail was falsely promised after uncertainty increased");
    for(const auto fs:{64U,6000U,48000U})for(const double initial:{-.00085,-.0001,0.,.0001,.00085})for(const bool steering:{false,true}) {
        Resampler resampler(fs,fs,true);resampler.initialize_timing(.375,initial);
        const auto origin=resampler.source_seconds();resampler.observe_affine_timing(origin,initial);
        ContinuousAffineTimingGuard guard(origin,initial,fs,fs,.01/fs);
        std::array<float,257> input{},output{};std::size_t offset=0;
        double greatest=0;
        for(unsigned block=0;block<400;++block) {
            const auto before=resampler.timing_block();
            const auto size=1+(block*79)%output.size();
            const auto plan=guard.plan(steering?(block%71<35?.0008:-.0008):initial,
                {resampler.source_seconds(),before.frames,resampler.rate_correction()},size,1e-5);
            resampler.begin_timing_block();resampler.set_rate_correction(plan.target,plan.slew);
            std::size_t count=0;
            while(count<size) {
                const auto p=resampler.process(std::span(input).subspan(offset),std::span(output).subspan(count,size-count));
                offset=(offset+p.consumed)%input.size();count+=p.produced;
                require(p.consumed || p.produced,"continuous guard fixture stalled");
            }
            const auto actual=resampler.timing_block();
            const ContinuousAffineTimingGuard::Point end{resampler.source_seconds(),actual.frames,resampler.rate_correction()};
            guard.verify(plan,end,actual.minimum,actual.maximum);
            greatest=std::max(greatest,static_cast<double>(std::max(std::abs(actual.minimum),std::abs(actual.maximum))));
            rejects([&]{guard.verify(plan,end,actual.minimum,actual.maximum);}); // stale certificate
        }
        require(greatest<=.01/fs,"continuous correction reset the original affine allowance");
        rejects([&]{resampler.observe_affine_timing(resampler.source_seconds(),0);});
    }
    ContinuousAffineTimingGuard check(0,0,48000,48000,.01/48000);
    const auto plan=check.plan(.0001,{0,0,0},1,1e-5);
    rejects([&]{check.verify(plan,{1.L/48000,1,0},-1.,1.);}); // safe-looking endpoint, unsafe interior
    const auto shorter=check.plan(0,{0,0,0},29,1e-5);
    check.verify(shorter,{1.L/48000,1,0},0,0); // final short block needs no horizon reset
}

void shaped_timing_error(datapump::modem::Config config) {
    using namespace datapump::modem;
    config.scramble=true;config.stream_epoch=1800000100;
    for(std::size_t i=0;i<config.spreading_seed.size();++i)config.spreading_seed[i]=static_cast<std::uint8_t>(i*31+7);
    for(std::size_t i=0;i<config.dsss_seed.size();++i)config.dsss_seed[i]=static_cast<std::uint8_t>(i*17+3);
    const datapump::Bytes bits{0,1,0};
    PatternTransmitter transmitter(bits,config,config.stream_epoch,0,false);
    std::vector<float> raw(static_cast<std::size_t>(transmitter.total_samples()));
    require(transmitter.read(raw)==raw.size(),"finite waveform fixture incomplete");
    PatternCode code(config,config.stream_epoch);
    const auto padding=pattern_pulse_padding_samples(config);
    const double amplitude=std::sqrt(2*nominal_signal_power);
    const auto ideal=[&](double sample) {
        std::complex<double> envelope{};
        for(std::size_t bit=0;bit<bits.size();++bit)
            envelope+=code.shaped_value(bit*code.chips_per_symbol(),bits[bit],
                sample-static_cast<double>(padding)-static_cast<double>(bit*code.symbol_samples()));
        return (pattern_limit_pcm(amplitude*envelope)*std::polar(1.,
            2*std::numbers::pi*config.carrier_hz*sample/config.sample_rate)).real();
    };
    datapump::audio::Resampler resampler(config.sample_rate,config.sample_rate,true);
    resampler.initialize_timing(.375,0);
    const auto slew=datapump::audio::conservative_timing_slew(config.sample_rate,
        static_cast<double>(code.symbol_samples())/config.sample_rate);
    resampler.set_rate_correction(.0001,slew);
    std::array<float,257> output{};
    std::size_t offset=0,produced=0;double energy=0,observed=0,dot=0,error=0;
    while(!resampler.finished()) {
        const auto count=std::min<std::size_t>(1031,raw.size()-offset);
        const auto progress=resampler.process(std::span(raw).subspan(offset,count),output,offset+count==raw.size());
        require(progress.consumed || progress.produced || resampler.finished(),"finite waveform ASRC stalled");
        offset+=progress.consumed;
        for(std::size_t i=0;i<progress.produced;++i,++produced) {
            const double t=static_cast<double>(produced)/config.sample_rate;
            const auto ramp=std::min(t,.0001/slew);
            const double source=.375+config.sample_rate*(t+.5*slew*ramp*ramp+.0001*(t-ramp));
            const auto reference=ideal(source),sample=static_cast<double>(output[i]);
            energy+=reference*reference;observed+=sample*sample;dot+=reference*sample;
            error+=(reference-sample)*(reference-sample);
        }
    }
    const auto mismatch_db=-10*std::log10(dot*dot/(energy*observed));
    const auto relative_error=error/energy;
    std::cout<<"ASRC finite-pulse case Fs="<<config.sample_rate<<" carrier="<<config.carrier_hz
        <<" DSSS="<<config.dsss_factor<<" symbol_seconds="<<static_cast<double>(code.symbol_samples())/config.sample_rate
        <<" samples="<<produced<<" waveform_mismatch_db="<<mismatch_db<<" error_energy_ratio="<<relative_error<<'\n';
    // Conditional waveform-norm regression, including every boundary/tail and
    // the actual nonlinear amplitude limiter. Not an acquisition-bank PD/PFA
    // or receiver noise-covariance qualification.
    require(mismatch_db<.01 && relative_error<.001,"UTC interpolation distorts finite shaped/limited waveform");
}
}
int main(int argc,char** argv) {try {
    const bool diagnose=argc==2&&std::string(argv[1])=="--diagnose-all";
    require(argc==1||diagnose,"usage: test_clock_sync [--diagnose-all]");
    unsigned diagnostic_failures=0;
    const auto run=[&](const char* name,auto test) {
        if(!diagnose) {test();return;}
        try {test();std::cout<<"clock_diagnostic case="<<name<<" result=passed\n"<<std::flush;}
        catch(const std::exception& error) {++diagnostic_failures;
            std::cerr<<"clock_diagnostic case="<<name<<" result=failed error="<<error.what()<<'\n';}
    };
    run("declared_clock_timing",declared_clock_timing);run("physical_waveform_rate",physical_waveform_rate);run("bounded_system_utc_phase",bounded_system_utc_phase);
    require(datapump::modem::FhssPlan{}.audio_error_seconds==datapump::clock_sync::default_audio_error_seconds &&
        datapump::modem::SchedHopPlan{}.audio_error_seconds==datapump::clock_sync::default_audio_error_seconds,
        "FHSS and Sched_Hop must share the configured audio-lag default");
    run("native_timestamp_intervals",native_timestamp_intervals);run("native_query_uncertainty",native_query_uncertainty);run("native_frozen_source_timing",native_frozen_source_timing);
    require(datapump::clock_sync::parse_audio_error("default")==.07&&
        datapump::clock_sync::parse_audio_error("Default")==.07&&
        datapump::clock_sync::parse_audio_error("50ms")==.05,
        "CLI Audio Default no longer resolves to70ms while preserving explicit50ms");
    rejects([]{datapump::clock_sync::parse_audio_error("50");});
    run("continuous_guard",continuous_guard);
    using namespace datapump::audio;
    constexpr long double epoch=1800000000.L;
    for(const auto fs:{64U,6000U,48000U})for(const auto ppm:{-200.,0.,200.})for(const auto age:{-7.,0.,8.}) {
        const datapump::clock_sync::Policy policy{.001,.4,2.564};
        const auto tau=(1+ppm*1e-6)/fs;
        const auto anchor=epoch+age;
        const auto map=datapump::clock_sync::arrival_map(policy,epoch,anchor,-173,fs,tau,.002,.1,fs-1,.5);
        require(map.has_value(),"valid modeled timing map unexpectedly unavailable");
        for(const auto phase:{0U,fs/3,fs-1})for(const auto rate:{-.002L,.002L})
            for(const auto arrival_error:{-.802L,.802L}) {
                const auto actual=-173+(epoch+policy.offset_seconds-anchor+
                    static_cast<long double>(phase)/fs+arrival_error)/(tau+rate/fs);
                const auto predicted=map->origin_samples+map->phase_scale*phase;
                require(std::abs(actual-predicted)<=map->half_width_samples,
                    "modeled UTC interval lost a delay/slope/phase corner");
            }
        require(map->half_width_samples<fs,"small region unexpectedly restored the full legacy window");
    }
    require(!datapump::clock_sync::arrival_map({},epoch,epoch,0,64,1./64,1,.1,63),
        "singular inverse-slope model authorized timing pruning");
    auto invalid_policy=datapump::clock_sync::Policy{};
    invalid_policy.accuracy_seconds=std::numeric_limits<double>::quiet_NaN();
    require(!datapump::clock_sync::arrival_map(invalid_policy,epoch,epoch,0,64,1./64,0,.1,63),
        "nonfinite GPS accuracy produced a zero-width prior");
    // Retain the original broad-budget cases, and test the new adjustable
    // default separately rather than shrinking their long-horizon assertions.
    EstimatedDeviceTiming assumed;assumed.maximum_utc_error_seconds=.5;
    require(assumed.initial_correction({},1,.1,.0003)==0.,"short stream needlessly waits for a rate estimate");
    require(!assumed.initial_correction({},14400,.1,.0003),"long stream discarded accumulated clock error");
    require(!assumed.initial_correction(LinkRateInterval{.00005,.00015,40},14400,.1,.0003),
        "imprecise rate estimate authorized long-message UTC timing");
    const auto prepared=assumed.initial_correction(LinkRateInterval{.00009,.00011,400},14400,.1,.0003);
    require(prepared && std::abs(*prepared-.0001)<1e-15,"finite timing preparation lost measured rate");
    require(!assumed.initial_correction(LinkRateInterval{.00029,.00031,40},14400,.1,.0003),
        "timing preparation exceeded the receiver correction domain");
    rejects([&]{assumed.initial_correction(LinkRateInterval{.00029,.00031,400},14400,.1,.0003);});
    rejects([&]{assumed.initial_correction(LinkRateInterval{.0004,.0005,40},14400,.1,.0003);});
    EstimatedDeviceTiming ordinary;
    require(ordinary.maximum_utc_error_seconds==.03 &&
        ordinary.initial_correction({},1,.013,.0003)==0.,"30 ms audio model needlessly delays a short waveform");
    rejects([&]{ordinary.initial_correction({},1,.1,.0003);});
    const auto narrow=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,.03,0,.03);
    const auto wide=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,.03,0,.08);
    require(narrow && wide && std::abs((wide->half_width_samples-narrow->half_width_samples)/48000-.05)<1e-12,
        "editable audio error did not propagate exactly into arrival coverage");
    const auto default_audio=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,datapump::clock_sync::default_audio_error_seconds/2,0);
    const auto zero_audio=datapump::clock_sync::arrival_map({},epoch,epoch,0,48000,1./48000,0,0,0,0);
    require(default_audio && zero_audio &&
        std::abs((default_audio->half_width_samples-zero_audio->half_width_samples)/48000-datapump::clock_sync::default_audio_error_seconds)<1e-12,
        "default total per-station audio interval incorrectly used as a radius");
    const auto positive=datapump::clock_sync::audio_lag_arrival_map({0,0,0},epoch,epoch,0,1000,1./1000,0,.01,0,.05);
    require(positive && std::abs(positive->origin_samples-25)<1e-6 &&
        positive->half_width_samples>=35 && positive->half_width_samples<35.02,
        "positive playback lag lost its center or halved provider uncertainty");
    require(!datapump::clock_sync::audio_lag_arrival_map({},epoch,epoch,0,1000,1./1000,0,.026,0,.05),
        "26 ms timestamp radius qualified inside a 50 ms total lag interval");
    require(datapump::clock_sync::audio_lag_arrival_map({},epoch,epoch,0,1000,1./1000,0,.024,0,.05).has_value(),
        "24 ms timestamp radius rejected a 50 ms total lag interval");
    // A hop calendar is GPS-aligned at Default. Unknown message start remains
    // a separate epoch/phase search, never an independent calendar translation.
    const auto hop_policy=datapump::clock_sync::effective_policy(std::nullopt,true);
    require(hop_policy && hop_policy->accuracy_seconds==.010 && hop_policy->region_seconds==0 &&
            hop_policy->offset_seconds==0,"Default hop calendar did not resolve the existing 10 ms station bound");
    require(!datapump::clock_sync::effective_policy(std::nullopt,false),
        "ordinary Default unexpectedly gained a narrow timing assumption");
    const datapump::clock_sync::Policy eme{.003,.04,2.564};
    require(datapump::clock_sync::effective_policy(eme,true)==eme,
        "hop default replaced selected accuracy, region or EME offset");
    for(const auto lag:{0.,.05,.1})for(const auto u:{0.,.012,.2}) {
        const auto calendar=datapump::clock_sync::calendar_arrival_map(*hop_policy,epoch,epoch,0,
            1000,1./1000,0,u,1000,lag);
        require(calendar && std::abs(calendar->origin_samples-lag*500)<1e-6 &&
                calendar->half_width_samples>=(.02+lag/2+u)*1000 &&
                calendar->half_width_samples<(.02+lag/2+u)*1000+.02,
            "calendar conflated provider dating uncertainty with causal Audio lag");
        for(const auto phase:{0.,333.,1000.})for(const auto endpoint:{-1.,1.}) {
            const auto actual=phase+lag*500+endpoint*(.02+lag/2+u)*1000;
            require(std::abs(actual-(calendar->origin_samples+calendar->phase_scale*phase))<=calendar->half_width_samples,
                "calendar joint arrival/phase map omitted an admitted corner");
        }
    }
    TimePrediction boundary{1.015L,.014,1./48000,TimingQuality::bounded,0};
    require(presentation_correction(boundary,1,48000,.03).uncertainty_seconds==.014,
        "valid 30 ms presentation allowance rejected");
    boundary.utc_seconds=1.017L;
    rejects([&]{presentation_correction(boundary,1,48000,.03);});
    require(presentation_correction(boundary,1,48000,.04).uncertainty_seconds==.014,
        "larger selected audio error was ignored");
    datapump::clock_sync::validate_audio_error(0.);
    require(datapump::clock_sync::default_audio_error_seconds==.07 &&
        datapump::clock_sync::Policy{}.region_seconds==.001 &&
        datapump::clock_sync::Policy{}.offset_seconds==0,
        "local clock defaults lost the 70 ms audio allowance or restored the Earth-Moon-Earth preset");
    for(const auto error:{-1.,61.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        rejects([&]{datapump::clock_sync::validate_audio_error(error);});
    for(const auto horizon:{1.,3600.,86400.})for(const auto initial:{-.0001,.0001}) {
        constexpr long double origin=.375L/48000;
        constexpr double limit=.01/48000;
        AffineTimingGuard guard(origin,initial,horizon,limit);
        long double source=origin,elapsed=0;
        double current=initial;
        for(unsigned i=0;i<1000;++i) {
            const auto target=guard.constrain(i%2?.001:-.001,source,elapsed,current);
            // A trapezoidal rate transition models one finite resampler ramp.
            const auto next=current+.5*(target-current);
            const auto dt=static_cast<long double>(horizon)/1000;
            source+=(1.L+(current+next)/2)*dt;elapsed+=dt;current=next;
            // Use the exact final coordinate to avoid fixture summation error
            // being mistaken for a caller that exceeds its declared duration.
            guard.verify(source,i==999?horizon:elapsed);
        }
        rejects([&]{guard.verify(source+.001,horizon);});
        rejects([&]{guard.verify(source,horizon+1);});
    }
    // A sustained correction spends the whole warp budget. Intermediate
    // coordinate rounding must not spend its reserved allowance a second time
    // or shrink the cap below the rate already safely installed.
    for(const auto horizon:{1.,3600.,86400.})for(const auto sign:{-1.,1.}) {
        constexpr long double origin=.375L/48000;
        constexpr double initial=.0001,limit=.01/48000,roundoff=limit/10;
        const auto radius=(limit-roundoff)/horizon;
        AffineTimingGuard guard(origin,initial,horizon,limit,roundoff);
        const auto current=guard.constrain(sign*.001,origin,0,initial);
        require(std::abs(current-initial-sign*radius)<2e-20,"finite timing cap changed its complete-horizon bound");
        for(const auto fraction:{.5L,.99L,1.L}) {
            const auto elapsed=fraction*horizon;
            const auto source=origin+(1.L+current)*elapsed+sign*.9L*roundoff;
            guard.verify(source,elapsed);
            require(guard.constrain(sign*.001,source,elapsed,current)==current,
                "reserved coordinate roundoff falsely exhausted finite timing continuation");
        }
        rejects([&]{guard.verify(origin+(1.L+initial)*horizon+sign*1.01L*limit,horizon);});
    }
    // A huge arbitrary codec offset cannot enter the rate calculation. The
    // interval contains every deterministic endpoint error, including jitter
    // with the same sign at every intermediate observation.
    for(const auto ppm:{-500.,0.,500.})for(const auto offset:{0.L,12345.L})
        for(const auto endpoint:{-1.,1.}) {
            LinkRateTimeline rate;
            for(unsigned i=0;i<=40;++i) {
                const long double elapsed=i*.1L;
                const auto jitter=(i?endpoint:-endpoint)*2e-6L;
                rate.observe({offset+elapsed*(1+ppm*1e-6L),epoch+elapsed+jitter,elapsed,
                    42e-9,2e-6,17});
            }
            const auto interval=rate.interval();
            const auto truth=1/(1+ppm*1e-6)-1;
            require(interval && interval->lower<=truth && truth<=interval->upper,
                "link clock interval lost true rate at timestamp error boundary");
            require(interval->uncertainty()>1e-6,"link clock fit averaged away bounded jitter");
            rejects([&]{rate.observe({offset+5,epoch+6,5,42e-9,2e-6,17});});
        }
    LinkRateTimeline unresolved;
    unresolved.observe({0,epoch,0,.1,.1,0});
    unresolved.observe({.05L,epoch+.05L,.05L,.1,.1,0});
    require(!unresolved.interval(),"short imprecise timestamps manufactured rate confidence");
    for(const auto ppm:{-500.,-100.,0.,100.,500.}) {
        UtcTimeline timeline(48000);
        for(unsigned i=0;i<80;++i) {
            const long double t=i*.05L;
            timeline.observe({t*48000*(1+ppm*1e-6L),epoch+t,t,2e-7,2e-8,TimingQuality::bounded,4});
        }
        const auto prediction=timeline.predict(4.1L*48000*(1+ppm*1e-6L));
        require(std::abs(prediction.utc_seconds-epoch-4.1L)<2e-8,"UTC fit lost fractional timestamp accuracy");
        require(prediction.quality==TimingQuality::bounded,"bounded provider was downgraded");
        const auto correction=presentation_correction(prediction,epoch+4.1L,48000);
        require(std::abs(correction.rate_fraction-(1/(1+ppm*1e-6)-1))<1e-8,"DAC correction has wrong sign or scale");
    }
    UtcTimeline timeline(48000);
    timeline.observe({0,epoch,0,.1,1e-6,TimingQuality::estimated,9});
    timeline.observe({2400,epoch+.05L,.05L,.1,1e-6,TimingQuality::estimated,9});
    require(timeline.predict(4800).quality==TimingQuality::estimated,"estimated device timing promoted to a bound");
    require(timeline.predict(4800).uncertainty_seconds>=.1,"fit discarded provider uncertainty");
    rejects([&] {timeline.observe({4800,epoch+1.1L,.1L,.1,1e-6,TimingQuality::estimated,9});});
    rejects([&] {timeline.observe({4800,epoch-.1L,.1L,.1,1e-6,TimingQuality::estimated,9});});
    rejects([&] {timeline.observe({4800,epoch+.1L,.1L,.1,1e-6,TimingQuality::estimated,10});});
    rejects([&] {presentation_correction(timeline.predict(4800),epoch,48000,.01);});
    require(conservative_timing_slew(48000,3600)<conservative_timing_slew(48000,.1)*1e-6,
        "timing slew uses audio block length instead of full coherent symbol");
    const auto policy=datapump::clock_sync::parse("GPS_1ms-400ms_region-2564ms_offset");
    require(policy && std::abs(policy->half_window_seconds()-.202)<1e-12,"clock window changed total-region semantics");
    datapump::modem::Config waveform;waveform.sample_rate=4800;waveform.carrier_hz=1200;waveform.bandwidth_hz=1200;
    shaped_timing_error(waveform);
    waveform.bandwidth_hz=120;waveform.dsss_factor=10;shaped_timing_error(waveform);
    waveform.sample_rate=64;waveform.carrier_hz=.5;waveform.bandwidth_hz=.5;waveform.dsss_factor=1;
    shaped_timing_error(waveform);
    if(diagnostic_failures)return 1;
    std::cout<<"UTC timestamp, uncertainty, rate direction and discontinuity tests passed\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}}

#include "../src/helper_reference_search.hpp"
#include "../src/helper_reference_tracking_estimate.hpp"
#include "../src/live_helper_frontend.hpp"
#include "datapump/simulation_estimate.hpp"
#include "datapump/tuning.hpp"
#include <cmath>
#include <iostream>
#include <set>
using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
void near(double a,double b,const char* text){check(std::isfinite(a)&&std::abs(a-b)<=1e-11*std::max({1.,std::abs(a),std::abs(b)}),text);}

void shared_frequency_clock_controls() {
    using Relation=modem::HelperReferenceClockRelation;
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_keystream,true,1500,10,
        modem::OuterDsssVersion::interleaved_v2).config;
    c.oscillator_search.emplace();auto& policy=*c.oscillator_search;
    policy.lf={100,.5};policy.rf={100,.5};policy.margin=3;policy.rf_shift_hz=30e6;
    policy.reference=modem::OscillatorReference::shared_radio;
    constexpr double H=2462.5,U=.04;
    const auto measure=[&](double observed,double uncertainty=.04,double correction=0) {
        return modem::helper_reference_shared_clock_interval(c,observed,H,uncertainty,correction,
            Relation::common_corrected_source);
    };
    // No hidden UTC steering accompanies the shared-radio Default or GPS
    // arrival policy. Neither timestamp nor decoded metadata enters the bound.
    check(modem::utc_transmit_rate_limit(c)==0,"shared radio acquired an independent UTC rate factor");
    const auto frequency=H+(30e6+H)*100e-6;
    const auto clock=measure(frequency);
    check(clock && clock->lower_ppm<100 && clock->upper_ppm>100 &&
        clock->upper_ppm-clock->lower_ppm<.002667,
        "qualified HF shared-radio frequency did not resolve DATA clock");
    std::size_t cases=0;
    for(const auto side:{modem::OscillatorSideband::upper,modem::OscillatorSideband::lower}) {
        policy.sideband=side;
        const auto shift=side==modem::OscillatorSideband::upper?30e6:-30e6;
        for(const double ppm:{-300.,-100.,0.,100.,300.})for(const double error:{-U,0.,U}) {
            // This pure API requires a SIGNED physical image. A real-PCM
            // magnitude alone has no authority to choose +f versus -f.
            const auto observed=static_cast<double>(static_cast<long double>(H)+
                (static_cast<long double>(shift)+H)*ppm*1e-6L+error);
            const auto got=measure(observed);
            check(got && got->lower_ppm<=ppm+1e-9 && got->upper_ppm>=ppm-1e-9,
                "signed shared-clock truth escaped its directed frequency interval");++cases;
        }
    }
    policy.sideband=modem::OscillatorSideband::upper;
    check(!measure(frequency,U,.0003) && !measure(frequency,U,NAN),
        "unresolved UTC steering was silently converted to DATA clock");
    check(!measure(frequency,-1) && !measure(NAN) && !measure(INFINITY),
        "invalid measured frequency gained source-clock authority");
    policy.reference=modem::OscillatorReference::independent_audio;
    check(!measure(frequency),"independent RF oscillator invented an audio-clock measurement");
    policy.reference=modem::OscillatorReference::shared_radio;policy.rf_shift_hz=0;
    check(!measure(frequency),"audio-only metadata invented shared-radio coupling");
    policy.rf_shift_hz=H;policy.sideband=modem::OscillatorSideband::lower;
    check(!measure(frequency),"zero signed helper coupling divided into clock authority");
    policy.rf_shift_hz=30e6;policy.sideband=modem::OscillatorSideband::upper;
    check(!measure(H+(30e6+H)*400e-6),"disjoint measured clock widened the original shared prior");
    // Select exact existing cells. Correlated measurement error is retained
    // conservatively in BOTH the RF window and this clock enclosure; no new
    // lattice, narrower timing cells or smaller logical trial charge is made.
    const auto original=modem::helper_reference_search(c,U,H,0,0,Relation::common_corrected_source);
    modem::PatternSearch search;modem::apply_helper_reference_search(
        modem::helper_reference_search(c,U,H,0,0,Relation::common_corrected_source),search);
    modem::detail::PatternHelperFrequencySelector selector;
    modem::PatternHelperFrequencyWindow window{0,U,H-c.carrier_hz,original.catalogue.clock_step_ppm/2,{}};
    check(selector.initialize(search,window),"original shared residual cells were not selectable");
    const auto broad=selector.selected;window.clock_interval=clock;selector.update(window);
    check(selector.selected>0 && selector.selected<broad && search.hypotheses==original.catalogue.hypotheses,
        "frequency-derived clock failed to omit work or changed original cells");
    window.clock_interval.reset();selector.update(window);
    check(selector.selected==broad,"clock qualification loss did not restore original clock alternatives");
    std::cout<<"shared_frequency_clock cases="<<cases<<" pairs="<<search.hypotheses.size()
        <<" frequency_only="<<broad<<" source_clock="<<clock->lower_ppm<<".."<<clock->upper_ppm<<'\n';
}

}
int main(){try {shared_frequency_clock_controls();
    live::detail::HelperViewBlock::ClockMeasurement clock{{-2,2},100,200,201,48000};
    check(clock.valid(48000),"dated marker evidence declined its captured source clock");
    check(!clock.valid(48960),"marker evidence crossed physical sample-clock domains");
    clock.captured_end_sample=200;
    check(!clock.valid(48000),"marker evidence used an uncaptured support endpoint");
    clock.captured_end_sample=26602;
    check(!clock.valid(48000),"stale marker evidence retained clock authority");
    auto c=tuning::resolve(36,8,tuning::PatternMode::auto_pattern,false,1500).config;
    c.oscillator_search.emplace();auto& o=*c.oscillator_search;
    o.lf={100,.1};o.rf={100,.1};o.margin=3;o.rf_shift_hz=30000000;
    o.reference=modem::OscillatorReference::independent_audio;
    const auto original=modem::oscillator_pattern_search(c);
    auto reference=modem::helper_reference_search(c,.15,2000,0,0);
    near(reference.clock_frequency_hz,-500,"helper search changed K=D-H");
    near(reference.rf_half_width_hz,.15,"helper search invented RF uncertainty");
    near(reference.catalogue.requested_clock_half_width_ppm,300,"helper tone removed unresolved LF");
    check(reference.catalogue.hypotheses.size()<original.hypotheses.size(),"helper correction did not reduce actual catalogue work");
    std::set<double> clocks;for(const auto& pair:reference.catalogue.hypotheses)clocks.insert(pair.clock_error_ppm);
    check(*clocks.begin()==-300&&*clocks.rbegin()==300,"residual catalogue omitted original LF endpoints");
    const auto utc=modem::helper_reference_search(c,.15,2000,0,.0003);
    check(utc.rf_half_width_hz>=.15+2000*.0003*(1+.0003),"helper correction silently removed peer UTC RF uncertainty");
    check(utc.catalogue.requested_clock_half_width_ppm>reference.catalogue.requested_clock_half_width_ppm,
        "helper correction omitted UTC DATA clock union");
    const auto shifted=modem::helper_reference_search(c,.15,2000,.125,0);
    check(shifted.catalogue.hypotheses.size()==reference.catalogue.hypotheses.size(),"residual center changed constructor LF alternatives");
    for(std::size_t i=0;i<shifted.catalogue.hypotheses.size();++i)
        near(shifted.catalogue.hypotheses[i].frequency_offset_hz-reference.catalogue.hypotheses[i].frequency_offset_hz,.125,
            "residual center was not applied to every exact pair");
    modem::PatternSearch applied;modem::apply_helper_reference_search(std::move(reference),applied);
    check(applied.hypotheses==modem::helper_reference_search(c,.15,2000,0,0).catalogue.hypotheses&&
        !applied.qualified_clock_interval&&applied.admitted_clock_half_width_ppm==300,
        "helper application pruned LF or changed the exact shared catalogue");
    auto audio=c;audio.oscillator_search->rf_shift_hz=0;
    const auto audio_reference=modem::helper_reference_search(audio,.15,audio.carrier_hz,0,0);
    check(audio_reference.catalogue.hypotheses.size()>1&&audio.oscillator_search->rf_shift_hz==0,
        "audio-only residual synthesis changed physical mapping or collapsed unknown LF");
    transfer::Options options;options.modem=c;options.dsp_workspace_bytes=256*1024*1024;
    transfer::Estimate one;one.wire_bits=1;one.total_seconds=modem::symbol_seconds(c);
    modem::ChannelConfig channel;channel.snr_db=10;
    const auto ordinary=simulation::estimate(one,options,true,channel,{},1,false,100,0,
        simulation::ReceiverWorkMode::hardware_fallback);
    const auto same=simulation::estimate_with_receiver_search(one,options,true,channel,original,1,100,
        simulation::ReceiverWorkMode::hardware_fallback);
    near(same.receiver_cpu_seconds,ordinary.receiver_cpu_seconds,"explicit original search changed ordinary cost model");
    check(same.frequency_rate_hypotheses==ordinary.frequency_rate_hypotheses&&
        same.receiver_workspace_supported==ordinary.receiver_workspace_supported,
        "explicit original search changed ordinary constructor geometry");
    check(same.carrier_in_search==ordinary.carrier_in_search&&same.clock_in_search==ordinary.clock_in_search&&
        same.execution_projection_bin_samples==ordinary.execution_projection_bin_samples&&
        same.carrier_search_half_width_hz==ordinary.carrier_search_half_width_hz,
        "explicit original zero-origin bank changed ordinary coverage or projection geometry");
    // A translated paired catalogue is a union of finite cells, not its local
    // zero-centered radius or a filled hull between separated RF alternatives.
    auto cell_options=options;cell_options.modem.receiver_strategy=modem::ReceiverStrategy::complex_only;
    auto cell_search=modem::oscillator_pattern_search(cell_options.modem);
    cell_search.frequency={2,2,2,2,false};
    cell_search.clock_half_width_ppm=cell_search.requested_clock_half_width_ppm=100;
    cell_search.clock_step_ppm=200;cell_search.limited=false;
    const auto cell_estimate=[&](double frequency,double clock) {
        auto test_channel=channel;test_channel.clock_error_ppm=clock;
        test_channel.frequency_offset_hz=frequency-cell_options.modem.carrier_hz*clock*1e-6;
        return simulation::estimate_with_receiver_search(one,cell_options,true,test_channel,cell_search,1,100,
            simulation::ReceiverWorkMode::hardware_fallback);
    };
    for(const double origin:{32.,-32.}) {
        cell_search.hypotheses={{origin-2,-100},{origin+2,100}};
        for(const auto [frequency,clock]:{std::pair{origin-2,-100.},std::pair{origin+2,100.}}) {
            const auto actual=cell_estimate(frequency,clock);
            check(actual.carrier_in_search&&actual.clock_in_search&&!actual.confidence_available&&
                !actual.reference_probability_available,
                "translated supplied RF/clock cell lost its actual supported input truth");
            check(actual.carrier_search_half_width_hz==2&&actual.requested_carrier_search_half_width_hz==2,
                "explicit RF cells were widened or their local radii were relabeled");
        }
        for(const auto frequency:{0.,origin}) {
            const auto gap=cell_estimate(frequency,0);
            check(!gap.carrier_in_search&&!gap.clock_in_search,
                "explicit translated cells manufactured coverage at zero or in a frequency gap");
        }
        const auto mismatch=cell_estimate(origin-2,100);
        const auto clock_outside=cell_estimate(origin-2,300);
        check(mismatch.carrier_in_search&&!mismatch.clock_in_search&&
            clock_outside.carrier_in_search&&!clock_outside.clock_in_search,
            "separate RF and clock marginals were substituted for actual joint cells");
        for(const auto endpoint:{origin-3,origin-1}) {
            const auto edge=cell_estimate(endpoint,0);
            check(edge.carrier_in_search&&edge.clock_in_search,"closed RF cell endpoint was dropped");
            const auto outside=cell_estimate(std::nextafter(endpoint,endpoint<origin-2?-INFINITY:INFINITY),0);
            check(!outside.carrier_in_search&&!outside.clock_in_search,
                "next representable frequency outside a cell was rounded into coverage");
        }
    }
    // A large input origin must also constrain projection-bin geometry. Use
    // the original runtime constructor rule, independent of the scalar radius.
    for(const double origin:{1024.,-1024.}) {
        cell_search.hypotheses={{origin-2,-100},{origin+2,100}};
        const auto projected=cell_estimate(origin-2,-100);
        const auto actual_bin=modem::pattern_projection_bin_samples(cell_options.modem,std::abs(origin)+2);
        check(actual_bin<modem::pattern_projection_bin_samples(cell_options.modem,2)&&
            projected.execution_projection_bin_samples==actual_bin,
            "explicit catalogue projection used local radius instead of actual input offsets");
    }
    const auto corrected=simulation::estimate_with_receiver_search(one,options,true,channel,
        modem::helper_reference_search(c,.15,2000,0,0).catalogue,1,100,simulation::ReceiverWorkMode::hardware_fallback);
    check(corrected.frequency_rate_hypotheses==applied.hypotheses.size()&&
        corrected.requested_clock_search_half_width_ppm==300&&
        !corrected.confidence_available&&!corrected.reference_probability_available,
        "conditional work used different pairs, removed LF or manufactured a reception probability");
    // Qualified presence must not replace a more precise ordinary RF prior.
    // Retain the exact old RF/LF/UTC cells and q=0; measured M selects raw cells
    // through F ~= M+(D-H)*b, never through the corrected zero-center strip.
    auto gps=c;gps.oscillator_search->rf.accuracy_ppm=.00001;
    for(const auto correction:{0.,modem::utc_transmit_rate_limit(gps)}) {
        const auto baseline=modem::oscillator_pattern_search(gps,correction);
        const auto held=modem::helper_reference_search(gps,.5,2000,.25,correction,
            modem::HelperReferenceClockRelation::common_corrected_source);
        check(held.original_prior_retained && held.catalogue.hypotheses==baseline.hypotheses &&
            held.catalogue.frequency.count==baseline.frequency.count &&
            held.catalogue.frequency.requested_half_width_hz==baseline.frequency.requested_half_width_hz &&
            held.catalogue.clock_step_ppm==baseline.clock_step_ppm && held.catalogue.limited==baseline.limited,
            "weak helper widened/recentered original GPSDO RF/LF/UTC catalogue");
        near(held.residual_center_hz,.25,"raw-cell measurement center was replaced with corrected zero");
        modem::PatternSearch raw;modem::apply_helper_reference_search(
            modem::helper_reference_search(gps,.5,2000,.25,correction,
                modem::HelperReferenceClockRelation::common_corrected_source),raw);
        const modem::PatternHelperFrequencyWindow measured{.25,.5,2000-gps.carrier_hz,
            *raw.admitted_clock_cell_half_width_ppm,{}};
        check(raw.hypotheses==baseline.hypotheses,"prior-preserving apply changed original cells");
        const auto rf=gps.oscillator_search->rf_shift_hz*gps.oscillator_search->rf.accuracy_ppm*gps.oscillator_search->margin*1e-6;
        const auto a=gps.oscillator_search->lf.accuracy_ppm*gps.oscillator_search->margin*1e-6;
        for(double lf:{-a,0.,a})for(double utc:{-correction,0.,correction})for(double r:{-rf,0.,rf}) {
            const auto b=(1+lf)*(1+utc)-1,frequency=gps.carrier_hz*b+r;
            const auto actual_helper=2000*b+r;
            if(std::abs(actual_helper-.25)>.5)continue; // Same measured closed interval, no assumed TX truth.
            bool covered=false;
            for(std::size_t i=0;i<raw.hypotheses.size();++i) {
                const auto pair=raw.hypotheses[i];
                covered|=std::abs(b*1e6-pair.clock_error_ppm)<=baseline.clock_step_ppm/2+1e-8 &&
                    std::abs(frequency-pair.frequency_offset_hz)<=baseline.frequency.step_hz/2+1e-10 &&
                    measured.retains(raw.hypotheses,i);
            }
            check(covered,"raw GPSDO/helper intersection discarded an admitted physical 2D cell");
        }
    }
    // Shared RF error lives in its clock-correlated on-air carrier term.
    // A zero independent residual must not disable strong HF assistance.
    auto shared=c;shared.oscillator_search->reference=modem::OscillatorReference::shared_radio;
    shared.oscillator_search->lf={100,.5};shared.oscillator_search->rf={100,.5};
    const auto strong_shared=modem::helper_reference_search(shared,.02,5062.5,0,0,
        modem::HelperReferenceClockRelation::common_corrected_source);
    check(!strong_shared.original_prior_retained && !strong_shared.catalogue.limited &&
        strong_shared.catalogue.hypotheses.size()<modem::oscillator_pattern_search(shared).hypotheses.size(),
        "shared-radio HF crystal prior incorrectly disabled strong helper correction");
    auto shared_gps=shared;shared_gps.oscillator_search->rf={.0000001,.001};
    const auto weak_shared=modem::helper_reference_search(shared_gps,.5,5062.5,.1,0,
        modem::HelperReferenceClockRelation::common_corrected_source);
    check(weak_shared.original_prior_retained &&
        weak_shared.catalogue.hypotheses==modem::oscillator_pattern_search(shared_gps).hypotheses,
        "weak shared-radio helper widened the tighter joint GPSDO RF/clock prior");
    // Basis choice is latched: a later better observation may narrow the raw
    // cells, but cannot move an accepted owner into another PCM/NCO convention.
    const auto later=modem::helper_reference_search(gps,1e-6,2000,.1,0,
        modem::HelperReferenceClockRelation::common_corrected_source,true);
    check(later.original_prior_retained && later.catalogue.hypotheses==modem::oscillator_pattern_search(gps).hypotheses,
        "improved precision silently changed the latched raw input basis");
    const auto existing=modem::helper_reference_search(gps,10,2000,0,0,
        modem::HelperReferenceClockRelation::common_corrected_source,false);
    check(!existing.original_prior_retained,"existing corrected owner was snapped back to raw q=0");
    struct OriginalHelperInputView {
        std::uint64_t id=0,budget_serial=0;std::uint32_t physical_rate=0,logical_rate=0;
        double helper_hz=0,clock_center=0,clock_lower=0,clock_upper=0;
        double correction_hz=0,translated_helper_hz=0;bool reference_passthrough=false;
        helper::CaptureBand input_band{},payload_output_band{};bool conjugate=false;
    };
    static_assert(sizeof(OriginalHelperInputView)==sizeof(live::detail::HelperInputView));
    static_assert(alignof(OriginalHelperInputView)==alignof(live::detail::HelperInputView));
    // Use the current two-tone placement. The former 2-kHz single-tone
    // placeholder puts its new auxiliary in this DATA passband.
    helper::Options helper_options;helper_options.power_w=40;helper_options.data_power_w=4;
    const auto held_pilot=helper::plan(helper_options,helper::placement_band(gps),24000).frequency_hz;
    helper::Reference weak_reference;weak_reference.frequency_valid=true;weak_reference.sample_rate=24000;
    weak_reference.frequency_hz=held_pilot+.25;weak_reference.frequency_uncertainty_hz=.5;
    live::detail::HelperInputFrontend held_frontend(
        live::detail::HelperInputFrontend::reservation_bound(24000,gps.sample_rate),1);
    try {held_frontend.admit_realtime(gps,held_pilot,weak_reference,24000,11990,0);}
    catch(const Error& e){throw Error(std::string("prior-preserving frontend: ")+e.what());}
    check(held_frontend.active() && held_frontend.original_prior_retained(),
        "detected weak helper paused a valid tighter ordinary prior");
    std::array<float,4096> silence{};
    held_frontend.push(silence,0);
    auto first_blocks=held_frontend.take(100000,weak_reference);
    check(!first_blocks.empty() && first_blocks.front().view->original_prior_retained &&
        first_blocks.front().view->correction_hz==0,"weak reference injected an NCO correction");
    const auto immutable_view=first_blocks.front().view;
    weak_reference.frequency_hz=held_pilot+.1;weak_reference.frequency_uncertainty_hz=1e-6;
    check(held_frontend.track_frequency(weak_reference,held_pilot),"later precision interrupted prior-preserving PCM");
    held_frontend.push(silence,silence.size());
    auto next_blocks=held_frontend.take(100000,weak_reference);
    check(!next_blocks.empty() && next_blocks.front().view==immutable_view &&
        next_blocks.front().view->correction_hz==0,"improved measurement changed raw PCM phase/basis");
    // Equal precision can be useful outside the nominal GPSDO location. The
    // measured interval chooses the input basis once; it does not remove any
    // original LF/UTC alternative or rebase the accepted raw owner above.
    auto tight=gps;
    const auto gpsdo=tuning::oscillator_model(tuning::parse_oscillator_preset("gpsdo-xo"));
    tight.oscillator_search->lf=tight.oscillator_search->rf=gpsdo;
    const auto pilot=held_pilot;constexpr double radius=.415663;
    for(const auto utc:{0.,modem::utc_transmit_rate_limit(tight)}) {
        const auto original=modem::oscillator_pattern_search(tight,utc);
        const auto contact=radius+original.frequency.requested_half_width_hz+
            std::abs(pilot-tight.carrier_hz)*original.requested_clock_half_width_ppm*1e-6;
        for(const auto center:{0.,contact,-contact})
            check(modem::helper_reference_prefers_original_prior(tight,radius,pilot,utc,
                modem::HelperReferenceClockRelation::common_corrected_source,center),
                "compatible/contacting GPSDO interval discarded the tighter original basis");
        for(const auto center:{2.17,-2.17}) {
            check(!modem::helper_reference_prefers_original_prior(tight,radius,pilot,utc,
                modem::HelperReferenceClockRelation::common_corrected_source,center),
                "contradicted GPSDO location retained impossible raw RF cells");
            const auto translated=modem::helper_reference_search(tight,radius,pilot,0,utc,
                modem::HelperReferenceClockRelation::common_corrected_source,false);
            check(translated.catalogue.requested_clock_half_width_ppm>=original.requested_clock_half_width_ppm &&
                translated.catalogue.clock_half_width_ppm>=original.clock_half_width_ppm,
                "GPSDO retranslation discarded original LF/UTC coverage");
            auto observed=weak_reference;observed.frequency_hz=pilot+center;
            observed.frequency_uncertainty_hz=radius;
            live::detail::HelperInputFrontend frontend(
                live::detail::HelperInputFrontend::reservation_bound(24000,tight.sample_rate),1);
            try {frontend.admit_realtime(tight,pilot,observed,24000,11990,0,utc);}
            catch(const Error& e){throw Error(std::string("translated GPSDO frontend: ")+e.what());}
            check(frontend.active() && !frontend.original_prior_retained(),
                "frontend retained a GPSDO prior contradicted by its qualified observation");
            frontend.push(silence,0);
            const auto blocks=frontend.take(100000,observed);
            check(!blocks.empty() && !blocks.front().view->reference_passthrough &&
                blocks.front().view->correction_hz==pilot-observed.frequency_hz,
                "out-of-prior pilot did not select the existing translated input basis");
        }
    }
    // A constant-variance contiguous endpoint chain is the explicit ideal
    // forecast, not a claim that the modulated helper reaches this precision.
    constexpr auto cn0=30.020599913279625;
    const auto sigma=std::hypot(.5,.05)*std::numbers::pi/180;
    const auto ideal=helper::steady_tone_estimate(48000,cn0,sigma);
    check(ideal.available,"30 dBHz ideal contiguous forecast declined");
    near(ideal.uncertainty_hz,.0604611302075783,"ideal forecast diverged from runtime endpoint covariance");
    check(ideal.accepted_observations==384&&ideal.observation_seconds==7.7&&
        ideal.reason.find("uninterrupted")!=std::string::npos&&
        ideal.reason.find("zero excess residual")!=std::string::npos&&
        ideal.reason.find("can widen uncertainty")!=std::string::npos,
        "ideal precision lost its uninterrupted/zero-excess scope");
    // Independently sum the tridiagonal phase-difference covariance in closed
    // form: OLS weights are arithmetic, so endpoint squares are
    // w0^2+wlast^2+(n-1)*step^2. No production endpoint loop is reused here.
    const auto thermal_only=helper::steady_tone_estimate(48000,cn0,0);
    const auto n=static_cast<double>(thermal_only.accepted_observations);
    const auto dt=thermal_only.block_seconds;
    const auto mean=-(n+1)*dt/2+1./48000;
    const auto tt=dt*dt*n*(n*n-1)/12;
    const auto first=1/n+mean*(n-1)*dt/(2*tt);
    const auto step=-mean*dt/tt,last=first+(n-1)*step;
    const auto endpoint_square=first*first+last*last+(n-1)*step*step;
    const auto v=thermal_only.noise_bandwidth_hz/(2*std::pow(10.,cn0/10));
    const auto tau=2*std::numbers::pi;
    near(thermal_only.uncertainty_hz,.005+3*std::sqrt(v*endpoint_square/(tau*tau*dt*dt)),
        "ideal thermal variance differs from independent OLS covariance");
    const auto louder=helper::steady_tone_estimate(48000,cn0+10,sigma);
    const auto more_process=helper::steady_tone_estimate(48000,cn0,2*sigma);
    const auto lower_rate=helper::steady_tone_estimate(8000,cn0,sigma);
    const auto higher_rate=helper::steady_tone_estimate(96000,cn0,sigma);
    check(louder.available&&more_process.available&&louder.uncertainty_hz<ideal.uncertainty_hz&&
        more_process.uncertainty_hz>ideal.uncertainty_hz&&ideal.uncertainty_hz>thermal_only.uncertainty_hz,
        "ideal precision lost monotonic CNO or phase-process scaling");
    // These exact 20 ms sample rates preserve observation dates; increasing the
    // Hann sample count slightly reduces its equivalent noise bandwidth.
    check(lower_rate.available&&higher_rate.available&&
        lower_rate.noise_bandwidth_hz>ideal.noise_bandwidth_hz&&
        ideal.noise_bandwidth_hz>higher_rate.noise_bandwidth_hz&&
        lower_rate.uncertainty_hz>ideal.uncertainty_hz&&
        ideal.uncertainty_hz>higher_rate.uncertainty_hz&&
        lower_rate.observation_seconds==ideal.observation_seconds&&
        higher_rate.observation_seconds==ideal.observation_seconds,
        "ideal rate scaling changed physical aperture or Hann bandwidth");
    const auto level_rejected=helper::steady_tone_estimate(48000,24,sigma);
    const auto unwrap_rejected=helper::steady_tone_estimate(48000,cn0,3.);
    check(!level_rejected.available&&level_rejected.reason.find("level gate")!=std::string::npos&&
        !unwrap_rejected.available&&unwrap_rejected.reason.find("following gate")!=std::string::npos,
        "ideal forecast bypassed real-time level or unwrap gate");
    const auto strong=helper::steady_tone_estimate(48960,49.7403126773,.5*std::numbers::pi/180);
    const auto weaker=helper::steady_tone_estimate(48960,40,.5*std::numbers::pi/180);
    const auto weak=helper::steady_tone_estimate(24000,20.7609125906,.007071*std::numbers::pi/180);
    check(strong.available&&weaker.available&&strong.uncertainty_hz<weaker.uncertainty_hz&&
        strong.observation_seconds<=8&&strong.observation_seconds>=7.6,
        "conditional tone covariance lost CNO, support or process scaling");
    check(!weak.available&&!weak.reason.empty(),"weak expected tone bypassed actual following level gate");
    std::cout.precision(17);
    std::cout<<"ideal_uninterrupted_U_hz="<<ideal.uncertainty_hz
        <<" observation_seconds="<<ideal.observation_seconds<<'\n';
    std::cout<<"helper reference catalogue/ordinary work/conditional covariance PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

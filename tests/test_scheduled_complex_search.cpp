#include "datapump/scheduled_complex_search.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/sched_hop.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

using namespace datapump;
namespace {
void check(bool value,const char* text){if(!value)throw Error(text);}
modem::Config configuration(unsigned multiplier=10) {
    modem::Config c;c.sample_rate=2000;c.carrier_hz=500;c.bandwidth_hz=50;
    c.spreading_factor=256;c.scramble=true;
    Bytes key(32);for(std::size_t i=0;i<key.size();++i)key[i]=static_cast<std::uint8_t>(3+7*i);
    c.data_key.emplace(key);c.stream_epoch=8312;
    auto schedule=std::make_shared<modem::SchedHopPlan>();schedule->time_multiplier=multiplier;schedule->group_hops=2;
    schedule->clock_error_seconds=schedule->peer_clock_error_seconds=schedule->region_seconds=schedule->audio_error_seconds=0;
    schedule->ramp_seconds=.01;c.scheduled_hopping=schedule;
    c.oscillator_search.emplace();c.oscillator_search->lf={100,.5};c.oscillator_search->rf={.0001,.005};
    c.oscillator_search->margin=3;c.oscillator_search->reference=modem::OscillatorReference::independent_audio;
    return c;
}
modem::PatternSearch search(const modem::Config& c,double correction=0) {
    modem::PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=.025123;
    const auto bank=modem::oscillator_pattern_search(c,correction);s.hypotheses=bank.hypotheses;
    s.admitted_frequency_half_width_hz=bank.frequency.requested_half_width_hz;
    s.admitted_clock_half_width_ppm=bank.requested_clock_half_width_ppm;
    s.complex_frequency_coverage=s.complex_clock_coverage=!bank.limited && !bank.timing_correction_unavailable;
    return s;
}
void original_overload_is_unchanged() {
    // These vectors come from the unchanged baseline formula and existing
    // oscillator regression contract, independently of either new overload.
    auto pinned=configuration();
    const auto crystal=modem::oscillator_pattern_search(pinned);
    check(crystal.hypotheses.size()==15 && crystal.frequency.count==15 &&
          crystal.hypotheses.front()==modem::PatternFrequencyRateHypothesis{0,0} &&
          crystal.hypotheses[13]==modem::PatternFrequencyRateHypothesis{-.15,-300} &&
          crystal.hypotheses[14]==modem::PatternFrequencyRateHypothesis{.15,300},
          "ordinary crystal bank retains pinned exact original endpoints/count");
    pinned.oscillator_search->lf.accuracy_ppm=.0001;
    const auto gps=modem::oscillator_pattern_search(pinned);
    check(gps.hypotheses.size()==3 && gps.frequency.count==3 &&
          gps.hypotheses.front()==modem::PatternFrequencyRateHypothesis{0,0} &&
          gps.hypotheses[1].frequency_offset_hz==-gps.frequency.requested_half_width_hz &&
          gps.hypotheses[2].clock_error_ppm==gps.requested_clock_half_width_ppm &&
          std::abs(gps.frequency.requested_half_width_hz-1.5e-7)<1e-22,
          "ordinary GPS bank retains pinned narrow three-lane geometry");
    for(const auto multiplier:{10u,100u,1000u,10000u})for(const auto accuracy:{.0001,100.}) {
        auto c=configuration(multiplier);c.oscillator_search->lf.accuracy_ppm=accuracy;
        for(const auto correction:{0.,modem::utc_transmit_rate_limit(c)}) {
            const auto old=modem::oscillator_pattern_search(c,correction);
            const auto same=modem::oscillator_pattern_search(c,correction,{});
            check(old.hypotheses==same.hypotheses && old.frequency.count==same.frequency.count &&
                  old.frequency.step_hz==same.frequency.step_hz && old.clock_step_ppm==same.clock_step_ppm &&
                  old.frequency.requested_half_width_hz==same.frequency.requested_half_width_hz &&
                  old.limited==same.limited && old.timing_correction_unavailable==same.timing_correction_unavailable,
                  "zero resolution must preserve exact original oscillator policy");
        }
    }
}
void physical_resolution_and_caps() {
    for(const auto accuracy:{.0001,100.})for(const auto multiplier:{10u,100u,1000u,10000u}) {
        auto c=configuration(multiplier);c.oscillator_search->lf.accuracy_ppm=accuracy;
        const auto original=search(c);
        const auto plan=modem::scheduled_complex_search(c,original);
        check(plan.applicable,"split scheduled geometry must be applicable");
        check(plan.geometry.physical_aperture_samples>modem::symbol_sample_count(c),
              "physical aperture must include intervening null intervals");
        check(plan.search.hypotheses.size()<=modem::maximum_pattern_frequency_rate_hypotheses,
              "refinement must remain bounded by finite pair cap");
        check(plan.search.hypotheses.size()>=original.hypotheses.size() &&
              std::equal(original.hypotheses.begin(),original.hypotheses.end(),plan.search.hypotheses.begin()),
              "refinement must preserve every original exact pair and its ordering");
        if(plan.complete_coverage) {
            check(plan.geometry.frequency_grid_qualified && plan.geometry.clock_grid_qualified && !plan.limited,
                  "complete flag must require physical phase and timing resolution");
            check(plan.search.complex_frequency_coverage==true && plan.search.complex_clock_coverage==true,
                  "complete bank must carry explicit joint-domain certificates");
        } else check(plan.limited && !plan.reason.empty(),"finite cap must report unavailable complete coverage");
        if(accuracy==.0001)check(plan.complete_coverage,"GPS residual must fit bounded high-multiplier bank");
        if(accuracy==100 && multiplier==10000)check(plan.limited,"extreme crystal aperture must expose finite cap");
        std::cout<<"grid multiplier="<<multiplier<<" lf_ppm="<<accuracy
                 <<" physical_samples="<<plan.geometry.physical_aperture_samples
                 <<" original_pairs="<<plan.original_hypotheses<<" refined_pairs="<<plan.refined_hypotheses
                 <<" complete="<<plan.complete_coverage<<" limited="<<plan.limited<<'\n';
    }
}
void coupled_rf_and_utc() {
    for(const auto reference:{modem::OscillatorReference::independent_audio,modem::OscillatorReference::shared_radio})
      for(const auto sideband:{modem::OscillatorSideband::upper,modem::OscillatorSideband::lower}) {
        auto c=configuration(100);auto& oscillator=*c.oscillator_search;
        oscillator.lf.accuracy_ppm=.0001;oscillator.rf_shift_hz=30000000;
        oscillator.reference=reference;oscillator.sideband=sideband;
        c.frequency_search_multiplier=1.2345;
        const auto correction=modem::utc_transmit_rate_limit(c);
        const auto original=search(c,correction);
        const auto plan=modem::scheduled_complex_search(c,original,correction);
        check(plan.complete_coverage,"translated GPS domain and UTC margin must retain refined complete coverage");
        check(std::equal(original.hypotheses.begin(),original.hypotheses.end(),plan.search.hypotheses.begin()),
              "fractional widening/UTC unions must preserve original exact lanes");
        check(plan.geometry.admitted_frequency_half_width_hz>=*original.admitted_frequency_half_width_hz &&
              plan.geometry.admitted_clock_half_width_ppm>=*original.admitted_clock_half_width_ppm,
              "refinement cannot shrink the declared translated or UTC domain");
    }
    auto c=configuration(10);const auto correction=modem::utc_transmit_rate_limit(c);
    const auto original=search(c,correction);
    const auto missing=modem::scheduled_complex_search(c,original,0);
    check(missing.limited && !missing.complete_coverage && missing.search.hypotheses==original.hypotheses,
          "omitting a required UTC allowance must fail coverage without losing original pairs");
}
void explicit_diagnostics_and_rectangle() {
    auto c=configuration(10);c.oscillator_search.reset();
    modem::PatternSearch s;s.start_offset_seconds=.12345;s.start_uncertainty_seconds=.001234;
    s.hypotheses={{0,0},{.000001,.25},{-.000001,-.25}};
    s.admitted_frequency_half_width_hz=0;s.admitted_clock_half_width_ppm=0;
    const auto diagnostic=modem::scheduled_complex_search(c,s);
    check(diagnostic.applicable && !diagnostic.complete_coverage && !diagnostic.refined &&
          diagnostic.search.hypotheses==s.hypotheses,
          "zero-domain diagnostic pairs must remain exact without invented uncertainty");
    s.admitted_frequency_half_width_hz=.002345;s.admitted_clock_half_width_ppm=1.2345;
    const auto rectangle=modem::scheduled_complex_search(c,s);
    check(rectangle.complete_coverage && rectangle.reason.find("rectangular")!=std::string::npos,
          "explicit nonoscillator domain may use bounded complete rectangular overcoverage");
    check(std::equal(s.hypotheses.begin(),s.hypotheses.end(),rectangle.search.hypotheses.begin()),
          "rectangular overcoverage must preserve diagnostic reference alternatives");
    s.admitted_frequency_half_width_hz=100;s.admitted_clock_half_width_ppm=10000;
    const auto capped=modem::scheduled_complex_search(c,s);
    check(capped.limited && !capped.complete_coverage && capped.search.hypotheses==s.hypotheses,
          "unaffordable rectangular refinement must retain reference and expose its cap");
    s.hypotheses.clear();s.frequency_offsets_hz={0,-.000001,.000001};s.clock_errors_ppm={-.25,.25};
    s.couple_clock_to_carrier=true;s.uncoupled_frequency_count=1;
    s.admitted_frequency_half_width_hz=.002345;s.admitted_clock_half_width_ppm=1.2345;
    const auto coupled=modem::scheduled_complex_search(c,s);
    check(coupled.complete_coverage && coupled.original_hypotheses==6,
          "explicit carrier-coupled clock banks must retain all original alternatives");
    std::size_t position=0;
    for(const auto ppm:s.clock_errors_ppm)for(std::size_t f=0;f<s.frequency_offsets_hz.size();++f) {
        const auto ratio=f>=s.uncoupled_frequency_count?1+s.frequency_offsets_hz[f]/c.carrier_hz:1.;
        const auto expected=ratio==1?ppm:(ratio*(1+ppm*1e-6)-1)*1e6;
        check(coupled.search.hypotheses[position++]==modem::PatternFrequencyRateHypothesis{s.frequency_offsets_hz[f],expected},
              "explicit paired flattening must match original receiver clock/coupling arithmetic");
    }
}
void finalized_bank_correction_identity() {
    auto c=configuration(3);c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;c.dsss_factor=10;
    auto schedule=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);
    schedule->group_hops=1;c.scheduled_hopping=schedule;
    c.oscillator_search->rf_shift_hz=30000000;
    const auto initial=modem::scheduled_complex_search(c,search(c),0);
    check(initial.complete_coverage,"correction-identity fixture lacks a finalized bank");
    const auto same=modem::scheduled_complex_search(c,initial.search,0);
    check(same.complete_coverage && same.search.hypotheses==initial.search.hypotheses && !same.refined,
          "same-domain finalized bank unnecessarily rebuilt its hypotheses");
    const auto changed=modem::scheduled_complex_search(c,initial.search,.0003);
    const auto expected=modem::oscillator_pattern_search(c,.0003);
    check(changed.complete_coverage && changed.refined &&
          *changed.search.admitted_frequency_half_width_hz>=expected.frequency.requested_half_width_hz &&
          *changed.search.admitted_clock_half_width_ppm>=expected.requested_clock_half_width_ppm &&
          std::equal(initial.search.hypotheses.begin(),initial.search.hypotheses.end(),changed.search.hypotheses.begin()),
          "finalized bank silently reused coverage for a larger UTC correction domain");
    const auto omitted=modem::scheduled_complex_search(c,changed.search,0);
    check(omitted.limited && !omitted.complete_coverage && omitted.search.hypotheses==changed.search.hypotheses,
          "finalized bank silently accepted an omitted UTC correction allowance");
    for(const auto invalid:{-.0001,.002,std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected=false;try{(void)modem::scheduled_complex_search(c,initial.search,invalid);}catch(const Error&){rejected=true;}
        check(rejected,"finalized-bank shortcut bypassed correction validation");
    }
}
void final_joint_clock_cells() {
    for(const auto multiplier:{3U,5U}) for(const auto group:{1U,3U}) {
        if(multiplier==3 && group==3)continue;
        auto c=configuration(multiplier);c.sample_rate=6120;c.carrier_hz=1500;c.bandwidth_hz=36;
        c.spreading_factor=256;c.dsss_factor=10;
        auto schedule=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);schedule->group_hops=group;
        c.scheduled_hopping=schedule;c.oscillator_search->lf.accuracy_ppm=100;
        c.oscillator_search->rf_shift_hz=30000000;
        const auto original=search(c);
        const auto plan=modem::scheduled_complex_search(c,original);
        check(plan.complete_coverage,"moderate final joint bank lost declared complete coverage");
        const auto bounded=modem::pattern_scheduled_complex_geometry(c,plan.search,512);
        check(!bounded.supported && bounded.reason.find("preparation exceeds DSP workspace")!=std::string::npos,
              "joint metadata silently exceeded its constructor allowance");
        auto step=std::numeric_limits<double>::infinity();
        for(const auto pair:plan.search.hypotheses)if(pair.frequency_offset_hz>0)step=std::min(step,pair.frequency_offset_hz);
        // Independent physical truth: a frequency just below the first cell
        // edge, RF residual -9mHz, hence the required LF clock at center0.
        const auto truth=(step/2-1e-6+.009)/1500*1e6;
        check(truth>0 && truth<*plan.search.admitted_clock_half_width_ppm &&
              truth<=plan.geometry.clock_cell_radius_ppm,
              "joint strip truth is outside the scheduled null/END clock certificate");
        const auto expanded=modem::scheduled_complex_search(c,original,.0003);
        check(expanded.complete_coverage,"moderate UTC-expanded joint union lost complete coverage");
        const auto old=*std::max_element(original.hypotheses.begin(),original.hypotheses.end(),
            [](const auto& a,const auto& b){return a.frequency_offset_hz<b.frequency_offset_hz;});
        auto next=std::numeric_limits<double>::infinity();
        for(const auto pair:expanded.search.hypotheses)if(pair.frequency_offset_hz>old.frequency_offset_hz)
            next=std::min(next,pair.frequency_offset_hz);
        check(std::isfinite(next),"UTC test lacks its neighboring refined frequency");
        const auto physical=(old.frequency_offset_hz+next)/2-1e-10;
        const auto actual_clock=(physical+.009)/1500*1e6;
        auto nearest=std::numeric_limits<double>::infinity();
        for(const auto pair:expanded.search.hypotheses)
            if(std::abs(pair.frequency_offset_hz-physical)<=expanded.geometry.frequency_step_hz/2)
                nearest=std::min(nearest,std::abs(actual_clock-pair.clock_error_ppm));
        check(actual_clock<=*expanded.search.admitted_clock_half_width_ppm &&
              nearest<=expanded.geometry.clock_coverage_radius_ppm &&
              std::abs(actual_clock-old.clock_error_ppm)<=expanded.geometry.clock_cell_radius_ppm,
              "UTC-expanded joint union received an invalid inherited clock certificate");
    }
    // A blanket max(old,new) would unnecessarily reject these nested grids.
    for(const auto multiplier:{3U,5U}) {
        auto c=configuration(multiplier);c.sample_rate=6000;c.carrier_hz=50;c.bandwidth_hz=120;
        c.spreading_factor=960;c.oscillator_search->margin=2;c.oscillator_search->rf_shift_hz=0;
        auto schedule=std::make_shared<modem::SchedHopPlan>(*c.scheduled_hopping);schedule->group_hops=1;c.scheduled_hopping=schedule;
        const auto original=search(c);const auto plan=modem::scheduled_complex_search(c,original);
        check(plan.complete_coverage && plan.geometry.clock_cell_radius_ppm<100 &&
              plan.geometry.clock_cell_radius_ppm<=plan.geometry.clock_step_ppm/2*(1+1e-12),
              "exact nested3/5 grid retained the obsolete larger clock cell");
        check(std::equal(original.hypotheses.begin(),original.hypotheses.end(),plan.search.hypotheses.begin()),
              "joint recertification changed original pair ordering");
    }
}
void very_low_carrier_timing_resolution() {
    auto c=configuration(10);c.carrier_hz=.005;c.bandwidth_hz=.001;c.sample_rate=64;c.spreading_factor=8;
    const auto original=search(c);
    const auto plan=modem::scheduled_complex_search(c,original);
    check(plan.applicable && (!plan.complete_coverage || plan.geometry.clock_grid_qualified),
          "very low carrier cannot certify timing merely from a fine phase grid");
    check(plan.search.hypotheses.size()<=modem::maximum_pattern_frequency_rate_hypotheses,
          "low carrier clock refinement remains bounded");
}
}
int main() {
    try {original_overload_is_unchanged();physical_resolution_and_caps();coupled_rf_and_utc();
        explicit_diagnostics_and_rectangle();finalized_bank_correction_identity();final_joint_clock_cells();very_low_carrier_timing_resolution();
        std::cout<<"scheduled complex search tests passed\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

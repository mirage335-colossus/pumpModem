#include "application.hpp"
#include "controller.hpp"
#include "document_layout.hpp"
#include "link_planner_model.hpp"
#include "link_planner_page.hpp"
#include "datapump/correlation_experiment.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_search.hpp"
#include "datapump/simulation_estimate.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string_view>
#include <thread>
#include <tuple>

using namespace datapump;
using namespace datapump::gui;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
void near(double actual,double expected,const char* message) {
    check(std::isfinite(actual)&&std::abs(actual-expected)<=1e-10*std::max(1.,std::abs(expected)),message);
}
planner::Inputs example() {
    planner::Inputs inputs;
    inputs.path_loss_db=170; // Preserve the independent weak-link reference budget.
    inputs.options.modem=tuning::resolve(3600,-8,tuning::PatternMode::auto_pattern,false,1500).config;
    return inputs;
}
void flatten(const ui::DocumentNode& node,std::vector<const ui::DocumentNode*>& nodes) {
    nodes.push_back(&node);for(const auto& child:node.children)flatten(child,nodes);
}
std::vector<const ui::DocumentNode*> nodes(const ui::DocumentNode& root) {
    std::vector<const ui::DocumentNode*> result;flatten(root,result);return result;
}
bool contains_text(const ui::DocumentNode& root,std::string_view text) {
    const auto flat=nodes(root);
    return std::any_of(flat.begin(),flat.end(),[&](const auto* node){return node->text.find(text)!=std::string::npos;});
}
void independent_reference_values() {
    const auto inputs=example();
    const auto short_bit=planner::build(inputs);
    check(short_bit.available&&short_bit.automatic_mode&&short_bit.observer_available,
          "The 3.6 kHz / 1.5 kHz short-bit example must be available");
    // Independent sampled anchors: ceil(10^((18-C/N0)/10)*14400) samples.
    // No training symbol fits here; the waveform adds 128 tail samples and
    // three seconds of suppression, without adding any payload bits.
    near(short_bit.bit_seconds,398.1072222222222,"-8 dB-Hz bit duration changed");
    near(short_bit.send_seconds,401.1161111111111,"One-bit example acquired framing or lost waveform tails");
    near(short_bit.finish_seconds,799.2233333333334,"One full absent symbol must follow the short-bit burst");
    near(short_bit.observer_ratio,2928.958076042251,"Short-bit observer/receiver time anchor changed");
    near(short_bit.occupied_bandwidth_hz,2250,"Shaped bandwidth must use the sampled chip rate");
    near(short_bit.low_audio_hz,375,"SSB lower edge changed");
    near(short_bit.high_audio_hz,2625,"SSB upper edge changed");
    near(short_bit.received_dbm,-167,"3 dBm less 170 dB must give -167 dBm");
    near(short_bit.actual_cn0_db_hz,-3,"Link power must be compared against the per-Hz noise floor");
    near(short_bit.margin_db,5,"Actual-link margin must compare against the selected target");
    auto weak_inputs=inputs;weak_inputs.target_db_hz=-23;
    const auto weak=planner::build(weak_inputs);
    check(weak.available&&weak.observer_available,"The weak example must retain its analytical estimate");
    near(weak.bit_seconds,12589.254166666668,"-23 dB-Hz must take approximately 3.5 hours per bit");
    near(weak.send_seconds,12592.263055555557,"Weak example did not preserve its exact one-bit endpoint");
    near(weak.finish_seconds,25181.517222222225,"Weak example completed without a whole absent symbol");
    near(weak.observer_ratio,92617.28706819766,"Weak observer/receiver time anchor changed");
    check(short_bit.receiver_status!=weak.receiver_status,"Free-running crystal limit did not distinguish the two examples");
    auto lpi_inputs=inputs;lpi_inputs.target_db_hz=23;
    const auto lpi_example=planner::build(lpi_inputs);
    check(lpi_example.available&&lpi_example.observer_available,"Positive 23 dB LPI example must be available");
    // At +23 dB the automatic profile is 1024 chips; settling uses four
    // additional symbols and absence uses eleven complete failed symbols.
    near(lpi_example.bit_seconds,.5688888888888889,"Positive LPI example must retain its subsecond bit");
    near(lpi_example.send_seconds,5.8533333333333335,"Positive LPI example lost its complete waveform overhead");
    near(lpi_example.finish_seconds,12.11111111111111,"Positive LPI example lost its complete-symbol silence check");
    near(lpi_example.observer_ratio,4.333069879228238,"Positive LPI observer/receiver time anchor changed");
}
void exact_geometry_and_physical_finish() {
    auto inputs=example();const auto one=planner::build(inputs);
    inputs.wire_bits=3;const auto three=planner::build(inputs);
    check(three.available,"Exact three-bit draft must be plannable");
    near(three.send_seconds-one.send_seconds,2*one.bit_seconds,"Three bits were padded to a byte or given per-bit overhead");
    near(three.observer_ratio,one.observer_ratio,"Draft size must not change normalized observer time");
    inputs.wire_bits=1216;const auto interval=planner::build(inputs);
    check(interval.available,"Existing 192+1024-bit fixed interval geometry must remain plannable");
    near(interval.send_seconds-one.send_seconds,1215*one.bit_seconds,"Current-draft interval bits were changed by planning");
    // A subsecond bit needs several consecutive fully scored absent symbols;
    // an hours-long bit still needs a whole additional symbol, never six seconds.
    inputs.wire_bits=1;
    for(const auto target:{21.,-8.,-23.,-33.}) {
        inputs.target_db_hz=target;const auto model=planner::build(inputs);
        check(model.available,"Physical-finish reference scenario unavailable");
        const auto absent=model.finish_seconds-model.send_seconds;
        const auto symbols=std::ceil(6/model.bit_seconds);
        near(absent,symbols*model.bit_seconds,"Physical finish must cover six seconds in whole failed symbols");
        check(absent+1e-8>=6&&absent-model.bit_seconds<6,
              "Physical finish did not use the first complete absence interval covering six seconds");
    }
}
void timing_milestones() {
    auto inputs=example();const auto model=planner::build(inputs);
    check(model.fast_target&&model.day_target,"Automatic planning must offer the one-second and one-day milestones");
    // The last automatic code below one second has 1024 chips at 1800 chips/s.
    // The one-day boundary instead uses continuous integration rounded to PCM.
    near(*model.fast_target,20.449725484634943,"Fast milestone ignored the automatic code-length step");
    near(*model.day_target,-31.365137424788934,"One-day milestone changed its 18 dB energy reference");
    for(const auto& milestone:{std::pair{*model.fast_target,1.},std::pair{*model.day_target,86400.}}) {
        inputs.target_db_hz=milestone.first;const auto selected=planner::build(inputs);
        check(selected.available&&selected.bit_seconds<=milestone.second,
              "A milestone must select an available sampled duration at or below its limit");
        inputs.target_db_hz=milestone.first-.00001;const auto weaker=planner::build(inputs);
        check(weaker.available&&weaker.bit_seconds>milestone.second,
              "The weak side of a milestone must cross the advertised duration");
    }
    inputs.target_db_hz=*model.fast_target;
    near(planner::build(inputs).bit_seconds,1024./1800,"Fast milestone must preserve its discrete 1024-chip duration");
    inputs.target_db_hz=*model.day_target;
    near(planner::build(inputs).bit_seconds,86400,"Day milestone must select one day per bit");
}
void clock_and_ram_milestones() {
    // Fixed analytical allowances avoid dependence on the host's free memory.
    // Both budgets cover the crystal offset at -8; only 1 GiB covers its FFT
    // workspace. No allocation scales to these planning limits.
    for(const std::size_t mib:{512,1024}) {
        auto inputs=example();inputs.options.dsp_workspace_bytes=mib*1024*1024;
        inputs.dsp_workspace_percent=75;
        const auto selected=planner::build(inputs);
        check(selected.available&&selected.clock_search_supported&&
              selected.receiver_workspace_supported==(mib==1024),
              "Clock coverage alone must not qualify a plan whose selected DSP allowance cannot hold its search");
        if(mib==512)check(selected.receiver_status.find("Wide RX search exceeds RAM (75%)")!=std::string::npos,
                          "RAM failure must identify the selected workspace percentage");
        check(selected.clock_target&&selected.clock_limit_reason=="RAM",
              "Memory-constrained clock milestone must identify RAM as its limiting condition");
        inputs.target_db_hz=*selected.clock_target;
        const auto milestone=planner::build(inputs);
        check(milestone.available&&milestone.clock_search_supported&&milestone.receiver_workspace_supported,
              "Returned clock/RAM milestone must itself fit the selected search and byte allowance");
    }
}
planner::Inputs gpsdo_islands() {
    planner::Inputs inputs;
    inputs.options.modem=tuning::resolve(1,-47,tuning::PatternMode::auto_pattern,false,1500).config;
    inputs.options.dsp_workspace_bytes=4096ULL*1024*1024;
    // Fixed residual-frequency stress geometry, independent of the GPSDO presets.
    inputs.channel.clock_error_ppm=.1;
    inputs.channel.phase_noise_degrees_per_sqrt_second=.5;
    inputs.target_db_hz=-47;
    return inputs;
}
modem::Config planned_config(const planner::Inputs& inputs,double target) {
    const std::array targets{target};
    return tuning::receive_profiles(inputs.options.modem,targets,inputs.mode,inputs.options.key.has_value()).front();
}
simulation::Estimate independently_check_receiver(const planner::Inputs& inputs,double target) {
    auto options=inputs.options;options.modem=planned_config(inputs,target);
    transfer::Estimate one;one.wire_bits=1;
    one.total_seconds=static_cast<double>(modem::symbol_sample_count(options.modem))/options.modem.sample_rate;
    return simulation::estimate(one,options,true,inputs.channel);
}
void sampled_clock_ram_islands() {
    auto inputs=gpsdo_islands();
    const auto nominal=planned_config(inputs,-47);
    const auto search=modem::default_pattern_frequency_search(nominal);
    check(nominal.sample_rate==6000&&modem::pattern_chip_samples(nominal)==12000&&
          modem::symbol_sample_count(nominal)==18973665962ULL&&
          modem::pattern_projection_bin_samples(nominal,search.half_width_hz)==2,
          "The exact -47 dB target must retain its independent sample-rounded geometry");
    const auto selected=planner::build(inputs);
    check(selected.available&&selected.clock_search_supported&&!selected.receiver_workspace_supported,
          "A GPSDO's clock coverage must not hide the exact -47 dB target's RAM gap");
    struct Anchor {std::uint64_t samples,bin;bool clock,ram;};
    for(const auto anchor:{Anchor{18973661999ULL,1,true,false},
                              {18973662000ULL,6000,true,true},
                              {18973662001ULL,1,true,false},
                              {18973668000ULL,6000,true,true},
                              {20479998000ULL,6000,true,true},
                              {20480004000ULL,6000,false,true}}) {
        // A point inside each sample's rounding interval, calculated without
        // the planner's grid search. Its neighboring samples have different
        // exact projection bins even though their displayed dB values agree.
        inputs.target_db_hz=static_cast<double>(18-10*std::log10((static_cast<long double>(anchor.samples)-.5L)/6000));
        const auto config=planned_config(inputs,inputs.target_db_hz);
        const auto frequencies=modem::default_pattern_frequency_search(config);
        check(modem::symbol_sample_count(config)==anchor.samples&&
              modem::pattern_projection_bin_samples(config,frequencies.half_width_hz)==anchor.bin,
              "One-sample GPSDO island fixtures no longer have their independent projection-bin geometry");
        const auto receiver=independently_check_receiver(inputs,inputs.target_db_hz);
        const auto model=planner::build(inputs);
        check(receiver.carrier_in_search==anchor.clock&&receiver.receiver_workspace_supported==anchor.ram&&
              model.available&&model.clock_search_supported==anchor.clock&&model.receiver_workspace_supported==anchor.ram,
              "Planner support must agree with the independent estimator on both usable islands and adjacent gaps");
    }
    check(selected.clock_target.has_value(),"GPSDO planning must locate a checked long-duration edge despite narrow RAM islands");
    const auto edge=planned_config(inputs,*selected.clock_target);
    // The capped frequency bank covers +/-512/T Hz. A .1 ppm error at
    // 1500 Hz permits 3,413,333 whole seconds, but not 3,413,334 seconds.
    check(modem::symbol_sample_count(edge)==20479998000ULL&&selected.clock_limit_reason=="Clock",
          "The GPSDO clock milestone stopped before the independently derived last fitting whole-second geometry");
    // A larger allowance admits a finer divisor near the same clock cap.
    // Searching only complete native bins misses this weaker usable island.
    inputs=gpsdo_islands();inputs.options.dsp_workspace_bytes=16384ULL*1024*1024;
    const auto finer_target=static_cast<double>(18-10*std::log10((20479999500.L-.5L)/6000));
    const auto finer=planned_config(inputs,finer_target);
    const auto finer_search=modem::default_pattern_frequency_search(finer);
    const auto receiver=independently_check_receiver(inputs,finer_target);
    check(modem::symbol_sample_count(finer)==20479999500ULL&&
          modem::pattern_projection_bin_samples(finer,finer_search.half_width_hz)==1500&&
          receiver.carrier_in_search&&receiver.receiver_workspace_supported,
          "The 16 GiB fixture must independently admit the finer 1500-sample projection grid");
    const auto larger=planner::build(inputs);
    check(larger.clock_target&&modem::symbol_sample_count(planned_config(inputs,*larger.clock_target))>=20479999500ULL,
          "The clock/RAM search missed a weaker fitting divisor island near the clock cap");
    const auto discovered=independently_check_receiver(inputs,*larger.clock_target);
    check(discovered.carrier_in_search&&discovered.receiver_workspace_supported,
          "The larger-budget edge must independently fit both receiver checks");
}
void checked_clock_ram_navigation() {
    auto inputs=gpsdo_islands();
    auto current=planner::build(inputs);
    const auto verify=[&](const planner::Model& model,const std::optional<double>& target,bool stronger) {
        check(target.has_value()&&std::isfinite(*target)&&*target>=-200&&*target<=200&&
              (stronger?*target>model.inputs.target_db_hz:*target<model.inputs.target_db_hz),
              "Clock/RAM navigation must advertise a finite target strictly in its requested direction");
        const auto receiver=independently_check_receiver(model.inputs,*target);
        check(receiver.carrier_in_search&&receiver.receiver_workspace_supported,
              "A navigation step advertised a target in a clock or RAM gap");
    };
    verify(current,current.stronger_fit_target,true);
    check(*current.stronger_fit_target>-46.01&&*current.stronger_fit_target<-45.99,
          "Stronger navigation should advance about one dB instead of crawling between adjacent sample endpoints");
    verify(current,current.weaker_fit_target,false);
    check(*current.weaker_fit_target>-48&&current.clock_target&&
          modem::symbol_sample_count(planned_config(inputs,*current.weaker_fit_target))==
          modem::symbol_sample_count(planned_config(inputs,*current.clock_target)),
          "Weaker navigation must retain the last fitting edge when a full one-dB step would skip it");
    inputs.target_db_hz=*current.weaker_fit_target;current=planner::build(inputs);
    check(!current.weaker_fit_target,"The weakest checked clock edge must disable further weaker steps");
    for(unsigned step=0;step<4;++step) {
        verify(current,current.stronger_fit_target,true);
        check(*current.stronger_fit_target-current.inputs.target_db_hz>.99,
              "Repeated stronger navigation regressed into one-sample stepping");
        inputs.target_db_hz=*current.stronger_fit_target;current=planner::build(inputs);
        verify(current,current.weaker_fit_target,false);
        check(current.inputs.target_db_hz-*current.weaker_fit_target>.99,
              "Repeated weaker navigation regressed into one-sample stepping");
    }
    inputs.target_db_hz=200;const auto strongest=planner::build(inputs);
    check(strongest.available&&!strongest.stronger_fit_target,"The finite upper target limit must not advertise a stronger step");
    inputs.target_db_hz=-200;const auto unavailable=planner::build(inputs);
    check(!unavailable.available&&!unavailable.stronger_fit_target&&!unavailable.weaker_fit_target,
          "An unrepresentable sampled duration must not advertise unchecked navigation targets");
    inputs=gpsdo_islands();inputs.mode=tuning::PatternMode::pattern_16;
    const auto fixed=planner::build(inputs);
    check(fixed.available&&!fixed.stronger_fit_target&&!fixed.weaker_fit_target,
          "A fixed-duration profile must not advertise target-driven timing navigation");
    // One MiB satisfies the transfer API's 256 KiB minimum, but cannot hold
    // even this rate's fastest expanded receiver bank and retained evidence.
    inputs=gpsdo_islands();inputs.options.dsp_workspace_bytes=1024*1024;
    const auto fastest_receiver=independently_check_receiver(inputs,200);
    check(fastest_receiver.carrier_in_search&&!fastest_receiver.receiver_workspace_supported,
          "The valid no-fit fixture must independently reject even its fastest receiver geometry on RAM");
    const auto no_workspace=planner::build(inputs);
    check(no_workspace.available&&!no_workspace.receiver_workspace_supported&&!no_workspace.clock_target&&
          !no_workspace.stronger_fit_target&&!no_workspace.weaker_fit_target,
          "A bounded search with no affordable receiver geometry must return no unchecked targets");
    inputs.options.dsp_workspace_bytes=1;
    const auto invalid_workspace=planner::build(inputs);
    check(!invalid_workspace.available&&!invalid_workspace.error.empty()&&!invalid_workspace.clock_target&&
          !invalid_workspace.stronger_fit_target&&!invalid_workspace.weaker_fit_target,
          "A budget below the transfer API minimum must be unavailable and advertise no navigation targets");
}
void nearest_usable_targets() {
    auto inputs=gpsdo_islands();
    const auto exact_target=static_cast<double>(18-10*std::log10((18973662000.L-.5L)/6000));
    inputs.target_db_hz=exact_target;
    check(planner::nearest_fit_target(inputs)==exact_target,
          "An already fitting request must retain its exact target and sample endpoint");
    inputs.target_db_hz=-47;
    const auto weaker=planner::nearest_fit_target(inputs);
    check(weaker&&*weaker<inputs.target_db_hz&&
          modem::symbol_sample_count(planned_config(inputs,*weaker))==18973668000ULL,
          "Nearest-fit rounding must select the independently known weaker island next to -47 dB");
    const auto stronger_target=static_cast<double>(18-10*std::log10((18973665000.L-.5L)/6000));
    const auto stronger_receiver=independently_check_receiver(inputs,stronger_target);
    const auto weaker_receiver=independently_check_receiver(inputs,*weaker);
    check(stronger_receiver.carrier_in_search&&stronger_receiver.receiver_workspace_supported&&
          stronger_target>inputs.target_db_hz&&stronger_target-inputs.target_db_hz<inputs.target_db_hz-*weaker&&
          weaker_receiver.carrier_in_search&&weaker_receiver.receiver_workspace_supported,
          "Weaker-first rounding must prefer its requested side even when a fitting stronger island is closer");
    const auto edge=planner::build(inputs).clock_target;
    check(edge.has_value(),"Nearest-fit fallback fixture must have a checked clock edge");
    for(const auto request:{-60.,-100.,-200.}) {
        inputs.target_db_hz=request;const auto fallback=planner::nearest_fit_target(inputs);
        check(fallback&&*fallback>request&&
              modem::symbol_sample_count(planned_config(inputs,*fallback))==
              modem::symbol_sample_count(planned_config(inputs,*edge)),
              "A request with no weaker fit must fall back to the nearest checked stronger edge, including sample overflow");
        const auto receiver=independently_check_receiver(inputs,*fallback);
        check(receiver.carrier_in_search&&receiver.receiver_workspace_supported,
              "Nearest-fit fallback must independently fit its receiver search and memory allowance");
    }
    inputs=gpsdo_islands();inputs.options.dsp_workspace_bytes=1024*1024;
    check(!planner::nearest_fit_target(inputs),"Nearest-fit search must return no target when a valid allowance fits no receiver");
    for(const auto invalid:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN(),201.,-201.}) {
        inputs=gpsdo_islands();inputs.target_db_hz=invalid;
        check(!planner::nearest_fit_target(inputs),"Nonfinite or out-of-range requests must not produce a rounded target");
    }
    inputs=gpsdo_islands();
    check(!planner::nearest_fit_target(inputs,{},planner::ReceiveBanks{})&&
          !planner::nearest_fit_target(inputs,{},planner::ReceiveBanks{true,130}),
          "Invalid explicit receive-bank contexts must not invent an implicit matching receiver");
    const std::array invalid_companion{std::numeric_limits<double>::quiet_NaN()};
    check(!planner::nearest_fit_target(inputs,invalid_companion),
          "An invalid companion receive target must not be omitted when choosing a fit");
}
simulation::Estimate independently_check_banks(const planner::Inputs& inputs,double target,
        std::span<const double> companions,planner::ReceiveBanks banks) {
    auto options=inputs.options;options.modem=planned_config(inputs,target);
    std::vector<double> targets{target};targets.insert(targets.end(),companions.begin(),companions.end());
    std::vector<modem::Config> profiles;
    const auto append=[&](bool keyed,std::size_t count) {
        const auto family=tuning::receive_profiles(inputs.options.modem,targets,inputs.mode,keyed);
        for(std::size_t key=0;key<count;++key)profiles.insert(profiles.end(),family.begin(),family.end());
    };
    if(banks.plaintext)append(false,1);
    if(banks.private_keys)append(true,banks.private_keys);
    transfer::Estimate one;one.wire_bits=1;
    one.total_seconds=static_cast<double>(modem::symbol_sample_count(options.modem))/options.modem.sample_rate;
    return simulation::estimate(one,options,true,inputs.channel,profiles);
}
void nearest_target_shares_receiver_budget() {
    auto inputs=gpsdo_islands();inputs.options.dsp_workspace_bytes=512ULL*1024*1024;
    const std::array companions{-8.,23.};
    const auto alone=planner::nearest_fit_target(inputs,{},planner::ReceiveBanks{true,0});
    const auto shared=planner::nearest_fit_target(inputs,companions,planner::ReceiveBanks{true,0});
    check(alone&&shared&&*shared>*alone,
          "Distinct companion targets must receive their share of the same DSP budget during automatic rounding");
    auto receiver=independently_check_banks(inputs,*alone,companions,{true,0});
    check(receiver.carrier_in_search&&!receiver.receiver_workspace_supported,
          "Companion fixture must independently reject the single-profile endpoint under the shared allowance");
    receiver=independently_check_banks(inputs,*shared,companions,{true,0});
    check(receiver.carrier_in_search&&receiver.receiver_workspace_supported,
          "The rounded target must independently fit its actual companion-profile bank");
    const std::array duplicates{-8.,23.,-8.,23.};
    check(planner::nearest_fit_target(inputs,duplicates,planner::ReceiveBanks{true,0})==shared,
          "Repeated receive targets must not charge duplicate waveform profiles or alter the rounded endpoint");
    const auto companion_copy=companions;
    (void)planner::nearest_fit_target(inputs,companions,planner::ReceiveBanks{true,0});
    check(companions==companion_copy&&inputs.target_db_hz==-47,
          "Rounding one request must not modify its companion targets or the caller's inputs");
    inputs=gpsdo_islands();inputs.options.key.emplace(Bytes(32,0x37));
    inputs.mode=tuning::PatternMode::auto_keystream;
    inputs.options.modem=tuning::resolve(1,-47,inputs.mode,true,1500).config;
    inputs.options.dsp_workspace_bytes=16384ULL*1024*1024;
    const auto one_key=planner::nearest_fit_target(inputs,{},planner::ReceiveBanks{false,1});
    const auto four_keys=planner::nearest_fit_target(inputs,{},planner::ReceiveBanks{false,4});
    check(one_key&&four_keys&&*four_keys>*one_key,
          "Distinct loaded private-key families must share the receiver allowance during rounding");
    receiver=independently_check_banks(inputs,*one_key,{},planner::ReceiveBanks{false,4});
    check(receiver.carrier_in_search&&!receiver.receiver_workspace_supported,
          "Private-key fixture must independently expose the extra bank's RAM cost");
    receiver=independently_check_banks(inputs,*four_keys,{},planner::ReceiveBanks{false,4});
    check(receiver.carrier_in_search&&receiver.receiver_workspace_supported,
          "The private rounded endpoint must fit the independently configured full receive bank");
}
void narrow_band_differential_model() {
    // The two user-reported target/rate combinations, with a fixed allowance
    // independent of this test host's physical RAM. Oscillator class never
    // gates either receiver branch; sample geometry and modeled drift do.
    auto inputs=example();
    inputs.tx_dbm=0;inputs.path_loss_db=200;inputs.noise_density_dbm_hz=-164;
    inputs.target_db_hz=-38.092699609758306;
    inputs.options.modem=tuning::resolve(.01,inputs.target_db_hz,inputs.mode,false,1500).config;
    inputs.options.dsp_workspace_bytes=2ULL*1024*1024*1024;
    inputs.channel.clock_error_ppm=.0001;
    inputs.channel.phase_noise_degrees_per_sqrt_second=.005;
    const auto shorter=planner::build(inputs);
    near(shorter.bit_seconds,409600,"The shorter narrow-band command changed exact bit duration");
    check(shorter.confidence_available && shorter.differential_windows==0 && !shorter.differential_model_available,
          "128 local windows must retain the appropriate older receiver probability");
    check(contains_text(planner_page::build(shorter,900,false,false),"local differential geometry not eligible"),
          "The shorter command must explain why local comparisons are absent from its estimate");
    inputs.target_db_hz=-44.25748830262745;
    inputs.options.modem=tuning::resolve(.01,inputs.target_db_hz,inputs.mode,false,1500).config;
    inputs.channel.phase_noise_degrees_per_sqrt_second=.05;
    const auto longer=planner::build(inputs);
    near(longer.bit_seconds,3276800,"The TCXO narrow-band command changed exact bit duration");
    check(longer.confidence_available && longer.differential_model_available && longer.differential_windows==1024 &&
          longer.differential_window_seconds==3200 && longer.probability_trials==4096 &&
          longer.probability_interval_available && longer.success_probability_low<longer.success_probability &&
          longer.probability_search_approximation && longer.probability_carrier_candidates>0,
          "The TCXO command must include the new detector and expose finite-sampling/search limits");
    const auto page=planner_page::build(longer,900,true,false);
    check(contains_text(page,"1024 windows") && contains_text(page,"included in RX estimate") &&
          contains_text(page,"95% sampling interval") && contains_text(page,"sampling uncertainty only"),
          "Supported local reception must disclose its detector geometry and model uncertainty");
}
void phase_loss_and_receiver_confidence() {
    auto inputs=gpsdo_islands();
    inputs.target_db_hz=static_cast<double>(18-10*std::log10((18973668000.L-.5L)/6000));
    const auto local=planner::build(inputs);
    check(local.available&&local.clock_search_supported&&local.receiver_workspace_supported&&
          local.differential_windows>=256&&local.differential_window_seconds==100&&
          !local.confidence_available&&!local.drift_model_available&&!local.coherent_reference_only,
          "Eligible local phase comparisons must withhold the unsupported receive probability while retaining search facts");
    near(local.phase_coherence_loss_db,17.832566385,"The hobby-GPSDO phase-loss anchor changed");
    const auto local_page=planner_page::build(local,900,true,false);
    check(contains_text(local_page,"RX estimate unavailable")&&
          contains_text(local_page,"Whole-bit phase loss: 17.8 dB")&&
          contains_text(local_page,"Differential detector:")&&contains_text(local_page,"Estimate coverage:")&&
          contains_text(local_page,"256 windows")&&
          !contains_text(local_page,"Coherent-only comparison:"),
          "Long-bit details must describe local windows without presenting the old probability as current");
    inputs.tx_dbm=-100;
    check(!planner::build(inputs).confidence_available,
          "Changing link power must not make an unmodeled local detector probability available");

    // Below the local detector's eligibility threshold, retain the original
    // four-quarter probability checks. Scale diffusion to retain the same
    // phase-loss stress while exercising a shorter complete symbol. More
    // chips retain the high-power fixture's ample independent observations.
    inputs.options.modem.bandwidth_hz=16;
    inputs.target_db_hz=18-10*std::log10(16384.-.5/6000);
    const auto shorter=planned_config(inputs,inputs.target_db_hz);
    const auto seconds=static_cast<double>(modem::symbol_sample_count(shorter))/shorter.sample_rate;
    const auto diffusion_scale=std::sqrt(local.bit_seconds/seconds);
    inputs.channel.phase_noise_degrees_per_sqrt_second=.5*diffusion_scale;
    // Compare actual received C/N0 with the timing target. Fitting the clock
    // and RAM does not supply the energy lost to whole-symbol phase drift.
    inputs.tx_dbm=inputs.target_db_hz+inputs.path_loss_db+inputs.noise_density_dbm_hz;
    const auto weak=planner::build(inputs);
    check(weak.available&&weak.clock_search_supported&&weak.receiver_workspace_supported&&weak.confidence_available&&
          weak.differential_windows==0&&weak.success_probability<.001,
          "A clock/RAM-compatible long symbol must retain its poor modeled reception when phase loss consumes its energy");
    near(weak.phase_coherence_loss_db,17.832566385,"The hobby-GPSDO phase-loss anchor changed");
    check(weak.drift_model_available&&!weak.coherent_reference_only&&weak.section_phase_coherence_loss_db>0&&
          weak.section_phase_coherence_loss_db<weak.phase_coherence_loss_db,
          "Long-pattern probability must identify the combined model and retain finite section phase loss");
    const auto weak_page=planner_page::build(weak,900,false,false);
    check(contains_text(weak_page,"RX estimate: <1%")&&contains_text(weak_page,"Whole-bit phase loss: 17.8 dB")&&
          !contains_text(weak_page,"Extra drift-tolerant gain is not yet estimated")&&
          !contains_text(weak_page,"Coherent-only comparison:")&&
          !contains_text(weak_page,"Meets target")&&!contains_text(weak_page,"Pattern transitions."),
          "The primary planner must show weak reception and phase loss without presenting clock/RAM fit as successful reception");
    for(const auto diffusion:{.5,.05,.005,0.}) {
        inputs.channel.phase_noise_degrees_per_sqrt_second=diffusion*diffusion_scale;
        const auto model=planner::build(inputs);
        const auto retained=simulation::expected_correlation_coherence(model.bit_seconds,diffusion*diffusion_scale,0);
        near(model.phase_coherence_loss_db,-10*std::log10(retained),
             "Planner phase loss must use the complete symbol duration, independently checked by the coherent reference");
        check(model.clock_search_supported&&model.receiver_workspace_supported&&model.confidence_available,
              "Changing phase diffusion must not silently change clock coverage or the sampled RAM geometry");
    }
    inputs.channel.phase_noise_degrees_per_sqrt_second=.5*diffusion_scale;inputs.tx_dbm=3;
    const auto stronger_link=planner::build(inputs);
    check(stronger_link.confidence_available&&stronger_link.success_probability>.999&&
          stronger_link.success_probability>weak.success_probability,
          "The modeled receive probability must use actual link power rather than the target or clock/RAM fit alone");
    near(stronger_link.phase_coherence_loss_db,weak.phase_coherence_loss_db,
         "Extra received power must not erase the modeled oscillator phase loss");
    const auto stronger_page=planner_page::build(stronger_link,900,false,false);
    check(contains_text(stronger_page,"RX estimate: >99%")&&contains_text(stronger_page,"Whole-bit phase loss: 17.8 dB"),
          "The reception headline must respond to actual link power while retaining the same phase loss");
    const auto detailed_page=planner_page::build(weak,900,true,false);
    check(contains_text(detailed_page,"Pattern transitions.")&&contains_text(detailed_page,"four sections")&&
          contains_text(detailed_page,"separate gain and phase")&&contains_text(detailed_page,"local windows")&&
          contains_text(detailed_page,"Supported estimates include all eligible detector scores")&&
          contains_text(detailed_page,"Coherent-only comparison:")&&
          contains_text(detailed_page,"Phase loss within a section:"),
          "Expanded details must explain implemented section fits and retain the coherent comparison");
    inputs.tx_dbm=inputs.target_db_hz+inputs.path_loss_db+inputs.noise_density_dbm_hz;
    // This intermediate phase case has both acquisition and continuation
    // failures; otherwise all continuation trials can pass and one/three-bit
    // probabilities legitimately coincide at the model's finite resolution.
    inputs.channel.phase_noise_degrees_per_sqrt_second=.1*diffusion_scale;inputs.wire_bits=3;
    const auto three=planner::build(inputs);
    auto options=inputs.options;options.modem=planned_config(inputs,inputs.target_db_hz);
    auto channel=inputs.channel;
    channel.snr_db=three.actual_cn0_db_hz-10*std::log10(options.modem.sample_rate/2.);
    const auto transmission=transfer::estimate_binary(Bytes{0,0,0},options);
    const auto independent=simulation::estimate(transmission,options,true,channel);
    near(three.success_probability,independent.success_probability,
         "Planner receive probability must cover the exact current wire-bit count");
    near(three.coherent_success_probability,independent.coherent_success_probability,
         "Planner coherent comparison must come from the same draft's statistical model");
    check(contains_text(planner_page::build(three,900,false,true),"RX estimate (all bits):"),
          "Current-draft reception must label its all-wire-bits probability");
    inputs.wire_bits=1;
    check(three.success_probability<planner::build(inputs).success_probability,
          "A three-bit preview must require all three bits rather than show a one-bit success estimate");
    for(const auto power:{-400.,400.}) {
        inputs.tx_dbm=power;const auto outside=planner::build(inputs);
        check(outside.available&&!outside.confidence_available&&outside.clock_search_supported&&
              outside.receiver_workspace_supported&&std::isfinite(outside.phase_coherence_loss_db),
              "Outside the supported sampled-noise range, hide extrapolated probability while retaining phase and search facts");
    }
}
void gpsdo_phase_does_not_change_clock_coverage() {
    auto inputs=gpsdo_islands();
    inputs.target_db_hz=static_cast<double>(18-10*std::log10((18973668000.L-.5L)/6000));
    std::optional<planner::Model> first;
    double previous_loss=std::numeric_limits<double>::infinity();
    for(const auto name:{"gpsdo-xo","gpsdo-tcxo","gpsdo-ocxo"}) {
        const auto oscillator=tuning::parse_oscillator_preset(name);
        inputs.channel.clock_error_ppm=oscillator.clock_error_ppm;
        inputs.channel.phase_noise_degrees_per_sqrt_second=oscillator.phase_noise_degrees_per_sqrt_second;
        auto model=planner::build(inputs);
        check(model.available&&model.clock_search_supported&&model.receiver_workspace_supported&&model.clock_target,
              "Each locked GPSDO profile must fit the same independently sampled clock/RAM island");
        check(model.phase_coherence_loss_db<previous_loss,
              "The GPSDO phase-sensitivity scenarios must retain their separate coherence assumptions");
        previous_loss=model.phase_coherence_loss_db;
        if(first) {
            check(model.receiver_status==first->receiver_status&&model.clock_target==first->clock_target&&
                  model.clock_limit_reason==first->clock_limit_reason&&
                  model.stronger_fit_target==first->stronger_fit_target&&model.weaker_fit_target==first->weaker_fit_target,
                  "Changing only GPSDO phase diffusion must not alter hard clock/RAM gates or navigation");
        } else first=std::move(model);
    }
    inputs.channel.clock_error_ppm=1;
    const auto explicit_offset=planner::build(inputs);
    check(explicit_offset.available&&!explicit_offset.clock_search_supported&&explicit_offset.receiver_workspace_supported,
          "An explicitly larger residual clock offset must still expose the actual finite frequency-search limit");
}
void separate_link_budget_and_observer_model() {
    auto inputs=example();const auto reference=planner::build(inputs);
    inputs.tx_dbm=50;inputs.path_loss_db=200;inputs.noise_density_dbm_hz=-170;
    const auto different_link=planner::build(inputs);
    check(different_link.available,"A changed illustrative link budget should remain available");
    near(different_link.actual_cn0_db_hz,20,"Power/path/noise arithmetic is not independent of the timing target");
    near(different_link.observer_ratio,reference.observer_ratio,"Actual power must not change the normalized time comparison");
    near(different_link.bit_seconds,reference.bit_seconds,"Power/path/noise edits must not silently retune the preview");
    check(reference.observer_hypothetical,"Public patterns need a hypothetical private-pattern observer estimate");
    inputs.options.key.emplace(Bytes(32,0x37));
    const auto keyed=planner::build(inputs);
    check(keyed.available&&!keyed.observer_hypothetical,"A private-pattern preview must recognize its configured key");
    near(keyed.observer_ratio,reference.observer_ratio,"Key material must not change the normalized observer time");
    check(!reference.inputs.options.key,"Planner build must not mutate the caller's encryption settings");
}
void quantization_fixed_modes_and_limits() {
    auto inputs=example();
    inputs.options.modem.sample_rate=15001;
    const auto quantized=planner::build(inputs);
    check(quantized.available,"Valid nonintegral sampled geometry must remain available");
    near(quantized.bit_seconds,5972006./15001,"Planner duration must be rounded to whole samples");
    near(quantized.occupied_bandwidth_hz,1.25*15001/9,"Planner bandwidth must use ceil-quantized chip samples");
    inputs=example();inputs.mode=tuning::PatternMode::pattern_16;
    const auto fixed=planner::build(inputs);
    check(fixed.available&&!fixed.automatic_mode,"A fixed code length must remain fixed in the planner");
    check(!fixed.fast_target&&!fixed.day_target&&!fixed.clock_target,
          "A fixed-length mode must not offer target-dependent timing milestones");
    near(fixed.bit_seconds,128./14400,"Fixed pattern length was replaced by automatic integration");
    inputs=example();
    const auto bounded=planner::build(inputs);
    check(!bounded.points.empty()&&bounded.points.size()<=1024,"Planner sweep must use bounded storage");
    for(std::size_t index=0;index<bounded.points.size();++index) {
        const auto& point=bounded.points[index];
        check(std::isfinite(point.target_db_hz)&&std::isfinite(point.bit_seconds)&&point.bit_seconds>0,
              "Planner curve exposed a nonfinite or nonpositive duration");
        check(!point.observer_available||(std::isfinite(point.observer_ratio)&&point.observer_ratio>0),
              "Available observer curve exposed a nonfinite ratio");
        if(index)check(point.target_db_hz>bounded.points[index-1].target_db_hz,
                       "Planner targets must remain ordered without duplicate plot positions");
    }
    for(const auto bad:{std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        for(auto member:{&planner::Inputs::target_db_hz,&planner::Inputs::tx_dbm,
                         &planner::Inputs::path_loss_db,&planner::Inputs::noise_density_dbm_hz}) {
            auto invalid=inputs;invalid.*member=bad;const auto rejected=planner::build(invalid);
            check(!rejected.available&&!rejected.error.empty(),"Nonfinite planner input needs an explicit unavailable result");
        }
    }
    inputs.target_db_hz=-200;
    check(!planner::build(inputs).available,"Unrepresentable sampled duration must not appear usable");
    inputs=example();inputs.wire_bits=0;
    check(!planner::build(inputs).available,"An empty bit selection must not invent a one-bit transmission");
    inputs=example();inputs.wire_bits=std::numeric_limits<std::size_t>::max();
    check(!planner::build(inputs).available,"An overflowing draft duration must not wrap the sample counter");
}
void prepare(Controller& controller) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(!controller.estimate()&&std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(controller.estimate().has_value(),"Current-draft estimate was not prepared");
}
void controller_checked_navigation() {
    using F=ui::Field;using C=ui::Command;
    Controller controller;
    controller.edit(F::bandwidth,"1 Hz");controller.edit(F::carrier,"1500 Hz");
    controller.select(F::simulation_oscillator,"gpsdo-xo");controller.edit(F::message,"e");prepare(controller);
    const auto inspection=controller.inspection();
    const auto waveform=controller.estimate()->waveform_samples;
    const auto receive_targets=controller.settings().transfer.receive_targets_db_hz;
    const auto initial=controller.link_plan();
    check(initial->clock_target.has_value(),"Controller GPSDO fixture must have a usable clock/RAM edge");
    controller.activate(C::planner_clock);
    const auto edge=controller.link_plan();
    check(edge->available&&edge->clock_search_supported&&edge->receiver_workspace_supported&&
          !controller.enabled(C::planner_weaker)&&controller.enabled(C::planner_stronger),
          "Controller navigation enablement must reflect its checked edge and available direction");
    for(unsigned read=0;read<256;++read)
        check(controller.link_plan()==edge,"Repeated planner presentation reads must reuse the bounded search result");
    const auto next=edge->stronger_fit_target;
    controller.activate(C::planner_stronger);
    const auto stepped=controller.link_plan();
    check(next&&stepped!=edge&&stepped->inputs.target_db_hz==*next,
          "Stronger action must use the exact checked target advertised by the cached model");
    auto receiver=independently_check_receiver(stepped->inputs,stepped->inputs.target_db_hz);
    check(receiver.carrier_in_search&&receiver.receiver_workspace_supported&&
          controller.inspection()==inspection&&controller.estimate()->waveform_samples==waveform&&
          controller.settings().transfer.receive_targets_db_hz==receive_targets&&controller.message_bytes()==Bytes{'e'},
          "Checked preview navigation must preserve the live receiver and exact existing draft estimate");
    check(controller.enabled(C::planner_weaker)&&stepped->weaker_fit_target,
          "A stronger usable point must offer its checked weaker return path");
    const auto weaker=*stepped->weaker_fit_target;
    controller.activate(C::planner_weaker);
    const auto selected=controller.link_plan();
    check(selected->inputs.target_db_hz==weaker&&selected->clock_search_supported&&selected->receiver_workspace_supported,
          "Weaker action must preserve the exact target used to check its clock/RAM fit");
    controller.activate(C::planner_target);const auto requests=controller.take_services();
    check(requests.size()==1,"Checked navigation target must remain editable through its native prompt");
    controller.complete_service({requests.front().id,false,requests.front().value,{}});
    check(controller.link_plan()->inputs.target_db_hz==weaker,
          "A displayed checked target lost its sample-sensitive precision during prompt round-trip");
    const auto samples=modem::symbol_sample_count(planned_config(selected->inputs,weaker));
    controller.activate(C::planner_apply_short);prepare(controller);
    check(modem::symbol_sample_count(controller.settings().transfer.modem)==samples&&
          std::find(controller.settings().transfer.receive_targets_db_hz.begin(),
                    controller.settings().transfer.receive_targets_db_hz.end(),weaker)!=
                    controller.settings().transfer.receive_targets_db_hz.end()&&
          controller.message_bytes()==Bytes{'e'}&&controller.estimate()->wire_bits==3,
          "Applying a checked GPSDO island must preserve its exact sampled endpoint and three-bit dictionary source");
    controller.close();
}
void controller_target_alignment() {
    using F=ui::Field;
    Controller controller;
    controller.edit(F::bandwidth,"1 Hz");controller.edit(F::carrier,"1500 Hz");
    controller.select(F::simulation_oscillator,"gpsdo-xo");controller.edit(F::short_bits,"00101");
    const auto expected=[&](double requested,double companion) {
        auto input=controller.link_plan()->inputs;input.options=controller.settings().transfer;
        input.target_db_hz=requested;
        const std::array companions{companion};
        const auto result=planner::nearest_fit_target(input,companions,planner::ReceiveBanks{true,0});
        check(result.has_value(),"Controller rounding fixture must have a checked receiver target");return *result;
    };
    const auto short_target=expected(-47,55);
    controller.edit(F::snr,"-47");prepare(controller);
    check(controller.field(F::snr).text=="-47"&&controller.estimate()->wire_bits==5&&
          controller.field(F::short_bits).text=="00101"&&
          modem::symbol_sample_count(controller.settings().transfer.modem)==
          modem::symbol_sample_count(planned_config(controller.link_plan()->inputs,short_target))&&
          std::find(controller.settings().transfer.receive_targets_db_hz.begin(),
                    controller.settings().transfer.receive_targets_db_hz.end(),short_target)!=
                    controller.settings().transfer.receive_targets_db_hz.end(),
          "Weak SNR editing must preserve typed text and exact raw bits while applying the checked timing to TX and RX");
    check((short_target==-47)==controller.field(F::snr).display_text.empty(),
          "An adjusted target must expose its actual value separately from the retained edit buffer");
    const auto long_target=expected(-46,short_target);
    controller.edit(F::long_snr,"-46");prepare(controller);
    const auto short_samples=modem::symbol_sample_count(controller.settings().transfer.modem);
    const auto long_samples=modem::symbol_sample_count(*controller.settings().long_message_modem);
    const auto targets=controller.settings().transfer.receive_targets_db_hz;
    check(controller.field(F::snr).text=="-47"&&controller.field(F::long_snr).text=="-46"&&
          std::find(targets.begin(),targets.end(),short_target)!=targets.end()&&
          std::find(targets.begin(),targets.end(),long_target)!=targets.end(),
          "Editing a second target must retain the first target's buffer and accepted receiver alignment");
    controller.edit(F::carrier,"1500.000 Hz");prepare(controller);
    check(controller.field(F::snr).text=="-47"&&controller.field(F::long_snr).text=="-46"&&
          controller.settings().transfer.receive_targets_db_hz==targets&&
          modem::symbol_sample_count(controller.settings().transfer.modem)==short_samples&&
          modem::symbol_sample_count(*controller.settings().long_message_modem)==long_samples,
          "An unrelated configuration refresh must not replace accepted targets with their unrounded edit buffers");
    const auto inspection=controller.inspection();const auto revision=controller.revision();
    const auto plan=controller.link_plan();
    controller.commit_target(F::snr);
    check(std::stod(controller.field(F::snr).text)==short_target&&controller.field(F::snr).display_text.empty()&&
          controller.field(F::long_snr).text=="-46"&&controller.inspection()==inspection&&
          controller.revision()==revision&&controller.link_plan()==plan&&
          controller.settings().transfer.receive_targets_db_hz==targets,
          "Committing a target must canonicalize only its buffer without retuning or discarding prepared estimates");
    controller.commit_target(F::long_snr);
    check(std::stod(controller.field(F::long_snr).text)==long_target&&controller.inspection()==inspection&&
          controller.revision()==revision&&controller.estimate()->wire_bits==5,
          "Committing the long target must preserve its full precision and the exact short raw draft");
    controller.edit(F::receive_snr,"-47, 55");controller.edit(F::carrier,"1500 Hz");
    const auto manual_targets=controller.settings().transfer.receive_targets_db_hz;
    check(manual_targets==std::vector<double>({short_target,55}),
          "The manual RX list must choose the same checked target with its actual companion bank");
    controller.commit_target(F::receive_snr);
    check(tuning::parse_receive_targets(controller.field(F::receive_snr).text).values==manual_targets&&
          controller.field(F::receive_snr).display_text.empty(),
          "Committing an adjusted RX list must expose its exact sampled targets");
    const auto boundary_target=expected(-20,long_target);
    controller.edit(F::snr,"-20");prepare(controller);
    check(controller.field(F::snr).text=="-20"&&
          modem::symbol_sample_count(controller.settings().transfer.modem)==
          modem::symbol_sample_count(planned_config(controller.link_plan()->inputs,boundary_target)),
          "The former -20 dB boundary must use the same clock/RAM alignment as every other target");
    const auto fallback=expected(-200,long_target);
    controller.edit(F::snr,"-200");prepare(controller);
    check(controller.field(F::snr).text=="-200"&&!controller.field(F::snr).display_text.empty()&&
          modem::symbol_sample_count(controller.settings().transfer.modem)==
          modem::symbol_sample_count(planned_config(controller.link_plan()->inputs,fallback))&&
          controller.estimate()->wire_bits==5,
          "An unrepresentable weak request must preserve its edit buffer and use a checked fallback without adding bits");
    controller.select(F::pattern,"pattern-16");controller.edit(F::snr,"-47");prepare(controller);
    check(controller.field(F::snr).text=="-47"&&controller.field(F::snr).display_text.empty()&&
          controller.settings().transfer.modem.integration_seconds==0&&
          controller.settings().transfer.modem.spreading_factor==16&&controller.estimate()->wire_bits==5,
          "Fixed pattern modes must keep their existing unsnapped timing and exact raw-bit path");
    controller.edit(F::message,"quick brown fox!!");prepare(controller);
    check(controller.inspection()->stream_layout&&controller.estimate()->wire_bits==1216,
          "Target rounding must not alter the fixed interval endpoint of a longer source");
    controller.close();
}
void current_draft_and_modem_isolation() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::bandwidth,"3600");controller.edit(F::carrier,"1500");
    controller.edit(F::message,"e");prepare(controller);
    const auto short_target=controller.field(F::snr).text;
    const auto long_target=controller.field(F::long_snr).text;
    const auto receive_targets=controller.settings().transfer.receive_targets_db_hz;
    const auto modem=controller.settings().transfer.modem;
    const auto estimate=controller.estimate();
    controller.activate(C::planner_example_lpi);
    check(controller.link_plan()->inputs.target_db_hz==23&&controller.link_plan()->inputs.wire_bits==1,
          "LPI example must begin with exactly one raw bit");
    controller.activate(C::planner_toggle_draft);
    check(controller.planner_uses_draft()&&controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==3,
          "Current draft must use the fixed dictionary's three-bit e endpoint");
    controller.activate(C::planner_power_100w);controller.activate(C::planner_toggle_details);
    prepare(controller); // Shared link power also refreshes the RX probability.
    check(controller.planner_details()&&controller.link_plan()->inputs.tx_dbm==50,
          "Planner detail and power actions did not update their own preview state");
    check(controller.field(F::snr).text==short_target&&controller.field(F::long_snr).text==long_target&&
          controller.settings().transfer.receive_targets_db_hz==receive_targets&&
          controller.settings().transfer.modem.integration_seconds==modem.integration_seconds&&
          controller.settings().transfer.modem.spreading_factor==modem.spreading_factor&&
          controller.settings().transfer.modem.memory_limit==modem.memory_limit&&
          controller.estimate()->wire_bits==estimate->wire_bits&&
          controller.estimate()->waveform_samples==estimate->waveform_samples&&
          controller.message_bytes()==Bytes{'e'},
          "Planner preview changed the draft, receive profiles, modem settings or transmission geometry");
    controller.edit(F::short_bits,"00101");
    check(!controller.link_plan()->available,"A draft edit must immediately withdraw its stale planner estimate");
    prepare(controller);
    check(controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==5,
          "Accepted raw draft must retain leading zeros and its partial-byte endpoint");
    controller.edit(F::message,std::string(17,'e'));prepare(controller);
    check(controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==controller.estimate()->wire_bits&&
          controller.link_plan()->inputs.wire_bits%1216==0,
          "Long draft planning must use the accepted fixed-interval wire geometry");
    controller.activate(C::planner_toggle_draft);
    check(!controller.planner_uses_draft()&&controller.link_plan()->inputs.wire_bits==1,
          "One-bit selection must not retain the previous draft length");
    controller.close();
}
void failed_draft_estimate_and_recovery() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::bandwidth,"3600");controller.edit(F::carrier,"1500");
    controller.edit(F::message,"e");
    controller.activate(C::planner_target);const auto requests=controller.take_services();
    check(requests.size()==1,"Failure fixture must edit its explicit planner target");
    controller.complete_service({requests.front().id,false,"-129",{}});
    check(controller.enabled(C::planner_apply_short),"The explicit one-bit failure fixture must be representable before applying it");
    controller.activate(C::planner_apply_short);controller.activate(C::planner_example_short);
    // Explicit Apply bypasses dropdown alignment. One bit and its absence fit
    // the sample counter, but the dictionary's exact three-bit draft does not.
    near(controller.settings().transfer.modem.integration_seconds,std::pow(10.,14.7),
         "Failure fixture must retain a valid, exceptionally long symbol");
    controller.activate(C::planner_toggle_draft);
    const auto pending=controller.link_plan();
    check(!pending->available&&pending->error.find("Calculating")!=std::string::npos,
          "Draft planning must show pending preparation before the worker completes");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(controller.link_plan()->error.find("Calculating")!=std::string::npos&&
          std::chrono::steady_clock::now()<deadline) {
        controller.poll();std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const auto failed=controller.link_plan();
    check(!controller.estimate()&&!failed->available&&failed!=pending&&
          failed->error.find("sample counter")!=std::string::npos,
          "Failed preparation must replace the cached pending planner with its actual failure");
    controller.activate(C::planner_toggle_draft);
    check(!controller.planner_uses_draft()&&controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==1,
          "Failed draft preparation must still allow a one-bit preview");
    controller.activate(C::planner_toggle_draft);controller.edit(F::snr,"32");
    check(!controller.link_plan()->available&&controller.link_plan()->error.find("Calculating")!=std::string::npos,
          "A corrected draft must clear the prior preparation failure while recalculating");
    prepare(controller);
    check(controller.link_plan()->available&&controller.link_plan()->inputs.wire_bits==3,
          "Successful corrected preparation must restore the exact current-draft plan");
    controller.close();
}
void selected_workspace_reaches_planner() {
    using F=ui::Field;using C=ui::Command;
    Controller controller({true,true});
    controller.edit(F::bandwidth,"3600");controller.edit(F::carrier,"1500");
    const auto initial=controller.link_plan();
    check(initial->inputs.dsp_workspace_percent==50&&
          initial->inputs.options.dsp_workspace_bytes==controller.settings().transfer.dsp_workspace_bytes,
          "Planner must receive the initial selected workspace percentage and actual byte allowance");
    controller.select(F::dsp_workspace,"ram-75");
    const auto selected=controller.link_plan();
    const auto allowance=controller.settings().dsp_workspace_bytes;
    check(controller.field(F::dsp_workspace).selected=="ram-75"&&selected!=initial&&
          selected->inputs.dsp_workspace_percent==75&&
          selected->inputs.options.dsp_workspace_bytes==allowance&&
          allowance==controller.settings().transfer.dsp_workspace_bytes,
          "Selecting 75% DSP workspace must invalidate the plan and forward its actual configured allowance");
    near(selected->bit_seconds,initial->bit_seconds,"Changing DSP memory must not change the selected bit duration");
    if(selected->clock_target) {
        controller.activate(C::planner_clock);
        const auto milestone=controller.link_plan();
        check(milestone->inputs.target_db_hz==*selected->clock_target&&milestone->available&&
              milestone->clock_search_supported&&milestone->receiver_workspace_supported,
              "A 75% workspace milestone must fit both the clock search and the selected memory allowance");
        controller.activate(C::planner_target);const auto requests=controller.take_services();
        check(requests.size()==1,"Clock/RAM target must remain editable through the normal prompt");
        controller.complete_service({requests.front().id,false,requests.front().value,{}});
        check(controller.link_plan()->inputs.target_db_hz==milestone->inputs.target_db_hz,
              "A clock/RAM boundary must survive its displayed prompt value without changing a sampled endpoint");
        controller.activate(C::planner_apply_short);
        const auto& applied=controller.settings().transfer;
        const auto& config=applied.modem;
        check(std::find(applied.receive_targets_db_hz.begin(),applied.receive_targets_db_hz.end(),
                        milestone->inputs.target_db_hz)!=applied.receive_targets_db_hz.end(),
              "Applying a clock/RAM boundary must retain its exact target in matching receive profiles");
        near(static_cast<double>(modem::symbol_sample_count(config))/config.sample_rate,milestone->bit_seconds,
             "Applying a clock/RAM boundary changed its sampled symbol duration");
    }
    controller.close();
}
void shared_link_budget_without_simulation() {
    using F=ui::Field;using C=ui::Command;
    Controller controller;
    check(controller.field(F::simulation).selected=="no"&&!controller.settings().simulation,
          "A normal GUI must begin with sampled simulation off");
    controller.edit(F::message,"e");controller.edit(F::snr,"0");prepare(controller);
    const auto initial=controller.link_plan();
    near(initial->inputs.tx_dbm,3,"Shared power default must be 3 dBm");
    near(initial->inputs.path_loss_db,120,"Shared path-loss default must be 120 dB");
    check(controller.field(F::link_loss).text=="120 dB","The initial path-loss field must show the accepted default");
    near(initial->inputs.noise_density_dbm_hz,-164,"Shared noise default must be -164 dBm/Hz");
    near(initial->received_dbm,-117,"Default received power must follow the 120 dB path loss");
    near(initial->actual_cn0_db_hz,47,"Default link strength must follow the 120 dB path loss");
    // Use the original weak budget so the following edit crosses a visible
    // probability change instead of comparing two saturated >99.9% labels.
    controller.edit(F::link_loss,"170 dB");prepare(controller);
    const auto initial_confidence=controller.field(F::simulation_confidence).text;
    check(initial_confidence.find('%')!=std::string::npos,
          "RX confidence must be computed from the link budget with Simulation No");
    const auto targets=controller.settings().transfer.receive_targets_db_hz;
    const auto geometry=controller.settings().transfer.modem;
    controller.edit(F::link_power,"100 mW");controller.edit(F::link_loss,"150 dB");
    controller.edit(F::link_noise,"-170 dBm/Hz");prepare(controller);
    const auto edited=controller.link_plan();
    near(edited->inputs.tx_dbm,20,"Editable power units did not reach the planner");
    near(edited->inputs.path_loss_db,150,"Editable path loss did not reach the planner");
    near(edited->inputs.noise_density_dbm_hz,-170,"Editable noise density did not reach the planner");
    near(edited->actual_cn0_db_hz,40,"Shared link inputs did not combine as power minus loss minus noise");
    near(controller.settings().simulation_snr_db,40-10*std::log10(geometry.sample_rate/2.),
         "RX confidence and planner must use the same sampled noise level");
    check(controller.field(F::simulation_confidence).text!=initial_confidence&&
          controller.field(F::simulation_confidence).text.find('%')!=std::string::npos&&
          !controller.settings().simulation&&controller.message_bytes()==Bytes{'e'}&&
          controller.settings().transfer.receive_targets_db_hz==targets&&
          controller.settings().transfer.modem.integration_seconds==geometry.integration_seconds&&
          controller.settings().transfer.modem.spreading_factor==geometry.spreading_factor,
          "Link-budget edits must update confidence while preserving the draft and modem profile");
    controller.activate(C::planner_power_1w);prepare(controller);
    near(controller.link_plan()->inputs.tx_dbm,30,"Planner power preset must update the shared link budget");
    near(controller.settings().simulation_snr_db,50-10*std::log10(geometry.sample_rate/2.),
         "Planner power preset did not update RX confidence's shared link strength");
    check(!controller.field(F::link_power).text.empty(),"Planner power edit must remain visible in the global editable field");
    controller.select(F::simulation,"yes");prepare(controller);
    const auto confidence=controller.field(F::simulation_confidence).text;
    check(controller.settings().simulation,"Simulation Yes did not enable sampled simulation");
    controller.select(F::simulation,"no");prepare(controller);
    check(!controller.settings().simulation&&controller.field(F::simulation_confidence).text==confidence&&
          controller.link_plan()->inputs.tx_dbm==30,
          "Simulation toggle must preserve the shared link and its modeled RX confidence");
    controller.close();
}
void shared_link_controls_visibility() {
    using F=ui::Field;
    Application app(Launch{});
    app.select(F::fast_mode,"robust");
    app.toggle(F::developer_mode,true);
    const auto& screen=ui::console_screen();
    const auto declaration=[&](F field)->const ui::Control& {
        const auto found=std::find_if(screen.begin(),screen.end(),[&](const auto& control){return control.field==field;});
        check(found!=screen.end(),"Shared link control is missing from the native declaration");return *found;
    };
    const auto& simulation=app.field(F::simulation);
    check(simulation.selected=="no"&&simulation.options.size()==2&&
          std::any_of(simulation.options.begin(),simulation.options.end(),[](const auto& option){return option.id=="yes";})&&
          std::any_of(simulation.options.begin(),simulation.options.end(),[](const auto& option){return option.id=="no";}),
          "Simulation must offer the simple Yes/No choices");
    for(const auto& page:ui::pages())for(const auto* mode:{"no","yes"}) {
        app.select_page(page.id);app.select(F::simulation,mode);
        const bool simulated=std::string_view(mode)=="yes";
        for(const auto field:{F::link_power,F::link_loss,F::link_noise}) {
            const auto& control=declaration(field);
            check(control.kind==ui::Kind::text&&control.persistent&&!app.field(field).options.empty()&&
                  app.control(control).visible,
                  "Editable link presets must remain native and visible on every page with either Simulation choice");
        }
        check(app.control(declaration(F::simulation_confidence)).visible,
              "RX confidence must remain visible with either Simulation choice");
        for(const auto field:{F::simulation_cpu_time,F::simulation_gpu_time})
            check(app.control(declaration(field)).visible==simulated,
                  "CPU/GPU computation estimates must appear only with Simulation Yes");
        for(const auto size:{ui::Rect{0,0,ui::min_width,ui::min_height},ui::Rect{0,0,ui::default_width,ui::default_height}}) {
            std::vector<ui::Rect> occupied;
            for(const auto field:{F::simulation,F::link_power,F::link_loss,F::link_noise,
                                 F::simulation_confidence,F::simulation_cpu_time,F::simulation_gpu_time}) {
                const auto& control=declaration(field);if(!app.control(control).visible)continue;
                const auto rect=app.control_layout(control,size.w,size.h).frame;
                check(rect.w>0&&rect.h>0&&rect.x>=0&&rect.y>=0&&rect.x+rect.w<=size.w,
                      "Visible shared link control falls outside the desktop");
                for(const auto& prior:occupied)
                    check(rect.x+rect.w<=prior.x||prior.x+prior.w<=rect.x||
                          rect.y+rect.h<=prior.y||prior.y+prior.h<=rect.y,
                          "Visible link inputs and simulation estimates overlap");
                occupied.push_back(rect);
            }
        }
    }
    app.select(F::simulation,"no");
    for(const auto field:{F::link_power,F::link_loss,F::link_noise}) {
        const auto& options=app.field(field).options;
        app.preset(declaration(field),options.front().id);
        check(!app.field(field).text.empty(),"A native link preset did not populate its editable field");
    }
    app.close();
}
void link_budget_edit_buffers() {
    using F=ui::Field;
    Controller controller;
    controller.edit(F::message,"e");prepare(controller);
    const auto targets=controller.settings().transfer.receive_targets_db_hz;
    const auto value=[&](F field) {
        const auto& inputs=controller.link_plan()->inputs;
        return field==F::link_power?inputs.tx_dbm:
               field==F::link_loss?inputs.path_loss_db:inputs.noise_density_dbm_hz;
    };
    struct Keystroke { const char* text;double accepted; };
    const auto type=[&](F field,std::initializer_list<Keystroke> steps) {
        for(const auto& step:steps) {
            controller.edit(field,step.text);controller.poll();
            check(controller.field(field).text==step.text,
                  "A native keystroke must retain its exact edit buffer instead of replacing it with formatted units");
            near(value(field),step.accepted,"A link-input prefix changed the accepted budget incorrectly");
        }
    };
    // Each successive string is the next native change callback, including
    // the temporarily incomplete unit suffix and a leading minus sign.
    type(F::link_power,{{"",3},{"1",1},{"10",10},{"100",100},{"100 ",100},{"100 W",50}});
    type(F::link_power,{{"",50},{"1",1},{"10",10},{"100",100},{"100 ",100},
                        {"100 m",100},{"100 mW",20}});
    type(F::link_noise,{{"",-164},{"-",-164},{"-1",-1},{"-17",-17},{"-174",-174},
                        {"-174 ",-174},{"-174 d",-174},{"-174 dB",-174},{"-174 dBm",-174},
                        {"-174 dBm/",-174},{"-174 dBm/H",-174},{"-174 dBm/Hz",-174}});
    type(F::link_loss,{{"",120},{"2",2},{"22",22},{"220",220},{"220 ",220},
                       {"220 d",220},{"220 dB",220}});
    prepare(controller);
    const auto accepted=controller.link_plan();
    const auto sampled_snr=controller.settings().simulation_snr_db;
    for(const auto field:{F::link_power,F::link_loss,F::link_noise}) {
        for(const auto* invalid:{"","-","1e","nonsense","nan","inf","1e300"}) {
            controller.edit(field,invalid);controller.poll();
            const auto& pending=controller.link_plan();
            check(controller.field(field).text==invalid&&!pending->available&&pending->error=="Check link inputs"&&
                  pending->inputs.tx_dbm==accepted->inputs.tx_dbm&&
                  pending->inputs.path_loss_db==accepted->inputs.path_loss_db&&
                  pending->inputs.noise_density_dbm_hz==accepted->inputs.noise_density_dbm_hz&&
                  controller.settings().simulation_snr_db==sampled_snr&&
                  controller.field(F::simulation_confidence).text.find("Check link inputs")!=std::string::npos&&
                  controller.field(F::simulation_confidence).text.find('%')==std::string::npos,
                  "Incomplete or invalid link input must retain its buffer and accepted budget while hiding stale confidence");
            check(controller.take_services().empty(),"An unfinished native edit must not open a validation dialog");
        }
    }
    controller.edit(F::link_noise,"-174 dBm/Hz");
    check(controller.link_plan()->available,"A valid link edit must restore planning and clear prior incomplete fields");
    controller.edit(F::link_noise,"-");prepare(controller);
    check(controller.estimate().has_value()&&!controller.link_plan()->available&&
          controller.field(F::simulation_confidence).text.find("Check link inputs")!=std::string::npos&&
          controller.field(F::simulation_confidence).text.find('%')==std::string::npos,
          "An asynchronously completed estimate must not restore stale confidence while a link input is incomplete");
    controller.edit(F::link_noise,"-174 dBm/Hz");prepare(controller);
    check(controller.link_plan()->available&&controller.field(F::simulation_confidence).text.find('%')!=std::string::npos,
          "Finishing a valid link input must restore the planner and RX confidence");
    check(controller.message_bytes()==Bytes{'e'}&&
          controller.settings().transfer.receive_targets_db_hz==targets&&!controller.settings().simulation,
          "Typing link estimates must preserve the source and live receiver targets");
    controller.close();
}
void link_budget_preset_and_dialog_sync() {
    using F=ui::Field;using C=ui::Command;
    Application app(Launch{});
    app.select(F::fast_mode,"robust");
    const auto declaration=[&](F field)->const ui::Control& {
        const auto& screen=ui::console_screen();
        const auto found=std::find_if(screen.begin(),screen.end(),[&](const auto& control){return control.field==field;});
        check(found!=screen.end(),"Editable link preset declaration is missing");return *found;
    };
    const auto prompt=[&](C command) {
        app.activate(command);const auto requests=app.take_services();
        check(requests.size()==1&&requests.front().kind==ui::ServiceKind::prompt,
              "The shared link dialog must use the existing native prompt service");
        return requests.front();
    };
    const auto accepted=[&](C command) {
        const auto request=prompt(command);app.complete_service({request.id,true,{},{}});
        return std::stod(request.value);
    };
    struct Preset { F field;C command;const char* text;double value; };
    for(const auto& preset:{Preset{F::link_power,C::planner_power,"100 mW",20},
                             {F::link_loss,C::planner_loss,"220 dB",220},
                             {F::link_noise,C::planner_noise,"-174 dBm/Hz",-174}}) {
        app.edit(preset.field,"1e");
        check(app.field(preset.field).text=="1e","Application refresh must preserve an incomplete native edit");
        const auto pending=app.document(ui::Page::planner,900);
        check(contains_text(*pending,"Check link inputs")&&!contains_text(*pending,"Received:"),
              "The planner must withhold stale link headlines while a shared field is incomplete");
        app.preset(declaration(preset.field),preset.text);
        check(app.field(preset.field).text==preset.text,"A native preset must replace the incomplete edit buffer");
        near(accepted(preset.command),preset.value,"A native preset did not reach the planner's accepted value");
        check(!contains_text(*app.document(ui::Page::planner,900),"Check link inputs"),
              "A native preset must restore the planner after an incomplete edit");
    }
    app.edit(F::link_power,"4 ");
    app.activate(C::planner_power_1w);
    check(app.field(F::link_power).text=="1 W","A planner preset must synchronize the native edit buffer");
    near(accepted(C::planner_power),30,"A planner preset did not synchronize the shared power dialog");
    for(const auto& entry:{Preset{F::link_power,C::planner_power,"4 W",10*std::log10(4000.)},
                            {F::link_loss,C::planner_loss,"123.5 dB",123.5},
                            {F::link_noise,C::planner_noise,"-180.25 dBm/Hz",-180.25}}) {
        app.edit(entry.field,"-");
        const auto request=prompt(entry.command);app.complete_service({request.id,false,entry.text,{}});
        check(app.field(entry.field).text==entry.text,"A completed planner dialog must synchronize the native edit buffer");
        near(accepted(entry.command),entry.value,"A completed planner dialog did not preserve its accepted numeric value");
    }
    app.close();
}
void one_warning_on_planner_page() {
    Launch launch;launch.simulation=true;Application app(launch);
    app.toggle(ui::Field::developer_mode,true);
    const auto& pages=ui::pages();
    check(pages.size()>1&&pages[0].id==ui::Page::console&&pages[1].id==ui::Page::planner,
          "Link planner must be the second tab immediately after Console");
    const auto& screen=ui::console_screen();
    const auto found=std::find_if(screen.begin(),screen.end(),[](const auto& control) {
        return control.field==ui::Field::lpi_estimate;
    });
    check(found!=screen.end()&&found->persistent,"Current-draft LPI advisory must remain a persistent shared control");
    const auto advisory=app.field(ui::Field::lpi_estimate).text;
    for(const auto& page:ui::pages()) {
        app.select_page(page.id);
        check(app.control(*found).visible==(page.id!=ui::Page::planner),
              "Current-draft advisory must hide only while the planner owns the LPI warning");
    }
    app.select_page(ui::Page::console);
    check(app.control(*found).visible&&app.field(ui::Field::lpi_estimate).text==advisory,
          "Returning to Console must restore the existing advisory without modifying its content");
    app.close();
}
void all_target_dropdowns_and_native_planner_editor() {
    using F=ui::Field;using C=ui::Command;
    Controller controller;
    controller.edit(F::short_bits,"00101");
    auto input=controller.link_plan()->inputs;input.options=controller.settings().transfer;input.target_db_hz=-18;
    const std::array companion{55.};
    const auto expected=planner::nearest_fit_target(input,companion,planner::ReceiveBanks{true,0});
    check(expected&&*expected!=-18,"Above -20 fixture must expose a real free-running clock gap");
    controller.edit(F::snr,"-18");prepare(controller);
    check(controller.field(F::snr).text=="-18"&&!controller.field(F::snr).display_text.empty()&&
          controller.settings().transfer.receive_targets_db_hz==std::vector<double>({*expected,55})&&
          controller.estimate()->wire_bits==5&&controller.field(F::short_bits).text=="00101",
          "Every automatic TX target, including above -20, must select a checked fit without rewriting a raw draft");
    const auto tx_samples=modem::symbol_sample_count(controller.settings().transfer.modem);
    controller.edit(F::receive_snr,"-18, 55");controller.commit_target(F::receive_snr);
    check(controller.settings().transfer.receive_targets_db_hz==std::vector<double>({*expected,55})&&
          tuning::parse_receive_targets(controller.field(F::receive_snr).text).values==
              controller.settings().transfer.receive_targets_db_hz&&
          modem::symbol_sample_count(controller.settings().transfer.modem)==tx_samples,
          "A manual RX list must snap all entries with companions without retuning transmission");
    controller.close();

    Application app({});
    // Planner controls belong to Robust; ordinary launches now default to Fast.
    app.select(F::fast_mode,"robust");
    app.toggle(F::developer_mode,true);
    const auto& screen=ui::console_screen();
    const auto editor=std::find_if(screen.begin(),screen.end(),[](const auto& c){return c.field==F::planner_target;});
    const auto active=std::find_if(screen.begin(),screen.end(),[](const auto& c){return c.field==F::snr;});
    const auto receive=std::find_if(screen.begin(),screen.end(),[](const auto& c){return c.field==F::receive_snr;});
    check(editor!=screen.end()&&active!=screen.end()&&receive!=screen.end()&&editor->kind==ui::Kind::text&&
          !editor->persistent&&editor->document_only&&editor->slot==ui::Slot::none&&std::string_view(editor->label)=="Target SNR (dB-Hz)"&&
          app.field(F::planner_target).options.size()==app.field(F::snr).options.size()&&
          !app.field(F::receive_snr).options.empty(),
          "Planner and receive targets must have actual shared editable preset controls in both adapters");
    for(const auto& page:ui::pages()) {
        app.select_page(page.id);
        check(app.control(*editor).visible==(page.id==ui::Page::planner),
              "The independent planner target editor must appear only on its tab");
    }
    app.select_page(ui::Page::planner);
    const auto document=app.document(ui::Page::planner,900);
    std::vector<const ui::DocumentNode*> nodes;flatten(*document,nodes);
    const auto native=std::find_if(nodes.begin(),nodes.end(),[](const auto* node) {
        return node->kind==ui::DocumentKind::control&&node->control&&node->control->field==F::planner_target;
    });
    check(native!=nodes.end(),"Planner target must be a native control in the scrollable document");
    const auto geometry=ui::document_control_layout(*editor,app.field(F::planner_target),{0,0,240,ui::label_height+28});
    check(geometry.has_label&&geometry.has_suggestions&&geometry.widget.w>200&&geometry.suggestions.w>0&&
          geometry.label.y==geometry.frame.y&&geometry.widget.y==ui::label_height&&
          geometry.widget.y+geometry.widget.h==geometry.frame.h,
          "Inline planner target must include its native label, textbox and presets inside its document bounds");
    const auto short_text=app.field(F::snr).text,long_text=app.field(F::long_snr).text,rx_text=app.field(F::receive_snr).text;
    app.edit(*editor,"-18");
    check(app.field(F::planner_target).text=="-18"&&!app.field(F::planner_target).display_text.empty(),
          "Native planner typing must preserve its buffer while exposing the checked target");
    check(app.submit(*editor,false,false),"Enter must commit the native planner's exact accepted target");
    const auto exact=std::stod(app.field(F::planner_target).text);
    check(exact!=-18&&app.field(F::planner_target).display_text.empty(),
          "Native planner commit must retain full target precision");
    app.edit(*editor,"-");
    check(app.field(F::planner_target).text=="-"&&contains_text(*app.document(ui::Page::planner,900),"Check planner target"),
          "An incomplete planner target must retain the edit and suppress stale calculations");
    check(app.field(F::status).text.find("Planner target must be a number")!=std::string::npos,
          "An incomplete planner number must identify its input error");
    app.edit(*editor,"-8.0");
    check(app.field(F::status).text.find("Planner target must be a number")==std::string::npos,
          "A valid planner target must immediately clear its previous input error");
    app.edit(*editor,"-");
    const auto callsign=std::find_if(screen.begin(),screen.end(),[](const auto& c){return c.field==F::callsign;});
    check(callsign!=screen.end(),"Callsign field missing from notice-ownership fixture");
    app.edit(*callsign,std::string(1,static_cast<char>(0xff)));
    const auto unrelated_notice=app.field(F::status).text;
    app.edit(*editor,"-8.0");
    check(app.field(F::status).text==unrelated_notice,
          "Recovering the planner must preserve a newer unrelated input notice");
    app.edit(*editor,"-");
    app.preset(*editor,"-20");
    check(app.field(F::planner_target).text!="-20"&&app.field(F::planner_target).display_text.empty()&&
          app.field(F::status).text.find("Planner target must be a number")==std::string::npos&&
          app.field(F::snr).text==short_text&&app.field(F::long_snr).text==long_text&&app.field(F::receive_snr).text==rx_text,
          "Planner preset selection must recover, reveal the checked target and preserve active TX/RX settings");
    app.preset(*receive,"-20");
    check(app.field(F::receive_snr).text!="-20"&&app.field(F::receive_snr).display_text.empty()&&
          app.field(F::snr).text==short_text&&app.field(F::long_snr).text==long_text,
          "The RX preset dropdown must snap and commit independently of both transmit targets");
    app.close();
}
void application_prompt_roundtrips() {
    using F=ui::Field;using C=ui::Command;
    Launch launch;launch.simulation=true;Application app(launch);
    app.edit(F::bandwidth,"3600");app.edit(F::carrier,"1500");app.edit(F::message,"e");
    const auto short_target=app.field(F::snr).text,long_target=app.field(F::long_snr).text;
    const auto receive_targets=app.field(F::receive_snr).text;
    const auto document=app.document(ui::Page::planner,900);
    check(document&&contains_text(*document,"Link planner")&&app.document(ui::Page::planner,900)==document,
          "Shared planner document must remain available and retain its unchanged presentation identity");
    const auto prompt=[&](C command) {
        app.activate(command);const auto requests=app.take_services();
        check(requests.size()==1&&requests.front().kind==ui::ServiceKind::prompt,
              "Planner editor must use one native prompt service");
        return requests.front();
    };
    const auto value=[&](C command) {
        const auto request=prompt(command);app.complete_service({request.id,true,{},{}});return request.value;
    };
    const auto edit=[&](C command,const std::string& entered) {
        const auto request=prompt(command);app.complete_service({request.id,false,entered,{}});
    };
    app.activate(C::planner_example_lpi);check(value(C::planner_target)=="23","Positive 23 dB LPI example did not reach the shared facade");
    const auto lpi_document=app.document(ui::Page::planner,900);
    check(lpi_document!=document&&contains_text(*lpi_document,"5.85 sec"),
          "Changing the target must replace the shared document with the positive LPI example's real duration");
    app.activate(C::planner_toggle_details);
    check(contains_text(*app.document(ui::Page::planner,900),"Model limits")&&
          contains_text(*app.document(ui::Page::planner,900),"Quick references"),
          "Model details action did not expose its assumptions and references");
    app.activate(C::planner_example_short);check(value(C::planner_target)=="-8","Short example action did not reach the shared facade");
    edit(C::planner_target,"-12.5");check(value(C::planner_target)=="-12.5","Planner target prompt did not round-trip");
    edit(C::planner_power,"20");
    check(value(C::planner_power)=="20 dBm"&&app.field(F::link_power).text=="100 mW",
          "A 20 dBm power entry must retain precise prompt units and display as 100 mW in the shared field");
    edit(C::planner_loss,"150");check(value(C::planner_loss)=="150","Planner path-loss prompt did not round-trip");
    edit(C::planner_noise,"-170");check(value(C::planner_noise)=="-170","Planner noise-density prompt did not round-trip");
    for(const auto command:{C::planner_target,C::planner_power,C::planner_loss,C::planner_noise}) {
        const auto before=value(command);
        for(const auto* invalid:{"nan","inf","1e300","nonsense"}) {
            edit(command,invalid);check(value(command)==before,"Rejected planner input changed its prior accepted value");
        }
        const auto request=prompt(command);app.complete_service({request.id,true,"42",{}});
        check(value(command)==before,"Cancelling a planner prompt changed the accepted value");
    }
    check(app.field(F::snr).text==short_target&&app.field(F::long_snr).text==long_target&&
          app.field(F::receive_snr).text==receive_targets&&app.field(F::message).text=="e",
          "Preview prompt edits changed live targets or draft text");
    app.activate(C::planner_apply_short);
    check(app.field(F::snr).text=="-12.5"&&app.field(F::long_snr).text==long_target&&
          app.field(F::receive_snr).text.find("-12.5")!=std::string::npos,
          "Explicit short-target apply did not use the existing matching receive-target update");
    edit(C::planner_target,"-18");app.activate(C::planner_apply_long);
    check(app.field(F::snr).text=="-12.5"&&app.field(F::long_snr).text=="-18"&&
          app.field(F::receive_snr).text.find("-18")!=std::string::npos,
          "Explicit long-target apply changed the wrong transmit profile");
    app.close();
}
void receiver_overlay_and_cpu_status() {
    planner::Model model;model.inputs=example();model.available=true;
    model.clock_search_supported=model.receiver_workspace_supported=true;
    model.bit_seconds=model.send_seconds=60;model.finish_seconds=120;
    model.points={{25,1,0,false},{0,60,0,false},{-35,86400,0,false}};
    model.one_bit_cpu_available=true;model.one_bit_cpu_seconds=30;
    for(const auto& [ratio,label,tone]:std::array{
        std::tuple{.2,"CPU estimate: headroom",ui::DocumentTone::positive},
        std::tuple{.75,"CPU estimate: limited headroom",ui::DocumentTone::caution},
        std::tuple{1.,"CPU estimate: slower than real time",ui::DocumentTone::negative}}) {
        model.cpu_realtime_ratio=ratio;
        const auto page=planner_page::build(model,900,false,false);
        const auto flat=nodes(page);
        check(std::any_of(flat.begin(),flat.end(),[&](const auto* node){return node->text==label&&node->tone==tone;}),
              "CPU headroom must retain a readable status as well as its green/yellow/red semantic color");
    }
    model.receiver_workspace_supported=false;
    const auto unsupported=planner_page::build(model,900,false,false);
    check(contains_text(unsupported,"CPU estimate unavailable")&&!contains_text(unsupported,"CPU estimate: headroom"),
          "An unsupported receiver must not advertise CPU feasibility");
    model.receiver_workspace_supported=true;
    const auto render=[&](const planner::Model& input,PixelFormat format,std::string_view name="planner/bit-time") {
        const auto page=planner_page::build(input,900,false,false);
        const auto flat=nodes(page);
        const auto plot=std::find_if(flat.begin(),flat.end(),[&](const auto* node){return node->plot_name==name;});
        check(plot!=flat.end(),"Time/receive comparison needs its stable shared plot");
        BitmapImage result(319,167,format);
        (*plot)->plot.paint(full_bitmap_request(319,167,format==PixelFormat::mono1,format==PixelFormat::rgb24),
            [&](unsigned x,unsigned y,PixelBlock pixels){result.blit(x,y,pixels);});
        return result;
    };
    for(const auto format:{PixelFormat::mono1,PixelFormat::gray8,PixelFormat::rgb24}) {
        model.receive_points.clear();const auto empty=render(model,format);
        model.receive_points={{25,.1,true,true,true},{0,0,false,false,false},{-35,.9,true,true,true}};
        const auto gap=render(model,format);
        check(gap.pixels()!=empty.pixels(),"Supported RX islands must remain visible as points beside gaps");
        const auto stride=pixel_row_bytes(319,format);
        for(unsigned y=0;y<167;++y)for(unsigned x=130;x<141;++x) {
            const auto begin=y*stride+(format==PixelFormat::mono1?x/8:format==PixelFormat::rgb24?3*x:x);
            const auto bytes=format==PixelFormat::rgb24?3u:1u;
            for(unsigned component=0;component<bytes;++component)
                check(gap.pixels()[begin+component]==empty.pixels()[begin+component],
                      "RX overlay must not bridge an unavailable Clock/RAM region");
        }
        model.receive_points[1]={0,.5,true,true,true};
        const auto connected=render(model,format);
        check(connected.pixels()!=empty.pixels(),"The RX curve must remain visible in color, grayscale and monochrome");

        model.cpu_points.clear();const auto cpu_empty=render(model,format,"planner/cpu-pace");
        model.cpu_points={{25,.1,true},{0,0,false},{-35,10,true}};
        const auto cpu_gap=render(model,format,"planner/cpu-pace");
        check(cpu_gap.pixels()!=cpu_empty.pixels(),"Supported CPU points must remain visible beside unavailable regions");
        for(unsigned y=0;y<167;++y)for(unsigned x=130;x<141;++x) {
            const auto begin=y*stride+(format==PixelFormat::mono1?x/8:format==PixelFormat::rgb24?3*x:x);
            for(unsigned component=0;component<(format==PixelFormat::rgb24?3u:1u);++component)
                check(cpu_gap.pixels()[begin+component]==cpu_empty.pixels()[begin+component],
                      "CPU curve must not bridge an unavailable Clock/RAM region");
        }
        model.cpu_points[1]={0,1,true};
        check(render(model,format,"planner/cpu-pace").pixels()!=cpu_gap.pixels(),
              "CPU pace must be a visible line in every supported pixel format");
    }
}
void estimate_warning_thresholds() {
    planner::Model model;model.inputs=example();model.available=true;
    model.clock_search_supported=model.receiver_workspace_supported=true;
    model.confidence_available=model.one_bit_confidence_available=model.observer_available=true;
    model.bit_seconds=model.send_seconds=60;model.finish_seconds=120;
    const auto tone_for=[&](const ui::DocumentNode& page,std::string_view prefix) {
        const auto flat=nodes(page);
        const auto found=std::find_if(flat.begin(),flat.end(),[&](const auto* node){return node->text.starts_with(prefix);});
        check(found!=flat.end(),"Expected an estimate label in the planner");
        return (*found)->tone;
    };
    for(const auto width:{220.f,900.f})for(const auto bits:{1u,3u})for(const auto reference:{false,true}) {
        model.inputs.wire_bits=bits;model.coherent_reference_only=reference;
        for(const auto probability:{0.,.79,std::nextafter(.8,0.),.8,.81,1.}) {
            model.success_probability=model.one_bit_success_probability=probability;
            const auto page=planner_page::build(model,width,false,bits>1);
            check(tone_for(page,reference?"RX reference":"RX estimate")==
                      (probability<.8?ui::DocumentTone::negative:ui::DocumentTone::accent),
                  "RX values must turn red strictly below 80%, including all-bit and reference estimates");
            check(tone_for(page,"┄ 1-bit RX")==
                      (probability<.8?ui::DocumentTone::negative:ui::DocumentTone::comparison),
                  "The one-bit percentage must use the same strict 80% threshold");
        }
        for(const auto ratio:{1.,7.,std::nextafter(8.,0.),8.,9.}) {
            model.observer_ratio=ratio;
            const auto page=planner_page::build(model,width,false,bits>1);
            const auto flat=nodes(page);
            const auto heading=std::find_if(flat.begin(),flat.end(),[](const auto* node){return node->text=="Observer / receiver time";});
            check(heading!=flat.end()&&heading+1!=flat.end()&&(*(heading+1))->tone==
                      (ratio<8?ui::DocumentTone::negative:ui::DocumentTone::accent),
                  "Observer values must turn red strictly below 8x, before display rounding");
        }
    }
    model.confidence_available=false;model.one_bit_success_probability=.79;
    const auto first_bit_only=planner_page::build(model,900,false,true);
    check(tone_for(first_bit_only,"RX estimate unavailable")!=ui::DocumentTone::negative&&
          tone_for(first_bit_only,"┄ 1-bit RX")==ui::DocumentTone::negative,
          "An unsupported draft must preserve its separately supported first-bit estimate");
    model.confidence_available=model.one_bit_confidence_available=model.observer_available=false;
    model.success_probability=model.one_bit_success_probability=model.observer_ratio=0;
    const auto unavailable=planner_page::build(model,900,false,false);
    check(tone_for(unavailable,"RX estimate unavailable")!=ui::DocumentTone::negative&&
          tone_for(unavailable,"┄ 1-bit RX")!=ui::DocumentTone::negative,
          "Unavailable RX estimates must not inherit a numerical red warning");
    check(tone_for(unavailable,"Outside model range")==ui::DocumentTone::negative,
          "An unavailable observer estimate must be red");
    for(const bool available:{false,true})for(const double ratio:{-1.,0.,
        std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        model.observer_available=available;model.observer_ratio=ratio;
        check(tone_for(planner_page::build(model,900,false,false),"Outside model range")==ui::DocumentTone::negative,
              "Invalid observer values must be red even when their availability flag is set");
    }
    const auto detailed=planner_page::build(model,900,true,false);
    check(!contains_text(unavailable,"Red indicators.")&&contains_text(detailed,"below 80%")&&
          contains_text(detailed,"below 8×")&&contains_text(detailed,"not a validated acoustic threshold")&&
          !contains_text(unavailable,"Sound and structure.")&&contains_text(detailed,"less abruptly changing")&&
          contains_text(detailed,"public noise-like pattern remains predictable")&&
          contains_text(detailed,"does not measure randomness")&&
          contains_text(detailed,"Tone modes still transmit tones"),
          "Hidden details must explain both thresholds and the limits of the listening guideline");
}
void document_semantics_layout_and_plots() {
    const auto model=planner::build(example());
    for(const float width:{220.f,460.f,720.f,740.f,900.f,1200.f}) {
        const auto document=planner_page::build(model,width,false,false);
        const auto flat=nodes(document);
        check(contains_text(document,"Time per bit")&&contains_text(document,"Observer / receiver time")&&
              contains_text(document,"Clock / RAM limit")&&
              contains_text(document,"Stronger → weaker · dB in 1 Hz")&&contains_text(document,"1 sec")&&
              contains_text(document,"1 min")&&contains_text(document,"1 hr")&&contains_text(document,"1 day"),
              "Planner labels and logarithmic time axes must remain native text");
        check(std::count_if(flat.begin(),flat.end(),[](const auto* node) {
                  return node->text.find("LPI is not guaranteed")!=std::string::npos;
              })==1,"Planner must use one succinct general LPI warning");
        check(!contains_text(document,"safely hidden")&&!contains_text(document,"safe bits"),
              "Observer time must not be presented as a count of safely hidden bits");
        check(contains_text(document,"1-bit RX · right axis")&&contains_text(document,"100%")&&
              contains_text(document,"50%")&&contains_text(document,"0%")&&contains_text(document,"CPU estimate")&&
              contains_text(document,"Processing seconds per second of audio")&&contains_text(document,"1 = real-time limit"),
              "Planner must label its probability axis and CPU feasibility alongside reception");
        check(std::none_of(flat.begin(),flat.end(),[](const auto* node){return node->command==ui::Command::planner_target;}),
              "Native planner dropdown must replace the former target prompt button");
        for(const auto command:{ui::Command::planner_example_short,
                                ui::Command::planner_example_lpi,ui::Command::planner_fast,
                                ui::Command::planner_day,ui::Command::planner_clock,
                                ui::Command::planner_toggle_draft,ui::Command::planner_toggle_details,ui::Command::planner_load_command})
            check(std::any_of(flat.begin(),flat.end(),[&](const auto* node) {
                      return node->kind==ui::DocumentKind::action&&node->command==command;
                  }),"Planner milestone and editing affordances must use native shared actions");
        const auto target=std::find_if(flat.begin(),flat.end(),[](const auto* node){return node->command==ui::Command::planner_stronger;});
        const auto verdict=std::find_if(flat.begin(),flat.end(),[](const auto* node){return node->text.starts_with("RX estimate")||node->text.starts_with("RX reference");});
        check(verdict!=flat.end()&&verdict<target,"Planner must put its reception estimate before the target controls");
        for(const auto command:{ui::Command::planner_power,ui::Command::planner_loss,ui::Command::planner_noise}) {
            check(std::none_of(flat.begin(),flat.end(),[&](const auto* node){return node->command==command;}),
                  "Planner must not duplicate the shared top-bar budget controls");
        }
        check(!contains_text(document,"Quick references"),"Rough propagation references must remain collapsed by default");
        const auto layout=ui::layout_document(document,static_cast<int>(width),[](const auto& node,int available) {
            const auto lines=std::max(1.,std::ceil(node.text.size()*node.font_size*.55/std::max(1,available)));
            return lines*node.font_size*1.2;
        });
        const auto within=[&](auto&& self,const ui::DocumentBox& box)->void {
            for(const auto& child:box.children) {
                check(child.bounds.width>0&&child.bounds.x>=0&&child.bounds.y>=0&&
                      child.bounds.x+child.bounds.width<=box.bounds.width&&
                      child.bounds.y+child.bounds.height<=box.bounds.height,
                      "Planner document overflowed or squeezed content to zero at a supported width");
                self(self,child);
            }
        };
        within(within,layout.root);
        const auto editor=std::find_if(flat.begin(),flat.end(),[](const auto* node) {
            return node->kind==ui::DocumentKind::control&&node->control&&node->control->field==ui::Field::planner_target;
        });
        check(editor!=flat.end(),"Planner target must be a native editor within its scrollable document");
        const auto command=std::find_if(flat.begin(),flat.end(),[](const auto* node) {
            return node->kind==ui::DocumentKind::control&&node->control&&node->control->field==ui::Field::planner_command;
        });
        check(command!=flat.end()&&(*command)->control->multiline&&(*command)->control->byte_limit==8192&&
              (*command)->control->document_only&&(*command)->control->tab_navigation&&
              (*command)->control->submit==ui::Command::none&&(*command)->height>=ui::label_height+50,
              "Launch command must be a small native multiline editor whose Enter key cannot transmit");
        if(width>=460) {
            ui::DocumentRect editor_rect,stronger_rect,weaker_rect,command_rect,load_rect,cpu_rect,cpu_heading;
            const auto locate=[&](auto&& self,const ui::DocumentNode& node,const ui::DocumentBox& box,int x,int y)->void {
                const auto& bounds=box.bounds;x+=bounds.x;y+=bounds.y;
                const ui::DocumentRect rect{x,y,bounds.width,bounds.height};
                if(&node==*editor)editor_rect=rect;
                if(node.command==ui::Command::planner_stronger)stronger_rect=rect;
                if(node.command==ui::Command::planner_weaker)weaker_rect=rect;
                if(&node==*command)command_rect=rect;
                if(node.command==ui::Command::planner_load_command)load_rect=rect;
                if(node.plot_name=="planner/cpu-pace")cpu_rect=rect;
                if(node.text=="CPU estimate")cpu_heading=rect;
                for(std::size_t i=0;i<node.children.size();++i)self(self,node.children[i],box.children[i],x,y);
            };
            locate(locate,document,layout.root,0,0);
            check(editor_rect.y+ui::label_height==stronger_rect.y&&stronger_rect.y==weaker_rect.y&&
                  editor_rect.x+editor_rect.width<=stronger_rect.x&&
                  command_rect.width>=220&&load_rect.x==command_rect.x&&
                  load_rect.y>=command_rect.y+command_rect.height,
                  "Target and step buttons must align; Load must remain below the command editor");
            if(width>=560)check(cpu_rect.x>=command_rect.x+command_rect.width&&
                  cpu_heading.y>=command_rect.y&&cpu_heading.y<=command_rect.y+12,
                  "Launch command and CPU graph must align side by side without overlap");
            if(width>=900)check(command_rect.x>=weaker_rect.x+weaker_rect.width&&
                  command_rect.y==editor_rect.y&&command_rect.width<=280,
                  "Wide planner layout must use compact aligned controls, command and CPU columns");
        }
    }
    const auto detailed=planner_page::build(model,900,true,false);
    check(contains_text(detailed,"Model limits")&&contains_text(detailed,"Quick references")&&
          contains_text(detailed,"90% detection")&&contains_text(detailed,"1% false alarm")&&
          contains_text(detailed,"FT8 reference"),"Expandable model details lost their assumptions and familiar reference");
    check(contains_text(detailed,"Groundwave · 1 MHz · 150 miles: 180 dB path loss.")&&
          contains_text(detailed,"Groundwave · 30 MHz · 150 miles: 210 dB path loss."),
          "Groundwave references must use path loss in dB, not received power in dBm");
    auto wide_band=example();
    wide_band.options.modem=tuning::resolve(10000,-8,tuning::PatternMode::auto_pattern,false).config;
    const auto wide_document=planner_page::build(planner::build(wide_band),900,false,false);
    const auto wide_nodes=nodes(wide_document);
    const auto band=std::find_if(wide_nodes.begin(),wide_nodes.end(),[](const auto* node) {
        return node->text.starts_with("Ideal signal:");
    });
    check(band!=wide_nodes.end()&&(*band)->text=="Ideal signal: 4.375–10.625 kHz",
          "A 10 kHz rate must show exact plain-decimal band endpoints, without scientific notation");
    const auto flat=nodes(detailed);
    std::size_t plot_count=0;
    for(const auto* node:flat)if(node->kind==ui::DocumentKind::bitmap) {
        ++plot_count;
        check(node->plot_name=="planner/bit-time"||node->plot_name=="planner/observer-time"||node->plot_name=="planner/cpu-pace",
              "Planner graph lost its stable native bitmap identity");
        for(const auto format:{PixelFormat::gray8,PixelFormat::rgb24,PixelFormat::mono1}) {
            constexpr unsigned width=319,height=167;
            BitmapImage full(width,height,format),damaged(width,height,format);
            auto request=full_bitmap_request(width,height,format==PixelFormat::mono1,format==PixelFormat::rgb24);
            node->plot.paint(request,[&](unsigned x,unsigned y,PixelBlock pixels){full.blit(x,y,pixels);});
            const std::set<unsigned char> levels(full.pixels().begin(),full.pixels().end());
            check(levels.size()>1,"Planner graph did not paint visible data and reference marks");
            for(const auto damage:{PixelRect{0,0,113,57},PixelRect{113,0,width-113,57},
                                   PixelRect{0,57,113,height-57},PixelRect{113,57,width-113,height-57}}) {
                request.damage=damage;
                node->plot.paint(request,[&](unsigned x,unsigned y,PixelBlock pixels){damaged.blit(x,y,pixels);});
            }
            check(full.pixels()==damaged.pixels(),"Planner damage repaint changed its immutable graph geometry");
            request.damage={0,0,0,0};bool painted=false;
            node->plot.paint(request,[&](unsigned,unsigned,PixelBlock){painted=true;});
            check(!painted,"Zero-area planner damage must not paint");
        }
    }
    check(plot_count==3,"Planner must include two time-comparison graphs and the compact CPU curve");
    auto invalid=model;invalid.available=false;invalid.error="Current draft pending";
    const auto unavailable=planner_page::build(invalid,220,true,true);
    check(contains_text(unavailable,"Current draft pending")&&!contains_text(unavailable,"Send current draft"),
          "Unavailable planner must withdraw stale numerical results and explain its pending state");
    check(contains_text(unavailable,"Link estimate unavailable")&&!contains_text(unavailable,"Received:"),
          "Unavailable model must hide stale derived link strength");
    const auto pending_nodes=nodes(unavailable);
    check(std::any_of(pending_nodes.begin(),pending_nodes.end(),[](const auto* node) {
              return node->kind==ui::DocumentKind::action&&node->command==ui::Command::planner_toggle_draft&&node->enabled;
          }),"Pending or invalid draft planning must leave an action to return to one bit");
}
}
int main() {
    try {
        independent_reference_values();exact_geometry_and_physical_finish();timing_milestones();
        clock_and_ram_milestones();sampled_clock_ram_islands();checked_clock_ram_navigation();
        nearest_usable_targets();nearest_target_shares_receiver_budget();
        narrow_band_differential_model();phase_loss_and_receiver_confidence();gpsdo_phase_does_not_change_clock_coverage();
        separate_link_budget_and_observer_model();quantization_fixed_modes_and_limits();
        current_draft_and_modem_isolation();application_prompt_roundtrips();all_target_dropdowns_and_native_planner_editor();
        controller_checked_navigation();
        controller_target_alignment();
        failed_draft_estimate_and_recovery();one_warning_on_planner_page();
        selected_workspace_reaches_planner();
        shared_link_budget_without_simulation();shared_link_controls_visibility();
        link_budget_edit_buffers();link_budget_preset_and_dialog_sync();
        receiver_overlay_and_cpu_status();estimate_warning_thresholds();document_semantics_layout_and_plots();
        std::cout<<"Shared Link planner tests passed\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

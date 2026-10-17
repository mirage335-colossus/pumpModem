#include "datapump/receiver_strategy.hpp"
#include "datapump/pattern_correlator.hpp"
#include <iostream>
#include <limits>
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/sched_hop.hpp"
#include "datapump/symbol_schedule.hpp"
#include <random>
#include <array>
#include "../src/pattern_envelope.hpp"
#include "../src/pattern_observation_layout.hpp"
#include "../src/live_receiver_admission.hpp"
using namespace datapump;
using namespace datapump::modem;
namespace {
void check(bool yes,const char* m){if(!yes)throw Error(m);}
Config geometry() {
    Config c;c.sample_rate=512;c.bandwidth_hz=4;c.carrier_hz=128;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=512;
    c.scramble=true;c.spreading_seed[0]=91;return c;
}
void amplitude_arrival_accounting() {
    auto c=geometry();c.scramble=false;c.spreading_factor=64;
    PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=7;
    search.hypotheses={{0,0}};search.receiver_bank_count=1;search.worker_threads=1;
    const auto count=receiver_strategy_amplitude_bank_count(c,search,1,1);
    check(count>=2*symbol_seconds(c)+2*search.start_uncertainty_seconds,
          "public amplitude cohort omitted firstfullyobserved symbol lifetime");
    search.receiver_strategy_workspace_bytes=4*1024*1024;
    c.receiver_strategy=ReceiverStrategy::existing_only;
    const auto original=resolve_receiver_strategy(c,search,8*1024*1024);
    auto rolling=search;rolling.amplitude_receiver_bank_count=count;rolling.amplitude_strategy_workspace_bytes=512*1024;
    const auto protected_fft=resolve_receiver_strategy(c,rolling,8*1024*1024);
    check(protected_fft.backend==original.backend&&protected_fft.selected_bank_count==1&&
          protected_fft.existing_processing_ratio==original.existing_processing_ratio&&
          protected_fft.selected_workspace_bytes==original.selected_workspace_bytes,
          "amplitude rolling allowance changed public continuousFFT accounting");
    c.receiver_strategy=ReceiverStrategy::amplitude_only;
    auto one_window=rolling;one_window.amplitude_receiver_bank_count=1;
    const auto single=resolve_receiver_strategy(c,one_window,8*1024*1024);
    const auto many=resolve_receiver_strategy(c,rolling,8*1024*1024);
    check(single.supported&&many.supported&&many.selected_bank_count==count&&many.selected_workspace_bytes==512*1024&&
          std::abs(many.amplitude_processing_ratio/single.amplitude_processing_ratio-count)<1e-8*count,
          "amplitude cost/count/share omitted rolling public arrival windows");
    rolling.amplitude_strategy_workspace_bytes=1024;
    check(!resolve_receiver_strategy(c,rolling,8*1024*1024).supported,
          "amplitude selection ignored its own rollingworkspace share");
    rolling.hypotheses={{0,-6000},{0,6000}};
    const auto slowed=receiver_strategy_amplitude_bank_count(c,rolling,1,1);
    check(pattern_amplitude_geometry(c,rolling).clock_hypotheses>1&&slowed>count,
          "slowclock lanes did not extend amplitude cohort lifetime");
    c.receiver_strategy=ReceiverStrategy::automatic;
    auto incomplete=search;incomplete.complex_frequency_coverage=false;
    incomplete.amplitude_receiver_bank_count=1000;
    incomplete.amplitude_strategy_workspace_bytes=4*1024*1024;
    const auto covered=resolve_receiver_strategy(c,incomplete,8*1024*1024);
    check(covered.backend==ReceiverBackend::amplitude_energy&&covered.full_frequency_coverage&&
          covered.amplitude_processing_ratio>covered.existing_processing_ratio&&
          covered.amplitude_processing_ratio<=covered.cpu_budget,
          "automatic retained cheaper incomplete complex coverage over affordable complete amplitude");
    incomplete.amplitude_receiver_bank_count=100000000;
    const auto too_costly=resolve_receiver_strategy(c,incomplete,8*1024*1024);
    check(too_costly.backend==ReceiverBackend::existing_complex&&!too_costly.full_frequency_coverage,
          "automatic hid an unaffordable complete amplitude cohort");
    c=geometry();c.scramble=false;c.bandwidth_hz=1;c.spreading_factor=16384;
    live::Settings settings;settings.transfer.modem=c;settings.simulation=false;
    PatternSearch partitioned;partitioned.start_offset_seconds=0;partitioned.start_uncertainty_seconds=7;
    live::detail::prepare_public_amplitude_windows(c,settings,false,partitioned);
    check(partitioned.amplitude_epoch_stride_seconds==64&&partitioned.amplitude_start_uncertainty_seconds==33&&
          partitioned.start_uncertainty_seconds==7&&!partitioned.amplitude_start_window,
          "public finite windows changed the complex domain or claimed dated timing");
    partitioned.amplitude_lookahead_epochs=live::detail::amplitude_epoch_lookahead(c,partitioned,1.L/c.sample_rate,0);
    const auto compact_count=receiver_strategy_amplitude_bank_count(c,partitioned,1,1);
    auto preceding=partitioned;preceding.amplitude_epoch_stride_seconds=1;preceding.amplitude_start_uncertainty_seconds.reset();
    check(compact_count*50<receiver_strategy_amplitude_bank_count(c,preceding,1,1),
          "public finite-window cohort did not remove per-second frontend duplication");
    for(const auto counts:{std::pair{2U,1U},std::pair{1U,2U}}) {
        auto mixed=search;
        live::detail::prepare_public_amplitude_windows(c,settings,false,mixed,counts.first,counts.second);
        check(mixed.amplitude_epoch_stride_seconds==1&&!mixed.amplitude_start_uncertainty_seconds,
              "mixed key/profile cohort inherited a single-public-family admission discount");
    }
    auto encrypted=search;settings.transfer.key.emplace(Bytes(32,0x53));
    live::detail::prepare_public_amplitude_windows(c,settings,false,encrypted);
    check(encrypted.amplitude_epoch_stride_seconds==1,"unseeded Data-key estimate used public arrival windows");
    settings.transfer.key.reset();
    auto timed=partitioned;timed.amplitude_epoch_stride_seconds=1;timed.amplitude_start_uncertainty_seconds.reset();
    settings.transfer.clock_sync=clock_sync::Policy{.001,.4,2.564};
    live::detail::prepare_public_amplitude_windows(c,settings,false,timed);
    check(timed.amplitude_epoch_stride_seconds==1&&!timed.amplitude_start_uncertainty_seconds,
          "public Default optimization widened an explicit clock prior");
    std::cout<<"amplitude public arrival accounting passed\n";
}
void fractional_clock_fft_boundary() {
    Config c;c.sample_rate=131072;c.bandwidth_hz=65536;c.carrier_hz=32768;
    c.pattern_symbols=true;c.spreading_factor=16384;c.integration_seconds=.5;c.constellation_bits=1;
    c.receiver_strategy=ReceiverStrategy::existing_only;
    PatternSearch search;search.frequency_offsets_hz={0};search.clock_errors_ppm={1};
    search.worker_threads=1;search.search_stream_phases=false;
    const auto fast=resolve_receiver_strategy(c,search,128U*1024*1024);
    search.clock_errors_ppm={-1};
    const auto slow=resolve_receiver_strategy(c,search,128U*1024*1024);
    // Runtime retains a 131072-point transform on both sides of this boundary.
    // One extra observed bin must not double the predicted FFT work.
    check(std::abs(slow.existing_processing_ratio/fast.existing_processing_ratio-1)<.001,
          "fractional clock coverage doubled the estimated FFT at a power-of-two boundary");
    c.sample_rate=1024;c.bandwidth_hz=128;c.carrier_hz=256;
    c.spreading_factor=1;c.integration_seconds=204.8125;
    search.clock_errors_ppm={0};search.hypotheses={{0,0}};
    const auto nominal=resolve_receiver_strategy(c,search,128U*1024*1024);
    search.hypotheses={{0,100}};
    const auto positive=resolve_receiver_strategy(c,search,128U*1024*1024);
    const auto bin=detail::pattern_observation_bin_samples(c,0);
    check(detail::pattern_observation_layout(c,bin,1).transform==65536 &&
          detail::pattern_observation_layout(c,bin,1.0001).transform==32768,
          "positive-only clock fixture no longer crosses the runtime transform boundary");
    check(positive.existing_processing_ratio<.997*nominal.existing_processing_ratio,
          "positive-only clock bank invented a slower nominal hypothesis in its FFT estimate");
    // Legacy coupled nominal clocks retain the runtime's symmetric aperture;
    // explicit positive pairs retain their shorter actual-clock aperture.
    c.search_arithmetic=SearchArithmetic::fp64;
    search.hypotheses.clear();search.frequency_offsets_hz={.0256};
    search.couple_clock_to_carrier=true;search.uncoupled_frequency_count=0;
    search.drift_tolerant=false;
    const auto coupled=resolve_receiver_strategy(c,search,128U*1024*1024);
    auto paired=search;paired.hypotheses={{.0256,-100}};
    const auto conservative=resolve_receiver_strategy(c,paired,128U*1024*1024);
    check(std::abs(coupled.existing_processing_ratio-conservative.existing_processing_ratio)<1e-12,
          "legacy coupled nominal-clock estimate omitted the runtime's conservative FFT aperture");
    paired.hypotheses={{.0256,100}};
    const auto explicit_positive=resolve_receiver_strategy(c,paired,128U*1024*1024);
    check(explicit_positive.existing_processing_ratio<.997*coupled.existing_processing_ratio,
          "legacy coupling fields overrode an authoritative positive-only pair");
    PatternReceiver runtime(c,128U*1024*1024,search);
    std::vector<float> silence(210760,0);runtime.push(silence);
    check(runtime.fft_work().input_transforms==0,"legacy FFT acquisition ran before its full aperture");
    const std::array<float,8> bin_silence{};runtime.push(bin_silence);
    check(runtime.fft_work().input_transforms==1,"legacy FFT acquisition aperture changed");
    silence.assign(104856,0);runtime.push(silence);
    check(runtime.fft_work().input_transforms==1,"legacy FFT second acquisition ran before its full hop");
    runtime.push(bin_silence);
    check(runtime.fft_work().input_transforms==2,"legacy FFT acquisition hop changed");
}
void amplitude_band_geometry_survives_factoring() {
    auto c=geometry();c.sample_rate=2048;c.carrier_hz=512;c.receiver_strategy=ReceiverStrategy::amplitude_only;
    PatternSearch s;s.start_offset_seconds=7;s.start_uncertainty_seconds=6;s.search_stream_phases=false;
    s.frequency_offsets_hz={-300,300};s.worker_threads=1;
    const auto g=pattern_amplitude_geometry(c,s);
    const auto starts=std::ceil(2*s.start_uncertainty_seconds*c.sample_rate/(pattern_chip_samples(c)/2.L))+1;
    const auto lanes=starts*g.clock_hypotheses;
    const auto original_base=192*1024.L+g.fft_samples*36.L+(g.bin_count+1.L)*sizeof(long double)+lanes*4096.L;
    const auto original_channel=lanes*sizeof(std::array<detail::EnvelopeFit,2>)+sizeof(PatternAmplitudeChannel);
    for(const auto budget:{1024U*1024,8U*1024*1024})for(const auto arithmetic:
            {SearchArithmetic::default_mode,SearchArithmetic::fp32,SearchArithmetic::fp64}) {
        c.search_arithmetic=arithmetic;
        const auto expected=static_cast<std::size_t>(std::min<long double>(g.channel_count,std::floor((budget-original_base)/original_channel)));
        check(expected>0,"band cap fixture lacks a complete carrier domain");
        const auto plan=resolve_receiver_strategy(c,s,budget);
        check(plan.supported && plan.amplitude_channels==expected,"smaller moments changed original band admission or trial count");
        auto admitted=s;admitted.amplitude_channel_limit=plan.amplitude_channels;
        PatternCorrelatorOptions o;o.amplitude_only=true;PatternCorrelator rx(c,admitted,budget,o);
        check(rx.work().amplitude_channels==expected && rx.working_bytes()<=budget,
            "production band cap changed during actual factored allocation");
        const auto actual=pattern_amplitude_geometry(c,admitted);
        for(std::size_t j=0;j<expected;++j) {
            const auto before=pattern_amplitude_channel(g,j,expected),after=pattern_amplitude_channel(actual,j);
            check(before.first_bin==after.first_bin && before.bin_count==after.bin_count,
                "factored allocation changed admitted band partitions");
        }
    }
}
void scheduled_strategy_geometry() {
    auto c=geometry();c.sample_rate=2048;c.carrier_hz=512;c.bandwidth_hz=64;
    c.data_key.emplace(Bytes(32,0x47));c.receiver_strategy=ReceiverStrategy::automatic;
    auto schedule=std::make_shared<SchedHopPlan>();schedule->time_multiplier=10;
    c.scheduled_hopping=schedule;
    c=scheduled_estimate_config(c);
    PatternSearch narrow;narrow.start_offset_seconds=1;narrow.start_uncertainty_seconds=.01;
    narrow.search_stream_phases=false;narrow.hypotheses={{0,0}};narrow.receiver_schedule_key_count=1;
    constexpr auto workspace=128U*1024*1024;
    // Preserve the independently guarded amplitude strategy checks.
    c.receiver_strategy=ReceiverStrategy::amplitude_only;
    const auto p=resolve_receiver_strategy(c,narrow,workspace);
    check(p.scheduled&&p.supported&&p.experimental&&p.backend==ReceiverBackend::amplitude_energy,
          "split schedule did not choose its implemented experimental energy decoder");
    auto excluded=narrow;
    excluded.qualified_start_window=PatternStartWindow{-10.L*c.sample_rate,1,0};
    const auto empty=resolve_receiver_strategy(c,excluded,workspace);
    check(!empty.supported&&empty.empty_scheduled_epoch&&!empty.workspace_limited,
          "proved-empty qualified epoch was mislabeled as unavailable receiver coverage");
    check(p.scheduled_frame_hops==10&&p.scheduled_active_hops==1&&
          p.scheduled_effective_multiplier>=10&&p.scheduled_effective_multiplier<10.01&&p.scheduled_frame_seconds==4&&
          p.scheduled_minimum_quiet_seconds==2&&p.scheduled_flat_duty>0&&p.scheduled_flat_duty<.1&&
          p.scheduled_recording_union_upper==1&&p.scheduled_fft_union_upper<.11&&
          p.scheduled_fft_seconds>0&&!p.scheduled_cost_calibrated,
          "scheduled strategy hid actual duty, source geometry or uncalibrated execution scope");
    auto admitted=narrow;admitted.amplitude_channel_limit=p.amplitude_channels;
    PatternCorrelatorOptions options;options.amplitude_only=true;
    PatternCorrelator runtime(c,admitted,workspace,options);
    check(runtime.work().hypotheses==p.scheduled_timing_positions&&runtime.working_bytes()<=workspace,
          "scheduled model timing count disagrees with production allocation");
    check(std::abs(p.scheduled_guaranteed_recovery_seconds-
              (p.scheduled_minimum_quiet_seconds-p.scheduled_interval_width_seconds))<1e-12&&
          p.scheduled_guaranteed_recovery_seconds>0&&
          p.scheduled_deadline_processing_ratio>p.scheduled_mean_processing_ratio&&
          p.selected_processing_ratio==p.scheduled_mean_processing_ratio,
          "scheduled mean selection lost its separate stricter per-hop diagnostic");
    auto broad=narrow;broad.start_uncertainty_seconds=6;
    const auto all=resolve_receiver_strategy(c,broad,workspace);
    check(all.scheduled_recording_union_upper==1&&all.scheduled_fft_union_upper==1&&
          all.amplitude_processing_ratio>p.amplitude_processing_ratio&&
          all.amplitude_channels==p.amplitude_channels,
          "broad arrival union was incorrectly divided by the nominal schedule multiplier");
    check(all.scheduled_guaranteed_recovery_seconds==0,
          "broad uncertainty invented a guaranteed null recovery interval");
    auto dual=narrow;dual.receiver_schedule_key_count=2;
    const auto two=resolve_receiver_strategy(c,dual,workspace);
    check(two.scheduled_recording_union_upper==1&&two.scheduled_fft_union_upper==2*p.scheduled_fft_union_upper,
          "FFT union omitted an independent key or nominal duty was mistaken for actual recorded PCM");
    schedule=std::make_shared<SchedHopPlan>(*schedule);schedule->group_hops=2;c.scheduled_hopping=schedule;
    c=scheduled_estimate_config(c);
    const auto diluted=resolve_receiver_strategy(c,narrow,workspace);
    check(diluted.scheduled_effective_multiplier>=5&&diluted.scheduled_effective_multiplier<5.01&&diluted.scheduled_active_hops==2&&
          diluted.scheduled_flat_duty>p.scheduled_flat_duty&&diluted.scheduled_minimum_quiet_seconds==1.6,
          "active dilution changed the total frame or failed to price extra active work");
    const auto bounded=receiver_strategy_bank_count(c,6,2,1);
    schedule=std::make_shared<SchedHopPlan>(*schedule);schedule->time_multiplier=10000;c.scheduled_hopping=schedule;
    check(receiver_strategy_bank_count(c,6,2,1)==bounded,
          "null-only UTC epochs inflated the active resident-bank bound with total frame length");
    schedule=std::make_shared<SchedHopPlan>(*schedule);schedule->time_multiplier=10;c.scheduled_hopping=schedule;
    for(const auto strategy:{ReceiverStrategy::automatic,ReceiverStrategy::complex_only,ReceiverStrategy::existing_only}) {
        c.receiver_strategy=strategy;
        // Explicit diagnostics retain supplied exact frequency/clock candidates.
        const auto complex=resolve_receiver_strategy(c,narrow,workspace);
        check(complex.supported&&!complex.workspace_limited&&complex.backend==ReceiverBackend::existing_complex&&
              complex.reason.find("split complex")!=std::string::npos,
              "scheduled split symbol unnecessarily disabled the original complex detector");
        check(complex.scheduled_timing_positions>0 && complex.scheduled_fft_seconds==0,
              "mapped streaming complex model invented a whole-symbol FFT");
        PatternReceiver runtime(c,workspace,narrow);
        check(runtime.working_bytes()<=workspace,"mapped split complex ignored workspace");
        runtime.finish();check(runtime.take_bursts().empty(),"mapped complex EOF manufactured physical absence");
    }

}

void packed_defaults_use_complex_evidence() {
    for(const auto chips:{128u,64u})for(const auto arithmetic:
            {SearchArithmetic::default_mode,SearchArithmetic::fp32,SearchArithmetic::fp64}) {
        Config c;c.sample_rate=14400;c.bandwidth_hz=3600;c.carrier_hz=1500;
        c.spreading_factor=chips;c.scramble=true;c.search_arithmetic=arithmetic;
        c.data_key.emplace(Bytes(32,0x5b));c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,1});
        c=scheduled_estimate_config(c);
        check(packed_scheduled_symbols(c),"ordinary short defaults did not retain protected whole-symbol slots");
        PatternSearch s;s.start_offset_seconds=1;s.start_uncertainty_seconds=6;
        s.search_stream_phases=true;s.hypotheses={{-.2,-300},{0,0},{.2,300}};s.worker_threads=1;
        constexpr auto bytes=64U*1024*1024;
        const auto phases=packed_scheduled_phase_count(c,c.stream_epoch);
        for(const auto strategy:{ReceiverStrategy::automatic,ReceiverStrategy::existing_only,ReceiverStrategy::complex_only}) {
            c.receiver_strategy=strategy;
            const auto p=resolve_receiver_strategy(c,s,bytes);
            check(p.supported&&p.backend==ReceiverBackend::existing_complex&&p.scheduled&&
                  p.full_frequency_coverage&&p.full_clock_coverage&&p.scheduled_canonical_phases==phases,
                  "packed defaults lost the complete complex detector or enumerated an all-sample phase bank");
            PatternReceiver rx(c,bytes,s);
            check(rx.working_bytes()<=bytes&&!rx.local_clock_fallback(),
                  "packed complex constructor exceeded workspace or discarded clock hypotheses");
            rx.finish();check(rx.take_bursts().empty(),"packed complex EOF manufactured absence");
        }
        c.receiver_strategy=ReceiverStrategy::amplitude_only;
        const auto denied=resolve_receiver_strategy(c,s,bytes);
        check(!denied.supported&&denied.reason.find("requires64 retained whole FFT observations")!=std::string::npos,
              "packed complex support weakened the independent amplitude eligibility guard");
        c.receiver_strategy=ReceiverStrategy::automatic;
        auto constrained=s;constrained.receiver_strategy_workspace_bytes=1024;
        const auto full=resolve_receiver_strategy(c,constrained,bytes);
        check(!full.supported&&full.workspace_limited,"packed complex ignored its rolling workspace reservation");
    }
}

void scheduled_packed_phase_model() {
    auto c=geometry();c.sample_rate=16384;c.bandwidth_hz=2048;c.carrier_hz=4096;c.spreading_factor=256;
    c.data_key.emplace(Bytes(32,0x35));c.receiver_strategy=ReceiverStrategy::amplitude_only;
    auto schedule=std::make_shared<SchedHopPlan>();schedule->time_multiplier=10;schedule->group_hops=2;
    // The quarter-second symbol safely splits with small guards. Deliberately
    // use a guard-dominated geometry to exercise full-power packed slots.
    schedule->clock_error_seconds=schedule->peer_clock_error_seconds=.05;
    schedule->audio_error_seconds=.1;c.scheduled_hopping=schedule;
    c=scheduled_estimate_config(c);
    check(packed_scheduled_symbols(c),"short scheduled fixture does not use full-power packed slots");
    PatternSearch search;search.start_offset_seconds=1;search.start_uncertainty_seconds=.01;
    search.search_stream_phases=true;search.hypotheses={{0,0}};search.worker_threads=1;
    constexpr auto workspace=128U*1024*1024;
    const auto plan=resolve_receiver_strategy(c,search,workspace);
    check(plan.supported&&plan.scheduled_canonical_phases>0&&
          plan.scheduled_canonical_phases==packed_scheduled_phase_count(c,c.stream_epoch),
          "packed model used an obsolete cipher-phase grid instead of actual UTC slots");
    search.amplitude_channel_limit=plan.amplitude_channels;
    PatternCorrelatorOptions options;options.amplitude_only=true;
    PatternCorrelator runtime(c,search,workspace,options);
    check(runtime.work().hypotheses==plan.scheduled_timing_positions&&runtime.working_bytes()<=workspace,
          "packed singleton phase model disagrees with actual receiver allocation");
}

void scheduled_short_split_late_phase_model() {
    auto c=geometry();c.sample_rate=20000;c.bandwidth_hz=2500;c.carrier_hz=5000;c.spreading_factor=256;
    c.data_key.emplace(Bytes(32,0x39));c.receiver_strategy=ReceiverStrategy::amplitude_only;
    auto schedule=std::make_shared<SchedHopPlan>();schedule->time_multiplier=10;schedule->group_hops=2;
    schedule->clock_error_seconds=schedule->peer_clock_error_seconds=.001;
    schedule->audio_error_seconds=.001;c.scheduled_hopping=schedule;
    const auto symbol=symbol_sample_count(c),step=canonical_phase_step(c);
    check(symbol<c.sample_rate&&!packed_scheduled_symbols(c),
          "late-phase fixture must use split sub-second symbols");
    const SchedHopSourceMap map(*schedule,*c.data_key,c.sample_rate);
    const auto initial=map.locate(1800000000ULL*c.sample_rate);
    const auto first=initial.active?initial.physical_begin:map.source_to_physical(initial.source).sample;
    c.stream_epoch=first/c.sample_rate;
    c.stream_phase_samples=(std::max(symbol+step,first%c.sample_rate+512)+step-1)/step*step;
    check(c.stream_phase_samples<c.sample_rate&&c.stream_phase_samples>symbol&&
          map.locate(c.stream_epoch*c.sample_rate+c.stream_phase_samples).active,
          "late-phase fixture must retain an active anchor beyond the symbol period");
    check(canonical_phase_extent(c)==c.sample_rate,
          "scheduled split phase domain omitted part of the UTC second");
    auto ordinary=c;ordinary.scheduled_hopping.reset();
    check(canonical_phase_extent(ordinary)==symbol,
          "scheduled phase correction changed the ordinary repeating-symbol domain");
    PatternSearch search;search.start_offset_seconds=1;search.start_uncertainty_seconds=.002;
    search.search_stream_phases=true;search.hypotheses={{0,0}};search.worker_threads=1;
    search.qualified_start_window=PatternStartWindow{
        static_cast<long double>(c.sample_rate-c.stream_phase_samples),1,1};
    constexpr auto workspace=64U*1024*1024;
    const auto plan=resolve_receiver_strategy(c,search,workspace);
    check(plan.supported&&plan.scheduled_timing_positions>0,
          "qualified scheduled model discarded a valid late UTC phase");
    search.amplitude_channel_limit=plan.amplitude_channels;
    PatternCorrelatorOptions options;options.amplitude_only=true;
    PatternCorrelator runtime(c,search,workspace,options);
    check(runtime.work().hypotheses==plan.scheduled_timing_positions&&runtime.working_bytes()<=workspace,
          "late scheduled phase model disagrees with actual receiver coverage/allocation");
}

void scheduled_long_chip_receiver_grid_model() {
    auto c=geometry();c.sample_rate=4000;c.bandwidth_hz=.4;c.carrier_hz=1000;c.spreading_factor=64;
    c.data_key.emplace(Bytes(32,0x46));c.receiver_strategy=ReceiverStrategy::amplitude_only;
    auto schedule=std::make_shared<SchedHopPlan>();schedule->time_multiplier=10;c.scheduled_hopping=schedule;
    c=scheduled_estimate_config(c);
    check(!packed_scheduled_symbols(c)&&pattern_chip_samples(c)>c.sample_rate,
          "long-chip fixture does not exercise split symbols");
    PatternSearch search;search.start_offset_seconds=1;search.start_uncertainty_seconds=.01;
    search.search_stream_phases=true;search.hypotheses={{0,0}};search.worker_threads=1;
    const auto geometry=pattern_amplitude_geometry(c,search);
    check(geometry.supported&&geometry.receiver_timing_subdivisions>1&&
          geometry.receiver_phase_step_samples<canonical_phase_step(c)&&
          canonical_phase_step(c)%geometry.receiver_phase_step_samples==0,
          "long chips retained arrival/phase cells wider than the usable active window");
    constexpr auto workspace=128U*1024*1024;
    const auto plan=resolve_receiver_strategy(c,search,workspace);
    check(plan.supported&&plan.scheduled_timing_spacing_seconds<.1&&
          plan.scheduled_phase_spacing_seconds<.1&&plan.scheduled_canonical_phases>0,
          "long-chip model did not expose the actual subdivided receiver grid");
    search.amplitude_channel_limit=plan.amplitude_channels;
    PatternCorrelatorOptions options;options.amplitude_only=true;
    PatternCorrelator runtime(c,search,workspace,options);
    check(runtime.work().hypotheses==plan.scheduled_timing_positions&&runtime.working_bytes()<=workspace,
          "subdivided long-chip model disagrees with actual receiver allocation");
    search.qualified_start_window=PatternStartWindow{static_cast<long double>(c.sample_rate)-c.stream_phase_samples,
        1,.001L*c.sample_rate};
    const auto qualified=resolve_receiver_strategy(c,search,workspace);
    check(qualified.supported&&qualified.scheduled_timing_positions>0&&
          qualified.scheduled_timing_positions<=plan.scheduled_timing_positions,
          "long-chip qualified subcells dropped a covered phase or expanded its original bank");
    search.amplitude_channel_limit=qualified.amplitude_channels;
    PatternCorrelator tight(c,search,workspace,options);
    check(tight.work().hypotheses==qualified.scheduled_timing_positions&&tight.working_bytes()<=workspace,
          "qualified long-chip model failed to retain the same phase/timing coverage cells as execution");
}

void frequency_axis_preserves_long_private_progress() {
    auto c=geometry();c.spreading_factor=64;c.stream_epoch=1800000000;
    c.search_arithmetic=SearchArithmetic::default_mode;
    PatternSearch s;s.initial_stream_symbols=1;s.worker_threads=1;s.chunk_bits=1;
    s.start_offset_seconds=1;s.start_uncertainty_seconds=.05;
    s.qualified_start_window=PatternStartWindow{512,1,25.6L};s.prefer_streamed_templates=true;
    const auto symbol=symbol_sample_count(c);const double df=.25/(static_cast<double>(symbol)/c.sample_rate);
    for(int i=-512;i<=512;++i)s.hypotheses.push_back({i*df,0});
    const Bytes bits{0,1};PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
    std::vector<std::complex<double>> analytic(tx.total_samples());tx.read_analytic(analytic);
    const auto pad=pattern_pulse_padding_samples(c);
    std::vector<float> pcm(512+3*symbol+512);
    std::mt19937_64 rng(173);std::normal_distribution<float> noise(0,.003f);
    for(auto& v:pcm)v=noise(rng);
    for(std::size_t i=pad;i<analytic.size()-pad;++i)
        pcm[512+i-pad]+=static_cast<float>((analytic[i]*std::polar(1.,.37)).real());
    constexpr std::size_t budget=64*1024*1024;
    c.receiver_strategy=ReceiverStrategy::existing_only;PatternReceiver reference(c,budget,s);
    c.receiver_strategy=ReceiverStrategy::complex_only;
    const auto axis_plan=resolve_receiver_strategy(c,s,budget);
    check(axis_plan.backend==ReceiverBackend::frequency_axis,
        "long private fixture did not select frequency-axis search");
    auto original=c;original.receiver_strategy=ReceiverStrategy::existing_only;
    check(axis_plan.complex_tracking_processing_ratio<
        resolve_receiver_strategy(original,s,budget).complex_tracking_processing_ratio*.1,
        "continuation work model retained the full direct frequency bank");
    PatternReceiver candidate(c,budget,s);
    std::array<Bytes,2> received;std::array<unsigned,2> completed{};
    const std::array<PatternReceiver*,2> receivers{&reference,&candidate};
    for(std::size_t position=0;position<pcm.size();) {
        const auto block=std::span(pcm).subspan(position,std::min<std::size_t>(317,pcm.size()-position));
        position+=block.size();
        for(std::size_t r=0;r<2;++r) {
            receivers[r]->push(block);
            check(receivers[r]->working_bytes()<=budget,"frequency-axis reception exceeded workspace");
            for(const auto& b:receivers[r]->take_bursts()) {
                check(b.end_sample<=position,"frequency-axis used future observations");
                received[r].insert(received[r].end(),b.bits.begin(),b.bits.end());
                if(b.complete) {++completed[r];check(position>=512+2*symbol+pattern_absence_samples(c),
                    "long private prior manufactured early absence");}
            }
        }
    }
    for(auto* r:receivers){r->finish();check(r->take_bursts().empty(),"EOF manufactured new decisions or completion");}
    check(received[0]==bits&&received[1]==bits&&completed[0]==1&&completed[1]==1,
        "frequency-axis changed private bits or physical completion");
    check(candidate.fft_work().frequency_axis_batches>0,"frequency-axis selected but did no numerical work");
    check(candidate.fft_work().frequency_axis_tracking_batches>0,"tracked frequency bank retained direct template work");
    check(candidate.fft_work().frequency_axis_fallback_batches==0,"qualified fixture retained broad numerical work");
}
}
int main(int argc,char** argv){try {if(argc==2&&std::string_view(argv[1])=="--amplitude-arrival-only"){amplitude_arrival_accounting();return 0;}amplitude_arrival_accounting();fractional_clock_fft_boundary();if(argc>1&&std::string_view(argv[1])=="--fft-geometry")return 0;amplitude_band_geometry_survives_factoring();scheduled_strategy_geometry();packed_defaults_use_complex_evidence();scheduled_packed_phase_model();scheduled_short_split_late_phase_model();scheduled_long_chip_receiver_grid_model();
    auto c=geometry();PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=7;
    s.worker_threads=1;s.compact_clock_search=true;s.search_stream_phases=true;
    for(int i=-100;i<=100;++i)s.hypotheses.push_back({i*.05,0});
    s.receiver_bank_count=receiver_strategy_bank_count(c,6,2,3);
    check(s.receiver_bank_count>6*13,"rolling private epochs omitted from policy cost");
    c.receiver_strategy=ReceiverStrategy::existing_only;
    auto p=resolve_receiver_strategy(c,s,8*1024*1024);
    check(p.backend==ReceiverBackend::existing_complex && p.supported,"existing-only enabled a new decoder");
    auto declared=s;
    declared.admitted_frequency_half_width_hz=5;
    declared.admitted_clock_half_width_ppm=0;
    auto declared_plan=resolve_receiver_strategy(c,declared,8*1024*1024);
    check(!declared_plan.full_frequency_coverage && !declared_plan.full_clock_coverage,
        "explicit endpoints alone certified an unverified continuous domain");
    declared.complex_frequency_coverage=true;declared.complex_clock_coverage=true;
    declared_plan=resolve_receiver_strategy(c,declared,8*1024*1024);
    check(declared_plan.full_frequency_coverage && declared_plan.full_clock_coverage,
        "policy provenance for the actual supplied bank was discarded");
    declared.admitted_frequency_half_width_hz=6;
    check(!resolve_receiver_strategy(c,declared,8*1024*1024).full_frequency_coverage,
        "supplied coverage provenance overrode a missing outer endpoint");
    auto local=s;local.hypotheses.clear();local.search_stream_phases=false;
    local.expand_clock_search=false;
    const auto local_plan=resolve_receiver_strategy(c,local,8*1024*1024);
    local.expand_clock_search=true;
    const auto expanded_plan=resolve_receiver_strategy(c,local,8*1024*1024);
    check(expanded_plan.existing_processing_ratio>local_plan.existing_processing_ratio,
        "expanded and five-bin local search received the same workload");
    auto explicit_expanded=local;
    for(auto f:default_pattern_frequency_offsets(c))explicit_expanded.hypotheses.push_back({f,0});
    for(auto f:default_pattern_frequency_offsets(c))
        explicit_expanded.hypotheses.push_back({f,((1+f/c.carrier_hz)-1)*1e6});
    check(resolve_receiver_strategy(c,explicit_expanded,8*1024*1024).existing_processing_ratio==
        expanded_plan.existing_processing_ratio,"implicit expanded cost omitted nominal/coupled pairs");
    auto edge_config=c;edge_config.pulse_shaping=false;edge_config.carrier_hz=edge_config.bandwidth_hz/2;
    auto edge_local=local;edge_local.expand_clock_search=false;
    const auto implicit_edge=resolve_receiver_strategy(edge_config,edge_local,8*1024*1024);
    edge_local.hypotheses={{0,0}};
    check(implicit_edge.existing_processing_ratio==
        resolve_receiver_strategy(edge_config,edge_local,8*1024*1024).existing_processing_ratio,
        "implicit local cost retained frequency offsets outside sampled headroom");
    c.receiver_strategy=ReceiverStrategy::amplitude_only;
    p=resolve_receiver_strategy(c,s,8*1024*1024);
    check(p.backend==ReceiverBackend::amplitude_energy && p.supported && p.experimental,
        "private amplitude-only did not select explicit experimental engine");
    check(p.full_frequency_coverage && p.full_clock_coverage,"amplitude policy inherited clipped coherent coverage");
    const auto single=pattern_amplitude_geometry(c,s);
    c.search_arithmetic=SearchArithmetic::fp64;
    const auto wide=pattern_amplitude_geometry(c,s);
    check(single.reason.find("FP32")!=std::string::npos && wide.reason.find("native FP64")!=std::string::npos &&
        single.fft_samples==wide.fft_samples && single.channel_count==wide.channel_count &&
        single.clock_hypotheses==wide.clock_hypotheses,"amplitude arithmetic labels or coverage changed");
    const auto fp64_cost=resolve_receiver_strategy(c,s,8*1024*1024);
    c.search_arithmetic=SearchArithmetic::fp32;
    const auto fp32_cost=resolve_receiver_strategy(c,s,8*1024*1024);
    c.search_arithmetic=SearchArithmetic::default_mode;
    check(fp32_cost.amplitude_processing_ratio==p.amplitude_processing_ratio &&
        fp64_cost.amplitude_processing_ratio>p.amplitude_processing_ratio &&
        fp64_cost.amplitude_channels==p.amplitude_channels,
        "amplitude arithmetic work model ignored native FFT precision or changed coverage");
    auto parallel=s;parallel.worker_threads=8;
    check(resolve_receiver_strategy(c,parallel,8*1024*1024).amplitude_processing_ratio==p.amplitude_processing_ratio,
        "serial amplitude cost was divided by idle search workers");
    auto bounded=s;bounded.receiver_strategy_workspace_bytes=1024;
    const auto denied=resolve_receiver_strategy(c,bounded,8*1024*1024);
    check(!denied.supported&&denied.workspace_limited,"rolling cohort workspace reserve was ignored");
    c.receiver_strategy=ReceiverStrategy::automatic;c.receiver_cpu_budget=1e-12;
    p=resolve_receiver_strategy(c,s,8*1024*1024);
    check(p.backend==ReceiverBackend::amplitude_energy,"automatic overloaded private bank did not use cheaper amplitude engine");
    c.receiver_strategy=ReceiverStrategy::complex_only;
    check(resolve_receiver_strategy(c,s,8*1024*1024).backend!=ReceiverBackend::amplitude_energy,
        "complex-only silently discarded phase");
    c.receiver_strategy=ReceiverStrategy::amplitude_only;c.dsss=true;c.dsss_factor=10;
    p=resolve_receiver_strategy(c,s,8*1024*1024);
    check(!p.supported && !p.reason.empty(),"unsupported amplitude combination silently fell back");
    c=geometry();c.receiver_strategy=ReceiverStrategy::amplitude_only;s.receiver_bank_count=1;
    s.search_stream_phases=false;s.start_uncertainty_seconds=0;
    PatternReceiver rx(c,8*1024*1024,s);
    check(rx.clock_windowed(),"amplitude selection did not reach production receiver");
    rx.finish();check(rx.take_bursts().empty(),"amplitude EOF manufactured absence");
    frequency_axis_preserves_long_private_progress();
    std::cout<<"receiver strategy policy checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }

#include "datapump/channel.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/simulation_estimate.hpp"
#include "../src/receiver_probability.hpp"
#include "../src/pattern_correlator_batch.hpp"
#include "../src/pattern_differential.hpp"
#include "../src/pattern_drift.hpp"
#include <numbers>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <string>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
constexpr std::size_t workspace=8*1024*1024;
constexpr unsigned captures_per_case=64;

struct Case {
    const char* name;
    bool keyed;
    double rate,energy_db,diffusion;
    double clock_ppm=0,frequency_hz=0;
    bool compare_coherent=false;
    unsigned bit_count=1;
    bool shaped=false;
};
transfer::Options options(const Case& fixture) {
    transfer::Options result;
    result.modem.sample_rate=128;
    result.modem.carrier_hz=32;
    result.modem.bandwidth_hz=fixture.rate;
    result.modem.integration_seconds=16;
    result.modem.pulse_shaping=fixture.shaped;
    result.modem.scramble=fixture.keyed;
    result.timestamp=1800000041;
    result.modem.stream_epoch=result.timestamp;
    for(std::size_t i=0;i<result.modem.spreading_seed.size();++i)
        result.modem.spreading_seed[i]=static_cast<std::uint8_t>(13*i+29);
    result.search_seconds=0;
    result.dsp_workspace_bytes=workspace;
    return result;
}
modem::ChannelConfig channel(const Case& fixture,const modem::Config& config,unsigned seed) {
    modem::ChannelConfig result;
    result.snr_db=fixture.energy_db-10*std::log10(modem::symbol_sample_count(config)/2.);
    result.phase_noise_degrees_per_sqrt_second=fixture.diffusion;
    result.clock_error_ppm=fixture.clock_ppm;
    result.frequency_offset_hz=fixture.frequency_hz;
    result.seed=7919+std::uint64_t{104729}*seed;
    return result;
}
transfer::Estimate transmission(const modem::Config& config,unsigned bits) {
    transfer::Estimate result;
    result.wire_bits=bits;
    result.total_seconds=static_cast<double>(modem::training_sample_count(config)+
        2*modem::pattern_pulse_padding_samples(config)+bits*modem::symbol_sample_count(config)+
        modem::suppression_sample_count(config))/config.sample_rate;
    return result;
}
std::vector<float> capture(const modem::Config& config,const modem::ChannelConfig& impairment,
                           const Bytes& bits) {
    // Exercise the production sampled channel, including seeded fractional
    // startup, interpolation, ongoing phase diffusion and suppression noise.
    // No exact timing or phase information reaches the receiver.
    modem::StreamingTransmitter source(modem::RawBits{bits},config,workspace);
    modem::SampledSimulationChannel transport(config,impairment);
    std::array<float,257> block{};
    std::vector<float> samples;
    while(const auto count=transport.read(source,block))
        samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
    auto trailing=modem::pattern_absence_samples(config)+config.sample_rate+
        2*modem::pattern_pulse_padding_samples(config);
    while(trailing) {
        const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(trailing,block.size()));
        transport.read_noise(std::span(block).first(count));
        samples.insert(samples.end(),block.begin(),block.begin()+static_cast<std::ptrdiff_t>(count));
        trailing-=count;
    }
    return samples;
}
bool receive(const modem::Config& config,std::span<const float> samples,const Bytes& expected,bool drift=true) {
    modem::PatternSearch search;
    search.drift_tolerant=drift;
    search.expand_clock_search=true;
    search.start_offset_seconds=static_cast<double>(modem::training_sample_count(config)+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    search.start_uncertainty_seconds=1;
    search.worker_threads=1;
    search.chunk_bits=1;
    modem::PatternReceiver receiver(config,workspace,search);
    check(receiver.drift_tolerant()==drift,"sampled probability fixture did not exercise the requested detector");
    Bytes observed;
    std::size_t completions=0,position=0;
    const auto harvest=[&] {
        check(receiver.working_bytes()<=workspace,"sampled probability validation exceeded receiver workspace");
        for(const auto& event:receiver.take_bursts()) {
            check(position>=event.end_sample,"receiver exposed evidence before observing its complete symbol");
            if(event.complete) {
                check(position>=event.end_sample+modem::pattern_absence_samples(config),
                      "sampled probability validation completed without a whole absent symbol");
                ++completions;
            }
            observed.insert(observed.end(),event.bits.begin(),event.bits.end());
            observed.insert(observed.end(),event.missing_slots,modem::missing_pattern_bit);
        }
    };
    while(position<samples.size()) {
        const auto count=std::min<std::size_t>(257,samples.size()-position);
        receiver.push(samples.subspan(position,count));position+=count;harvest();
    }
    receiver.finish();harvest();
    return completions==1 && observed==expected;
}


void real_atom_reduction() {
    using namespace simulation::detail;
    using namespace modem::detail;
    for(const auto total:{std::uint64_t{16401},std::uint64_t{16465}}) {
        ReceiverProbabilityParameters p;
        p.real_samples=total;p.real_window_samples=64;p.real_sample_rate=64;
        p.differential_windows=static_cast<std::uint32_t>(total/64);
        p.differential_window_seconds=1;p.differential_tail_seconds=static_cast<double>(total%64)/64;
        p.noise_dimensions=p.coherent_dimensions=static_cast<double>(total)/2;
        p.section_dimensions=static_cast<double>(total)/2;p.requested_trials=256;
        p.frequency_step_hz=.005;p.frequency_bin_min=-1;p.frequency_bin_max=1;
        std::vector<std::array<double,4>> dots;
        std::array<std::array<CorrelationFit,5>,2> fits{};
        std::array<std::vector<CorrelationFit>,2> windows;
        for(auto& local:windows)local.resize(p.differential_windows);
        double energy=0;
        for(std::uint64_t first=0;first<total;) {
            unsigned section=0;while(section<3&&first>=drift_boundary(section+1,total,4))++section;
            const auto end=std::min({total,(first/64+1)*64,drift_boundary(section+1,total,4)});
            ReceiverProbabilityAtom atom;atom.first_sample=first;atom.samples=end-first;atom.section=section;
            std::array<double,4> dot{};
            for(auto n=first;n<end;++n) {
                const auto chip=n/4;
                const auto a=std::polar(1.,std::numbers::pi/2*static_cast<double>((chip*13+chip/7)%4));
                const auto b=std::polar(1.,std::numbers::pi/2*static_cast<double>((chip*7+chip/11+1)%4));
                const auto carrier=std::polar(1.,2*std::numbers::pi*.5*static_cast<double>(n)/64);
                const auto ua=a*carrier,ub=b*carrier;
                const std::array v{ua.real(),ua.imag(),ub.real(),ub.imag()};
                const auto x=.2*std::real(a*std::polar(1.,2*std::numbers::pi*.505*static_cast<double>(n)/64))+
                    .08*std::sin(static_cast<double>(n)*1.61803398875);
                energy+=x*x;
                for(unsigned i=0;i<4;++i) {
                    dot[i]+=x*v[i];for(unsigned j=0;j<4;++j)atom.gram[4*i+j]+=v[i]*v[j];
                }
                for(unsigned bit=0;bit<2;++bit) {
                    const auto k=bit*2;
                    const CorrelationProjection projection{x*v[k],x*v[k+1],v[k]*v[k],v[k+1]*v[k+1],v[k]*v[k+1],x*x};
                    for(const auto j:{0u,section+1})fits[bit][j].add(projection,{1,0},1);
                    if(n/64<p.differential_windows)windows[bit][n/64].add(projection,{1,0},1);
                }
            }
            dots.push_back(dot);p.real_atoms.push_back(atom);first=end;
        }
        const auto scores=receiver_real_atom_evidence(p,dots,energy);
        for(unsigned bit=0;bit<2;++bit) {
            const auto coherent=drift_evidence(fits[bit][0].explained(),energy,p.coherent_dimensions,1,false);
            double fitted=0,strongest=0;
            for(unsigned j=1;j<5;++j) {const auto e=fits[bit][j].explained();fitted+=e;strongest=std::max(strongest,e);}
            const auto older=combine_drift_evidence(coherent,
                drift_evidence(fitted-strongest,energy,p.section_dimensions,4,false),4);
            DifferentialAccumulator differential;
            for(std::size_t i=0;i<windows[bit].size();++i) {
                const auto& fit=windows[bit][i];
                differential.add(differential_whiten(fit.xc,fit.xs,fit.cc,fit.ss,fit.cs),i,total,64);
            }
            check(differential.pairs==128,"an odd final complete window or partial tail must not create a product");
            const auto combined=combine_differential_evidence(older,differential.score(),true);
            const auto close=[](double x,double y){return std::abs(x-y)<=2e-10*std::max(1.,std::abs(y));};
            check(close(scores[bit].coherent,coherent)&&close(scores[bit].older,older)&&close(scores[bit].combined,combined),
                  "real atom reduction changed production whole/quarter/local evidence");
            check(close(scores[bit].differential,differential.score()),
                  "real atom reduction changed complete local-window products");
        }
        auto changed=dots;changed.back()[0]+=1;
        const auto changed_tail=receiver_real_atom_evidence(p,changed,energy+1);
        check(changed_tail[0].differential==scores[0].differential&&
              changed_tail[1].differential==scores[1].differential&&
              changed_tail[0].coherent!=scores[0].coherent,
              "a partial tail must change whole-symbol evidence without creating local products");
        const auto positive=receiver_real_atom_evidence(p,dots,energy,1);
        const auto negative=receiver_real_atom_evidence(p,dots,energy,-1);
        check(positive[0].coherent>negative[0].coherent+100,
              "real raw-basis carrier search used the wrong rotation sign");
        check(receiver_probability(p).frequency_search_approximation,
              "midpoint carrier rotations must report their bounded approximation");
        // Zero signal exercises PSD/rank-deficient covariance without claiming
        // a rare-event rate. The deterministic noise draws retain whole energy.
        p.frequency_bin_min=p.frequency_bin_max=0;
        const auto probability=receiver_probability(p);
        check(probability.available&&probability.differential_model&&probability.trials==256,
              "a singular short atom must not reject a nonsingular complete receiver fit");
        const auto unavailable=[](const ReceiverProbabilityParameters& model,const char* reason) {
            const auto result=receiver_probability(model);
            check(!result.available&&result.trials==0&&result.acquired_correct==0&&
                  result.unsupported_reason.find(reason)!=std::string::npos,
                  "unsupported real geometry must name its limiting gate without inventing a probability");
        };
        auto carrier_bank=p;
        carrier_bank.frequency_bin_min=-8;carrier_bank.frequency_bin_max=8;carrier_bank.frequency_step_hz=.0001;
        const auto full_bank=receiver_probability(carrier_bank);
        check(full_bank.available&&full_bank.frequency_candidates==17&&full_bank.trials==p.requested_trials&&
              full_bank.frequency_search_approximation,
              "the supported 17-candidate real model must retain every carrier candidate and trial");
        // The 8.2 kHz / target −49 GUI geometry has 101 candidates relative
        // to its selected lane. Keeping its small local rotation does not
        // permit replacing the full bank with a selected-lane probability.
        carrier_bank.frequency_bin_min=-67;carrier_bank.frequency_bin_max=33;
        carrier_bank.frequency_step_hz=4.92e-8;
        unavailable(carrier_bank,"at most 17 carrier candidates; this search has 101");
        auto unresolved=p;unresolved.diffusion_degrees=60;
        unavailable(unresolved,"0.5 radian² per local window");
        unresolved=p;unresolved.residual_frequency=.051;
        unavailable(unresolved,"Residual carrier rotation");
        unresolved=p;unresolved.frequency_bin_min=-8;unresolved.frequency_bin_max=8;
        unresolved.frequency_step_hz=.007;
        unavailable(unresolved,"Carrier-search rotation");
        unresolved=p;unresolved.requested_trials=255;
        unavailable(unresolved,"trial count must be within 256–4096");
        unresolved=p;unresolved.noise_dimensions+=1;
        unavailable(unresolved,"noise dimensions do not match complete received energy");
        auto malformed=p;malformed.section_dimensions=std::numeric_limits<double>::infinity();
        unavailable(malformed,"detector dimensions must be finite");
        malformed=p;malformed.differential_windows=0;
        unavailable(malformed,"256–4096 complete local windows");
        p.real_atoms.back().first_sample--;
        check(!receiver_probability(p).available,"overlapping atoms must not duplicate received energy");
    }
}

void sampled_estimates() {
    // The short PCM clocks make repeated physical captures cheap. Phase
    // diffusion is deliberately increased to span the dimensionless phase
    // changes of long symbols. These are synthetic model checks, not measured
    // oscillator specifications or evidence about any particular radio link.
    constexpr std::array cases{
        Case{"64-chip public stable",false,8,18,0},
        Case{"64-chip private stable",true,8,18,0},
        Case{"public weak",false,32,16,0},
        Case{"public transition",false,32,18,0},
        Case{"private transition",true,32,18,0},
        Case{"public strong",false,32,22,0},
        Case{"private strong",true,32,22,0},
        Case{"public mild drift",false,32,20,30},
        Case{"private mild drift",true,32,20,30},
        Case{"public drift transition",false,32,22,60},
        Case{"private drift transition",true,32,22,60},
        Case{"public drift recovery",false,32,26,60,0,0,true},
        Case{"private drift recovery",true,32,26,60,0,0,true},
        Case{"public faster drift",false,32,26,90},
        Case{"public clock and drift",false,32,22,30,100,.007},
        Case{"private clock and drift",true,32,22,30,-100,-.007},
        Case{"public 001 mild drift",false,32,20,30,0,0,false,3},
        Case{"private 001 drift recovery",true,32,26,60,0,0,false,3},
        Case{"shaped public mild drift",false,32,20,30,0,0,false,1,true},
        Case{"shaped private drift recovery",true,32,26,60,0,0,false,1,true},
    };
    double squared_error=0,maximum_error=0;
    unsigned section_recoveries=0,coherent_recoveries=0;
    const auto begun=std::chrono::steady_clock::now();
    for(const auto& fixture:cases) {
        const auto configured=options(fixture);
        const auto prediction=simulation::estimate(transmission(configured.modem,fixture.bit_count),configured,true,
            channel(fixture,configured.modem,0));
        check(prediction.confidence_available && prediction.drift_model_available && !prediction.coherent_reference_only,
              "sampled probability fixture lacks the complete implemented-detector estimate");
        check(std::isfinite(prediction.success_probability) && prediction.success_probability>=0 &&
              prediction.success_probability<=1,"sampled success estimate is not a finite probability");
        unsigned recovered=0,baseline=0;
        for(unsigned seed=0;seed<captures_per_case;++seed) {
            const auto bits=fixture.bit_count==1?Bytes{static_cast<std::uint8_t>(seed%2)}:Bytes{0,0,1};
            const auto samples=capture(configured.modem,channel(fixture,configured.modem,seed),bits);
            recovered+=receive(configured.modem,samples,bits);
            if(fixture.compare_coherent)baseline+=receive(configured.modem,samples,bits,false);
        }
        const auto observed=static_cast<double>(recovered)/captures_per_case;
        const auto error=std::abs(observed-prediction.success_probability);
        squared_error+=error*error;maximum_error=std::max(maximum_error,error);
        if(fixture.compare_coherent){section_recoveries+=recovered;coherent_recoveries+=baseline;}
        std::cout<<fixture.name<<": predicted "<<prediction.success_probability
                 <<", sampled "<<recovered<<'/'<<captures_per_case;
        if(fixture.compare_coherent)std::cout<<", coherent "<<baseline<<'/'<<captures_per_case;
        std::cout<<'\n';
        // Deliberately allow both finite-capture variation and remaining
        // engineering-model error. This catches gross optimism/pessimism;
        // it does not certify a calibrated probability or a false-alarm rate.
        const auto uncertainty=3*std::sqrt(prediction.success_probability*
            (1-prediction.success_probability)/captures_per_case);
        check(error<=.15+uncertainty,"success estimate disagrees materially with independent sampled reception");
    }
    const auto rms_error=std::sqrt(squared_error/cases.size());
    std::cout<<"Sampled probability RMS error "<<rms_error<<", maximum "<<maximum_error
             <<", "<<std::chrono::duration<double>(std::chrono::steady_clock::now()-begun).count()<<" s\n";
    check(rms_error<=.18,"sampled probability matrix retains a material systematic model error");
    check(section_recoveries>=coherent_recoveries+8,
          "independent sampled phase trajectories no longer demonstrate the implemented reception improvement");
}
void sampled_partial_estimates() {
    // A bounded production-PCM check of the newly supported real covariance
    // branch. These model-error gates are not receiver sensitivity-loss gates.
    for(const double energy:{12.,15.,18.}) {
        Case fixture{"partial private",true,32,energy,.5,0,0,false,1,true};
        auto o=options(fixture);o.modem.sample_rate=64;o.modem.carrier_hz=16;
        o.modem.integration_seconds=(16401.-.25)/64;
        modem::OscillatorSearchConfig oscillator;oscillator.lf={0,.5};oscillator.rf={0,.5};
        o.modem.oscillator_search=oscillator;
        const auto prediction=simulation::estimate(transmission(o.modem,1),o,true,
            channel(fixture,o.modem,0),{},1,true,1,1024);
        check(prediction.one_bit_confidence_available&&prediction.differential_model_available&&
              prediction.differential_windows==256,"sampled partial geometry lost its real-covariance model");
        constexpr unsigned captures=32;unsigned correct=0;
        for(unsigned seed=0;seed<captures;++seed) {
            const Bytes bits{static_cast<std::uint8_t>(seed%2)};
            const auto impairment=channel(fixture,o.modem,seed+1000);
            const auto samples=capture(o.modem,impairment,bits);
            modem::SampledSimulationChannel timing(o.modem,impairment);
            // Conditional selected-lane check: cover uniform subchip mismatch
            // without rerunning an entire timing acquisition bank in each draw.
            // Search-bank arbitration is covered by receiver fixtures separately.
            const auto mismatch=((seed*17%32+.5)/32-.5)*modem::pattern_chip_samples(o.modem)/2;
            const auto origin=timing.startup_offset_samples()+modem::training_sample_count(o.modem)+
                modem::pattern_pulse_padding_samples(o.modem)+mismatch;
            modem::PatternSearch search;search.differential_window_seconds=1;search.worker_threads=1;
            search.start_offset_seconds=static_cast<double>(origin/o.modem.sample_rate);
            search.start_uncertainty_seconds=0;search.hypotheses={{0,0}};
            // Retain the full planner bank's false-acceptance spending even
            // though only its selected timing lane is replayed here. The
            // zero-accuracy policy has one carrier/clock pair; a partial
            // second introduces two private phase alternatives in that bank.
            const auto starts=std::ceil(4*(o.search_seconds+1)*o.modem.sample_rate/
                modem::pattern_chip_samples(o.modem))+1;
            const auto trials=2*starts;
            search.false_alarm_probability=2e-10/(trials*(trials+1));
            search.chunk_bits=1;search.track_limit=8;
            modem::PatternCorrelator receiver(o.modem,search,workspace);Bytes received;
            const auto end=static_cast<std::size_t>(std::ceil(origin+modem::symbol_sample_count(o.modem)));
            for(std::size_t at=0;at<end;) {
                const auto n=std::min<std::size_t>(257,end-at);
                receiver.push(std::span(samples).subspan(at,n));at+=n;
                for(const auto& event:receiver.take_bursts())received.insert(received.end(),event.bits.begin(),event.bits.end());
            }
            correct+=received==bits;
        }
        const auto observed=static_cast<double>(correct)/captures;
        const auto uncertainty=3*std::sqrt(prediction.one_bit_success_probability*
            (1-prediction.one_bit_success_probability)/captures);
        std::cout<<"partial "<<energy<<" dB: predicted "<<prediction.one_bit_success_probability
            <<", sampled "<<correct<<'/'<<captures<<std::endl;
        check(std::abs(observed-prediction.one_bit_success_probability)<=.15+uncertainty,
              "partial-window model disagrees materially with independent sampled reception");
    }
}
}
int main(int argc,char** argv) {
    try {real_atom_reduction();
        if(argc==1||std::string(argv[1])!="--atoms-only")sampled_partial_estimates();
        if(argc==1||(std::string(argv[1])!="--atoms-only"&&std::string(argv[1])!="--partial-sampled-only"))sampled_estimates();
        std::cout<<"receiver probability tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"receiver probability tests failed: "<<error.what()<<'\n';return 1;}
}

#include "datapump/channel.hpp"
#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/simulation_estimate.hpp"
#include "../src/pattern_differential.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

using namespace datapump;
namespace {
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
constexpr std::size_t workspace=8*1024*1024;
// Live reserves half of a one-profile workspace for peer/transmit/plot state.
constexpr std::size_t receiver_workspace=workspace/2;
constexpr unsigned captures_per_case=64;
constexpr unsigned maximum_workers=16;
constexpr unsigned shaped_shards=2;
constexpr double local_seconds=1;

unsigned bounded_workers(unsigned available) {
    return std::clamp(available,1U,maximum_workers);
}

template<class Function>
void for_worker_seeds(unsigned worker,unsigned workers,Function&& function) {
    for(unsigned seed=worker;seed<captures_per_case;seed+=workers)function(seed);
}

template<class Function>
void for_shard_worker_seeds(unsigned shard,unsigned worker,unsigned workers,Function&& function) {
    // Partition the selected seeds among all workers, including when the
    // number of workers is itself divisible by the number of shards.
    for(unsigned seed=shard+shaped_shards*worker;seed<captures_per_case;seed+=shaped_shards*workers)
        function(seed);
}

void check_worker_plan() {
    check(bounded_workers(0)==1&&bounded_workers(1)==1&&bounded_workers(4)==4&&
          bounded_workers(16)==16&&bounded_workers(64)==16,
          "calibration worker selection exceeded its available-core bounds");
    for(unsigned workers=1;workers<=maximum_workers;++workers) {
        std::array<unsigned,captures_per_case> visits{};
        std::array<unsigned,captures_per_case> assigned{};
        for(unsigned worker=0;worker<workers;++worker)
            for_worker_seeds(worker,workers,[&](unsigned seed) {
                ++visits[seed];assigned[seed]=worker;
            });
        for(unsigned seed=0;seed<captures_per_case;++seed)
            check(visits[seed]==1&&assigned[seed]==seed%workers,
                  "calibration worker partition changed, repeated or omitted a fixed seed");
    }
}

struct Case {
    const char* name;
    bool keyed;
    double energy_db,diffusion;
    unsigned bits=1;
    bool compare_previous=false;
    bool shaped=false;
    double window_seconds=local_seconds;
};

// Keep the two shaped cases together when reducing their original RMS gate.
// Their complete captures may be distributed, but neither case nor seed can
// disappear from the required joined result.
constexpr std::array shaped_cases{
    Case{"shaped public differential transition",false,28,8.838834764831844,1,false,true,8},
    Case{"shaped private differential transition",true,28,8.838834764831844,1,false,true,8},
};

transfer::Options options(const Case& fixture) {
    transfer::Options result;
    result.modem.sample_rate=64;
    result.modem.carrier_hz=fixture.shaped?10:16;
    result.modem.bandwidth_hz=32;
    result.modem.spreading_factor=8192;
    result.modem.integration_seconds=512*fixture.window_seconds;
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
    result.clock_error_ppm=0;
    result.phase_noise_degrees_per_sqrt_second=fixture.diffusion;
    // Independent from the estimator draws and from the older sampled matrix.
    result.seed=0x7389b0741ULL+std::uint64_t{1301081}*seed;
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
                           const Bytes& bits,bool noise_only=false) {
    modem::StreamingTransmitter source(modem::RawBits{bits},config,workspace);
    modem::SampledSimulationChannel transport(config,impairment);
    std::array<float,1021> block{};
    std::vector<float> samples;
    if(noise_only) {
        const auto length=source.total_samples()+modem::pattern_absence_samples(config)+config.sample_rate;
        samples.resize(static_cast<std::size_t>(length));
        transport.read_noise(samples);
        return samples;
    }
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

struct Received {Bytes bits;unsigned completions=0;bool compact=false;};
Received receive(const modem::Config& config,std::span<const float> samples,bool differential=true,
                 double window_seconds=local_seconds,std::size_t receiver_bytes=receiver_workspace) {
    modem::PatternSearch search;
    search.expand_clock_search=true;
    search.start_offset_seconds=static_cast<double>(modem::training_sample_count(config)+
        modem::pattern_pulse_padding_samples(config))/config.sample_rate;
    search.start_uncertainty_seconds=1;
    search.differential_window_seconds=differential?window_seconds:0;
    // Match the application's clock-window preference for long keyed codes.
    search.compact_clock_search=config.scramble && modem::symbol_sample_count(config)>=60ULL*config.sample_rate;
    search.worker_threads=1;
    search.chunk_bits=1;
    modem::PatternReceiver receiver(config,receiver_bytes,search);
    Received result;result.compact=receiver.clock_windowed();
    std::size_t position=0;
    const auto harvest=[&] {
        check(receiver.working_bytes()<=receiver_bytes,"differential probability capture exceeded receiver workspace");
        for(const auto& event:receiver.take_bursts()) {
            check(position>=event.end_sample,"local matches published a partially observed physical symbol");
            if(event.complete) {
                check(position>=event.end_sample+modem::pattern_absence_samples(config),
                      "differential probability capture completed before fully scored absence");
                ++result.completions;
            }
            result.bits.insert(result.bits.end(),event.bits.begin(),event.bits.end());
            result.bits.insert(result.bits.end(),event.missing_slots,modem::missing_pattern_bit);
        }
    };
    while(position<samples.size()) {
        const auto count=std::min<std::size_t>(1021,samples.size()-position);
        receiver.push(samples.subspan(position,count));position+=count;harvest();
    }
    receiver.finish();harvest();
    return result;
}

struct CasePrediction {bool confidence_available;double success_probability;};
CasePrediction predict(const Case& fixture) {
    const auto configured=options(fixture);
    const auto prediction=simulation::estimate(transmission(configured.modem,fixture.bits),configured,true,
        channel(fixture,configured.modem,0),{},1,true,fixture.window_seconds);
    if(!prediction.one_bit_confidence_available)
        std::cerr<<fixture.name<<": probability unavailable: "<<prediction.probability_model_limit<<'\n';
    check(prediction.one_bit_confidence_available && prediction.drift_model_available &&
          !prediction.coherent_reference_only && prediction.differential_windows==512 &&
          prediction.differential_window_seconds==fixture.window_seconds,
          "sampled differential geometry lacks a matching implemented-detector probability");
    if(fixture.bits==1) {
        check(prediction.confidence_available && std::isfinite(prediction.success_probability) &&
              prediction.success_probability>=0 && prediction.success_probability<=1,
              "single-bit differential estimate is not an available finite probability");
    } else {
        // The original held-out 001 captures exposed an actual limitation:
        // the compact receiver keeps the earliest admitted timing lane,
        // which can have a poorer fit on subsequent bits. Multiplying
        // independent nearest-lane probabilities was overly optimistic.
        // Keep these captures and require the explicit coverage exclusion
        // until a model includes that conditional ownership mechanism.
        check(!prediction.confidence_available &&
              prediction.probability_model_limit.find("timing ownership")!=std::string::npos,
              "compact multi-bit differential estimate hid unmodeled timing ownership");
    }
    return {prediction.confidence_available,prediction.success_probability};
}

struct CaptureOutcome {unsigned case_index,seed,recovered,baseline;};
std::vector<CaptureOutcome> sampled_case(const Case& fixture,unsigned case_index,unsigned workers,
                                        unsigned shard=shaped_shards) {
    const auto configured=options(fixture);
    // Independent complete captures do not share receiver state or random
    // streams. Each receiver still has one DSP worker. A shard assigns all
    // its fixed seed positions among its workers without changing the seeds.
    std::vector<std::future<std::vector<CaptureOutcome>>> tasks(workers);
    for(unsigned worker=0;worker<workers;++worker)
        tasks[worker]=std::async(std::launch::async,[&,worker] {
            std::vector<CaptureOutcome> results;
            const auto run=[&](unsigned seed) {
                const auto bits=fixture.bits==1?Bytes{static_cast<std::uint8_t>(seed%2)}:Bytes{0,0,1};
                const auto samples=capture(configured.modem,channel(fixture,configured.modem,seed),bits);
                const auto observed=receive(configured.modem,samples,true,fixture.window_seconds);
                check(observed.compact,"sampled probability matrix changed its modeled compact engine");
                CaptureOutcome result{case_index,seed,observed.completions==1 && observed.bits==bits,0};
                if(fixture.compare_previous) {
                    const auto old=receive(configured.modem,samples,false);
                    result.baseline=old.completions==1 && old.bits==bits;
                }
                results.push_back(result);
            };
            if(shard<shaped_shards)for_shard_worker_seeds(shard,worker,workers,run);
            else for_worker_seeds(worker,workers,run);
            return results;
        });
    std::vector<CaptureOutcome> results;
    for(auto& task:tasks) {
        const auto part=task.get();results.insert(results.end(),part.begin(),part.end());
    }
    std::sort(results.begin(),results.end(),[](const auto& left,const auto& right){return left.seed<right.seed;});
    return results;
}

struct MatrixReduction {
    double squared_error=0,maximum_error=0;
    unsigned improved=0,previous=0,modeled_cases=0;
    void add(const Case& fixture,CasePrediction prediction,unsigned recovered,unsigned baseline,
             std::ostream& output=std::cout) {
        const auto observed=static_cast<double>(recovered)/captures_per_case;
        const auto error=std::abs(observed-prediction.success_probability);
        if(prediction.confidence_available) {
            squared_error+=error*error;maximum_error=std::max(maximum_error,error);++modeled_cases;
        }
        if(fixture.compare_previous){improved+=recovered;previous+=baseline;}
        output<<fixture.name<<": predicted ";
        if(prediction.confidence_available)output<<prediction.success_probability;
        else output<<"unavailable (timing ownership)";
        output<<", sampled "<<recovered<<'/'<<captures_per_case;
        if(fixture.compare_previous)output<<", previous "<<baseline<<'/'<<captures_per_case;
        output<<std::endl;
        // Set before observing the holdout captures. These allow Monte Carlo
        // variation while bounding model error more tightly than the older
        // coherent/four-quarter matrix. They do not establish radio-link
        // reliability or a rare-event/false-alarm certification.
        const auto uncertainty=3*std::sqrt(prediction.success_probability*
            (1-prediction.success_probability)/captures_per_case);
        if(prediction.confidence_available)
            check(error<=.10+uncertainty,"differential prediction disagrees materially with sampled reception");
        else check(recovered>0,"unmodeled multi-bit control lost every full sampled reception");
    }
    void finish(bool compare_previous,double seconds,std::ostream& output=std::cout) const {
        check(modeled_cases>0,"differential matrix lost all of its probability coverage");
        const auto rms=std::sqrt(squared_error/modeled_cases);
        output<<"Differential probability RMS error "<<rms<<", maximum "<<maximum_error<<", "<<modeled_cases<<" modeled cases"
              <<", "<<seconds<<" s\n";
        check(rms<=.07,"differential probability matrix retains excessive systematic error");
        if(compare_previous)
            check(improved>=previous+8,"sampled differential matrix lost recovery beyond the earlier detector");
    }
};

void check_matrix(std::span<const Case> cases,bool compare_previous,unsigned workers) {
    MatrixReduction reduction;
    const auto begun=std::chrono::steady_clock::now();
    for(unsigned case_index=0;case_index<cases.size();++case_index) {
        const auto& fixture=cases[case_index];
        const auto prediction=predict(fixture);
        unsigned recovered=0,baseline=0;
        for(const auto& outcome:sampled_case(fixture,case_index,workers)) {
            recovered+=outcome.recovered;baseline+=outcome.baseline;
        }
        reduction.add(fixture,prediction,recovered,baseline);
    }
    reduction.finish(compare_previous,std::chrono::duration<double>(std::chrono::steady_clock::now()-begun).count());
}

void sampled_matrix(unsigned workers) {
    // Predeclared holdout matrix. These use exactly the production waveform,
    // fractional-start channel and adaptive receiver; only the local receiver
    // duration is scaled to make repeated PCM captures practical. No sampled
    // trajectory, source timing, bit or noise seed is given to the estimator.
    // Each 512-second symbol has 8192 chips and 512 complete 16-chip windows.
    constexpr std::array cases{
        Case{"public stable weak",false,16,0},
        Case{"public stable transition",false,18,0},
        Case{"private stable transition",true,18,0},
        Case{"public stable strong",false,22,0},
        Case{"public slow diffusion",false,24,10},
        Case{"private slow diffusion",true,24,10},
        Case{"public differential weak",false,26,25},
        Case{"public differential transition",false,29,25},
        Case{"private differential transition",true,29,25},
        Case{"public differential strong",false,32,25},
        Case{"private differential strong",true,32,25},
        Case{"public faster diffusion",false,35,40,1,true},
        Case{"private faster diffusion",true,35,40,1,true},
        Case{"public 001 diffusion",false,29,25,3},
        Case{"private 001 diffusion",true,32,25,3},
    };
    check_matrix(cases,true,workers);
}

void sampled_shaped_matrix(unsigned workers) {
    // The carrier lies exactly at the shaped signal's lower supported edge.
    // Its default finite bank therefore contains the center carrier only;
    // ordinary workspace policy selects the same compact raw-PCM receiver.
    // Evaluate these separately: passing them cannot dilute the original
    // unshaped holdout's predeclared aggregate error target. Eight-second
    // windows contain 128 chips, making all local real-PCM image/cross-image
    // covariances fit the model's explicit one-percent bound. Diffusion is
    // scaled to retain the same dimensionless 25-degree local variation.
    check_matrix(shaped_cases,false,workers);
}

struct ShapedResult {unsigned shard;std::vector<CaptureOutcome> outcomes;};
using ShapedCounts=std::array<std::pair<unsigned,unsigned>,shaped_cases.size()>;

void validate_shaped_result(const ShapedResult& result) {
    check(result.shard<shaped_shards,"shaped result has an invalid shard identity");
    check(result.outcomes.size()==shaped_cases.size()*captures_per_case/shaped_shards,
          "shaped result is missing or has extra captures");
    std::array<std::array<unsigned,captures_per_case>,shaped_cases.size()> visits{};
    for(const auto& outcome:result.outcomes) {
        check(outcome.case_index<shaped_cases.size() && outcome.seed<captures_per_case,
              "shaped result has an unknown case or seed");
        check(outcome.seed%shaped_shards==result.shard,"shaped result contains another shard's seed");
        check(outcome.recovered<=1 && outcome.baseline==0,"shaped result has an invalid capture outcome");
        check(++visits[outcome.case_index][outcome.seed]==1,"shaped result repeats a case/seed identity");
    }
    for(const auto& seeds:visits)
        for(unsigned seed=0;seed<captures_per_case;++seed)
            check(seeds[seed]==static_cast<unsigned>(seed%shaped_shards==result.shard),
                  "shaped result omits a required case/seed identity");
}

ShapedCounts join_shaped_results(std::span<const ShapedResult> results) {
    check(results.size()==shaped_shards,"shaped aggregation requires exactly two shard results");
    std::array<unsigned,shaped_shards> shards{};
    ShapedCounts counts{};
    for(const auto& result:results) {
        validate_shaped_result(result);
        check(++shards[result.shard]==1,"shaped aggregation repeats a shard identity");
        for(const auto& outcome:result.outcomes) {
            counts[outcome.case_index].first+=outcome.recovered;
            counts[outcome.case_index].second+=outcome.baseline;
        }
    }
    check(std::all_of(shards.begin(),shards.end(),[](unsigned count){return count==1;}),
          "shaped aggregation is missing a required shard");
    return counts;
}

std::string shaped_case_identity(unsigned index) {
    const auto& fixture=shaped_cases[index];
    const auto configured=options(fixture);
    std::ostringstream output;
    output<<std::setprecision(std::numeric_limits<double>::max_digits10)
          <<"case "<<index<<' '<<std::quoted(fixture.name)<<' '<<fixture.keyed<<' '
          <<fixture.energy_db<<' '<<fixture.diffusion<<' '<<fixture.bits<<' '
          <<fixture.compare_previous<<' '<<fixture.shaped<<' '<<fixture.window_seconds<<' '
          <<configured.modem.sample_rate<<' '<<configured.modem.carrier_hz<<' '
          <<configured.modem.bandwidth_hz<<' '<<configured.modem.spreading_factor<<' '
          <<configured.modem.integration_seconds<<' '<<configured.timestamp<<' '
          <<configured.search_seconds<<' '<<workspace<<' '<<receiver_workspace;
    for(auto byte:configured.modem.spreading_seed)output<<' '<<static_cast<unsigned>(byte);
    return output.str();
}

void write_shaped_result(std::ostream& output,const ShapedResult& result) {
    validate_shaped_result(result);
    // Bump the format version if capture or reduction semantics change. The
    // fixture and seed identities also reject results from a different plan.
    output<<"DATAPUMP_SHAPED_CALIBRATION_V1\n"
          <<"capture-plan 2 64 0x7389b0741 1301081\n"
          <<"shard "<<result.shard<<'\n';
    for(unsigned index=0;index<shaped_cases.size();++index)output<<shaped_case_identity(index)<<'\n';
    for(const auto& outcome:result.outcomes)
        output<<"capture "<<outcome.case_index<<' '<<outcome.seed<<' '
              <<outcome.recovered<<' '<<outcome.baseline<<'\n';
    output<<"end\n";
    output.flush();
    check(static_cast<bool>(output),"could not write shaped calibration result");
}

ShapedResult read_shaped_result(std::istream& input) {
    const auto line=[&] {
        std::string value;
        check(static_cast<bool>(std::getline(input,value)),"shaped calibration result is truncated");
        return value;
    };
    check(line()=="DATAPUMP_SHAPED_CALIBRATION_V1","unsupported shaped calibration result version");
    check(line()=="capture-plan 2 64 0x7389b0741 1301081","inconsistent shaped calibration capture plan");
    ShapedResult result{};
    {
        std::istringstream header(line());
        std::string tag;
        check(static_cast<bool>(header>>tag>>result.shard) && tag=="shard",
              "malformed shaped calibration shard header");
        header>>std::ws;
        check(header.eof(),"extra data in shaped calibration shard header");
    }
    for(unsigned index=0;index<shaped_cases.size();++index)
        check(line()==shaped_case_identity(index),"inconsistent shaped calibration fixture");
    for(unsigned capture_index=0;capture_index<shaped_cases.size()*captures_per_case/shaped_shards;++capture_index) {
        std::istringstream record(line());
        std::string tag;
        CaptureOutcome outcome{};
        check(static_cast<bool>(record>>tag>>outcome.case_index>>outcome.seed>>outcome.recovered>>outcome.baseline) &&
              tag=="capture","malformed shaped calibration capture record");
        record>>std::ws;
        check(record.eof(),"extra data in shaped calibration capture record");
        result.outcomes.push_back(outcome);
    }
    check(line()=="end","shaped calibration result lacks its completion record");
    check(input.peek()==std::char_traits<char>::eof() && !input.bad(),
          "extra data after shaped calibration completion record");
    validate_shaped_result(result);
    return result;
}

void reduce_shaped_results(std::span<const ShapedResult> results,
                           const std::array<CasePrediction,shaped_cases.size()>& predictions,
                           double seconds,std::ostream& output=std::cout) {
    const auto counts=join_shaped_results(results);
    MatrixReduction reduction;
    for(unsigned index=0;index<shaped_cases.size();++index)
        reduction.add(shaped_cases[index],predictions[index],counts[index].first,counts[index].second,output);
    reduction.finish(false,seconds,output);
}

void sampled_shaped_shard(unsigned workers,unsigned shard,const std::string& path) {
    // Fail early for an unusable path and invalidate any previous result.
    // Only fully completed captures receive the required completion record.
    std::ofstream output(path,std::ios::trunc);
    check(output.is_open(),"could not open shaped calibration result path");
    ShapedResult result{shard,{}};
    for(unsigned index=0;index<shaped_cases.size();++index) {
        // Keep the implemented-detector geometry checks in the capture job;
        // statistical assertions require both shards and run in aggregation.
        (void)predict(shaped_cases[index]);
        const auto part=sampled_case(shaped_cases[index],index,workers,shard);
        result.outcomes.insert(result.outcomes.end(),part.begin(),part.end());
        std::cout<<shaped_cases[index].name<<": completed "<<part.size()
                 <<"/64 captures in shard "<<shard<<"; aggregate validation required"<<std::endl;
    }
    write_shaped_result(output,result);
    output.close();
    check(static_cast<bool>(output),"could not close shaped calibration result");
    std::cout<<"shaped capture coverage completed; aggregate validation required: "<<path<<'\n';
}

void aggregate_shaped_results(const std::array<std::string,shaped_shards>& paths) {
    const auto begun=std::chrono::steady_clock::now();
    std::array<ShapedResult,shaped_shards> results;
    for(unsigned index=0;index<shaped_shards;++index) {
        std::ifstream input(paths[index]);
        check(input.is_open(),"could not open required shaped calibration result");
        results[index]=read_shaped_result(input);
    }
    // Validate coverage before doing any estimation, including duplicate files.
    (void)join_shaped_results(results);
    std::array<CasePrediction,shaped_cases.size()> predictions;
    for(unsigned index=0;index<shaped_cases.size();++index)predictions[index]=predict(shaped_cases[index]);
    reduce_shaped_results(results,predictions,
        std::chrono::duration<double>(std::chrono::steady_clock::now()-begun).count());
}

void check_shard_plan() {
    for(unsigned workers=1;workers<=maximum_workers;++workers) {
        std::array<unsigned,captures_per_case> visits{};
        for(unsigned shard=0;shard<shaped_shards;++shard)
            for(unsigned worker=0;worker<workers;++worker)
                for_shard_worker_seeds(shard,worker,workers,[&](unsigned seed) {
                    ++visits[seed];
                    check(seed%shaped_shards==shard && (seed/shaped_shards)%workers==worker,
                          "shaped shard changed fixed seed assignment");
                });
        check(std::all_of(visits.begin(),visits.end(),[](unsigned count){return count==1;}),
              "shaped shards repeat or omit fixed seeds");
    }
    const auto rejects=[](auto&& operation,const char* message) {
        bool rejected=false;
        try {operation();} catch(const Error&) {rejected=true;}
        check(rejected,message);
    };
    std::array<ShapedResult,shaped_shards> results;
    for(unsigned shard=0;shard<shaped_shards;++shard) {
        results[shard].shard=shard;
        for(unsigned index=0;index<shaped_cases.size();++index)
            for(unsigned seed=shard;seed<captures_per_case;seed+=shaped_shards)
                results[shard].outcomes.push_back({index,seed,static_cast<unsigned>(seed<32),0});
    }
    std::ostringstream encoded;
    for(auto& result:results) {
        std::ostringstream serialized;
        write_shaped_result(serialized,result);
        std::istringstream input(serialized.str());
        result=read_shaped_result(input);
        if(result.shard==0)encoded<<serialized.str();
    }
    const auto counts=join_shaped_results(results);
    check(counts[0].first==32 && counts[1].first==32 && counts[0].second==0 && counts[1].second==0,
          "shaped aggregation changed complete-case counts");
    const std::array predictions{CasePrediction{true,.5},CasePrediction{true,.5}};
    std::ostringstream output;
    reduce_shaped_results(results,predictions,0,output);
    // A single case's 6/64 error exceeds .07 while the two-case RMS remains
    // below .07. Preserving that accepted result rules out a tighter but
    // different per-case replacement for the original aggregate assertion.
    auto asymmetric=results;
    for(auto& result:asymmetric)
        for(auto& outcome:result.outcomes)
            outcome.recovered=outcome.seed<(outcome.case_index==0?38U:32U);
    reduce_shaped_results(asymmetric,predictions,0,output);
    auto reversed=results;
    std::swap(reversed[0],reversed[1]);
    check(join_shaped_results(reversed)==counts,"shaped aggregation depends on file order");
    rejects([&]{join_shaped_results(std::span(results).first(1));},"shaped aggregate accepted a missing shard");
    auto duplicate=results;
    duplicate[1]=duplicate[0];
    rejects([&]{join_shaped_results(duplicate);},"shaped aggregate accepted duplicate shards");
    const auto invalid_result=[&](auto&& change) {
        auto invalid=results;
        change(invalid[0]);
        rejects([&]{join_shaped_results(invalid);},"shaped aggregate accepted invalid capture coverage");
    };
    invalid_result([](auto& result){result.outcomes.pop_back();});
    invalid_result([](auto& result){result.outcomes[1]=result.outcomes[0];});
    invalid_result([](auto& result){result.outcomes[0].seed=1;});
    invalid_result([](auto& result){result.outcomes[0].seed=64;});
    invalid_result([](auto& result){result.outcomes[0].case_index=2;});
    invalid_result([](auto& result){result.outcomes[0].recovered=2;});
    invalid_result([](auto& result){result.outcomes[0].baseline=1;});
    invalid_result([](auto& result){result.shard=2;});
    const auto invalid_text=[&](const std::string& old,const std::string& replacement) {
        auto text=encoded.str();
        const auto found=text.find(old);
        check(found!=std::string::npos,"shaped parser fixture could not locate its mutation");
        text.replace(found,old.size(),replacement);
        std::istringstream malformed(text);
        rejects([&]{read_shaped_result(malformed);},"shaped result parser accepted malformed or inconsistent data");
    };
    invalid_text("_V1","_V2");
    invalid_text("capture-plan 2 64","capture-plan 2 63");
    invalid_text("case 0","case 9");
    invalid_text("shard 0","shard 0 extra");
    invalid_text("capture 0 0 1 0","capture 0 0 1 0 extra");
    invalid_text("capture 0 0 1 0","capture 0 0 invalid 0");
    invalid_text("end\n","");
    invalid_text("end\n","end\nextra\n");
    // Both cases separately pass the original uncertainty bound at 40/64,
    // but their joined RMS fails. Do not replace this gate with per-shard or
    // per-case limits, or accidentally dilute it with the unshaped matrix.
    auto systematic=results;
    for(auto& result:systematic)
        for(auto& outcome:result.outcomes)outcome.recovered=outcome.seed<40;
    MatrixReduction per_case;
    for(const auto& fixture:shaped_cases)per_case.add(fixture,{true,.5},40,0,output);
    rejects([&]{reduce_shaped_results(systematic,predictions,0,output);},
            "shaped aggregate lost its joined RMS assertion");
    auto materially_wrong=results;
    for(auto& result:materially_wrong)
        for(auto& outcome:result.outcomes)outcome.recovered=1;
    MatrixReduction per_case_failure;
    rejects([&]{per_case_failure.add(shaped_cases[0],{true,.5},64,0,output);},
            "shaped aggregate lost its per-case probability assertion");
    rejects([&]{reduce_shaped_results(materially_wrong,predictions,0,output);},
            "shaped aggregate accepted materially wrong probabilities");
}

void null_controls() {
    const Case fixture{"null controls",true,35,40};
    const auto configured=options(fixture);
    for(unsigned seed=0;seed<16;++seed) {
        const Bytes bits{static_cast<std::uint8_t>(seed%2)};
        const auto noise=capture(configured.modem,channel(fixture,configured.modem,seed),bits,true);
        const auto null=receive(configured.modem,noise);
        check(null.bits.empty() && null.completions==0,"differential probability null control admitted noise");
        auto wrong=configured.modem;wrong.spreading_seed[0]^=1;
        const auto signal=capture(configured.modem,channel(fixture,configured.modem,seed),bits);
        const auto unrelated=receive(wrong,signal);
        check(unrelated.bits.empty() && unrelated.completions==0,"differential probability null control admitted a wrong key");
    }
}

void default_window_capture() {
    // Run the real 100-second default too. Carrier, chip and diffusion rates
    // are scaled together; this remains sampled PCM through the complete
    // 14.2-hour bit and one equally long observed absence. It establishes
    // implementation coverage at the default duration, not a four-capture
    // estimate of a rare failure probability.
    for(bool keyed:{false,true}) {
        Case fixture{"default 100-second windows",keyed,35,2.5};
        auto configured=options(fixture);
        configured.modem.carrier_hz=.16;
        configured.modem.bandwidth_hz=.32;
        configured.modem.integration_seconds=51200;
        configured.dsp_workspace_bytes=32*1024*1024;
        const auto window=modem::detail::differential_window_samples(
            modem::symbol_sample_count(configured.modem),modem::pattern_chip_samples(configured.modem),
            configured.modem.sample_rate,modem::PatternSearch{}.differential_window_seconds);
        check(window==6400 && modem::symbol_sample_count(configured.modem)/window==512,
              "default sampled capture missed receiver differential eligibility");
        const auto prediction=simulation::estimate(transmission(configured.modem,1),configured,true,
            channel(fixture,configured.modem,0));
        check(prediction.confidence_available && prediction.differential_windows==512 &&
              prediction.differential_window_seconds==100 && prediction.success_probability>.99,
              "default-window strong-link estimate did not model differential evidence");
        for(unsigned seed=0;seed<2;++seed) {
            const Bytes bits{static_cast<std::uint8_t>(seed)};
            const auto pcm=capture(configured.modem,channel(fixture,configured.modem,seed+211),bits);
            const auto observed=receive(configured.modem,pcm,true,100,16*1024*1024);
            check(observed.compact==keyed,"default duration coverage missed the intended FFT/compact engines");
            check(observed.completions==1 && observed.bits==bits,
                  "default 100-second differential window failed full sampled reception");
        }
    }
}
}
int main(int argc,char** argv) {
    try {
        auto workers=bounded_workers(std::thread::hardware_concurrency());
        bool worker_plan=false,shard_plan=false,workers_set=false,section_set=false;
        bool shard_set=false,result_set=false,aggregate=false;
        unsigned shard=0;
        std::string result_path;
        std::array<std::string,shaped_shards> aggregate_paths;
        std::string_view section="all";
        for(int arg=1;arg<argc;++arg) {
            const std::string_view option=argv[arg];
            if(option=="--check-worker-plan"&&!worker_plan)worker_plan=true;
            else if(option=="--check-shard-plan"&&!shard_plan)shard_plan=true;
            else if(option=="--workers"&&!workers_set&&arg+1<argc) {
                const std::string_view value=argv[++arg];
                const auto parsed=std::from_chars(value.data(),value.data()+value.size(),workers);
                check(parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()&&
                      workers>=1&&workers<=maximum_workers,
                      "calibration --workers must be an integer from 1 to 16");
                workers_set=true;
            } else if(option=="--section"&&!section_set&&arg+1<argc) {
                section=argv[++arg];
                check(section=="all"||section=="matrix"||section=="shaped"||section=="null"||section=="default-window",
                      "calibration --section must be all, matrix, shaped, null or default-window");
                section_set=true;
            } else if(option=="--shaped-shard"&&!shard_set&&arg+1<argc) {
                const std::string_view value=argv[++arg];
                check(value=="0"||value=="1","calibration --shaped-shard must be 0 or 1");
                shard=static_cast<unsigned>(value[0]-'0');shard_set=true;
            } else if(option=="--shaped-result"&&!result_set&&arg+1<argc) {
                result_path=argv[++arg];result_set=true;
                check(!result_path.empty(),"calibration --shaped-result requires a nonempty path");
            } else if(option=="--aggregate-shaped"&&!aggregate&&arg+2<argc) {
                aggregate_paths[0]=argv[++arg];aggregate_paths[1]=argv[++arg];aggregate=true;
            } else throw Error("Usage: test_differential_receiver_probability [--workers 1..16] [--section all|matrix|shaped|null|default-window] [--check-worker-plan] [--check-shard-plan] [--shaped-shard 0|1 --shaped-result PATH] | --aggregate-shaped PATH0 PATH1");
        }
        check(!aggregate || !(workers_set||section_set||shard_set||result_set||worker_plan||shard_plan),
              "calibration --aggregate-shaped cannot be combined with capture or plan options");
        check(shard_set==result_set,"calibration --shaped-shard and --shaped-result must be specified together");
        check(!shard_set || section=="shaped","calibration sharding requires --section shaped");
        check_worker_plan();
        check_shard_plan();
        if(aggregate) {
            std::cout<<"Differential receiver calibration: aggregate two shaped shards, "
                     <<captures_per_case<<" fixed seeds per case"<<std::endl;
            aggregate_shaped_results(aggregate_paths);
            std::cout<<"shaped differential receiver probability aggregate tests passed\n";
            return 0;
        }
        std::cout<<"Differential receiver calibration: "<<workers<<" independent capture workers, "
                 <<captures_per_case<<" fixed seeds per matrix case; section "<<section;
        if(shard_set)std::cout<<"; shaped shard "<<shard<<'/'<<shaped_shards<<" (32 seeds per case; aggregate required)";
        std::cout<<std::endl;
        if(worker_plan||shard_plan) {
            std::cout<<"worker partitions 1..16 and both shaped shards preserve all fixed seeds; "
                     <<"strict result parsing and shared aggregate gates checked; no calibration run\n";
            return 0;
        }
        if(shard_set) {
            sampled_shaped_shard(workers,shard,result_path);
            return 0;
        }
        // Keep each matrix whole: its aggregate error and improvement checks
        // must not be replaced by assertions on independently selected cases.
        if(section=="all"||section=="matrix")sampled_matrix(workers);
        if(section=="all"||section=="shaped")sampled_shaped_matrix(workers);
        if(section=="all"||section=="null")null_controls();
        if(section=="all"||section=="default-window")default_window_capture();
        std::cout<<"differential receiver probability tests passed\n";return 0;}
    catch(const std::exception& error){std::cerr<<"differential receiver probability tests failed: "<<error.what()<<'\n';return 1;}
}

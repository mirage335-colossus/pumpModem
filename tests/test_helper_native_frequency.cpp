#include "datapump/helper_carrier.hpp"
#include "datapump/helper_frontend.hpp"
#include "datapump/helper_receiver.hpp"
#include "datapump/types.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <numbers>
#include <random>
#include <string>

using namespace datapump;
namespace {
constexpr std::uint32_t rate=48960;
constexpr double amplitude=.01,cn0=49.7403126773,cfo=3000,clock_ppm=100,delay=.03719,seconds=12;
constexpr double phase_sigma=.5*std::numbers::pi/180,tau=2*std::numbers::pi;
// The original HF Auto trace's nominal observer center and declared RF band,
// before coarse peak detection. Neither is the injected or measured mean CFO.
constexpr double coarse_expected=2462.78875,coarse_width=9000.4500000000007;
constexpr std::uint64_t capture_revision=77;
void check(bool condition,const char* message) {if(!condition)throw Error(message);}
double cpu() {return static_cast<double>(std::clock())/CLOCKS_PER_SEC;}
struct Stats {
    std::size_t references=0,global=0,bootstrap=0,misses=0,looks=0,commits=0,declines=0;
    std::size_t cells=0,work_units=0,calls=0,max_units=0,zero_cell_calls=0,coarse_candidates=0,unsupported_seeds=0;
    double first_global=-1,first_bootstrap=-1,min_ci=std::numeric_limits<double>::infinity(),max_ci=0,max_error=0;
    double ingest_cpu=0,analysis_cpu=0,max_advance_wall=0;
};
void check_geometry() {
    helper::FrequencyWorkOptions work;work.workspace_grant=helper::Receiver::optional_frequency_workspace_required();
    work.coherent_phase_model=true;
    for(const auto fs:{24000U,48000U,48960U}) {
        work.sample_rate=fs;
        const auto g=helper::Receiver::optional_frequency_geometry(5462.75,20,work);
        check(g.eligible&&g.supported&&g.bin_samples==fs/250&&g.aperture_bins<=2048,
              "native allowlist changed an existing geometry");
        check(g.aperture_seconds<=8&&g.aperture_seconds>7.99&&g.cold_staging_deadline_seconds==10,
              "unexpected full-fit geometry");
        check(g.required_bytes==work.workspace_grant&&g.required_bytes==g.heap_bytes+g.worker_scratch_bytes&&
              g.worker_scratch_bytes==helper::Receiver::optional_frequency_worker_scratch_bound,
              "unfunded optional storage");
        std::cout<<"GEOMETRY,rate="<<fs<<",bin="<<g.bin_samples<<",bins="<<g.aperture_bins
            <<",fit="<<g.aperture_seconds<<",startup="<<g.startup_support_seconds
            <<",grant="<<g.required_bytes<<",heap="<<g.heap_bytes<<",worker="<<g.worker_scratch_bytes<<'\n';
    }
}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"usage: test_native signal|noise output-directory");
        const std::string which=argv[1];check(which=="signal"||which=="noise","unknown native case");
        const bool signal=which=="signal";
        std::cout<<std::setprecision(17)<<std::unitbuf;check_geometry();
        check(argv[2][0]!='\0',"native test requires an explicit output directory");
        std::filesystem::create_directories(argv[2]);
        std::ofstream references(std::string(argv[2])+"/references.csv"),looks(std::string(argv[2])+"/looks.csv");
        check(references.good()&&looks.good(),"cannot open native evidence output");
        references<<std::setprecision(17)<<"available,first,sample,frequency_sample,frequency,error,ci,fit,phase,timing,metadata\n";
        looks<<std::setprecision(17)<<"available,first,end,cells,units,recent_presence,snr_db,committed,decline\n";
        helper::Options options;options.power_w=15000;options.data_power_w=4;options.timecode=true;
        // Actual A waveform geometry: Fs6120/Rate36/Interleave10 quantizes a
        // fine chip to34 logical samples, hence 1.25*6120/34=225Hz RRC support.
        // The helper-only component does not carry or decode Robust payload.
        const auto public_plan=helper::plan(options,1500,225,rate);
        check(public_plan.frequency_hz==2462.5,"HF helper placement changed");
        auto received_plan=public_plan;received_plan.helper_gain=amplitude;received_plan.data_gain=0;
        const Crypto key{std::array<std::uint8_t,32>{}};
        helper::Generator generator(options,received_plan,1800000000,true,key);
        const auto truth=public_plan.frequency_hz*(1+clock_ppm*1e-6)+cfo;
        const helper::CoarseToneSearch::Config coarse_config{rate,coarse_expected,coarse_width,4096,3};
        const auto coarse_grant=helper::CoarseToneSearch::workspace_required(coarse_config);
        helper::CoarseToneSearch coarse(coarse_config,coarse_grant);
        const auto band=coarse.capture_band();
        check(truth>=band.lower_hz&&truth<=band.upper_hz,"injected reference outside actual capture passband");
        check(helper::CoarseToneSearch::seed_half_width_bound(coarse_config)==25.90625,
              "native FFT engineering envelope changed");
        const helper::FrequencyPrecisionOptions precision{0,1000,phase_sigma};
        std::unique_ptr<helper::Receiver> receiver;
        std::optional<helper::ToneCandidate> seed;
        std::uint64_t narrow_first=0,available=0;
        std::mt19937_64 random(1),phase_random(1^0xa0761d6478bd642fULL);
        const auto noise_sigma=std::sqrt(amplitude*amplitude*rate/(4*std::pow(10.,cn0/10)));
        std::normal_distribution<double> noise(0,noise_sigma),phase_step(0,phase_sigma/std::sqrt(rate));
        double phase=.81;std::array<float,137> samples{};Stats stats;
        std::cout<<"PARAMETERS,case="<<which<<",rate="<<rate<<",cn0="<<cn0<<",nominal_helper="<<public_plan.frequency_hz
            <<",coarse_expected="<<coarse_expected<<",coarse_width="<<coarse_width<<",capture_low="<<band.lower_hz
            <<",capture_high="<<band.upper_hz<<",fft=4096,persistence=3,truth="<<truth<<",cfo="<<cfo
            <<",clock_ppm="<<clock_ppm<<",phase_sigma="<<phase_sigma<<",maximum_audio_clock_ppm=1000,delay="<<delay
            <<",coarse_bytes="<<coarse.workspace_bytes()<<",coarse_grant="<<coarse_grant
            <<",seconds="<<seconds<<",encrypted_metadata=1,known_receiver_timestamp=0,payload_present=0"
            <<",signed_RF_branch_authority=0,coarse_seed_is_confidence=0,common_phase_model=continuous_Generator\n";
        const auto consume=[&] {
            if(!receiver)return;
            if(const auto r=receiver->take_update()) {
                ++stats.references;
                const auto error=std::abs(r->frequency_hz-truth);
                const bool valid=r->frequency_valid&&std::isfinite(r->frequency_hz)&&
                    std::isfinite(r->frequency_uncertainty_hz)&&r->frequency_uncertainty_hz>=0;
                if(!valid||!(error<=r->frequency_uncertainty_hz))++stats.misses;
                check(r->frequency_first_sample>=narrow_first&&r->frequency_first_sample<=r->frequency_sample&&
                      r->frequency_sample<=r->sample&&r->sample<available,
                      "narrow reference reused coarse/older PCM or invalid sample dates");
                stats.max_error=std::max(stats.max_error,error);stats.min_ci=std::min(stats.min_ci,r->frequency_uncertainty_hz);
                stats.max_ci=std::max(stats.max_ci,r->frequency_uncertainty_hz);
                if(r->frequency_fit==helper::FrequencyFit::masked_tone_profile) {
                    check(!r->phase_valid&&!r->timing_valid&&!r->phase_observation_known&&!r->metadata&&
                          !r->punctuation.known,"native global reference manufactured phase/metadata/marker authority");
                    if(!stats.global)stats.first_global=static_cast<double>(r->sample)/rate;
                    ++stats.global;
                    if(valid&&r->frequency_uncertainty_hz<=.25) {
                        if(!stats.bootstrap)stats.first_bootstrap=static_cast<double>(r->sample)/rate;
                        ++stats.bootstrap;
                    }
                }
                references<<available<<','<<r->frequency_first_sample<<','<<r->sample<<','<<r->frequency_sample<<','
                    <<r->frequency_hz<<','<<error<<','<<r->frequency_uncertainty_hz<<','<<static_cast<int>(r->frequency_fit)
                    <<','<<r->phase_valid<<','<<r->timing_valid<<','<<bool(r->metadata)<<'\n';
            }
        };
        const auto start_cpu=cpu();const auto start=std::chrono::steady_clock::now();
        for(std::uint64_t first=0;first<static_cast<std::uint64_t>(seconds*rate);first+=samples.size()) {
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(samples.size(),seconds*rate-first));
            for(std::size_t i=0;i<count;++i) {
                const auto time=static_cast<long double>(first+i)/rate;
                const auto source_time=time*(1+clock_ppm*1e-6)-delay;phase+=phase_step(phase_random);
                const auto helper=!signal||source_time<0?0.:std::real(generator.mix_analytic({},source_time,rate)*
                    std::polar(1.,phase+tau*cfo*static_cast<double>(time)));
                samples[i]=static_cast<float>(helper+noise(random));
            }
            available=first+count;
            auto started=cpu();
            if(!receiver) {
                coarse.push(std::span<const float>(samples).first(count),first);
                if(auto candidate=coarse.take_candidate()) {
                    ++stats.coarse_candidates;
                    check(candidate->first_sample<candidate->end_sample&&candidate->end_sample<=available&&candidate->coherent_support>=3&&
                          std::isfinite(candidate->frequency_hz)&&std::isfinite(candidate->frequency_half_width_hz)&&
                          candidate->frequency_half_width_hz==helper::CoarseToneSearch::seed_half_width_bound(coarse_config),
                          "invalid coarse seed evidence or engineering envelope");
                    helper::FrequencyWorkOptions work;work.sample_rate=rate;work.coherent_phase_model=true;
                    work.workspace_grant=helper::Receiver::optional_frequency_workspace_required();
                    // No truth-chosen narrower prior: retain the ENTIRE actual
                    // coarse engineering band, including every compatible cell.
                    const auto g=helper::Receiver::optional_frequency_geometry(candidate->frequency_hz,
                        candidate->frequency_half_width_hz,work,precision);
                    if(!g.eligible) {
                        ++stats.unsupported_seeds;
                        std::cout<<"UNSUPPORTED_SEED,frequency="<<candidate->frequency_hz<<",half_width="<<candidate->frequency_half_width_hz
                            <<",first="<<candidate->first_sample<<",end="<<candidate->end_sample
                            <<",decline="<<static_cast<int>(g.decline)<<",receiver_constructed=0\n";
                        check(!signal,"unexpected unsupported native signal seed");
                    } else {
                        if(signal)check(g.bin_samples==195&&g.aperture_bins==329,"unexpected native modeled geometry");
                        seed=*candidate;narrow_first=seed->end_sample;
                        receiver=std::make_unique<helper::Receiver>(seed->frequency_hz,seed->frequency_half_width_hz,
                            std::vector<Crypto>{key},std::nullopt,precision);
                        check(receiver->set_optional_frequency_work(work),"native optional grant rejected");
                        std::cout<<"SEED,first="<<seed->first_sample<<",end="<<seed->end_sample<<",available="<<available
                            <<",frequency="<<seed->frequency_hz<<",half_width="<<seed->frequency_half_width_hz
                            <<",support_windows="<<seed->coherent_support<<",qualified=0,bin="<<g.bin_samples
                            <<",bins="<<g.aperture_bins<<",fit="<<g.aperture_seconds<<",cadence="<<g.look_cadence_seconds
                            <<",startup="<<g.startup_support_seconds<<",cold_deadline="<<g.cold_staging_deadline_seconds
                            <<",eligible="<<g.eligible<<",required_bytes="<<g.required_bytes<<",heap="<<g.heap_bytes<<",worker="<<g.worker_scratch_bytes<<'\n';
                    }
                }
            }
            if(receiver) {
                const auto skip=static_cast<std::size_t>(std::min<std::uint64_t>(count,narrow_first>first?narrow_first-first:0));
                if(skip<count)receiver->push(std::span<const float>(samples).subspan(skip,count-skip),rate,first+skip);
            }
            stats.ingest_cpu+=cpu()-started;consume();started=cpu();
            if(receiver&&receiver->prepare_optional_frequency(capture_revision)) {
                ++stats.looks;
                for(;;) {
                    const auto before=std::chrono::steady_clock::now();
                    const auto step=receiver->advance_optional_frequency(64);
                    stats.max_advance_wall=std::max(stats.max_advance_wall,
                        std::chrono::duration<double>(std::chrono::steady_clock::now()-before).count());
                    check(step.processed_cells<=64&&step.processed_work_units<=8,"native optional stage exceeded its bound");
                    ++stats.calls;stats.zero_cell_calls+=!step.processed_cells;stats.max_units=std::max(stats.max_units,step.processed_work_units);
                    if(step.complete)break;
                }
                const auto d=receiver->optional_frequency_work();
                check(d.state==helper::FrequencyWorkState::all_cells_complete&&d.processed_cells==d.total_cells&&
                      d.first_sample>=narrow_first,"partial/coarse-supported work reached native commit");
                stats.cells+=d.total_cells;stats.work_units+=d.processed_work_units;
                const auto committed=receiver->commit_optional_frequency({capture_revision,d.generation,0,
                    helper::FrequencyPresenceState::unavailable});
                stats.commits+=committed;stats.declines+=!committed;
                looks<<available<<','<<d.first_sample<<','<<d.end_sample<<','<<d.total_cells<<','<<d.processed_work_units
                    <<','<<d.recent_presence_qualified<<','<<d.presence_snr_db<<','<<committed<<','
                    <<static_cast<int>(receiver->optional_frequency_work().decline)<<'\n';
            }
            stats.analysis_cpu+=cpu()-started;consume();
        }
        const auto wall=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout<<"RESULT,case="<<which<<",seeded="<<bool(seed)<<",narrow_first="<<narrow_first
            <<",coarse_candidates="<<stats.coarse_candidates<<",unsupported_seeds="<<stats.unsupported_seeds
            <<",references="<<stats.references<<",global="<<stats.global<<",bootstrap="<<stats.bootstrap<<",misses="<<stats.misses
            <<",first_global="<<stats.first_global<<",first_bootstrap="<<stats.first_bootstrap<<",min_ci="<<stats.min_ci
            <<",max_ci="<<stats.max_ci<<",max_error="<<stats.max_error<<",looks="<<stats.looks<<",commits="<<stats.commits
            <<",declines="<<stats.declines<<",cells="<<stats.cells<<",work_units="<<stats.work_units<<",calls="<<stats.calls
            <<",max_units="<<stats.max_units<<",zero_cell_calls="<<stats.zero_cell_calls<<",max_advance_wall="<<stats.max_advance_wall
            <<",ingest_cpu="<<stats.ingest_cpu<<",analysis_cpu="<<stats.analysis_cpu<<",cpu_per_audio="
            <<(stats.ingest_cpu+stats.analysis_cpu)/seconds<<",total_cpu="<<cpu()-start_cpu<<",wall="<<wall<<'\n';
        check(stats.misses==0,"native published frequency interval missed test-only truth");
        if(signal)check(seed&&stats.global&&stats.bootstrap&&stats.first_bootstrap<10,"no native bootstrap within fixed cold deadline");
        else check(stats.references==0,"native noise-only reference admitted");
        std::cout<<"PASS,"<<which<<'\n';return 0;
    }catch(const std::exception& error) {std::cerr<<"FAIL,"<<error.what()<<'\n';return 1;}
}

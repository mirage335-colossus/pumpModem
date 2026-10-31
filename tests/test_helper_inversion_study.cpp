// Standalone received-sample marker measurement, with the existing physical
// Helper/noise/FHSS fixture. Truth is used only AFTER a dated result is emitted.
#include "datapump/helper_inversion_timing.hpp"
#include "datapump/helper_measurement.hpp"
#include "helper_live_probe_fixture.hpp"
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace h=datapump::helper;
namespace hp=helper_live_probe;
namespace {
double cpu(){return static_cast<double>(std::clock())/CLOCKS_PER_SEC;}
void check(bool v,const char* message){if(!v)throw std::runtime_error(message);}
}
int main(int argc,char** argv){try{
    hp::Config cfg;cfg.path_loss_db=argc>1?std::stod(argv[1]):120;
    const std::string mode=argc>2?argv[2]:"plain";
    check(mode=="plain"||mode=="fhss"||mode=="gap"||mode=="noise","mode must be plain/fhss/gap/noise");
    cfg.hopping=mode=="fhss";cfg.seed=argc>3?std::stoull(argv[3]):1910000001ULL;
    cfg.expanded_schedule_key=true;cfg.noise_seed=cfg.seed+30'000'000ULL;
    const unsigned dwells=argc>4?static_cast<unsigned>(std::stoul(argv[4])):15;
    check(dwells>=5&&dwells<=100,"study duration must be 5..100 dwells");
    hp::Source source(cfg);
    h::HelperMeasurementConfig mc;mc.sample_rate=hp::sample_rate;
    mc.high_search_min_hz=2300;mc.high_search_max_hz=2500;mc.audio_error_bound_ppm=200;
    mc.phase_noise_radians_per_sqrt_second=.05*std::numbers::pi/180;
    h::HelperMeasurement pilots(mc);
    std::unique_ptr<h::HelperInversionTiming> detector;
    h::HelperInversionTiming::Config centers;
    unsigned accepted=0,at36=0,recenters=0,after_gap=0,covered=0;
    double square=0,worst=0,max_latency=0,generation_cpu=0,pilot_cpu=0,marker_cpu=0,evaluation_cpu=0;
    std::mt19937_64 noise_rng(cfg.seed+40'000'000ULL);std::normal_distribution<float> noise;
    std::uint64_t last_marker=0;
    for(unsigned dwell=0;dwell<dwells;++dwell){
        double start=cpu();auto capture=source.generate(dwell);
        if(mode=="noise")for(auto& z:capture.samples)z={noise(noise_rng),noise(noise_rng)};
        generation_cpu+=cpu()-start;
        // A missing capture is an explicit gap; no zeros or fabricated marker.
        if(mode=="gap"&&dwell==5){if(detector)detector->missing((dwell+1ULL)*hp::dwell_samples);continue;}
        start=cpu();const auto measurement=pilots.observe(std::span(capture.samples).subspan(480,8640),
            capture.shift_hz,capture.first_seconds+.02);pilot_cpu+=cpu()-start;
        start=cpu();
        if(measurement.valid&&(!detector||std::abs(measurement.high_frequency_hz-centers.high_hz)>8||
                std::abs(measurement.low_frequency_hz-centers.low_hz)>8)){
            centers.high_hz=measurement.high_frequency_hz;centers.low_hz=measurement.low_frequency_hz;
            centers.auxiliary_hz=centers.low_hz+(950.-550.)/(2400.-550.)*(centers.high_hz-centers.low_hz);
            // Fresh measured frequency centers, never oscillator truth. Reset
            // only this optional detector; this study supplies no correction.
            if(detector)++recenters;
            detector=std::make_unique<h::HelperInversionTiming>(centers);
        }
        // Exercise noise-only discovery even if no pilot acquisition succeeds.
        if(!detector&&mode=="noise")detector=std::make_unique<h::HelperInversionTiming>(centers);
        if(detector)detector->observe(capture.samples,static_cast<std::uint64_t>(dwell)*hp::dwell_samples);
        marker_cpu+=cpu()-start;
        start=cpu();
        if(detector)for(auto p=detector->take_update();p.known;p=detector->take_update()){
            check(!accepted||p.sample>last_marker,"duplicate/backward marker");last_marker=p.sample;
            check(p.last_sample<(dwell+1ULL)*hp::dwell_samples,"marker uses future data");
            const double date=(p.sample+p.fraction)/hp::sample_rate;
            // Evaluation only: compare to the transmitted 160ms marker;
            // no truth-derived dates or coefficients are sent to a receiver.
            const double error=std::remainder(source.tx_time(date)-.160,.4)/source.audio_scale_at(date);
            square+=error*error;worst=std::max(worst,std::abs(error));++accepted;
            covered+=std::abs(error)*hp::sample_rate<=p.uncertainty_samples;
            if(dwell>5)++after_gap;
            max_latency=std::max(max_latency,(dwell+1)*.4-date);
        }
        evaluation_cpu+=cpu()-start;
        if(dwell==89)at36=accepted; // frozen; no later observation may revise it
    }
    std::cout<<std::setprecision(10)<<"helper_inversion_study mode="<<mode<<" loss_db="<<cfg.path_loss_db
      <<" seed="<<cfg.seed<<" noise_seed="<<*cfg.noise_seed<<" capture_seconds="<<dwells*.4
      <<" helper_w="<<cfg.helper_w<<" probe_w="<<cfg.probe_w<<" simultaneous_w="<<cfg.helper_w+cfg.probe_w
      <<" probe_capture_begin_seconds=38 temperature_K="<<cfg.temperature_kelvin<<" noise_figure_db="<<cfg.noise_figure_db
      <<" nominal_source_high_cn0_db_hz="<<source.high_cn0_db_hz()<<" noise_only_replaced_signal="<<(mode=="noise")
      <<" markers="<<accepted<<" markers_available_at36="<<at36
      <<" has36_snapshot="<<(dwells>=90)<<" rms_us="<<(accepted?std::sqrt(square/accepted)*1e6:0)
      <<" worst_us="<<worst*1e6<<" reported_radius_covered="<<covered<<" post_gap_markers="<<(mode=="gap"?after_gap:0)
      <<" max_capture_to_publication_seconds="<<max_latency<<" frequency_recenters="<<recenters
      <<" pilot_cpu_seconds="<<pilot_cpu<<" marker_cpu_seconds="<<marker_cpu<<" receiver_cpu_seconds="<<pilot_cpu+marker_cpu
      <<" generation_cpu_seconds="<<generation_cpu<<" evaluation_cpu_seconds="<<evaluation_cpu
      <<" marker_workspace_bytes="<<(detector?detector->workspace_bytes():0)<<" boundary_correction_applied=0\n";
    if(mode=="noise")check(accepted==0,"noise falsely qualified as inversion");
    if(cfg.path_loss_db<=120&&mode!="noise")check(accepted>=2,"strong received-sample inversion unavailable");
    if(cfg.path_loss_db<=120&&mode=="gap")check(after_gap>=2,"marker detector failed to recover from capture gap");
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

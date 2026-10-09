#include "datapump/tuning.hpp"
#include "datapump/transfer.hpp"
#include "datapump/runtime.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/simulation_estimate.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
using namespace datapump;
int main(){
 struct Case{const char* name;double rate,carrier,shift,target,tx,loss;const char* lf;const char* rf;unsigned factor;};
 const Case cases[]={
 {"primary-high",.01,1500,0,4.2185134083910505,3,170,"gpsdo-xo","gpsdo-xo",1},
 {"primary-mid",.01,.5,0,4.2185134083910505,3,170,"gpsdo-xo","gpsdo-xo",1},
 {"primary-low",.01,.005,0,4.2185134083910505,3,170,"gpsdo-xo","gpsdo-xo",1},
 {"strong",.1,.05,0,-38,36.020599913279625,200,"gpsdo-xo","gpsdo-xo",1},
 {"rf30",1,30001500,30000000,-24,36.020599913279625,220,"gpsdo-ocxo","gpsdo-ocxo",1},
 {"sub9",.01,8200,0,-49,0,200,"gpsdo-ocxo","gpsdo-ocxo",1},
 {"latest-wide",1200,1009000,1000000,70,36.020599913279625,120,"gpsdo-xo","gpsdo-ocxo",10},
 {"voice10",360,1500,0,70,3,100,"gpsdo-xo","gpsdo-xo",10},
 {"voice100",36,1500,0,70,3,100,"gpsdo-xo","gpsdo-xo",100},
 {"voice1000",3.6,1500,0,70,3,100,"gpsdo-xo","gpsdo-xo",1000}};
 const auto available=runtime::available_memory_bytes(),workspace=runtime::dsp_workspace_budget(50);
 std::cout<<std::setprecision(17)<<"case,available_memory_bytes,workspace_50_percent_bytes,inner_rate,dsss_factor,absolute_carrier,shift,logical_sample_rate,chip_samples,chip_seconds,symbol_samples,symbol_seconds,inner_chips,ideal_shaped_width,ideal_low,ideal_high,frequency_clock_pairs,frequency_bins,frequency_half_width,clock_half_width,model_epochs,model_timing_hypotheses,model_phase_groups,model_drift_sections,model_differential_windows,model_pulse,model_segments,model_workspace_supported\n";
 for(auto x:cases){
  transfer::Options o;o.modem=tuning::resolve(x.rate,x.target,tuning::PatternMode::auto_keystream,true,x.carrier-x.shift,x.factor).config;o.key.emplace(Bytes(32,0x63));o.search_seconds=6;o.dsp_workspace_bytes=workspace;
  modem::OscillatorSearchConfig p;p.lf=tuning::oscillator_model(tuning::parse_oscillator_preset(x.lf));p.rf=x.shift?tuning::oscillator_model(tuning::parse_oscillator_preset(x.rf)):modem::OscillatorModel{0,0};p.rf_shift_hz=x.shift;p.margin=3;p.reference=modem::OscillatorReference::independent_audio;o.modem.oscillator_search=p;
  const auto c=transfer::seeded_config(o,1800000000);const auto N=modem::symbol_sample_count(c),L=modem::pattern_chip_samples(c);const auto bank=modem::oscillator_pattern_search(c);
  modem::ChannelConfig ch;const auto e=modem::oscillator_effects(c);ch.clock_error_ppm=e.clock_error_ppm;ch.frequency_offset_hz=e.frequency_offset_hz;ch.phase_noise_degrees_per_sqrt_second=e.phase_noise_degrees_per_sqrt_second;ch.snr_db=x.tx-x.loss+164-10*std::log10(c.sample_rate/2.);
  transfer::Estimate d;d.wire_bits=1;d.total_seconds=(modem::training_sample_count(c)+2.L*modem::pattern_pulse_padding_samples(c)+modem::suppression_sample_count(c)+N)/c.sample_rate;
  const auto w=simulation::estimate(d,o,true,ch,{},1,false,100,4096,simulation::ReceiverWorkMode::hardware_fallback);
  const double width=modem::pattern_pulse_enabled(c)?1.25*c.sample_rate/L:x.rate*x.factor;
  std::cout<<x.name<<','<<available<<','<<workspace<<','<<x.rate<<','<<x.factor<<','<<x.carrier<<','<<x.shift<<','<<c.sample_rate<<','<<L<<','<<double(L)/c.sample_rate<<','<<N<<','<<double(N)/c.sample_rate<<','<<c.spreading_factor<<','<<width<<','<<c.carrier_hz-width/2<<','<<c.carrier_hz+width/2<<','<<bank.hypotheses.size()<<','<<bank.frequency.count<<','<<bank.frequency.half_width_hz<<','<<bank.clock_half_width_ppm<<','<<w.epoch_hypotheses<<','<<w.timing_hypotheses<<','<<w.timing_phase_groups<<','<<w.drift_sections<<','<<w.differential_windows<<','<<w.pulse_projection_modeled<<','<<w.pulse_segment_projection_modeled<<','<<w.receiver_workspace_supported<<'\n';
 }
}

#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/fhss_schedule.hpp"
#include "datapump/symbol_schedule.hpp"
#include <iostream>
#include <vector>
using namespace datapump;
void check(bool value,const char* text){if(!value)throw Error(text);}
int main(){try {
    modem::Config c;c.sample_rate=14400;c.carrier_hz=1500;c.bandwidth_hz=360;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=true;c.dsss=true;c.dsss_factor=10;
    c.stream_epoch=1800000000;c.spreading_seed[3]=51;c.dsss_seed[13]=171;c.data_key=Crypto(c.spreading_seed);
    auto hops=std::make_shared<modem::FhssPlan>();hops->channels=3;hops->dwell_seconds=.4;
    hops->shifts[0]=1000000;hops->shifts[1]=1010000;hops->shifts[2]=1033000;
    hops->clock_error_seconds=hops->peer_clock_error_seconds=.001;hops->audio_error_seconds=.002;
    c.fake_hopping=modem::resolve_fhss_power(*hops,c);
    check(c.fake_hopping->defer_symbols && modem::symbol_sample_count(c)==5120,"work fixture lost B calendar geometry");
    modem::PatternSearch s;s.start_offset_seconds=0;s.start_uncertainty_seconds=7;s.search_stream_phases=true;
    s.initial_stream_symbols=4;s.worker_threads=1;s.prefer_streamed_templates=true;s.drift_tolerant=false;
    s.candidate_limit=128;s.track_limit=8;s.bit_limit=256;s.chunk_bits=1;
    s.early_qualified_fft=false;s.component_qualified_search=false;
    s.admitted_clock_half_width_ppm=350;s.admitted_clock_cell_half_width_ppm=50;
    const auto step=.25*c.sample_rate/modem::symbol_sample_count(c);
    for(const auto f:{0.,-step,step})for(const auto ppm:{-300.,-200.,-100.,0.,100.,200.,300.})s.hypotheses.push_back({f,ppm});
    modem::PatternReceiver receiver(c,16*1024*1024,s);
    const auto born=receiver.acquisition_work_bound();
    check(born && !born->ready && born->initial_stream_symbols==4 && born->original_phase_groups==8 &&
          born->original_pair_descriptors==8*s.hypotheses.size() &&
          born->maximum_fft_pair_jobs==born->original_pair_descriptors && born->transform_length,
        "fresh native owner did not expose its original finite descriptor bound");
    const auto before=receiver.fft_work();const auto bytes=receiver.working_bytes();
    const auto again=receiver.acquisition_work_bound();
    check(again && again->original_pair_descriptors==born->original_pair_descriptors &&
          receiver.working_bytes()==bytes && receiver.fft_work().template_jobs==before.template_jobs,
        "work-bound query allocated or changed receiver work");
    while(!receiver.acquisition_state().ready){
        const auto state=receiver.acquisition_state();check(state.supported && state.input_room_samples,"work fixture lost native observation fence");
        std::vector<float> noise(state.input_room_samples);check(receiver.observe_acquisition(noise)==noise.size(),"native bound setup missed its original fence");
    }
    const auto ready=receiver.acquisition_work_bound();check(ready && ready->ready &&
        ready->original_pair_descriptors==born->original_pair_descriptors,"ready fence changed the original descriptor envelope");
    receiver.process_acquisition();const auto after=receiver.fft_work();
    check(after.template_jobs-before.template_jobs<=ready->original_pair_descriptors &&
          after.fft_jobs-before.fft_jobs<=ready->maximum_fft_pair_jobs,"actual cold batch exceeded its prepaid descriptor bound");
    std::cout<<"native work bound="<<ready->original_pair_descriptors<<" actual_jobs="
        <<after.template_jobs-before.template_jobs<<" actual_fft="<<after.fft_jobs-before.fft_jobs<<'\n';
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

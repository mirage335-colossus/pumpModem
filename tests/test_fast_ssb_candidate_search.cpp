#include "datapump/fast/modem.hpp"
#include "fast/ssb_candidate_search.hpp"
#include "fast/ssb_quiet.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <random>
#include <stdexcept>

namespace {
namespace fast = datapump::fast;
using namespace datapump::fast::detail;
using Complex = std::complex<double>;

void check(bool condition,const char* message) {
    if(!condition)throw std::runtime_error(message);
}

SsbAcquisitionRequest bounds(const fast::Profile& profile) {
    SsbAcquisitionRequest request{};
    request.max_excess_delay_ticks=.022*profile.sample_rate;
    return request;
}

void check_arrival(std::uint32_t rate,std::int64_t epoch,std::size_t chunk) {
    auto profile=fast::capacity_profile(fast::Channel::ssb);
    profile.sample_rate=rate;
    const auto geometry=make_ssb_frontend_geometry(rate,profile.carrier_hz);
    const DelayDopplerFit fit(profile,geometry);
    std::optional<SsbAcquisitionLock> lock;
    std::vector<std::uint64_t> starts;
    SsbCandidateSearch search(fit,geometry,bounds(profile),.10,
        [&](SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token stop) {
            starts.push_back(request.training_first_center_tick);
            lock=fit.fit(view,request,stop);
            return lock.has_value();
        });
    const auto workspace=search.workspace_bytes();
    SsbFrontend frontend(geometry,[&](const SsbFrontendOutput& output) {
        check(search.push(output)==output.samples.size(),"Continuous SSB search lost a native sample center");
    });

    std::mt19937 random(27);
    fast::Transmitter tx(profile,[&](std::span<std::uint8_t> bits) {
        for(auto& bit:bits)bit=static_cast<std::uint8_t>(random()&1U);
        return true;
    });
    const auto idle=static_cast<std::size_t>(std::max<std::int64_t>(0,epoch));
    const auto crop=static_cast<std::size_t>(std::max<std::int64_t>(0,-epoch));
    std::vector<float> waveform(idle+crop+rate*14/10);
    const auto payload=std::span(waveform).subspan(idle);
    check(tx.read(payload)==payload.size(),"Continuous SSB fixture transmitter ended early");
    const auto input=std::span(waveform).subspan(crop);
    for(std::size_t offset=0;offset<input.size();offset+=chunk) {
        const auto part=input.subspan(offset,std::min(chunk,input.size()-offset));
        check(frontend.push(part)==part.size(),"Continuous SSB frontend truncated an input chunk");
    }
    check(lock && search.progress().locked,"Continuous SSB listener did not verify the native marker");
    const auto recovered=static_cast<double>(lock->reference_input_tick)+
        lock->epoch_from_reference_ticks/(1.+lock->clock_ratio_error);
    const auto word_ticks=64.*rate/profile.symbol_rate;
    check(std::abs(recovered-static_cast<double>(epoch))<word_ticks/16.,
          "Continuous SSB listener locked to a displaced periodic epoch");
    check(std::ranges::is_sorted(starts) && std::adjacent_find(starts.begin(),starts.end())==starts.end(),
          "Continuous SSB listener did not offer oldest candidate starts in order");
    check(starts.size()<=12 && search.workspace_bytes()==workspace,
          "Continuous SSB listener exceeded bounded clean-arrival work");
    const auto retained=search.retained();
    check(retained.first_center_input_tick+(retained.samples.size()-1)*retained.stride==
          geometry.radius()+(frontend.output_samples()-1)*geometry.decimation,
          "Continuous SSB lookback lost the last observed center");
    std::cout<<"rate="<<rate<<" epoch_ticks="<<epoch<<" recovered_ticks="<<recovered
             <<" windows="<<search.progress().projected_windows<<" fit_requests="<<starts.size()<<'\n';
}

void check_absent_training() {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    const auto geometry=make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    const DelayDopplerFit fit(profile,geometry);
    SsbCandidateSearch search(fit,geometry,bounds(profile),.10,
        [](SsbAcquisitionView,const SsbAcquisitionRequest&,std::stop_token) -> bool {
            throw std::runtime_error("Noise or CW reached the heavy fitter");
        });
    SsbFrontend frontend(geometry,[&](const SsbFrontendOutput& output) {
        check(search.push(output)==output.samples.size(),"Noise/CW observation was truncated");
    });
    std::vector<float> pcm(3*profile.sample_rate);
    std::mt19937 random(471);
    std::normal_distribution<float> noise(0.F,.006F);
    for(std::size_t i=0;i<pcm.size();++i) {
        pcm[i]=noise(random);
        if(i>profile.sample_rate)
            pcm[i]+=.31F*static_cast<float>(std::cos(2*std::numbers::pi*1597.*i/profile.sample_rate));
    }
    const auto bytes=search.workspace_bytes();
    check(frontend.push(pcm)==pcm.size(),"Continuous SSB control source ended early");
    check(search.progress().projected_windows>10 && !search.progress().training_proposals &&
          !search.progress().fit_requests && !search.progress().locked && search.workspace_bytes()==bytes,
          "Continuous SSB unsuccessful search exceeded its bounded work");
}

void check_cancelled_request() {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    const auto geometry=make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    const DelayDopplerFit fit(profile,geometry);
    std::stop_source cancel;
    std::vector<std::uint64_t> starts;
    SsbCandidateSearch search(fit,geometry,bounds(profile),.10,
        [&](SsbAcquisitionView,const SsbAcquisitionRequest& request,std::stop_token) {
            starts.push_back(request.training_first_center_tick);
            if(starts.size()==1)cancel.request_stop();
            return false;
        });
    const auto reference=fit.proposal_word();
    std::vector<Complex> signal(profile.sample_rate*12/(10*geometry.decimation));
    for(std::size_t i=0;i<signal.size();++i)signal[i]=reference[i%reference.size()];
    const auto consumed=search.push({geometry.radius(),geometry.decimation,signal},cancel.get_token());
    check(consumed<signal.size() && starts.size()==1 && !search.progress().locked,
          "Cancelled fit did not stop continuous SSB processing");
    const auto center=geometry.radius()+consumed*geometry.decimation;
    const auto rest=std::span(signal).subspan(consumed);
    check(search.push({center,geometry.decimation,rest})==rest.size(),
          "Continuous SSB listener could not resume a retained input suffix");
    check(starts.size()>=2 && starts[1]==starts[0] && std::ranges::is_sorted(starts),
          "Cancellation discarded the oldest supported fit request");
    const auto retained=search.retained();
    try {
        search.push({center,geometry.decimation,rest.first(1)});
    }catch(const std::invalid_argument&) {
        check(search.retained().first_center_input_tick==retained.first_center_input_tick &&
              search.retained().samples.size()==retained.samples.size(),
              "Rejected discontinuity changed the continuous SSB history");
        return;
    }
    throw std::runtime_error("Continuous SSB listener accepted discontinuous input");
}

void check_wrapped_history_growth() {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    const auto geometry=make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    const DelayDopplerFit fit(profile,geometry);
    SsbCandidateSearch search(fit,geometry,bounds(profile),.10,
        [](SsbAcquisitionView,const SsbAcquisitionRequest&,std::stop_token) { return true; });
    const auto reference=fit.proposal_word();
    std::vector<Complex> signal(3*profile.sample_rate/geometry.decimation);
    for(std::size_t i=0;i<signal.size();++i)signal[i]=reference[i%reference.size()];
    const auto center=geometry.radius();
    check(search.push({center,geometry.decimation,signal})==signal.size() && search.progress().locked,
          "Wrapped history fixture did not reach its retained payload stage");
    const auto before=search.retained();
    check(before.samples.size()<signal.size(),"Wrapped history fixture did not retire any samples");
    const auto first=before.first_center_input_tick;
    const std::vector<Complex> saved(before.samples.begin(),before.samples.end());
    const auto old_workspace=search.workspace_bytes();
    search.reserve_history(saved.size()+1024);
    const auto expanded=search.retained();
    check(expanded.first_center_input_tick==first && std::ranges::equal(expanded.samples,saved),
          "Growing wrapped history exposed unobserved native centers");
    check(search.workspace_bytes()>=old_workspace+2048*sizeof(Complex),
          "Expanded history did not charge allocated sample storage");
    SsbQuietRun quiet(profile.sample_rate,geometry.decimation);
    check(!quiet.observe(expanded,{first-geometry.decimation,first},SsbPublicPresence::absent) &&
          quiet.elapsed_input_ticks()==0,"New storage was eligible for physical quiet");

    const std::array<Complex,17> next{Complex{1.,-1.}};
    check(search.push({center+signal.size()*geometry.decimation,geometry.decimation,next})==next.size(),
          "Expanded history failed to accept the next actual center");
    const auto resumed=search.retained();
    check(resumed.first_center_input_tick==first && resumed.samples.size()==saved.size()+next.size() &&
          std::ranges::equal(resumed.samples.last(next.size()),next),
          "Expanded history changed the order of retained and new observations");
}
}

int main() {
    try {
        check_wrapped_history_growth();
        check_arrival(44100,8962,1023);
        check_arrival(48000,-2411,73);
        check_absent_training();
        check_cancelled_request();
        std::cout<<"Fast SSB continuous search verified arbitrary and clipped native arrivals\n";
        return 0;
    }catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}

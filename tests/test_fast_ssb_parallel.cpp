#include "datapump/fast/modem.hpp"
#include "fast/ssb_acquisition_workers.hpp"
#include "fast/ssb_receiver.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string_view>

namespace {
namespace fast = datapump::fast;
namespace execution = datapump::execution;
using namespace datapump::fast::detail;
using Complex = std::complex<double>;
using namespace std::chrono_literals;

void check(bool condition,const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void check_ordered_private_results() {
    execution::Mutex mutex;
    execution::StopCondition changed;
    bool oldest_started=false, release_oldest=false, younger_failed=false;
    std::array<Complex,8> pcm;
    pcm.fill({1.,2.});
    SsbFitWorkers workers(3,[&](SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token stop) {
        check(view.first_center_input_tick==100 && view.stride==4 && view.samples.size()==8,
            "Concurrent SSB fitter changed the immutable observation geometry");
        if (request.training_first_center_tick==100) {
            std::unique_lock lock(mutex);
            oldest_started=true;
            changed.notify_all();
            if (!changed.wait(lock,stop,[&] { return release_oldest; })) return SsbFitResult{};
            check(view.samples.front()==Complex{1.,2.},"Concurrent SSB fitter read reused caller input");
            SsbFitResult result;
            result.lock.emplace();
            return result;
        }
        check(view.samples.front()==Complex{3.,4.},"Concurrent SSB fitter received the wrong snapshot");
        {
            std::lock_guard lock(mutex);
            younger_failed=true;
            changed.notify_all();
        }
        throw std::runtime_error("younger fit failed");
    },{});
    SsbAcquisitionRequest request{};
    request.training_first_center_tick=100;
    check(workers.submit({100,4,pcm},request,240),"Could not offer oldest SSB fit");
    pcm.fill({3.,4.});
    request.training_first_center_tick=104;
    check(workers.submit({100,4,pcm},request,244),"Could not offer younger SSB fit");
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock,5s,[&] { return oldest_started && younger_failed; }),
            "Independent SSB fits did not execute concurrently");
    }
    check(!workers.take(false),"Younger SSB failure escaped ahead of the oldest unresolved fit");
    {
        std::lock_guard lock(mutex);
        release_oldest=true;
        changed.notify_all();
    }
    const auto oldest=workers.take(true);
    check(oldest && oldest->result.lock && !oldest->failure && oldest->request.training_first_center_tick==100,
        "Oldest verified SSB fit did not commit before younger failure");
    check(workers.pending()==1,"Younger SSB result was lost before the oldest owner decided");
    workers.close();
    check(!workers.take(false) && !workers.pending(),"Accepted SSB fit left younger work publishable");
}

void check_cancellation_wakes_running_fit() {
    execution::Mutex mutex;
    execution::StopCondition changed;
    bool started=false, observed_stop=false;
    std::stop_source source;
    SsbFitWorkers workers(2,[&](SsbAcquisitionView,const SsbAcquisitionRequest&,std::stop_token stop) {
        std::unique_lock lock(mutex);
        started=true;
        changed.notify_all();
        changed.wait(lock,stop,[]{ return false; });
        observed_stop=stop.stop_requested();
        return SsbFitResult{};
    },source.get_token());
    const std::array<Complex,2> pcm{{1.,1.}};
    check(workers.submit({100,4,pcm},{},240),"Could not offer cancellation fixture");
    {
        std::unique_lock lock(mutex);
        check(changed.wait_for(lock,5s,[&]{ return started; }),"SSB cancellation fixture never started");
    }
    source.request_stop();
    check(!workers.take(true),"Cancelled SSB fit published an ordered result");
    workers.close();
    check(observed_stop && !workers.pending(),"SSB cancellation did not join and clear the active fit");
}

struct Offer {
    std::uint64_t training,first,past_last,hash;
    bool operator==(const Offer&) const=default;
};

std::uint64_t view_hash(std::span<const Complex> samples) {
    std::uint64_t hash=0;
    for (const auto value : samples) {
        hash=std::rotl(hash,5)^std::bit_cast<std::uint64_t>(value.real());
        hash=std::rotl(hash,5)^std::bit_cast<std::uint64_t>(value.imag());
    }
    return hash;
}

std::vector<Offer> collect_views(std::span<const float> pcm,std::size_t chunk,bool lookahead) {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    const auto geometry=make_ssb_frontend_geometry(profile.sample_rate,profile.carrier_hz);
    const DelayDopplerFit fitter(profile,geometry);
    SsbAcquisitionRequest bounds{};
    bounds.max_excess_delay_ticks=.022*profile.sample_rate;
    std::vector<Offer> offers;
    std::deque<std::uint64_t> outstanding;
    SsbCandidateSearch search(fitter,geometry,bounds,.10,
        [&](SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::stop_token) {
            offers.push_back({request.training_first_center_tick,view.first_center_input_tick,
                view.first_center_input_tick+view.stride*view.samples.size(),view_hash(view.samples)});
            if (lookahead) outstanding.push_back(view.first_center_input_tick);
            return false;
        });
    if (lookahead) {
        search.allow_pending_fits(4,[&](bool wait) {
            if (wait && !outstanding.empty()) outstanding.pop_front();
            return false;
        },[&]() -> std::optional<SsbCandidateSearch::FitFrontiers> {
            if (outstanding.empty()) return {};
            return SsbCandidateSearch::FitFrontiers{outstanding.front(),std::numeric_limits<std::uint64_t>::max()};
        });
    }
    SsbFrontend frontend(geometry,[&](const SsbFrontendOutput& output) {
        check(search.push(output)==output.samples.size(),"Concurrent SSB search lost a native center");
        if (!outstanding.empty()) check(search.retained().first_center_input_tick<=outstanding.front(),
            "Concurrent SSB retention overwrote an unresolved oldest view");
    });
    for (std::size_t first=0;first<pcm.size();first+=chunk) {
        const auto block=pcm.subspan(first,std::min(chunk,pcm.size()-first));
        check(frontend.push(block)==block.size(),"Concurrent SSB frontend lost a PCM sample");
    }
    while (!outstanding.empty()) search.drain_fits({});
    check(search.progress().fit_requests==offers.size() && !search.progress().locked,
        "Parallel SSB offer test did not preserve all eligible attempts");
    return offers;
}

void check_unchanged_serial_offer_views() {
    const auto profile=fast::capacity_profile(fast::Channel::ssb);
    std::mt19937 random(27);
    fast::Transmitter transmitter(profile,[&](std::span<std::uint8_t> bits) {
        for (auto& bit : bits) bit=static_cast<std::uint8_t>(random()&1U);
        return true;
    });
    std::vector<float> pcm(2*profile.sample_rate);
    check(transmitter.read(pcm)==pcm.size(),"SSB offer fixture transmitter ended early");
    const auto serial=collect_views(pcm,1023,false);
    const auto parallel=collect_views(pcm,73,true);
    check(serial.size()>8 && serial==parallel,
        "Concurrent lookahead changed a serial public candidate window or its observed PCM");
}
}

int main() {
    try {
#ifndef DATAPUMP_EXECUTION_FIBERS
        check_ordered_private_results();
        check_cancellation_wakes_running_fit();
#endif
        check_unchanged_serial_offer_views();
        std::cout << "Fast SSB concurrent acquisition preserved serial observations and ordered results\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

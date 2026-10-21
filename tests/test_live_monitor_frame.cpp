#include "../src/signal_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace allocation_probe {
thread_local bool enabled=false;
thread_local std::size_t allocations=0,deallocations=0;
struct Counts { std::size_t allocations,deallocations; };
template<class Work> Counts measure(Work&& work) {
    allocations=deallocations=0; enabled=true;
    try { work(); } catch(...) { enabled=false; throw; }
    enabled=false; return {allocations,deallocations};
}
}
void* operator new(std::size_t size) {
    if (allocation_probe::enabled) ++allocation_probe::allocations;
    if (auto* p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept {
    if (p && allocation_probe::enabled) ++allocation_probe::deallocations;
    std::free(p);
}
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p,std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p,std::size_t) noexcept { ::operator delete(p); }

using namespace datapump;
namespace {
void check(bool value,const char* message) { if (!value) throw Error(message); }

std::vector<float> samples(std::uint64_t start,std::size_t count,const modem::Config& config) {
    std::vector<float> result(count);
    for (std::size_t i=0;i<count;++i) {
        const auto phase=2*std::numbers::pi*config.carrier_hz*static_cast<double>(start+i)/config.sample_rate;
        result[i]=static_cast<float>(.61*std::cos(phase+.27)+.13*std::sin(phase*.37));
    }
    return result;
}

void matches_generic_environment_view() {
    for (const auto [rate,capacity]:std::array<std::pair<std::uint32_t,std::size_t>,3>{{
            {8000,2048},{48000,2048},{480001,8001}}}) {
        modem::Config config; config.sample_rate=rate; config.bandwidth_hz=1100; config.carrier_hz=973;
        live::detail::SignalWindow window(config);
        auto workspace=std::make_unique<live::detail::EnvironmentFrameWorkspace>();
        check(window.sample_storage_bytes()>=capacity*sizeof(float),
              "monitor storage accounting does not match its allocated sample ring");

        const auto compare=[&] {
            const auto expected=window.frame(config,live::detail::SignalView::environment);
            const auto actual=window.environment_frame(*workspace);
            check(std::equal(actual.waveform.begin(),actual.waveform.end(),expected.waveform.begin(),expected.waveform.end()),
                  "monitor waveform differs from the generic environment frame");
            check(std::equal(actual.spectrum.begin(),actual.spectrum.end(),expected.spectrum.begin(),expected.spectrum.end()),
                  "monitor spectrum differs from the generic environment frame");
            check(std::equal(actual.spectrum_complex.begin(),actual.spectrum_complex.end(),expected.spectrum_complex.begin(),expected.spectrum_complex.end()),
                  "monitor complex spectrum differs from the generic environment frame");
            check(actual.spectrum_start_sample==expected.spectrum_start_sample &&
                  actual.spectrum_sample_count==expected.spectrum_sample_count,
                  "monitor sample interval differs from the generic environment frame");
        };
        compare(); // Empty startup preserves the established -240 dB display.
        for (const auto count:{1U,2U,3U}) {
            window.reset(17);
            const auto tiny=samples(17,count,config);
            window.push(tiny);
            compare(); // Rectangular 1/2 samples and three-sample Hann differ.
        }
        window.reset();
        const auto initial=samples(0,capacity/2+17,config);
        window.push(initial);
        compare(); // Partial startup exercises zeroed unused FFT bins.
        auto full=samples(window.samples_seen(),capacity+93,config);
        window.push(full);
        compare(); // Oversized push wraps and retains only the ring tail.
        std::array<float,317> tail{};
        for (std::size_t i=0;i<tail.size();++i) tail[i]=static_cast<float>(std::sin(i*.071));
        window.push(tail);
        compare(); // Reuse after another wrap must not retain old FFT bin values.
        window.reset(900137);
        compare();
        auto restarted=samples(window.samples_seen(),capacity+29,config);
        window.push(restarted);
        compare();

        const auto calls=allocation_probe::measure([&] {
            for (int i=0;i<3;++i) (void)window.environment_frame(*workspace);
        });
        check(calls.allocations==0 && calls.deallocations==0,
              "prepared monitor frame allocated or freed memory during rendering");
    }
}

void matches_generic_constellation_view() {
    for (const auto [rate,capacity]:std::array<std::pair<std::uint32_t,std::size_t>,3>{{
            {8000,2048},{48000,2048},{480001,8001}}}) {
        modem::Config config; config.sample_rate=rate; config.bandwidth_hz=1100; config.carrier_hz=973;
        live::detail::SignalWindow window(config);
        check(live::detail::SignalWindow::sample_capacity(config)==capacity,
              "constellation fixture does not match its configured ring capacity");
        const auto compare=[&](std::uint64_t origin,std::size_t count) {
            const auto input=samples(origin,count,config);
            window.reset(origin);
            window.push(input);
            const auto retained=std::span(input).last(std::min(input.size(),capacity));
            const auto retained_origin=origin+(input.size()-retained.size());
            const auto expected=live::detail::signal_plots(
                retained,config,retained_origin,live::detail::SignalView::constellation);
            const auto actual=window.frame(config,live::detail::SignalView::constellation);
            check(actual.constellation==expected.constellation,
                  "logical ring constellation differs from generic span rendering");
        };
        for (const auto count:{0U,1U,2U,3U}) compare(37,count);
        compare(37,capacity/2+17); // Partial startup with a nonzero absolute origin.
        compare(900137,capacity); // Full ring after reset.
        compare(1700011,capacity+93); // Oversized push wraps and retains the logical tail.
        for (const auto count:{1U,317U,static_cast<unsigned>(capacity/2+7)}) {
            const auto tail=samples(window.samples_seen(),count,config);
            window.push(tail); // Split the logical window across both ends of the physical ring.
            const auto first=window.samples_seen()-capacity;
            const auto expected_samples=samples(first,capacity,config);
            const auto expected=live::detail::signal_plots(
                expected_samples,config,first,live::detail::SignalView::constellation);
            const auto actual=window.frame(config,live::detail::SignalView::constellation);
            check(actual.constellation==expected.constellation,
                  "wrapped logical ring constellation differs from contiguous samples");
        }

        if (rate==480001) {
            modem::Config smaller=config; smaller.sample_rate=8000;
            window.reset(9);
            window.push(samples(9,3000,config));
            bool threw=false;
            try { (void)window.frame(smaller,live::detail::SignalView::constellation); }
            catch (const Error&) { threw=true; }
            check(threw,"constellation frame bypassed generic span capacity validation for a changed config");
        }

        window.reset(900137);
        window.push(samples(900137,capacity,config));
        live::detail::SignalPlots rendered;
        const auto calls=allocation_probe::measure([&] {
            rendered=window.frame(config,live::detail::SignalView::constellation);
        });
        check(!rendered.constellation.empty() && calls.allocations<=1 && calls.deallocations==0,
              "prepared constellation render did not limit allocation to its returned output vector");
    }
}
}

int main() {
    try {
        matches_generic_environment_view();
        matches_generic_constellation_view();
        std::cout<<"live monitor frame workspace checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}

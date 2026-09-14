#include "datapump/streaming_modem.hpp"
#include "datapump/transfer.hpp"
#include "datapump/tuning.hpp"
#include "datapump/pattern_pulse.hpp"
#include <charconv>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <thread>

namespace {
template<class Number> Number argument(std::string_view text,const char* name) {
    if(text.starts_with('+')) {
        text.remove_prefix(1);
        if(text.starts_with('-'))throw datapump::Error(std::string("invalid ")+name);
    }
    if(text.empty())throw datapump::Error(std::string("invalid ")+name);
    Number value{};
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw datapump::Error(std::string("invalid ")+name);
    return value;
}
}

// Generated PCM only: this benchmark never opens an audio device. Run without
// other CPU-heavy work; its result is a measurement, not a portable test limit.
int main(int argc, char** argv) {
    using namespace datapump;
    try {
        if(argc>4)throw Error("usage: benchmark_receiver [bandwidth_hz [target_cn0_db_hz [epoch_radius]]]");
        const auto bandwidth=argc>1?argument<double>(argv[1],"bandwidth_hz"):1200.;
        const auto target=argc>2?argument<double>(argv[2],"target_cn0_db_hz"):40.;
        const auto radius=argc>3?argument<unsigned>(argv[3],"epoch_radius"):6U;
        if(!std::isfinite(target) || target < -200 || target > 200)
            throw Error("target_cn0_db_hz must be finite and within -200..200");
        if(radius>32)throw Error("epoch_radius must be within 0..32");
        transfer::Options options;
        options.modem=tuning::resolve(bandwidth,target,tuning::PatternMode::auto_pattern,true).config;
        options.search_seconds=radius;
        options.key.emplace(Bytes(32, 0x37));
        options.timestamp = 1800000000;
        std::vector<std::unique_ptr<modem::StreamingReceiver>> bank;
        for (int offset = -static_cast<int>(radius); offset <= static_cast<int>(radius); ++offset) {
            const auto epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(options.timestamp) + offset);
            modem::PatternSearch search;
            search.start_offset_seconds=static_cast<double>(offset)+
                (static_cast<double>(modem::training_sample_count(options.modem))+
                 static_cast<double>(modem::pattern_pulse_padding_samples(options.modem)))/options.modem.sample_rate;
            search.start_uncertainty_seconds=static_cast<double>(radius)+1;
            bank.push_back(std::make_unique<modem::StreamingReceiver>(
                transfer::seeded_config(options,epoch),8*1024*1024,search));
        }
        std::vector<float> block(2048);
        std::mt19937_64 random(1);
        std::normal_distribution<float> normal(0, .05f);
        const auto feed = [&](std::stop_token stop = {}) {
            for (auto& sample : block) sample = normal(random);
            for (auto& receiver : bank) receiver->push(block, stop);
        };
        for (std::size_t warm = 0; warm < options.modem.sample_rate*5; warm += block.size()) feed();
        std::stop_source stop;
        std::jthread timer([&] {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            stop.request_stop();
        });
        std::uint64_t samples = 0;
        const auto start = std::chrono::steady_clock::now();
        try { while (true) { feed(stop.get_token()); samples += block.size(); } }
        catch (const Error&) { if (!stop.stop_requested()) throw; }
        const auto wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const double media = static_cast<double>(samples) / options.modem.sample_rate;
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10)
                  << options.modem.bandwidth_hz << " Hz bandwidth, " << target << " dB-Hz C/N0 target, "
                  << options.modem.spreading_factor << " chips, " << options.modem.constellation_bits << " bits/symbol, "
                  << bank.size() << " keyed epochs (radius " << radius << "), " << options.modem.sample_rate << " Hz generated PCM: "
                  << std::setprecision(6) << media << " media seconds / "
                  << wall << " wall seconds = " << media / wall << "x real time\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

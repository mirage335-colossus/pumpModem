#include "datapump/streaming_modem.hpp"
#include "datapump/transfer.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Generated PCM only: this benchmark never opens an audio device. Run without
// other CPU-heavy work; its result is a measurement, not a portable test limit.
int main() {
    using namespace datapump;
    try {
        transfer::Options options;
        options.key.emplace(Bytes(32, 0x37));
        options.timestamp = 1800000000;
        std::vector<std::unique_ptr<modem::StreamingReceiver>> bank;
        for (int offset = -6; offset <= 6; ++offset) {
            const auto epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(options.timestamp) + offset);
            auto expected = options.key->xor_data(modem::preamble(options.modem), epoch);
            const auto mask = options.key->xor_data(Bytes(packet_prefix_size), epoch, expected.size());
            bank.push_back(std::make_unique<modem::StreamingReceiver>(options.modem, std::move(expected),
                8 * 1024 * 1024, [mask](const Bytes& prefix) {
                    try {
                        auto plain = prefix;
                        for (std::size_t i = 0; i < plain.size(); ++i) plain[i] ^= mask.at(i);
                        return packet_bootstrap_possible(plain) && packet_frame_size(plain).has_value();
                    } catch (const Error&) { return false; }
                }));
        }
        std::vector<float> block(2048);
        std::mt19937_64 random(1);
        std::normal_distribution<float> normal(0, .05f);
        const auto feed = [&](std::stop_token stop = {}) {
            for (auto& sample : block) sample = normal(random);
            for (auto& receiver : bank) receiver->push(block, stop);
        };
        for (std::size_t warm = 0; warm < 240000; warm += block.size()) feed();
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
        std::cout << "13 keyed epochs, 48 kHz generated PCM: " << media << " media seconds / "
                  << wall << " wall seconds = " << media / wall << "x real time\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <utility>
namespace alsa_test {
struct Hint {std::string name,io;};
struct State {
    std::vector<Hint> hints;
    std::vector<std::string> available,wrong_format,attempts;
    std::vector<std::pair<std::string,int>> open_errors;
    std::vector<std::int16_t> played;
    std::vector<unsigned> supported_rates;
    // Existing scalar PCM fixtures model a mono-only sound card.
    std::vector<unsigned> supported_channels{1},configured_channels;
    std::vector<std::pair<std::string,unsigned>> configured;
    std::vector<std::size_t> write_frames;
    std::function<std::int16_t(std::size_t,unsigned)> sample;
    std::string selected;
    unsigned opens=0,closes=0,live=0,rate=0,channels=0,configured_streams=0;
    int direction=0;
    std::size_t captured=0,write_limit=137,read_limit=4096;
};
extern State state;
void reset();
}

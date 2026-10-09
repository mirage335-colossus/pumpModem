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
    std::function<void()> after_open,after_configure,after_hint;
    std::string selected;
    unsigned opens=0,closes=0,live=0,rate=0,channels=0,configured_streams=0;
    unsigned hint_calls=0,hints_freed=0;
    int direction=0,hint_error=0;
    std::size_t captured=0,write_limit=137,read_limit=4096;
    std::vector<long> read_results;
    std::vector<long> write_results;
    std::vector<int> wait_results,drain_results,recovered_errors,delay_results;
    std::size_t delay_result=0,delay_calls=0;
    std::function<void()> before_delay;
    // Existing unpaced fixtures consume writes immediately (RUNNING). Model
    // PREPARED explicitly for native plugins awaiting their first latency.
    int pcm_state=3;
    std::function<int()> observe_state;
    std::size_t read_result=0,write_result=0,wait_result=0,drain_result=0,drain_calls=0;
    std::function<void()> after_drain;
    // Script a bounded device queue independently from producer callbacks.
    // Hooks run only for the corresponding native adapter operation.
    std::function<void()> before_write;
    std::function<void(std::size_t)> after_write;
    std::function<void(int)> before_wait;
    std::function<long()> delay_observation;
    std::function<void()> after_drop,after_prepare;
    unsigned drops=0,prepares=0;
    bool stopped=false;
    unsigned long buffer_frames=4800,period_frames=1200;
    long delay_frames=0;
    int recover_result=-1;
    std::function<void()> after_recover;
};
extern State state;
void reset();
}

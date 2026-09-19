#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace datapump::legacy {
enum class Mode { olivia4_2000, bpsk31, bpsk125 };
struct Config { Mode mode=Mode::bpsk31; double carrier_hz=1500; };
inline constexpr std::uint32_t sample_rate=8000;
inline constexpr std::size_t text_byte_limit=32768;
void validate(const Config&);
std::string_view mode_name(Mode);
// Character callback fires only when that character's waveform has been emitted.
using TextCallback=std::function<void(std::string_view)>;
class Transmitter {
public:
    Transmitter(Config,std::string,TextCallback sent={});
    ~Transmitter();
    Transmitter(const Transmitter&)=delete;
    Transmitter& operator=(const Transmitter&)=delete;
    std::size_t read(std::span<float>);
private: struct Impl;std::unique_ptr<Impl> impl_;
};
class Receiver {
public:
    Receiver(Config,TextCallback);
    ~Receiver();
    Receiver(const Receiver&)=delete;
    Receiver& operator=(const Receiver&)=delete;
    void push(std::span<const float>);
    void reset();
private: struct Impl;std::unique_ptr<Impl> impl_;
};
namespace detail {
class WaveTransmitter {public:virtual ~WaveTransmitter()=default;virtual std::size_t read(std::span<float>)=0;};
class WaveReceiver {public:virtual ~WaveReceiver()=default;virtual void push(std::span<const float>)=0;};
std::unique_ptr<WaveTransmitter> psk_transmitter(Config,std::string,TextCallback);
std::unique_ptr<WaveReceiver> psk_receiver(Config,TextCallback);
std::unique_ptr<WaveTransmitter> olivia_transmitter(Config,std::string,TextCallback);
std::unique_ptr<WaveReceiver> olivia_receiver(Config,TextCallback);
}
}

#pragma once
#include "datapump/fast/modem.hpp"

namespace datapump::fast::acoustic_ofdm {
bool enabled(const Profile&);
std::uint64_t transmission_samples(const Profile&,std::size_t intervals);
std::size_t interval_symbols(const Profile&,std::size_t interval=0);
std::size_t preamble_symbols(const Profile&);
std::size_t pulse_tail_symbols(const Profile&);
double occupied_lower(const Profile&);
double occupied_upper(const Profile&);
double gross_bitrate(const Profile&);
struct Observation {
    std::uint64_t block=0,bit_offset=0;
    std::size_t bin=0;
    std::complex<double> value{};
    double variance=0,channel_power=0,clock_ppm=0,timing_correction=0;
    bool admitted=false;
};
using DiagnosticObserver=std::function<void(const Observation&)>;

class Transmitter {
public:
    Transmitter(Profile,IntervalReader,SymbolObserver={});
    ~Transmitter();
    std::size_t read(std::span<float>);
    bool finished() const;
    std::uint64_t samples_generated() const;
    std::size_t workspace_bytes() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
class Receiver {
public:
    Receiver(Profile,IntervalSink,SymbolObserver={},SymbolObserver={},DiagnosticObserver={});
    ~Receiver();
    void push(std::span<const float>);
    void finish();
    const ModemProgress& progress() const;
    std::size_t workspace_bytes() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}

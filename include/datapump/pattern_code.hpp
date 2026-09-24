#pragma once

#include "datapump/modem.hpp"
#include <functional>
#include <memory>

namespace datapump::modem {

// The pattern waveform carries one meaningful bit per complete pattern. A
// partial final chip consumes a whole stream position before the next symbol.
std::uint64_t pattern_chip_samples(const Config& config);
std::uint64_t pattern_chips_per_symbol(const Config& config);

// Seekable, bounded pattern templates. Public patterns restart each symbol;
// secret Scrambler and DSSS bytes use the full absolute chip address. Each
// chip consumes eight bytes to map circular noise amplitude and phase.
// Epoch/address are local clock
// hypotheses and are never transmitted as metadata.
class PatternCode {
public:
    explicit PatternCode(Config config, std::uint64_t stream_epoch = 0);
    ~PatternCode();
    PatternCode(PatternCode&&) noexcept;
    PatternCode& operator=(PatternCode&&) noexcept;
    // Public and private patterns return bounded circular I/Q noise.
    // Tone uses two frequencies at
    // nominal carrier +/- chip_rate/4; tone bit labels therefore require a
    // known nominal carrier and a frequency search narrower than chip_rate/4.
    // fraction is the position within a chip in [0,1).
    std::complex<double> value(std::uint64_t absolute_chip, unsigned bit,
                               double fraction = 0);
    std::uint64_t chip_samples() const;
    std::uint64_t chips_per_symbol() const;
    std::uint64_t symbol_samples() const;
    std::size_t working_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Binary pattern PCM: exactly bits.size() payload symbols,
// optionally preceded by rounded hardware-settling audio. The lead-in carries
// no payload or acquisition marker. Input bytes are individual 0/1 bits;
// start_chip addresses the first payload stream fragment. Disable the lead-in
// explicitly when generating a bare capture or testing preamble loss.
class PatternTransmitter {
public:
    static constexpr std::size_t analytic_preview_limit = 2112;
    // Actual baseband value at the first emitted sample of each payload chip.
    // Settling audio and preview reconstruction never notify the observer.
    using ChipObserver = std::function<void(std::complex<double>)>;
    PatternTransmitter(Bytes bits, Config config, std::uint64_t stream_epoch = 0,
                       std::uint64_t start_chip = 0, bool hardware_preamble = true);
    ~PatternTransmitter();
    PatternTransmitter(PatternTransmitter&&) noexcept;
    PatternTransmitter& operator=(PatternTransmitter&&) noexcept;
    std::size_t read(std::span<float> output, std::stop_token stop = {},
                     const ChipObserver& observer = {});
    std::size_t read_analytic(std::span<std::complex<double>> output,
                              std::stop_token stop = {}, const ChipObserver& observer = {});
    // Bounded reconstruction from stream coordinates; no waveform history is
    // retained and preview does not advance the transmitter.
    void preview_last_analytic(std::span<std::complex<double>> output) const;
    bool finished() const;
    std::uint64_t total_samples() const;
    std::uint64_t samples_emitted() const;
    double bit_rate() const;
    std::size_t working_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace datapump::modem

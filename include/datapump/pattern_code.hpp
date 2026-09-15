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
// secret Scrambler and DSSS bytes use the whole second at each symbol start
// and distinct positions for symbols starting in that second. That epoch stays
// fixed throughout the symbol. Each chip consumes eight bytes to map circular
// noise amplitude and phase. Epoch/address are local clock hypotheses and are
// never transmitted as metadata. absolute_chip indexes the symbol schedule
// anchored at stream_epoch and config.stream_phase_samples.
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
    // Linear contribution from one symbol, including its filter tails. The
    // coordinate is a sample offset from that symbol's unpadded start. Other
    // symbols contribute zero; receivers use this without any guessed bits.
    std::complex<double> shaped_value(std::uint64_t first_chip, unsigned bit,
                                      double within_symbol);
    // Reuse one bounded template for a different subsecond start hypothesis.
    // This changes only stream addressing and invalidates mapped chip caches.
    void set_stream_phase_samples(std::uint64_t phase_samples);
    std::uint64_t chip_samples() const;
    std::uint64_t chips_per_symbol() const;
    std::uint64_t symbol_samples() const;
    std::size_t working_bytes() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Binary pattern PCM: exactly bits.size() payload symbols,
// optionally preceded by rounded hardware-settling audio and followed by
// exactly two seconds of independent suppression noise. Shaped patterns add
// eight chip times at either payload edge to emit the full finite filter tails;
// the suppression noise starts after those tails. Neither noise section carries
// payload or an acquisition marker. Input bytes are individual 0/1 bits;
// start_chip addresses the first complete payload symbol and must be a multiple
// of pattern_chips_per_symbol(config). Use PatternCode for arbitrary chip
// fragment access. Disable surrounding_noise explicitly when generating a
// bare capture without either noise section.
class PatternTransmitter {
public:
    static constexpr std::size_t analytic_preview_limit = 2112;
    // Input chip constellation before pulse shaping, reported at each payload
    // chip's position in the padded output waveform.
    // Surrounding noise and preview reconstruction never notify the observer.
    using ChipObserver = std::function<void(std::complex<double>)>;
    PatternTransmitter(Bytes bits, Config config, std::uint64_t stream_epoch = 0,
                       std::uint64_t start_chip = 0, bool surrounding_noise = true);
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

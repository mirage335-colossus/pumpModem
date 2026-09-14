#include "datapump/pattern_code.hpp"
#include "datapump/crypto.hpp"

#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>

namespace datapump::modem {
namespace {
constexpr double tau = 2 * std::numbers::pi;
constexpr std::array<std::uint8_t, 32> public_seed{
    'D','a','t','a','P','u','m','p','/','p','a','t','t','e','r','n',
    '/','b','i','n','a','r','y','/','v','1',0,0,0,0,0,0};
// Balanced, neither a global sign reversal nor an alternating carrier shift.
// Every four consecutive positions include a nonconstant, nonalternating row.
constexpr std::array<int, 8> bit_mask{1,-1,-1,1,1,1,-1,-1};

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("pattern operation cancelled");
}
struct StreamCache {
    static constexpr std::size_t capacity = 512;
    Crypto key;
    StreamPurpose purpose;
    std::uint64_t epoch = 0, begin = 0;
    bool valid = false;
    std::array<std::uint8_t, capacity> bytes{};
    StreamCache(std::span<const std::uint8_t> seed, StreamPurpose use,
                std::uint64_t time): key(seed), purpose(use), epoch(time) {}
    ~StreamCache() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
    int sign(std::uint64_t chip) {
        const auto offset = chip / 8;
        const auto aligned = offset - offset % capacity;
        if (!valid || aligned != begin) {
            auto generated = key.stream(purpose, epoch, aligned, capacity);
            std::copy(generated.begin(), generated.end(), bytes.begin());
            OPENSSL_cleanse(generated.data(), generated.size());
            begin = aligned; valid = true;
        }
        return ((bytes[static_cast<std::size_t>(offset - begin)] >> (chip % 8)) & 1U) ? -1 : 1;
    }
};
}

std::uint64_t pattern_chip_samples(const Config& config) {
    validate(config);
    return static_cast<std::uint64_t>(std::ceil(2. * config.sample_rate / config.bandwidth_hz));
}
std::uint64_t pattern_chips_per_symbol(const Config& config) {
    const auto chip = pattern_chip_samples(config);
    const auto symbol = symbol_sample_count(config);
    return symbol / chip + (symbol % chip != 0);
}

struct PatternCode::Impl {
    Config config;
    std::uint64_t chip = 0, symbol = 0, chips = 0;
    StreamCache pattern, dsss;
    Impl(Config value, std::uint64_t epoch): config(value),
        pattern(config.scramble ? std::span<const std::uint8_t>(config.spreading_seed) :
                                 std::span<const std::uint8_t>(public_seed),
                StreamPurpose::Scrambler, config.scramble ? epoch : 0),
        dsss(config.dsss_seed, StreamPurpose::Dsss, epoch) {
        chip = pattern_chip_samples(config);
        symbol = symbol_sample_count(config);
        chips = symbol / chip + (symbol % chip != 0);
        require(sizeof(Impl) + sizeof(PatternCode) <= config.memory_limit,
                "pattern code exceeds memory limit");
    }
    int sign(std::uint64_t absolute_chip, unsigned bit) {
        require(bit <= 1, "pattern symbol must be a zero or one bit");
        require(config.spreading_mode == SpreadingMode::pattern,
                "tone templates require complex pattern values");
        const auto local = absolute_chip % chips;
        int result = pattern.sign(config.scramble ? absolute_chip : local);
        if (bit) result *= bit_mask[static_cast<std::size_t>(local % bit_mask.size())];
        if (config.dsss) result *= dsss.sign(absolute_chip);
        return result;
    }
    std::complex<double> value(std::uint64_t absolute_chip, unsigned bit, double fraction) {
        require(bit <= 1, "pattern symbol must be a zero or one bit");
        require(std::isfinite(fraction) && fraction >= 0 && fraction < 1,
                "pattern chip fraction must be within [0,1)");
        if (config.spreading_mode == SpreadingMode::pattern)
            return {static_cast<double>(sign(absolute_chip, bit)), 0};
        const auto angle = (bit ? 1. : -1.) * std::numbers::pi / 2 *
                           (static_cast<double>(absolute_chip % 4) + fraction);
        auto result = std::polar(1., angle);
        if (config.dsss) result *= static_cast<double>(dsss.sign(absolute_chip));
        return result;
    }
};

PatternCode::PatternCode(Config config, std::uint64_t epoch): impl_(std::make_unique<Impl>(config, epoch)) {}
PatternCode::~PatternCode() = default;
PatternCode::PatternCode(PatternCode&&) noexcept = default;
PatternCode& PatternCode::operator=(PatternCode&&) noexcept = default;
int PatternCode::sign(std::uint64_t chip, unsigned bit) { return impl_->sign(chip, bit); }
void PatternCode::fill(std::uint64_t chip, unsigned bit, std::span<int> output) {
    require(bit <= 1, "pattern symbol must be a zero or one bit");
    require(output.empty() || output.size() - 1 <= std::numeric_limits<std::uint64_t>::max() - chip,
            "pattern chip address would overflow");
    for (std::size_t i = 0; i < output.size(); ++i) output[i] = impl_->sign(chip + i, bit);
}
std::complex<double> PatternCode::value(std::uint64_t chip, unsigned bit, double fraction) {
    return impl_->value(chip, bit, fraction);
}
std::uint64_t PatternCode::chip_samples() const { return impl_->chip; }
std::uint64_t PatternCode::chips_per_symbol() const { return impl_->chips; }
std::uint64_t PatternCode::symbol_samples() const { return impl_->symbol; }
std::size_t PatternCode::working_bytes() const { return sizeof(PatternCode) + sizeof(Impl); }

struct PatternTransmitter::Impl {
    Bytes bits;
    Config config;
    PatternCode code;
    std::unique_ptr<StreamCache> settling;
    std::uint64_t start = 0, position = 0, total = 0, training = 0;
    Impl(Bytes input, Config value, std::uint64_t epoch, std::uint64_t start_chip, bool hardware_preamble):
        bits(std::move(input)), config(value), code(config, epoch), start(start_chip) {
        require(!bits.empty(), "pattern transmission requires at least one bit");
        require(std::all_of(bits.begin(), bits.end(), [](auto bit) { return bit <= 1; }),
                "pattern input elements must be zero or one");
        require(bits.size() <= std::numeric_limits<std::uint64_t>::max() / code.symbol_samples(),
                "pattern transmission duration would overflow");
        total = static_cast<std::uint64_t>(bits.size()) * code.symbol_samples();
        training=hardware_preamble?training_sample_count(config):0;
        require(training<=std::numeric_limits<std::uint64_t>::max()-total,"pattern transmission duration would overflow");
        total+=training;
        require(bits.size() <= std::numeric_limits<std::uint64_t>::max() / code.chips_per_symbol(),
                "pattern transmission chip count would overflow");
        const auto chip_count = static_cast<std::uint64_t>(bits.size()) * code.chips_per_symbol();
        require(chip_count - 1 <= std::numeric_limits<std::uint64_t>::max() - start,
                "pattern transmission chip address would overflow");
        const auto fixed = sizeof(Impl) + sizeof(PatternTransmitter) + code.working_bytes()+(training?sizeof(StreamCache):0);
        require(fixed <= config.memory_limit && bits.capacity() <= config.memory_limit - fixed,
                "pattern transmitter exceeds memory limit");
        if(training) {
            // A separate PRF domain produces noise-like settling chips. It
            // consumes no payload, Scrambler or DSSS stream positions and is
            // reproducible for previews without retaining its waveform.
            const auto& source=config.scramble?config.spreading_seed:config.dsss?config.dsss_seed:public_seed;
            const Crypto domain(source);
            constexpr std::string_view label="DataPump/hardware-settling/v1";
            auto seed=domain.mac(std::span(reinterpret_cast<const std::uint8_t*>(label.data()),label.size()));
            settling=std::make_unique<StreamCache>(seed,StreamPurpose::Scrambler,epoch);
            OPENSSL_cleanse(seed.data(),seed.size());
        }
    }
    template<class Output, class Convert>
    std::size_t render(std::uint64_t& cursor, std::span<Output> output, Convert convert, std::stop_token stop) {
        cancelled(stop);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), total - cursor));
        const auto angle = std::remainder(static_cast<long double>(cursor) * tau * config.carrier_hz / config.sample_rate,
                                         static_cast<long double>(tau));
        auto oscillator = std::polar(1., static_cast<double>(angle));
        const auto step = std::polar(1., tau * config.carrier_hz / config.sample_rate);
        const auto amplitude = std::sqrt(2 * nominal_signal_power);
        for (std::size_t i = 0; i < count;) {
            cancelled(stop);
            if(cursor<training) {
                const auto pattern=static_cast<double>(settling->sign(cursor/code.chip_samples()));
                const auto run=static_cast<std::size_t>(std::min<std::uint64_t>({count-i,training-cursor,
                    code.chip_samples()-cursor%code.chip_samples()}));
                for(std::size_t j=0;j<run;++j,++i,++cursor) {
                    if((i&4095U)==0)cancelled(stop);
                    output[i]=convert(amplitude*oscillator*pattern);oscillator*=step;
                }
                continue;
            }
            const auto payload=cursor-training;
            const auto symbol = payload / code.symbol_samples();
            const auto within = payload % code.symbol_samples();
            const auto chip = start + symbol * code.chips_per_symbol() + within / code.chip_samples();
            const auto fraction = static_cast<double>(within % code.chip_samples()) / static_cast<double>(code.chip_samples());
            const auto bit = bits[static_cast<std::size_t>(symbol)];
            auto pattern = code.value(chip, bit, fraction);
            const auto pattern_step = config.spreading_mode == SpreadingMode::tone ?
                std::polar(1., (bit ? 1. : -1.) * std::numbers::pi / (2 * static_cast<double>(code.chip_samples()))) :
                std::complex<double>{1, 0};
            const auto run = static_cast<std::size_t>(std::min<std::uint64_t>({count - i,
                code.symbol_samples() - within, code.chip_samples() - within % code.chip_samples()}));
            for (std::size_t j = 0; j < run; ++j, ++i, ++cursor) {
                if ((i & 4095U) == 0) cancelled(stop);
                output[i] = convert(amplitude * oscillator * pattern);
                oscillator *= step; pattern *= pattern_step;
            }
        }
        return count;
    }
};

PatternTransmitter::PatternTransmitter(Bytes bits, Config config, std::uint64_t epoch, std::uint64_t start_chip, bool hardware_preamble):
    impl_(std::make_unique<Impl>(std::move(bits), config, epoch, start_chip, hardware_preamble)) {}
PatternTransmitter::~PatternTransmitter() = default;
PatternTransmitter::PatternTransmitter(PatternTransmitter&&) noexcept = default;
PatternTransmitter& PatternTransmitter::operator=(PatternTransmitter&&) noexcept = default;
std::size_t PatternTransmitter::read(std::span<float> output, std::stop_token stop) {
    return impl_->render(impl_->position, output, [](auto value) { return static_cast<float>(value.real()); }, stop);
}
std::size_t PatternTransmitter::read_analytic(std::span<std::complex<double>> output, std::stop_token stop) {
    return impl_->render(impl_->position, output, [](auto value) { return value; }, stop);
}
void PatternTransmitter::preview_last_analytic(std::span<std::complex<double>> output) const {
    require(output.size() <= analytic_preview_limit, "pattern preview exceeds its bounded sample limit");
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), impl_->position));
    const auto leading = output.size() - count;
    std::fill_n(output.begin(), leading, std::complex<double>{});
    auto cursor = impl_->position - count;
    impl_->render(cursor, output.subspan(leading), [](auto value) { return value; }, {});
}
bool PatternTransmitter::finished() const { return impl_->position == impl_->total; }
std::uint64_t PatternTransmitter::total_samples() const { return impl_->total; }
std::uint64_t PatternTransmitter::samples_emitted() const { return impl_->position; }
double PatternTransmitter::bit_rate() const { return static_cast<double>(impl_->config.sample_rate) / static_cast<double>(impl_->code.symbol_samples()); }
std::size_t PatternTransmitter::working_bytes() const {
    return sizeof(PatternTransmitter) + sizeof(Impl) + impl_->code.working_bytes() + impl_->bits.capacity()+
        (impl_->settling?sizeof(StreamCache):0);
}

} // namespace datapump::modem

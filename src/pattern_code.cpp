#include "datapump/pattern_code.hpp"
#include "datapump/pattern_pulse.hpp"
#include "datapump/symbol_schedule.hpp"
#include "datapump/crypto.hpp"

#include <openssl/crypto.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

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
    StreamDomain domain;
    std::uint64_t epoch = 0, begin = 0;
    bool valid = false;
    std::array<std::uint8_t, capacity> bytes{};
    StreamCache(const Crypto& source, StreamPurpose use, std::uint64_t time,
                StreamDomain counter_domain):key(source),purpose(use),domain(counter_domain),epoch(time) {}
    StreamCache(std::span<const std::uint8_t> seed, StreamPurpose use,
                std::uint64_t time, StreamDomain counter_domain=StreamDomain::Payload):
        StreamCache(Crypto(seed),use,time,counter_domain) {}
    ~StreamCache() { OPENSSL_cleanse(bytes.data(), bytes.size()); }
    void select_epoch(std::uint64_t time) {
        if (epoch != time) { epoch = time; valid = false; }
    }
    std::uint8_t byte(std::uint64_t offset) {
        const auto aligned = offset - offset % capacity;
        if (!valid || aligned != begin) {
            auto generated = key.stream(purpose, epoch, aligned, capacity, domain);
            std::copy(generated.begin(), generated.end(), bytes.begin());
            OPENSSL_cleanse(generated.data(), generated.size());
            begin = aligned; valid = true;
        }
        return bytes[static_cast<std::size_t>(offset-begin)];
    }
    int sign(std::uint64_t chip) {
        return ((byte(chip/8) >> (chip%8)) & 1U) ? -1 : 1;
    }
    std::complex<double> noise(std::uint64_t position, StreamCache* first = nullptr,
                               StreamCache* second = nullptr, StreamCache* third = nullptr) {
        require(position<=(std::numeric_limits<std::uint64_t>::max()-7)/8,"pattern noise coordinate overflow");
        const auto uniform=[&](std::uint64_t offset) {
            std::uint32_t value=0;
            for(unsigned i=0;i<4;++i) {
                auto encoded=byte(offset+i);
                if(first)encoded^=first->byte(offset+i);
                if(second)encoded^=second->byte(offset+i);
                if(third)encoded^=third->byte(offset+i);
                value=(value<<8)|encoded;
            }
            return (static_cast<double>(value)+.5)/4294967296.;
        };
        // Circular Gaussian noise avoids the one-quadrature pattern structure
        // of random +/- chips. Limit its crest factor to stay inside PCM
        // headroom, normalizing E[|noise|^2] back to one.
        constexpr double maximum_radius=1.75;
        static const double normalization=std::sqrt(1-std::exp(-maximum_radius*maximum_radius));
        const auto radius=std::min(maximum_radius,std::sqrt(-std::log(uniform(position*8))));
        return std::polar(radius/normalization,tau*uniform(position*8+4));
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
    std::uint64_t chip = 0, symbol = 0, chips = 0, epoch = 0;
    bool shaped = false;
    StreamCache pattern, dsss;
    struct CachedChip {
        std::uint64_t address=0;
        std::complex<double> value{};
        bool valid=false;
    };
    std::array<CachedChip,32> shaped_chips{};
    Impl(Config value, std::uint64_t start_epoch): config(value), epoch(start_epoch),
        pattern(config.scramble ? std::span<const std::uint8_t>(config.spreading_seed) :
                                 std::span<const std::uint8_t>(public_seed),
                StreamPurpose::Scrambler, config.scramble ? start_epoch : 0),
        dsss(config.dsss_seed, StreamPurpose::Dsss, start_epoch) {
        chip = pattern_chip_samples(config);
        symbol = symbol_sample_count(config);
        chips = symbol / chip + (symbol % chip != 0);
        require(config.stream_phase_samples < config.sample_rate,
                "pattern stream phase must be within its first second");
        shaped=pattern_pulse_enabled(config);
        require(sizeof(Impl) + sizeof(PatternCode) <= config.memory_limit,
                "pattern code exceeds memory limit");
    }
    ~Impl() { OPENSSL_cleanse(shaped_chips.data(),sizeof(shaped_chips)); }
    std::complex<double> value(std::uint64_t absolute_chip, unsigned bit, double fraction) {
        require(bit <= 1, "pattern symbol must be a zero or one bit");
        require(std::isfinite(fraction) && fraction >= 0 && fraction < 1,
                "pattern chip fraction must be within [0,1)");
        auto position = absolute_chip % chips;
        if (config.scramble || config.dsss) {
            const auto address = symbol_stream_address(epoch, config.stream_phase_samples,
                absolute_chip / chips, symbol, config.sample_rate);
            position = symbol_stream_chip(address, chips, position);
            if (config.scramble) pattern.select_epoch(address.epoch);
            if (config.dsss) dsss.select_epoch(address.epoch);
        }
        if (config.spreading_mode == SpreadingMode::pattern) {
            // A secret +/- sign on a real carrier disappears on squaring.
            // Mix every enabled private stream before mapping both amplitude
            // and phase, retaining the public internal-transition distinction
            // between the two candidate patterns used by blind acquisition.
            // Public rows use the same I/Q map and repeat at symbol boundaries
            // so acquisition needs no transmission-index hypothesis.
            auto result = pattern.noise(position, config.dsss ? &dsss : nullptr);
            if (bit) result *= bit_mask[static_cast<std::size_t>((absolute_chip % chips) % bit_mask.size())];
            return result;
        }
        const auto angle = (bit ? 1. : -1.) * std::numbers::pi / 2 *
                           (static_cast<double>(absolute_chip % 4) + fraction);
        auto result = std::polar(1., angle);
        if (config.dsss) result *= static_cast<double>(dsss.sign(position));
        return result;
    }
    std::complex<double> shaped_value(std::uint64_t first_chip,unsigned bit,double within) {
        require(bit<=1,"pattern symbol must be a zero or one bit");
        require(std::isfinite(within),"pattern sample coordinate must be finite");
        if(!shaped) {
            if(within<0 || within>=static_cast<double>(symbol))return {};
            const auto local=static_cast<std::uint64_t>(within/static_cast<double>(chip));
            require(local<=std::numeric_limits<std::uint64_t>::max()-first_chip,"pattern chip address would overflow");
            return value(first_chip+local,bit,std::clamp(within/static_cast<double>(chip)-static_cast<double>(local),0.,std::nextafter(1.,0.)));
        }
        return pattern_pulse_sum(within,symbol,chip,[&](std::uint64_t local) {
            require(local<=std::numeric_limits<std::uint64_t>::max()-first_chip,"pattern chip address would overflow");
            const auto absolute=first_chip+local;
            auto& entry=shaped_chips[absolute%shaped_chips.size()];
            if(!entry.valid || entry.address!=absolute) {
                entry.value=value(absolute,0,0);entry.address=absolute;entry.valid=true;
            }
            auto result=entry.value;
            if(bit)result*=bit_mask[static_cast<std::size_t>((absolute%chips)%bit_mask.size())];
            return result;
        });
    }
};

PatternCode::PatternCode(Config config, std::uint64_t epoch): impl_(std::make_unique<Impl>(config, epoch)) {}
PatternCode::~PatternCode() = default;
PatternCode::PatternCode(PatternCode&&) noexcept = default;
PatternCode& PatternCode::operator=(PatternCode&&) noexcept = default;
std::complex<double> PatternCode::value(std::uint64_t chip, unsigned bit, double fraction) {
    return impl_->value(chip, bit, fraction);
}
std::complex<double> PatternCode::shaped_value(std::uint64_t first_chip,unsigned bit,double within) {
    return impl_->shaped_value(first_chip,bit,within);
}
void PatternCode::set_stream_phase_samples(std::uint64_t phase_samples) {
    require(phase_samples < impl_->config.sample_rate,
            "pattern stream phase must be within its first second");
    if (phase_samples == impl_->config.stream_phase_samples) return;
    impl_->config.stream_phase_samples = phase_samples;
    for (auto& entry : impl_->shaped_chips) entry.valid = false;
}
std::uint64_t PatternCode::chip_samples() const { return impl_->chip; }
std::uint64_t PatternCode::chips_per_symbol() const { return impl_->chips; }
std::uint64_t PatternCode::symbol_samples() const { return impl_->symbol; }
std::size_t PatternCode::working_bytes() const { return sizeof(PatternCode) + sizeof(Impl); }

struct PatternTransmitter::Impl {
    Bytes bits;
    Config config;
    PatternCode code;
    std::unique_ptr<StreamCache> settling, settling_data, settling_pattern, settling_dsss;
    std::unique_ptr<StreamCache> suppression, suppression_data, suppression_pattern, suppression_dsss;
    std::uint64_t start = 0, position = 0, total = 0, training = 0, padding = 0, content_end = 0, suppression_samples = 0;
    struct SettlingChip {
        std::uint64_t address=0;
        std::complex<double> value{};
        bool valid=false;
    };
    std::array<SettlingChip,32> settling_chips{};
    std::array<SettlingChip,32> suppression_chips{};
    Impl(Bytes input, Config value, std::uint64_t epoch, std::uint64_t start_chip, bool surrounding_noise):
        bits(std::move(input)), config(value), code(config, epoch), start(start_chip) {
        require(!bits.empty(), "pattern transmission requires at least one bit");
        require(std::all_of(bits.begin(), bits.end(), [](auto bit) { return bit <= 1; }),
                "pattern input elements must be zero or one");
        require(start % code.chips_per_symbol() == 0,
                "complete pattern transmission must start at a symbol-aligned chip address");
        require(bits.size() <= std::numeric_limits<std::uint64_t>::max() / code.symbol_samples(),
                "pattern transmission duration would overflow");
        total = static_cast<std::uint64_t>(bits.size()) * code.symbol_samples();
        training=surrounding_noise?training_sample_count(config):0;
        require(training<=std::numeric_limits<std::uint64_t>::max()-total,"pattern transmission duration would overflow");
        total+=training;
        padding=pattern_pulse_padding_samples(config);
        require(padding<=(std::numeric_limits<std::uint64_t>::max()-total)/2,"pattern transmission duration would overflow");
        total+=2*padding;
        content_end=total;
        suppression_samples=surrounding_noise?suppression_sample_count(config):0;
        require(suppression_samples<=std::numeric_limits<std::uint64_t>::max()-total,
                "pattern suppression duration would overflow");
        total+=suppression_samples;
        require(bits.size() <= std::numeric_limits<std::uint64_t>::max() / code.chips_per_symbol(),
                "pattern transmission chip count would overflow");
        const auto chip_count = static_cast<std::uint64_t>(bits.size()) * code.chips_per_symbol();
        require(chip_count - 1 <= std::numeric_limits<std::uint64_t>::max() - start,
                "pattern transmission chip address would overflow");
        if(config.scramble || config.dsss)
            require(start+chip_count-1 <= (std::numeric_limits<std::uint64_t>::max()-7)/8,
                    "private pattern byte address would overflow");
        const auto caches=(static_cast<unsigned>(training!=0)+static_cast<unsigned>(suppression_samples!=0))*
            (1U+static_cast<unsigned>(config.data_key.has_value())+
             static_cast<unsigned>(config.scramble)+static_cast<unsigned>(config.dsss));
        const auto fixed = sizeof(Impl) + sizeof(PatternTransmitter) + code.working_bytes()+caches*sizeof(StreamCache);
        require(fixed <= config.memory_limit && bits.capacity() <= config.memory_limit - fixed,
                "pattern transmitter exceeds memory limit");
        if(training) {
            // Private layers use the same keys, purposes and epoch as the
            // payload; only the fixed CTR pad changes. Encrypt noise bytes
            // together with every enabled spreading stream before I/Q mapping.
            constexpr auto domain=StreamDomain::Preamble;
            settling=std::make_unique<StreamCache>(public_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.data_key)settling_data=std::make_unique<StreamCache>(
                *config.data_key,StreamPurpose::Data,epoch,domain);
            if(config.scramble)settling_pattern=std::make_unique<StreamCache>(
                config.spreading_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.dsss)settling_dsss=std::make_unique<StreamCache>(
                config.dsss_seed,StreamPurpose::Dsss,epoch,domain);
        }
        if(suppression_samples) {
            // A third CTR domain makes the tail independent of both the
            // settling prefix and every valid payload pattern/mask position.
            constexpr auto domain=StreamDomain::Suppression;
            suppression=std::make_unique<StreamCache>(public_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.data_key)suppression_data=std::make_unique<StreamCache>(
                *config.data_key,StreamPurpose::Data,epoch,domain);
            if(config.scramble)suppression_pattern=std::make_unique<StreamCache>(
                config.spreading_seed,StreamPurpose::Scrambler,epoch,domain);
            if(config.dsss)suppression_dsss=std::make_unique<StreamCache>(
                config.dsss_seed,StreamPurpose::Dsss,epoch,domain);
        }
    }
    ~Impl() {
        OPENSSL_cleanse(settling_chips.data(),sizeof(settling_chips));
        OPENSSL_cleanse(suppression_chips.data(),sizeof(suppression_chips));
    }
    std::complex<double> suppression_sample(std::uint64_t offset) {
        const auto noise=[&](std::uint64_t chip) {
            auto& entry=suppression_chips[chip%suppression_chips.size()];
            if(!entry.valid || entry.address!=chip) {
                entry.value=suppression->noise(chip,suppression_data.get(),suppression_pattern.get(),suppression_dsss.get());
                entry.address=chip;entry.valid=true;
            }
            return entry.value;
        };
        // Virtual surrounding chips give shaped noise its normal power from
        // the first sample, without changing or overlapping the payload tail.
        const auto value=padding?pattern_pulse_sum(static_cast<double>(offset+padding),
            suppression_samples+2*padding,code.chip_samples(),noise):noise(offset/code.chip_samples());
        // A ten-millisecond edge taper avoids an abrupt boundary. The fixed
        // output duration includes both tapers, including for very long bits.
        const auto edge=std::min(offset,suppression_samples-1-offset);
        const auto ramp=std::max<std::uint64_t>(1,(config.sample_rate+99U)/100U);
        const auto window=edge>=ramp?1.:std::pow(std::sin(.5*std::numbers::pi*
            static_cast<double>(edge)/static_cast<double>(ramp)),2);
        return value*window;
    }
    std::complex<double> shaped_sample(std::uint64_t cursor) {
        const auto relative=static_cast<long double>(cursor)-padding;
        std::complex<double> result{};
        if(training) {
            result+=pattern_pulse_sum(static_cast<double>(relative),training,code.chip_samples(),[&](std::uint64_t chip) {
                auto& entry=settling_chips[chip%settling_chips.size()];
                if(!entry.valid || entry.address!=chip) {
                    entry.value=settling->noise(chip,settling_data.get(),settling_pattern.get(),settling_dsss.get());
                    entry.address=chip;entry.valid=true;
                }
                return entry.value;
            });
        }
        const auto payload=relative-training;
        const auto support=static_cast<long double>(padding);
        const auto duration=static_cast<long double>(code.symbol_samples());
        const auto begin=std::max(0.L,std::floor((payload-support)/duration));
        const auto end=std::min(static_cast<long double>(bits.size()),std::floor((payload+support)/duration)+1);
        for(auto symbol=static_cast<std::uint64_t>(begin);symbol<static_cast<std::uint64_t>(std::max(begin,end));++symbol)
            result+=code.shaped_value(start+symbol*code.chips_per_symbol(),bits[static_cast<std::size_t>(symbol)],
                static_cast<double>(payload-symbol*duration));
        return result;
    }
    template<class Output, class Convert>
    std::size_t render(std::uint64_t& cursor, std::span<Output> output, Convert convert,
                       std::stop_token stop, const ChipObserver& observer) {
        cancelled(stop);
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), total - cursor));
        const auto angle = std::remainder(static_cast<long double>(cursor) * tau * config.carrier_hz / config.sample_rate,
                                         static_cast<long double>(tau));
        auto oscillator = std::polar(1., static_cast<double>(angle));
        const auto step = std::polar(1., tau * config.carrier_hz / config.sample_rate);
        const auto amplitude = std::sqrt(2 * nominal_signal_power);
        if(padding) {
            for(std::size_t i=0;i<count;++i,++cursor) {
                if((i&4095U)==0)cancelled(stop);
                const auto baseband=cursor<content_end?shaped_sample(cursor):suppression_sample(cursor-content_end);
                output[i]=convert(oscillator*pattern_limit_pcm(amplitude*baseband));
                if(observer && cursor>=padding+training && cursor<content_end-padding) {
                    const auto payload=cursor-padding-training;
                    const auto symbol=payload/code.symbol_samples();
                    const auto within=payload%code.symbol_samples();
                    if(within%code.chip_samples()==0)
                        observer(amplitude*code.value(start+symbol*code.chips_per_symbol()+within/code.chip_samples(),
                            bits[static_cast<std::size_t>(symbol)]));
                }
                oscillator*=step;
            }
            return count;
        }
        for (std::size_t i = 0; i < count;) {
            cancelled(stop);
            if(cursor>=content_end) {
                for(;i<count;++i,++cursor) {
                    if((i&4095U)==0)cancelled(stop);
                    output[i]=convert(amplitude*oscillator*suppression_sample(cursor-content_end));
                    oscillator*=step;
                }
                continue;
            }
            if(cursor<training) {
                const auto chip=cursor/code.chip_samples();
                auto pattern=settling->noise(chip,settling_data.get(),settling_pattern.get(),settling_dsss.get());
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
                if (j == 0 && within % code.chip_samples() == 0 && observer)
                    observer(amplitude * pattern);
                oscillator *= step; pattern *= pattern_step;
            }
        }
        return count;
    }
};

PatternTransmitter::PatternTransmitter(Bytes bits, Config config, std::uint64_t epoch, std::uint64_t start_chip, bool surrounding_noise):
    impl_(std::make_unique<Impl>(std::move(bits), config, epoch, start_chip, surrounding_noise)) {}
PatternTransmitter::~PatternTransmitter() = default;
PatternTransmitter::PatternTransmitter(PatternTransmitter&&) noexcept = default;
PatternTransmitter& PatternTransmitter::operator=(PatternTransmitter&&) noexcept = default;
std::size_t PatternTransmitter::read(std::span<float> output, std::stop_token stop, const ChipObserver& observer) {
    return impl_->render(impl_->position, output, [](auto value) { return static_cast<float>(value.real()); }, stop, observer);
}
std::size_t PatternTransmitter::read_analytic(std::span<std::complex<double>> output, std::stop_token stop,
                                          const ChipObserver& observer) {
    return impl_->render(impl_->position, output, [](auto value) { return value; }, stop, observer);
}
void PatternTransmitter::preview_last_analytic(std::span<std::complex<double>> output) const {
    require(output.size() <= analytic_preview_limit, "pattern preview exceeds its bounded sample limit");
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(output.size(), impl_->position));
    const auto leading = output.size() - count;
    std::fill_n(output.begin(), leading, std::complex<double>{});
    auto cursor = impl_->position - count;
    impl_->render(cursor, output.subspan(leading), [](auto value) { return value; }, {}, {});
}
bool PatternTransmitter::finished() const { return impl_->position == impl_->total; }
std::uint64_t PatternTransmitter::total_samples() const { return impl_->total; }
std::uint64_t PatternTransmitter::samples_emitted() const { return impl_->position; }
double PatternTransmitter::bit_rate() const { return static_cast<double>(impl_->config.sample_rate) / static_cast<double>(impl_->code.symbol_samples()); }
std::size_t PatternTransmitter::working_bytes() const {
    return sizeof(PatternTransmitter) + sizeof(Impl) + impl_->code.working_bytes() + impl_->bits.capacity()+
        ((impl_->settling?1U:0U)+(impl_->settling_data?1U:0U)+(impl_->settling_pattern?1U:0U)+
         (impl_->settling_dsss?1U:0U)+(impl_->suppression?1U:0U)+(impl_->suppression_data?1U:0U)+
         (impl_->suppression_pattern?1U:0U)+(impl_->suppression_dsss?1U:0U))*sizeof(StreamCache);
}

} // namespace datapump::modem

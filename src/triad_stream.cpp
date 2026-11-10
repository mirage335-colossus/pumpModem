#include "datapump/triad_stream.hpp"

#include "datapump/triad_transmission_keys.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>

namespace datapump::triad::v3 {
namespace {

constexpr auto last_pattern_epoch = (std::uint64_t{1} << 32) - 7;
constexpr auto helper_epoch_limit = std::uint64_t{1} << 32;
constexpr auto max_coordinate = std::numeric_limits<std::uint64_t>::max();
constexpr double spectral_skirt_allowance_hz = 200;

void require(bool condition, const char* description) {
    if (!condition) throw Error(description);
}

void check_stop(std::stop_token stop) {
    require(!stop.stop_requested(), "Robust transmission cancelled");
}

std::uint64_t add(std::uint64_t left, std::uint64_t right) {
    require(right <= max_coordinate - left, "Robust sample or slot coordinate overflow");
    return left + right;
}

std::uint64_t receive_coordinate(double value, bool clamp_before_origin) {
    require(std::isfinite(value) && value < std::ldexp(1.L, 64) &&
        (clamp_before_origin || value >= 0), "public receive window overflows sample coordinates");
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

HelperBurstConfig burst_config(const PublicProfileConfig& config, const Geometry& geometry) {
    require(config.format.sample_rate_denominator == 1,
        "whole-bit Robust stream requires an integer sample rate");
    const auto count = std::round(static_cast<long double>(geometry.bit_seconds) * config.format.sample_rate);
    require(count >= 1 && count < std::ldexp(1.L, 64), "whole-bit logical calendar extent overflow");
    return {config.format.sample_rate, static_cast<std::uint64_t>(count), geometry.source_samples,
        geometry.calendar_origin_sample, config.wait_bits, config.helper_enabled, 0,
        config.random_extra_bits, config.format.epoch, config.format.symbol_ordinal};
}

std::uint64_t mix64(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

Complex rotation(double frequency, long double seconds) {
    constexpr auto tau = 2 * std::numbers::pi;
    return std::polar(1., tau * static_cast<double>(std::remainder(frequency * seconds, 1.L)));
}

helper::PowerReference helper_reference(const Format& format, long double helper_end) {
    helper::PowerReference reference;
    reference.epoch = format.epoch;
    reference.begin_seconds = 39;
    reference.end_seconds = 41;
    reference.helper_start_seconds = 0;
    reference.helper_end_seconds = helper_end;
    reference.active = [format](long double seconds) {
        if (!format.fhss) return true;
        auto within = std::fmod(seconds - helper_lead_seconds, static_cast<long double>(format.dwell_seconds));
        if (within < 0) within += format.dwell_seconds;
        return std::min(within, static_cast<long double>(format.dwell_seconds) - within) > format.quiet_seconds;
    };
    return reference;
}

DataPowerReference bounded_tile_power(PatternSource& source, std::stop_token stop) {
    // Fixed public bit zero on at most sixteen stratified keyed tiles. One
    // normalization applies to the complete stream, including gaps and tails.
    constexpr unsigned maximum_tiles = 16, nodes_per_tile = 2048;
    const auto& format = source.format();
    const auto& geometry = source.geometry();
    const auto tiles = static_cast<unsigned>(std::min<std::uint64_t>(maximum_tiles, geometry.tile_count));
    const long double active_dwell = format.dwell_seconds - 2 * format.quiet_seconds;
    const long double active_tile = geometry.tile_seconds * (format.fhss ? active_dwell / format.dwell_seconds : 1.L);
    DataPowerReference result;
    result.active_seconds = static_cast<double>(active_tile * geometry.tile_count);
    result.sampled_tiles = tiles;
    result.nodes = tiles * nodes_per_tile;
    long double sum = 0, squares = 0;
    for (unsigned j = 0; j < tiles; ++j) {
        const auto begin = static_cast<std::uint64_t>(static_cast<long double>(geometry.tile_count) * j / tiles);
        const auto end = static_cast<std::uint64_t>(static_cast<long double>(geometry.tile_count) * (j + 1) / tiles);
        const auto tile = begin + mix64(format.epoch ^ (0x54494c45504f57ULL + j)) % (end - begin);
        long double energy = 0;
        for (unsigned i = 0; i < nodes_per_tile; ++i) {
            if ((i & 255) == 0) check_stop(stop);
            const auto random = mix64(format.epoch ^ tile ^ (0x4e4f4445504f57ULL + i));
            const auto fraction = static_cast<long double>(random >> 11) / 9007199254740992.L;
            const auto coordinate = active_tile * (i + fraction) / nodes_per_tile;
            long double within = coordinate;
            if (format.fhss) {
                const auto dwell = std::floor(coordinate / active_dwell);
                within = dwell * format.dwell_seconds + format.quiet_seconds + coordinate - dwell * active_dwell;
            }
            const auto seconds = tile * (static_cast<long double>(format.burst_wait_tiles) + 1) * geometry.tile_seconds + within;
            energy += std::norm(source.sample_at(geometry.calendar_origin_sample + seconds * format.sample_rate));
        }
        const auto mean = energy / nodes_per_tile;
        sum += mean;
        squares += mean * mean;
    }
    result.mean_norm = static_cast<double>(sum / tiles);
    require(result.mean_norm > 0 && std::isfinite(result.mean_norm), "invalid bounded DATA tile power reference");
    if (tiles > 1) {
        const auto variance = std::max(0.L, (squares - sum * sum / tiles) / (tiles - 1));
        result.tile_relative_standard_error = static_cast<double>(std::sqrt(variance / tiles) / result.mean_norm);
    }
    return result;
}

} // namespace

PublicProfile::PublicProfile(PublicProfileConfig config, std::optional<Crypto> calendar_key)
    : config_(std::move(config)), geometry_(v3::geometry(config_.format)), calendar_(burst_config(config_, geometry_), std::move(calendar_key)),
      placement_{config_.data_if_hz, geometry_.occupied_bandwidth_hz, config_.gate_bandwidth_hz, config_.format.fhss} {
    require(config_.format.epoch >= 6 && config_.format.epoch <= last_pattern_epoch,
        "Robust calendar epoch must support the thirteen public epoch alternatives");
    require(std::isfinite(config_.data_if_hz) && config_.data_if_hz > 0 && config_.data_if_hz < effective_sample_rate() / 2,
        "Robust DATA IF must be positive and below Nyquist");
    for (const auto value : {config_.capture_lead_seconds, config_.capture_trail_seconds, config_.audio_pair_ppm,
            config_.clock_bound_ppm, config_.cfo_bound_hz, config_.spread_delay_ms, config_.spread_doppler_hz,
            config_.signal_delay_ms.value_or(0), config_.delay_excursion_seconds, config_.gate_bandwidth_hz})
        require(std::isfinite(value) && value >= 0, "invalid Robust public search or channel bounds");
    require(config_.audio_pair_ppm < 2e6 && std::isfinite(config_.arrival_low_seconds) &&
        std::isfinite(config_.arrival_high_seconds) && config_.arrival_low_seconds <= config_.arrival_high_seconds &&
        config_.signal_delay_ms.value_or(0) <= config_.spread_delay_ms,
        "invalid Robust public arrival, clock, or delay range");
    if (!config_.lead_samples)
        config_.lead_samples = config_.helper_enabled ? std::uint64_t{helper_lead_seconds} * config_.format.sample_rate : 0;
    const auto first = transmit_slot(0);
    require(first.helper_begin >= origin(), "Robust calendar anchor must precede the first Helper sample");
    helper::validate(config_.helper);
    if (config_.helper_enabled) {
        // Use the same public active reference window as the emitted source.
        // The placement guard is independent of its keyed auxiliary symbols.
        const auto reference = helper_reference(config_.format, 42);
        helper_plan_ = helper::plan(config_.helper, placement_, config_.format.sample_rate, 0, 1, &reference);
    }
}

HelperBurstSlot PublicProfile::transmit_slot(std::uint64_t index) const {
    (void)add(config_.format.symbol_ordinal, index);
    const auto slot = calendar_.slot_at(calendar_.add(origin(), *config_.lead_samples), index);
    require(slot.pattern_begin.epoch >= 6 && slot.pattern_begin.epoch <= last_pattern_epoch &&
        slot.helper_end.epoch < helper_epoch_limit, "public slot exceeds keyed epoch or Helper metadata range");
    return slot;
}

std::uint64_t PublicProfile::first_slot_not_before(HelperBurstCoordinate earliest_helper) const {
    (void)calendar_.add(earliest_helper, 0);
    const auto first = transmit_slot(0);
    if (earliest_helper <= first.helper_begin) return 0;
    if (config_.random_extra_bits) {
        auto high = last_slot();
        require(earliest_helper <= transmit_slot(high).helper_begin, "Robust transmission calendar is exhausted");
        std::uint64_t low = 1;
        while (low < high) {
            const auto middle = low + (high - low) / 2;
            if (transmit_slot(middle).helper_begin < earliest_helper) low = middle + 1;
            else high = middle;
        }
        return low;
    }
    const auto next = calendar_.successor(first);
    const auto stride = calendar_.distance(first.helper_begin, next.helper_begin);
    const auto distance = calendar_.distance(first.helper_begin, earliest_helper);
    const auto index = distance / stride + (distance % stride != 0);
    (void)transmit_slot(index);
    return index;
}

std::uint64_t PublicProfile::last_slot() const {
    const auto first = transmit_slot(0);
    // Every recurrence advances at least 60 seconds, including random waits.
    auto high = std::min((last_pattern_epoch - first.pattern_begin.epoch) / 60,
        max_coordinate - config_.format.symbol_ordinal);
    std::uint64_t low = 0;
    const auto first_support = calendar_.add(origin(), *config_.lead_samples);
    while (low < high) {
        const auto middle = low + (high - low + 1) / 2;
        const auto candidate = calendar_.slot_at(first_support, middle);
        if (candidate.pattern_begin.epoch <= last_pattern_epoch && candidate.helper_end.epoch < helper_epoch_limit)
            low = middle;
        else high = middle - 1;
    }
    return low;
}

std::uint64_t PublicProfile::capture_stop(long double source_end_seconds) const {
    const auto rate = effective_sample_rate();
    const auto half = config_.audio_pair_ppm / 2;
    const auto tx_min = 1 - half * 1e-6, rx_max = 1 + half * 1e-6;
    const auto tail = config_.spread_delay_ms * .001 + config_.delay_excursion_seconds +
        2 / (3 * geometry_.carrier_spacing_hz) + 32. / rate;
    const auto samples = std::ceil(rate * rx_max * (config_.capture_lead_seconds +
        static_cast<double>(source_end_seconds) / tx_min + config_.arrival_high_seconds + config_.capture_trail_seconds + tail));
    return add(add(receive_coordinate(samples, false), config_.receiver_tail_samples),
        config_.receiver_filter_halo_samples);
}

std::uint64_t PublicProfile::capture_first(long double source_begin_seconds) const {
    const auto half = config_.audio_pair_ppm / 2;
    const auto tx_max = 1 + half * 1e-6, rx_min = 1 - half * 1e-6;
    return receive_coordinate(std::floor(effective_sample_rate() * rx_min *
        (static_cast<double>(source_begin_seconds) / tx_max + config_.arrival_low_seconds)), true);
}

std::uint64_t PublicProfile::signal_first(long double source_begin_seconds) const {
    const auto rate = effective_sample_rate();
    const auto half = config_.audio_pair_ppm / 2;
    const auto tx_max = 1 + half * 1e-6, tx_min = 1 - half * 1e-6;
    return receive_coordinate(std::floor(rate * tx_min * (config_.capture_lead_seconds +
        static_cast<double>(source_begin_seconds) / tx_max + config_.arrival_low_seconds - 32. / (rate * tx_min))), true);
}

std::uint64_t PublicProfile::signal_stop(long double source_end_seconds) const {
    const auto rate = effective_sample_rate();
    const auto half = config_.audio_pair_ppm / 2;
    const auto tx_min = 1 - half * 1e-6, rx_max = 1 + half * 1e-6;
    const auto tail = config_.signal_delay_ms.value_or(config_.spread_delay_ms) * .001 + config_.delay_excursion_seconds +
        (config_.selective_signal_tail ? 2 / (3 * geometry_.carrier_spacing_hz) : 0.) + 32. / rate;
    return receive_coordinate(std::ceil(rate * rx_max * (config_.capture_lead_seconds +
        static_cast<double>(source_end_seconds) / tx_min + config_.arrival_high_seconds + tail)), false);
}

PublicSlot PublicProfile::slot(std::uint64_t index) const {
    const auto tx = transmit_slot(index);
    const auto next = calendar_.successor(tx);
    const auto seconds = [&](HelperBurstCoordinate value) { return calendar_.relative_seconds(origin(), value); };
    const auto halo = config_.receiver_filter_halo_samples;
    const auto earlier = [halo](std::uint64_t sample) { return sample - std::min(sample, halo); };
    const auto filtered_stop = capture_stop(seconds(tx.helper_end));
    const SampleRange original_observation{capture_first(seconds(tx.helper_begin)), filtered_stop - halo};
    require(original_observation.stop > original_observation.first, "public receive window is empty");
    const SampleRange observation{earlier(original_observation.first), filtered_stop};
    const SampleRange data{signal_first(seconds(tx.support_begin)), signal_stop(seconds(tx.support_end))};
    const auto previous_stop = index ? add(signal_stop(seconds(transmit_slot(index - 1).helper_end)), halo) : 0;
    const auto next_first = earlier(signal_first(seconds(next.helper_begin)));
    // Extra samples retain filter support in the scorer. They cannot enlarge
    // the existing quiet aperture, and adjacent pulse tails can narrow it.
    const auto quiet_first = std::min(original_observation.stop,
        std::max({original_observation.first, data.first, previous_stop}));
    const auto quiet_stop = std::max(quiet_first, std::min({original_observation.stop, data.stop, next_first}));
    return {index, tx.pattern_begin.epoch, config_.format.symbol_ordinal + index, tx,
        observation, data, {quiet_first, quiet_stop}, previous_stop, next_first};
}

Format PublicProfile::basis_format(const PublicSlot& slot) const {
    auto result = config_.format;
    result.epoch = slot.pattern_epoch;
    result.symbol_ordinal = slot.symbol_ordinal;
    return result;
}

DataPowerReference data_power_reference(const Format& format, const Crypto& key, std::stop_token stop) {
    check_stop(stop);
    PatternSource source(format, key, 0);
    return bounded_tile_power(source, stop);
}

struct Emission::Impl {
    PublicProfile profile;
    Format format;
    TransmissionKeys keys;
    EmissionReport report;
    Bytes bits;
    std::stop_token stop;
    std::unique_ptr<PatternSource> data;
    std::unique_ptr<helper::Generator> helper;
    std::optional<HelperBurstSlot> current;
    std::uint64_t last_slot = 0, source_first = 0, source_stop = 0, data_first = 0;
    std::uint64_t active_helper_first = 0, active_helper_stop = 0;
    long double active_helper_begin_seconds = 0, active_helper_end_seconds = 0;
    bool data_enabled = false, helper_enabled = false;
    bool noise = false;
    std::uint8_t isolated_bit = 0;

    Impl(const PublicProfile& selected_profile, const Crypto& key, const EmissionOptions& options,
         std::optional<std::pair<std::uint8_t, bool>> isolated = {})
        : profile(selected_profile), format(profile.config().format),
          keys(resolve_transmission_keys(key, format.encrypt,
              options.mode == EmissionMode::Noise ? TransmissionMode::Noise : TransmissionMode::Message)),
          stop(options.stop), source_first(options.source_first_sample) {
        check_stop(stop);
        format.encrypt = keys.encrypt_data;
        noise = options.mode == EmissionMode::Noise;
        data_enabled = isolated ? isolated->second : options.mode == EmissionMode::Message || noise;
        helper_enabled = profile.config().helper_enabled && options.mode != EmissionMode::Quiet;
        if (options.mode == EmissionMode::HelperOnly)
            require(helper_enabled && profile.helper_plan().effective_power_w > 0, "Helper-only transmission requires enabled Helper power");
        const auto rate = format.sample_rate;
        const auto& geometry = profile.geometry();
        if (isolated) {
            isolated_bit = isolated->first;
            require(isolated_bit <= 1 && rate % 5 == 0, "invalid isolated Robust fixture geometry");
            const auto dwell_samples = 2ULL * rate / 5;
            const auto lead = std::uint64_t{helper_lead_seconds} * rate;
            require(!helper_enabled || geometry.calendar_origin_sample <= lead,
                "DATA pulse padding exceeds the fixed Helper lead");
            data_first = helper_enabled ? lead - geometry.calendar_origin_sample : 0;
            const auto data_stop = add(data_first, geometry.source_samples);
            source_stop = add(data_stop, dwell_samples - 1) / dwell_samples * dwell_samples;
            active_helper_stop = source_stop;
            active_helper_end_seconds = static_cast<long double>(source_stop) / rate;
            require(!helper_enabled || static_cast<long double>(format.epoch) + std::ceil(active_helper_end_seconds) < helper_epoch_limit,
                "source endpoint exceeds Helper32-bit epoch calendar");
        } else {
            const auto count = options.mode == EmissionMode::Message ? options.bits.size() : options.slot_count;
            require(count > 0 && (options.mode == EmissionMode::Message || options.bits.empty()),
                "invalid Robust transmission slot count or unexpected draft");
            // Draft allocation is bounded locally before examining its bits.
            require(options.bits.size() <= 65536 && std::all_of(options.bits.begin(), options.bits.end(),
                [](std::uint8_t bit) { return bit <= 1; }), "invalid or oversized Robust transmit draft");
            bits.assign(options.bits.begin(), options.bits.end());
            last_slot = add(options.first_slot, count - 1);
            source_stop = profile.calendar().distance(profile.origin(), profile.transmit_slot(last_slot).helper_end);
            set_slot(profile.transmit_slot(options.first_slot));
            require(source_first <= active_helper_first, "Robust playback starts after the first transmitted sample");
        }
        report.first_data_sample = data_first - source_first;
        prepare_power();
    }

    void set_slot(const HelperBurstSlot& slot) {
        current = slot;
        data.reset();
        const auto& calendar = profile.calendar();
        const auto origin = profile.origin();
        data_first = calendar.distance(origin, slot.support_begin);
        active_helper_first = calendar.distance(origin, slot.helper_begin);
        active_helper_stop = calendar.distance(origin, slot.helper_end);
        active_helper_begin_seconds = calendar.relative_seconds(origin, slot.helper_begin);
        active_helper_end_seconds = calendar.relative_seconds(origin, slot.helper_end);
    }

    void prepare_power() {
        const auto& geometry = profile.geometry();
        const auto rate = format.sample_rate;
        PatternSource reference_source(format, keys.data, 0);
        const auto preview_offset = static_cast<long double>(helper_lead_seconds) * rate - geometry.calendar_origin_sample;
        const auto helper_end = static_cast<long double>(source_stop) / rate;
        auto reference = helper_reference(format, current ? std::max(helper_end, 42.L) : helper_end);
        reference.time_key = keys.helper;
        long double norm = 0;
        unsigned nodes = 0;
        for (unsigned i = 0; i < helper::power_reference_nodes; ++i) {
            if ((i & 255) == 0) check_stop(stop);
            const auto seconds = helper::power_reference_node(reference, i);
            if (!reference.active(seconds)) continue;
            norm += std::norm(reference_source.sample_at(seconds * rate - preview_offset));
            ++nodes;
        }
        require(nodes && norm > 0, "empty DATA power reference");
        report.reference_data_mean_norm = static_cast<double>(norm / nodes);
        reference.data_mean_power = report.reference_data_mean_norm / 2;
        if (helper_enabled)
            report.helper_plan = helper::plan(profile.config().helper, profile.placement(), rate, 0, 1, &reference);
        report.data_reference = bounded_tile_power(reference_source, stop);
        report.data_calibration_gain = std::sqrt(report.reference_data_mean_norm / report.data_reference.mean_norm);
        const auto data_gain = report.helper_plan.data_gain;
        report.composite_gain = helper_enabled ? 1 / (data_gain * std::sqrt(report.reference_data_mean_norm)) : 1.;
        report.data_gain = helper_enabled ? data_gain * report.composite_gain * report.data_calibration_gain :
            1 / std::sqrt(report.data_reference.mean_norm);
        if (!helper_enabled) return;
        helper = std::make_unique<helper::Generator>(profile.config().helper, report.helper_plan, format.epoch, true, keys.helper);
        long double measured_helper_norm = 0;
        for (unsigned i = 0; i < helper::power_reference_nodes; ++i) {
            if ((i & 255) == 0) check_stop(stop);
            const auto seconds = helper::power_reference_node(reference, i);
            if (reference.active(seconds))
                measured_helper_norm += std::norm(helper->mix_analytic({}, seconds, rate,
                    reference.helper_end_seconds, 0) * report.composite_gain);
        }
        report.reference_helper_ratio = static_cast<double>(measured_helper_norm / nodes);
    }

    Complex next(std::uint64_t local_index) {
        require(local_index == report.generated_samples && local_index < source_stop - source_first,
            "Robust emitted source requires one forward reader");
        if ((local_index & 255) == 0) check_stop(stop);
        const auto index = source_first + local_index;
        if (current && current->index < last_slot && index >= active_helper_stop)
            set_slot(profile.calendar().successor(*current));
        Complex raw_data;
        if (data_enabled && index >= data_first && index - data_first < profile.geometry().source_samples) {
            if (!data) {
                auto addressed_format = format;
                auto bit = isolated_bit;
                if (current) {
                    addressed_format.epoch = current->pattern_begin.epoch;
                    addressed_format.symbol_ordinal += current->index;
                    if (!noise) {
                        const auto first_slot = last_slot - (bits.size() - 1);
                        bit = bits[static_cast<std::size_t>(current->index - first_slot)];
                    }
                }
                data = std::make_unique<PatternSource>(addressed_format, keys.data, bit);
            }
            raw_data = data->sample_at(static_cast<long double>(index - data_first));
        }
        const auto seconds = static_cast<long double>(index) / format.sample_rate;
        const auto up = rotation(profile.data_if_hz(), seconds);
        const auto normalized_data = raw_data * report.data_gain;
        const bool active = helper && seconds >= active_helper_begin_seconds && seconds < active_helper_end_seconds;
        const auto audio = active ? helper->mix_analytic(raw_data * report.data_calibration_gain * up,
            seconds, format.sample_rate, active_helper_end_seconds, active_helper_begin_seconds) * report.composite_gain :
            normalized_data * up;
        const auto helper_component = active ? audio - normalized_data * up : Complex{};
        report.data_energy += std::norm(normalized_data);
        report.helper_energy += std::norm(helper_component);
        ++report.generated_samples;
        return audio * std::conj(up);
    }
};

Emission::Emission(const PublicProfile& profile, const Crypto& key, const EmissionOptions& options)
    : impl_(std::make_unique<Impl>(profile, key, options)) {}
Emission::Emission(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Emission Emission::isolated_fixture(const PublicProfile& profile, const Crypto& key, std::uint8_t bit, bool data_enabled) {
    return Emission(std::make_unique<Impl>(profile, key, EmissionOptions{}, std::pair{bit, data_enabled}));
}
Emission::~Emission() = default;
Emission::Emission(Emission&&) noexcept = default;
Emission& Emission::operator=(Emission&&) noexcept = default;

EmittedSource Emission::source() const {
    auto* state = impl_.get();
    const auto& geometry = state->profile.geometry();
    const auto full_bandwidth = geometry.occupied_bandwidth_hz + 2 / state->format.ramp_seconds;
    const auto if_hz = state->profile.data_if_hz();
    EmittedSource result;
    result.format = state->format;
    result.geometry = geometry;
    result.geometry.source_samples = state->source_stop - state->source_first;
    result.geometry.calendar_origin_sample = 0;
    result.minimum_frequency_hz = (state->helper_enabled ? std::min(-full_bandwidth / 2,
        helper::low_pilot_frequency_hz - if_hz) : -full_bandwidth / 2) - spectral_skirt_allowance_hz;
    result.maximum_frequency_hz = std::max(full_bandwidth / 2,
        state->helper_enabled ? state->report.helper_plan.frequency_hz - if_hz : 0.) + spectral_skirt_allowance_hz;
    result.working_bytes = sizeof(Impl) + state->bits.capacity() +
        (state->data_enabled ? geometry.source_workspace_bytes : 0) + (state->helper ? sizeof(helper::Generator) : 0);
    result.sample_at = [state](std::uint64_t index) { return state->next(index); };
    return result;
}

std::size_t Emission::read(std::span<Complex> output) {
    require(output.size() <= 16384, "Robust emitted chunk exceeds 16384 samples");
    check_stop(impl_->stop);
    const auto remaining = impl_->source_stop - impl_->source_first - impl_->report.generated_samples;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, output.size()));
    for (auto& value : output.first(count)) value = impl_->next(impl_->report.generated_samples);
    return count;
}

const EmissionReport& Emission::report() const { return impl_->report; }

} // namespace datapump::triad::v3

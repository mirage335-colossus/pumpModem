#include "datapump/triad_stream.hpp"

#include "datapump/triad_transmission_keys.hpp"
#include "triad_coordinate.hpp"

#include <algorithm>
#include <cmath>
#include <exception>
#include <functional>
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

std::uint64_t receive_coordinate(long double value, bool clamp_before_origin) {
    require(std::isfinite(value) && value < std::ldexp(1.L, 64) &&
        (clamp_before_origin || value >= 0), "public receive window overflows sample coordinates");
    return value > 0 ? static_cast<std::uint64_t>(value) : 0;
}

std::uint64_t packed_bit_samples(double seconds, std::uint32_t sample_rate) {
    const double rounded = seconds * sample_rate;
    require(std::isfinite(rounded) && rounded > 0 && rounded < std::ldexp(1., 64),
        "invalid continuous Robust DATA extent");
    const auto integral = std::ceil(rounded);
    const auto rows = static_cast<std::uint64_t>(integral);
    if (rounded != integral) return rows;
    // The binary64 product can round to an integer on either side of the
    // exact boundary. Its fused residual restores the outward-rounded sample
    // address without relying on the platform's long double precision.
    const auto correction = std::ceil(std::fma(seconds, static_cast<double>(sample_rate), -rounded));
    return correction < 0 ? rows - static_cast<std::uint64_t>(-correction) :
                            add(rows, static_cast<std::uint64_t>(correction));
}

HelperBurstConfig burst_config(const PublicProfileConfig& config, const Geometry& geometry) {
    require(config.format.sample_rate_denominator == 1,
        "whole-bit Robust stream requires an integer sample rate");
    const auto rounded = std::round(static_cast<long double>(geometry.bit_seconds) * config.format.sample_rate);
    require(rounded >= 1 && rounded < std::ldexp(1.L, 64), "whole-bit logical calendar extent overflow");
    // PatternSource admits integer samples strictly before bit_seconds. In
    // a packed stream the last such sample belongs to the preceding bit.
    const auto count = config.continuous_data ? packed_bit_samples(geometry.bit_seconds, config.format.sample_rate) :
                                                 static_cast<std::uint64_t>(rounded);
    return {config.format.sample_rate, count, geometry.source_samples,
        geometry.calendar_origin_sample, config.wait_bits, config.helper_enabled, 0,
        config.random_extra_bits, config.format.epoch, config.format.symbol_ordinal, config.continuous_data};
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

// Runs part(index, begin, end) over [0, count) in at most parts contiguous
// parts, on lanes when there are several. The first failing part's error is
// the one that a single pass in order would have met first.
void in_parts(std::size_t count, std::size_t parts, const StreamChannelLanes& lanes,
              const std::function<void(std::size_t, std::size_t, std::size_t)>& part) {
    parts = std::clamp<std::size_t>(std::min<std::size_t>(parts, count), 1, lanes.run ? std::max(1U, lanes.count) : 1);
    if (parts == 1) {
        part(0, 0, count);
        return;
    }
    std::vector<std::exception_ptr> errors(parts);
    lanes.run(parts, [&](std::size_t index) {
        try {
            part(index, count * index / parts, count * (index + 1) / parts);
        } catch (...) {
            errors[index] = std::current_exception();
        }
    });
    for (const auto& error : errors) if (error) std::rethrow_exception(error);
}

// A copy of the reference source per part beyond the first, bounded to 32 MiB.
std::size_t source_parts(const PatternSource& source, std::size_t wanted) {
    const auto bytes = std::max<std::size_t>(1, source.working_bytes());
    return std::clamp<std::size_t>(std::size_t{32} * 1024 * 1024 / bytes, 1, wanted);
}

DataPowerReference bounded_tile_power(PatternSource& source, std::stop_token stop,
                                      const StreamChannelLanes& lanes = {}, const Crypto* key = nullptr) {
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
    // Each tile's nodes sum in order on one part; tiles then sum in order.
    std::vector<long double> means(tiles);
    in_parts(tiles, key ? source_parts(source, tiles) : 1, lanes, [&](std::size_t part, std::size_t begin, std::size_t end) {
        std::unique_ptr<PatternSource> own;
        if (part) own = std::make_unique<PatternSource>(format, *key, 0);
        auto& reader = part ? *own : source;
        for (auto j = static_cast<unsigned>(begin); j < end; ++j) {
            const auto first = static_cast<std::uint64_t>(static_cast<long double>(geometry.tile_count) * j / tiles);
            const auto last = static_cast<std::uint64_t>(static_cast<long double>(geometry.tile_count) * (j + 1) / tiles);
            const auto tile = first + mix64(format.epoch ^ (0x54494c45504f57ULL + j)) % (last - first);
            long double energy = 0;
            for (unsigned i = 0; i < nodes_per_tile; ++i) {
                if ((i & 255) == 0) check_stop(stop);
                const auto random = mix64(format.epoch ^ tile ^ (0x4e4f4445504f57ULL + i));
                const auto fraction = static_cast<long double>(random >> 11) / 9007199254740992.L;
                const auto coordinate = active_tile * (i + fraction) / nodes_per_tile;
                long double within = coordinate;
                if (format.fhss) {
                    // Once per node: keep the x87 control word (triad_coordinate.hpp).
                    const auto dwell = detail::floor_coordinate(coordinate / active_dwell);
                    within = dwell * format.dwell_seconds + format.quiet_seconds + coordinate - dwell * active_dwell;
                }
                const auto seconds = tile * (static_cast<long double>(format.burst_wait_tiles) + 1) * geometry.tile_seconds + within;
                energy += std::norm(reader.sample_at(geometry.calendar_origin_sample + seconds * format.sample_rate));
            }
            means[j] = energy / nodes_per_tile;
        }
    });
    long double sum = 0, squares = 0;
    for (const auto mean : means) {
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

helper::PowerReference public_helper_reference(const Format& format) {
    return helper_reference(format, 42);
}

PublicProfile::PublicProfile(PublicProfileConfig config, std::optional<Crypto> calendar_key)
    : config_(std::move(config)), geometry_(v3::geometry(config_.format)), calendar_(burst_config(config_, geometry_), std::move(calendar_key)),
      placement_{config_.data_if_hz, geometry_.occupied_bandwidth_hz, config_.gate_bandwidth_hz, config_.format.fhss} {
    require(config_.format.epoch >= 6 && config_.format.epoch <= last_pattern_epoch,
        "Robust calendar epoch must support the thirteen public epoch alternatives");
    require(std::isfinite(config_.data_if_hz) && config_.data_if_hz > 0 && config_.data_if_hz < effective_sample_rate() / 2,
        "Robust DATA IF must be positive and below Nyquist");
    // A public stream without an explicit RF coordinate uses its configured
    // audio carrier. Freeze this once; slot addresses and hops never retune it.
    if (config_.format.configured_carrier_hz == 0)
        config_.format.configured_carrier_hz = config_.data_if_hz;
    for (const auto value : {config_.capture_lead_seconds, config_.capture_trail_seconds, config_.audio_pair_ppm,
            config_.clock_bound_ppm, config_.cfo_bound_hz, config_.spread_delay_ms, config_.spread_doppler_hz,
            config_.signal_delay_ms.value_or(0), config_.delay_excursion_seconds, config_.gate_bandwidth_hz})
        require(std::isfinite(value) && value >= 0, "invalid Robust public search or channel bounds");
    require(config_.audio_pair_ppm < 2e6 && config_.clock_bound_ppm < 1e6 &&
        std::isfinite(config_.arrival_low_seconds) &&
        std::isfinite(config_.arrival_high_seconds) && config_.arrival_low_seconds <= config_.arrival_high_seconds &&
        config_.signal_delay_ms.value_or(0) <= config_.spread_delay_ms,
        "invalid Robust public arrival, clock, or delay range");
    nominal_endpoint_ = config_.audio_pair_ppm * .5e-6L;
    const auto clock = config_.clock_bound_ppm * 1e-6;
    affine_min_scale_ = 1 - clock;
    affine_max_scale_ = 1 + clock;
    require(affine_min_scale_ > 0, "invalid Robust affine clock scale");
    // The affine scorer uses 1/(1 +/- clock). For independently bounded
    // endpoints, (1 + endpoint)/(1 - endpoint) reaches 1/(1 - clock)
    // at clock/(2 - clock). That endpoint also covers the earlier edge.
    const long double slow = affine_min_scale_;
    receive_endpoint_ = std::max(nominal_endpoint_, (1 - slow) / (1 + slow));
    if (!config_.lead_samples)
        config_.lead_samples = config_.helper_enabled ? std::uint64_t{helper_lead_seconds} * config_.format.sample_rate : 0;
    const auto first = transmit_slot(0);
    require(first.helper_begin >= origin(), "Robust calendar anchor must precede the first Helper sample");
    helper::validate(config_.helper);
    if (config_.helper_enabled) {
        // Use the same public active reference window as the emitted source.
        // The placement guard is independent of its keyed auxiliary symbols.
        const auto reference = public_helper_reference(config_.format);
        helper_plan_ = helper::plan(config_.helper, placement_, config_.format.sample_rate, 0, 1, &reference);
    }
    maximum_slot_opportunities_per_day_ = bound_slot_opportunities_per_day();
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
    const auto latest_pattern = HelperBurstCoordinate{last_pattern_epoch, config_.format.sample_rate - 1};
    auto high = std::min(calendar_.distance(first.pattern_begin, latest_pattern) / calendar_.minimum_stride_samples(),
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
    return capture_stop(source_end_seconds, true);
}

long double PublicProfile::capture_end_offset(long double rate) const {
    const auto tail = config_.spread_delay_ms * .001 + config_.delay_excursion_seconds +
        2 / (3 * geometry_.nominal_carrier_bandwidth_hz) + 32. / rate;
    return static_cast<long double>(config_.capture_lead_seconds) + config_.arrival_high_seconds +
        config_.capture_trail_seconds + tail;
}

std::uint32_t PublicProfile::bound_slot_opportunities_per_day() const {
    // Endpoints translate by at least the minimum stride. The ready time is
    // the maximum of two fixed affine maps of that clock, both with slope
    // at least one, followed by upward rounding and an additive sample tail.
    // Bound the loss from floating evaluation and rounding between the two
    // endpoints of a whole day. Fixed sample tails cancel in the difference.
    const long double rate = effective_sample_rate();
    const auto source_span = static_cast<long double>(helper_epoch_limit - origin().epoch);
    const auto shortest_scale = std::min(1 - receive_endpoint_, static_cast<long double>(affine_min_scale_));
    const auto magnitude = 2 * rate * ((source_span + 2 / rate) / shortest_scale + std::abs(capture_end_offset(rate)));
    const auto roundoff_rows = std::ceil(1 + 32 * std::numeric_limits<long double>::epsilon() * magnitude);
    try {
        const auto stride_rows = calendar_.minimum_stride_samples();
        const auto count = std::floor((86'400 * rate + roundoff_rows) / stride_rows) + 1;
        if (std::isfinite(count) && count >= 1 && count <= UINT32_MAX)
            return static_cast<std::uint32_t>(count);
    } catch (const Error&) {
        // A calendar outside the numerical proof keeps the reference gate.
    }
    return 0;
}

std::uint64_t PublicProfile::capture_stop(long double source_end_seconds, bool search) const {
    const long double rate = effective_sample_rate();
    const auto endpoint = search ? receive_endpoint_ : nominal_endpoint_;
    const auto tx_min = 1 - endpoint, rx_max = 1 + endpoint;
    const auto offset = capture_end_offset(rate);
    auto end = rx_max * (source_end_seconds / tx_min + offset);
    if (search) {
        // The coarse source includes two rows past the finite support. Keep
        // signed lead and arrival offsets additive in its affine coordinates.
        end = std::max(end, (source_end_seconds + 2 / rate) / affine_min_scale_ + offset);
    }
    return add(add(receive_coordinate(std::ceil(rate * end), false), config_.receiver_tail_samples),
        config_.receiver_filter_halo_samples);
}

std::uint64_t PublicProfile::capture_first(long double source_begin_seconds, bool search) const {
    const long double rate = effective_sample_rate();
    const auto endpoint = search ? receive_endpoint_ : nominal_endpoint_;
    const auto tx_max = 1 + endpoint, rx_min = 1 - endpoint;
    auto begin = rx_min * (source_begin_seconds / tx_max + config_.arrival_low_seconds);
    if (search) {
        const auto edge = source_begin_seconds - 2 / rate;
        const auto scale = edge < 0 ? affine_min_scale_ : affine_max_scale_;
        begin = std::min(begin, edge / scale + config_.capture_lead_seconds + config_.arrival_low_seconds);
    }
    return receive_coordinate(std::floor(rate * begin), true);
}

std::uint64_t PublicProfile::signal_first(long double source_begin_seconds, bool search) const {
    const long double rate = effective_sample_rate();
    const auto endpoint = search ? receive_endpoint_ : nominal_endpoint_;
    const auto tx_max = 1 + endpoint, tx_min = 1 - endpoint;
    const auto offset = static_cast<long double>(config_.capture_lead_seconds) + config_.arrival_low_seconds;
    auto begin = tx_min * (source_begin_seconds / tx_max + offset);
    if (search) begin = std::min(begin, source_begin_seconds / affine_max_scale_ + offset);
    return receive_coordinate(std::floor(rate * begin - 32), true);
}

std::uint64_t PublicProfile::signal_stop(long double source_end_seconds, bool search) const {
    const long double rate = effective_sample_rate();
    const auto endpoint = search ? receive_endpoint_ : nominal_endpoint_;
    const auto tx_min = 1 - endpoint, rx_max = 1 + endpoint;
    const auto tail = config_.signal_delay_ms.value_or(config_.spread_delay_ms) * .001 + config_.delay_excursion_seconds +
        (config_.selective_signal_tail ? 2 / (3 * geometry_.nominal_carrier_bandwidth_hz) : 0.) + 32. / rate;
    const auto offset = static_cast<long double>(config_.capture_lead_seconds) + config_.arrival_high_seconds + tail;
    auto end = rx_max * (source_end_seconds / tx_min + offset);
    if (search) end = std::max(end, source_end_seconds / affine_min_scale_ + offset);
    return receive_coordinate(std::ceil(rate * end), false);
}

PublicSlot PublicProfile::slot(std::uint64_t index) const {
    const auto tx = transmit_slot(index);
    const auto next = calendar_.successor(tx);
    const auto seconds = [&](HelperBurstCoordinate value) { return calendar_.relative_seconds(origin(), value); };
    const auto halo = config_.receiver_filter_halo_samples;
    const auto earlier = [halo](std::uint64_t sample) { return sample - std::min(sample, halo); };
    const auto begin = seconds(tx.helper_begin), end = seconds(tx.helper_end);
    const SampleRange observation{earlier(capture_first(begin, true)), capture_stop(end)};
    const SampleRange nominal_observation{capture_first(begin, false), capture_stop(end, false) - halo};
    require(observation.stop > observation.first && nominal_observation.stop > nominal_observation.first,
        "public receive window is empty");
    // Packed DATA has zero envelope in the reference halos. Only the
    // active gates, their channel tails and the receive filter can intrude
    // on a neighboring quiet aperture. Retain both padded observation edges.
    const auto data_begin = seconds(config_.continuous_data ? tx.pattern_begin : tx.support_begin);
    const auto data_end = seconds(config_.continuous_data ? tx.pattern_end : tx.support_end);
    const SampleRange data{signal_first(data_begin, false), signal_stop(data_end, false)};
    const auto previous_end = [&](const HelperBurstSlot& previous) {
        return config_.continuous_data ? previous.pattern_end : previous.helper_end;
    };
    const auto previous_stop = index ? add(signal_stop(seconds(previous_end(transmit_slot(index - 1))), true), halo) : 0;
    const auto next_first = earlier(signal_first(seconds(config_.continuous_data ? next.pattern_begin : next.helper_begin), true));
    // Larger clock support and the filter halo cannot extend the quiet
    // aperture. Their adjacent signal exclusions can only narrow it.
    const auto quiet_first = std::min(nominal_observation.stop,
        std::max({nominal_observation.first, data.first, previous_stop}));
    const auto quiet_stop = std::max(quiet_first, std::min({nominal_observation.stop, data.stop, next_first}));
    SampleRange protected_data;
    if (config_.continuous_data) {
        // Keep the nominal quiet aperture and the reachable DATA in the
        // affine clock and filter margins. Exclude both neighboring signals.
        const auto wanted_first = earlier(signal_first(data_begin, true));
        const auto wanted_stop = add(signal_stop(data_end, true), halo);
        const auto first = std::clamp(std::max(previous_stop, wanted_first), observation.first, observation.stop);
        const bool next_is_public = index < max_coordinate - config_.format.symbol_ordinal &&
            next.pattern_begin.epoch <= last_pattern_epoch && next.helper_end.epoch < helper_epoch_limit;
        const auto stop = std::clamp(next_is_public ? std::min(next_first, wanted_stop) : wanted_stop,
            first, observation.stop);
        protected_data = {first, stop};
    }
    return {index, tx.pattern_begin.epoch, config_.format.symbol_ordinal + index, tx,
        observation, data, {quiet_first, quiet_stop}, previous_stop, next_first, protected_data};
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
    std::function<std::uint8_t(std::uint64_t)> bit_at;
    std::stop_token stop;
    std::unique_ptr<PatternSource> data;
    std::unique_ptr<helper::Generator> helper;
    std::optional<HelperBurstSlot> current;
    std::uint64_t first_slot = 0, last_slot = 0, source_first = 0, source_stop = 0, data_first = 0;
    std::uint64_t active_helper_first = 0, active_slot_stop = 0;
    std::uint64_t data_begin_offset = 0, data_end_offset = 0;
    long double active_helper_begin_seconds = 0, active_helper_end_seconds = 0;
    bool data_enabled = false, helper_enabled = false;
    bool noise = false, packed = false;
    std::uint8_t isolated_bit = 0;
    // The bit of the current slot's DATA source once it is resolved.
    std::optional<std::uint8_t> slot_bit;

    // Readers for a channel with lanes. A plan holds the slot states that
    // next() would pass through; each reader owns its DATA source and Helper
    // generator, whose values depend on the sample index alone.
    struct Span {
        std::uint64_t begin = 0; // First local sample of this slot state.
        std::optional<HelperBurstSlot> slot;
        std::uint64_t data_first = 0, slot_stop = 0;
        long double helper_begin_seconds = 0, helper_end_seconds = 0;
        std::optional<std::uint8_t> bit;
    };
    struct Reader {
        std::uint64_t data_slot = 0;
        std::unique_ptr<PatternSource> data;
        std::unique_ptr<helper::Generator> helper;
    };
    std::vector<Reader> readers;
    std::vector<Span> spans;
    std::vector<std::array<double, 2>> norms;
    std::uint64_t plan_first = 0, plan_count = 0, plan_error_index = UINT64_MAX;
    std::exception_ptr plan_error;

    Impl(const PublicProfile& selected_profile, const Crypto& key, const EmissionOptions& options,
         std::optional<std::pair<std::uint8_t, bool>> isolated = {})
        : profile(selected_profile), format(profile.config().format),
          keys(resolve_transmission_keys(key, format.encrypt,
              options.mode == EmissionMode::Noise ? TransmissionMode::Noise : TransmissionMode::Message)),
          bit_at(options.bit_at), stop(options.stop), first_slot(options.first_slot),
          source_first(options.source_first_sample) {
        check_stop(stop);
        format.encrypt = keys.encrypt_data;
        noise = options.mode == EmissionMode::Noise;
        data_enabled = isolated ? isolated->second : options.mode == EmissionMode::Message || noise;
        helper_enabled = profile.config().helper_enabled && options.mode != EmissionMode::Quiet;
        if (options.mode == EmissionMode::HelperOnly)
            require(helper_enabled && profile.helper_plan().effective_power_w > 0, "Helper-only transmission requires enabled Helper power");
        const auto rate = format.sample_rate;
        const auto& geometry = profile.geometry();
        packed = profile.config().continuous_data && !isolated;
        data_end_offset = geometry.source_samples;
        if (packed) {
            data_begin_offset = geometry.calendar_origin_sample;
            data_end_offset = data_begin_offset + profile.calendar().config().bit_samples;
        }
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
            active_slot_stop = source_stop;
            active_helper_end_seconds = static_cast<long double>(source_stop) / rate;
            require(!helper_enabled || static_cast<long double>(format.epoch) + std::ceil(active_helper_end_seconds) < helper_epoch_limit,
                "source endpoint exceeds Helper32-bit epoch calendar");
        } else {
            const auto count = options.mode == EmissionMode::Message && !bit_at ? options.bits.size() : options.slot_count;
            require(count > 0 && (options.mode == EmissionMode::Message || (options.bits.empty() && !bit_at)) &&
                (!bit_at || options.bits.empty()),
                "invalid Robust transmission slot count or unexpected draft");
            // Only the materialized convenience input makes a per-bit copy.
            require(options.bits.size() <= 65536 && std::all_of(options.bits.begin(), options.bits.end(),
                [](std::uint8_t bit) { return bit <= 1; }), "invalid or oversized Robust transmit draft");
            bits.assign(options.bits.begin(), options.bits.end());
            last_slot = add(options.first_slot, count - 1);
            source_stop = profile.calendar().distance(profile.origin(), profile.transmit_slot(last_slot).helper_end);
            set_slot(profile.transmit_slot(options.first_slot));
            require(source_first <= active_helper_first, "Robust playback starts after the first transmitted sample");
        }
        report.first_data_sample = data_first - source_first;
        prepare_power(options.lanes);
    }

    std::uint64_t slot_stop(const HelperBurstSlot& slot) const {
        return profile.calendar().distance(profile.origin(), packed ? slot.pattern_end : slot.helper_end);
    }

    void set_slot(const HelperBurstSlot& slot) {
        current = slot;
        data.reset();
        slot_bit.reset();
        const auto& calendar = profile.calendar();
        const auto origin = profile.origin();
        data_first = calendar.distance(origin, slot.support_begin);
        active_helper_first = calendar.distance(origin, slot.helper_begin);
        active_slot_stop = slot_stop(slot);
        active_helper_begin_seconds = calendar.relative_seconds(origin, slot.helper_begin);
        active_helper_end_seconds = calendar.relative_seconds(origin, slot.helper_end);
    }

    void prepare_power(const StreamChannelLanes& lanes) {
        const auto& geometry = profile.geometry();
        const auto rate = format.sample_rate;
        PatternSource reference_source(format, keys.data, 0);
        const auto preview_offset = static_cast<long double>(helper_lead_seconds) * rate - geometry.calendar_origin_sample;
        const auto helper_end = static_cast<long double>(source_stop) / rate;
        auto reference = helper_reference(format, current ? std::max(helper_end, 42.L) : helper_end);
        reference.time_key = keys.helper;
        // The active reference nodes in order; each part evaluates its own.
        std::vector<long double> active;
        active.reserve(helper::power_reference_nodes);
        for (unsigned i = 0; i < helper::power_reference_nodes; ++i) {
            if ((i & 255) == 0) check_stop(stop);
            const auto seconds = helper::power_reference_node(reference, i);
            if (reference.active(seconds)) active.push_back(seconds);
        }
        std::vector<double> values(active.size());
        const auto parts = source_parts(reference_source, 16);
        in_parts(active.size(), parts, lanes, [&](std::size_t part, std::size_t begin, std::size_t end) {
            std::unique_ptr<PatternSource> own;
            if (part) own = std::make_unique<PatternSource>(format, keys.data, 0);
            auto& source = part ? *own : reference_source;
            for (auto k = begin; k < end; ++k) {
                if ((k & 255) == 0) check_stop(stop);
                values[k] = std::norm(source.sample_at(active[k] * rate - preview_offset));
            }
        });
        long double norm = 0;
        for (const auto value : values) norm += value;
        const auto nodes = static_cast<unsigned>(active.size());
        require(nodes && norm > 0, "empty DATA power reference");
        report.reference_data_mean_norm = static_cast<double>(norm / nodes);
        reference.data_mean_power = report.reference_data_mean_norm / 2;
        if (helper_enabled)
            report.helper_plan = helper::plan(profile.config().helper, profile.placement(), rate, 0, 1, &reference);
        report.data_reference = bounded_tile_power(reference_source, stop, lanes, &keys.data);
        report.data_calibration_gain = std::sqrt(report.reference_data_mean_norm / report.data_reference.mean_norm);
        const auto data_gain = report.helper_plan.data_gain;
        report.composite_gain = helper_enabled ? 1 / (data_gain * std::sqrt(report.reference_data_mean_norm)) : 1.;
        report.data_gain = helper_enabled ? data_gain * report.composite_gain * report.data_calibration_gain :
            1 / std::sqrt(report.data_reference.mean_norm);
        if (!helper_enabled) return;
        helper = std::make_unique<helper::Generator>(profile.config().helper, report.helper_plan, format.epoch, true, keys.helper);
        in_parts(active.size(), 16, lanes, [&](std::size_t part, std::size_t begin, std::size_t end) {
            std::unique_ptr<helper::Generator> own;
            if (part) own = std::make_unique<helper::Generator>(profile.config().helper, report.helper_plan,
                format.epoch, true, keys.helper);
            const auto& generator = part ? *own : *helper;
            for (auto k = begin; k < end; ++k) {
                if ((k & 255) == 0) check_stop(stop);
                values[k] = std::norm(generator.mix_analytic({}, active[k], rate,
                    reference.helper_end_seconds, 0) * report.composite_gain);
            }
        });
        long double measured_helper_norm = 0;
        for (const auto value : values) measured_helper_norm += value;
        report.reference_helper_ratio = static_cast<double>(measured_helper_norm / nodes);
    }

    // The bit that next() addresses the DATA source of a slot with.
    std::uint8_t slot_data_bit(const std::optional<HelperBurstSlot>& slot) const {
        auto bit = isolated_bit;
        if (slot && !noise) {
            const auto relative = slot->index-first_slot;
            bit = bit_at ? bit_at(relative) : bits[static_cast<std::size_t>(relative)];
            require(bit <= 1, "invalid Robust generated transmit bit");
        }
        return bit;
    }
    std::unique_ptr<PatternSource> data_source(const std::optional<HelperBurstSlot>& slot, std::uint8_t bit) const {
        auto addressed_format = format;
        if (slot) {
            addressed_format.epoch = slot->pattern_begin.epoch;
            addressed_format.symbol_ordinal += slot->index;
        }
        return std::make_unique<PatternSource>(addressed_format, keys.data, bit);
    }
    bool in_data(std::uint64_t index, std::uint64_t first) const {
        return data_enabled && index >= first && index - first >= data_begin_offset &&
            index - first < data_end_offset;
    }

    struct Value {
        Complex sample;
        double data_norm = 0, helper_norm = 0;
    };
    // One sample of a slot state. data is that slot's DATA source when the
    // sample lies in its support.
    Value value_at(std::uint64_t index, PatternSource* source, std::uint64_t first,
                   long double helper_begin, long double helper_end, const helper::Generator* generator) const {
        Complex raw_data;
        if (source) raw_data = source->sample_at(static_cast<long double>(index - first));
        const auto seconds = static_cast<long double>(index) / format.sample_rate;
        const auto up = rotation(profile.data_if_hz(), seconds);
        const auto normalized_data = raw_data * report.data_gain;
        const bool active = generator && seconds >= helper_begin && seconds < helper_end;
        const auto audio = active ? generator->mix_analytic(raw_data * report.data_calibration_gain * up,
            seconds, format.sample_rate, helper_end, helper_begin) * report.composite_gain :
            normalized_data * up;
        const auto helper_component = active ? audio - normalized_data * up : Complex{};
        return {audio * std::conj(up), std::norm(normalized_data), std::norm(helper_component)};
    }

    Complex next(std::uint64_t local_index) {
        require(local_index == report.generated_samples && local_index < source_stop - source_first,
            "Robust emitted source requires one forward reader");
        if ((local_index & 255) == 0) check_stop(stop);
        const auto index = source_first + local_index;
        if (current && current->index < last_slot && index >= active_slot_stop)
            set_slot(profile.calendar().successor(*current));
        PatternSource* source = nullptr;
        if (in_data(index, data_first)) {
            if (!data) {
                const auto bit = slot_bit ? *slot_bit : slot_data_bit(current);
                data = data_source(current, bit);
                slot_bit = bit;
            }
            source = data.get();
        }
        const auto value = value_at(index, source, data_first, active_helper_begin_seconds,
            active_helper_end_seconds, helper.get());
        report.data_energy += value.data_norm;
        report.helper_energy += value.helper_norm;
        ++report.generated_samples;
        return value.sample;
    }

    std::size_t span_capacity(std::uint64_t plan) const {
        if (!packed) return 16;
        const auto interior_boundaries = plan / profile.calendar().config().bit_samples;
        require(interior_boundaries <= std::numeric_limits<std::size_t>::max() / sizeof(Span) - 2,
            "Robust emitted span bound overflow");
        return static_cast<std::size_t>(interior_boundaries) + 2;
    }

    std::size_t reader_bytes(std::size_t count, std::uint64_t plan) const {
        return count * (sizeof(Reader) + (data_enabled ? profile.geometry().source_workspace_bytes : 0) +
            (helper ? sizeof(helper::Generator) : 0)) + static_cast<std::size_t>(plan) * sizeof(norms[0]) +
            span_capacity(plan) * sizeof(Span);
    }
    void open(std::size_t count, std::uint64_t plan) {
        readers.resize(count);
        norms.resize(static_cast<std::size_t>(plan));
        spans.reserve(span_capacity(plan));
    }
    Span current_span(std::uint64_t begin) const {
        return {begin, current, data_first, active_slot_stop, active_helper_begin_seconds,
            active_helper_end_seconds, slot_bit};
    }
    Span successor_span(const Span& span, std::uint64_t begin) const {
        const auto slot = profile.calendar().successor(*span.slot);
        const auto& calendar = profile.calendar();
        const auto origin = profile.origin();
        return {begin, slot, calendar.distance(origin, slot.support_begin), slot_stop(slot),
            calendar.relative_seconds(origin, slot.helper_begin), calendar.relative_seconds(origin, slot.helper_end), {}};
    }
    // The slot states of local samples [first, first + count) and the bits
    // of their DATA sources, resolved in order as next() resolves them.
    void plan(std::uint64_t first, std::uint64_t count) {
        require(first == report.generated_samples && count <= source_stop - source_first - first,
            "Robust emitted source requires one forward reader");
        require(count <= norms.size(), "Robust emitted source plan exceeds its admitted sample count");
        plan_first = first;
        plan_count = count;
        plan_error_index = UINT64_MAX;
        plan_error = nullptr;
        spans.clear();
        const auto end = first + count;
        auto span = current_span(first);
        for (auto earliest = first;;) {
            // next() moves to the following slot at most once per sample.
            auto stop = end;
            if (span.slot && span.slot->index < last_slot)
                stop = std::min(end, std::max(earliest, span.slot_stop > source_first ? span.slot_stop - source_first : 0));
            if (!span.bit && data_enabled) {
                const auto lo = std::max(source_first + span.begin, span.data_first + data_begin_offset);
                const auto hi = std::min(source_first + stop, span.data_first + data_end_offset);
                if (lo < hi) {
                    try {
                        span.bit = slot_data_bit(span.slot);
                    } catch (...) {
                        plan_error_index = lo - source_first;
                        plan_error = std::current_exception();
                        spans.push_back(span);
                        return;
                    }
                }
            }
            spans.push_back(span);
            if (stop == end) return;
            try {
                span = successor_span(span, stop);
            } catch (...) {
                plan_error_index = stop;
                plan_error = std::current_exception();
                return;
            }
            earliest = stop + 1;
        }
    }
    Complex evaluate(std::size_t which, std::uint64_t local) {
        if (local >= plan_error_index) std::rethrow_exception(plan_error);
        const auto& span = *std::prev(std::upper_bound(spans.begin(), spans.end(), local,
            [](std::uint64_t value, const Span& candidate) { return value < candidate.begin; }));
        auto& reader = readers[which];
        const auto index = source_first + local;
        PatternSource* source = nullptr;
        if (in_data(index, span.data_first)) {
            const auto slot = span.slot ? span.slot->index : 0;
            if (!reader.data || reader.data_slot != slot) {
                reader.data.reset();
                reader.data = data_source(span.slot, *span.bit);
                reader.data_slot = slot;
            }
            source = reader.data.get();
        }
        if (helper && !reader.helper)
            reader.helper = std::make_unique<helper::Generator>(profile.config().helper, report.helper_plan,
                format.epoch, true, keys.helper);
        const auto value = value_at(index, source, span.data_first, span.helper_begin_seconds,
            span.helper_end_seconds, reader.helper.get());
        norms[static_cast<std::size_t>(local - plan_first)] = {value.data_norm, value.helper_norm};
        return value.sample;
    }
    // next()'s order: its stop check, then the slot state of the sample.
    bool reach(std::uint64_t local, std::size_t& span, std::exception_ptr& error) {
        if ((local & 255) == 0) {
            try {
                check_stop(stop);
            } catch (...) {
                error = std::current_exception();
                return false;
            }
        }
        if (span + 1 < spans.size() && spans[span + 1].begin == local) {
            const auto& next_span = spans[++span];
            current = next_span.slot;
            data.reset();
            data_first = next_span.data_first;
            active_helper_first = profile.calendar().distance(profile.origin(), next_span.slot->helper_begin);
            active_slot_stop = next_span.slot_stop;
            active_helper_begin_seconds = next_span.helper_begin_seconds;
            active_helper_end_seconds = next_span.helper_end_seconds;
            slot_bit.reset();
        }
        return true;
    }
    std::uint64_t accept(std::uint64_t count, bool failed, std::exception_ptr& error) {
        require(count <= plan_count, "Robust emitted source accepted unplanned samples");
        std::size_t span = 0;
        std::uint64_t accepted = 0;
        for (; accepted < count; ++accepted) {
            const auto local = plan_first + accepted;
            if (!reach(local, span, error)) break;
            if (in_data(source_first + local, data_first)) slot_bit = spans[span].bit;
            const auto& value = norms[static_cast<std::size_t>(accepted)];
            report.data_energy += value[0];
            report.helper_energy += value[1];
            ++report.generated_samples;
        }
        if (accepted == count && failed && count < plan_count) (void)reach(plan_first + count, span, error);
        plan_first += accepted;
        plan_count = 0;
        return accepted;
    }
};

struct Emission::Readers final : EmittedSourceReaders {
    explicit Readers(Impl& state) : state(state) {}
    std::size_t reader_bytes(std::size_t count, std::uint64_t plan) const override {
        return state.reader_bytes(count, plan);
    }
    void open(std::size_t count, std::uint64_t plan) override { state.open(count, plan); }
    void plan(std::uint64_t first, std::uint64_t count) override { state.plan(first, count); }
    Complex evaluate(std::size_t reader, std::uint64_t index) override { return state.evaluate(reader, index); }
    std::uint64_t accept(std::uint64_t count, bool failed, std::exception_ptr& error) override {
        return state.accept(count, failed, error);
    }
    Impl& state;
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
    result.readers = std::make_shared<Readers>(*state);
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

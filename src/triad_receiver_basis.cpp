#include "triad_receiver_basis.hpp"
#include "triad_coordinate.hpp"
#include "triad_mapped_kernel.hpp"
#include "datapump/triad_shaping.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <mutex>
#include <shared_mutex>
#include <numbers>
#include <numeric>
#include <tuple>

namespace datapump::triad::v3::receiver_detail {
namespace {
constexpr std::size_t cache_limit = 32U * 1024U * 1024U;
constexpr std::size_t tile_samples = 8192;
constexpr std::size_t tile_bytes = tile_samples * sizeof(Complex);
constexpr std::size_t maximum_pins = 32;
constexpr std::size_t maximum_samples = 16384;
constexpr std::size_t maximum_taps = 2;
constexpr std::size_t owner_count = receiver_epoch_offsets.size();

void require(bool condition, const char* message) {
    if (!condition) throw Error(message);
}
std::size_t add(std::size_t a, std::size_t b) {
    require(b <= SIZE_MAX-a, "mapped basis workspace overflow");
    return a+b;
}
std::size_t multiply(std::size_t a, std::size_t b) {
    require(!a || b <= SIZE_MAX/a, "mapped basis workspace overflow");
    return a*b;
}
void cancelled(const std::stop_token& stop) {
    if (stop.stop_requested()) throw Error("Robust receive cancelled");
}
// The phase of `value` cycles in [0, 1]: std::fmod(value, 1.L), plus one
// when negative, without the x87 remainder loop.
long double unit_cycle(long double value) {
    const auto result = detail::unit_remainder(value);
    return result < 0 ? result+1.L : result;
}
void validate_shape(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                    std::size_t rows, unsigned workers) {
    require(rows && rows <= maximum_samples && workers,
            "mapped basis requires bounded rows and admitted workers");
    require(std::isfinite(config.sample_rate_hz) && config.sample_rate_hz > 0 &&
            std::isfinite(config.data_if_hz) && config.data_if_hz >= 0 && config.data_if_hz <= 120e6 &&
            std::isfinite(config.data_basis_gain) && config.data_basis_gain >= 0,
            "invalid mapped basis rate, center or gain");
    require(slot.format.epoch >= 6 && slot.format.epoch <= UINT64_MAX-6,
            "mapped basis epoch cannot cover public alternatives");
}
} // namespace

struct MappedBasis::Impl {
    using Key = std::tuple<unsigned, unsigned, std::uint64_t>;
    using Tile = std::array<Complex, tile_samples>;
    // Lookups hold the cache lock shared, so a lane preempted inside one
    // cannot stall the others. Recency is a stamp instead of a list splice.
    struct Entry {
        Entry(std::shared_ptr<const Tile> tile, std::uint64_t stamp) : data(std::move(tile)), used(stamp) {}
        std::shared_ptr<const Tile> data;
        std::atomic<std::uint64_t> used;
        // Filled ahead of its first request, which then accounts as the fill.
        std::atomic<bool> ahead{false};
    };
    struct Sources {
        std::mutex mutex;
        std::array<std::unique_ptr<PatternSource>, 2> bits;
    };

    ReceiverConfig config;
    ReceiverSlotPlan slot;
    Crypto key;
    Geometry source_geometry;
    std::size_t maximum_rows;
    unsigned workers;
    double source_rate;
    std::uint64_t dense_samples;
    unsigned dense_factor = 1;
    std::array<Sources, owner_count> sources;
    mutable std::shared_mutex mutex;
    std::map<Key, Entry> cache;
    MappedBasisReport statistics;
    // Per-lookup and per-render receipts that need no exclusive lock.
    std::atomic<std::uint64_t> recency{0}, cache_hits{0}, cache_misses{0};
    std::atomic<std::uint64_t> calls{0}, interpolated_rows{0};
    std::atomic<std::size_t> peak_pinned_tiles{0};
    std::atomic<unsigned> active{0};
    std::atomic<std::uint64_t> addressed_calls{0}, addressed_columns{0}, preparation_nanoseconds{0};
    // Built on first request, for a consumer that renders the dense tiles.
    struct TileDescription {
        std::vector<double> pulse;
        FineTileSource source;
    };
    std::unique_ptr<TileDescription> tile_description;

    Impl(const ReceiverConfig& value, const ReceiverSlotPlan& plan, const Crypto& selected_key,
         std::size_t rows, unsigned count)
        : config(value), slot(plan), key(selected_key), source_geometry(geometry(plan.format)),
          maximum_rows(rows), workers(count), source_rate(effective_sample_rate(plan.format)),
          dense_samples(0) {
        validate_shape(config, slot, rows, count);
        if (slot.format.pulse_shape != PulseShape::TaperedRrc)
            statistics.fallback_reason = "unsupported-or-abrupt-pulse";
        double width = source_geometry.occupied_bandwidth_hz;
        if (slot.format.fhss) {
            if (!std::isfinite(slot.format.ramp_seconds) || slot.format.ramp_seconds <= 0)
                statistics.fallback_reason = "invalid-gate-description";
            else width += 2/slot.format.ramp_seconds;
        }
        if (!std::isfinite(width) || width <= 0) {
            statistics.fallback_reason = "missing-spectral-description";
            width = config.sample_rate_hz;
        }
        const double wanted = std::ceil(8*width/config.sample_rate_hz);
        if (!std::isfinite(wanted) || wanted > 4) {
            statistics.fallback_reason = "insufficient-cache-oversampling";
            // No dense allocation occurs on fallback. Keep this bounded value
            // solely so reports and source extent remain representable.
            dense_factor = 4;
        } else dense_factor = static_cast<unsigned>(std::max(1., wanted));
        const auto extent = std::ceil(static_cast<long double>(source_geometry.source_samples) *
            config.sample_rate_hz/source_rate*dense_factor);
        require(std::isfinite(extent) && extent > 0 && extent < static_cast<long double>(UINT64_MAX),
                "mapped basis dense extent outside uint64 range");
        dense_samples = static_cast<std::uint64_t>(extent);
        statistics.tile_samples = tile_samples;
        statistics.cache_limit_bytes = cache_limit;
        statistics.dense_factor = dense_factor;
        statistics.dense_sample_rate_hz = config.sample_rate_hz * dense_factor;
        statistics.oversampling_ratio = config.sample_rate_hz*dense_factor/width;
        statistics.interpolation_amplitude_proxy = (3./128)*
            std::pow(std::numbers::pi/statistics.oversampling_ratio, 4);
    }

    // Source samples per dense sample.
    long double dense_step() const {
        return static_cast<long double>(source_rate)/(config.sample_rate_hz*dense_factor);
    }

    // One lane's keyed coefficients, as the native renderer reads them.
    void key_lane(std::size_t lane, std::span<Complex> chips, const std::stop_token& stop) {
        const auto cells = static_cast<std::size_t>(source_geometry.fine_cells);
        require(lane < 2*owner_count && chips.size() == 3*cells, "mapped basis keyed lane extent mismatch");
        const auto epoch = static_cast<unsigned>(lane/2);
        {
            std::lock_guard source_lock(sources[epoch].mutex);
            cancelled(stop);
            prepare_sources(epoch);
            auto& source = *sources[epoch].bits[lane%2];
            for (std::size_t cell = 0; cell != cells; ++cell) {
                if (!(cell%4096)) cancelled(stop);
                const auto chip = source.pattern_coefficient_at(cell);
                std::copy(chip.begin(), chip.end(), chips.begin()+static_cast<std::ptrdiff_t>(3*cell));
            }
        }
        std::lock_guard lock(mutex);
        ++statistics.keyed_lanes;
    }

    const FineTileSource* describe() {
        if (!statistics.fallback_reason.empty() || source_geometry.fine_cells > SIZE_MAX/(3*sizeof(Complex)))
            return nullptr;
        std::lock_guard lock(mutex);
        if (tile_description) return &tile_description->source;
        auto description = std::make_unique<TileDescription>();
        constexpr std::size_t resolution = 256, span = 8;
        description->pulse.resize(span*resolution+1);
        for (std::size_t i = 0; i != description->pulse.size(); ++i)
            description->pulse[i] = shaping_pulse(slot.format.pulse_shape, static_cast<double>(i)/resolution);
        const auto& format = slot.format;
        auto& source = description->source;
        source.sample_step = static_cast<double>(dense_step());
        source.gain = config.data_basis_gain;
        source.calendar_origin_sample = static_cast<double>(source_geometry.calendar_origin_sample);
        source.source_samples = static_cast<double>(source_geometry.source_samples);
        source.sample_rate_hz = source_rate;
        source.fine_chip_rate_hz = source_geometry.fine_chip_rate_hz;
        source.center_offset = format.fhss_center_aligned ? 0. : .5;
        source.carrier_spacing_hz = source_geometry.carrier_spacing_hz;
        source.bit_seconds = source_geometry.bit_seconds;
        source.envelope_edge_seconds = std::max(format.ramp_seconds, 8/source_geometry.fine_chip_rate_hz);
        if (format.burst_wait_tiles) {
            source.burst_period_tiles = static_cast<double>(format.burst_wait_tiles)+1;
            source.tile_seconds = source_geometry.tile_seconds;
            source.tile_count = static_cast<double>(source_geometry.tile_count);
        }
        if (format.fhss) {
            source.dwell_seconds = format.dwell_seconds;
            source.quiet_seconds = format.quiet_seconds;
            source.ramp_seconds = format.ramp_seconds;
        }
        source.dense_samples = dense_samples;
        source.tile_samples = tile_samples;
        source.cells = static_cast<std::size_t>(source_geometry.fine_cells);
        source.lanes = 2*owner_count;
        source.pulse = description->pulse;
        source.key_lane = [this](std::size_t lane, std::span<Complex> chips, std::stop_token stop) {
            key_lane(lane, chips, stop);
        };
        tile_description = std::move(description);
        return &tile_description->source;
    }

    unsigned source_index(std::int64_t epoch) const {
        require(epoch >= -6 && epoch <= 6, "mapped basis epoch outside public alternatives");
        return static_cast<unsigned>(epoch+6);
    }

    void prepare_sources(unsigned epoch) {
        // Caller holds this epoch's lock. All other epochs remain independent.
        auto& pair = sources[epoch].bits;
        if (pair[0]) return;
        auto format = slot.format;
        format.epoch = epoch < 6 ? format.epoch-(6-epoch) : format.epoch+(epoch-6);
        std::array<std::unique_ptr<PatternSource>, 2> prepared{
            std::make_unique<PatternSource>(format, key, 0),
            std::make_unique<PatternSource>(format, key, 1)};
        require(prepared[0]->working_bytes() <= source_geometry.source_workspace_bytes &&
                prepared[1]->working_bytes() <= source_geometry.source_workspace_bytes,
                "mapped basis source exceeds public workspace bound");
        pair = std::move(prepared);
    }

    std::uint64_t stamp() { return recency.fetch_add(1, std::memory_order_relaxed)+1; }

    // Caller holds the cache lock, shared or exclusive.
    std::shared_ptr<const Tile> find_locked(const Key& address) {
        const auto found = cache.find(address);
        if (found == cache.end()) return {};
        auto& entry = found->second;
        entry.used.store(stamp(), std::memory_order_relaxed);
        // A fill made ahead already counted this request's miss. Receipts
        // therefore match on-demand filling of the same tiles.
        if (!entry.ahead.load(std::memory_order_relaxed) || !entry.ahead.exchange(false, std::memory_order_relaxed))
            cache_hits.fetch_add(1, std::memory_order_relaxed);
        return entry.data;
    }

    // Caller holds the cache lock exclusively.
    void insert_locked(const Key& address, std::shared_ptr<const Tile> data) {
        cache.erase(address);
        while (cache.size() >= cache_limit/tile_bytes) {
            cache.erase(std::ranges::min_element(cache, {}, [](const auto& item) {
                return item.second.used.load(std::memory_order_relaxed);
            }));
            ++statistics.evictions;
        }
        cache.try_emplace(address, std::move(data), stamp());
        statistics.cache_bytes = cache.size()*tile_bytes;
        statistics.peak_cache_bytes = std::max(statistics.peak_cache_bytes, statistics.cache_bytes);
    }

    // Caller holds this epoch's source lock and has counted the miss. Both
    // bits enter the cache together. `ahead` marks the requested bit when no
    // render is waiting for it; otherwise that bit becomes the latest use.
    std::shared_ptr<const Tile> fill(unsigned bit, unsigned epoch, std::uint64_t index, bool ahead,
                                     const std::stop_token& stop) {
        const Key address{bit, epoch, index};
        prepare_sources(epoch);
        require(index <= UINT64_MAX/tile_samples, "mapped basis tile address overflow");
        const auto begin = index*tile_samples;
        require(begin < dense_samples, "mapped basis tile outside source support");
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(tile_samples, dense_samples-begin));
        std::array<PatternSource*, 2> bank{sources[epoch].bits[0].get(), sources[epoch].bits[1].get()};
        const auto began = std::chrono::steady_clock::now();
        std::vector<Complex> rendered(2*count);
        const auto step = dense_step();
        PatternSource::render_bank(bank, static_cast<long double>(begin)*step, step, count, rendered);
        std::array<std::shared_ptr<Tile>, 2> values{std::make_shared<Tile>(), std::make_shared<Tile>()};
        for (unsigned b = 0; b != 2; ++b) {
            for (std::size_t i = 0; i != count; ++i) {
                const auto value = rendered[b*count+i]*config.data_basis_gain;
                require(std::isfinite(value.real()) && std::isfinite(value.imag()),
                        "nonfinite mapped basis cache preparation");
                (*values[b])[i] = value;
            }
        }
        preparation_nanoseconds.fetch_add(static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::nanoseconds>(std::chrono::steady_clock::now()-began).count()), std::memory_order_relaxed);
        cancelled(stop);
        std::lock_guard lock(mutex);
        for (unsigned b = 0; b != 2; ++b) insert_locked({b, epoch, index}, values[b]);
        ++statistics.native_preparation_calls;
        statistics.native_preparation_samples += 2*count;
        const auto found = cache.find(address);
        if (ahead) found->second.ahead.store(true, std::memory_order_relaxed);
        // The requested bit is the latest use, just as in the Python cache.
        else found->second.used.store(stamp(), std::memory_order_relaxed);
        return found->second.data;
    }

    std::shared_ptr<const Tile> tile(unsigned bit, unsigned epoch, std::uint64_t index,
                                     const std::stop_token& stop) {
        const Key address{bit, epoch, index};
        {
            std::shared_lock lock(mutex);
            if (auto found = find_locked(address)) return found;
        }
        std::lock_guard source_lock(sources[epoch].mutex);
        cancelled(stop);
        {
            std::shared_lock lock(mutex);
            if (auto found = find_locked(address)) return found;
        }
        cache_misses.fetch_add(1, std::memory_order_relaxed);
        return fill(bit, epoch, index, false, stop);
    }

    // Pins a render group's tiles in bit-major order. One cache lock serves
    // every tile already present; a missing tile takes the filling path.
    void pin(unsigned first_bit, unsigned bits, unsigned epoch, std::uint64_t low, std::size_t count,
             std::span<std::shared_ptr<const Tile>> pins, const std::stop_token& stop) {
        const auto total = bits*count;
        const auto address = [&](std::size_t item) {
            return Key{first_bit+static_cast<unsigned>(item/count), epoch, low+item%count};
        };
        for (std::size_t next = 0; next != total;) {
            {
                std::shared_lock lock(mutex);
                for (; next != total; ++next) {
                    auto found = find_locked(address(next));
                    if (!found) break;
                    pins[next] = std::move(found);
                }
            }
            if (next == total) break;
            const auto [bit, source, index] = address(next);
            pins[next] = tile(bit, source, index, stop);
            ++next;
        }
    }

    void note_pins(std::size_t pinned) {
        for (auto peak = peak_pinned_tiles.load(std::memory_order_relaxed); pinned > peak &&
             !peak_pinned_tiles.compare_exchange_weak(peak, pinned, std::memory_order_relaxed);) {}
    }

    // The fill a later render would otherwise wait for, with no lookup credit.
    void fill_ahead(unsigned bit, unsigned epoch, std::uint64_t index, const std::stop_token& stop) {
        const Key address{bit, epoch, index};
        {
            std::shared_lock lock(mutex);
            if (cache.contains(address)) return;
        }
        std::lock_guard source_lock(sources[epoch].mutex);
        cancelled(stop);
        {
            std::shared_lock lock(mutex);
            if (cache.contains(address)) return;
        }
        cache_misses.fetch_add(1, std::memory_order_relaxed);
        (void)fill(bit, epoch, index, true, stop);
    }

    void direct(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                std::span<const double> offsets, std::span<Complex> output, const std::stop_token& stop) {
        const auto epoch = source_index(row.epoch_offset_seconds);
        std::lock_guard source_lock(sources[epoch].mutex);
        cancelled(stop);
        prepare_sources(epoch);
        Candidate candidate;
        candidate.epoch_offset_seconds = row.epoch_offset_seconds;
        candidate.arrival_seconds = row.arrival_seconds;
        candidate.frequency_hz = row.frequency_hz;
        candidate.clock_ppm = row.clock_ppm;
        candidate.channel.taps.assign(taps.begin(), taps.end());
        for (unsigned bit = 0; bit != 2; ++bit) {
            candidate.source_bit = static_cast<std::uint8_t>(bit);
            auto destination = output.subspan(bit*taps.size()*offsets.size(), taps.size()*offsets.size());
            shaped_basis_mapped(*sources[epoch].bits[bit], candidate, config.sample_rate_hz,
                                first, offsets, destination, config.data_if_hz);
            for (auto& value : destination) value *= config.data_basis_gain;
            cancelled(stop);
        }
        std::lock_guard lock(mutex);
        statistics.direct_rows += 2*taps.size();
    }

    struct Map {
        long double origin = 0;
        double fraction = 0, scale = 0, phase = 0, phase_step = 0;
        std::uint64_t low_tile = 0;
        std::size_t count = 0;
        bool empty = false, fallback = false, large_phase = false, staging = false;
        std::string_view fallback_reason;
    };

    // The public affine map of one row and tap over a block from `first`.
    Map mapping(const MappedBasisRow& row, const Tap& tap, std::uint64_t first,
                double first_offset, double last_offset) const {
        Map map;
        const double scale = 1+row.clock_ppm*1e-6;
        const auto wide_scale = static_cast<long double>(scale);
        const auto shift = static_cast<long double>(row.arrival_seconds+tap.delay_seconds)*config.sample_rate_hz;
        const auto dense_origin = ((static_cast<long double>(first)-shift)*wide_scale)*dense_factor;
        // These forms keep the x87 control word.
        map.origin = detail::floor_coordinate(dense_origin);
        map.fraction = static_cast<double>(dense_origin-map.origin);
        map.scale = scale*dense_factor;
        const double relative_first = first_offset*map.scale+map.fraction;
        const double relative_last = last_offset*map.scale+map.fraction;
        const auto initial_time = static_cast<long double>(first)/config.sample_rate_hz;
        const auto difference = ((wide_scale-1)*first-wide_scale*shift)/config.sample_rate_hz;
        const std::array<long double, 3> phases{
            static_cast<long double>(row.frequency_hz)*initial_time,
            static_cast<long double>(tap.doppler_hz)*initial_time,
            static_cast<long double>(config.data_if_hz)*difference};
        map.phase = static_cast<double>(unit_cycle(unit_cycle(phases[0])+unit_cycle(phases[1])+unit_cycle(phases[2])));
        map.phase_step = (row.frequency_hz+tap.doppler_hz+config.data_if_hz*(scale-1))/config.sample_rate_hz;
        map.large_phase = std::any_of(phases.begin(), phases.end(), [](auto p) { return std::abs(p) > 0x1p40L; });
        bound(map, relative_first, relative_last, first_offset, last_offset);
        return map;
    }

    // The terms of mapping() shared by every row and tap of one block, and
    // by both taps of one row in it. The addressed owner path computes each
    // once, with the same operations on the same operands.
    struct BlockTerms {
        std::uint64_t first = 0;
        long double time = 0;
        std::array<long double, maximum_taps> doppler_phase{}, doppler_cycle{};
    };
    struct RowTerms {
        double scale = 0, dense_scale = 0;
        long double wide_scale = 0, wide_first = 0, frequency_phase = 0, frequency_cycle = 0;
    };

    BlockTerms block_terms(std::uint64_t first, std::span<const Tap> taps) const {
        BlockTerms block;
        block.first = first;
        block.time = static_cast<long double>(first)/config.sample_rate_hz;
        for (std::size_t tap = 0; tap != taps.size(); ++tap) {
            block.doppler_phase[tap] = static_cast<long double>(taps[tap].doppler_hz)*block.time;
            block.doppler_cycle[tap] = unit_cycle(block.doppler_phase[tap]);
        }
        return block;
    }

    RowTerms row_terms(const MappedBasisRow& row, const BlockTerms& block) const {
        RowTerms terms;
        terms.scale = 1+row.clock_ppm*1e-6;
        terms.wide_scale = static_cast<long double>(terms.scale);
        terms.dense_scale = terms.scale*dense_factor;
        terms.wide_first = (terms.wide_scale-1)*block.first;
        terms.frequency_phase = static_cast<long double>(row.frequency_hz)*block.time;
        terms.frequency_cycle = unit_cycle(terms.frequency_phase);
        return terms;
    }

    // mapping(row, taps[tap], block.first, first_offset, last_offset), equal
    // bit for bit: every value is the same IEEE operation on the same
    // operands, only evaluated once for its block or row.
    Map mapping(const MappedBasisRow& row, std::span<const Tap> taps, std::size_t tap, const RowTerms& terms,
                const BlockTerms& block, double first_offset, double last_offset) const {
        Map map;
        const auto shift = static_cast<long double>(row.arrival_seconds+taps[tap].delay_seconds)*config.sample_rate_hz;
        const auto dense_origin = ((static_cast<long double>(block.first)-shift)*terms.wide_scale)*dense_factor;
        map.origin = detail::floor_coordinate(dense_origin);
        map.fraction = static_cast<double>(dense_origin-map.origin);
        map.scale = terms.dense_scale;
        const double relative_first = first_offset*map.scale+map.fraction;
        const double relative_last = last_offset*map.scale+map.fraction;
        const auto difference = (terms.wide_first-terms.wide_scale*shift)/config.sample_rate_hz;
        const auto center_phase = static_cast<long double>(config.data_if_hz)*difference;
        map.phase = static_cast<double>(unit_cycle(terms.frequency_cycle+block.doppler_cycle[tap]+
                                                   unit_cycle(center_phase)));
        map.phase_step = (row.frequency_hz+taps[tap].doppler_hz+config.data_if_hz*(terms.scale-1))/
                         config.sample_rate_hz;
        map.large_phase = std::abs(terms.frequency_phase) > 0x1p40L || std::abs(block.doppler_phase[tap]) > 0x1p40L ||
                          std::abs(center_phase) > 0x1p40L;
        bound(map, relative_first, relative_last, first_offset, last_offset);
        return map;
    }

    // The rest of a map whose origin, fraction, scale, phase and step are set.
    void bound(Map& map, double relative_first, double relative_last, double first_offset, double last_offset) const {
        if (!std::isfinite(relative_first) || !std::isfinite(relative_last) || !std::isfinite(map.origin) ||
                !std::isfinite(map.phase) || !std::isfinite(map.phase_step)) {
            map.fallback = true;
            map.fallback_reason = "nonfinite-relative-map";
            return;
        }
        const auto lower = map.origin+std::floor(relative_first)-1;
        const auto upper = map.origin+std::floor(relative_last)+2;
        if (upper < 0 || lower >= dense_samples) {
            map.empty = true;
            return;
        }
        map.low_tile = detail::unsigned_index(std::max(0.L, lower))/tile_samples;
        const auto high_tile = detail::unsigned_index(std::min(static_cast<long double>(dense_samples-1), upper))/tile_samples;
        const auto count = high_tile-map.low_tile+1;
        map.staging = count > maximum_pins;
        const auto origin = map.origin-static_cast<long double>(map.low_tile)*tile_samples;
        map.fallback = map.large_phase || map.staging || std::abs(relative_last) > 0x1p31 ||
            std::abs(map.phase+map.phase_step*first_offset) > 0x1p30 ||
            std::abs(map.phase+map.phase_step*last_offset) > 0x1p30 ||
            origin < static_cast<long double>(INT64_MIN) || origin > static_cast<long double>(INT64_MAX);
        if (map.fallback) map.fallback_reason = map.large_phase ? "large-phase" :
            map.staging ? "pinned-tile-limit" : "relative-map-range";
        if (!map.fallback) map.count = static_cast<std::size_t>(count);
    }

    unsigned validate_request(const MappedBasisRow& row, std::span<const Tap> taps,
                              std::uint64_t first, std::span<const double> offsets,
                              bool consecutive = false) const {
        require(!offsets.empty() && offsets.size() <= maximum_rows && !taps.empty() && taps.size() <= maximum_taps &&
                offsets.size() <= maximum_samples, "mapped basis output extent mismatch");
        require(std::isfinite(row.arrival_seconds) && std::isfinite(row.frequency_hz) &&
                std::isfinite(row.clock_ppm) && row.clock_ppm > -1e6, "invalid mapped basis candidate");
        const auto epoch = source_index(row.epoch_offset_seconds);
        // The receiver's own blocks use consecutive rows. Their bounds follow
        // from the extent, with no per-row extended-precision proof.
        if (!consecutive) {
            consecutive = true;
            for (std::size_t i = 0; i != offsets.size(); ++i) consecutive &= offsets[i] == static_cast<double>(i);
        }
        if (consecutive) {
            require(offsets.size()-1 <= UINT64_MAX-first, "mapped basis offset outside bounded coordinates");
        } else {
            long double previous = -1;
            for (const auto offset : offsets) {
                require(std::isfinite(offset) && offset >= 0 && offset <= 1048576 &&
                        static_cast<long double>(offset) <= static_cast<long double>(UINT64_MAX-first),
                        "mapped basis offset outside bounded coordinates");
                const auto index = static_cast<long double>(first)+offset;
                require(index > previous, "mapped basis coordinates are not strictly increasing");
                previous = index;
            }
        }
        for (const auto& tap : taps)
            require(std::isfinite(tap.delay_seconds) && tap.delay_seconds >= 0 && std::isfinite(tap.doppler_hz),
                    "invalid mapped basis channel tap");
        return epoch;
    }

    void render(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                std::span<const double> offsets, std::span<Complex> output, const std::stop_token& stop) {
        cancelled(stop);
        require(output.size() == 2*taps.size()*offsets.size(), "mapped basis output extent mismatch");
        const auto epoch = validate_request(row, taps, first, offsets);
        calls.fetch_add(1, std::memory_order_relaxed);
        if (!statistics.fallback_reason.empty() ||
                (std::numeric_limits<long double>::digits < 64 && first > (UINT64_C(1)<<48))) {
            direct(row, taps, first, offsets, output, stop);
            return;
        }
        for (std::size_t t = 0; t != taps.size(); ++t) {
            cancelled(stop);
            const auto map = mapping(row, taps[t], first, offsets.front(), offsets.back());
            if (map.fallback) {
                // Direct rendering is one bounded pair. Temporary output keeps
                // the public [bit][tap][row] layout without aliasing other taps.
                std::vector<Complex> fallback(2*offsets.size());
                direct(row, taps.subspan(t, 1), first, offsets, fallback, stop);
                for (unsigned bit = 0; bit != 2; ++bit)
                    std::copy_n(fallback.data()+bit*offsets.size(), offsets.size(),
                                output.data()+(bit*taps.size()+t)*offsets.size());
                std::lock_guard lock(mutex);
                statistics.large_phase_fallback_rows += 2*map.large_phase;
                statistics.staging_fallback_rows += 2*map.staging;
                continue;
            }
            if (map.empty) {
                for (unsigned bit = 0; bit != 2; ++bit)
                    std::fill_n(output.data()+(bit*taps.size()+t)*offsets.size(), offsets.size(), Complex{});
                continue;
            }
            // Large spans use one bit at a time to preserve the same 32-tile
            // pin bound. Usual blocks render both hypotheses with one map.
            const unsigned group = map.count <= maximum_pins/2 ? 2 : 1;
            for (unsigned begin = 0; begin != 2; begin += group) {
                std::array<std::shared_ptr<const Tile>, maximum_pins> storage;
                const auto pins = std::span(storage).first(group*map.count);
                pin(begin, group, epoch, map.low_tile, map.count, pins, stop);
                std::array<std::vector<const double*>, 2> tables;
                std::array<TriadMappedRow, 2> rows;
                for (unsigned b = 0; b != group; ++b) {
                    tables[b].reserve(map.count);
                    for (std::size_t i = 0; i != map.count; ++i)
                        tables[b].push_back(reinterpret_cast<const double*>(pins[b*map.count+i]->data()));
                    rows[b] = {map.fraction, map.scale, map.phase, map.phase_step,
                        detail::signed_index(map.origin-static_cast<long double>(map.low_tile)*tile_samples),
                        map.count, tables[b].data(),
                        reinterpret_cast<double*>(output.data()+((begin+b)*taps.size()+t)*offsets.size())};
                }
                cancelled(stop);
                if (group == 2) triad_render_mapped_paired_v1(rows.data(), 1, offsets.data(), offsets.size(), tile_samples);
                else triad_render_mapped_v1(rows.data(), 1, offsets.data(), offsets.size(), tile_samples);
                interpolated_rows.fetch_add(group, std::memory_order_relaxed);
                note_pins(pins.size());
            }
        }
        bool finite = true;
        for (const auto value : output) finite &= std::isfinite(value.real()) & std::isfinite(value.imag());
        require(finite, "mapped basis produced nonfinite samples");
    }

    // The tiles that render() pins for consecutive rows, and no others.
    void prepare(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                 std::size_t rows, const std::stop_token& stop) {
        cancelled(stop);
        require(rows && rows <= maximum_rows && rows <= maximum_samples && !taps.empty() &&
                taps.size() <= maximum_taps && rows-1 <= UINT64_MAX-first,
                "mapped basis output extent mismatch");
        require(std::isfinite(row.arrival_seconds) && std::isfinite(row.frequency_hz) &&
                std::isfinite(row.clock_ppm) && row.clock_ppm > -1e6, "invalid mapped basis candidate");
        const auto epoch = source_index(row.epoch_offset_seconds);
        if (!statistics.fallback_reason.empty() ||
                (std::numeric_limits<long double>::digits < 64 && first > (UINT64_C(1)<<48))) return;
        for (const auto& tap : taps) {
            require(std::isfinite(tap.delay_seconds) && tap.delay_seconds >= 0 && std::isfinite(tap.doppler_hz),
                    "invalid mapped basis channel tap");
            const auto map = mapping(row, tap, first, 0., static_cast<double>(rows-1));
            if (map.fallback || map.empty) continue;
            for (std::size_t i = 0; i != map.count; ++i)
                for (unsigned bit = 0; bit != 2; ++bit) fill_ahead(bit, epoch, map.low_tile+i, stop);
        }
    }

    // The reason every column of a request takes the exact native route, if any.
    std::string_view request_fallback(std::uint64_t first) const {
        auto reason = std::string_view(statistics.fallback_reason);
        if (reason.empty() && std::numeric_limits<long double>::digits < 64 && first > (UINT64_C(1)<<48))
            reason = "native-origin-precision";
        return reason;
    }

    // Both bit columns of one tap by the exact native route. At most one
    // native bit-pair is materialized: the canonical fallback, never the
    // normal device path.
    void direct_columns(const MappedBasisRow& row, std::span<const Tap> taps, std::size_t tap, std::uint64_t first,
                        std::span<const double> offsets, const BulkMappedSink& sink, const std::stop_token& stop,
                        std::string_view reason) {
        std::vector<Complex> values(2*offsets.size());
        direct(row, taps.subspan(tap, 1), first, offsets, values, stop);
        const std::array<BulkDirectColumn, 2> columns{{
            {tap, std::span<const Complex>(values).first(offsets.size()), reason},
            {taps.size()+tap, std::span<const Complex>(values).subspan(offsets.size()), reason}}};
        sink(offsets, {}, columns);
        cancelled(stop);
        std::lock_guard lock(mutex);
        statistics.prepared_direct_columns += columns.size();
    }

    // A map that the native route renders: its columns, then its receipts.
    void fallback_columns(const MappedBasisRow& row, std::span<const Tap> taps, std::size_t tap, std::uint64_t first,
                          std::span<const double> offsets, const BulkMappedSink& sink, const std::stop_token& stop,
                          const Map& map) {
        direct_columns(row, taps, tap, first, offsets, sink, stop, map.fallback_reason);
        std::lock_guard lock(mutex);
        statistics.large_phase_fallback_rows += 2*map.large_phase;
        statistics.staging_fallback_rows += 2*map.staging;
    }

    // The bit columns [begin, begin+group) of a mapped tap, without tiles.
    static void describe_columns(const Map& map, std::int64_t origin, unsigned epoch, std::size_t tap,
                                 std::size_t taps, unsigned begin, unsigned group,
                                 std::array<BulkMappedColumn, 2>& columns) {
        for (unsigned b = 0; b != group; ++b) {
            auto& column = columns[b];
            column.fraction = map.fraction;
            column.scale = map.scale;
            column.phase = map.phase;
            column.phase_step = map.phase_step;
            column.origin = origin;
            column.output_column = (begin+b)*taps+tap;
            column.address = {map.low_tile, 2*epoch+begin+b, static_cast<std::uint32_t>(map.count)};
        }
    }

    // A prepared visit leases each mapped column's native tiles.
    void visit(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
               std::span<const double> offsets, const BulkMappedSink& sink, const std::stop_token& stop,
               bool consecutive) {
        cancelled(stop);
        require(static_cast<bool>(sink), "mapped basis requires a synchronous mapping sink");
        const auto epoch = validate_request(row, taps, first, offsets, consecutive);
        calls.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard lock(mutex);
            ++statistics.prepared_calls;
        }
        const auto fallback_reason = request_fallback(first);
        for (std::size_t tap = 0; tap != taps.size(); ++tap) {
            cancelled(stop);
            if (!fallback_reason.empty()) {
                direct_columns(row, taps, tap, first, offsets, sink, stop, fallback_reason);
                continue;
            }
            const auto map = mapping(row, taps[tap], first, offsets.front(), offsets.back());
            if (map.fallback) {
                fallback_columns(row, taps, tap, first, offsets, sink, stop, map);
                continue;
            }
            if (map.empty) {
                std::lock_guard lock(mutex);
                statistics.prepared_empty_columns += 2;
                continue;
            }
            const unsigned group = map.count <= maximum_pins/2 ? 2 : 1;
            const auto origin = detail::signed_index(
                map.origin-static_cast<long double>(map.low_tile)*tile_samples);
            for (unsigned begin = 0; begin != 2; begin += group) {
                std::array<BulkMappedColumn, 2> columns;
                describe_columns(map, origin, epoch, tap, taps.size(), begin, group, columns);
                std::array<std::shared_ptr<const Tile>, maximum_pins> pins;
                pin(begin, group, epoch, map.low_tile, map.count, std::span(pins).first(group*map.count), stop);
                for (unsigned b = 0; b != group; ++b) {
                    auto& column = columns[b];
                    column.tiles.reserve(map.count);
                    for (std::size_t i = 0; i != map.count; ++i) {
                        auto& data = pins[b*map.count+i];
                        const std::span<const Complex> samples(*data);
                        column.tiles.push_back({std::move(data), samples});
                    }
                }
                cancelled(stop);
                sink(offsets, std::span<const BulkMappedColumn>(columns).first(group), {});
                cancelled(stop);
                note_pins(group*map.count);
                std::lock_guard lock(mutex);
                statistics.prepared_mapped_columns += group;
            }
        }
    }
};

struct MappedBasis::Addresses::State {
    explicit State(Impl& value) : impl(value) {}
    Impl& impl;
    std::span<const MappedBasisRow> rows;
    std::span<const Tap> taps;
    // The block of the latest call, kept for the rows that follow it.
    Impl::BlockTerms block;
    bool block_ready = false, admitted = false;
    // Receipts credited on close.
    std::uint64_t calls = 0, columns = 0, empty_columns = 0;
};

ConsecutiveBasisOffsets::ConsecutiveBasisOffsets(std::size_t rows) : offsets_(rows) {
    std::iota(offsets_.begin(), offsets_.end(), 0.);
}

MappedBasis::MappedBasis(const ReceiverConfig& config, const ReceiverSlotPlan& slot, const Crypto& key,
                         std::size_t rows, unsigned workers)
    : impl_(std::make_unique<Impl>(config, slot, key, rows, workers)) {}
MappedBasis::~MappedBasis() = default;

std::size_t MappedBasis::workspace_bytes(const ReceiverConfig& config, const ReceiverSlotPlan& slot,
                                        std::size_t rows, unsigned workers) {
    validate_shape(config, slot, rows, workers);
    const auto shape = geometry(slot.format);
    auto total = add(cache_limit, multiply(2*owner_count, shape.source_workspace_bytes));
    // LRU/map metadata, epoch locks and owner bookkeeping are bounded by the
    // fixed cache entry count; no received-content-sized descriptors exist.
    total = add(total, 64U*1024U);
    auto per_worker = multiply(maximum_pins+4, tile_bytes);
    per_worker = add(per_worker, PatternSource::render_bank_scratch_bytes);
    per_worker = add(per_worker, multiply(2*maximum_taps*rows, sizeof(Complex)));
    per_worker = add(per_worker, multiply(2*rows, sizeof(Complex))); // direct fallback pair
    per_worker = add(per_worker, 16U*1024U); // pointer/pin tables and bounded tap metadata
    return add(total, multiply(workers, per_worker));
}

void MappedBasis::render_pair(const MappedBasisRow& row, std::span<const Tap> taps,
                              std::uint64_t first, std::span<const double> offsets,
                              std::span<Complex> output, const std::stop_token& stop) {
    const auto before = impl_->active.fetch_add(1);
    struct Release { std::atomic<unsigned>& active; ~Release() { active.fetch_sub(1); } } release{impl_->active};
    require(before < impl_->workers, "mapped basis concurrency exceeds admitted worker share");
    impl_->render(row, taps, first, offsets, output, stop);
}

void MappedBasis::prepare_pair(const MappedBasisRow& row, std::span<const Tap> taps,
                               std::uint64_t first, std::size_t rows, const std::stop_token& stop) {
    const auto before = impl_->active.fetch_add(1);
    struct Release { std::atomic<unsigned>& active; ~Release() { active.fetch_sub(1); } } release{impl_->active};
    require(before < impl_->workers, "mapped basis concurrency exceeds admitted worker share");
    impl_->prepare(row, taps, first, rows, stop);
}

void MappedBasis::visit_pair(const MappedBasisRow& row, std::span<const Tap> taps,
                             std::uint64_t first, std::span<const double> offsets,
                             const BulkMappedSink& sink, const std::stop_token& stop) {
    visit_pair_impl(row, taps, first, offsets, sink, stop, false);
}

void MappedBasis::visit_pair(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                             const ConsecutiveBasisOffsets& offsets, std::size_t rows,
                             const BulkMappedSink& sink, const std::stop_token& stop) {
    visit_pair_impl(row, taps, first, consecutive(offsets, rows), sink, stop, true);
}

std::span<const double> MappedBasis::consecutive(const ConsecutiveBasisOffsets& offsets, std::size_t rows) {
    require(rows <= offsets.size(), "mapped basis offsets exceed the consecutive row extent");
    return std::span(offsets.offsets_).first(rows);
}

const FineTileSource* MappedBasis::tile_source() { return impl_->describe(); }

void MappedBasis::address_pair(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                               const ConsecutiveBasisOffsets& offsets, std::size_t rows,
                               const BulkMappedSink& sink, const std::stop_token& stop) {
    Addresses addresses(*this);
    addresses.open(std::span(&row, 1), taps);
    addresses.address(0, first, offsets, rows, sink, stop);
}

void MappedBasis::visit_pair_impl(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                                  std::span<const double> offsets, const BulkMappedSink& sink,
                                  const std::stop_token& stop, bool consecutive) {
    const auto before = impl_->active.fetch_add(1);
    struct Release { std::atomic<unsigned>& active; ~Release() { active.fetch_sub(1); } } release{impl_->active};
    require(before < impl_->workers, "mapped basis concurrency exceeds admitted worker share");
    impl_->visit(row, taps, first, offsets, sink, stop, consecutive);
}

MappedBasis::Addresses::Addresses(MappedBasis& basis) : state_(std::make_unique<State>(*basis.impl_)) {}
MappedBasis::Addresses::~Addresses() { close(); }

void MappedBasis::Addresses::open(std::span<const MappedBasisRow> rows, std::span<const Tap> taps) {
    close();
    state_->rows = rows;
    state_->taps = taps;
}

void MappedBasis::Addresses::close() noexcept {
    auto& state = *state_;
    auto& impl = state.impl;
    if (state.calls) {
        impl.calls.fetch_add(state.calls, std::memory_order_relaxed);
        impl.addressed_calls.fetch_add(state.calls, std::memory_order_relaxed);
    }
    if (state.columns) impl.addressed_columns.fetch_add(state.columns, std::memory_order_relaxed);
    if (state.empty_columns) {
        std::lock_guard lock(impl.mutex);
        impl.statistics.prepared_empty_columns += state.empty_columns;
    }
    if (state.admitted) impl.active.fetch_sub(1);
    state.calls = state.columns = state.empty_columns = 0;
    state.admitted = state.block_ready = false;
    state.rows = {};
    state.taps = {};
}

// The steps, checks, cancellation points and sink calls of address_pair. A
// map's terms of its block are kept for the following rows of that block,
// and those of its row are shared by both taps.
void MappedBasis::Addresses::address(std::size_t index, std::uint64_t first,
                                     const ConsecutiveBasisOffsets& consecutive_offsets, std::size_t count,
                                     const BulkMappedSink& sink, const std::stop_token& stop) {
    auto& state = *state_;
    auto& impl = state.impl;
    const auto offsets = consecutive(consecutive_offsets, count);
    require(index < state.rows.size(), "mapped basis address outside its open rows");
    if (!state.admitted) {
        const auto before = impl.active.fetch_add(1);
        if (before >= impl.workers) {
            impl.active.fetch_sub(1);
            throw Error("mapped basis concurrency exceeds admitted worker share");
        }
        state.admitted = true;
    }
    const auto& row = state.rows[index];
    const auto taps = state.taps;
    cancelled(stop);
    require(static_cast<bool>(sink), "mapped basis requires a synchronous mapping sink");
    const auto epoch = impl.validate_request(row, taps, first, offsets, true);
    ++state.calls;
    const auto fallback_reason = impl.request_fallback(first);
    if (!state.block_ready || state.block.first != first) {
        state.block = impl.block_terms(first, taps);
        state.block_ready = true;
    }
    const auto terms = impl.row_terms(row, state.block);
    for (std::size_t tap = 0; tap != taps.size(); ++tap) {
        cancelled(stop);
        if (!fallback_reason.empty()) {
            impl.direct_columns(row, taps, tap, first, offsets, sink, stop, fallback_reason);
            continue;
        }
        const auto map = impl.mapping(row, taps, tap, terms, state.block, offsets.front(), offsets.back());
        if (map.fallback) {
            impl.fallback_columns(row, taps, tap, first, offsets, sink, stop, map);
            continue;
        }
        if (map.empty) {
            state.empty_columns += 2;
            continue;
        }
        const unsigned group = map.count <= maximum_pins/2 ? 2 : 1;
        const auto origin = detail::signed_index(map.origin-static_cast<long double>(map.low_tile)*tile_samples);
        for (unsigned begin = 0; begin != 2; begin += group) {
            std::array<BulkMappedColumn, 2> columns;
            Impl::describe_columns(map, origin, epoch, tap, taps.size(), begin, group, columns);
            // No tile is pinned: the consumer renders these addresses.
            cancelled(stop);
            sink(offsets, std::span<const BulkMappedColumn>(columns).first(group), {});
            cancelled(stop);
            state.columns += group;
        }
    }
}

MappedBasisReport MappedBasis::report() const {
    std::shared_lock lock(impl_->mutex);
    auto result = impl_->statistics;
    result.cache_hits = impl_->cache_hits.load();
    result.cache_misses = impl_->cache_misses.load();
    result.calls = impl_->calls.load();
    result.interpolated_rows = impl_->interpolated_rows.load();
    result.peak_pinned_tiles = impl_->peak_pinned_tiles.load();
    result.addressed_calls = impl_->addressed_calls.load();
    result.addressed_columns = impl_->addressed_columns.load();
    result.native_preparation_seconds = static_cast<double>(impl_->preparation_nanoseconds.load())*1e-9;
    return result;
}

} // namespace datapump::triad::v3::receiver_detail

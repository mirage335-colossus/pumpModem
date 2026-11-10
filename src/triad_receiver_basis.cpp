#include "triad_receiver_basis.hpp"
#include "triad_mapped_kernel.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <numbers>
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
void cancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw Error("Robust receive cancelled");
}
long double unit_cycle(long double value) {
    auto result = std::fmod(value, 1.L);
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
    struct Entry {
        std::shared_ptr<const Tile> data;
        std::list<Key>::iterator order;
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
    mutable std::mutex mutex;
    std::list<Key> lru;
    std::map<Key, Entry> cache;
    MappedBasisReport statistics;
    std::atomic<unsigned> active{0};

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
        statistics.dense_factor = dense_factor;
        statistics.oversampling_ratio = config.sample_rate_hz*dense_factor/width;
        statistics.interpolation_amplitude_proxy = (3./128)*
            std::pow(std::numbers::pi/statistics.oversampling_ratio, 4);
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

    std::shared_ptr<const Tile> find_locked(const Key& address) {
        const auto found = cache.find(address);
        if (found == cache.end()) return {};
        lru.splice(lru.end(), lru, found->second.order);
        ++statistics.cache_hits;
        return found->second.data;
    }

    void insert_locked(const Key& address, std::shared_ptr<const Tile> data) {
        if (const auto found = cache.find(address); found != cache.end()) {
            lru.erase(found->second.order);
            cache.erase(found);
        }
        while (cache.size() >= cache_limit/tile_bytes) {
            cache.erase(lru.front());
            lru.pop_front();
            ++statistics.evictions;
        }
        lru.push_back(address);
        cache.emplace(address, Entry{std::move(data), std::prev(lru.end())});
        statistics.cache_bytes = cache.size()*tile_bytes;
        statistics.peak_cache_bytes = std::max(statistics.peak_cache_bytes, statistics.cache_bytes);
    }

    std::shared_ptr<const Tile> tile(unsigned bit, unsigned epoch, std::uint64_t index,
                                     std::stop_token stop) {
        const Key address{bit, epoch, index};
        {
            std::lock_guard lock(mutex);
            if (auto found = find_locked(address)) return found;
        }
        std::lock_guard source_lock(sources[epoch].mutex);
        cancelled(stop);
        {
            std::lock_guard lock(mutex);
            if (auto found = find_locked(address)) return found;
            ++statistics.cache_misses;
        }
        prepare_sources(epoch);
        require(index <= UINT64_MAX/tile_samples, "mapped basis tile address overflow");
        const auto begin = index*tile_samples;
        require(begin < dense_samples, "mapped basis tile outside source support");
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(tile_samples, dense_samples-begin));
        std::array<PatternSource*, 2> bank{sources[epoch].bits[0].get(), sources[epoch].bits[1].get()};
        std::vector<Complex> rendered(2*count);
        const long double step = static_cast<long double>(source_rate)/(config.sample_rate_hz*dense_factor);
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
        cancelled(stop);
        std::lock_guard lock(mutex);
        for (unsigned b = 0; b != 2; ++b) insert_locked({b, epoch, index}, values[b]);
        ++statistics.native_preparation_calls;
        statistics.native_preparation_samples += 2*count;
        // The requested bit is the latest use, just as in the Python cache.
        const auto found = cache.find(address);
        lru.splice(lru.end(), lru, found->second.order);
        return found->second.data;
    }

    void direct(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                std::span<const double> offsets, std::span<Complex> output, std::stop_token stop) {
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
    };

    Map mapping(const MappedBasisRow& row, const Tap& tap, std::uint64_t first,
                std::span<const double> offsets) const {
        Map map;
        const double scale = 1+row.clock_ppm*1e-6;
        const auto wide_scale = static_cast<long double>(scale);
        const auto shift = static_cast<long double>(row.arrival_seconds+tap.delay_seconds)*config.sample_rate_hz;
        const auto dense_origin = ((static_cast<long double>(first)-shift)*wide_scale)*dense_factor;
        map.origin = std::floor(dense_origin);
        map.fraction = static_cast<double>(dense_origin-map.origin);
        map.scale = scale*dense_factor;
        const double relative_first = offsets.front()*map.scale+map.fraction;
        const double relative_last = offsets.back()*map.scale+map.fraction;
        const auto initial_time = static_cast<long double>(first)/config.sample_rate_hz;
        const auto difference = ((wide_scale-1)*first-wide_scale*shift)/config.sample_rate_hz;
        const std::array<long double, 3> phases{
            static_cast<long double>(row.frequency_hz)*initial_time,
            static_cast<long double>(tap.doppler_hz)*initial_time,
            static_cast<long double>(config.data_if_hz)*difference};
        map.phase = static_cast<double>(unit_cycle(unit_cycle(phases[0])+unit_cycle(phases[1])+unit_cycle(phases[2])));
        map.phase_step = (row.frequency_hz+tap.doppler_hz+config.data_if_hz*(scale-1))/config.sample_rate_hz;
        map.large_phase = std::any_of(phases.begin(), phases.end(), [](auto p) { return std::abs(p) > 0x1p40L; });
        if (!std::isfinite(relative_first) || !std::isfinite(relative_last) || !std::isfinite(map.origin) ||
                !std::isfinite(map.phase) || !std::isfinite(map.phase_step)) {
            map.fallback = true;
            return map;
        }
        const auto lower = map.origin+std::floor(relative_first)-1;
        const auto upper = map.origin+std::floor(relative_last)+2;
        if (upper < 0 || lower >= dense_samples) {
            map.empty = true;
            return map;
        }
        map.low_tile = static_cast<std::uint64_t>(std::max(0.L, lower))/tile_samples;
        const auto high_tile = static_cast<std::uint64_t>(std::min(static_cast<long double>(dense_samples-1), upper))/tile_samples;
        const auto count = high_tile-map.low_tile+1;
        map.staging = count > maximum_pins;
        const auto origin = map.origin-static_cast<long double>(map.low_tile)*tile_samples;
        map.fallback = map.large_phase || map.staging || std::abs(relative_last) > 0x1p31 ||
            std::abs(map.phase+map.phase_step*offsets.front()) > 0x1p30 ||
            std::abs(map.phase+map.phase_step*offsets.back()) > 0x1p30 ||
            origin < static_cast<long double>(INT64_MIN) || origin > static_cast<long double>(INT64_MAX);
        if (!map.fallback) map.count = static_cast<std::size_t>(count);
        return map;
    }

    void render(const MappedBasisRow& row, std::span<const Tap> taps, std::uint64_t first,
                std::span<const double> offsets, std::span<Complex> output, std::stop_token stop) {
        cancelled(stop);
        require(!offsets.empty() && offsets.size() <= maximum_rows && !taps.empty() && taps.size() <= maximum_taps &&
                output.size() == 2*taps.size()*offsets.size(), "mapped basis output extent mismatch");
        require(std::isfinite(row.arrival_seconds) && std::isfinite(row.frequency_hz) &&
                std::isfinite(row.clock_ppm) && row.clock_ppm > -1e6, "invalid mapped basis candidate");
        const auto epoch = source_index(row.epoch_offset_seconds);
        long double previous = -1;
        for (const auto offset : offsets) {
            require(std::isfinite(offset) && offset >= 0 && offset <= 1048576 &&
                    static_cast<long double>(offset) <= static_cast<long double>(UINT64_MAX-first),
                    "mapped basis offset outside bounded coordinates");
            const auto index = static_cast<long double>(first)+offset;
            require(index > previous, "mapped basis coordinates are not strictly increasing");
            previous = index;
        }
        for (const auto& tap : taps)
            require(std::isfinite(tap.delay_seconds) && tap.delay_seconds >= 0 && std::isfinite(tap.doppler_hz),
                    "invalid mapped basis channel tap");
        {
            std::lock_guard lock(mutex);
            ++statistics.calls;
        }
        if (!statistics.fallback_reason.empty() ||
                (std::numeric_limits<long double>::digits < 64 && first > (UINT64_C(1)<<48))) {
            direct(row, taps, first, offsets, output, stop);
            return;
        }
        for (std::size_t t = 0; t != taps.size(); ++t) {
            cancelled(stop);
            const auto map = mapping(row, taps[t], first, offsets);
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
                std::vector<std::shared_ptr<const Tile>> pins;
                pins.reserve(group*map.count);
                std::array<std::vector<const double*>, 2> tables;
                std::array<TriadMappedRow, 2> rows;
                for (unsigned b = 0; b != group; ++b) {
                    tables[b].reserve(map.count);
                    for (std::size_t i = 0; i != map.count; ++i) {
                        auto data = tile(begin+b, epoch, map.low_tile+i, stop);
                        tables[b].push_back(reinterpret_cast<const double*>(data->data()));
                        pins.push_back(std::move(data));
                    }
                    rows[b] = {map.fraction, map.scale, map.phase, map.phase_step,
                        static_cast<std::int64_t>(map.origin-static_cast<long double>(map.low_tile)*tile_samples),
                        map.count, tables[b].data(),
                        reinterpret_cast<double*>(output.data()+((begin+b)*taps.size()+t)*offsets.size())};
                }
                cancelled(stop);
                if (group == 2) triad_render_mapped_paired_v1(rows.data(), 1, offsets.data(), offsets.size(), tile_samples);
                else triad_render_mapped_v1(rows.data(), 1, offsets.data(), offsets.size(), tile_samples);
                std::lock_guard lock(mutex);
                statistics.interpolated_rows += group;
                statistics.peak_pinned_tiles = std::max(statistics.peak_pinned_tiles, pins.size());
            }
        }
        for (const auto value : output)
            require(std::isfinite(value.real()) && std::isfinite(value.imag()), "mapped basis produced nonfinite samples");
    }
};

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
                              std::span<Complex> output, std::stop_token stop) {
    const auto before = impl_->active.fetch_add(1);
    struct Release { std::atomic<unsigned>& active; ~Release() { active.fetch_sub(1); } } release{impl_->active};
    require(before < impl_->workers, "mapped basis concurrency exceeds admitted worker share");
    impl_->render(row, taps, first, offsets, output, stop);
}

MappedBasisReport MappedBasis::report() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->statistics;
}

} // namespace datapump::triad::v3::receiver_detail

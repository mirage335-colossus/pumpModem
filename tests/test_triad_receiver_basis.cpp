#include "triad_receiver_basis.hpp"
#include "triad_coordinate.hpp"
#include "triad_mapped_kernel.hpp"
#include "triad_crypto_fixture.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

namespace v = datapump::triad::v3;
namespace r = v::receiver_detail;
namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F work) {
    bool rejected = false;
    try { work(); } catch (const datapump::Error&) { rejected = true; }
    check(rejected, "invalid mapped receiver request accepted");
}
datapump::Crypto key() {
    std::array<std::uint8_t, 32> bytes{};
    for (std::size_t i = 0; i != bytes.size(); ++i) bytes[i] = static_cast<std::uint8_t>(3+17*i);
    return triad_test_key(bytes);
}
v::ReceiverConfig config() {
    v::ReceiverConfig result;
    result.sample_rate_hz = 6000;
    result.data_if_hz = 1725;
    result.data_basis_gain = .71;
    return result;
}
v::ReceiverSlotPlan slot() {
    v::ReceiverSlotPlan result;
    result.format.sample_rate = 6000;
    result.format.inner_rate_hz = 36;
    result.format.added_symbols = 1;
    result.format.bit_seconds = 2;
    result.format.pulse_shape = v::PulseShape::TaperedRrc;
    return result;
}
std::vector<double> positions(std::size_t count) {
    std::vector<double> result(count);
    for (std::size_t i = 0; i != count; ++i) result[i] = i+.17+.03*std::sin(.01*i);
    return result;
}
std::vector<v::Complex> direct(const v::ReceiverConfig& settings, const v::ReceiverSlotPlan& plan,
                               const datapump::Crypto& crypto, const r::MappedBasisRow& row,
                               std::span<const v::Tap> taps, std::uint64_t first,
                               std::span<const double> offsets) {
    auto format = plan.format;
    format.epoch = static_cast<std::uint64_t>(static_cast<std::int64_t>(format.epoch)+row.epoch_offset_seconds);
    v::Candidate candidate;
    candidate.arrival_seconds = row.arrival_seconds;
    candidate.frequency_hz = row.frequency_hz;
    candidate.clock_ppm = row.clock_ppm;
    candidate.channel.taps.assign(taps.begin(), taps.end());
    std::vector<v::Complex> result(2*taps.size()*offsets.size());
    for (unsigned bit = 0; bit != 2; ++bit) {
        v::PatternSource source(format, crypto, static_cast<std::uint8_t>(bit));
        auto destination = std::span(result).subspan(bit*taps.size()*offsets.size(), taps.size()*offsets.size());
        v::shaped_basis_mapped(source, candidate, settings.sample_rate_hz, first, offsets, destination,
                               settings.data_if_hz);
    }
    for (auto& value : result) value *= settings.data_basis_gain;
    return result;
}
void close(std::span<const v::Complex> actual, std::span<const v::Complex> expected) {
    check(actual.size() == expected.size(), "basis comparison geometry changed");
    double error = 0, energy = 0, peak = 0;
    for (std::size_t i = 0; i != actual.size(); ++i) {
        error += std::norm(actual[i]-expected[i]);
        energy += std::norm(expected[i]);
        peak = std::max(peak, std::abs(actual[i]-expected[i]));
    }
    // The existing actual-native cache gate qualifies this interpolation at
    // RMS < .001 and peak < .004 (test_triad_tracking_basis.py). A direct call
    // is exact; the public metadata-selected cubic cache is an approximation.
    check(error <= 1e-6*std::max(energy, 1e-20) && peak < .004,
          "cached basis differs from direct native map");
}

void cached_maps(const datapump::Crypto& crypto) {
    const auto offsets = positions(257);
    const std::array<v::Tap, 2> taps{{{0,.2},{.007,-.4}}};
    const r::MappedBasisRow row{2,.013,1.25,127.25};
    for (const auto source_rate : {6000U, 4800U}) {
        auto plan = slot();
        plan.format.sample_rate = source_rate;
        const auto settings = config();
        r::MappedBasis basis(settings, plan, crypto, offsets.size(), 2);
        const auto end = static_cast<std::uint64_t>(std::ceil(
            static_cast<long double>(v::geometry(plan.format).source_samples)*settings.sample_rate_hz/source_rate));
        for (const auto first : {UINT64_C(0), UINT64_C(12500), UINT64_C(25000), end-200, end+1000}) {
            std::vector<v::Complex> actual(4*offsets.size()), again(actual.size());
            basis.render_pair(row, taps, first, offsets, actual);
            basis.render_pair(row, taps, first, offsets, again);
            close(actual, direct(settings, plan, crypto, row, taps, first, offsets));
            check(actual == again, "immutable cache replay changed samples");
        }
        const auto report = basis.report();
        check(report.fallback_reason.empty() && report.interpolated_rows && !report.direct_rows,
              "smooth supported waveform did not use cached interpolation");
        check(report.cache_hits && report.cache_misses && report.peak_cache_bytes <= 32*1024*1024 &&
                report.peak_pinned_tiles <= 32, "mapped cache bound or reuse failed");
        check(r::MappedBasis::workspace_bytes(settings, plan, offsets.size(), 2) >= report.peak_cache_bytes,
              "mapped workspace omits cache");
    }
}

void concurrent_pairs(const datapump::Crypto& crypto) {
    const auto offsets = positions(401);
    const std::array<v::Tap, 1> taps{{{0,0}}};
    const auto settings = config();
    const auto plan = slot();
    r::MappedBasis basis(settings, plan, crypto, offsets.size(), 4);
    std::array<std::future<std::vector<v::Complex>>, 4> work;
    for (unsigned i = 0; i != work.size(); ++i) {
        work[i] = std::async(std::launch::async, [&, i] {
            std::vector<v::Complex> result(2*offsets.size());
            basis.render_pair({static_cast<std::int64_t>(i%2), .01, .125, 17.}, taps, 25000, offsets, result);
            return result;
        });
    }
    const auto zero = work[0].get(), one = work[1].get();
    check(zero == work[2].get() && one == work[3].get(), "concurrent paired cache changed hypotheses");
    check(std::any_of(zero.begin(), zero.end(), [](auto value) { return std::norm(value) > 1e-10; }),
          "concurrent fixture missed active DATA support");
    close(zero, direct(settings, plan, crypto, {0,.01,.125,17.}, taps, 25000, offsets));
}

void dense_and_gated(const datapump::Crypto& crypto) {
    const auto settings = config();
    const auto offsets = positions(257);
    const std::array<v::Tap, 1> taps{{{0,.2}}};
    const r::MappedBasisRow row{2,.00013,1.25,127.25};
    for (const bool gated : {false, true}) {
        auto plan = slot();
        plan.format.inner_rate_hz = 3600;
        plan.format.bit_seconds = .001;
        plan.format.fhss = gated;
        plan.format.quiet_seconds = gated ? .055 : 0;
        plan.format.ramp_seconds = .02;
        plan.format.burst_wait_tiles = gated ? 1 : 0;
        r::MappedBasis basis(settings, plan, crypto, offsets.size(), 1);
        std::vector<v::Complex> output(2*offsets.size());
        const auto first = v::geometry(plan.format).calendar_origin_sample+101;
        basis.render_pair(row, taps, first, offsets, output);
        close(output, direct(settings, plan, crypto, row, taps, first, offsets));
        const auto report = basis.report();
        check(report.fallback_reason.empty() && report.interpolated_rows,
              "dense or gated source did not use qualified cache");
        if (!gated) check(report.dense_factor == 3, "high-rate cache did not prepare dense samples");
    }
}

void fallback_and_validation(const datapump::Crypto& crypto) {
    auto settings = config();
    auto plan = slot();
    const auto offsets = positions(101);
    const std::array<v::Tap, 1> taps{{{0,0}}};
    r::MappedBasisRow row{-6,.1,-.2,-100};
    std::vector<v::Complex> output(2*offsets.size());
    plan.format.pulse_shape = v::PulseShape::Rrc;
    r::MappedBasis fallback(settings, plan, crypto, offsets.size(), 1);
    fallback.render_pair(row, taps, 6000, offsets, output);
    check(output == direct(settings, plan, crypto, row, taps, 6000, offsets),
          "unsupported pulse did not preserve exact native fallback");
    check(fallback.report().direct_rows == 2 && !fallback.report().fallback_reason.empty(),
          "direct fallback was not reported");
    rejects([&] { r::MappedBasis bad(settings, plan, crypto, 0, 1); });
    rejects([&] { r::MappedBasis bad(settings, plan, crypto, offsets.size(), 0); });
    rejects([&] { fallback.render_pair({7,0,0,0}, taps, 0, offsets, output); });
    auto bad_offsets = offsets;
    bad_offsets[1] = bad_offsets[0];
    rejects([&] { fallback.render_pair(row, taps, 0, bad_offsets, output); });
    rejects([&] { fallback.render_pair(row, taps, 0, offsets, std::span(output).first(1)); });
    std::stop_source stopped;
    stopped.request_stop();
    rejects([&] { fallback.render_pair(row, taps, 0, offsets, output, stopped.get_token()); });
    // Cancellation and rejected requests must release the admitted call slot.
    fallback.render_pair(row, taps, 6000, offsets, output);
}

bool same_value_bits(std::span<const v::Complex> left, std::span<const v::Complex> right) {
    if (left.size() != right.size()) return false;
    for (std::size_t i = 0; i != left.size(); ++i)
        if (std::bit_cast<std::uint64_t>(left[i].real()) != std::bit_cast<std::uint64_t>(right[i].real()) ||
            std::bit_cast<std::uint64_t>(left[i].imag()) != std::bit_cast<std::uint64_t>(right[i].imag()))
            return false;
    return true;
}

void concurrent_direct_pairs(const datapump::Crypto& crypto, unsigned workers) {
    const auto settings = config();
    auto plan = slot();
    plan.format.inner_rate_hz = 3.6;
    plan.format.bit_seconds = 160.4;
    plan.format.pulse_shape = v::PulseShape::Rrc;
    plan.format.minimum_inner_coefficients = 64;
    plan.format.minimum_carrier_spacing_hz = 265;
    const auto offsets = positions(401);
    const std::array<v::Tap, 2> taps{{{0, .2}, {.007, -.4}}};
    // All rows use one epoch, while timing, sample-rate error, and carrier
    // frequency vary on a waveform with 265 Hz carrier spacing.
    const std::array<r::MappedBasisRow, 4> rows{{
        {0, .011, -1.25, -265.}, {0, .017, -.25, 0.},
        {0, .023, .75, 265.}, {0, .029, 1.75, 530.}}};
    constexpr std::uint64_t first = 250000;
    // This is the 265 Hz, 3.6 Hz startup geometry with a 160.4 s DATA slot.
    // W=126 crossed the former all-or-none exact-source admission boundary.
    r::MappedBasis basis(settings, plan, crypto, offsets.size(), workers);
    const auto expected_sources = std::max<std::size_t>(workers, v::receiver_epoch_offsets.size());
    check(basis.report().exact_source_slots == expected_sources,
          "direct geometry did not admit its exact source pool");
    std::array<std::vector<v::Complex>, rows.size()> serial;
    for (std::size_t i = 0; i != rows.size(); ++i) {
        serial[i].resize(2*taps.size()*offsets.size());
        basis.render_pair(rows[i], taps, first, offsets, serial[i]);
        check(std::ranges::any_of(serial[i], [](auto value) { return value != v::Complex{}; }),
              "concurrent RRC fixture omitted active waveform support");
        check(same_value_bits(serial[i], direct(settings, plan, crypto, rows[i], taps, first, offsets)),
              "serial RRC direct rendering changed native samples");
    }

    std::array<std::future<std::vector<v::Complex>>, rows.size()> work;
    std::promise<void> start;
    const auto ready = start.get_future().share();
    try {
        for (std::size_t i = 0; i != rows.size(); ++i) {
            work[i] = std::async(std::launch::async, [&, i, ready] {
                std::vector<v::Complex> result(2*taps.size()*offsets.size());
                ready.get();
                basis.render_pair(rows[i], taps, first, offsets, result);
                return result;
            });
        }
    } catch (...) {
        start.set_exception(std::current_exception());
        throw;
    }
    start.set_value();
    for (std::size_t i = 0; i != rows.size(); ++i) {
        const auto concurrent = work[i].get();
        check(same_value_bits(concurrent, serial[i]),
              "concurrent same-epoch RRC rendering differs from serial samples");
    }
    const auto report = basis.report();
    check(report.calls == 2*rows.size() && report.direct_rows == 2*report.calls*taps.size() &&
          !report.interpolated_rows && report.fallback_reason == "unsupported-or-abrupt-pulse" &&
          report.exact_source_slots == expected_sources,
          "concurrent direct RRC work lost its receipts or fallback reason");

    std::vector<v::Complex> reused(serial.front().size());
    for (const auto epoch : v::receiver_epoch_offsets) {
        auto row = rows.front();
        row.epoch_offset_seconds = epoch;
        basis.render_pair(row, taps, first, offsets, reused);
        check(same_value_bits(reused, direct(settings, plan, crypto, row, taps, first, offsets)),
              "exact source reuse retained the wrong keyed epoch");
    }
    basis.render_pair(rows.front(), taps, 0, offsets, reused);
    check(same_value_bits(reused, direct(settings, plan, crypto, rows.front(), taps, 0, offsets)),
          "paired exact rendering changed zeros at the source boundary");
}

void partial_exact_source_pool(const datapump::Crypto& crypto) {
    auto plan = slot();
    plan.format.inner_rate_hz = 3.6;
    plan.format.bit_seconds = 160.4;
    plan.format.minimum_inner_coefficients = 900'000;
    plan.format.pulse_shape = v::PulseShape::Rrc;
    plan.format.minimum_carrier_spacing_hz = 265;
    r::MappedBasis basis(config(), plan, crypto, 401, 32);
    const auto report = basis.report();
    check(report.exact_source_slots >= 13 && report.exact_source_slots < 32,
          "large direct geometry did not retain a bounded partial exact source pool");
    check(r::MappedBasis::workspace_bytes(config(), plan, 401, 32) > 2U*1024*1024*1024,
          "large direct geometry test does not exercise a bounded partial pool");
}

struct ViewResult {
    std::vector<v::Complex> values;
    std::weak_ptr<const void> first_tile;
    std::size_t callbacks = 0, mapped_columns = 0, direct_columns = 0, maximum_pins = 0;
    std::string fallback_reason;
};

ViewResult render_view(r::MappedBasis& basis, const r::MappedBasisRow& row,
                       std::span<const v::Tap> taps, std::uint64_t first,
                       std::span<const double> offsets) {
    ViewResult result;
    result.values.resize(2*taps.size()*offsets.size());
    basis.visit_pair(row, taps, first, offsets,
        [&](std::span<const double> coordinates, std::span<const r::BulkMappedColumn> mapped,
            std::span<const r::BulkDirectColumn> direct_columns) {
            ++result.callbacks;
            check(coordinates.data() == offsets.data() && coordinates.size() == offsets.size(),
                  "prepared mapping changed borrowed coordinates");
            std::size_t pins = 0;
            for (const auto& column : mapped) {
                check(column.output_column < 2*taps.size() && !column.tiles.empty(),
                      "prepared mapping column outside destination");
                std::vector<const double*> table;
                for (const auto& tile : column.tiles) {
                    check(tile.owner && tile.samples.size() == 8192,
                          "prepared mapping lost immutable tile owner or extent");
                    if (result.first_tile.expired()) result.first_tile = tile.owner;
                    table.push_back(reinterpret_cast<const double*>(tile.samples.data()));
                    ++pins;
                }
                const TriadMappedRow descriptor{column.fraction, column.scale, column.phase,
                    column.phase_step, column.origin, table.size(), table.data(),
                    reinterpret_cast<double*>(result.values.data()+column.output_column*offsets.size())};
                triad_render_mapped_v1(&descriptor, 1, coordinates.data(), coordinates.size(), 8192);
                ++result.mapped_columns;
            }
            for (const auto& column : direct_columns) {
                check(column.output_column < 2*taps.size() && column.samples.size() == offsets.size() &&
                      !column.reason.empty(), "prepared direct fallback lost its extent or reason");
                std::copy(column.samples.begin(), column.samples.end(),
                          result.values.begin()+static_cast<std::ptrdiff_t>(column.output_column*offsets.size()));
                result.fallback_reason = column.reason;
                ++result.direct_columns;
            }
            check(pins <= 32, "prepared mapping exceeded the synchronous pin bound");
            result.maximum_pins = std::max(result.maximum_pins, pins);
        });
    return result;
}

void prepared_views(const datapump::Crypto& crypto) {
    const auto settings = config();
    const auto offsets = positions(257);
    const std::array<v::Tap, 2> taps{{{0,.2},{.007,-.4}}};
    const r::MappedBasisRow row{2,.013,1.25,127.25};
    std::weak_ptr<const void> released_tile;
    for (const bool dense : {false, true}) {
        auto plan = slot();
        if (dense) plan.format.inner_rate_hz = 3600;
        const auto first = v::geometry(plan.format).calendar_origin_sample+1001;
        r::MappedBasis basis(settings, plan, crypto, offsets.size(), 1);
        auto view = render_view(basis, row, taps, first, offsets);
        check(view.mapped_columns == 4 && !view.direct_columns && view.maximum_pins,
              "supported view unexpectedly rendered a host fallback");
        const auto prepared = basis.report();
        check(prepared.prepared_calls == 1 && prepared.prepared_mapped_columns == 4 &&
              !prepared.interpolated_rows && !prepared.direct_rows && prepared.native_preparation_calls,
              "prepared view rendered CPU basis or lost preparation receipts");
        std::vector<v::Complex> reference(view.values.size());
        basis.render_pair(row, taps, first, offsets, reference);
        check(view.values == reference, "prepared map changed canonical cubic/phase output");
        const auto repeated = render_view(basis, row, taps, first, offsets);
        check(!view.first_tile.owner_before(repeated.first_tile) &&
              !repeated.first_tile.owner_before(view.first_tile), "cached tile identity changed without eviction");
        released_tile = view.first_tile;
        const auto end = v::geometry(plan.format).source_samples+10000;
        const auto empty = render_view(basis, row, taps, end, offsets);
        check(empty.callbacks == 0 && std::all_of(empty.values.begin(), empty.values.end(),
              [](auto value) { return value == v::Complex{}; }), "empty support exposed invalid tiles");
        check(basis.report().prepared_empty_columns == 4, "empty prepared support was not reported");
        rejects([&] { basis.visit_pair(row, taps, first, offsets, {}); });
        rejects([&] {
            basis.visit_pair(row, taps, first, offsets,
                [](auto, auto, auto) { throw datapump::Error("injected synchronous sink failure"); });
        });
        check(render_view(basis, row, taps, first, offsets).values == reference,
              "failed mapping sink retained its admitted call or changed tiles");
    }
    check(released_tile.expired(), "prepared view retained a strong host tile lease after owner close");

    auto plan = slot();
    plan.format.pulse_shape = v::PulseShape::Rrc;
    r::MappedBasis fallback(settings, plan, crypto, offsets.size(), 1);
    const auto direct_view = render_view(fallback, row, taps, 25000, offsets);
    check(direct_view.direct_columns == 4 && !direct_view.mapped_columns &&
          direct_view.fallback_reason == "unsupported-or-abrupt-pulse" &&
          direct_view.values == direct(settings, plan, crypto, row, taps, 25000, offsets),
          "prepared fallback lost exact native mapping or its reason");
    check(fallback.report().prepared_direct_columns == 4 && fallback.report().direct_rows == 4,
          "prepared direct work was not charged");

    plan = slot();
    plan.format.bit_seconds = 50;
    const std::array<v::Tap, 1> one_tap{{{0,0}}};
    const r::MappedBasisRow simple{0,0,0,0};
    const std::array<double, 2> wide_offsets{.17, 200000.21};
    r::MappedBasis wide(settings, plan, crypto, wide_offsets.size(), 1);
    const auto wide_view = render_view(wide, simple, one_tap, 25000, wide_offsets);
    check(wide_view.callbacks == 2 && wide_view.mapped_columns == 2 &&
          wide_view.maximum_pins > 16 && !wide_view.direct_columns,
          "wide map did not split paired pin ownership into bounded callbacks");
    std::vector<v::Complex> wide_cpu(4);
    wide.render_pair(simple, one_tap, 25000, wide_offsets, wide_cpu);
    check(wide_view.values == wide_cpu, "split prepared mapping changed source output");
    const std::array<double, 2> excessive_offsets{.17, 300000.21};
    const auto excessive = render_view(wide, simple, one_tap, 25000, excessive_offsets);
    check(excessive.direct_columns == 2 && excessive.fallback_reason == "pinned-tile-limit",
          "unpreparable map did not report the canonical direct fallback");
}
// The value bits of a long double: the 80-bit x87 format leaves padding.
bool same_bits(long double a, long double b) {
    constexpr std::size_t bytes = std::numeric_limits<long double>::digits == 64 ? 10 : sizeof(long double);
    unsigned char x[sizeof(long double)]{}, y[sizeof(long double)]{};
    std::memcpy(x, &a, sizeof(a));
    std::memcpy(y, &b, sizeof(b));
    return std::memcmp(x, y, bytes) == 0;
}

// The remainder form of the map phases equals std::fmod(x, 1.L) bit for bit,
// including signed zeros, subnormals, values beside the 2^51 limit, huge and
// non-finite values, integers and their neighbours.
void unit_remainders() {
    using limits = std::numeric_limits<long double>;
    std::vector<long double> values{0.L, limits::denorm_min(), limits::min(), limits::min()-limits::denorm_min(),
        .5L, 1-limits::epsilon()/2, 1.L, 1+limits::epsilon(), 1.5L, limits::max(), limits::infinity(),
        limits::quiet_NaN(), std::nextafter(0x1p51L, 0.L), 0x1p51L, std::nextafter(0x1p51L, 0x1p64L),
        0x1p51L+.5L, 0x1p63L, 0x1p64L, 0x1p40L, std::nextafter(0x1p40L, 0.L)};
    for (int k = 0; k <= 70; ++k) {
        const auto power = std::ldexp(1.L, k);
        values.insert(values.end(), {power, std::nextafter(power, 0.L), std::nextafter(power, limits::infinity()),
                                     power+.5L, power-.25L});
    }
    std::mt19937_64 random(0x5eed);
    for (std::size_t i = 0; i != 400000; ++i) {
        // Any finite magnitude, subnormals included.
        const auto mantissa = random() | (UINT64_C(1)<<63);
        const auto exponent = static_cast<int>(random()%32900)-16450;
        values.push_back(std::ldexp(static_cast<long double>(mantissa), exponent-63));
        // Phase-sized values: products of a double and a long double time.
        const auto scale = std::ldexp(1., static_cast<int>(random()%126)-70);
        values.push_back(static_cast<long double>(std::uniform_real_distribution<double>(-1, 1)(random)*scale)*
                         (static_cast<long double>(random()>>11)/0x1p40L));
        // Integers below and beyond the limit, and their neighbours.
        const auto integer = static_cast<long double>(static_cast<std::int64_t>(random()>>(random()%64)));
        values.insert(values.end(), {integer, std::nextafter(integer, 0.L), std::nextafter(integer, 0x1p70L)});
    }
    std::size_t compared = 0;
    for (const auto value : values) {
        for (const auto x : {value, -value}) {
            const auto expected = std::fmod(x, 1.L), actual = v::detail::unit_remainder(x);
            if (!same_bits(actual, expected) && !(std::isnan(x) && std::isnan(actual) && std::isnan(expected)))
                throw std::runtime_error("unit remainder differs from fmod at " + std::to_string(static_cast<double>(x)));
            ++compared;
        }
    }
    check(compared > 2000000, "unit remainder comparison lost its inputs");
    check(std::signbit(v::detail::unit_remainder(-3.L)) && std::signbit(v::detail::unit_remainder(-0.L)) &&
          !std::signbit(v::detail::unit_remainder(3.L)), "unit remainder lost the sign of a zero");
}

// One synchronous sink call as a consumer sees it, without tile leases.
struct SinkCall {
    const double* offsets = nullptr;
    std::size_t rows = 0;
    struct Mapped {
        std::array<std::uint64_t, 4> bits{};
        std::int64_t origin = 0;
        std::size_t output = 0, tiles = 0;
        r::BulkTileAddress address;
        bool operator==(const Mapped& other) const {
            return bits == other.bits && origin == other.origin && output == other.output &&
                   address.first == other.address.first && address.lane == other.address.lane &&
                   address.count == other.address.count;
        }
    };
    struct Direct {
        std::size_t output = 0;
        std::string reason;
        std::vector<std::uint64_t> bits;
        bool operator==(const Direct&) const = default;
    };
    std::vector<Mapped> mapped;
    std::vector<Direct> direct;
    bool operator==(const SinkCall& other) const {
        return offsets == other.offsets && rows == other.rows && mapped == other.mapped && direct == other.direct;
    }
};

struct Visit {
    std::vector<SinkCall> calls;
    std::optional<std::string> error;
};

template<class Call> Visit record(Call call) {
    Visit result;
    const r::BulkMappedSink sink = [&](std::span<const double> offsets, std::span<const r::BulkMappedColumn> mapped,
                                       std::span<const r::BulkDirectColumn> direct_columns) {
        auto& entry = result.calls.emplace_back();
        entry.offsets = offsets.data();
        entry.rows = offsets.size();
        for (const auto& column : mapped)
            entry.mapped.push_back({{std::bit_cast<std::uint64_t>(column.fraction), std::bit_cast<std::uint64_t>(column.scale),
                std::bit_cast<std::uint64_t>(column.phase), std::bit_cast<std::uint64_t>(column.phase_step)},
                column.origin, column.output_column, column.tiles.size(), column.address});
        for (const auto& column : direct_columns) {
            auto& item = entry.direct.emplace_back();
            item.output = column.output_column;
            item.reason = column.reason;
            for (const auto value : column.samples)
                item.bits.insert(item.bits.end(), {std::bit_cast<std::uint64_t>(value.real()),
                                                   std::bit_cast<std::uint64_t>(value.imag())});
        }
    };
    try {
        call(sink);
    } catch (const datapump::Error& error) {
        result.error = error.what();
    }
    return result;
}

// The owner's addressed maps, whose row and block terms are computed once,
// equal the maps of the canonical per-tap form that a prepared visit
// leases tiles for: every column field bit for bit, in the same sink calls,
// with the same exact native fallback columns, failures and receipts.
// Rows, taps and blocks are random, from realistic support to wide phases,
// distant or invalid maps and coordinates beyond 2^48.
void addressed_maps(const datapump::Crypto& crypto) {
    const auto settings = config();
    const auto plan = slot();
    constexpr std::size_t maximum_rows = 64;
    r::MappedBasis canonical(settings, plan, crypto, maximum_rows, 2), owner(settings, plan, crypto, maximum_rows, 2);
    r::MappedBasis lone(settings, plan, crypto, maximum_rows, 2);
    const auto* source = owner.tile_source();
    check(source && source->dense_samples > 8192*8, "addressed map fixture lacks a dense tile source");
    const auto dense = source->dense_samples;
    const r::ConsecutiveBasisOffsets offsets(maximum_rows);
    std::mt19937_64 random(0xadd7e55);
    const auto uniform = [&](double low, double high) { return std::uniform_real_distribution<double>(low, high)(random); };
    const auto magnitude = [&](double low, double high) {
        return (random()%2 ? -1. : 1.)*std::pow(10., uniform(low, high));
    };
    std::size_t compared = 0, mapped = 0, empty = 0, direct = 0, failed = 0, shared = 0;
    r::MappedBasis::Addresses addresses(owner);
    for (std::size_t trial = 0; trial != 160; ++trial) {
        // 0, 1: realistic maps; 2: wide and mostly distant maps; 3: wide
        // maps placed on the source, with phases beyond the native bound.
        const auto mix = trial%4;
        std::vector<v::Tap> taps{{0, 0}};
        if (random()%4) {
            taps.push_back(mix >= 2 ? v::Tap{std::pow(10., uniform(-4, 2)), magnitude(-3, 6)}
                                    : v::Tap{uniform(0, .05), uniform(-100, 100)});
            if (!(random()%5)) taps[0] = {uniform(0, .01), uniform(-30, 30)};
        }
        std::vector<r::MappedBasisRow> rows(1+random()%12);
        for (auto& row : rows) {
            if (mix < 2) row = {std::array<std::int64_t, 3>{-1, 0, 2}[random()%3], uniform(-1, 5), uniform(-60, 60),
                                uniform(-300, 300)};
            else if (mix == 2) row = {static_cast<std::int64_t>(random()%13)-6, magnitude(-3, 5), magnitude(-3, 10),
                                      random()%3 ? uniform(-1e5, 1e5) : (random()%2 ? -999999.5 : 9e5)};
            else row = {static_cast<std::int64_t>(random()%3), std::pow(10., uniform(-3, 6)), magnitude(-3, 10),
                        uniform(-1e3, 1e3)};
            // Requests the canonical form rejects must fail alike.
            if (!(random()%23)) row.clock_ppm = -1e6;
            if (!(random()%29)) row.epoch_offset_seconds = 7;
            if (!(random()%31)) row.arrival_seconds = std::numeric_limits<double>::quiet_NaN();
        }
        std::vector<std::uint64_t> firsts(1+random()%5);
        for (auto& first : firsts) {
            if (mix < 2) first = random()%(dense+4000);
            else if (mix == 2) first = random()>>(random()%64);
            else if (random()%4) {
                // On the source for one of the rows, whatever its arrival.
                const auto arrival = rows[random()%rows.size()].arrival_seconds*settings.sample_rate_hz;
                first = std::isfinite(arrival) ? static_cast<std::uint64_t>(std::llround(arrival)) + random()%dense : 0;
            } else {
                first = std::array<std::uint64_t, 8>{0, 1, 8191, 8192, dense-1, dense, UINT64_C(1)<<48,
                                                     (UINT64_C(1)<<48)+1}[random()%8];
            }
            first = std::min(first, UINT64_MAX-maximum_rows);
        }
        // A repeated block keeps its terms; returning to one recomputes them.
        firsts.push_back(firsts.front());
        const auto count = 1+random()%maximum_rows;
        const auto before = canonical.report(), owner_before = owner.report();
        addresses.open(rows, taps);
        for (const auto first : firsts) {
            for (std::size_t index = 0; index != rows.size(); ++index) {
                const auto row = random()%5 ? index : random()%rows.size();
                const auto expected = record([&](const r::BulkMappedSink& sink) {
                    canonical.visit_pair(rows[row], taps, first, offsets, count, sink);
                });
                const auto actual = record([&](const r::BulkMappedSink& sink) {
                    addresses.address(row, first, offsets, count, sink, {});
                });
                if (!(actual.calls == expected.calls && actual.error == expected.error))
                    throw std::runtime_error("addressed map differs from the canonical map in trial " +
                                             std::to_string(trial));
                for (const auto& call : expected.calls) {
                    for (const auto& column : call.mapped)
                        check(column.tiles == column.address.count, "canonical map lost its tile leases");
                    mapped += call.mapped.size();
                    direct += call.direct.size();
                }
                failed += expected.error.has_value();
                empty += !expected.error && expected.calls.empty();
                ++compared;
                if (!(compared%7)) {
                    // The single-call form is the same path.
                    const auto single = record([&](const r::BulkMappedSink& sink) {
                        lone.address_pair(rows[row], taps, first, offsets, count, sink);
                    });
                    check(single.calls == expected.calls && single.error == expected.error,
                          "single addressed call differs from the canonical map");
                    ++shared;
                }
            }
        }
        addresses.close();
        // Receipts of the owner's calls are credited once, on close.
        const auto after = canonical.report(), owner_after = owner.report();
        check(owner_after.addressed_calls-owner_before.addressed_calls == after.prepared_calls-before.prepared_calls &&
              owner_after.calls-owner_before.calls == after.calls-before.calls &&
              owner_after.addressed_columns-owner_before.addressed_columns ==
                  after.prepared_mapped_columns-before.prepared_mapped_columns &&
              owner_after.prepared_empty_columns-owner_before.prepared_empty_columns ==
                  after.prepared_empty_columns-before.prepared_empty_columns &&
              owner_after.prepared_direct_columns-owner_before.prepared_direct_columns ==
                  after.prepared_direct_columns-before.prepared_direct_columns &&
              owner_after.direct_rows-owner_before.direct_rows == after.direct_rows-before.direct_rows &&
              owner_after.large_phase_fallback_rows-owner_before.large_phase_fallback_rows ==
                  after.large_phase_fallback_rows-before.large_phase_fallback_rows &&
              owner_after.staging_fallback_rows-owner_before.staging_fallback_rows ==
                  after.staging_fallback_rows-before.staging_fallback_rows &&
              !owner_after.cache_misses && !owner_after.native_preparation_calls,
              "addressed receipts differ from the canonical maps");
    }
    check(compared > 2000 && mapped > 1000 && empty > 100 && direct > 20 && failed > 20 && shared > 300,
          "addressed map comparison missed a class of maps");
    std::cout << "addressed maps equal canonical maps: " << compared << " calls, " << mapped << " mapped and "
              << direct << " native columns, " << empty << " empty calls, " << failed << " rejected\n";

    // An open owner holds one admitted call: a basis admitting one worker
    // refuses another call until it closes, also after a failed request.
    r::MappedBasis single(settings, plan, crypto, maximum_rows, 1);
    r::MappedBasis::Addresses held(single);
    const std::array<r::MappedBasisRow, 1> row{{{0, .5, 3, 20}}};
    const std::array<v::Tap, 1> one_tap{{{0, 0}}};
    std::vector<v::Complex> output(2*16);
    const auto rendered_offsets = positions(16);
    held.open(row, one_tap);
    rejects([&] {
        held.address(0, 6000, offsets, 16, [](auto, auto, auto) { throw datapump::Error("injected sink failure"); }, {});
    });
    rejects([&] { single.render_pair(row[0], one_tap, 6000, rendered_offsets, output); });
    rejects([&] { held.address(1, 6000, offsets, 16, [](auto, auto, auto) {}, {}); });
    held.close();
    check(single.report().addressed_calls == 1 && single.report().calls == 1,
          "a failed addressed call was not credited on close");
    single.render_pair(row[0], one_tap, 6000, rendered_offsets, output);

    // A basis without a dense source supplies every column natively, with
    // its reason, on both routes.
    auto unsupported = slot();
    unsupported.format.pulse_shape = v::PulseShape::Rrc;
    r::MappedBasis native(settings, unsupported, crypto, maximum_rows, 1), addressed(settings, unsupported, crypto,
                                                                                     maximum_rows, 1);
    const std::array<v::Tap, 2> two_taps{{{0, 0}, {.007, -.4}}};
    for (const auto first : {UINT64_C(0), UINT64_C(25000)}) {
        const auto expected = record([&](const r::BulkMappedSink& sink) {
            native.visit_pair(row[0], two_taps, first, offsets, 24, sink);
        });
        const auto actual = record([&](const r::BulkMappedSink& sink) {
            addressed.address_pair(row[0], two_taps, first, offsets, 24, sink);
        });
        check(actual.calls == expected.calls && !actual.error && actual.calls.size() == 2 &&
              actual.calls[0].direct.size() == 2 && actual.calls[0].direct[0].reason == "unsupported-or-abrupt-pulse",
              "addressed map lost the native fallback of an unsupported source");
    }
    check(addressed.report().prepared_direct_columns == native.report().prepared_direct_columns &&
          addressed.report().direct_rows == native.report().direct_rows &&
          addressed.report().addressed_calls == native.report().prepared_calls,
          "addressed native fallback receipts differ");
}

} // namespace

int main() {
    try {
        const auto crypto = key();
        cached_maps(crypto);
        concurrent_pairs(crypto);
        dense_and_gated(crypto);
        fallback_and_validation(crypto);
        concurrent_direct_pairs(crypto, 4);
        concurrent_direct_pairs(crypto, 126);
        partial_exact_source_pool(crypto);
        prepared_views(crypto);
        unit_remainders();
        addressed_maps(crypto);
        std::cout << "native mapped receiver basis passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

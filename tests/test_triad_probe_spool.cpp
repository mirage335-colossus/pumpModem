#include "triad_probe_capture_cache.hpp"
#include "triad_probe_spool.hpp"

#include <bit>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <iostream>
#include <vector>

namespace {
using datapump::triad::probe::CaptureCacheReader;
using datapump::triad::probe::CaptureCacheWriter;
using datapump::triad::probe::CaptureRecord;
using datapump::triad::probe::ObservationSpool;
using Complex = ObservationSpool::Complex;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Expected, class F>
void rejects(F&& operation) {
    try { operation(); } catch (const Expected&) { return; }
    throw std::runtime_error("spool accepted invalid input, state, or I/O result");
}

void roundtrip_and_limits() {
    constexpr std::size_t rows = ObservationSpool::maximum_chunk_rows + 8;
    constexpr auto quota = rows * ObservationSpool::row_bytes;
    rejects<std::invalid_argument>([]{ ObservationSpool empty(0, 0); });
    rejects<std::invalid_argument>([]{ ObservationSpool undersized(rows, quota - 1); });
    rejects<std::invalid_argument>([]{ ObservationSpool overflow(UINT64_MAX, UINT64_MAX); });

    std::vector<Complex> samples(rows), replay(rows);
    std::vector<std::uint8_t> mask(rows), replay_mask(rows);
    for (std::uint64_t i = 0; i < rows; ++i) {
        // Exercise exact binary64 preservation, including signed zero,
        // multiple NaN payloads, and infinity, without floating comparisons.
        const std::array<std::uint64_t, 2> raw{
            i % 2 ? 0x7ff8000000000000ULL + i : 0x8000000000000000ULL,
            i % 3 ? 0x3fd0000000000000ULL + i : 0xfff0000000000000ULL};
        samples[i] = std::bit_cast<Complex>(raw);
        mask[i] = static_cast<std::uint8_t>(i);
    }

    ObservationSpool spool(rows, quota);
    check(spool.working_bytes() < 128 * 1024, "spool did not fit the fixed staging budget");
    const auto reservation = spool.resident_reservation_bytes();
    const std::string_view backing = spool.backing_kind();
    check(spool.bytes() == 0 && (backing == "known_disk" ? reservation == 0 : reservation >= quota),
        "spool did not reserve its planned backing before the first append");
    rejects<std::logic_error>([&]{ spool.rewind(); });
    rejects<std::logic_error>([&]{ spool.seal(); });
    rejects<std::logic_error>([&]{ spool.read(replay, replay_mask); });
    rejects<std::invalid_argument>([&]{ spool.append({}, {}); });
    rejects<std::invalid_argument>([&]{ spool.append(std::span(samples).first(1), {}); });
    rejects<std::invalid_argument>([&]{ spool.append(samples, mask); });
    std::size_t cursor = 0;
    for (const auto count : std::array<std::size_t, 3>{7, ObservationSpool::maximum_chunk_rows, 1}) {
        spool.append(std::span(samples).subspan(cursor, count), std::span(mask).subspan(cursor, count));
        cursor += count;
    }
    rejects<std::invalid_argument>([&]{ spool.append(std::span(samples).first(1), std::span(mask).first(1)); });
    spool.seal();
    rejects<std::logic_error>([&]{ spool.seal(); });
    rejects<std::logic_error>([&]{ spool.append({}, {}); });
    rejects<std::logic_error>([&]{ spool.read(std::span(replay).first(1), std::span(replay_mask).first(1)); });
    for (int pass = 0; pass < 2; ++pass) {
        spool.rewind();
        rejects<std::invalid_argument>([&]{ spool.read({}, {}); });
        rejects<std::invalid_argument>([&]{ spool.read(replay, replay_mask); });
        cursor = 0;
        for (const auto count : std::array<std::size_t, 3>{ObservationSpool::maximum_chunk_rows, 3, 5}) {
            spool.read(std::span(replay).subspan(cursor, count), std::span(replay_mask).subspan(cursor, count));
            cursor += count;
        }
        rejects<std::invalid_argument>([&]{ spool.read(std::span(replay).first(1), std::span(replay_mask).first(1)); });
        check(spool.rows() == rows && spool.bytes() == quota && spool.resident_reservation_bytes() == reservation &&
            replay_mask == mask &&
            std::memcmp(replay.data(), samples.data(), samples.size() * sizeof(Complex)) == 0,
            "spool did not preserve I/Q and mask bits across chunk boundaries");
    }
}

void retain_complete_capture() {
    namespace fs = std::filesystem;
    struct TemporaryDirectory {
        fs::path path = fs::temp_directory_path() / ("datapump-spool-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() { check(fs::create_directory(path), "could not create spool test directory"); }
        ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
    } directory;
    const auto path = (directory.path / "retained.iq").string();
    const auto partial_path = (directory.path / "partial.iq").string();
    constexpr auto bytes = ObservationSpool::row_bytes;
    const std::array sample{Complex{1.25, -2.5}};
    const std::array<std::uint8_t, 1> mask{1};
    {
        ObservationSpool partial(2, 2 * bytes, partial_path);
        partial.append(sample, mask);
        rejects<std::logic_error>([&] { partial.seal(); });
        rejects<std::logic_error>([&] { partial.rewind(); });
    }
    check(fs::file_size(partial_path) == bytes, "unverified explicit output must remain under caller ownership");
    {
        ObservationSpool complete(1, bytes, path);
        complete.append(sample, mask);
        complete.seal();
        complete.rewind();
        std::array<Complex, 1> iq{};
        std::array<std::uint8_t, 1> valid{};
        complete.read(iq, valid);
        check(iq == sample && valid == mask, "the named spool did not replay its sealed row");
    }
    rejects<std::runtime_error>([&] { ObservationSpool existing(1, bytes, path); });
    std::ifstream retained(path, std::ios::binary);
    std::array<char, bytes> row{};
    check(retained.read(row.data(), row.size()).gcount() == row.size() && retained.peek() == EOF &&
        std::memcmp(row.data(), sample.data(), sizeof(Complex)) == 0 && row.back() == mask.front(),
        "exclusive create altered the exact retained I/Q and validity bytes");
    const auto read_only = ObservationSpool::OpenMode::retained_read_only;
    ObservationSpool reopened(1, bytes, path, read_only);
    reopened.rewind();
    std::array<Complex, 1> iq{};
    std::array<std::uint8_t, 1> valid{};
    reopened.read(iq, valid);
    check(iq == sample && valid == mask, "retained read-only replay differs from its original spool");
    rejects<std::logic_error>([&] { reopened.append(sample, mask); });
    rejects<std::invalid_argument>([&] { ObservationSpool short_file(2, 2 * bytes, partial_path, read_only); });
    {
        std::ofstream trailing(partial_path, std::ios::binary | std::ios::app);
        trailing.put('x');
        check(bool(trailing), "could not make a replay file with a trailing byte");
    }
    rejects<std::invalid_argument>([&] { ObservationSpool long_file(1, bytes, partial_path, read_only); });
}

// A capture cache file round-trips its header and rows bit for bit, and a
// reader rejects every changed header byte, size or validity byte, and a
// resealed header that is not canonical.
void capture_cache_file() {
    namespace fs = std::filesystem;
    struct TemporaryDirectory {
        fs::path path = fs::temp_directory_path() / ("datapump-capture-cache-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryDirectory() { check(fs::create_directory(path), "could not create cache test directory"); }
        ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
    } directory;
    constexpr std::uint64_t rows = ObservationSpool::maximum_chunk_rows + 5;
    std::vector<Complex> samples(rows);
    std::vector<std::uint8_t> mask(rows);
    for (std::uint64_t i = 0; i < rows; ++i) {
        samples[i] = std::bit_cast<Complex>(std::array<std::uint64_t, 2>{0x8000000000000000ULL + i, 0x7ff8000000000001ULL + i});
        mask[i] = static_cast<std::uint8_t>(i % 3 != 0);
    }
    CaptureRecord record;
    for (std::size_t i = 0; i < 32; ++i) { record.key[i] = static_cast<std::uint8_t>(i); record.digest[i] = static_cast<std::uint8_t>(255 - i); }
    record.first = 7;
    record.rows = rows;
    record.generated = 7 + rows;
    record.erased_samples = 3;
    record.working_bytes = 123456;
    record.selective_filter = true;
    record.bounded_spread = CaptureRecord::BoundedSpread{.01, 1, 90, .5, std::nullopt, .25, -89, 88, -150, 140, 99, rows};
    record.common_doppler = CaptureRecord::CommonDoppler{90, 1, -0.0, 89.5, 7, rows};
    const auto encoded = record.encode();
    const auto decoded = CaptureRecord::decode(encoded);
    check(decoded.encode() == encoded && decoded.bounded_spread && !decoded.bounded_spread->maximum_echo_ratio &&
          std::signbit(decoded.common_doppler->minimum_hz) && decoded.erased_samples == 3 &&
          decoded.working_bytes == 123456, "capture cache header did not round-trip exactly");
    // An absent value with a valid header SHA-256 is still rejected: offset
    // 192 holds the absent maximum echo ratio.
    {
        auto resealed = encoded;
        resealed[192] = 1;
        CaptureRecord::seal(resealed);
        rejects<std::invalid_argument>([&]{ (void)CaptureRecord::decode(resealed); });
    }
    const auto path = (directory.path / "a.capture").string();
    {
        CaptureCacheWriter writer(path + ".tmp", path, rows);
        rejects<std::logic_error>([&]{ writer.commit(record); });
        writer.append(std::span(samples).first(ObservationSpool::maximum_chunk_rows),
                      std::span(mask).first(ObservationSpool::maximum_chunk_rows));
        writer.append(std::span(samples).subspan(ObservationSpool::maximum_chunk_rows),
                      std::span(mask).subspan(ObservationSpool::maximum_chunk_rows));
        rejects<std::invalid_argument>([&]{ writer.append(std::span(samples).first(1), std::span(mask).first(1)); });
        writer.commit(record);
    }
    check(fs::file_size(path) == record.file_bytes() && !fs::exists(path + ".tmp"), "capture cache file has another size");
    {
        // An abandoned writer leaves no file behind.
        CaptureCacheWriter abandoned(path + ".tmp2", path + ".never", rows);
    }
    check(!fs::exists(path + ".tmp2") && !fs::exists(path + ".never"), "an incomplete capture cache file remained");
    const auto limit = record.file_bytes();
    const auto read_all = [&](const std::string& file, std::uint64_t byte_limit) {
        auto reader = CaptureCacheReader::open(file);
        check(reader.has_value(), "capture cache file did not open");
        reader->header(record.key, record.first, rows, byte_limit);
        std::vector<Complex> iq(rows);
        std::vector<std::uint8_t> valid(rows);
        for (std::uint64_t at = 0; at < rows;) {
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(rows - at, ObservationSpool::maximum_chunk_rows));
            reader->read_rows(std::span(iq).subspan(at, n), std::span(valid).subspan(at, n));
            at += n;
        }
        reader->finish();
        check(valid == mask && std::memcmp(iq.data(), samples.data(), rows * sizeof(Complex)) == 0,
              "capture cache rows did not round-trip exactly");
    };
    read_all(path, limit);
    check(!CaptureCacheReader::open(path + ".missing"), "a missing capture cache file opened");
    rejects<std::invalid_argument>([&]{ read_all(path, limit - 1); });
    rejects<std::invalid_argument>([&]{
        auto reader = CaptureCacheReader::open(path);
        auto other = record.key; other[0] ^= 1;
        reader->header(other, record.first, rows, limit);
    });
    rejects<std::invalid_argument>([&]{ CaptureCacheReader::open(path)->header(record.key, record.first + 1, rows, limit); });
    rejects<std::invalid_argument>([&]{ CaptureCacheReader::open(path)->header(record.key, record.first, rows - 1, limit); });
    const auto original = [&] {
        std::ifstream in(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(in), {});
    }();
    const auto variant = [&](auto change) {
        auto bytes = original;
        change(bytes);
        const auto file = (directory.path / "variant.capture").string();
        std::ofstream(file, std::ios::binary | std::ios::trunc).write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        return file;
    };
    // Every header byte, among them the report fields that only the header's
    // SHA-256 protects (erased rows at 136, workspace at 144, excess delay at
    // 160); then a validity byte, a missing row byte and a trailing byte.
    for (std::size_t at = 0; at < CaptureRecord::header_bytes; ++at)
        for (const std::uint8_t bit : {std::uint8_t{0x01}, std::uint8_t{0x80}})
            rejects<std::invalid_argument>([&]{ read_all(variant([&](auto& b){ b[at] ^= static_cast<char>(bit); }), limit); });
    rejects<std::invalid_argument>([&]{
        read_all(variant([&](auto& b){ b[CaptureRecord::header_bytes + 2 * CaptureRecord::row_bytes - 1] = 2; }), limit); });
    rejects<std::invalid_argument>([&]{ read_all(variant([](auto& b){ b.pop_back(); }), limit); });
    rejects<std::invalid_argument>([&]{ read_all(variant([](auto& b){ b.push_back(0); }), limit + 1); });
    rejects<std::invalid_argument>([&]{ read_all(variant([](auto& b){ b.resize(100); }), limit); });
}

#ifdef DATAPUMP_TEST_SPOOL_IO_WRAPPERS
enum class Fault { none, write, read, flush, seek };
Fault pending = Fault::none;

bool inject(Fault fault) {
    if (pending != fault) return false;
    pending = Fault::none;
    return true;
}

void short_io_is_terminal() {
    const std::array<Complex, 1> sample{Complex{12, 23}};
    const std::array<std::uint8_t, 1> mask{1};
    for (const auto fault : {Fault::write, Fault::read, Fault::flush, Fault::seek}) {
        ObservationSpool spool(1, ObservationSpool::row_bytes);
        if (fault == Fault::write) {
            pending = fault;
            rejects<std::runtime_error>([&]{ spool.append(sample, mask); });
            check(spool.rows() == 0 && spool.bytes() == 0, "partial write committed a row");
            rejects<std::logic_error>([&]{ spool.append(sample, mask); });
            continue;
        }
        spool.append(sample, mask);
        if (fault == Fault::flush) {
            pending = fault;
            rejects<std::runtime_error>([&]{ spool.seal(); });
            rejects<std::logic_error>([&]{ spool.seal(); });
            continue;
        }
        spool.seal();
        if (fault == Fault::seek) {
            pending = fault;
            rejects<std::runtime_error>([&]{ spool.rewind(); });
            rejects<std::logic_error>([&]{ spool.rewind(); });
            continue;
        }
        spool.rewind();
        std::array<Complex, 1> output{Complex{1, 2}};
        std::array<std::uint8_t, 1> output_mask{17};
        pending = fault;
        rejects<std::runtime_error>([&]{ spool.read(output, output_mask); });
        check(output[0] == Complex{1, 2} && output_mask[0] == 17, "partial read changed output");
        rejects<std::logic_error>([&]{ spool.rewind(); });
    }
}
#endif
} // namespace

#ifdef DATAPUMP_TEST_SPOOL_IO_WRAPPERS
extern "C" {
std::size_t __real_fwrite(const void*, std::size_t, std::size_t, std::FILE*);
std::size_t __wrap_fwrite(const void* data, std::size_t size, std::size_t count, std::FILE* file) {
    return __real_fwrite(data, size, count - (count && inject(Fault::write)), file);
}
std::size_t __real_fread(void*, std::size_t, std::size_t, std::FILE*);
std::size_t __wrap_fread(void* data, std::size_t size, std::size_t count, std::FILE* file) {
    return __real_fread(data, size, count - (count && inject(Fault::read)), file);
}
int __real_fflush(std::FILE*);
int __wrap_fflush(std::FILE* file) { return inject(Fault::flush) ? EOF : __real_fflush(file); }
int __real_fseek(std::FILE*, long, int);
int __wrap_fseek(std::FILE* file, long offset, int direction) {
    return inject(Fault::seek) ? -1 : __real_fseek(file, offset, direction);
}
}
#endif

int main() {
    try {
        roundtrip_and_limits();
        retain_complete_capture();
        capture_cache_file();
#ifdef DATAPUMP_TEST_SPOOL_IO_WRAPPERS
        short_io_is_terminal();
#endif
        std::cout << "triad observation spool checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

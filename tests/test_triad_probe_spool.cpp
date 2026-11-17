#include "triad_probe_spool.hpp"

#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
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
#ifdef DATAPUMP_TEST_SPOOL_IO_WRAPPERS
        short_io_is_terminal();
#endif
        std::cout << "triad observation spool checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

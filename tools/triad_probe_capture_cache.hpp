#pragma once

#include "triad_probe_spool.hpp"

#include <openssl/evp.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace datapump::triad::probe {

// An exact copy of one generated probe capture, kept for later receiver runs
// of the same probe binary with the same simulator arguments. The file holds
// a fixed little-endian header, then the capture's rows in the spool's row
// layout: the native binary64 I/Q pair and the validity byte. The key binds
// the binary's identity and the normalized arguments, so a file serves only
// the binary and arguments that wrote it. The stored digest is the capture's
// input_sha256 over the rows. The header's own SHA-256, taken with its field
// zeroed, covers every other header byte, including that digest and the
// report fields. A reader checks the header's SHA-256 before it uses a field,
// and the probe recomputes the rows' SHA-256 before any receiver work, so a
// file that differs in any byte, size or row is rejected. This checks
// integrity, not provenance: whoever can write the directory can replace a
// capture together with its digests, so keep the directory private.
struct CaptureRecord {
    static constexpr std::array<char, 8> magic{'D', 'P', 'T', 'R', 'C', 'A', 'P', 'T'};
    static constexpr std::uint32_t version = 2;
    static constexpr std::size_t header_bytes = 304;
    // The header's SHA-256 follows the magic, version, size, key and digest.
    static constexpr std::size_t header_digest_offset = 80;
    static constexpr std::size_t row_bytes = ObservationSpool::row_bytes;
    using Header = std::array<std::uint8_t, header_bytes>;

    struct BoundedSpread {
        double excess_delay_seconds = 0, knot_seconds = 0, doppler_bound_hz = 0;
        std::optional<double> minimum_echo_ratio, maximum_echo_ratio, fixed_direct_power_fraction;
        double minimum_doppler_hz = 0, maximum_doppler_hz = 0;
        double minimum_combined_physical_doppler_hz = 0, maximum_combined_physical_doppler_hz = 0;
        std::uint64_t seed = 0, processed_rows = 0;
    };
    struct CommonDoppler {
        double bound_hz = 0, knot_seconds = 0, minimum_hz = 0, maximum_hz = 0;
        std::uint64_t seed = 0, processed_rows = 0;
    };

    std::array<std::uint8_t, 32> key{}, digest{};
    std::uint64_t first = 0, rows = 0, generated = 0;
    // The channel report fields that the probe's receipts read.
    std::uint64_t erased_samples = 0, working_bytes = 0;
    bool selective_filter = false;
    std::optional<BoundedSpread> bounded_spread;
    std::optional<CommonDoppler> common_doppler;

    std::uint64_t file_bytes() const {
        if (rows > (std::numeric_limits<std::uint64_t>::max() - header_bytes) / row_bytes)
            throw std::invalid_argument("capture cache extent overflows");
        return header_bytes + rows * row_bytes;
    }

    // The SHA-256 of a header with its own digest field zeroed.
    static std::array<std::uint8_t, 32> header_sha256(std::span<const std::uint8_t, header_bytes> header) {
        Header zeroed;
        std::memcpy(zeroed.data(), header.data(), header_bytes);
        std::memset(zeroed.data() + header_digest_offset, 0, 32);
        std::array<std::uint8_t, 32> out{};
        unsigned size = 0;
        if (EVP_Digest(zeroed.data(), zeroed.size(), out.data(), &size, EVP_sha256(), nullptr) != 1 ||
            size != out.size())
            throw std::runtime_error("could not hash a capture cache header");
        return out;
    }
    // Stores the header's SHA-256 in its field.
    static void seal(std::span<std::uint8_t, header_bytes> header) {
        const auto sum = header_sha256(header);
        std::memcpy(header.data() + header_digest_offset, sum.data(), sum.size());
    }

    Header encode() const {
        Header out{};
        std::size_t at = 0;
        const auto bytes = [&](const void* data, std::size_t size) {
            std::memcpy(out.data() + at, data, size);
            at += size;
        };
        const auto u64 = [&](std::uint64_t value) {
            for (unsigned i = 0; i < 8; ++i) out[at++] = static_cast<std::uint8_t>(value >> (8 * i));
        };
        const auto f64 = [&](double value) { u64(std::bit_cast<std::uint64_t>(value)); };
        bytes(magic.data(), magic.size());
        for (unsigned i = 0; i < 4; ++i) out[at++] = static_cast<std::uint8_t>(version >> (8 * i));
        for (unsigned i = 0; i < 4; ++i) out[at++] = static_cast<std::uint8_t>(header_bytes >> (8 * i));
        bytes(key.data(), key.size());
        bytes(digest.data(), digest.size());
        if (at != header_digest_offset) throw std::logic_error("capture cache header layout");
        at += 32;
        u64(first); u64(rows); u64(generated); u64(erased_samples); u64(working_bytes);
        const auto& spread = bounded_spread;
        u64((selective_filter ? 1U : 0U) | (spread ? 2U : 0U) | (common_doppler ? 4U : 0U) |
            (spread && spread->minimum_echo_ratio ? 8U : 0U) | (spread && spread->maximum_echo_ratio ? 16U : 0U) |
            (spread && spread->fixed_direct_power_fraction ? 32U : 0U));
        const BoundedSpread b = spread.value_or(BoundedSpread{});
        f64(b.excess_delay_seconds); f64(b.knot_seconds); f64(b.doppler_bound_hz);
        f64(b.minimum_echo_ratio.value_or(0)); f64(b.maximum_echo_ratio.value_or(0));
        f64(b.fixed_direct_power_fraction.value_or(0));
        f64(b.minimum_doppler_hz); f64(b.maximum_doppler_hz);
        f64(b.minimum_combined_physical_doppler_hz); f64(b.maximum_combined_physical_doppler_hz);
        u64(b.seed); u64(b.processed_rows);
        const CommonDoppler c = common_doppler.value_or(CommonDoppler{});
        f64(c.bound_hz); f64(c.knot_seconds); f64(c.minimum_hz); f64(c.maximum_hz);
        u64(c.seed); u64(c.processed_rows);
        if (at != header_bytes) throw std::logic_error("capture cache header layout");
        seal(out);
        return out;
    }

    // A header exactly as encode() writes it. Its SHA-256 must match before
    // any field is read, and absent sections and flags must be zero, so every
    // accepted header has one encoding.
    static CaptureRecord decode(std::span<const std::uint8_t, header_bytes> in) {
        std::size_t at = 0;
        const auto u32 = [&] {
            std::uint32_t value = 0;
            for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t{in[at++]} << (8 * i);
            return value;
        };
        const auto u64 = [&] {
            std::uint64_t value = 0;
            for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{in[at++]} << (8 * i);
            return value;
        };
        const auto f64 = [&] { return std::bit_cast<double>(u64()); };
        if (std::memcmp(in.data(), magic.data(), magic.size()) != 0)
            throw std::invalid_argument("capture cache file has no capture header");
        at = magic.size();
        if (u32() != version) throw std::invalid_argument("capture cache file has an unsupported version");
        if (u32() != header_bytes) throw std::invalid_argument("capture cache header has another size");
        if (header_sha256(in) != [&] {
                std::array<std::uint8_t, 32> stored{};
                std::memcpy(stored.data(), in.data() + header_digest_offset, stored.size());
                return stored;
            }())
            throw std::invalid_argument("capture cache header differs from its SHA-256");
        CaptureRecord record;
        std::memcpy(record.key.data(), in.data() + at, record.key.size());
        at += record.key.size();
        std::memcpy(record.digest.data(), in.data() + at, record.digest.size());
        at += record.digest.size() + 32;
        record.first = u64(); record.rows = u64(); record.generated = u64();
        record.erased_samples = u64(); record.working_bytes = u64();
        const auto flags = u64();
        if (flags & ~std::uint64_t{63} || (!(flags & 2) && (flags & 56)))
            throw std::invalid_argument("capture cache header has unknown or inconsistent flags");
        record.selective_filter = flags & 1;
        BoundedSpread b;
        b.excess_delay_seconds = f64(); b.knot_seconds = f64(); b.doppler_bound_hz = f64();
        const auto minimum_ratio = f64(), maximum_ratio = f64(), fixed_direct = f64();
        b.minimum_doppler_hz = f64(); b.maximum_doppler_hz = f64();
        b.minimum_combined_physical_doppler_hz = f64(); b.maximum_combined_physical_doppler_hz = f64();
        b.seed = u64(); b.processed_rows = u64();
        if (flags & 8) b.minimum_echo_ratio = minimum_ratio;
        if (flags & 16) b.maximum_echo_ratio = maximum_ratio;
        if (flags & 32) b.fixed_direct_power_fraction = fixed_direct;
        CommonDoppler c;
        c.bound_hz = f64(); c.knot_seconds = f64(); c.minimum_hz = f64(); c.maximum_hz = f64();
        c.seed = u64(); c.processed_rows = u64();
        if (flags & 2) record.bounded_spread = b;
        if (flags & 4) record.common_doppler = c;
        if (record.generated < record.rows || record.generated - record.rows != record.first)
            throw std::invalid_argument("capture cache header extent is inconsistent");
        const auto expected = record.encode();
        if (std::memcmp(expected.data(), in.data(), header_bytes) != 0)
            throw std::invalid_argument("capture cache header is not canonical");
        return record;
    }
};

// Writes one record to a private temporary file in the cache directory and
// renames it into place when complete. Rows arrive in order, in chunks of at
// most ObservationSpool::maximum_chunk_rows. On POSIX systems the temporary
// file is created exclusively with owner-only permissions; on Windows it
// inherits the directory's access control. A run that is killed can leave its
// temporary file, which a later run never reads.
class CaptureCacheWriter {
public:
    // Staging and stdio buffer, live while the capture is delivered; the
    // probe's generation preflight charges them.
    static constexpr std::size_t stdio_buffer_bytes = 32 * 1024;
    static constexpr std::size_t buffer_bytes =
        ObservationSpool::maximum_chunk_rows * CaptureRecord::row_bytes + stdio_buffer_bytes;
    CaptureCacheWriter(std::string temporary, std::string final_path, std::uint64_t rows)
        : temporary_(std::move(temporary)), final_(std::move(final_path)), expected_rows_(rows) {
#ifdef _WIN32
        file_.reset(std::fopen(temporary_.c_str(), "wbx"));
#else
        // Readable by the owner alone, created exclusively.
        const int descriptor = ::open(temporary_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (descriptor >= 0) {
            file_.reset(::fdopen(descriptor, "wb"));
            if (!file_) (void)::close(descriptor);
        }
#endif
        if (!file_) throw std::runtime_error("could not create a capture cache file");
        created_ = true;
        if (std::setvbuf(file_.get(), buffer_.data(), _IOFBF, buffer_.size()) != 0) fail("capture cache buffer");
        const std::array<std::uint8_t, CaptureRecord::header_bytes> placeholder{};
        write(placeholder.data(), placeholder.size());
    }
    CaptureCacheWriter(const CaptureCacheWriter&) = delete;
    CaptureCacheWriter& operator=(const CaptureCacheWriter&) = delete;
    ~CaptureCacheWriter() {
        file_.reset();
        if (created_ && !renamed_) (void)std::remove(temporary_.c_str());
    }

    void append(std::span<const v3::Complex> samples, std::span<const std::uint8_t> validity) {
        if (samples.size() != validity.size() || samples.size() > ObservationSpool::maximum_chunk_rows ||
            samples.size() > expected_rows_ - rows_)
            throw std::invalid_argument("capture cache append exceeds its rows");
        for (std::size_t i = 0; i < samples.size(); ++i) {
            auto* row = staging_.data() + i * CaptureRecord::row_bytes;
            std::memcpy(row, &samples[i], sizeof(v3::Complex));
            row[sizeof(v3::Complex)] = validity[i];
        }
        write(staging_.data(), samples.size() * CaptureRecord::row_bytes);
        rows_ += samples.size();
    }

    // Writes the header and moves the complete file into place.
    void commit(const CaptureRecord& record) {
        if (record.rows != expected_rows_ || rows_ != expected_rows_)
            throw std::logic_error("capture cache file is incomplete");
        const auto header = record.encode();
        if (std::fflush(file_.get()) != 0 || std::fseek(file_.get(), 0, SEEK_SET) != 0) fail("capture cache header");
        write(header.data(), header.size());
        if (std::fflush(file_.get()) != 0 || std::ferror(file_.get())) fail("capture cache flush");
        file_.reset();
        // Replaces a file that a concurrent run of the same key completed
        // first; both hold the same capture.
#ifdef _WIN32
        if (!MoveFileExA(temporary_.c_str(), final_.c_str(), MOVEFILE_REPLACE_EXISTING)) fail("capture cache rename");
#else
        if (std::rename(temporary_.c_str(), final_.c_str()) != 0) fail("capture cache rename");
#endif
        renamed_ = true;
    }

private:
    void write(const void* data, std::size_t size) {
        if (std::fwrite(data, 1, size, file_.get()) != size || std::ferror(file_.get())) fail("capture cache write");
    }
    [[noreturn]] static void fail(const char* what) { throw std::runtime_error(std::string("could not complete ") + what); }
    struct CloseFile {
        void operator()(std::FILE* file) const noexcept { (void)std::fclose(file); }
    };
    std::string temporary_, final_;
    std::uint64_t expected_rows_ = 0, rows_ = 0;
    bool created_ = false, renamed_ = false;
    std::vector<std::uint8_t> staging_ = std::vector<std::uint8_t>(ObservationSpool::maximum_chunk_rows * CaptureRecord::row_bytes);
    std::vector<char> buffer_ = std::vector<char>(stdio_buffer_bytes);
    // Close before destroying the borrowed stdio buffer.
    std::unique_ptr<std::FILE, CloseFile> file_;
};

// Reads a record whose key, extent and rows the caller declares. The file
// never sizes an allocation: a header that differs from the declaration, a
// validity byte other than zero or one, or any byte past the declared rows
// rejects the file.
class CaptureCacheReader {
public:
    // Nothing when the file does not exist.
    static std::optional<CaptureCacheReader> open(const std::string& path) {
        std::unique_ptr<std::FILE, CloseFile> file(std::fopen(path.c_str(), "rb"));
        if (!file) return std::nullopt;
        return CaptureCacheReader(std::move(file));
    }
    CaptureCacheReader(CaptureCacheReader&&) noexcept = default;

    const CaptureRecord& header(const std::array<std::uint8_t, 32>& key, std::uint64_t first, std::uint64_t rows,
                                std::uint64_t byte_limit) {
        std::array<std::uint8_t, CaptureRecord::header_bytes> raw{};
        read(raw.data(), raw.size());
        record_ = CaptureRecord::decode(raw);
        if (record_.key != key) throw std::invalid_argument("capture cache file belongs to another key");
        if (record_.first != first || record_.rows != rows)
            throw std::invalid_argument("capture cache file holds another extent");
        if (record_.file_bytes() > byte_limit)
            throw std::invalid_argument("capture cache file exceeds the probe's sample or spool budget");
        return record_;
    }

    // The next rows, in order, into caller-owned spans of one chunk.
    void read_rows(std::span<v3::Complex> samples, std::span<std::uint8_t> validity) {
        if (samples.size() != validity.size() || samples.size() > ObservationSpool::maximum_chunk_rows ||
            samples.size() > record_.rows - rows_)
            throw std::invalid_argument("capture cache read exceeds its rows");
        read(staging_.data(), samples.size() * CaptureRecord::row_bytes);
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto* row = staging_.data() + i * CaptureRecord::row_bytes;
            std::memcpy(&samples[i], row, sizeof(v3::Complex));
            validity[i] = row[sizeof(v3::Complex)];
            if (validity[i] > 1) throw std::invalid_argument("capture cache file has an invalid validity byte");
        }
        rows_ += samples.size();
    }

    // After every row: nothing may follow them.
    void finish() {
        if (rows_ != record_.rows) throw std::logic_error("capture cache file was not read completely");
        if (std::fgetc(file_.get()) != EOF || std::ferror(file_.get()))
            throw std::invalid_argument("capture cache file is longer than its rows");
    }

private:
    struct CloseFile {
        void operator()(std::FILE* file) const noexcept { (void)std::fclose(file); }
    };
    explicit CaptureCacheReader(std::unique_ptr<std::FILE, CloseFile> file) : file_(std::move(file)) {}
    void read(void* data, std::size_t size) {
        if (std::fread(data, 1, size, file_.get()) != size || std::ferror(file_.get()))
            throw std::invalid_argument("capture cache file is shorter than its header declares");
    }
    std::vector<std::uint8_t> staging_ = std::vector<std::uint8_t>(ObservationSpool::maximum_chunk_rows * CaptureRecord::row_bytes);
    std::unique_ptr<std::FILE, CloseFile> file_;
    CaptureRecord record_;
    std::uint64_t rows_ = 0;
};

} // namespace datapump::triad::probe

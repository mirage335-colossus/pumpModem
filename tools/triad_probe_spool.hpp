#pragma once

#include "datapump/triad_modem_v3.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#ifdef __linux__
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/vfs.h>
#endif
#endif

namespace datapump::triad::probe {

// Stores one native I/Q pair and its validity byte per row in a private
// temporary file. Normal destruction closes and removes incomplete captures
// too. The internal representation is local to this process.
class ObservationSpool {
public:
    using Complex = v3::Complex;
    static constexpr std::size_t row_bytes = 17;
    static constexpr std::size_t maximum_chunk_rows = 4096;

    ObservationSpool(std::uint64_t expected_rows, std::uint64_t max_bytes)
        : expected_rows_(expected_rows) {
        if (expected_rows_ == 0 || expected_rows_ > max_bytes / row_bytes)
            throw std::invalid_argument("observation spool exceeds its byte quota or has no rows");
        file_.reset(open_temporary_file());
        if (!file_) throw std::runtime_error("could not create the observation spool");
        if (std::setvbuf(file_.get(), stdio_buffer_.data(), _IOFBF, stdio_buffer_.size()) != 0)
            throw std::runtime_error("could not install the observation spool buffer");
        account_backing();
    }

    ObservationSpool(const ObservationSpool&) = delete;
    ObservationSpool& operator=(const ObservationSpool&) = delete;
    ObservationSpool(ObservationSpool&&) = delete;
    ObservationSpool& operator=(ObservationSpool&&) = delete;

    void append(std::span<const Complex> samples, std::span<const std::uint8_t> validity) {
        if (sealed_ || failed_) throw std::logic_error("observation spool is not writable");
        const auto count = chunk_rows(samples.size(), validity.size());
        if (count > expected_rows_ - rows_) throw std::invalid_argument("observation spool append exceeds its row quota");
        for (std::size_t i = 0; i < count; ++i) {
            auto* row = staging_.data() + i * row_bytes;
            std::memcpy(row, &samples[i], sizeof(Complex));
            row[sizeof(Complex)] = validity[i];
        }
        const auto byte_count = count * row_bytes;
        if (std::fwrite(staging_.data(), 1, byte_count, file_.get()) != byte_count || std::ferror(file_.get()))
            fail_io("could not append complete observation spool rows");
        rows_ += count;
    }

    void seal() {
        if (sealed_ || failed_) throw std::logic_error("observation spool cannot be sealed in its current state");
        if (rows_ != expected_rows_) throw std::logic_error("observation spool is incomplete");
        if (std::fflush(file_.get()) != 0 || std::ferror(file_.get()))
            fail_io("could not flush the observation spool");
        sealed_ = true;
    }

    void rewind() {
        if (!sealed_ || failed_) throw std::logic_error("observation spool must be sealed before rewinding");
        // No host-sized tell or end-offset calculation is needed.
#ifdef _WIN32
        const auto seek_status = _fseeki64(file_.get(), 0, SEEK_SET);
#else
        const auto seek_status = std::fseek(file_.get(), 0, SEEK_SET);
#endif
        if (seek_status != 0 || std::ferror(file_.get()))
            fail_io("could not rewind the observation spool");
        read_rows_ = 0;
        rewound_ = true;
    }

    void read(std::span<Complex> samples, std::span<std::uint8_t> validity) {
        if (!sealed_ || !rewound_ || failed_)
            throw std::logic_error("observation spool must be sealed and rewound before reading");
        const auto count = chunk_rows(samples.size(), validity.size());
        if (count > rows_ - read_rows_) throw std::invalid_argument("observation spool read exceeds its stored rows");
        const auto byte_count = count * row_bytes;
        if (std::fread(staging_.data(), 1, byte_count, file_.get()) != byte_count || std::ferror(file_.get()))
            fail_io("could not read complete observation spool rows");
        for (std::size_t i = 0; i < count; ++i) {
            const auto* row = staging_.data() + i * row_bytes;
            std::memcpy(&samples[i], row, sizeof(Complex));
            validity[i] = row[sizeof(Complex)];
        }
        read_rows_ += count;
    }

    std::uint64_t bytes() const noexcept { return rows_ * row_bytes; }
    std::uint64_t rows() const noexcept { return rows_; }
    // Includes both fixed owned buffers and object bookkeeping. The host
    // owns the FILE handle's metadata; setvbuf avoids a second stdio buffer.
    std::size_t working_bytes() const noexcept { return sizeof(*this); }
    // Charge possible resident file data at its planned full extent, before
    // any append. Unknown storage is a reservation, not a RAM measurement.
    std::uint64_t resident_reservation_bytes() const noexcept { return resident_reservation_bytes_; }
    const char* backing_kind() const noexcept { return backing_kind_; }

private:
    static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8);
    static_assert(std::is_trivially_copyable_v<Complex> && sizeof(Complex) == 2 * sizeof(double));

    static std::FILE* open_temporary_file() {
#ifdef _WIN32
        // The MS CRT's tmpfile uses the drive root. Use the user's configured
        // temp directory and a noninheritable, exclusive delete-on-close handle.
        std::array<wchar_t, MAX_PATH> path{};
        const auto directory_length = GetTempPathW(static_cast<DWORD>(path.size()), path.data());
        constexpr std::wstring_view prefix = L"datapump-triad-", suffix = L".tmp";
        constexpr wchar_t hex[] = L"0123456789abcdef";
        std::array<unsigned char, 16> nonce{};
        const auto leaf_length = prefix.size() + 2 * nonce.size() + suffix.size();
        if (directory_length == 0 || directory_length >= path.size() ||
                leaf_length >= path.size() - directory_length)
            throw std::runtime_error("Windows temporary directory cannot fit an observation spool path");
        auto* const name_start = std::copy(prefix.begin(), prefix.end(), path.data() + directory_length);
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            if (BCryptGenRandom(nullptr, nonce.data(), static_cast<ULONG>(nonce.size()),
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                throw std::runtime_error("could not generate an observation spool name");
            auto* cursor = name_start;
            for (const auto byte : nonce) {
                *cursor++ = hex[byte >> 4];
                *cursor++ = hex[byte & 15];
            }
            *std::copy(suffix.begin(), suffix.end(), cursor) = L'\0';
            const auto handle = CreateFileW(path.data(), GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr,
                CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE | FILE_FLAG_OPEN_REPARSE_POINT |
                    FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (handle == INVALID_HANDLE_VALUE) {
                const auto error = GetLastError();
                if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
                throw std::runtime_error("could not create a Windows observation spool in the temporary directory");
            }
            const auto fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_BINARY | _O_RDWR | _O_NOINHERIT);
            if (fd == -1) {
                (void)CloseHandle(handle);
                throw std::runtime_error("could not attach an observation spool descriptor");
            }
            // Successful calls transfer ownership first to the descriptor,
            // then to the stream. Each failure closes its current owner.
            if (auto* stream = _fdopen(fd, "w+b")) return stream;
            (void)_close(fd);
            throw std::runtime_error("could not attach an observation spool stream");
        }
        throw std::runtime_error("observation spool name collided on every attempt");
#else
        return std::tmpfile();
#endif
    }

#ifdef __linux__
    static bool direct_block_device(int descriptor) {
        struct stat identity{};
        if (::fstat(descriptor, &identity) != 0) return false;
        std::array<char, 96> name{};
        const auto count = std::snprintf(name.data(), name.size(), "/sys/dev/block/%u:%u",
            major(identity.st_dev), minor(identity.st_dev));
        if (count <= 0 || static_cast<std::size_t>(count) >= name.size()) return false;
        std::array<char, 4096> target{};
        const auto length = ::readlink(name.data(), target.data(), target.size());
        if (length <= 0 || static_cast<std::size_t>(length) >= target.size()) return false;
        const std::string_view device(target.data(), static_cast<std::size_t>(length));
        return device.starts_with("../../devices/") && !device.starts_with("../../devices/virtual/");
    }
#endif

    void account_backing() {
        // Bound both host-page and filesystem-block rounding. No file data
        // has been written yet.
        std::array<std::uint64_t, 3> allocation_units{64 * 1024, 0, 0};
#ifdef _WIN32
        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        if (system.dwPageSize) allocation_units[0] = system.dwPageSize;
#else
        const auto page_size = ::sysconf(_SC_PAGESIZE);
        if (page_size > 0) allocation_units[0] = static_cast<std::uint64_t>(page_size);
#endif
#ifdef __linux__
        const auto descriptor = ::fileno(file_.get());
        struct statfs filesystem{};
        if (descriptor >= 0 && ::fstatfs(descriptor, &filesystem) == 0) {
            const auto kind = static_cast<std::uint32_t>(filesystem.f_type);
            if (kind == TMPFS_MAGIC || kind == RAMFS_MAGIC || kind == HUGETLBFS_MAGIC) {
                backing_kind_ = "memory_filesystem";
            } else if ((kind == EXT4_SUPER_MAGIC || kind == XFS_SUPER_MAGIC) && direct_block_device(descriptor)) {
                // Direct block storage can use reclaimable page cache. Treat
                // loop, overlay, mapped, network and unverified backing as unknown.
                backing_kind_ = "known_disk";
                return;
            }
            if (filesystem.f_bsize > 0)
                allocation_units[1] = static_cast<std::uint64_t>(filesystem.f_bsize);
            if (filesystem.f_frsize > 0)
                allocation_units[2] = static_cast<std::uint64_t>(filesystem.f_frsize);
        }
#endif
        const auto payload = expected_rows_ * row_bytes;
        for (const auto unit : allocation_units) {
            if (!unit) continue;
            const auto remainder = payload % unit;
            const auto rounding = remainder ? unit - remainder : 0;
            if (payload > std::numeric_limits<std::uint64_t>::max() - rounding)
                throw std::invalid_argument("observation spool backing reservation overflows");
            resident_reservation_bytes_ = std::max(resident_reservation_bytes_, payload + rounding);
        }
    }

    static std::size_t chunk_rows(std::size_t samples, std::size_t validity) {
        if (samples == 0 || samples != validity || samples > maximum_chunk_rows)
            throw std::invalid_argument("observation spool requires 1..4096 paired samples and validity bytes");
        return samples;
    }

    [[noreturn]] void fail_io(const char* message) {
        failed_ = true;
        throw std::runtime_error(message);
    }

    struct CloseFile {
        void operator()(std::FILE* file) const noexcept { (void)std::fclose(file); }
    };

    std::uint64_t expected_rows_, rows_ = 0, read_rows_ = 0;
    std::uint64_t resident_reservation_bytes_ = 0;
    const char* backing_kind_ = "unknown";
    bool sealed_ = false, rewound_ = false, failed_ = false;
    std::array<std::uint8_t, maximum_chunk_rows * row_bytes> staging_;
    std::array<char, 32 * 1024> stdio_buffer_;
    // Close before destroying the borrowed stdio buffer.
    std::unique_ptr<std::FILE, CloseFile> file_;
};

} // namespace datapump::triad::probe

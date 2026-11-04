#pragma once
// Fixed source capabilities only; keep receiver layout/planning independent of caches.
#include <cstddef>
#include <cstdint>
namespace datapump::modem::detail::scheduled_source_samples {
enum class Purpose:std::uint8_t {acquisition,prefit,diagnostic,preparation,count};
inline constexpr std::size_t purpose_count=static_cast<std::size_t>(Purpose::count);
struct PointFamilyToken {
    std::uint64_t pool=0,generation=0,id=0;
    explicit operator bool()const noexcept{return pool && generation && id;}
};
} // namespace datapump::modem::detail::scheduled_source_samples

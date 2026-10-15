#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <string_view>
namespace datapump::modem::detail::scheduled_linear_fragment {
inline constexpr std::size_t default_cells=2048;
inline std::size_t select(const char* text){
    if(!text)return default_cells;
    const std::string_view value(text);
    if(value=="2048")return 2048;if(value=="8192")return 8192;if(value=="16384")return 16384;
    throw Error("linear fragment cells requires2048,8192or16384");
}
inline std::size_t requested(){return select(std::getenv("DATAPUMP_SCHED_LINEAR_FRAGMENT"));}
inline std::size_t effective(std::size_t requested_cells,std::size_t template_cells){
    if((requested_cells!=2048&&requested_cells!=8192&&requested_cells!=16384)||!template_cells)
        throw Error("linear fragment cells invalid extent");
    // Never pad a narrow/short template bank up to the diagnostic block size.
    return std::min(requested_cells,template_cells);
}
} // namespace datapump::modem::detail::scheduled_linear_fragment

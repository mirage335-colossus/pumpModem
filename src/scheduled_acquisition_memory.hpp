#pragma once
#include "datapump/types.hpp"
#include <algorithm>
#include <cstddef>
#include <limits>

namespace datapump::modem::detail::scheduled_acquisition_memory {
// Pure constructor-admission geometry. Slot bytes are supplied from complete
// runtime types; this helper never guesses their layout or allocates storage.
struct StablePool {
    static constexpr std::size_t minimum_slots=5,capacity_multiplier=2,maximum_slots=2048;
    static std::size_t minimum_available(std::size_t per_slot) {
        constexpr auto units=minimum_slots*capacity_multiplier;
        if(!per_slot || per_slot>std::numeric_limits<std::size_t>::max()/units)
            throw Error("implicit minimum stable slot reservation overflow");
        return units*per_slot;
    }
    // Workspace-derived owner ceiling is reserved before any borrower exists.
    // The smaller initial capacity remains a preparation policy, not a limit
    // on required admitted aliases.
    static std::size_t owner_capacity(std::size_t available,std::size_t per_slot) {
        if(!per_slot || per_slot>std::numeric_limits<std::size_t>::max()/capacity_multiplier)
            throw Error("implicit stable slot reservation overflow");
        return available/(capacity_multiplier*per_slot);
    }
    static std::size_t capacity(std::size_t available,std::size_t per_slot) {
        return std::min(maximum_slots,owner_capacity(available,per_slot));
    }
    static bool admits(std::size_t fixed,std::size_t budget,std::size_t per_slot) {
        const auto required=minimum_available(per_slot);
        return fixed<=budget && required<=budget-fixed;
    }
};
} // namespace datapump::modem::detail::scheduled_acquisition_memory

#pragma once
#include "../src/native_diagnostic_history.hpp"
#include <array>
#include <stdexcept>

namespace datapump::test {
inline std::size_t native_diagnostic_history_oracle() {
    using Entry=std::uint64_t;
    using Tail=modem::detail::NativeDiagnosticHistoryTail<Entry>;
    std::size_t comparisons=0;
    for(const std::size_t capacity:{1U,2U,7U,127U,128U,129U,255U,2048U}) {
        std::vector<std::size_t> limits{1,std::min<std::size_t>(128,capacity),capacity};
        std::sort(limits.begin(),limits.end());limits.erase(std::unique(limits.begin(),limits.end()),limits.end());
        for(const auto limit:limits) {
            std::vector<std::size_t> before{0,1,limit-1,limit,std::min(capacity,limit+1),capacity-1,capacity,capacity+3};
            std::vector<std::size_t> delta{0,1,limit-1,limit,limit+1,capacity-limit,capacity-1,capacity,capacity+1,2*capacity+3};
            if(capacity==2048)delta.insert(delta.end(),{1919,1920,1921,1999,2000,2047,2048,2049});
            for(const auto initial:before)for(const auto appended:delta) {
                Tail tail(capacity,limit);std::vector<Entry> baseline,history;history.reserve(limit);
                std::uint64_t insertions=0;
                const auto compare=[&] {
                    const auto count=std::min(limit,baseline.size());
                    if(history.size()!=count || !std::equal(history.begin(),history.end(),baseline.end()-count) ||
                       tail.logical_size()!=baseline.size())throw std::runtime_error("diagnostic tail differs from original FIFO");
                    ++comparisons;
                };
                const auto append=[&] {
                    if(baseline.size()==capacity)baseline.erase(baseline.begin());
                    baseline.push_back(++insertions);tail.append(history,insertions);compare();
                };
                for(std::size_t i=0;i<initial;++i)append();
                // Multiple rollback cycles retain cumulative insertion counts.
                // The first rollback can leave L far below cumulative D/C.
                for(unsigned cycle=0;cycle<3;++cycle) {
                    tail.checkpoint(history);const auto saved_insertions=insertions;
                    const auto count=cycle==0?appended:cycle==1?capacity-limit+1:limit+2;
                    for(std::size_t i=0;i<count;++i)append();
                    compare(); // Cancellation after a committed prefix changes no history.
                    const auto d=insertions-saved_insertions;
                    baseline.resize(baseline.size()-static_cast<std::size_t>(std::min<std::uint64_t>(baseline.size(),d)));
                    tail.rollback(history,d);compare();
                    if(tail.checkpoint_bytes())throw std::runtime_error("rollback retained checkpoint backing");
                    for(std::size_t i=0;i<cycle+2;++i)append();
                }
                tail.checkpoint(history);tail.release_checkpoint();compare();
                if(tail.checkpoint_bytes())throw std::runtime_error("ended obligation retained checkpoint backing");
            }
        }
    }
    return comparisons;
}
}

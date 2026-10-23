#pragma once
#include "datapump/live.hpp"
#include <algorithm>
#include <utility>

namespace datapump::live::detail {
// Helper observations yield every queue slot to payload progress. Coalescing
// only helper rows preserves the existing sequence of unpolled payload events.
inline void append_signal_event(std::vector<SignalUpdate>& queue,SignalUpdate event,std::size_t limit) {
    const auto helper=[](const auto& item) {return item.helper_carrier;};
    if(event.helper_carrier) {
        const auto same=std::find_if(queue.begin(),queue.end(),[&](const auto& item) {
            return item.helper_carrier && item.id==event.id;
        });
        if(same!=queue.end()) {*same=std::move(event);return;}
    }
    if(queue.size()==limit) {
        const auto disposable=std::find_if(queue.begin(),queue.end(),helper);
        if(disposable!=queue.end())queue.erase(disposable);
        else {
            if(event.helper_carrier)return;
            const auto completed=[](const auto& item) {return item.validated || item.complete;};
            const auto pending=std::find_if(queue.begin(),queue.end(),[&](const auto& item) {return !completed(item);});
            if(pending==queue.end() && !completed(event))return;
            queue.erase(pending==queue.end()?queue.begin():pending);
        }
    }
    queue.push_back(std::move(event));
}
}

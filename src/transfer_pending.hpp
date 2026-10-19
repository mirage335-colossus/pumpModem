#pragma once

#include "datapump/transfer.hpp"
#include <utility>

// Live-only delivery policy. Pending presentation consumes scalar diagnostics
// and the accepted prefix; full point vectors are retained only when the
// physical burst completes. The public StreamReceiver::push path is unchanged.
namespace datapump::transfer::detail {
inline Received push_live_pending(StreamReceiver& receiver,modem::PatternBurst burst,
                                 modem::Diagnostics diagnostics) {
    if(!burst.complete) {
        std::vector<std::complex<double>>{}.swap(diagnostics.constellation);
        std::vector<float>{}.swap(diagnostics.waveform);
    }
    return receiver.push(std::move(burst),std::move(diagnostics));
}
}

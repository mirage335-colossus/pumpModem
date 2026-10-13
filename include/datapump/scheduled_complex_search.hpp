#pragma once

#include "datapump/pattern_correlator.hpp"

namespace datapump::modem {

// Bounded bank preparation only: no receiver, template bank or PCM allocation.
// Original exact alternatives always precede added physical-aperture lanes.
// Finite caps reject complete coverage rather than silently coarsening the grid.
struct PatternScheduledComplexSearch {
    PatternSearch search;
    PatternScheduledComplexGeometry geometry;
    std::size_t original_hypotheses=0,refined_hypotheses=0;
    bool applicable=false,refined=false,complete_coverage=false,limited=false;
    std::string reason;
};

// Applications must pass the same UTC steering allowance used for the original
// oscillator bank. Explicit diagnostic banks declaring zero continuous widths
// remain exact supplied banks; a finite bank alone does not invent a domain.
PatternScheduledComplexSearch scheduled_complex_search(const Config&,const PatternSearch&,
                                                       double transmit_rate_correction=0);

} // namespace datapump::modem

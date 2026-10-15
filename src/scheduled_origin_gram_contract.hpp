#pragma once
#include "scheduled_acquisition_certificate.hpp"
#include <span>
namespace datapump::modem::detail::scheduled_origin_gram {
enum class Mode { complete, external_gram };
inline bool active_plane(Mode mode,unsigned plane){return mode==Mode::complete || plane%3==0;}
struct Gram {
    double cc=0,ss=0,cs=0,radius=scheduled_acquisition_certificate::infinity;
    std::uint64_t count=0,template_identity=0,capture_generation=0,capture_revision=0;
    bool supported=false;
    std::size_t variant=0;
};
// Borrowed immutable results from Prefix::query. The exact source-template
// identity MUST be bound by the integrating caller to its current prepared
// fragment family (key/address/phase/rate/source fraction/gain/mask/limiter).
// No detector observations, energy, threshold or admission travel in this token.
struct Binding {
    // Integrating caller obtains these from CURRENT actual input generation
    // and exact prepared template-family identity, not by copying token labels.
    std::uint64_t template_identity=0,capture_generation=0,capture_revision=0;
    std::size_t variant=0;
};
struct Override {
    std::span<const std::array<Gram,2>> rows;
    std::span<const std::size_t> starts;
    std::uint64_t origin=0,template_identity=0,capture_generation=0,capture_revision=0;
    std::size_t variant=0;
    bool valid_for(std::uint64_t expected_origin,std::span<const std::size_t> expected_starts,const Binding& expected)const{
        if(!scheduled_acquisition_certificate::environment<double>()||origin!=expected_origin||
           starts.size()!=expected_starts.size()||rows.size()!=starts.size()||
           template_identity!=expected.template_identity||capture_generation!=expected.capture_generation||capture_revision!=expected.capture_revision||variant!=expected.variant)return false;
        for(std::size_t i=0;i<starts.size();++i){if(starts[i]!=expected_starts[i])return false;
            for(const auto& g:rows[i])if(!g.supported||!std::isfinite(g.cc)||!std::isfinite(g.ss)||!std::isfinite(g.cs)||
                !std::isfinite(g.radius)||g.radius<0||g.template_identity!=template_identity||
                g.capture_generation!=capture_generation||g.capture_revision!=capture_revision||g.variant!=variant)return false;
            if(rows[i][0].count!=rows[i][1].count)return false;
        }return true;
    }
};
} // namespace datapump::modem::detail::scheduled_origin_gram

#include "ssb_candidate_search.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {
namespace {
constexpr unsigned gate_words=8;
constexpr std::size_t max_retained_centers=1U<<18;

double checked_centers_per_word(double input_rate,std::size_t stride,double proposal_rate) {
    if(!stride || !std::isfinite(input_rate) || input_rate<=0. ||
       !std::isfinite(proposal_rate) || proposal_rate<=0.)
        throw std::invalid_argument("Invalid Fast SSB acquisition sample grid");
    return ssb_proposal_word_samples*input_rate/(static_cast<double>(stride)*proposal_rate);
}

std::size_t checked_count(double centers) {
    if(!std::isfinite(centers) || centers<1. || centers>max_retained_centers)
        throw std::invalid_argument("Fast SSB acquisition window exceeds bounded sample geometry");
    return static_cast<std::size_t>(centers);
}

std::uint64_t advance(std::uint64_t index,std::size_t count) {
    if(count>std::numeric_limits<std::uint64_t>::max()-index)
        throw std::length_error("Fast SSB acquisition timeline exhausted");
    return index+count;
}
}

SsbCandidateSearch::SsbCandidateSearch(const DelayDopplerFit& fit,
                                       const SsbFrontendGeometry& frontend,
                                       SsbAcquisitionRequest limits,double minimum_coherence,
                                       CandidateSink sink)
    : request_(limits),timing_(fit.timing(limits)),stride_(frontend.decimation),
      input_rate_(frontend.input_rate),proposal_rate_(fit.proposal_sample_rate()),
      minimum_coherence_(minimum_coherence),
      projection_(fit.proposal_word(),proposal_rate_,request_.max_abs_doppler_hz),
      sink_(std::move(sink)) {
    if(!sink_ || !std::isfinite(minimum_coherence_) || minimum_coherence_<0. || minimum_coherence_>1.)
        throw std::invalid_argument("Invalid Fast SSB candidate sink or projection threshold");
    const auto word=checked_centers_per_word(input_rate_,stride_,proposal_rate_);
    const auto earliest=timing_.earliest_training_start_after_epoch_ticks/static_cast<double>(stride_);
    const auto latest=timing_.latest_training_start_after_epoch_ticks/static_cast<double>(stride_);
    const auto last=timing_.last_required_center_after_epoch_ticks/static_cast<double>(stride_);
    if(!(earliest>0. && latest>earliest && last>latest))
        throw std::invalid_argument("Invalid Fast SSB acquisition timing geometry");
    // Cubic regridding needs two following centers. A small margin in fit
    // spacing covers integral center rounding at either training edge.
    gate_wait_=checked_count(std::ceil(gate_words*word)+3);
    gate_step_=checked_count(std::floor(4*word));
    fit_wait_=checked_count(std::ceil(last-earliest)+1);
    fit_step_=checked_count(std::floor(std::min(2*word,latest-earliest-2)));
    training_length_=checked_count(std::ceil(request_.training_words*word));
    minimum_overlap_=checked_count(std::ceil(4*word));
    if(fit_wait_<=training_length_ || request_.training_words<gate_words)
        throw std::invalid_argument("Fast SSB acquisition has no independent marker context");
    history_capacity_=std::max(gate_wait_+2,checked_count(std::ceil(last+latest-earliest+2*word)+4));
    history_.resize(2*history_capacity_);
    positive_gates_.reserve((history_capacity_+gate_wait_)/gate_step_+2);
}

std::uint64_t SsbCandidateSearch::center(std::uint64_t sample_index) const noexcept {
    return first_center_+sample_index*stride_;
}

SsbAcquisitionView SsbCandidateSearch::retained() const noexcept {
    const auto first=(write_at_+history_capacity_-retained_count_)%history_capacity_;
    return {center(received_-retained_count_),stride_,std::span(history_).subspan(first,retained_count_)};
}

void SsbCandidateSearch::reserve_history(std::size_t minimum_centers) {
    if(minimum_centers<=history_capacity_)return;
    if(!progress_.locked || minimum_centers>max_retained_centers)
        throw std::invalid_argument("Fast SSB payload history exceeds its admitted geometry");
    const auto samples=retained().samples;
    std::vector<Complex> expanded(2*minimum_centers);
    std::copy(samples.begin(),samples.end(),expanded.begin());
    std::copy(samples.begin(),samples.end(),expanded.begin()+static_cast<std::ptrdiff_t>(minimum_centers));
    write_at_=samples.size();
    history_capacity_=minimum_centers;
    history_.swap(expanded);
}

void SsbCandidateSearch::project_window(std::stop_token stop) {
    const auto view=retained();
    const auto uniform=regrid_ssb_preamble(view,center(next_gate_),gate_words,proposal_rate_,input_rate_);
    ++progress_.projected_windows;
    const auto first_retained=received_-view.samples.size();
    auto keep=positive_gates_.begin();
    while(keep!=positive_gates_.end() && *keep<first_retained && first_retained-*keep>=gate_wait_)++keep;
    positive_gates_.erase(positive_gates_.begin(),keep);
    if(uniform) {
        const auto peak=projection_.search(*uniform,stop);
        if(stop.stop_requested())return;
        if(peak && peak->coherence>=minimum_coherence_) {
            positive_gates_.push_back(next_gate_);
            ++progress_.training_proposals;
        }
    }
    next_gate_=advance(next_gate_,gate_step_);
}

void SsbCandidateSearch::offer_fit(std::stop_token stop) {
    const auto fit_start=next_fit_;
    const auto view=retained();
    const auto first_retained=received_-view.samples.size();
    const auto fit_end=fit_start+training_length_;
    const auto evidence=fit_start>=first_retained && std::ranges::any_of(positive_gates_,[&](auto gate_start) {
        const auto gate_end=gate_start+gate_wait_;
        const auto start=std::max(gate_start,fit_start);
        const auto end=std::min(gate_end,fit_end);
        return end>start && end-start>=minimum_overlap_;
    });
    if(evidence) {
        auto request=request_;
        request.training_first_center_tick=center(fit_start);
        // This periodic interior defines the epoch hypotheses. Verification
        // uses the separately observed marker.
        request.earliest_epoch_from_training_ticks=-timing_.latest_training_start_after_epoch_ticks;
        request.latest_epoch_from_training_ticks=-timing_.earliest_training_start_after_epoch_ticks;
        ++progress_.fit_requests;
        const auto locked=sink_(view,request,stop);
        if(stop.stop_requested())return;
        progress_.locked=locked;
    }
    next_fit_=advance(next_fit_,fit_step_);
}

void SsbCandidateSearch::service_ready(std::stop_token stop) {
    if(progress_.locked || stop.stop_requested())return;
    if(received_>=next_gate_ && received_-next_gate_>=gate_wait_)project_window(stop);
    if(stop.stop_requested())return;
    if(received_>=next_fit_ && received_-next_fit_>=fit_wait_)offer_fit(stop);
}

std::size_t SsbCandidateSearch::push(const SsbFrontendOutput& block,std::stop_token stop) {
    if(block.samples.empty())return 0;
    if(block.decimation!=stride_ || (received_ && block.first_center_input_sample!=center(received_)))
        throw std::invalid_argument("Discontinuous Fast SSB acquisition observations");
    if(block.samples.size()>(std::numeric_limits<std::uint64_t>::max()-block.first_center_input_sample)/stride_)
        throw std::length_error("Fast SSB acquisition input extent overflow");
    if(!received_)first_center_=block.first_center_input_sample;
    std::size_t consumed=0;
    service_ready(stop);
    for(const auto value:block.samples) {
        if(stop.stop_requested())break;
        history_[write_at_]=history_[write_at_+history_capacity_]=value;
        if(++write_at_==history_capacity_)write_at_=0;
        if(retained_count_<history_capacity_)++retained_count_;
        ++received_;
        ++consumed;
        service_ready(stop);
    }
    return consumed;
}

std::size_t SsbCandidateSearch::workspace_bytes() const noexcept {
    const auto grid_bytes=gate_words*ssb_proposal_word_samples*sizeof(Complex);
    return sizeof(*this)-sizeof(projection_)+projection_.workspace_bytes()+grid_bytes+
        history_.capacity()*sizeof(Complex)+positive_gates_.capacity()*sizeof(std::uint64_t);
}

} // namespace datapump::fast::detail

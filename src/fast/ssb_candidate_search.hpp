#pragma once

#include "ssb_acquisition.hpp"
#include "ssb_folded_preamble.hpp"
#include <functional>

namespace datapump::fast::detail {

struct SsbCandidateSearchProgress {
    std::uint64_t projected_windows = 0;
    std::uint64_t training_proposals = 0;
    std::uint64_t fit_requests = 0;
    bool locked = false;
};

// Keeps true-supported native observations and offers oldest ready public
// training windows to the independent acquisition fitter. Only an accepted
// fitter result may stop the search.
class SsbCandidateSearch {
public:
    // The observation view is borrowed until the call returns. The sink
    // returns true after independent frame verification and must not reenter.
    using CandidateSink = std::function<bool(SsbAcquisitionView,const SsbAcquisitionRequest&,std::stop_token)>;

    // Uses the fixed training count and clock/delay/Doppler limits from the
    // request. Observation time and epoch bounds are set as input arrives.
    SsbCandidateSearch(const DelayDopplerFit&,const SsbFrontendGeometry&,
                       SsbAcquisitionRequest limits,double minimum_coherence,
                       CandidateSink);

    // Returns the number of appended centers, possibly short on cancellation.
    std::size_t push(const SsbFrontendOutput&,std::stop_token stop={});
    // After lock, admit the local payload window before replacing history.
    void reserve_history(std::size_t minimum_centers);
    // The borrowed samples remain valid until the next mutation.
    SsbAcquisitionView retained() const noexcept;
    const SsbCandidateSearchProgress& progress() const noexcept { return progress_; }
    std::size_t workspace_bytes() const noexcept;

private:
    using Complex = std::complex<double>;

    std::uint64_t center(std::uint64_t sample_index) const noexcept;
    void service_ready(std::stop_token);
    void project_window(std::stop_token);
    void offer_fit(std::stop_token);

    SsbAcquisitionRequest request_;
    SsbAcquisitionTiming timing_;
    std::size_t stride_;
    double input_rate_, proposal_rate_, minimum_coherence_;
    SsbFoldedPreamble projection_;
    CandidateSink sink_;
    std::size_t history_capacity_, gate_step_, gate_wait_, fit_step_, fit_wait_,
        training_length_, minimum_overlap_;
    std::vector<Complex> history_;
    std::vector<std::uint64_t> positive_gates_;
    std::size_t write_at_ = 0, retained_count_ = 0;
    std::uint64_t first_center_ = 0, received_ = 0, next_gate_ = 2, next_fit_ = 2;
    SsbCandidateSearchProgress progress_;
};

} // namespace datapump::fast::detail

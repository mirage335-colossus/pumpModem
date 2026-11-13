#pragma once

#include "datapump/execution.hpp"
#include "ssb_acquisition.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace datapump::fast::detail {

struct SsbFitResult {
    std::optional<SsbAcquisitionLock> lock;
    SsbAcquisitionDiagnostics diagnostics;
    double wall_seconds = 0.;
};

struct SsbFitReceipt {
    SsbAcquisitionRequest request;
    std::uint64_t first_center, past_last_center, first_payload_frontier;
    SsbFitResult result;
    std::exception_ptr failure;
};

// Each task owns the copied serial observation window until the pure fit
// returns. Only the caller takes results, in submission order.
class SsbFitWorkers {
    struct Cancel {
        SsbFitWorkers* workers;
        void operator()() const noexcept { workers->request_stop(); }
    };
public:
    using Score = std::function<SsbFitResult(SsbAcquisitionView,const SsbAcquisitionRequest&,std::stop_token)>;

    SsbFitWorkers(unsigned owners,Score score,std::stop_token external)
        : owners_(owners),score_(std::move(score)),cancel_(external,Cancel{this}) {
        if (owners_ < 2 || !score_) throw std::invalid_argument("Invalid Fast SSB fit worker limits");
        helpers_.reserve(owners_ - 1);
    }
    ~SsbFitWorkers() { close(); }
    SsbFitWorkers(const SsbFitWorkers&) = delete;
    SsbFitWorkers& operator=(const SsbFitWorkers&) = delete;

    bool submit(SsbAcquisitionView view,const SsbAcquisitionRequest& request,std::uint64_t first_payload_frontier) {
        if (stop_.stop_requested()) return false;
        if (!view.stride || view.samples.empty() || view.samples.size() >
            (std::numeric_limits<std::uint64_t>::max() - view.first_center_input_tick) / view.stride)
            throw std::invalid_argument("Invalid Fast SSB fit worker observation extent");
        auto job = std::make_unique<Job>();
        job->receipt = {request,view.first_center_input_tick,
            view.first_center_input_tick + view.stride * view.samples.size(),first_payload_frontier,{}, {}};
        job->stride = view.stride;
        job->samples.assign(view.samples.begin(),view.samples.end());
        std::lock_guard lock(mutex_);
        if (stop_.stop_requested()) return false;
        if (jobs_.size() >= owners_) throw std::logic_error("Fast SSB fit owner limit exceeded");
        jobs_.push_back(std::move(job));
        try {
            const auto demand = std::min<std::size_t>(owners_ - 1,running_helpers_ + ready_jobs());
            while (helpers_.size() < demand) helpers_.emplace_back([this] { run(); });
        } catch (...) {
            jobs_.pop_back();
            throw;
        }
        ready_.notify_all();
        return true;
    }

    std::optional<SsbFitReceipt> take(bool wait) {
        std::unique_lock lock(mutex_);
        if (jobs_.empty()) return {};
        while (!stop_.stop_requested() && !jobs_.front()->finished) {
            if (!wait) return {};
            // The controller counts as one DSP lane. Never make an oldest
            // result wait for the controller to finish a younger fit.
            auto& oldest = *jobs_.front();
            if (!oldest.claimed) {
                oldest.claimed = true;
                execute(oldest,lock);
            } else ready_.wait(lock,stop_.get_token(),[&] { return oldest.finished; });
        }
        if (stop_.stop_requested()) return {};
        const auto job = std::move(jobs_.front());
        jobs_.pop_front();
        return std::optional<SsbFitReceipt>(std::in_place,std::move(job->receipt));
    }

    std::size_t pending() const noexcept { return jobs_.size(); }
    const SsbFitReceipt* oldest() const noexcept {
        // The controller reads only the immutable coordinates in this slot.
        return jobs_.empty() ? nullptr : &jobs_.front()->receipt;
    }
    unsigned started_helpers() const noexcept { return static_cast<unsigned>(helpers_.size()); }
    std::size_t peak_active_fits() {
        std::lock_guard lock(mutex_);
        return peak_active_fits_;
    }
    std::size_t workspace_bytes() const noexcept {
        return sizeof(*this) + helpers_.capacity() * sizeof(execution::Task) +
            (owners_+1ULL) * sizeof(std::unique_ptr<Job>) + 1024; // Empty deque block and map.
    }
    static constexpr std::size_t result_lease_bytes() noexcept {
        // Job, deque blocks/map, optional lock and maximum owned atom list.
        return sizeof(Job) + 1024 + 2 * DelayDopplerFit::maximum_atoms * sizeof(SsbAcquisitionAtom);
    }
    std::size_t reserved_bytes(std::size_t fit_bytes,std::size_t stack_bytes) const noexcept {
        // Keep a conservative full lease until each ordered result retires.
        return workspace_bytes() + helpers_.size() * stack_bytes + jobs_.size() * (result_lease_bytes() + fit_bytes);
    }

    void close() noexcept {
        request_stop();
        for (auto& helper : helpers_) if (helper.joinable()) helper.join();
        helpers_.clear();
        jobs_.clear();
    }

private:
    struct Job {
        SsbFitReceipt receipt{};
        std::size_t stride = 0;
        std::vector<std::complex<double>> samples;
        bool claimed = false, finished = false;
    };

    void request_stop() noexcept {
        stop_.request_stop();
        ready_.notify_all();
    }

    std::size_t ready_jobs() const {
        return static_cast<std::size_t>(std::ranges::count_if(jobs_,[](const auto& job) {return !job->claimed;}));
    }

    Job* claim() {
        const auto it = std::ranges::find_if(jobs_,[](const auto& job) {return !job->claimed;});
        if (it == jobs_.end()) return nullptr;
        (*it)->claimed = true;
        return it->get();
    }

    void execute(Job& job,std::unique_lock<execution::Mutex>& lock) {
        ++active_fits_;
        peak_active_fits_ = std::max(peak_active_fits_,active_fits_);
        lock.unlock();
        SsbFitResult result;
        std::exception_ptr failure;
        try {
            result = score_({job.receipt.first_center,job.stride,job.samples},job.receipt.request,stop_.get_token());
        } catch (...) { failure = std::current_exception(); }
        std::vector<std::complex<double>>().swap(job.samples);
        lock.lock();
        --active_fits_;
        job.receipt.result = std::move(result);
        job.receipt.failure = std::move(failure);
        job.finished = true;
        ready_.notify_all();
    }

    void run() {
        std::unique_lock lock(mutex_);
        while (ready_.wait(lock,stop_.get_token(),[&] { return ready_jobs() != 0; })) {
            if (stop_.stop_requested()) break;
            auto* job = claim();
            ++running_helpers_;
            execute(*job,lock);
            --running_helpers_;
        }
    }

    const unsigned owners_;
    const Score score_;
    std::stop_source stop_;
    execution::Mutex mutex_;
    execution::StopCondition ready_;
    std::deque<std::unique_ptr<Job>> jobs_;
    std::vector<execution::Task> helpers_;
    std::size_t running_helpers_ = 0;
    std::size_t active_fits_ = 0, peak_active_fits_ = 0;
    std::stop_callback<Cancel> cancel_;
};

} // namespace datapump::fast::detail

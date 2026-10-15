// Include in Impl after lazy_defer_diagnostic. This consumes a rejection already
// decided by the detector; additional diagnostic work NEVER replaces it.
    detail::scheduled_default_point_diagnostics::Outcome lazy_default_point_diagnostic(const LazyState::Catalog::Lane& lane,const LazyState::DueRange& range,
            std::uint64_t index,std::uint64_t event,std::uint64_t original_trials,std::uint64_t block_end,
            double admission_threshold,const detail::scheduled_default_linear_runtime::Decision& rejected_point,
            bool whole_prefilter_ready,const detail::scheduled_default_point_guard_summary::Summary* completed_summary,
            std::stop_token stop) {
        namespace policy=detail::scheduled_default_point_diagnostics;
        namespace guard=detail::scheduled_default_point_guard_summary;
        cancelled(stop);const auto selected=policy::policy();
        auto& state=*lazy_state();
        const auto trace=[&](policy::Outcome outcome) {
            audio::trace::record("scheduled_default_point_diagnostic",[&](auto& f){
                f.add("lane",lane.id);f.add("index",index);f.add("ordinal",event);
                f.add("policy",selected==policy::Policy::chosen?"chosen":"original");
                f.add("selected_statistic",policy::statistic(outcome));f.add("resolution",policy::reason(outcome));
            });
        };
        if(selected==policy::Policy::original) {
            lazy_defer_diagnostic(lane,index,event,original_trials,block_end,stop,
                detail::scheduled_diagnostics::RejectionProvenance::default_point);
            trace(policy::Outcome::original_requested);return policy::Outcome::original_requested;
        }
        // This policy explicitly uses the same experimental point-screen gate
        // for eager retention exclusion. FP32 FFT whole fields are NOT an exact
        // bound for native full-guard fields near a numerical boundary. Hence
        // this is screen-gated chosen history, not an all-native FIFO oracle.
        if(detail::scheduled_default_linear_events::below_rejection_floor(rejected_point.scores,search.retain_score)) {
            cancelled(stop);state.diagnostics->observe_resolved(event);
            ++state.default_chosen_diagnostic_resolved;trace(policy::Outcome::chosen_fft_excluded);return policy::Outcome::chosen_fft_excluded;
        }
        guard::Summary calculated;const auto* value=completed_summary;
        if(!value||!value->valid) {
            ++state.default_chosen_diagnostic_guard_lookups;
            // Optional sink returns SAME full native guards (including outer),
            // not the whole-only FP32 FFT fields. This numerical result is for
            // history only; never assign it back to rejected_point.
            const auto prepared=lazy_default_point_guard_summary(lane,range,admission_threshold,stop,&calculated);
            const auto ignored=prepared.ready?prepared:lazy_default_linear_point(lane,range,admission_threshold,stop,
                whole_prefilter_ready,&calculated);
            value=ignored.ready&&calculated.valid?&calculated:nullptr;
        }
        if(value) {
            const auto end=state.catalog->due(lane,index,*state.cursor);
            const auto result=policy::evidence(*value,policy::Identity{end,index,event,lane.phase_lower,
                config.carrier_hz+search.frequency_offsets_hz[lane.frequency],admission_threshold});
            if(result) {
                cancelled(stop);
                // No cancellation point separates these owned FIFO/journal
                // commits. Caller immediately commits generation and trials.
                lazy_remember_diagnostic(*result);state.diagnostics->observe_resolved(event);
                ++state.default_chosen_diagnostic_resolved;
                if(result->score>=search.retain_score)++state.default_chosen_diagnostic_retained;
                trace(policy::Outcome::chosen_native_guards);return policy::Outcome::chosen_native_guards;
            }
        }
        // True unsupported/resource failure retains the old reference path,
        // explicitly counted/labeled; do not call this mixed coverage chosen.
        lazy_defer_diagnostic(lane,index,event,original_trials,block_end,stop,
            detail::scheduled_diagnostics::RejectionProvenance::default_point);
        ++state.default_chosen_diagnostic_original_fallbacks;
        trace(policy::Outcome::original_fallback);return policy::Outcome::original_fallback;
    }

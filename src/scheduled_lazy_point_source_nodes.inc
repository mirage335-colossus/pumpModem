// Include inside Impl before both Default chosen-chord consumers. The pool is
// optional source-template storage, not a receiver hypothesis or PCM owner.
    static bool lazy_point_source_nodes_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_POINT_SOURCE_NODES");
        if(!value||std::string_view(value)=="1")return true;
        require(std::string_view(value)=="0","point source node cache requires 0 or 1");return false;
    }
    detail::scheduled_point_source_nodes::Pool::Pin lazy_point_source_nodes(
            std::uint64_t phase,std::uint64_t index,unsigned subdivisions,std::stop_token stop) {
        namespace cache=detail::scheduled_point_source_nodes;
        auto& state=*lazy_state();
        if(!lazy_point_source_nodes_requested() || config.search_arithmetic!=SearchArithmetic::default_mode ||
           options.raw_reference || options.scheduled_raw_score_reference || options.amplitude_only ||
           state.screens_running || !lazy_reference_environment())return {};
        const cache::Key key{config.stream_epoch,phase,index,code.symbol_samples(),code.chip_samples(),subdivisions};
        const auto bytes=cache::Pool::row_reservation(key);
        if(bytes==SIZE_MAX)return {};
        const auto refresh=[&] {
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            work.peak_workspace_bytes=std::max(work.peak_workspace_bytes,sizeof(PatternCorrelator)+accounted_bytes);
        };
        const auto fits=[&](std::size_t n) {
            refresh();const auto used=sizeof(PatternCorrelator)+accounted_bytes;
            return used<=budget && n<=budget-used;
        };
        // No bank retirement or general room_for while Context/plan loans may
        // be live. This optional cache only evicts its own unborrowed rows.
        // The ordinary required-geometry workspace path remains unchanged.
        struct Loan {
            Impl& self;std::size_t bytes;
            Loan(Impl& s,std::size_t n):self(s),bytes(n){self.lazy_state()->transient_reservation+=n;}
            ~Loan(){self.lazy_state()->transient_reservation-=bytes;
                self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;
                self.work.peak_workspace_bytes=std::max(self.work.peak_workspace_bytes,sizeof(PatternCorrelator)+self.accounted_bytes);}
        };
        cancelled(stop);
        if(!state.point_source_nodes) {
            const auto meta=cache::Pool::metadata_bytes();
            if(!fits(meta)){++state.point_source_node_stats.pressure_fallbacks;return {};}
            try {Loan loan(*this,meta);state.point_source_nodes=std::make_unique<cache::Pool>(state.point_source_node_stats);}
            catch(const std::bad_alloc&){++state.point_source_node_stats.allocation_fallbacks;cancelled(stop);return {};}
        }
        auto& pool=*state.point_source_nodes;
        // Protect the container itself while evaluating optional allocation.
        // Returned row Pins protect it after this lease ends.
        cache::Pool::Lease lease(pool);
        if(auto found=pool.find(key))return found;
        if(bytes>pool.limit()){++pool.stats.pressure_fallbacks;return {};}
        while(!pool.can_fit(bytes)||!fits(bytes)) {
            if(!pool.evict_one()){++pool.stats.pressure_fallbacks;refresh();return {};}
        }
        cancelled(stop);
        try {Loan loan(*this,bytes);auto pin=pool.create(key,bytes);cancelled(stop);return pin;}
        catch(const std::bad_alloc&){++pool.stats.allocation_fallbacks;cancelled(stop);return {};}
    }

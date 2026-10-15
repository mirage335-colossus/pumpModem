// Include in Impl before the Default point producer and detailed guard hooks.
    static bool lazy_default_point_guard_archive_requested() {
        const auto* value=std::getenv("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_ARCHIVE");
        if(!value||std::string_view(value)=="0")return false;
        require(std::string_view(value)=="1","Default point guard archive requires 0 or 1");
        return lazy_default_linear_requested()&&lazy_default_point_fft_requested();
    }
    static unsigned lazy_default_point_guard_edge_width(unsigned subdivisions,long double spacing) {
        if(!lazy_default_point_guard_archive_requested())return 0;
        const auto* value=std::getenv("DATAPUMP_SCHED_DEFAULT_POINT_GUARD_EDGES");
        unsigned requested=0;
        if(value&&std::string_view(value)!="0") {
            if(std::string_view(value)=="1")requested=1;
            else {require(std::string_view(value)=="2","Default point guard edges require 0, 1 or 2");requested=2;}
        }
        return detail::scheduled_default_point_guard_archive_runtime::select_edge_width(requested,subdivisions,spacing);
    }
    template<class Geometry>
    detail::scheduled_default_point_guard_archive_runtime::Key lazy_default_point_guard_archive_key(
            const LazyState::Catalog::Lane& reference,const LazyState::DueRange& range,
            const Geometry& geometry,unsigned subdivisions) {
        namespace archive=detail::scheduled_default_point_guard_archive_runtime;
        const auto& state=*lazy_state();archive::Key key;
        key.bank=reinterpret_cast<std::uintptr_t>(&state);
        key.processing=reinterpret_cast<std::uintptr_t>(state.processing.get());
        key.raw_capture=state.capture_fences?reinterpret_cast<std::uintptr_t>(state.capture_fences.get()):
            reinterpret_cast<std::uintptr_t>(&state.capture);
        key.capture=state.screen_capture_end;key.index=range.index;key.epoch=scheduled->epoch_sample;
        key.phase=reference.phase_lower;key.anchor_sample=reference.anchor.sample;
        key.symbol=code.symbol_samples();key.chip=code.chip_samples();
        key.family=range.family;key.first=range.first;key.end=range.end;key.frequency=reference.frequency;
        key.template_cells=geometry.template_cells;key.step=geometry.step;key.subdivisions=subdivisions;
        key.origin=reference.origin;key.rate=reference.rate;key.anchor_fraction=reference.anchor.fraction;
        key.physical_base=geometry.physical_base;key.node_spacing=geometry.node_spacing;
        key.amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
        const auto& f=scheduled->fhss_shape;key.fhss_enabled=f.enabled;key.fhss_smooth=f.smooth;
        key.fhss_period=f.period;key.fhss_quiet=f.quiet;key.fhss_support=f.support;key.fhss_lag=f.lag_center;
        return key;
    }

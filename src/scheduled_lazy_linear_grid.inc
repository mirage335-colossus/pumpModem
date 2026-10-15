// Include in PatternCorrelator::Impl. Headers: scheduled_linear_family.hpp,
// coarse_pulse_cells.hpp. Opt-in only; parent integrates dispatch before A/B/D.
    template<class T>bool lazy_screen_linear_grid(LazyState::DueRange& range,std::stop_token stop) {
        namespace family=detail::scheduled_linear_family;namespace grid=detail::scheduled_linear_grid;
        namespace fft=detail::scheduled_linear_grid_fft;namespace coarse=detail::scheduled_coarse_pulse_cells;
        namespace cert=detail::scheduled_acquisition_certificate;namespace tile=detail::scheduled_acquisition_tile;
        namespace native=detail::scheduled_linear_native_bound;namespace zero=detail::scheduled_zero_guard;
        namespace gains=detail::scheduled_linear_gain_cells;
        const auto* requested=std::getenv("DATAPUMP_SCHED_LINEAR_GRID");
        if(!requested||config.search_arithmetic==SearchArithmetic::fp64)return false;
        const std::string_view choice(requested);const unsigned subdivisions=choice=="8"?8:choice=="16"?16:choice=="4"?4:choice=="2"?2:0;
        if(!subdivisions||!lazy_reference_environment()||!shaped||!lazy_fhss_screen_supported()||range.first==range.end||
            options.scheduled_float_curve_experiment||scheduled->curves||scheduled->float_curves||scheduled->moments_eligible||
            scheduled_affine||!scheduled->moments.empty())return false;
        rx_cpu::Scope cpu_screen(rx_cpu::Stage::search_residual);
        auto& execution=lazy_execution();auto& code=execution.code;const auto reference=lazy_family_lane(range.family,range.first);
        const auto outputs=range.end-range.first;const std::size_t step=subdivisions/2;
        if(outputs>tile::maximum_start_span||range.index==UINT64_MAX||code.symbol_samples()%code.chip_samples()||
            code.symbol_samples()/code.chip_samples()<16||reference.rate<.99L||reference.rate>1.01L)return false;
        const auto spacing=static_cast<long double>(code.chip_samples())/(subdivisions*reference.rate);
        // A cell cannot contain a hidden complete active/null or FHSS cycle.
        if(!(spacing>0&&spacing*reference.rate<scheduled_map()->source_samples_per_group()/4.L&&
            spacing*reference.rate<config.sample_rate/10.L))return false;
        std::array<std::uint8_t,tile::maximum_start_span> original{};long double translation_error=0;
        for(std::size_t j=0;j<outputs;++j){const auto lane=lazy_family_lane(range.family,range.first+j);
            if(lane.rate!=reference.rate||lane.phase_lower!=reference.phase_lower||lane.anchor.distance(reference.anchor)!=0)return false;
            const auto deviation=lane.origin-reference.origin-static_cast<long double>(j)*step*spacing;
            if(std::abs(deviation)>.001L){
                // Retain a clipped terminal original hypothesis, with direct
                // original scoring. Never turn it into a missing FFT lane.
                if(j+1!=outputs||std::abs(deviation)>step*spacing)return false;original[j]=1;
            }else translation_error=std::max(translation_error,std::abs(deviation)*reference.rate);
        }
        const auto source_first=static_cast<long double>(range.index)*code.symbol_samples();
        const auto begin=lazy_state()->catalog->boundary(reference,source_first,false,execution.cursor);
        const auto end=lazy_state()->catalog->boundary(reference,source_first+code.symbol_samples(),false,execution.cursor);
        const auto padding=static_cast<long double>(step+2)*spacing;
        if(static_cast<long double>(lazy_history_first())>std::max(0.L,std::floor(begin-padding)-2))return false;
        const auto base=begin-padding;
        const auto cells=static_cast<std::size_t>(std::ceil((end-begin+2*padding)/spacing));
        if(!cells||cells>0x40000000ULL)return false;
        const auto frontier=lazy_state()->block_end; // frozen before this numerical preparation
        if(!frontier||frontier>lazy_state()->capture_end)return false;
        const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
        auto coordinate_error=translation_error+lazy_fhss_coordinate_radius();
        coordinate_error+=2048*std::numeric_limits<long double>::epsilon()*(1+std::abs(reference.origin)+
            scheduled->complex_geometry.physical_aperture_samples+std::abs(source_first)+code.symbol_samples());
        if(coordinate_error>.001L)return false;
        if(range.index>=UINT64_MAX/code.chips_per_symbol())return false; // leave room for Original carry validation
        const auto first_chip=range.index*code.chips_per_symbol();code.set_stream_phase_samples(reference.phase_lower);code.prepare_symbol(first_chip,stop);
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
        const auto gain_upper=detail::scheduled_cold_colored::smooth_gain_upper();
        const auto qmax=cert::add(cert::mul(cert::mul(cert::up(pattern_pcm_radius_limit/amplitude),gain_upper),
            cert::add(1,cert::gamma(32,std::numeric_limits<double>::epsilon()/2))),
            cert::divide(128*std::numeric_limits<double>::denorm_min(),amplitude));
        const auto& pulse_table=coarse::defects<8>();if(!pulse_table.valid)return false;
        const auto group_size=scheduled_map()->source_samples_per_group();if(!group_size)return false;
        const auto group_bound=code.symbol_samples()/group_size+(code.symbol_samples()%group_size!=0)+4;
        const auto magnitude=2*(1.L+frontier+std::abs(reference.origin)+
            static_cast<long double>(outputs)*step*spacing+std::abs(source_first)+code.symbol_samples()+reference.phase_lower+
            cells*spacing+padding+2.L*config.sample_rate);
        native::Request nr;nr.symbol=code.symbol_samples();nr.chip=code.chip_samples();nr.groups=group_bound;
        nr.rate=reference.rate;nr.magnitude=magnitude;nr.qmax=nr.source_q_upper=qmax;nr.amplitude=amplitude;
        nr.gain_upper=gain_upper;nr.gate_slope=lazy_screen_gate_slope();nr.pulse_peak=pulse_table.peak;
        nr.pulse_slope=pulse_table.slope;nr.pulse_jump=pulse_table.jump;
        const auto ramp=std::ceil(static_cast<long double>(config.sample_rate)*config.scheduled_hopping->ramp_seconds);
        if(!(ramp>=1&&ramp<=0x1p52L))return false;nr.scheduled_ramp=static_cast<std::uint64_t>(ramp);
        nr.fhss=scheduled->fhss_shape.enabled;nr.qualified=true;
        if(nr.fhss){const auto support=scheduled->fhss_shape.support;
            const auto dwells=std::ceil(static_cast<long double>(group_size)/(2.L*config.sample_rate/5))+2;
            if(!(support>=1&&support<=0x1p52L&&dwells>=1&&dwells<=0x1p40L))return false;
            nr.fhss_support=static_cast<std::uint64_t>(support);nr.fhss_dwells_per_group=static_cast<std::uint64_t>(dwells);}
        auto native_bound=native::build(nr);if(!native_bound.valid)return false;
        coordinate_error=std::max(coordinate_error,translation_error+static_cast<long double>(native_bound.coordinate));
        if(coordinate_error>=.001L)return false;
        // Compute actual private limited-q bounds only at the two source mask
        // ends. RAW endpoint permission does not mark those cells observed.
        const auto source_cells=code.symbol_samples()/code.chip_samples()*subdivisions;
        const auto edge_cells=static_cast<std::uint64_t>(std::ceil(native_bound.edge_radius*subdivisions/code.chip_samples()))+1;
        if(edge_cells>=source_cells)return false;double source_edge_q=0;
        for(const auto end_side:{false,true})for(std::uint64_t j=0;j<edge_cells;++j){
            const auto cell=end_side?source_cells-1-j:j;
            coarse::Request request{code.symbol_samples(),code.chip_samples(),cell,subdivisions,amplitude,0,true,true};
            const auto proof=coarse::certify(request,[&](std::uint64_t k){return code.values(first_chip+k);},
                [&](double x){return code.shaped_values(first_chip,x);},stop);
            if(!proof.cell.supported)return false;
            for(unsigned bit=0;bit<2;++bit)source_edge_q=std::max(source_edge_q,cert::add(
                std::max(cert::norm(proof.cell.left[bit]),cert::norm(proof.cell.right[bit])),proof.cell.residual[bit]));}
        nr.source_q_upper=std::min(qmax,cert::mul(source_edge_q,gain_upper));
        native_bound=native::build(nr);if(!native_bound.valid)return false;
        const auto membership_radius=2.L+coordinate_error;

        const auto requested_fragment=lazy_linear_grid_fragment_cells();
        const auto actual_fragment=detail::scheduled_linear_fragment::effective(requested_fragment,cells);
        family::Geometry geometry{base,spacing,lazy_history_first(),frontier,cells,outputs,step,actual_fragment,std::span<const std::uint8_t>(original).first(outputs)};
        fft::Geometry transform_geometry{std::min(geometry.fragment_cells,cells),outputs,step,0,true,false};
        const auto reserve=family::reservation(geometry,fft::StreamingEngine<T>::reservation(transform_geometry));
        const auto old_scratch=execution.scratch_reservation;
        if(reserve>execution.available_bytes||old_scratch>execution.available_bytes-reserve)return false;
        if(execution.numeric_bytes()>execution.available_bytes-old_scratch-reserve)execution.drop_plans();
        if(execution.numeric_bytes()>execution.available_bytes-old_scratch-reserve)return false;
        struct Restore{LazyState::Execution&e;std::size_t n;~Restore(){e.scratch_reservation=n;}}restore{execution,old_scratch};
        execution.scratch_reservation=old_scratch+reserve;
        struct Pin{LazyState::Execution&e;explicit Pin(LazyState::Execution&x):e(x){e.screen_active=true;}~Pin(){e.screen_active=false;}}pin{execution};
        std::uint64_t coefficient_reads=0,endpoint_reads=0,gain_cells=0;
        const auto fhss_interval=[&](SchedHopCoordinate lo,SchedHopCoordinate hi,bool flat){
            if(!scheduled->fhss_shape.enabled)return true;const auto&shape=scheduled->fhss_shape;
            const auto two=2.L*config.sample_rate,period=two/5;
            auto a=std::fmod((lo.sample%(2ULL*config.sample_rate))+lo.fraction+shape.lag_center,two);if(a<0)a+=two;
            const auto width=hi.distance(lo);const auto b=a+width;
            const auto hop=std::floor(a/period);if(width<0||std::floor(b/period)!=hop)return false;
            const auto reserve=shape.quiet+(flat?shape.support:0)+lazy_fhss_coordinate_radius();
            return a-hop*period>reserve&&(hop+1)*period-b>reserve;
        };
        const auto cell=[&](std::size_t, long double left,long double right){cancelled(stop);grid::Cell output;
            const auto lo=phase.add_signed((left-reference.origin)*reference.rate-membership_radius);
            const auto hi=phase.add_signed((right-reference.origin)*reference.rate+membership_radius);
            const auto a=scheduled_map()->locate(lo),b=scheduled_map()->locate(hi);
            const auto first=a.source.distance(reference.anchor)-source_first,last=b.source.distance(reference.anchor)-source_first;
            const bool same_active=a.active&&b.active&&a.frame_index==b.frame_index&&a.physical_begin==b.physical_begin&&a.physical_end==b.physical_end;
            // active marks the entire source group, including protected zero
            // clock/audio shoulders. Prove the WHOLE enlarged interval quiet,
            // never infer it from two endpoint gains.
            const auto center=phase.add_signed(((left+right)/2-reference.origin)*reference.rate);
            const auto center_location=scheduled_map()->locate(center);
            const auto center_source=center_location.source.distance(reference.anchor)-source_first;
            const auto quiet_radius=zero::radius((right-left)*reference.rate/2,coordinate_error,magnitude,reference.rate);
            const bool zero_guard=same_active&&static_cast<bool>(zero::prove_current(center_location,center,center_source,code.symbol_samples(),quiet_radius));
            const bool quiet=zero_guard||(!a.active&&!b.active&&a.frame_index==b.frame_index)||last<0||first>=code.symbol_samples();
            if(quiet){output.supported=true;output.observed=false;output.residual={0,0};return output;}
            if(!a.active||!b.active||a.frame_index!=b.frame_index||first<=0||last>=code.symbol_samples()||
                !same_active||lo.distance(SchedHopCoordinate{a.rise_begin,0})<=0||SchedHopCoordinate{a.fall_end,0}.distance(hi)<=0||
                !fhss_interval(lo,hi,false))return output;
            const bool schedule_flat=a.continuous||(lo.distance(SchedHopCoordinate{a.flat_begin,0})>0&&
                SchedHopCoordinate{a.flat_end,0}.distance(hi)>0);
            const bool fhss_flat=fhss_interval(lo,hi,true);
            const auto tick=phase.add_signed((left-reference.origin)*reference.rate);
            const auto start=scheduled_map()->locate(tick).source.distance(reference.anchor)-source_first;
            const auto width=static_cast<long double>(code.chip_samples())/subdivisions;
            const auto nearest=std::round(start/width);if(nearest<0||nearest>0x1p40L)return output;
            const auto radius=coordinate_error+std::abs(start-nearest*width)+
                128*std::numeric_limits<long double>::epsilon()*(1+std::abs(start));
            coarse::Request request{code.symbol_samples(),code.chip_samples(),static_cast<std::uint64_t>(nearest),subdivisions,
                amplitude,cert::up(static_cast<double>(radius)),true};
            const auto proof=coarse::certify(request,[&](std::uint64_t k){return code.values(first_chip+k);},
                [&](double x){return code.shaped_values(first_chip,x);},stop);
            coefficient_reads+=proof.coefficient_reads;endpoint_reads+=proof.endpoint_reads;
            if(!proof.cell.supported)return output;
            const auto right_tick=phase.add_signed((right-reference.origin)*reference.rate);
            const auto left_location=scheduled_map()->locate(tick),right_location=scheduled_map()->locate(right_tick);
            gains::Request gr;gr.left_gain=lazy_screen_gain(left_location,tick);gr.right_gain=lazy_screen_gain(right_location,right_tick);
            gr.width=cert::up(static_cast<double>((right-left)*reference.rate));
            gr.coordinate_radius=cert::up(static_cast<double>(coordinate_error));gr.gain_upper=gain_upper;gr.amplitude=amplitude;
            gr.scheduled_ramp=schedule_flat?0:static_cast<double>(nr.scheduled_ramp);
            gr.fhss_ramp=fhss_flat?0:static_cast<double>(nr.fhss_support);gr.qualified=true;
            output=gains::apply(proof.cell,gr);if(output.supported&&(!schedule_flat||!fhss_flat))++gain_cells;return output;
        };
        const auto visit=[&](std::uint64_t first,std::uint64_t end_sample,auto emit){if(first>=end_sample)return;
            lazy_visit_blocks(first,end_sample,[&](const auto& block){cancelled(stop);
                const auto a=std::max(first,block.first),b=std::min<std::uint64_t>(end_sample,block.first+block.pcm.size());
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*banks[reference.frequency].frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto rotation=std::polar(1.,tau*banks[reference.frequency].frequency/config.sample_rate);
                for(auto n=block.first;n<b;++n){if((n&255U)==0)cancelled(stop);if(n>=a){const auto x=static_cast<double>(block.pcm[n-block.first]);
                    require(cert::norm(o)<=2,"linear grid raw carrier exceeds certificate contract");
                    emit(n,detail::CorrelationProjection{x*o.real(),x*o.imag(),o.real()*o.real(),o.imag()*o.imag(),o.real()*o.imag(),x*x});}o*=rotation;}
            },false);
        };
        // This is intentionally a direct-map PROXY, not an invented future
        // processing piece. native_bound encloses its differences from ActualOriginal.
        const auto proxy_row=[&](std::size_t j,std::uint64_t n){family::OriginalRow output;const auto lane=lazy_family_lane(range.family,range.first+j);
            const auto tick=phase.add_signed((static_cast<long double>(n)-lane.origin)*lane.rate);const auto location=scheduled_map()->locate(tick);
            const auto source=location.source.distance(lane.anchor)-source_first;const auto gain=lazy_screen_gain(location,tick);
            output.observed=location.active&&gain>0&&source>=0&&source<code.symbol_samples();if(output.observed){const auto raw=code.shaped_values(first_chip,static_cast<double>(source));
                for(unsigned b=0;b<2;++b)output.q[b]=gain*pattern_limit_pcm(amplitude*raw[b])/amplitude;}return output;
        };
        family::Result result;
        try{result=family::run<T>(geometry,reserve,cell,visit,proxy_row,stop);}
        catch(const Error&){if(stop.stop_requested())throw;return false;}
        const auto maximum=lazy_state()->summary_maximum,energy=lazy_state()->summary_union_energy;

        // These reference radii describe raw-fence execution. The SERIAL
        // lazy_cold_upper consumer independently calls lazy_carrier_restart,
        // fails closed on unavailable proof, and adds actual raw/processing
        // restart radius+magnitude. Never update that shared cache in workers.
        std::vector<cert::ReferenceErrors> original_errors(outputs);
        for(std::size_t j=0;j<outputs;++j){cancelled(stop);const auto count=result.values[j].fits[0].count;if(native_bound.mask_count>UINT64_MAX-count)return false;const auto n=count+native_bound.mask_count;
            if(n>(SIZE_MAX-128*block_samples-128)/128)return false;
            const auto rho=cert::add(cert::gamma(128*static_cast<std::size_t>(n)+128*block_samples+128,std::numeric_limits<double>::epsilon()/2),
                cert::mul(4*block_samples,cert::gamma(8*block_samples+8,std::numeric_limits<double>::epsilon()/2)));
            original_errors[j]={cert::mul(8*rho*qmax,cert::root(cert::mul(static_cast<double>(n),energy))),cert::mul(32*rho*n,qmax*qmax),
                cert::mul(4*rho,energy),n>2?1e-8/(.5*static_cast<double>(n-2)):0.,true};
        }
        cancelled(stop);range.screen=std::move(result.values);range.reference=std::move(original_errors);
        range.independent_whole_template=true;range.exact_observation_mask=false;range.mask_difference=native_bound.mask_count;
        range.numeric_origin_stride=1;range.screen_first=range.first;range.maximum=maximum;
        // Supported cells prove the actual current/positive predicate over the
        // expanded native/ceil interval, and their chord already includes the
        // complete source radius. The new native bound belongs only to the
        // DISJOINT unsupported/direct rows (all rows of a forced lane). Thus
        // squared masses add; this is not an invalid overlapping L2 merge.
        range.proxy_residual_square=cert::add(result.residual_square,native_bound.square);
        range.proxy_residual_l1=cert::add(result.residual_l1,native_bound.l1);
        range.proxy_residual_gram=cert::add(result.residual_gram,native_bound.gram);
        range.certificate_cohort=certdiag::cohort<T>(true);range.colored.clear();range.colored_sample_pairs=0;range.colored_selected=0;range.colored_mode=0;range.colored_gate=0;
        range.colored_residual_square=range.colored_residual_l1=range.colored_residual_gram=0;
        execution.input_transforms+=result.work.transforms.input_ffts;execution.template_transforms+=result.work.transforms.template_ffts;execution.inverse_transforms+=result.work.transforms.inverse_ffts;
        execution.peak_prepared_bytes=std::max(execution.peak_prepared_bytes,execution.numeric_bytes()+old_scratch+reserve);
        if constexpr(std::is_same_v<T,float>)++execution.fp32_ranges;else ++execution.fp64_ranges;++execution.proxy_ranges;
        audio::trace::record("scheduled_linear_grid",[&](auto&f){f.add("epoch",config.stream_epoch);f.add("lanes",outputs);f.add("subdivisions",subdivisions);
            f.add("node_spacing",static_cast<double>(spacing));f.add("cells",cells);f.add("fragment_cells",actual_fragment);f.add("requested_fragment_cells",requested_fragment);f.add("original_lanes",std::count(original.begin(),original.end(),1));
            f.add("supported_cells",result.work.supported_cells);f.add("original_cells",result.work.original_cells);f.add("quiet_cells",result.work.quiet_cells);
            f.add("gain_cells",gain_cells);f.add("coefficient_reads",coefficient_reads);f.add("endpoint_reads",endpoint_reads);f.add("original_visits",result.work.original_visits);f.add("original_runs",result.work.original_runs);f.add("original_run_cells",result.work.original_run_cells);
            f.add("input_visits",result.work.input_visits);f.add("skipped_fragments",result.work.skipped_fragments);f.add("workspace",reserve);
            f.add("residual_square",result.residual_square);f.add("residual_l1",result.residual_l1);f.add("residual_gram",result.residual_gram);
            f.add("native_source_radius",native_bound.coordinate);f.add("source_edge_q_upper",nr.source_q_upper);
            f.add("native_square",native_bound.square);f.add("native_l1",native_bound.l1);f.add("native_gram",native_bound.gram);
            f.add("schedule_edge_gain",native_bound.scheduled_gain);f.add("fhss_edge_gain",native_bound.fhss_gain);f.add("mask_difference",native_bound.mask_count);
            f.add("scope","opt-in exact-clock coarse-cell proxy FFT; native masks/guard/refinement retained");});return true;
    }

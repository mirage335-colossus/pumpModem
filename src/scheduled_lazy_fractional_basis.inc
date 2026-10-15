// Included in PatternCorrelator::Impl; bounded diagnostic prototype.
    template<class T> bool lazy_screen_fractional_basis(LazyState::DueRange& range,std::stop_token stop,std::size_t origin_stride=1) {
        rx_cpu::Scope cpu_screen(rx_cpu::Stage::search_residual);
        namespace tile=detail::scheduled_acquisition_tile;
        namespace origin_gram=detail::scheduled_origin_gram;
        const auto* gram_policy=std::getenv("DATAPUMP_SCHED_ORIGIN_GRAM");
        const bool use_origin_gram=gram_policy && std::string_view(gram_policy)=="1";
        const auto execution_mode=use_origin_gram?origin_gram::Mode::external_gram:origin_gram::Mode::complete;
        const auto gram_build=origin_gram::requested_build();
        namespace cert=detail::scheduled_acquisition_certificate;
        namespace basis=detail::scheduled_fractional_basis;
        namespace chord=detail::scheduled_pulse_chord;
        namespace gch=detail::scheduled_gain_chord;
        namespace attribution=detail::scheduled_template_cpu_attribution;
        attribution::Recorder component_cpu(attribution::requested());
        auto* component=component_cpu.enabled()?&component_cpu:nullptr;bool component_complete=false;
        attribution::OnExit component_report{[&] {
            if(component_cpu.enabled())audio::trace::record("scheduled_template_component_cpu",[&](auto& f) {
                f.add("kind","scalar");f.add("epoch",config.stream_epoch);f.add("complete",component_complete);component_cpu.fields(f);
            });
        }};
        namespace derivative=detail::scheduled_pulse_derivative_chord;
        const auto* aggregate_policy=std::getenv("DATAPUMP_SCHED_DERIVATIVE_AGGREGATE");
        const bool use_derivative_aggregate=aggregate_policy && std::string_view(aggregate_policy)=="1";
        const auto* derivative_policy=std::getenv("DATAPUMP_SCHED_DERIVATIVE_CHORD");
        const bool use_derivative_chord=use_derivative_aggregate || (derivative_policy && std::string_view(derivative_policy)=="1");
        const bool periodic_cell_qualified=gch::periodic_cell_math_qualified();
        if(!lazy_fractional_basis_requested() || config.search_arithmetic==SearchArithmetic::fp64)return false;
        range.independent_whole_template=true;
        range.exact_observation_mask=false;
        if(!lazy_reference_environment() || !shaped || !lazy_fhss_screen_supported() || range.first==range.end)return lazy_screen_gate("fractional_basis",__LINE__);
        auto& execution=lazy_execution();auto& code=execution.code;
        if(lazy_state()->certificate_profile.enabled)execution.certificate_pending={};
        const auto reference=lazy_family_lane(range.family,range.first);
        require(origin_stride==1 || origin_stride==2,"unsupported numerical origin stride");
        const auto size=1+(range.end-range.first-1)/origin_stride;
        if(size>tile::maximum_start_span || scheduled_map()->source_samples_per_group()<4)return lazy_screen_gate("fractional_basis",__LINE__);
        std::vector<std::size_t> starts;starts.reserve(size);std::vector<double> weights;weights.reserve(size);
        long double displacement=0;
        constexpr long double half_width=.5L;
        if(range.index==std::numeric_limits<std::uint64_t>::max())return lazy_screen_gate("fractional_basis_index",__LINE__);
        const auto source_first=static_cast<long double>(range.index)*code.symbol_samples();
        const auto first_boundary=lazy_state()->catalog->boundary(reference,source_first,false,execution.cursor);
        const auto last_boundary=lazy_state()->catalog->boundary(reference,source_first+code.symbol_samples(),false,execution.cursor);
        const auto original_aperture=std::max(std::abs(first_boundary-reference.origin),std::abs(last_boundary-reference.origin))+tile::maximum_start_span+2;
        for(std::size_t j=0;j<size;++j){const auto lane=lazy_family_lane(range.family,range.first+j*origin_stride);
            const auto delta=lane.origin-reference.origin,integer=std::round(delta);
            if(integer<0 || integer>=tile::maximum_start_span || (!starts.empty() && integer<=starts.back()))return lazy_screen_gate("fractional_basis",__LINE__);
            starts.push_back(static_cast<std::size_t>(integer));
            if(lane.rate!=reference.rate || lane.phase_lower!=reference.phase_lower ||
               lane.anchor.distance(reference.anchor)!=0)return lazy_screen_gate("fractional_basis_identity",__LINE__);
            const auto delta_fraction=delta-integer;
            const auto weight=(delta_fraction+half_width)/(2*half_width);
            if(!(weight>=0 && weight<=1))return lazy_screen_gate("fractional_basis_weight",__LINE__);
            weights.push_back(static_cast<double>(weight));
        }
        if(displacement>0 && config.search_arithmetic==SearchArithmetic::fp64)return lazy_screen_gate("fractional_basis",__LINE__);
        displacement+=256*std::numeric_limits<long double>::epsilon()*(1+scheduled->complex_geometry.physical_aperture_samples+
            std::abs(reference.origin)+code.symbol_samples());
        displacement+=lazy_fhss_coordinate_radius();
        // Preserve the original reserve and additionally enclose actual late
        // physical/source intermediate subtraction before residual construction.
        displacement+=1024*std::numeric_limits<long double>::epsilon()*
            (1+original_aperture+std::abs(reference.origin)+source_first+code.symbol_samples());
        if(!(reference.rate>=.99L && reference.rate<=1.01L) || displacement>.001L)
            return lazy_screen_gate("fractional_basis_rounding",__LINE__);
        const auto span=starts.back()+1;
        const auto available=execution.available_bytes;
        const auto shape=detail::scheduled_parallel_preparation::shape(span);const auto fragment_samples=shape.maximum_fragment;
        const auto transform=shape.transform_size();
        const bool native_accumulation=std::is_same_v<T,float> && config.search_arithmetic!=SearchArithmetic::fp64 &&
            detail::scheduled_parallel_preparation::native_float_accumulation_requested();
        const auto shift=static_cast<std::uint64_t>(std::ceil(std::max(0.L,-reference.origin))+fragment_samples);
        const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
        const auto begin=lazy_state()->catalog->boundary(reference,static_cast<long double>(range.index)*code.symbol_samples(),false,execution.cursor);
        const auto central_end=lazy_state()->catalog->boundary(reference,static_cast<long double>(range.index+1)*code.symbol_samples(),false,execution.cursor);
        if(static_cast<long double>(lazy_history_first())>std::max(0.L,std::floor(begin-.5L-displacement/reference.rate)-1))return lazy_screen_gate("fractional_basis",__LINE__);

        auto v=static_cast<std::uint64_t>(std::max(0.L,std::floor(begin+shift)-1));
        const auto until=static_cast<std::uint64_t>(std::max(0.L,std::ceil(central_end+shift)+2));
        namespace zero_guard=detail::scheduled_zero_guard;
        const auto zero_magnitude=2*(1.L+std::abs(reference.origin)+lazy_state()->capture_end+until+shift+
            span+fragment_samples+code.symbol_samples()+source_first+reference.phase_lower);
        const auto zero_guard_radius=zero_guard::radius(half_width*reference.rate,displacement,zero_magnitude,reference.rate);
        std::vector<std::uint64_t> gram_checkpoints;
        std::vector<std::pair<std::uint64_t,std::uint64_t>> gram_crops;
        if(use_origin_gram) {
            if(until>static_cast<std::uint64_t>(origin_gram::Powers::maximum_exponent)||
               shift>static_cast<std::uint64_t>(origin_gram::Powers::maximum_exponent)||
               lazy_state()->capture_end>static_cast<std::uint64_t>(origin_gram::Powers::maximum_exponent))
                return lazy_screen_gate("origin_gram_physical_extent",__LINE__);
            const auto metadata=tile::add_size(8192,tile::add_size(
                tile::multiply_size(2*size+2,sizeof(std::uint64_t)),
                tile::multiply_size(size,sizeof(std::pair<std::uint64_t,std::uint64_t>))));
            const auto occupied=tile::add_size(execution.numeric_bytes(),execution.scratch_reservation);
            if(occupied>available || metadata>available-occupied)return lazy_screen_gate("origin_gram_crop_workspace",__LINE__);
            gram_checkpoints.reserve(2*size+2);gram_crops.reserve(size);
            gram_checkpoints.push_back(0);gram_checkpoints.push_back(until);
            for(const auto start:starts) {
                // All operands are integers below2^40; these intermediate sums
                // remain exact even where long double is IEEE binary64.
                const auto lo=static_cast<std::uint64_t>(std::clamp(
                    static_cast<long double>(lazy_history_first())+shift-start,0.L,static_cast<long double>(until)));
                const auto hi=static_cast<std::uint64_t>(std::clamp(
                    static_cast<long double>(lazy_state()->capture_end)+shift-start,0.L,static_cast<long double>(until)));
                gram_crops.emplace_back(lo,hi);gram_checkpoints.push_back(lo);gram_checkpoints.push_back(hi);
            }
            std::sort(gram_checkpoints.begin(),gram_checkpoints.end());
            gram_checkpoints.erase(std::unique(gram_checkpoints.begin(),gram_checkpoints.end()),gram_checkpoints.end());
        }
        const auto gram_reservation=use_origin_gram?tile::add_size(16384,
            tile::add_size(tile::add_size(origin_gram::Prefix<3>::reservation(gram_checkpoints.size()),
                    tile::add_size(tile::multiply_size(gram_checkpoints.capacity(),sizeof(std::uint64_t)),
                        tile::multiply_size(gram_crops.capacity(),sizeof(gram_crops[0])))),
                tile::add_size(sizeof(origin_gram::RawReference)+sizeof(origin_gram::Powers),
                    tile::multiply_size(size,3*sizeof(std::array<origin_gram::Gram,2>))))):0;
        const auto derivative_setup_bytes=use_derivative_chord?derivative::DerivativeTable::reservation():0;
        const auto reservation=tile::add_size(derivative_setup_bytes,tile::add_size(gram_reservation,tile::multiply_size(3,
            detail::scheduled_parallel_preparation::scratch_bytes(span,size,fragment_samples,native_accumulation))));
        const auto previous_scratch=execution.scratch_reservation;
        struct RestoreScratch {LazyState::Execution& execution;std::size_t previous;
            ~RestoreScratch(){execution.scratch_reservation=previous;}} restore_scratch{execution,previous_scratch};
        std::unique_ptr<derivative::DerivativeTable> derivative_table;
        execution.scratch_reservation=tile::add_size(previous_scratch,reservation);
        if(execution.scratch_reservation>available)return lazy_screen_gate("fractional_basis",__LINE__);
        if(execution.numeric_bytes()>available-execution.scratch_reservation)execution.drop_plans();
        rx_cpu::Scope cpu_plan(rx_cpu::Stage::setup);
        const auto& plan=lazy_plan<T>(shape,stop);cpu_plan.finish();
        struct ScreenPin {LazyState::Execution& state;explicit ScreenPin(LazyState::Execution& s):state(s){state.screen_active=true;}
            ~ScreenPin(){state.screen_active=false;}} screen_pin{execution};
        tile::Accumulator<T> accumulator(plan,0,starts,available,stop,lazy_state()->certificate_profile.enabled,native_accumulation,execution_mode);
        tile::Accumulator<T> second(plan,0,starts,available,stop,lazy_state()->certificate_profile.enabled,native_accumulation,execution_mode);
        tile::Accumulator<T> difference(plan,0,starts,available,stop,lazy_state()->certificate_profile.enabled,native_accumulation,execution_mode);
        basis::DifferenceCertificate subtraction;gch::Totals gain_totals;
        std::uint64_t gain_chord_rows=0,fhss_flat_rows=0,fhss_unproved_rows=0;
        std::uint64_t chord_rows=0,chord_evaluations=0,coarse_rows=0,endpoint_fallback_rows=0,zero_guard_rows=0;
        std::uint64_t derivative_aggregate_rows=0,derivative_aggregate_fallback_rows=0,derivative_aggregate_valid_rows=0;
        std::vector<float> pcm(span+fragment_samples-1);
        std::vector<Complex> oscillator(pcm.size());std::vector<std::uint8_t> observed(pcm.size());
        std::vector<std::array<Complex,2>> rows(fragment_samples),rows_b(fragment_samples),rows_d(fragment_samples);
        std::vector<std::uint8_t> mask(rows.size());
        struct Wipe {decltype(rows)& a;decltype(rows)& b;decltype(rows)& d;~Wipe(){
            for(auto* q:{&a,&b,&d})if(!q->empty())OPENSSL_cleanse(q->data(),q->size()*sizeof((*q)[0]));}} wipe{rows,rows_b,rows_d};
        // Tokens are invocation-local and never stored in a reusable cache. The
        // family tag covers both independently addressed endpoint rows and D;
        // explicit variant identities prevent accidentally swapping A/B/D.
        struct GramFamilyTag{} gram_family_tag;
        const auto gram_identity=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&gram_family_tag));
        std::unique_ptr<origin_gram::Powers> gram_powers;
        std::unique_ptr<origin_gram::RawReference> gram_reference;
        std::unique_ptr<origin_gram::Prefix<3>> gram_prefix;
        std::array<std::vector<std::array<origin_gram::Gram,2>>,3> gram_outputs;
        const auto actual_gram_bytes=[&] {
            std::size_t bytes=tile::add_size(tile::multiply_size(gram_checkpoints.capacity(),sizeof(std::uint64_t)),
                tile::multiply_size(gram_crops.capacity(),sizeof(gram_crops[0])));
            if(gram_powers)bytes=tile::add_size(bytes,sizeof(*gram_powers));
            if(gram_reference)bytes=tile::add_size(bytes,gram_reference->working_bytes());
            if(gram_prefix)bytes=tile::add_size(bytes,gram_prefix->working_bytes());
            for(const auto& output:gram_outputs)bytes=tile::add_size(bytes,tile::multiply_size(output.capacity(),sizeof(output[0])));
            return bytes;
        };
        const auto actual_bytes=[&] {
            return tile::add_size(derivative_table?derivative_table->working_bytes():0,tile::add_size(actual_gram_bytes(),tile::add_size(16384,tile::add_size(accumulator.working_bytes()+second.working_bytes()+difference.working_bytes(),
                tile::add_size(tile::multiply_size(starts.capacity(),sizeof(std::size_t))+tile::multiply_size(weights.capacity(),sizeof(double)),
                tile::add_size(tile::multiply_size(pcm.capacity(),sizeof(float))+tile::multiply_size(oscillator.capacity(),sizeof(Complex))+observed.capacity(),
                    tile::multiply_size(rows.capacity()+rows_b.capacity()+rows_d.capacity(),sizeof(rows[0]))+mask.capacity()))))));
        };
        const auto check_actual=[&](std::size_t extra=0) {
            const auto live=tile::add_size(actual_bytes(),extra);
            require(live<=reservation,"fractional basis live chain exceeds preflight");
            const auto total=tile::add_size(execution.numeric_bytes(),tile::add_size(previous_scratch,live));
            require(total<=available,"fractional basis live chain exceeds worker loan");
            execution.peak_prepared_bytes=std::max(execution.peak_prepared_bytes,total);
        };check_actual();
        if(use_derivative_chord) {
            // Before allocation, include the immutable table AND constructor
            // monotonic queues. Later live checks charge only the actual table.
            check_actual(derivative_setup_bytes);
            rx_cpu::Scope cpu_derivative_setup(rx_cpu::Stage::setup);
            attribution::Span component_table(component,attribution::Bucket::table_setup);
            derivative_table=std::make_unique<derivative::DerivativeTable>(code.chip_samples(),derivative_setup_bytes,stop,
                use_derivative_aggregate?derivative::AggregateWidth::source_33_over_32:derivative::AggregateWidth::disabled);
            if(!derivative_table->valid())return lazy_screen_gate("fractional_derivative_table",__LINE__);
            component_table.finish();
            check_actual();
        }
        const auto chord_temporary_bytes=use_derivative_chord?8192:chord::reservation(2048);
        const auto chord_workspace=use_derivative_chord?
            tile::add_size(derivative_table->working_bytes(),chord_temporary_bytes):chord_temporary_bytes;

        if(use_origin_gram) {
            if(until>static_cast<std::uint64_t>(origin_gram::Powers::maximum_exponent))
                return lazy_screen_gate("origin_gram_physical_extent",__LINE__);
            rx_cpu::Scope cpu_gram_reference(rx_cpu::Stage::mixing);
            const auto step=std::polar(1.,tau*banks[reference.frequency].frequency/config.sample_rate);
            gram_powers=std::make_unique<origin_gram::Powers>(step);
            // This object's lifetime binds the same immutable captured block
            // generation used below; no foreign/reused prepared input is used.
            gram_reference=std::make_unique<origin_gram::RawReference>(*gram_powers,
                static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&gram_reference)));
            bool supported=true;
            const auto first=static_cast<std::uint64_t>(std::max<long double>(lazy_history_first(),
                std::max(0.L,static_cast<long double>(v)-shift)));
            const auto end=static_cast<std::uint64_t>(std::max(0.L,std::min<long double>(lazy_state()->capture_end,
                static_cast<long double>(until)-shift+starts.back())));
            if(first<end)lazy_visit_blocks(first,end,[&](const auto& block) {
                cancelled(stop);
                const auto seed=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*
                    banks[reference.frequency].frequency/config.sample_rate,static_cast<long double>(tau))));
                supported&=gram_reference->append(block.first,block.pcm.size(),seed);
            },false);
            if(!supported||!gram_reference->valid())return lazy_screen_gate("origin_gram_raw_fences",__LINE__);
            gram_prefix=std::make_unique<origin_gram::Prefix<3>>(*gram_powers,gram_identity,gram_checkpoints,gram_reservation,4096,gram_build);
            for(auto& output:gram_outputs)output.resize(size);
            check_actual();
        }
        const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
        const auto qmax=cert::add(cert::mul(cert::mul(cert::up(pattern_pcm_radius_limit/amplitude),
            detail::scheduled_cold_colored::smooth_gain_upper()),cert::add(1,cert::gamma(32,std::numeric_limits<double>::epsilon()/2))),
            cert::divide(128*std::numeric_limits<double>::denorm_min(),amplitude));
        static const auto pulse_slope=[] {double value=0;for(unsigned i=0;i<2048;++i)value=std::max(value,
            std::abs(pattern_pulse((i+1)/256.)-pattern_pulse(i/256.))*256);return std::nextafter(value*1.000000000001,std::numeric_limits<double>::infinity());}();
        const auto gate_slope=lazy_screen_gate_slope();
        const auto smooth=cert::mul(static_cast<double>(displacement),cert::add(cert::divide(34*pulse_slope,code.chip_samples()),cert::mul(gate_slope,qmax)));
        const auto jump=cert::up(4*std::abs(pattern_pulse(8.)));
        double residual_square=0,residual_l1=0,residual_gram=0,maximum=0,union_energy=0;
        std::uint64_t groups=0,last_group=std::numeric_limits<std::uint64_t>::max();
        maximum=lazy_state()->summary_maximum;union_energy=lazy_state()->summary_union_energy;
        code.set_stream_phase_samples(reference.phase_lower);
        require(range.index<=std::numeric_limits<std::uint64_t>::max()/code.chips_per_symbol(),"implicit template chip coordinate overflow");
        const auto chip_first=range.index*code.chips_per_symbol();attribution::Span component_prepare(component,attribution::Bucket::source_prepare);
        code.prepare_symbol(chip_first,stop);component_prepare.finish();
        attribution::Span component_reader_setup(component,attribution::Bucket::source_reader);
        detail::scheduled_source_samples::Reader source_samples(config,code,chip_first,reference.rate);component_reader_setup.finish();
        range.colored.clear();range.colored_sample_pairs=0;range.colored_selected=0;range.colored_mode=0;range.colored_gate=0;
        range.colored_residual_square=range.colored_residual_l1=range.colored_residual_gram=0;
        // Chip guards are supplied independently by the unchanged original
        // demand path. No A/B/D chip summary may pretend to be original q.
        const bool exact_mask=false;
        ResidualPrefixWork residual_work;range.reference.resize(size);
        for(auto& e:range.reference)e.conditioned_proxy_dot_upper=cert::infinity;
        while(v<until){cancelled(stop);
            const auto real=static_cast<long double>(v)-shift;
            const auto tick=phase.add_signed((real-reference.origin)*reference.rate);const auto location=scheduled_map()->locate(tick);
            if(!location.active) {
                // BOTH outward endpoints must lie in the same quiet group.
                // Keep two physical halo rows at both edges, including the
                // original piece-association reserve. Never skip an unknown
                // original-only observation merely because central q is zero.
                const auto pad=half_width+displacement/reference.rate+2;
                const auto left=scheduled_map()->locate(phase.add_signed((real-reference.origin-pad)*reference.rate));
                const auto right=scheduled_map()->locate(phase.add_signed((real-reference.origin+pad)*reference.rate));
                if(!left.active && !right.active && left.frame_index==right.frame_index) {
                    const auto next=scheduled_map()->source_to_physical(location.source,false);
                    const auto next_real=reference.origin+next.distance(phase)/reference.rate;
                    const auto target=static_cast<std::uint64_t>(std::max(0.L,std::floor(next_real+shift-pad)));
                    if(target>v+1){v=std::min(target,until);continue;}
                }
            }
            const auto count=static_cast<std::size_t>(std::min<std::uint64_t>(rows.size(),until-v));
            std::fill(rows.begin(),rows.end(),std::array<Complex,2>{});
            std::fill(rows_b.begin(),rows_b.end(),std::array<Complex,2>{});
            std::fill(rows_d.begin(),rows_d.end(),std::array<Complex,2>{});std::fill(mask.begin(),mask.end(),0);
            rx_cpu::Scope cpu_template(rx_cpu::Stage::template_build);
            for(std::size_t i=0;i<count;++i) {
                if((i&63U)==0)cancelled(stop);
                attribution::Span component_geometry(component,attribution::Bucket::row_geometry);
                component_cpu.observe(attribution::Counter::rows);
                const auto p=static_cast<long double>(v+i)-shift;
                const auto t=phase.add_signed((p-reference.origin)*reference.rate);
                const auto l=scheduled_map()->locate(t);
                const auto source=l.source.distance(reference.anchor)-source_first;
                const auto gain=lazy_screen_gain(l,t);
                if(l.active && l.frame_index!=last_group){++groups;last_group=l.frame_index;}
                const auto low_tick=t.add_signed(-half_width*reference.rate);
                const auto high_tick=t.add_signed(half_width*reference.rate);
                const auto low=scheduled_map()->locate(low_tick),high=scheduled_map()->locate(high_tick);
                const auto low_source=low.source.distance(reference.anchor)-source_first;
                const auto high_source=high.source.distance(reference.anchor)-source_first;
                const auto low_gain=lazy_screen_gain(low,low_tick),high_gain=lazy_screen_gain(high,high_tick);
                const bool selected=gain>0 && source>=0 && source<code.symbol_samples();
                if(selected)component_cpu.observe(attribution::Counter::selected_rows);
                mask[i]=selected?1:0;
                const auto make=[&](const auto& at,long double coordinate,double g) {
                    std::array<Complex,2> q{};
                    if(at.active && g>0 && coordinate>=0 && coordinate<code.symbol_samples()) {
                        attribution::Span component_reader(component,attribution::Bucket::fallback_reader);
                        const auto raw=source_samples.value(coordinate,stop);component_reader.finish();
                        for(unsigned bit=0;bit<2;++bit)q[bit]=g*pattern_limit_pcm(amplitude*raw[bit])/amplitude;
                    }return q;
                };
                // Endpoint A is origin-h (higher source); B is origin+h.
                // All three spectra use exactly the SAME central mask. Rows
                // outside it remain zero; original-only rows retain residual.
                double residual=0;
                const auto extended_low=source-half_width*reference.rate-displacement;
                const auto extended_high=source+half_width*reference.rate+displacement;
                const bool same_group=l.active && low.active && high.active &&
                    l.frame_index==low.frame_index && l.frame_index==high.frame_index;
                // The only zero residual exterior is structurally quiet on
                // BOTH enlarged endpoints, or entirely outside this symbol.
                const auto before=scheduled_map()->locate(t.add_signed(-half_width*reference.rate-displacement));
                const auto after=scheduled_map()->locate(t.add_signed(half_width*reference.rate+displacement));
                const bool exact_zero_guard=gain==0 && static_cast<bool>(zero_guard::prove_current(l,t,
                    source,code.symbol_samples(),zero_guard_radius));
                if(exact_zero_guard)++zero_guard_rows;
                const bool quiet=exact_zero_guard || (!l.active && !before.active && !after.active &&
                    l.frame_index==before.frame_index && l.frame_index==after.frame_index);
                const bool outside=extended_high<0 || extended_low>=code.symbol_samples();
                const bool interior=selected && same_group && extended_low>0 && extended_high<code.symbol_samples();
                const bool scheduled_flat=interior && SchedHopSourceMap::gain(l,t)==1 &&
                    SchedHopSourceMap::gain(low,low_tick)==1 && SchedHopSourceMap::gain(high,high_tick)==1 &&
                    t.distance(SchedHopCoordinate{l.flat_begin,0})>half_width*reference.rate+displacement &&
                    SchedHopCoordinate{l.flat_end,0}.distance(t)>half_width*reference.rate+displacement;
                component_geometry.finish();
                bool coarse=false,fhss_flat=false,endpoints_proven=false;
                if(!quiet && !outside) {
                    if(interior) {
                        const auto lo=static_cast<double>(low_source),hi=static_cast<double>(high_source);
                        const chord::Request request{lo,hi,amplitude,1,2,code.symbol_samples(),code.chip_samples(),
                            2048,chord_workspace,true,true};
                        attribution::Span component_workspace(component,attribution::Bucket::workspace_checks);
                        check_actual(chord_temporary_bytes);component_workspace.finish();
                        component_cpu.observe(attribution::Counter::proof_attempts);
                        attribution::Span component_proof(component,attribution::Bucket::proof_inclusive);
                        derivative::ExecutionReport derivative_report{use_derivative_aggregate?derivative::Execution::aggregate:derivative::Execution::reference};
                        const auto proof=gch::certify_row(request,attribution::wrap_original(component,[&](double coordinate,std::stop_token token) {
                            cancelled(token);return code.shaped_values(chip_first,coordinate);
                        }),l,scheduled->fhss_shape,t,half_width*reference.rate,displacement,lazy_fhss_coordinate_radius(),
                            config.sample_rate,low_gain,high_gain,scheduled_flat,periodic_cell_qualified,stop,derivative_table.get(),use_derivative_chord?&derivative_report:nullptr);
                        component_proof.finish();if(proof.value.valid)component_cpu.observe(attribution::Counter::proof_valid);
                        attribution::Span component_proof_result(component,attribution::Bucket::target_residual);
                        if(derivative_report.aggregate_used)++derivative_aggregate_rows;
                        if(derivative_report.aggregate_fallback)++derivative_aggregate_fallback_rows;
                        if(derivative_report.aggregate_used && proof.value.valid)++derivative_aggregate_valid_rows;
                        chord_evaluations+=proof.evaluations;fhss_flat=proof.fhss_flat;
                        if(proof.value.valid) {
                            rows[i]=proof.value.right;rows_b[i]=proof.value.left;endpoints_proven=true;++chord_rows;++gain_chord_rows;
                            if(scheduled->fhss_shape.enabled && fhss_flat)++fhss_flat_rows;
                            residual=std::max(proof.value.residual[0],proof.value.residual[1]);
                            const auto coordinate_error=proof.coordinate_error;
                            residual=cert::add(residual,cert::mul(proof.gain_upper,cert::mul(coordinate_error,
                                cert::divide(40*pulse_slope,code.chip_samples()))));
                            residual=cert::add(residual,cert::mul(8*std::numeric_limits<double>::epsilon(),qmax));
                            const auto radius=half_width*reference.rate+static_cast<long double>(coordinate_error);
                            const auto edge=source/code.chip_samples()-.5L;
                            if(std::abs(edge-std::round(edge))*code.chip_samples()<=radius)
                                residual=cert::add(residual,cert::mul(proof.gain_upper,jump));
                            const auto tail=code.symbol_samples()%code.chip_samples();
                            if(tail){const auto center=static_cast<long double>(code.symbol_samples()-tail)+.5L*tail;
                                if(std::min(std::abs(source-center-8.L*code.chip_samples()),std::abs(source-center+8.L*code.chip_samples()))<=radius)
                                    residual=cert::add(residual,cert::mul(proof.gain_upper,jump));}
                        }else {coarse=true;++coarse_rows;if(scheduled->fhss_shape.enabled && !fhss_flat)++fhss_unproved_rows;}
                    }else {coarse=true;++coarse_rows;}
                }
                // A successful certificate already supplied the exact stored
                // endpoint rows. Construct the original fallback only when it
                // will survive; do not evaluate then overwrite private q.
                if(selected && !endpoints_proven) {
                    attribution::Span component_fallback(component,attribution::Bucket::fallback_endpoints);
                    component_cpu.observe(attribution::Counter::fallback_rows);
                    rows[i]=make(high,high_source,high_gain);rows_b[i]=make(low,low_source,low_gain);
                    ++endpoint_fallback_rows;
                }
                // Exact subtraction is certified against the ACTUAL rows
                // passed to native transforms. Additional zero rows only make
                // its total residual/count certificate more conservative.
                attribution::Span component_difference(component,attribution::Bucket::difference_norm);
                rows_d[i]=subtraction.subtract(rows[i],rows_b[i]);
                const auto norm=std::max({cert::norm(rows[i][0]),cert::norm(rows[i][1]),
                    cert::norm(rows_b[i][0]),cert::norm(rows_b[i][1])});component_difference.finish();
                attribution::Span component_residual(component,attribution::Bucket::target_residual);
                component_cpu.observe(attribution::Counter::target_pairs,1);
                double target_upper=0;
                if(coarse) {
                    residual=gch::coarse_residual(cert::down(static_cast<double>(std::max(0.L,extended_low))),
                        cert::up(static_cast<double>(std::min<long double>(code.symbol_samples(),extended_high))),
                        code.symbol_samples(),code.chip_samples(),amplitude,detail::scheduled_cold_colored::smooth_gain_upper(),
                        norm,cert::mul(2,qmax),true,&target_upper);
                }
                const auto kind=!interior?gch::Kind::window:(scheduled->fhss_shape.enabled?gch::Kind::fhss:
                    (scheduled_flat?gch::Kind::flat:gch::Kind::ramp));
                gain_totals.observe(kind,residual,norm,coarse?target_upper:0,coarse?norm:0);
                residual_square=cert::add(residual_square,cert::mul(residual,residual));
                residual_l1=cert::add(residual_l1,residual);
                residual_gram=cert::add(residual_gram,cert::add(cert::mul(2*norm,residual),cert::mul(residual,residual)));
            }
            if(gram_prefix){attribution::Span component_gram(component,attribution::Bucket::gram_append);origin_gram::append_three(*gram_prefix,v,
                std::span<const std::array<Complex,2>>(rows).first(count),
                std::span<const std::array<Complex,2>>(rows_b).first(count),
                std::span<const std::array<Complex,2>>(rows_d).first(count),
                std::span<const std::uint8_t>(mask).first(count),stop);}
            cpu_template.finish();rx_cpu::Scope cpu_mix(rx_cpu::Stage::mixing);
            std::fill(pcm.begin(),pcm.end(),0);std::fill(observed.begin(),observed.end(),0);std::fill(oscillator.begin(),oscillator.end(),Complex{});
            const auto input_first=static_cast<long double>(v)-shift;
            const auto captured_first=static_cast<std::uint64_t>(std::max<long double>(lazy_history_first(),std::max(0.L,input_first)));
            const auto captured_end=static_cast<std::uint64_t>(std::max(0.L,std::min<long double>(lazy_state()->capture_end,input_first+pcm.size())));
            if(captured_first<captured_end)lazy_visit_blocks(captured_first,captured_end,[&](const auto& block) {
                const auto a=static_cast<std::uint64_t>(std::max<long double>(block.first,std::max(0.L,input_first)));
                const auto b=static_cast<std::uint64_t>(std::min<long double>(block.first+block.pcm.size(),input_first+pcm.size()));
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*banks[reference.frequency].frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto step=std::polar(1.,tau*banks[reference.frequency].frequency/config.sample_rate);
                for(std::uint64_t n=block.first;n<b;++n){if(n>=a){const auto at=static_cast<std::size_t>(static_cast<long double>(n)-input_first);
                    pcm[at]=block.pcm[n-block.first];oscillator[at]=o;observed[at]=1;
                    require(cert::norm(o)<=2,"original carrier magnitude exceeded screen contract");}o*=step;}
            },false);
            tile::PreparedInput<T> input(plan,{pcm,oscillator,observed,v},available,stop,execution_mode);cpu_mix.finish();
            rx_cpu::Scope cpu_transform(rx_cpu::Stage::template_build);
            attribution::Span component_fragments(component,attribution::Bucket::prepared_fragments);
            tile::PreparedFragment<T> a(plan,{std::span<const std::array<Complex,2>>(rows).first(count),
                std::span<const std::uint8_t>(mask).first(count),v},available,stop,execution_mode);
            tile::PreparedFragment<T> b(plan,{std::span<const std::array<Complex,2>>(rows_b).first(count),
                std::span<const std::uint8_t>(mask).first(count),v},available,stop,execution_mode);
            tile::PreparedFragment<T> d(plan,{std::span<const std::array<Complex,2>>(rows_d).first(count),
                std::span<const std::uint8_t>(mask).first(count),v},available,stop,execution_mode);
            execution.template_transforms+=a.work.template_transforms+b.work.template_transforms+d.work.template_transforms;
            component_fragments.finish();cpu_transform.finish();rx_cpu::Scope cpu_product(rx_cpu::Stage::gram);
            check_actual(input.working_bytes()+a.working_bytes()+b.working_bytes()+d.working_bytes());
            accumulator.add(input,a,stop);second.add(input,b,stop);difference.add(input,d,stop);
            check_actual(input.working_bytes()+a.working_bytes()+b.working_bytes()+d.working_bytes());
            execution.input_transforms+=input.work.input_transforms;
            execution.peak_prepared_bytes=std::max(execution.peak_prepared_bytes,
                execution.numeric_bytes()+execution.scratch_reservation);
            v+=count;
        }
        rx_cpu::Scope cpu_certificate(rx_cpu::Stage::gram);
        std::array<origin_gram::Override,3> gram_tokens;
        std::array<origin_gram::Binding,3> gram_bindings;
        if(gram_prefix) {
            gram_prefix->finish(until,stop);
            for(std::size_t j=0;j<size;++j) {
                cancelled(stop);
                const auto physical_origin=static_cast<std::int64_t>(starts[j])-static_cast<std::int64_t>(shift);
                // Prefix::query independently proves coverage of EVERY shifted
                // selected run in actual captured fences; final count is also
                // checked against the actual input observations by finish.
                const auto result=gram_prefix->query(gram_crops[j].first,gram_crops[j].second,physical_origin,*gram_reference,true);
                for(std::size_t variant=0;variant<3;++variant) {
                    for(const auto& bit:result[variant])if(!bit.supported)
                        return lazy_screen_gate("origin_gram_selected_capture",__LINE__);
                    gram_outputs[variant][j]=result[variant];
                }
            }
            for(std::size_t variant=0;variant<3;++variant) {
                gram_bindings[variant]={gram_identity,gram_reference->generation(),gram_reference->revision(),variant};
                gram_tokens[variant]={gram_outputs[variant],starts,0,gram_identity,
                    gram_reference->generation(),gram_reference->revision(),variant};
            }
        }
        if(gram_prefix && (!accumulator.accepts_origin_gram(gram_tokens[0],gram_bindings[0]) ||
            !second.accepts_origin_gram(gram_tokens[1],gram_bindings[1]) ||
            !difference.accepts_origin_gram(gram_tokens[2],gram_bindings[2])))
            return lazy_screen_gate("origin_gram_actual_observation_count",__LINE__);
        const auto values=accumulator.finish(stop,nullptr,gram_prefix?&gram_tokens[0]:nullptr,gram_prefix?&gram_bindings[0]:nullptr);
        const auto values_b=second.finish(stop,nullptr,gram_prefix?&gram_tokens[1]:nullptr,gram_prefix?&gram_bindings[1]:nullptr);
        const auto values_d=difference.finish(stop,nullptr,gram_prefix?&gram_tokens[2]:nullptr,gram_prefix?&gram_bindings[2]:nullptr);
        execution.inverse_transforms+=accumulator.work().inverse_transforms+second.work().inverse_transforms+difference.work().inverse_transforms;
        check_actual();
        if constexpr(std::is_same_v<T,float>)++execution.fp32_ranges;else ++execution.fp64_ranges;
        ++execution.proxy_ranges;range.certificate_cohort=certdiag::cohort<T>(true);
        range.screen.resize(size);range.reference.resize(size);
        for(std::size_t j=0;j<size;++j) {
            if((j&63U)==0)cancelled(stop);
            if(!basis::combine(values[j],values_b[j],values_d[j],weights[j],subtraction,2,range.screen[j]))
                return lazy_screen_gate("fractional_basis_contraction",__LINE__);
        }cancelled(stop);
        range.numeric_origin_stride=1;
        // Original/central boundaries move <.512 physical sample after the
        // producer reserve; two observations per endpoint enclose strict ceil.
        range.screen_first=range.first;range.mask_difference=lazy_screen_mask_difference(groups);range.maximum=maximum;
        range.proxy_residual_square=residual_square;range.proxy_residual_l1=residual_l1;range.proxy_residual_gram=residual_gram;
        for(std::size_t j=0;j<size;++j){
            if((j&63U)==0)cancelled(stop);
            if(range.mask_difference>std::numeric_limits<std::uint64_t>::max()-range.screen[j].fits[0].count)
                return lazy_screen_gate("fractional_basis",__LINE__);
            const auto n=range.screen[j].fits[0].count+range.mask_difference;
            if(n>(std::numeric_limits<std::size_t>::max()-128*block_samples-128)/128)return lazy_screen_gate("fractional_basis",__LINE__);
            const auto rho=cert::add(cert::gamma(128*static_cast<std::size_t>(n)+128*block_samples+128,std::numeric_limits<double>::epsilon()/2),
                cert::mul(4*block_samples,cert::gamma(8*block_samples+8,std::numeric_limits<double>::epsilon()/2)));
            const auto dot=cert::mul(8*rho*qmax,cert::root(cert::mul(static_cast<double>(n),union_energy)));
            const auto gram=cert::mul(32*rho*n,qmax*qmax);
            const auto conditioned=range.reference[j].conditioned_proxy_dot_upper;
            range.reference[j]={dot,gram,cert::mul(4*rho,union_energy),n>2?1e-8/(.5*static_cast<double>(n-2)):0.,true,conditioned};
        }
        if(lazy_state()->certificate_profile.enabled){execution.certificate_pending.observe(range.certificate_cohort,
            static_cast<double>(displacement),residual_l1,residual_gram,residual_square,accumulator.certificate_profile());
            execution.certificate_preparation.merge(execution.certificate_pending);}
        // Last publication: exceptions/cancellation cannot expose a partial proof.
        cancelled(stop);range.exact_observation_mask=false;
        audio::trace::record("scheduled_fractional_basis",[&](auto& f){f.add("epoch",config.stream_epoch);
            f.add("lanes",size);f.add("span",span);f.add("chord_rows",chord_rows);f.add("chord_evaluations",chord_evaluations);
            f.add("coarse_rows",coarse_rows);f.add("origin_gram",use_origin_gram);
            f.add("origin_gram_bulk",use_origin_gram&&gram_build==origin_gram::Build::native_compensated);
            f.add("origin_gram_native_rows",gram_prefix?gram_prefix->native_rows():0);
            f.add("origin_gram_native_batches",gram_prefix?gram_prefix->native_batches():0);
            f.add("origin_gram_interval_rows",gram_prefix?gram_prefix->interval_rows():0);
            f.add("origin_gram_null_batches",gram_prefix?gram_prefix->native_null_batches():0);
            f.add("origin_gram_bytes",actual_gram_bytes());f.add("residual_square",residual_square);f.add("residual_l1",residual_l1);
            f.add("exact_zero_guard_rows",zero_guard_rows);f.add("zero_guard_radius_ticks",zero_guard_radius);
            f.add("endpoint_proven_rows",chord_rows);f.add("endpoint_fallback_rows",endpoint_fallback_rows);
            f.add("gain_chord_rows",gain_chord_rows);f.add("fhss_flat_rows",fhss_flat_rows);f.add("fhss_unproved_rows",fhss_unproved_rows);gain_totals.fields(f);
            f.add("derivative_chord",use_derivative_chord);f.add("periodic_cell_qualified",periodic_cell_qualified);
            f.add("derivative_table_bytes",derivative_table?derivative_table->working_bytes():0);
            f.add("derivative_table_setup_bytes",derivative_setup_bytes);
            f.add("derivative_window_segments",derivative_table?derivative_table->window_segments():0);
            f.add("derivative_aggregate_requested",use_derivative_aggregate);
            f.add("derivative_aggregate_window_segments",derivative_table?derivative_table->aggregate_window_segments():0);
            f.add("derivative_aggregate_lookup_rows",derivative_aggregate_rows);
            f.add("derivative_aggregate_valid_chord_rows",derivative_aggregate_valid_rows);
            f.add("derivative_aggregate_reference_fallback_rows",derivative_aggregate_fallback_rows);
            f.add("mask_bound",range.mask_difference);f.add("scope","opt-in A/B/D whole FFT; original chip guards/refinement unchanged");});
        component_complete=true;return true;
    }

// Included inside PatternCorrelator::Impl, after lazy_plan. Depends on the
// released parallel Execution accessor and the metadata-preserving visitors.
// The old lazy_screen_range and original cold/refinement path remain available.
template<class T> bool lazy_screen_fractional_family(std::span<LazyState::DueRange* const> ranges,std::stop_token stop) {
    namespace tile=detail::scheduled_acquisition_tile;
    namespace origin_gram=detail::scheduled_origin_gram;
    const auto* gram_policy=std::getenv("DATAPUMP_SCHED_ORIGIN_GRAM");
    const bool use_origin_gram=gram_policy && std::string_view(gram_policy)=="1";
    const auto execution_mode=use_origin_gram?origin_gram::Mode::external_gram:origin_gram::Mode::complete;
    const auto gram_build=origin_gram::requested_build();
    namespace cert=detail::scheduled_acquisition_certificate;
    namespace fusion=detail::scheduled_frequency_moments;
    namespace recurrence=detail::scheduled_frequency_recurrence;
    namespace runtime=detail::scheduled_lazy_family_runtime;
    namespace basis=detail::scheduled_fractional_basis;
    namespace chord=detail::scheduled_pulse_chord;
    namespace gch=detail::scheduled_gain_chord;
    namespace attribution=detail::scheduled_template_cpu_attribution;
    attribution::Recorder component_cpu(attribution::requested());
    auto* component=component_cpu.enabled()?&component_cpu:nullptr;bool component_complete=false;
    attribution::OnExit component_report{[&] {
        if(component_cpu.enabled())audio::trace::record("scheduled_template_component_cpu",[&](auto& f) {
            f.add("kind","family");f.add("epoch",config.stream_epoch);f.add("complete",component_complete);component_cpu.fields(f);
        });
    }};
    namespace derivative=detail::scheduled_pulse_derivative_chord;
    const auto* aggregate_policy=std::getenv("DATAPUMP_SCHED_DERIVATIVE_AGGREGATE");
    const bool use_derivative_aggregate=aggregate_policy && std::string_view(aggregate_policy)=="1";
    const auto* derivative_policy=std::getenv("DATAPUMP_SCHED_DERIVATIVE_CHORD");
    const bool use_derivative_chord=use_derivative_aggregate || (derivative_policy && std::string_view(derivative_policy)=="1");
    const bool periodic_cell_qualified=gch::periodic_cell_math_qualified();
    const auto derivative_setup_bytes=use_derivative_chord?derivative::DerivativeTable::reservation():0;
    if(!lazy_fractional_basis_requested() || config.search_arithmetic==SearchArithmetic::fp64)return false;
    constexpr long double half_width=.5L;
    rx_cpu::Scope cpu_screen(rx_cpu::Stage::search_residual);
    auto& execution=lazy_execution();auto& pattern=execution.code;auto& cursor=execution.cursor;
    if(ranges.empty()){component_complete=true;return true;}
    for(auto* range:ranges)if(range){range->exact_observation_mask=false;range->independent_whole_template=false;}
    if(lazy_state()->certificate_profile.enabled)execution.certificate_pending={};
    struct ResetOnFailure {
        std::span<LazyState::DueRange* const> ranges;bool complete=false;
        ~ResetOnFailure(){if(!complete)for(auto* range:ranges)if(range){tile::wipe(range->screen);range->screen.clear();range->reference.clear();range->colored.clear();range->exact_observation_mask=false;range->independent_whole_template=false;}}
    } reset{ranges};
    if(!lazy_reference_environment() || !shaped || !lazy_fhss_screen_supported() ||
       scheduled_map()->source_samples_per_group()<4)return lazy_screen_gate("fractional_frequency_family",__LINE__);
    struct Geometry {
        LazyState::DueRange* range=nullptr;LazyState::Catalog::Lane reference;
        long double displacement=0,aperture=0;double residual_square=0,residual_l1=0,residual_gram=0;

    };
    std::vector<Geometry> geometry;std::vector<std::size_t> starts;std::vector<double> weights;
    const auto old_scratch=execution.scratch_reservation;
    struct RestoreScratch {LazyState::Execution& execution;std::size_t previous;
        ~RestoreScratch(){execution.scratch_reservation=previous;}} restore{execution,old_scratch};
    std::unique_ptr<derivative::DerivativeTable> derivative_table;
    const auto check=[&](std::size_t live) {
        execution.scratch_reservation=tile::add_size(old_scratch,live);
        require(tile::add_size(execution.numeric_bytes(),execution.scratch_reservation)<=execution.available_bytes,
            "implicit frequency family exceeds worker workspace");
        execution.peak_prepared_bytes=std::max(execution.peak_prepared_bytes,
            tile::add_size(execution.numeric_bytes(),execution.scratch_reservation));
    };
    check(tile::add_size(tile::helper_reservation,tile::add_size(tile::multiply_size(ranges.size(),sizeof(Geometry)),
        tile::multiply_size(tile::maximum_start_span,sizeof(std::size_t)+sizeof(double)))));
    if(!ranges.front() || ranges.front()->end<=ranges.front()->first ||
       ranges.front()->end-ranges.front()->first>tile::maximum_start_span)return lazy_screen_gate("fractional_frequency_family",__LINE__);
    geometry.reserve(ranges.size());starts.reserve(ranges.front()->end-ranges.front()->first);weights.reserve(ranges.front()->end-ranges.front()->first);
    for(auto* range:ranges) {
        cancelled(stop);
        if(!range || range->end<=range->first || range->end-range->first>tile::maximum_start_span)return lazy_screen_gate("fractional_frequency_family",__LINE__);
        range->exact_observation_mask=false;
        range->colored.clear();range->colored_sample_pairs=0;range->colored_selected=0;range->colored_mode=0;range->colored_gate=0;
        range->colored_residual_square=range->colored_residual_l1=range->colored_residual_gram=0;
        if(range->index==std::numeric_limits<std::uint64_t>::max())return lazy_screen_gate("fractional_frequency_family",__LINE__);
        Geometry item;item.range=range;item.reference=lazy_family_lane(range->family,range->first);
        // Same immutable reference policy as single-range/class execution.
        // The compatibility checks below retain actual rate, source anchor,
        // phase, origin, index and original start-grid identity.
        const auto& reference=item.reference;
        const auto source_first=static_cast<long double>(range->index)*pattern.symbol_samples();
        const auto first=lazy_state()->catalog->boundary(reference,source_first,false,cursor);
        const auto last=lazy_state()->catalog->boundary(reference,source_first+pattern.symbol_samples(),false,cursor);
        const auto aperture=std::max(std::abs(first-reference.origin),std::abs(last-reference.origin))+tile::maximum_start_span+2;
        item.aperture=aperture;
        std::size_t position=0,previous=0;
        for(auto i=range->first;i<range->end;++i,++position) {
            const auto lane=lazy_family_lane(range->family,i);const auto integer=std::round(lane.origin-reference.origin);
            if(integer<0 || integer>=tile::maximum_start_span || (position && integer<=previous))return lazy_screen_gate("fractional_frequency_family",__LINE__);
            const auto start=static_cast<std::size_t>(integer);previous=start;
            if(geometry.empty())starts.push_back(start);else if(position>=starts.size() || starts[position]!=start)return lazy_screen_gate("fractional_frequency_family",__LINE__);
            if(lane.rate!=reference.rate || lane.phase_lower!=reference.phase_lower ||
               lane.anchor.distance(reference.anchor)!=0)return lazy_screen_gate("fractional_family_identity",__LINE__);
            const auto fraction=(lane.origin-reference.origin)-integer;
            const auto weight=(fraction+half_width)/(2*half_width);
            if(!std::isfinite(weight) || weight<0 || weight>1)return lazy_screen_gate("fractional_family_weight",__LINE__);
            const auto stored=static_cast<double>(weight);
            if(geometry.empty())weights.push_back(stored);
            else if(position>=weights.size() || weights[position]!=stored)return lazy_screen_gate("fractional_family_weight",__LINE__);
        }
        if(position!=starts.size())return lazy_screen_gate("fractional_frequency_family",__LINE__);
        if(item.displacement>0 && config.search_arithmetic==SearchArithmetic::fp64)return lazy_screen_gate("fractional_frequency_family",__LINE__);
        item.displacement+=256*std::numeric_limits<long double>::epsilon()*(1+scheduled->complex_geometry.physical_aperture_samples+
            std::abs(reference.origin)+pattern.symbol_samples());
        item.displacement+=lazy_fhss_coordinate_radius();
        // Coefficient and mask certificates use the same current-index reserve.
        item.displacement+=1024*std::numeric_limits<long double>::epsilon()*
            (1+aperture+std::abs(reference.origin)+source_first+pattern.symbol_samples());
        if(!(reference.rate>=.99L && reference.rate<=1.01L) || item.displacement>.001L)
            return lazy_screen_gate("fractional_family_rounding",__LINE__);
        if(!geometry.empty()) {
            const auto& common=geometry.front();const auto& base=common.reference;
            if(reference.origin!=base.origin || reference.rate!=base.rate || reference.phase_lower!=base.phase_lower ||
               reference.anchor.distance(base.anchor)!=0 || range->index!=common.range->index ||
               range->first!=common.range->first || range->end!=common.range->end)return lazy_screen_gate("fractional_frequency_family",__LINE__);
        }
        // All final target outputs are precharged by parallel preparation. No
        // worker may allocate/grow them, including the failure/direct fallback.
        if(range->screen.capacity()<starts.size() || range->reference.capacity()<starts.size())return lazy_screen_gate("fractional_frequency_family",__LINE__);
        geometry.push_back(std::move(item));
    }
    long double maximum_displacement=0;for(const auto& g:geometry)maximum_displacement=std::max(maximum_displacement,g.displacement);
    const auto span=starts.back()+1;const auto shape=detail::scheduled_parallel_preparation::shape(span);
    const auto fragment_samples=shape.maximum_fragment;const auto input_samples=tile::add_size(span,fragment_samples-1);
    const auto& reference=geometry.front().reference;const auto index=geometry.front().range->index;
    const auto& plan=lazy_plan<T>(shape,stop);
    struct Pin {LazyState::Execution& execution;explicit Pin(LazyState::Execution& e):execution(e){require(!e.screen_active,"nested implicit family screen");e.screen_active=true;}
        ~Pin(){execution.screen_active=false;}} pin{execution};
    const auto shift=static_cast<std::uint64_t>(std::ceil(std::max(0.L,-reference.origin))+fragment_samples);
    const auto phase=SchedHopCoordinate{scheduled->epoch_sample,0}.add_samples(reference.phase_lower);
    const auto begin=lazy_state()->catalog->boundary(reference,static_cast<long double>(index)*pattern.symbol_samples(),false,cursor);
    const auto central_end=lazy_state()->catalog->boundary(reference,static_cast<long double>(index+1)*pattern.symbol_samples(),false,cursor);
    const auto physical_pad=half_width+maximum_displacement/reference.rate;
    const auto required_first=std::max(0.L,std::floor(begin-physical_pad)-1);
    // Retired positive capture is UNKNOWN, never zero/covered by retained PCM
    // maxima. The complete wider ORIGINAL window must still be in history.
    if(static_cast<long double>(lazy_history_first())>required_first)return lazy_screen_gate("fractional_family_history",__LINE__);
    // Fixed PHYSICAL input tiles let concurrent phase families share their
    // input transforms. Only conservative template padding is added: every
    // coordinate still uses the original calendar/mask and physical carrier.
    const auto aligned=[&](std::uint64_t point) {
        if(!lazy_state()->shared_inputs)return point;
        const auto physical=static_cast<long double>(point)-shift;
        return static_cast<std::uint64_t>(std::max(0.L,
            std::floor(physical/fragment_samples)*fragment_samples+shift));
    };
    const auto first_v=aligned(static_cast<std::uint64_t>(std::max(0.L,std::floor(begin-physical_pad+shift)-1)));
    const auto until=static_cast<std::uint64_t>(std::max(0.L,std::ceil(central_end+physical_pad+shift)+2));
    namespace zero_guard=detail::scheduled_zero_guard;
    const auto zero_magnitude=2*(1.L+std::abs(reference.origin)+lazy_state()->capture_end+until+shift+
        span+fragment_samples+pattern.symbol_samples()+static_cast<long double>(index)*pattern.symbol_samples()+reference.phase_lower);
    const auto zero_guard_radius=zero_guard::radius(half_width*reference.rate,maximum_displacement,zero_magnitude,reference.rate);
    std::vector<std::uint64_t> gram_checkpoints;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> gram_crops;
    if(use_origin_gram) {
        if(until>static_cast<std::uint64_t>(origin_gram::Powers::maximum_exponent)||
           shift>static_cast<std::uint64_t>(origin_gram::Powers::maximum_exponent)||
           lazy_state()->capture_end>static_cast<std::uint64_t>(origin_gram::Powers::maximum_exponent))
            return lazy_screen_gate("origin_gram_family_extent",__LINE__);
        check(tile::add_size(execution.scratch_reservation-old_scratch,tile::add_size(8192,
            tile::add_size(tile::multiply_size(2*starts.size()+2,sizeof(std::uint64_t)),
                tile::multiply_size(starts.size(),sizeof(std::pair<std::uint64_t,std::uint64_t>))))));
        gram_checkpoints.reserve(2*starts.size()+2);gram_crops.reserve(starts.size());
        gram_checkpoints.push_back(0);gram_checkpoints.push_back(until);
        for(const auto start:starts) {
            const auto lo=static_cast<std::uint64_t>(std::clamp(static_cast<long double>(lazy_history_first())+shift-start,0.L,static_cast<long double>(until)));
            const auto hi=static_cast<std::uint64_t>(std::clamp(static_cast<long double>(lazy_state()->capture_end)+shift-start,0.L,static_cast<long double>(until)));
            gram_crops.emplace_back(lo,hi);gram_checkpoints.push_back(lo);gram_checkpoints.push_back(hi);
        }
        std::sort(gram_checkpoints.begin(),gram_checkpoints.end());
        gram_checkpoints.erase(std::unique(gram_checkpoints.begin(),gram_checkpoints.end()),gram_checkpoints.end());
    }
    // These capabilities are private to this exact source family and target
    // frequency. No Gram/reference/prepared object enters an ordinary cache.
    struct GramTarget {
        std::unique_ptr<origin_gram::Powers> powers;
        std::unique_ptr<origin_gram::RawReference> reference;
        std::unique_ptr<origin_gram::Prefix<3>> prefix;
        std::array<std::vector<std::array<origin_gram::Gram,2>>,3> outputs;
        std::uint64_t identity()const{return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(this));}
        std::size_t working_bytes()const {
            auto bytes=sizeof(*this)+256;
            if(powers)bytes=tile::add_size(bytes,sizeof(*powers));
            if(reference)bytes=tile::add_size(bytes,reference->working_bytes());
            if(prefix)bytes=tile::add_size(bytes,prefix->working_bytes());
            for(const auto& output:outputs)bytes=tile::add_size(bytes,tile::multiply_size(output.capacity(),sizeof(output[0])));
            return bytes;
        }
        ~GramTarget(){for(auto& output:outputs)tile::wipe(output);}
    };
    const auto one_gram_bound=use_origin_gram?tile::add_size(sizeof(GramTarget)+256,
        tile::add_size(sizeof(origin_gram::Powers)+sizeof(origin_gram::RawReference),
            tile::add_size(origin_gram::Prefix<3>::reservation(gram_checkpoints.size()),
                tile::multiply_size(starts.size(),3*sizeof(std::array<origin_gram::Gram,2>))))):0;
    const auto gram_bound=[&](std::size_t count) {return use_origin_gram?tile::add_size(16384,
        tile::multiply_size(count,tile::add_size(one_gram_bound,sizeof(std::unique_ptr<GramTarget>)))):0;};
    const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(config);
    if(!std::isfinite(amplitude) || amplitude<=0)return lazy_screen_gate("fractional_family_amplitude",__LINE__);
    const auto u=std::numeric_limits<double>::epsilon()/2,eta=std::numeric_limits<double>::denorm_min();
    const auto gain_upper=detail::scheduled_cold_colored::smooth_gain_upper();
    // Executed quintic gains can exceed1 by ulps. Also enclose limiter,
    // gain multiply and amplitude division rather than asserting an exact
    // radial cap for their rounded FP64 pipeline. The native sqrt contract
    // is the same explicit qualified producer contract as the chord helper.
    const auto qmax=cert::add(cert::mul(cert::mul(cert::up(pattern_pcm_radius_limit/amplitude),gain_upper),
        cert::add(1,cert::gamma(32,u))),cert::mul(256*eta,cert::add(1,cert::divide(gain_upper,amplitude))));
    if(!std::isfinite(qmax))return lazy_screen_gate("fractional_family_amplitude",__LINE__);
    static const auto pulse_slope=[] {double value=0;for(unsigned i=0;i<2048;++i)value=std::max(value,
        std::abs(pattern_pulse((i+1)/256.)-pattern_pulse(i/256.))*256);return std::nextafter(value*1.000000000001,std::numeric_limits<double>::infinity());}();
    const auto jump=cert::up(4*std::abs(pattern_pulse(8.)));
    lazy_capture_summary();const auto maximum=lazy_state()->summary_maximum,union_energy=lazy_state()->summary_union_energy;
    pattern.set_stream_phase_samples(reference.phase_lower);
    require(index<=std::numeric_limits<std::uint64_t>::max()/pattern.chips_per_symbol(),"implicit family template chip overflow");
    const auto chip_first=index*pattern.chips_per_symbol();attribution::Span component_prepare(component,attribution::Bucket::source_prepare);
    pattern.prepare_symbol(chip_first,stop);component_prepare.finish();
    attribution::Span component_reader_setup(component,attribution::Bucket::source_reader);
    detail::scheduled_source_samples::Reader source_samples(config,pattern,chip_first,reference.rate);component_reader_setup.finish();
    const auto metadata_bytes=[&] {auto bytes=tile::add_size(tile::helper_reservation,tile::add_size(
        tile::multiply_size(geometry.capacity(),sizeof(Geometry)),tile::add_size(tile::multiply_size(starts.capacity(),sizeof(std::size_t)),tile::multiply_size(weights.capacity(),sizeof(double)))));
        bytes=tile::add_size(bytes,tile::add_size(tile::multiply_size(gram_checkpoints.capacity(),sizeof(std::uint64_t)),
            tile::multiply_size(gram_crops.capacity(),sizeof(gram_crops[0]))));
        return tile::add_size(bytes,derivative_table?derivative_table->working_bytes():0);};
    if(use_derivative_chord) {
        // One table per invocation, shared by EVERY bounded frequency group.
        // Setup includes table+queues before allocation, then live bytes only.
        const auto setup=tile::add_size(metadata_bytes(),derivative_setup_bytes);
        const auto used=tile::add_size(execution.numeric_bytes(),old_scratch);
        if(used>execution.available_bytes || setup>execution.available_bytes-used)
            return lazy_screen_gate("fractional_derivative_table_workspace",__LINE__);
        check(setup);rx_cpu::Scope cpu_derivative_setup(rx_cpu::Stage::setup);
        attribution::Span component_table(component,attribution::Bucket::table_setup);
        derivative_table=std::make_unique<derivative::DerivativeTable>(pattern.chip_samples(),derivative_setup_bytes,stop,
            use_derivative_aggregate?derivative::AggregateWidth::source_33_over_32:derivative::AggregateWidth::disabled);
        if(!derivative_table->valid())return lazy_screen_gate("fractional_derivative_table",__LINE__);
        component_table.finish();
        check(metadata_bytes());
    }
    using Piece=typename LazyState::template TemplatePiece<T>;
    const auto prepared_bound=tile::add_size(sizeof(tile::PreparedFragment<T>)+tile::helper_reservation,
        tile::add_size(tile::multiply_size((use_origin_gram?2:6)*plan.size,sizeof(std::complex<T>)),tile::multiply_size((fragment_samples+1)/2,sizeof(tile::Run))));
    const auto single_piece_bound=tile::add_size(sizeof(Piece),tile::add_size(tile::multiply_size(fragment_samples,
        sizeof(std::array<Complex,2>)+sizeof(long double)+sizeof(double)+sizeof(std::uint64_t)+3),
        tile::add_size(sizeof(tile::PreparedFragment<T>)+tile::helper_reservation,
            tile::add_size(tile::multiply_size((use_origin_gram?2:6)*plan.size,sizeof(std::complex<T>)),tile::multiply_size((fragment_samples+1)/2,sizeof(tile::Run))))));
    const auto piece_bound=tile::multiply_size(3,single_piece_bound);
    // Table is already in metadata_bytes/live; row scratch charges it once.
    const auto chord_bound=use_derivative_chord?8192:chord::reservation(2048);
    const auto chord_workspace=use_derivative_chord?
        tile::add_size(derivative_table->working_bytes(),chord_bound):chord_bound;
    const auto run_bound=tile::add_size((input_samples+511)/512,4);
    const auto array_bound=tile::add_size(tile::multiply_size(input_samples,sizeof(float)+2*sizeof(Complex)+1),
        tile::multiply_size(run_bound,sizeof(runtime::OriginalRun)+sizeof(recurrence::Segment)));
    runtime::Work cost;cost.maximum_fragment=fragment_samples;
    const bool native_accumulation=std::is_same_v<T,float> && config.search_arithmetic!=SearchArithmetic::fp64 &&
        detail::scheduled_parallel_preparation::native_float_accumulation_requested();
    std::size_t next=0;
    while(next<geometry.size()) {
        cancelled(stop);
        // Diagnostic family-width override changes execution grouping only.
        // Every target remains, and the live workspace/Taylor gates below
        // can reduce the group or retain the original per-frequency path.
        std::size_t width=4;
        if(const auto* text=std::getenv("DATAPUMP_SCHED_FREQUENCY_FAMILY_WIDTH")) {
            char* tail=nullptr;const auto n=std::strtoull(text,&tail,10);
            require(tail!=text && !*tail && n && n<=256,"invalid scheduled frequency family width");width=n;
        }
        std::size_t count=std::min(width,geometry.size()-next);
        const auto choose_coarse=[&]() {
            auto selected=next;double best=std::numeric_limits<double>::infinity();
            for(auto a=next;a<next+count;++a) {
                double radius=0;const auto f=banks[geometry[a].reference.frequency].frequency;
                for(auto b=next;b<next+count;++b)radius=std::max(radius,std::abs(f-banks[geometry[b].reference.frequency].frequency));
                if(radius<best){best=radius;selected=a;}
            }
            return selected;
        };
        std::size_t coarse=choose_coarse();bool use_moments=count>=4 && config.search_arithmetic!=SearchArithmetic::fp64;
        if(use_moments)for(std::size_t t=next;t<next+count;++t)
            if(fusion::taylor_tail(input_samples,config.sample_rate,banks[geometry[t].reference.frequency].frequency-
                banks[geometry[coarse].reference.frequency].frequency,3).angle>.125)use_moments=false;
        auto chain_bound=[&] {return use_moments?tile::add_size(runtime::moment_bytes(plan,3),runtime::resolved_bytes(plan,execution_mode)):
            runtime::input_bytes(plan,execution_mode);};
        const auto available=[&] {const auto used=tile::add_size(execution.numeric_bytes(),old_scratch);
            return used<execution.available_bytes?execution.available_bytes-used:0;};
        const auto base_bound=tile::add_size(metadata_bytes(),tile::add_size(array_bound,tile::add_size(piece_bound,chord_bound)));
        while(tile::add_size(tile::add_size(tile::add_size(base_bound,gram_bound(count)),chain_bound()),
            tile::multiply_size(tile::multiply_size(3,count),runtime::accumulator_bytes(plan,starts.size(),native_accumulation,execution_mode)))>available()) {
            if(use_moments){use_moments=false;continue;}
            if(count==1)return lazy_screen_gate("fractional_frequency_family",__LINE__);--count;coarse=choose_coarse();
        }
        const bool gram_timing=use_origin_gram && bool(audio::trace::active.load());
        bool gram_cpu_valid=gram_timing;std::uint64_t gram_constructor_cpu_ns=0,gram_append_cpu_ns=0,gram_query_cpu_ns=0;
        const auto gram_tick=[&] {return gram_timing?rx_cpu::native_thread_cpu(nullptr):rx_cpu::Tick{};};
        const auto gram_charge=[&](rx_cpu::Tick before,std::uint64_t& destination) {
            if(!gram_timing)return;const auto after=rx_cpu::native_thread_cpu(nullptr);
            if(!before.valid||!after.valid||after.ns<before.ns||after.ns-before.ns>std::numeric_limits<std::uint64_t>::max()-destination)
                gram_cpu_valid=false;
            else destination+=after.ns-before.ns;
        };
        std::vector<std::unique_ptr<tile::Accumulator<T>>> accumulators;accumulators.reserve(tile::multiply_size(3,count));
        std::vector<std::unique_ptr<GramTarget>> gram_targets;
        if(use_origin_gram)gram_targets.reserve(count);
        std::vector<float> pcm(input_samples);std::vector<Complex> oscillator(input_samples),target_oscillator;
        std::vector<std::uint8_t> observed(input_samples);
        std::vector<runtime::OriginalRun> runs;runs.reserve(run_bound);
        std::vector<recurrence::Segment> segments;segments.reserve(run_bound);
        std::array<std::unique_ptr<Piece>,3> pieces;
        basis::DifferenceCertificate subtraction;gch::Totals gain_totals;
        std::uint64_t gain_chord_rows=0,fhss_flat_rows=0,fhss_unproved_rows=0;
        std::uint64_t chord_rows=0,chord_evaluations=0,coarse_rows=0,endpoint_fallback_rows=0,zero_guard_rows=0;
        std::uint64_t derivative_aggregate_rows=0,derivative_aggregate_fallback_rows=0,derivative_aggregate_valid_rows=0;
        std::unique_ptr<fusion::MomentInput<T>> moment;
        std::shared_ptr<const detail::scheduled_shared_input::Pool::Entry> shared_moment;
        const fusion::MomentInput<T>* moment_view=nullptr;
        std::unique_ptr<tile::PreparedInput<T>> original_input;
        std::unique_ptr<fusion::ResolvedInput<T>> resolved;
        const auto live=[&] {
            auto bytes=tile::add_size(metadata_bytes(),use_origin_gram?16384:0);
            bytes=tile::add_size(bytes,tile::multiply_size(accumulators.capacity(),sizeof(accumulators[0])));
            bytes=tile::add_size(bytes,tile::multiply_size(pcm.capacity(),sizeof(float)));
            bytes=tile::add_size(bytes,tile::multiply_size(tile::add_size(oscillator.capacity(),target_oscillator.capacity()),sizeof(Complex)));
            bytes=tile::add_size(bytes,observed.capacity());bytes=tile::add_size(bytes,tile::multiply_size(runs.capacity(),sizeof(runs[0])));
            bytes=tile::add_size(bytes,tile::multiply_size(segments.capacity(),sizeof(segments[0])));
            bytes=tile::add_size(bytes,tile::multiply_size(gram_targets.capacity(),sizeof(gram_targets[0])));
            for(const auto& target:gram_targets)bytes=tile::add_size(bytes,target->working_bytes());
            for(const auto& a:accumulators)bytes=tile::add_size(bytes,a->working_bytes());
            for(const auto& piece:pieces)if(piece)bytes=tile::add_size(bytes,piece->working_bytes());
            if(moment)bytes=tile::add_size(bytes,moment->working_bytes());
            if(original_input)bytes=tile::add_size(bytes,original_input->working_bytes());if(resolved)bytes=tile::add_size(bytes,resolved->working_bytes());
            return bytes;
        };
        const auto remaining=[&] {check(live());const auto used=tile::add_size(execution.numeric_bytes(),execution.scratch_reservation);
            return execution.available_bytes-used;};
        check(live());
        // The numerical whole mix has independent rows and masks. Its chip
        // evidence must come from the unchanged ORIGINAL owned-whole demand
        // path, never a central-template chip fit or conditioned dot prefix.
        for(std::size_t t=next;t<next+count;++t) {
            auto& range=*geometry[t].range;range.reference.resize(starts.size());
            for(auto& e:range.reference)e.conditioned_proxy_dot_upper=cert::infinity;
        }
        for(std::size_t t=0;t<tile::multiply_size(3,count);++t) {
            const auto reserve=tile::add_size(piece_bound,chain_bound());const auto room=remaining();
            if(room<=reserve)return lazy_screen_gate("fractional_frequency_family",__LINE__);
            accumulators.push_back(std::make_unique<tile::Accumulator<T>>(plan,0,starts,room-reserve,stop,lazy_state()->certificate_profile.enabled,native_accumulation,execution_mode));check(live());
        }
        const auto gram_constructor_tick=gram_tick();
        if(use_origin_gram) {
            rx_cpu::Scope cpu_gram_reference(rx_cpu::Stage::mixing);
            const auto captured_first=static_cast<std::uint64_t>(std::max<long double>(lazy_history_first(),
                std::max(0.L,static_cast<long double>(first_v)-shift)));
            const auto captured_end=static_cast<std::uint64_t>(std::max(0.L,std::min<long double>(lazy_state()->capture_end,
                static_cast<long double>(until)-shift+starts.back())));
            for(std::size_t t=next;t<next+count;++t) {
                cancelled(stop);check(tile::add_size(live(),one_gram_bound));
                auto target=std::make_unique<GramTarget>();
                const auto frequency=banks[geometry[t].reference.frequency].frequency;
                const auto step=std::polar(1.,tau*frequency/config.sample_rate);
                target->powers=std::make_unique<origin_gram::Powers>(step);
                target->reference=std::make_unique<origin_gram::RawReference>(*target->powers,target->identity());
                bool supported=true;
                // Metadata, not copied PCM, identifies each actual Original
                // <=512 recurrence fence. Native seeds use the SAME formula
                // as original/target construction below; no ideal trig premise.
                if(captured_first<captured_end)lazy_visit_fences(captured_first,captured_end,[&](const auto& fence) {
                    cancelled(stop);
                    const auto seed=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(fence.first)*tau*
                        frequency/config.sample_rate,static_cast<long double>(tau))));
                    supported&=target->reference->append(fence.first,fence.count,seed);
                });
                if(!supported||!target->reference->valid())return lazy_screen_gate("origin_gram_family_fences",__LINE__);
                target->prefix=std::make_unique<origin_gram::Prefix<3>>(*target->powers,target->identity(),gram_checkpoints,
                    origin_gram::Prefix<3>::reservation(gram_checkpoints.size()),4096,gram_build);
                for(auto& output:target->outputs)output.resize(starts.size());
                gram_targets.push_back(std::move(target));check(live());
            }
        }
        gram_charge(gram_constructor_tick,gram_constructor_cpu_ns);
        runtime::add_count(cost.groups,1);if(use_moments)runtime::add_count(cost.moment_groups,1);
        runtime::add_count(native_accumulation?cost.native_float_accumulators:cost.wide_accumulators,tile::multiply_size(3,count));
        std::uint64_t groups=0,last_group=std::numeric_limits<std::uint64_t>::max(),fragments=0;
        for(std::size_t t=next;t<next+count;++t){geometry[t].residual_square=geometry[t].residual_l1=geometry[t].residual_gram=0;}
        auto v=first_v;
        while(v<until) {
            cancelled(stop);require(++fragments<=tile::maximum_fragments,"implicit family fragment bound exceeded");
            const auto real=static_cast<long double>(v)-shift;const auto tick=phase.add_signed((real-reference.origin)*reference.rate);
            const auto location=execution.map.locate(tick);
            if(!location.active) {
                // Preserve actual rows on BOTH enlarged quiet edges. The
                // source-association reserve is in source ticks, pad in RX
                // samples. No skipped original-only row escapes its residual.
                const auto pad=half_width+maximum_displacement/reference.rate+2;
                const auto left=execution.map.locate(phase.add_signed((real-reference.origin-pad)*reference.rate));
                const auto right=execution.map.locate(phase.add_signed((real-reference.origin+pad)*reference.rate));
                if(!left.active && !right.active && left.frame_index==right.frame_index) {
                    const auto next_tick=execution.map.source_to_physical(location.source,false);
                    const auto next_real=reference.origin+next_tick.distance(phase)/reference.rate;
                    const auto target=aligned(static_cast<std::uint64_t>(std::max(0.L,std::floor(next_real+shift-pad))));
                    if(target>v+1){v=std::min(target,until);continue;}
                }
            }
            const auto length=static_cast<std::size_t>(std::min<std::uint64_t>(fragment_samples,until-v));
            // Ordinary TemplateKey has no basis role or endpoint identity.
            // Keep A/B/D owners branch-local until a separately keyed cache is
            // implemented; never publish them as ordinary central templates.
            check(tile::add_size(live(),tile::add_size(piece_bound,chord_bound)));
            for(auto& piece:pieces) {
                piece=std::make_unique<Piece>();piece->rows.resize(fragment_samples);piece->mask.resize(fragment_samples);check(live());
            }
            auto& central=*pieces[0];central.source.resize(length);central.gain.resize(length);
            central.active.resize(length);central.frame.resize(length);check(live());
            rx_cpu::Scope cpu_template(rx_cpu::Stage::template_build);
            for(std::size_t i=0;i<length;++i) {
                if((i&63U)==0)cancelled(stop);
                attribution::Span component_geometry(component,attribution::Bucket::row_geometry);
                component_cpu.observe(attribution::Counter::rows);
                const auto p=static_cast<long double>(v+i)-shift;
                const auto tick=phase.add_signed((p-reference.origin)*reference.rate);
                const auto l=execution.map.locate(tick);
                const auto source=l.source.distance(reference.anchor)-static_cast<long double>(index)*pattern.symbol_samples();
                const auto gain=lazy_screen_gain(l,tick);
                central.source[i]=source;central.gain[i]=gain;central.active[i]=l.active;central.frame[i]=l.frame_index;
                if(l.active && l.frame_index!=last_group){++groups;last_group=l.frame_index;}
                const auto low_tick=tick.add_signed(-half_width*reference.rate),high_tick=tick.add_signed(half_width*reference.rate);
                const auto low=execution.map.locate(low_tick),high=execution.map.locate(high_tick);
                const auto low_source=low.source.distance(reference.anchor)-static_cast<long double>(index)*pattern.symbol_samples();
                const auto high_source=high.source.distance(reference.anchor)-static_cast<long double>(index)*pattern.symbol_samples();
                const auto low_gain=lazy_screen_gain(low,low_tick),high_gain=lazy_screen_gain(high,high_tick);
                const bool selected=gain>0 && source>=0 && source<pattern.symbol_samples();
                if(selected)component_cpu.observe(attribution::Counter::selected_rows);
                for(auto& piece:pieces)piece->mask[i]=selected?1:0;
                const auto make=[&](const auto& at,long double coordinate,double power) {
                    std::array<Complex,2> q{};
                    if(at.active && power>0 && coordinate>=0 && coordinate<pattern.symbol_samples()) {
                        attribution::Span component_reader(component,attribution::Bucket::fallback_reader);
                        const auto raw=source_samples.value(coordinate,stop);component_reader.finish();
                        for(unsigned bit=0;bit<2;++bit)q[bit]=power*pattern_limit_pcm(amplitude*raw[bit])/amplitude;
                    }return q;
                };
                // A represents origin-h (higher source), B origin+h. D uses
                // the ACTUAL stored double rows, with its own subtraction proof.
                const bool same_group=l.active && low.active && high.active && l.frame_index==low.frame_index && l.frame_index==high.frame_index;
                const auto wide_low=source-half_width*reference.rate-maximum_displacement;
                const auto wide_high=source+half_width*reference.rate+maximum_displacement;
                const bool common_interior=selected && same_group && wide_low>0 && wide_high<pattern.symbol_samples();
                const bool scheduled_flat=common_interior && SchedHopSourceMap::gain(l,tick)==1 &&
                    SchedHopSourceMap::gain(low,low_tick)==1 && SchedHopSourceMap::gain(high,high_tick)==1 &&
                    tick.distance(SchedHopCoordinate{l.flat_begin,0})>half_width*reference.rate+maximum_displacement &&
                    SchedHopCoordinate{l.flat_end,0}.distance(tick)>half_width*reference.rate+maximum_displacement;
                component_geometry.finish();
                bool chord_valid=false,fhss_flat=false;double chord_error=0,chord_gain_upper=0;double lo=0,hi=0;
                if(common_interior) {
                    lo=static_cast<double>(low_source);hi=static_cast<double>(high_source);
                    const chord::Request request{lo,hi,amplitude,1,2,pattern.symbol_samples(),pattern.chip_samples(),
                        2048,chord_workspace,true,true};
                    attribution::Span component_workspace(component,attribution::Bucket::workspace_checks);
                    check(tile::add_size(live(),chord_bound));component_workspace.finish();
                    component_cpu.observe(attribution::Counter::proof_attempts);
                    attribution::Span component_proof(component,attribution::Bucket::proof_inclusive);
                    derivative::ExecutionReport derivative_report{use_derivative_aggregate?derivative::Execution::aggregate:derivative::Execution::reference};
                    const auto proof=gch::certify_row(request,attribution::wrap_original(component,[&](double coordinate,std::stop_token token) {
                        cancelled(token);return pattern.shaped_values(chip_first,coordinate);
                    }),l,scheduled->fhss_shape,tick,half_width*reference.rate,maximum_displacement,lazy_fhss_coordinate_radius(),
                        config.sample_rate,low_gain,high_gain,scheduled_flat,periodic_cell_qualified,stop,derivative_table.get(),use_derivative_chord?&derivative_report:nullptr);
                    component_proof.finish();if(proof.value.valid)component_cpu.observe(attribution::Counter::proof_valid);
                    attribution::Span component_proof_result(component,attribution::Bucket::target_residual);
                    if(derivative_report.aggregate_used)++derivative_aggregate_rows;
                    if(derivative_report.aggregate_fallback)++derivative_aggregate_fallback_rows;
                    if(derivative_report.aggregate_used && proof.value.valid)++derivative_aggregate_valid_rows;
                    runtime::add_count(chord_evaluations,proof.evaluations);fhss_flat=proof.fhss_flat;
                    if(proof.value.valid){pieces[0]->rows[i]=proof.value.right;pieces[1]->rows[i]=proof.value.left;
                        chord_error=std::max(proof.value.residual[0],proof.value.residual[1]);chord_gain_upper=proof.gain_upper;
                        chord_valid=true;++chord_rows;++gain_chord_rows;
                        if(scheduled->fhss_shape.enabled && fhss_flat)++fhss_flat_rows;}
                    else if(scheduled->fhss_shape.enabled && !fhss_flat)++fhss_unproved_rows;
                }
                // Successful proof rows are authoritative A/B endpoints;
                // preserve the unchanged Original fallback when proof fails.
                if(selected && !chord_valid) {
                    attribution::Span component_fallback(component,attribution::Bucket::fallback_endpoints);
                    component_cpu.observe(attribution::Counter::fallback_rows);
                    pieces[0]->rows[i]=make(high,high_source,high_gain);
                    pieces[1]->rows[i]=make(low,low_source,low_gain);++endpoint_fallback_rows;
                }
                attribution::Span component_difference(component,attribution::Bucket::difference_norm);
                pieces[2]->rows[i]=subtraction.subtract(pieces[0]->rows[i],pieces[1]->rows[i]);
                const auto row_norm=std::max({cert::norm(pieces[0]->rows[i][0]),cert::norm(pieces[0]->rows[i][1]),
                    cert::norm(pieces[1]->rows[i][0]),cert::norm(pieces[1]->rows[i][1])});component_difference.finish();
                attribution::Span component_residual(component,attribution::Bucket::target_residual);
                component_cpu.observe(attribution::Counter::target_pairs,count);
                bool any_coarse=false;
                for(std::size_t target=next;target<next+count;++target) {
                    auto& g=geometry[target];const auto displacement=g.displacement;
                    const auto extended_low=source-half_width*reference.rate-displacement;
                    const auto extended_high=source+half_width*reference.rate+displacement;
                    const auto before=execution.map.locate(tick.add_signed(-half_width*reference.rate-displacement));
                    const auto after=execution.map.locate(tick.add_signed(half_width*reference.rate+displacement));
                    const bool exact_zero_guard=gain==0 && static_cast<bool>(zero_guard::prove_current(l,tick,
                    source,pattern.symbol_samples(),zero_guard_radius));
                    if(exact_zero_guard)++zero_guard_rows;
                    const bool quiet=exact_zero_guard || (!l.active && !before.active && !after.active && l.frame_index==before.frame_index && l.frame_index==after.frame_index);
                    const bool outside=extended_high<0 || extended_low>=pattern.symbol_samples();
                    double residual=0,target_upper=0;bool coarse=false;
                    if(!quiet && !outside) {
                        if(chord_valid) {
                            residual=chord_error;
                            const auto coordinate_error=cert::add(cert::up(static_cast<double>(displacement)),
                                cert::add(cert::add(cert::ulp(lo),cert::ulp(hi)),cert::mul(16*std::numeric_limits<double>::epsilon(),
                                    cert::add(1,cert::add(std::abs(lo),std::abs(hi))))));
                            residual=cert::add(residual,cert::mul(chord_gain_upper,cert::mul(coordinate_error,cert::divide(40*pulse_slope,pattern.chip_samples()))));
                            residual=cert::add(residual,cert::mul(8*std::numeric_limits<double>::epsilon(),qmax));
                            const auto radius=half_width*reference.rate+static_cast<long double>(coordinate_error);
                            const auto edge=source/pattern.chip_samples()-.5L;
                            if(std::abs(edge-std::round(edge))*pattern.chip_samples()<=radius)residual=cert::add(residual,cert::mul(chord_gain_upper,jump));
                            const auto tail=pattern.symbol_samples()%pattern.chip_samples();
                            if(tail){const auto center=static_cast<long double>(pattern.symbol_samples()-tail)+.5L*tail;
                                if(std::min(std::abs(source-center-8.L*pattern.chip_samples()),std::abs(source-center+8.L*pattern.chip_samples()))<=radius)
                                    residual=cert::add(residual,cert::mul(chord_gain_upper,jump));}
                        }else {coarse=true;any_coarse=true;}
                    }
                    if(coarse)residual=gch::coarse_residual(
                        cert::down(static_cast<double>(std::max(0.L,extended_low))),
                        cert::up(static_cast<double>(std::min<long double>(pattern.symbol_samples(),extended_high))),
                        pattern.symbol_samples(),pattern.chip_samples(),amplitude,detail::scheduled_cold_colored::smooth_gain_upper(),
                        row_norm,cert::mul(2,qmax),true,&target_upper);
                    const bool interior=selected && same_group && extended_low>0 && extended_high<pattern.symbol_samples();
                    const auto kind=!interior?gch::Kind::window:(scheduled->fhss_shape.enabled?gch::Kind::fhss:
                        (scheduled_flat?gch::Kind::flat:gch::Kind::ramp));
                    gain_totals.observe(kind,residual,row_norm,coarse?target_upper:0,coarse?row_norm:0);
                    g.residual_square=cert::add(g.residual_square,cert::mul(residual,residual));
                    g.residual_l1=cert::add(g.residual_l1,residual);
                    g.residual_gram=cert::add(g.residual_gram,cert::add(cert::mul(2*row_norm,residual),cert::mul(residual,residual)));
                }
                if(any_coarse)++coarse_rows;
            }
            const auto gram_append_tick=gram_tick();
            if(use_origin_gram){attribution::Span component_gram(component,attribution::Bucket::gram_append);for(auto& target:gram_targets)origin_gram::append_three(*target->prefix,v,
                std::span<const std::array<Complex,2>>(pieces[0]->rows).first(length),
                std::span<const std::array<Complex,2>>(pieces[1]->rows).first(length),
                std::span<const std::array<Complex,2>>(pieces[2]->rows).first(length),
                std::span<const std::uint8_t>(pieces[0]->mask).first(length),stop);}
            gram_charge(gram_append_tick,gram_append_cpu_ns);
            cpu_template.finish();
            attribution::Span component_fragments(component,attribution::Bucket::prepared_fragments);
            for(auto& piece:pieces) {
                check(tile::add_size(live(),prepared_bound));
                piece->prepared=std::make_unique<tile::PreparedFragment<T>>(plan,tile::Fragment{
                    std::span<const std::array<Complex,2>>(piece->rows).first(length),
                    std::span<const std::uint8_t>(piece->mask).first(length),v},remaining(),stop,execution_mode);
                runtime::add_count(execution.template_transforms,piece->prepared->work.template_transforms);check(live());
            }
            component_fragments.finish();
            rx_cpu::Scope cpu_mix(rx_cpu::Stage::mixing);
            std::fill(pcm.begin(),pcm.end(),0);std::fill(observed.begin(),observed.end(),0);std::fill(oscillator.begin(),oscillator.end(),Complex{});runs.clear();
            const auto input_first=static_cast<long double>(v)-shift;
            const auto captured_first=static_cast<std::uint64_t>(std::max<long double>(lazy_history_first(),std::max(0.L,input_first)));
            const auto captured_end=static_cast<std::uint64_t>(std::max(0.L,std::min<long double>(lazy_state()->capture_end,input_first+input_samples)));
            const auto coarse_frequency=banks[geometry[coarse].reference.frequency].frequency;
            // Caller pushes may have ANY original fence size <=512. Count the
            // exact metadata before reserving; ceil(N/512) is not an upper
            // bound when the original caller supplied one-sample pushes.
            std::size_t descriptor_count=0,covered=0;
            const auto count_empty_until=[&](std::size_t end) {
                require(end>=covered && end<=input_samples,"implicit family fence metadata overlaps");
                descriptor_count=tile::add_size(descriptor_count,(end-covered+511)/512);covered=end;
            };
            if(captured_first<captured_end)lazy_visit_fences(captured_first,captured_end,[&](const auto& fence) {
                require(fence.count<=recurrence::maximum_reset_segment,"implicit carrier original fence exceeds 512");
                const auto a=static_cast<std::uint64_t>(std::max<long double>(fence.first,std::max(0.L,input_first)));
                const auto b=static_cast<std::uint64_t>(std::min<long double>(tile::add_coordinate(fence.first,fence.count),input_first+input_samples));
                if(a>=b)return;const auto local=static_cast<std::size_t>(static_cast<long double>(a)-input_first);
                count_empty_until(local);descriptor_count=tile::add_size(descriptor_count,1);covered=local+static_cast<std::size_t>(b-a);
            });
            count_empty_until(input_samples);
            require(descriptor_count<=input_samples,"implicit family fence metadata exceeded physical cells");
            const auto reserve_descriptors=[&](auto& values) {
                if(values.capacity()>=descriptor_count)return;
                using Element=typename std::remove_reference_t<decltype(values)>::value_type;
                const auto bytes=tile::multiply_size(descriptor_count,sizeof(Element));check(tile::add_size(live(),bytes));
                // reserve may temporarily retain the old allocation; account
                // the complete replacement as well until the swap releases it.
                std::vector<Element> replacement;replacement.reserve(descriptor_count);
                check(tile::add_size(live(),tile::multiply_size(replacement.capacity(),sizeof(Element))));values.swap(replacement);
            };
            reserve_descriptors(runs);if(use_moments)reserve_descriptors(segments);check(live());
            const auto append_run=[&](runtime::OriginalRun run) {
                require(runs.size()<runs.capacity(),"implicit family fence metadata changed during copy");runs.push_back(run);
            };
            const auto empty_until=[&](std::size_t end) {
                auto first=runs.empty()?0:runs.back().end;
                while(first<end){const auto last=std::min(end,first+recurrence::maximum_reset_segment);append_run({first,last,0,0,{},{},false});first=last;}
            };
            if(captured_first<captured_end)lazy_visit_blocks(captured_first,captured_end,[&](const auto& block) {
                require(block.pcm.size()<=recurrence::maximum_reset_segment,"implicit carrier original fence exceeds 512");
                const auto a=static_cast<std::uint64_t>(std::max<long double>(block.first,std::max(0.L,input_first)));
                const auto b=static_cast<std::uint64_t>(std::min<long double>(block.first+block.pcm.size(),input_first+input_samples));if(a>=b)return;
                const auto local=static_cast<std::size_t>(static_cast<long double>(a)-input_first);empty_until(local);
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*coarse_frequency/config.sample_rate,static_cast<long double>(tau))));
                const auto step=std::polar(1.,tau*coarse_frequency/config.sample_rate);
                for(auto n=block.first;n<a;++n)o*=step;
                append_run({local,local+static_cast<std::size_t>(b-a),block.first,static_cast<std::size_t>(a-block.first),o,step,true});
                for(auto n=a;n<b;++n){const auto at=static_cast<std::size_t>(static_cast<long double>(n)-input_first);
                    pcm[at]=block.pcm[n-block.first];oscillator[at]=o;observed[at]=1;
                    require(cert::norm(o)<=2,"original carrier magnitude exceeded family contract");o*=step;}
            },false); // one original PCM copy, no redundant energy-prefix rebuild
            empty_until(input_samples);require(runs.size()==descriptor_count,"implicit family fence identity changed during copy");check(live());
            const auto first_observed=std::find_if(runs.begin(),runs.end(),[](const auto& run){return run.observed;});
            const bool any_observed=first_observed!=runs.end();
            const tile::Input coarse_input{pcm,oscillator,observed,v};
            if(use_moments) {
                bool built=true;
                if constexpr(std::is_same_v<T,float>)if(lazy_state()->shared_inputs) {
                    const auto negative=v<shift;
                    const detail::scheduled_shared_input::Pool::Key input_key{
                        negative?shift-v:v-shift,v,std::bit_cast<std::uint64_t>(coarse_frequency),
                        lazy_state()->capture_end,lazy_history_first(),config.sample_rate,
                        plan.shape.start_span,plan.shape.maximum_fragment,negative};
                    auto borrowed=lazy_state()->shared_inputs->borrow(input_key,plan,coarse_input,stop);
                    if(borrowed.entry){built=borrowed.built;shared_moment=std::move(borrowed.entry);
                        moment_view=shared_moment->moment.get();}
                }
                if(!moment_view){moment=std::make_unique<fusion::MomentInput<T>>(plan,coarse_input,3,remaining(),stop);moment_view=moment.get();}
                check(live());
                if(built){runtime::add_count(execution.input_transforms,moment_view->work.shared_forward_transforms);
                    runtime::add_count(cost.moment_transforms,moment_view->work.shared_forward_transforms);}
            }
            cpu_mix.finish();
            for(std::size_t t=next;t<next+count;++t) {
                cancelled(stop);const auto frequency=banks[geometry[t].reference.frequency].frequency;
                const auto target_step=std::polar(1.,tau*frequency/config.sample_rate);
                const auto seed=[&](const runtime::OriginalRun& run) {
                    auto value=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(run.block_first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                    for(std::size_t i=0;i<run.block_offset;++i)value*=target_step;return value;
                };
                if(moment_view) {
                    segments.clear();for(const auto& run:runs) {
                        require(segments.size()<segments.capacity(),"implicit family descriptor capacity changed");
                        const auto target_seed=run.observed?seed(run):Complex{};
                        // Certify the same <=2 original-carrier contract for
                        // EVERY target recurrence, not only the coarse input.
                        // This uses its actual seed/step and IEEE operation
                        // bound, without assuming an accuracy bound for polar.
                        if(run.observed && recurrence::oscillator_magnitudes(target_seed,target_step,
                            run.end-run.begin).maximum>2)return lazy_screen_gate("fractional_frequency_family",__LINE__);
                        segments.push_back({run.begin,run.end,run.coarse_seed,target_seed,run.coarse_step,
                            run.observed?target_step:Complex{},run.block_first,run.block_offset});
                    }check(live());
                    const auto delta=frequency-coarse_frequency;
                    auto coefficients=any_observed?recurrence::anchored_coefficients(*moment_view,delta,config.sample_rate,0,
                        first_observed->begin,first_observed->coarse_seed,seed(*first_observed)):
                        fusion::coefficients(*moment_view,delta,config.sample_rate,0);
                    const auto& original=moment_view->base.original;
                    const tile::Input target{original.pcm,t==coarse?original.oscillator:std::span<const Complex>{},original.observed,v};
                    const auto proof=recurrence::certify(*moment_view,coefficients,target,segments,{true},stop);
                    if(!proof.remainder.valid || !runtime::capture_origin(proof.capture_origin,v,shift,any_observed))return lazy_screen_gate("fractional_frequency_family",__LINE__);
                    resolved=std::make_unique<fusion::ResolvedInput<T>>(*moment_view,coefficients,target,proof.remainder,remaining(),stop,execution_mode);check(live());
                    for(unsigned role=0;role<3;++role)accumulators[3*(t-next)+role]->add(*resolved,*pieces[role]->prepared,stop);
                    runtime::add_count(cost.synthesized_products,resolved->work.synthesized_products);
                    runtime::add_count(cost.recurrence_segments,proof.work.segments);resolved.reset();check(live());
                }else {
                    if(t!=coarse) {
                        if(target_oscillator.capacity()<input_samples)
                            check(tile::add_size(live(),tile::multiply_size(input_samples,sizeof(Complex))));
                        target_oscillator.assign(input_samples,Complex{});check(live());
                        for(const auto& run:runs)if(run.observed){auto o=seed(run);
                            if(recurrence::oscillator_magnitudes(o,target_step,run.end-run.begin).maximum>2)return lazy_screen_gate("fractional_frequency_family",__LINE__);
                            for(auto i=run.begin;i<run.end;++i){target_oscillator[i]=o;o*=target_step;}}
                    }
                    const tile::Input target{pcm,t==coarse?std::span<const Complex>(oscillator):std::span<const Complex>(target_oscillator),observed,v};
                    original_input=std::make_unique<tile::PreparedInput<T>>(plan,target,remaining(),stop,execution_mode);check(live());
                    for(unsigned role=0;role<3;++role)accumulators[3*(t-next)+role]->add(*original_input,*pieces[role]->prepared,stop);
                    runtime::add_count(execution.input_transforms,original_input->work.input_transforms);original_input.reset();check(live());
                }
            }
            moment_view=nullptr;moment.reset();shared_moment.reset();check(live());runtime::add_count(cost.fragments,1);
            for(auto& piece:pieces)piece.reset();check(live());v=tile::add_coordinate(v,length);
        }
        // Keep the exporting accumulator/starts/Plan alive through every exact
        // trace consumer. Each consumer retains its own independent radii.
        std::array<tile::TraceReuse<T>,3> trace;
        const auto gram_finish_tick=gram_tick();
        if(use_origin_gram)for(auto& target:gram_targets)target->prefix->finish(until,stop);
        gram_charge(gram_finish_tick,gram_query_cpu_ns);
        for(std::size_t t=next;t<next+count;++t) {
            cancelled(stop);std::array<std::span<const tile::Value>,3> values;
            std::array<origin_gram::Override,3> gram_tokens;
            std::array<origin_gram::Binding,3> gram_bindings;
            const auto gram_query_tick=gram_tick();
            if(use_origin_gram) {
                auto& target=*gram_targets[t-next];
                for(std::size_t j=0;j<starts.size();++j) {
                    cancelled(stop);
                    const auto result=target.prefix->query(gram_crops[j].first,gram_crops[j].second,
                        static_cast<std::int64_t>(starts[j])-static_cast<std::int64_t>(shift),*target.reference,true);
                    for(unsigned role=0;role<3;++role) {
                        for(const auto& bit:result[role])if(!bit.supported)
                            return lazy_screen_gate("origin_gram_family_selected_capture",__LINE__);
                        target.outputs[role][j]=result[role];
                    }
                }
                for(unsigned role=0;role<3;++role) {
                    gram_bindings[role]={target.identity(),target.reference->generation(),target.reference->revision(),role};
                    gram_tokens[role]={target.outputs[role],starts,0,target.identity(),target.reference->generation(),target.reference->revision(),role};
                    if(!accumulators[3*(t-next)+role]->accepts_origin_gram(gram_tokens[role],gram_bindings[role]))
                        return lazy_screen_gate("origin_gram_family_actual_count",__LINE__);
                }
            }
            gram_charge(gram_query_tick,gram_query_cpu_ns);
            for(unsigned role=0;role<3;++role) {
                const auto at=3*(t-next)+role;
                values[role]=accumulators[at]->finish(stop,use_origin_gram||t==next?nullptr:&trace[role],
                    use_origin_gram?&gram_tokens[role]:nullptr,use_origin_gram?&gram_bindings[role]:nullptr);
                if(t==next && !use_origin_gram)trace[role]=accumulators[role]->trace_reuse();
                const auto& work=accumulators[at]->work();runtime::add_count(execution.inverse_transforms,work.inverse_transforms);
                runtime::add_count(cost.trace_inverse_reuses,work.trace_inverse_reuses);runtime::add_count(cost.trace_reuse_fallbacks,work.trace_reuse_fallbacks);
            }
            auto& g=geometry[t];auto& range=*g.range;range.screen.resize(starts.size());range.reference.resize(starts.size());
            for(std::size_t j=0;j<starts.size();++j) {
                if((j&63U)==0)cancelled(stop);
                if(!basis::combine(values[0][j],values[1][j],values[2][j],weights[j],subtraction,2,range.screen[j]))
                    return lazy_screen_gate("fractional_family_contraction",__LINE__);
            }cancelled(stop);
            range.numeric_origin_stride=1;
            // Actual source displacement <=.5*1.01+.001; physical
            // cutoff shift <.506/.99<1 sample. The existing two-per-edge
            // reserve encloses ceil and strict gain>0 endpoints, including
            // each smooth FHSS zero edge. Union rows keep their full residual.
            range.screen_first=range.first;range.mask_difference=lazy_screen_mask_difference(groups);range.maximum=maximum;
            range.proxy_residual_square=g.residual_square;range.proxy_residual_l1=g.residual_l1;range.proxy_residual_gram=g.residual_gram;
            for(std::size_t j=0;j<starts.size();++j) {
                if((j&63U)==0)cancelled(stop);
                if(range.mask_difference>std::numeric_limits<std::uint64_t>::max()-range.screen[j].fits[0].count)
                    return lazy_screen_gate("fractional_frequency_family",__LINE__);
                const auto n=range.screen[j].fits[0].count+range.mask_difference;
                if(n>(std::numeric_limits<std::size_t>::max()-128*block_samples-128)/128)return lazy_screen_gate("fractional_frequency_family",__LINE__);
                const auto rho=cert::add(cert::gamma(128*static_cast<std::size_t>(n)+128*block_samples+128,std::numeric_limits<double>::epsilon()/2),
                    cert::mul(4*block_samples,cert::gamma(8*block_samples+8,std::numeric_limits<double>::epsilon()/2)));
                const auto dot=cert::mul(8*rho*qmax,cert::root(cert::mul(static_cast<double>(n),union_energy)));
                const auto gram=cert::mul(32*rho*n,qmax*qmax);
                const auto conditioned=range.reference[j].conditioned_proxy_dot_upper;
                range.reference[j]={dot,gram,cert::mul(4*rho,union_energy),n>2?1e-8/(.5*static_cast<double>(n-2)):0.,true,conditioned};
            }
            range.exact_observation_mask=false;
            if constexpr(std::is_same_v<T,float>)++execution.fp32_ranges;else ++execution.fp64_ranges;
            ++execution.proxy_ranges;range.certificate_cohort=certdiag::cohort<T>(true);
            if(lazy_state()->certificate_profile.enabled)execution.certificate_pending.observe(range.certificate_cohort,
                static_cast<double>(g.displacement),g.residual_l1,g.residual_gram,g.residual_square,accumulators[3*(t-next)]->certificate_profile());
        }
        trace={};accumulators.clear();check(live());
        audio::trace::record("scheduled_fractional_frequency_family",[&](auto& f){f.add("epoch",config.stream_epoch);
            f.add("targets",count);f.add("lanes",starts.size());f.add("span",span);f.add("moments",use_moments);
            f.add("origin_gram",use_origin_gram);f.add("origin_gram_checkpoint_count",gram_checkpoints.size());
            std::size_t gram_bytes=0;for(const auto& target:gram_targets)gram_bytes=tile::add_size(gram_bytes,target->working_bytes());
            f.add("origin_gram_target_bytes",gram_bytes);
            f.add("origin_gram_bulk",use_origin_gram&&gram_build==origin_gram::Build::native_compensated);
            std::uint64_t gram_rows=0,gram_batches=0,gram_interval_rows=0,gram_null_batches=0;for(const auto& target:gram_targets){
                runtime::add_count(gram_rows,target->prefix->native_rows());runtime::add_count(gram_batches,target->prefix->native_batches());
                runtime::add_count(gram_interval_rows,target->prefix->interval_rows());runtime::add_count(gram_null_batches,target->prefix->native_null_batches());}
            f.add("origin_gram_native_rows",gram_rows);f.add("origin_gram_native_batches",gram_batches);
            f.add("origin_gram_interval_rows",gram_interval_rows);f.add("origin_gram_null_batches",gram_null_batches);
            f.add("origin_gram_cpu_timing",gram_timing);f.add("origin_gram_cpu_valid",gram_cpu_valid);
            f.add("origin_gram_constructor_cpu_ns",gram_constructor_cpu_ns);
            f.add("origin_gram_append_cpu_ns",gram_append_cpu_ns);f.add("origin_gram_query_cpu_ns",gram_query_cpu_ns);
            f.add("chord_rows",chord_rows);f.add("chord_evaluations",chord_evaluations);f.add("coarse_rows",coarse_rows);
            f.add("derivative_chord",use_derivative_chord);f.add("periodic_cell_qualified",periodic_cell_qualified);
            f.add("derivative_table_bytes",derivative_table?derivative_table->working_bytes():0);
            f.add("derivative_table_setup_bytes",derivative_setup_bytes);
            f.add("derivative_window_segments",derivative_table?derivative_table->window_segments():0);
            f.add("derivative_aggregate_requested",use_derivative_aggregate);
            f.add("derivative_aggregate_window_segments",derivative_table?derivative_table->aggregate_window_segments():0);
            f.add("derivative_aggregate_lookup_rows",derivative_aggregate_rows);
            f.add("derivative_aggregate_valid_chord_rows",derivative_aggregate_valid_rows);
            f.add("derivative_aggregate_reference_fallback_rows",derivative_aggregate_fallback_rows);
            f.add("exact_zero_guard_rows",zero_guard_rows);f.add("zero_guard_radius_ticks",zero_guard_radius);
            f.add("endpoint_proven_rows",chord_rows);f.add("endpoint_fallback_rows",endpoint_fallback_rows);
            f.add("gain_chord_rows",gain_chord_rows);f.add("fhss_flat_rows",fhss_flat_rows);f.add("fhss_unproved_rows",fhss_unproved_rows);gain_totals.fields(f);
            f.add("scope","opt-in whole A/B/D FFT with existing fine-frequency input fusion; original owned-whole guards/refinement");});
        next+=count;
    }
    cancelled(stop);runtime::publish(execution,cost);
    if(lazy_state()->certificate_profile.enabled)execution.certificate_preparation.merge(execution.certificate_pending);
    cancelled(stop);for(auto* range:ranges)range->independent_whole_template=true;
    reset.complete=true;component_complete=true;return true;
}

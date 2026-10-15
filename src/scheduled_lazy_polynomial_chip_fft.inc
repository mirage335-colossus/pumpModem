    // One physical overlap-save tile feeds all source/private models sharing
    // this exact raw-carrier frequency and Original capture/restart ledger.
    // Failure leaves the existing single-window FIR producer available.
    bool lazy_colored_polynomial_fft_fill(
            detail::scheduled_polynomial_chip::PhysicalKey request,std::stop_token stop,
            const detail::scheduled_owned_fft_window_filter::View* window_filter=nullptr) {
        namespace fft=detail::scheduled_polynomial_chip_fft;
        namespace poly=detail::scheduled_polynomial_chip;
        namespace cert=detail::scheduled_acquisition_certificate;
        namespace tile=detail::scheduled_acquisition_tile;
        if(!lazy_state()->colored_polynomial_fft_requested || !lazy_state()->colored_polynomial_cache)return false;
        ++lazy_state()->colored_polynomial_fft_queries;
        require(lazy_state()->colored_source_active,"FFT moment fill without source/cache owner pin");
        struct Loan {
            Impl& self;std::size_t amount;
            Loan(Impl& value,std::size_t bytes):self(value),amount(bytes) {
                self.lazy_state()->transient_reservation+=amount;
                self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;
            }
            void retain(std::size_t bytes) {
                require(bytes<=amount,"FFT moment loan transfer overflow");
                amount-=bytes;self.lazy_state()->transient_reservation-=bytes;
                self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;
            }
            ~Loan(){self.lazy_state()->transient_reservation-=amount;
                self.accounted_bytes=self.working_bytes()+self.unallocated_history_bytes()+self.drift_reserved+self.amplitude_scratch_reservation;}
        };
        const auto affordable=[&](std::size_t bytes) {
            accounted_bytes=working_bytes()+unallocated_history_bytes()+drift_reserved+amplitude_scratch_reservation;
            return sizeof(PatternCorrelator)+accounted_bytes<=budget && bytes<=budget-sizeof(PatternCorrelator)-accounted_bytes;
        };
        try {
            if(!lazy_state()->colored_polynomial_fft_plan) {
                const fft::Config config{4096,16*1024*1024};
                const auto reserve=fft::Plan::reservation(config);
                if(!affordable(reserve)){++lazy_state()->colored_polynomial_fft_workspace_fallbacks;return false;}
                Loan loan(*this,reserve);
                auto fresh=std::make_unique<fft::Plan>(config,stop);
                const auto charge=fresh->working_bytes();require(charge<=reserve,"FFT moment plan exceeded reservation");
                cancelled(stop);lazy_state()->colored_polynomial_fft_plan=std::move(fresh);loan.retain(charge);
                ++lazy_state()->colored_polynomial_fft_plan_builds;
                lazy_state()->colored_polynomial_fft_kernel_transforms+=lazy_state()->colored_polynomial_fft_plan->kernel_transforms();
            }
            const auto& plan=*lazy_state()->colored_polynomial_fft_plan;
            const auto payload=plan.maximum_payload_samples(),maximum=plan.maximum_input_samples();
            require(payload && maximum<=4096 && maximum>=34,"FFT moment plan geometry unsupported");
            const auto base=request.physical_first-request.physical_first%payload;
            const auto first=std::max(base,lazy_history_first());
            const auto end=std::min({tile::add_coordinate(base,maximum),lazy_state()->screen_capture_end,lazy_state()->capture_end});
            if(end<first || end-first<34 || request.physical_first<first || request.physical_first>end-34)return false;
            const auto count=static_cast<std::size_t>(end-first);
            // Actual target8: C34, origin17, phase136, Fs6120. Filling only the
            // REQUESTED residue requires no global hypothesis-lattice claim.
            std::array<std::size_t,256> selected{};
            const auto selection=detail::scheduled_owned_fft_window_filter::select(
                window_filter,request.physical_first,first,count,selected,stop);
            const auto selected_count=selection.count;
            const auto selected_view=std::span<const std::size_t>(selected).first(selected_count);
            constexpr std::size_t input_reservation=128*1024;
            const auto reserve=tile::add_size(input_reservation,plan.execution_reservation(count,selected_count));
            if(!affordable(reserve)){++lazy_state()->colored_polynomial_fft_workspace_fallbacks;return false;}
            Loan loan(*this,reserve);
            std::vector<float> pcm(count);
            std::vector<Complex> oscillator(count);
            std::vector<std::uint8_t> observed(count,0);
            const auto input_bytes=sizeof(pcm)+sizeof(oscillator)+sizeof(observed)+sizeof(selected)+
                pcm.capacity()*sizeof(float)+oscillator.capacity()*sizeof(Complex)+observed.capacity();
            require(input_bytes<=input_reservation,"FFT moment input exceeded reservation");
            const auto frequency=std::bit_cast<double>(request.frequency);
            if(!std::isfinite(frequency))return false;
            const auto step=std::polar(1.,tau*frequency/config.sample_rate);
            lazy_visit_blocks(first,end,[&](const auto& block) {
                cancelled(stop);
                const auto a=std::max(first,block.first),b=std::min(end,block.first+block.pcm.size());
                auto o=std::polar(1.,static_cast<double>(std::remainder(static_cast<long double>(block.first)*tau*frequency/config.sample_rate,static_cast<long double>(tau))));
                for(auto i=block.first;i<a;++i)o*=step;
                for(auto i=a;i<b;++i) {
                    const auto index=static_cast<std::size_t>(i-first);
                    pcm[index]=block.pcm[i-block.first];oscillator[index]=o;observed[index]=1;
                    if(cert::norm(o)>2)throw Error("FFT moment carrier exceeded Original bound");
                    o*=step;
                }
            },false);
            request.physical_first=first;
            const fft::InputTile input{request,pcm,oscillator,observed};
            const auto work=fft::fill_cache(input,selected_view,plan,*lazy_state()->colored_polynomial_cache,stop);
            cancelled(stop);
            ++lazy_state()->colored_polynomial_fft_tiles;
            lazy_state()->colored_polynomial_fft_windows+=selected_count;
            lazy_state()->colored_polynomial_fft_input_rows+=count;
            lazy_state()->colored_polynomial_fft_input_transforms+=work.input_transforms;
            lazy_state()->colored_polynomial_fft_inverse_transforms+=work.inverse_transforms;
            lazy_state()->colored_polynomial_fft_spectral_products+=work.spectral_products;
            lazy_state()->colored_polynomial_fft_cache_installs+=work.cache_installs;
            return true;
        } catch(const Error&) {cancelled(stop);++lazy_state()->colored_polynomial_fft_numeric_fallbacks;return false;}
          catch(const std::bad_alloc&) {cancelled(stop);++lazy_state()->colored_polynomial_fft_workspace_fallbacks;return false;}
    }

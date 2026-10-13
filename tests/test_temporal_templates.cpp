// Actual source-inclusion regression; root owns build/runtime qualification.
#include "../src/pattern_correlator.cpp"
#include <iostream>
#include <random>
#include <openssl/evp.h>
#include <string_view>
#include <future>
#include <functional>

namespace datapump::modem {
struct PatternCorrelatorTimingTestAccess {
    using Batch=detail::ScheduledTemporalTemplates;
    using State=PatternCorrelator::Impl;
    static void check(bool ok,const char* message){if(!ok)throw Error(message);}
    static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
    static bool bits(Complex a,Complex b){return bits(a.real(),b.real())&&bits(a.imag(),b.imag());}

    // Process comparison is intentionally independent of the process-static
    // FLAT/R1 switches. Serialize semantic fields, never object padding,
    // pointers, crypto keys, wall times or optimization counters. Buffering
    // keeps SHA256 storage bounded independently of the number of snapshots.
    class SemanticDigest {
        std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> context_{EVP_MD_CTX_new(),EVP_MD_CTX_free};
        std::array<unsigned char,4096> pending_{};
        std::size_t used_=0;
        std::uint64_t bytes_=0;
        void flush() {
            check(EVP_DigestUpdate(context_.get(),pending_.data(),used_)==1,"semantic digest update failed");used_=0;
        }
    public:
        SemanticDigest() {
            check(context_ && EVP_DigestInit_ex(context_.get(),EVP_sha256(),nullptr)==1,"semantic digest initialization failed");
            text("temporal-original-state-v1");
        }
        void octet(unsigned char value) {pending_[used_++]=value;++bytes_;if(used_==pending_.size())flush();}
        void word(std::uint64_t value) {for(unsigned i=0;i<8;++i)octet(static_cast<unsigned char>(value>>(8*i)));}
        void text(std::string_view value) {word(value.size());for(const auto c:value)octet(static_cast<unsigned char>(c));}
        void real(double value) {word(std::bit_cast<std::uint64_t>(value));}
        void complex(Complex value) {real(value.real());real(value.imag());}
        // frexp and power-of-two extraction are exact for finite binary LD.
        // This preserves all significand bits and signed zero without x87/
        // binary128 padding, byte-order assumptions or FP64 truncation.
        void wide(long double value) {
            static_assert(std::numeric_limits<long double>::radix==2);
            word(std::signbit(value));
            check(!std::isnan(value),"semantic digest refuses NaN long-double state");
            if(std::isinf(value)){word(2);return;}
            if(value==0){word(0);return;}
            word(1);word(std::numeric_limits<long double>::digits);
            int exponent=0;auto fraction=std::frexp(std::abs(value),&exponent);
            word(static_cast<std::uint64_t>(static_cast<std::int64_t>(exponent)));
            for(int left=std::numeric_limits<long double>::digits;left>0;) {
                const auto width=std::min(left,32);fraction=std::ldexp(fraction,width);
                const auto part=static_cast<std::uint32_t>(fraction);word(part);fraction-=part;left-=width;
            }
            check(fraction==0,"semantic digest lost long-double significand bits");
        }
        void wide_complex(std::complex<long double> value) {wide(value.real());wide(value.imag());}
        std::uint64_t bytes()const{return bytes_;}
        std::string finish() {
            flush();std::array<unsigned char,EVP_MAX_MD_SIZE> result{};unsigned size=0;
            check(EVP_DigestFinal_ex(context_.get(),result.data(),&size)==1 && size==32,"semantic digest finalization failed");
            constexpr char hex[]="0123456789abcdef";std::string out;out.reserve(64);
            for(unsigned i=0;i<size;++i){out.push_back(hex[result[i]>>4]);out.push_back(hex[result[i]&15]);}return out;
        }
    };
    static SemanticDigest& digest(){static SemanticDigest value;return value;}
    inline static std::uint64_t state_boundaries_=0,evidence_boundaries_=0,burst_boundaries_=0;
    template<class Values,class Consume> static void digest_range(const Values& values,Consume consume) {
        digest().word(values.size());for(const auto& value:values)consume(value);
    }
    static void digest_fit(const detail::CorrelationFit& x) {
        auto& d=digest();d.real(x.xc);d.real(x.xs);d.real(x.cc);d.real(x.ss);d.real(x.cs);d.real(x.energy);d.word(x.count);
    }
    static void digest_chip(const detail::CorrelationChipEvidence& x) {
        digest_fit(x.cell);auto& d=digest();d.real(x.projected_energy);d.word(x.projected_rank);d.word(x.cell_index);d.word(x.nested);
    }
    static void digest_outer(const detail::CorrelationOuterEvidence& x) {
        auto& d=digest();for(auto z:x.pulse)d.complex(z);for(auto z:x.dot)d.wide_complex(z);
        for(auto v:x.variance)d.wide(v);for(auto n:x.contributing_terms)d.word(n);d.word(x.first);d.word(x.last);d.word(x.initialized);
    }
    static void digest_drift(const detail::CorrelationDriftFit& x) {
        digest_fit(x.active);auto& d=digest();d.real(x.explained_sum);d.real(x.largest_explained);d.word(x.section);
    }
    static void digest_differential(const detail::CorrelationDifferentialFit& x) {
        digest_fit(x.active);auto& d=digest();d.word(x.window_index);d.word(x.expected_count);d.word(x.initialized);
        for(auto z:x.evidence.sums)d.complex(z);d.real(x.evidence.squared_magnitudes);d.complex(x.evidence.previous);
        d.word(x.evidence.previous_index);d.word(x.evidence.pairs);d.word(x.evidence.pending);
    }
    static void digest_burst(const PatternBurst& x) {
        auto& d=digest();digest_range(x.bits,[&](auto bit){d.word(bit);});d.word(x.first_sample);d.word(x.end_sample);
        d.word(x.first_stream_symbol);d.real(x.frequency_hz);d.real(x.score);d.word(x.complete);d.word(x.stream_phase_samples);
        d.word(x.stream_first_sample);d.word(x.stream_first_symbol);d.word(x.missing_slots);d.real(x.support_samples);
    }
    static void digest_evidence(const PatternEvidence& x) {
        auto& d=digest();d.word(x.first_sample);d.word(x.end_sample);d.word(x.stream_symbol);d.real(x.frequency_hz);
        d.real(x.score);d.real(x.alternative_score);d.word(x.bit);d.word(x.stream_phase_samples);d.real(x.admission_threshold);
        d.word(x.frequency_hypothesis);d.word(x.outer_presence_active);d.real(x.outer_presence_score);
    }
    static void digest_state(const State& x,std::string_view side) {
        auto& d=digest();d.text("state");d.text(side);d.word(x.sample);d.word(x.trials);d.word(x.finished);
        d.word(x.drift_sections);d.word(x.differential_window);d.word(x.outer_presence);d.word(x.guard_chains);
        d.word(x.timing_enabled);d.word(x.timing_dirty);d.word(x.amplitude_publication_dirty);
        digest_range(x.hypotheses,[&](const auto& h) {
            d.wide(h.origin);d.wide(h.rate);d.word(h.index);d.word(h.observed_start);d.word(h.phase_lower);d.word(h.phase_upper);
            for(const auto& fit:h.fits)digest_fit(fit);digest_burst(h.burst);d.real(h.sum_score);d.real(h.pending_score);d.real(h.sum_support);
            d.word(h.committed);d.word(h.gap_slots);d.word(h.committed_end);d.word(h.frequency);d.word(h.rate_index);d.word(h.tone_bank_base);
            d.word(h.admitted);d.word(h.pending_gaps);d.word(h.affine_groups);
        });
        const auto pair_range=[&](const auto& values,auto consume){digest_range(values,[&](const auto& pair){for(const auto& v:pair)consume(v);});};
        pair_range(x.alternate_fits,digest_fit);pair_range(x.chip_evidence,digest_chip);
        pair_range(x.drift_fits,digest_drift);pair_range(x.differential_fits,digest_differential);
        digest_range(x.pending_chip_scores,[&](auto v){d.real(v);});
        d.word(bool(x.outer_evidence));if(x.outer_evidence)digest_range(*x.outer_evidence,digest_outer);
        d.word(bool(x.scheduled));
        if(x.scheduled) {
            const auto& s=*x.scheduled;
            digest_range(s.scheduled_carry,[&](const auto& v){for(const auto& fit:v.fits)digest_fit(fit);d.word(v.observed_start);});
            pair_range(s.scheduled_carry_chips,digest_chip);pair_range(s.scheduled_carry_drift,digest_drift);
            pair_range(s.scheduled_carry_differential,digest_differential);digest_range(s.scheduled_carry_outer,digest_outer);
            pair_range(s.scheduled_quarter_ranks,[&](auto v){d.word(v);});pair_range(s.scheduled_carry_quarter_ranks,[&](auto v){d.word(v);});
            digest_range(s.anchors,[&](const auto& v){d.word(v.sample);d.wide(v.fraction);});
            digest_range(s.observed_begin,[&](auto v){d.wide(v);});digest_range(s.last_accepted,[&](auto v){d.word(v);});
            digest_range(s.source_starts,[&](auto v){d.word(v);});digest_range(s.symbol_ends,[&](auto v){d.wide(v);});
            digest_range(s.end_indices,[&](auto v){d.word(v);});digest_range(s.first_full_indices,[&](auto v){d.word(v);});d.wide(s.earliest_end);
            // These are the ORIGINAL cache fields already compared below;
            // no new optional Temporal/Plan/FP32 counters enter the digest.
            digest_range(s.worker_template_caches,[&](const auto& cache) {
                d.word(bool(cache));if(!cache)return;d.word(cache->hits);d.word(cache->constructions);d.word(cache->stamp);d.word(cache->block);
                for(const auto& row:cache->rows) {
                    d.word(row.occupied);d.wide(row.origin);d.wide(row.rate);d.word(row.phase);d.word(row.index);d.word(row.stamp);
                    for(auto mask:row.valid)d.word(mask);
                    for(std::size_t n=0;n<512;++n)if(row.valid[n/64]&(1ULL<<(n%64)))for(auto value:row.value[n])d.complex(value);
                }
            });
        }
        digest_range(x.history,digest_evidence);digest_range(x.bursts,digest_burst);
        digest_range(x.emissions,[&](const auto& e) {
            auto owner=std::numeric_limits<std::uint64_t>::max();
            if(e.owner)for(std::size_t i=0;i<x.hypotheses.size();++i)if(e.owner==&x.hypotheses[i]){owner=i;break;}
            check(!e.owner || owner!=std::numeric_limits<std::uint64_t>::max(),"semantic emission owner is not an original lane");
            d.word(owner);d.word(e.first_symbol);d.word(e.last_symbol);d.word(e.stream_first_sample);d.wide(e.origin);
            d.real(e.frequency);d.real(e.reported_frequency);d.real(e.carrier_score);d.word(e.ended);d.word(e.published_unknown);
        });
        d.word(bool(x.timing));if(x.timing)digest_range(*x.timing,[&](const auto& entry) {
            d.word(entry.hypothesis);const auto& t=entry.state;
            for(const auto& v:t.decisions){d.word(v.index);d.word(v.epoch);d.word(v.ordinal);d.real(v.score);d.word(v.accepted);d.word(v.full);d.word(v.bit);}
            for(const auto& v:t.relations){d.word(v.neighbor);d.word(v.next);d.word(v.first_unaccepted);d.real(v.owner_score);d.real(v.guide_score);d.word(v.invalid);}
            d.word(t.first);d.word(t.last_accepted);d.word(t.terminal_index);d.word(t.tracking);d.word(t.pending_terminal);
        });
        d.word(x.work.scheduled_carry_samples);d.word(x.work.scheduled_colored_guard_activations);d.word(x.work.scheduled_quarter_guard_fallbacks);
    }

    static Config configuration() {
        Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
        c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
        c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x67));
        for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
        c.stream_epoch=1799999967;return c;
    }
    static void same(const detail::CorrelationFit& a,const detail::CorrelationFit& b) {
        check(bits(a.xc,b.xc)&&bits(a.xs,b.xs)&&bits(a.cc,b.cc)&&bits(a.ss,b.ss)&&bits(a.cs,b.cs)&&bits(a.energy,b.energy)&&a.count==b.count,
              "raw-output copy changed original full real Fit bits/E/N");
    }
    static void raw_oracles() {
        auto c=configuration();PatternCode code(c,c.stream_epoch),original(c,c.stream_epoch);
        check(code.chip_samples()==15,"temporal oracle lost original D100 chip15 geometry");
        auto batch=std::make_unique<Batch>();
        for(const std::uint64_t shift:{0ULL,555ULL,15000ULL,45000ULL}) {
            const auto phase=(shift/15*17)%c.sample_rate,first_chip=(shift/15%7)*code.chips_per_symbol();
            batch->begin(42,4096);batch->start(phase,first_chip,4096);
            for(std::size_t n=0;n<512;++n)if(n%7)check(batch->append(n,135.5+shift+n),"bounded raw exporter overflow");
            batch->end(4608);const auto prior_rows=batch->statistics.numeric.weight_rows;
            check(batch->prepare(code,c),"translated raw tile unexpectedly unavailable");
            if(shift)check(batch->statistics.numeric.weight_rows==prior_rows,
                "genuinely translated private binding rebuilt the public weight plane");
            code.set_stream_phase_samples(phase);code.prepare_symbol(first_chip);
            original.set_stream_phase_samples(phase);original.prepare_symbol(first_chip);
            std::array<detail::CorrelationFit,2> actual{},expected{};
            for(std::size_t n=0;n<512;++n) {
                std::array<Complex,2> raw{};const auto at=135.5+shift+n;
                const auto found=batch->raw(42,4096,phase,first_chip,4096+n,n,at,raw);
                check(found==(n%7!=0),"raw tile changed physical selected-mask identity");if(!found)continue;
                const auto reference=original.shaped_values(first_chip,at);
                const auto carrier=std::polar(1.,.317*n);const auto x=.6*std::sin(.27*n);
                const detail::CorrelationProjection p{x*carrier.real(),x*carrier.imag(),carrier.real()*carrier.real(),carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),x*x};
                for(unsigned bit=0;bit<2;++bit) {
                    check(bits(raw[bit],reference[bit]),"translated Plan changed original nonzero pulse sum bits");
                    const auto a=.73*pattern_limit_pcm(2.2*raw[bit])/2.2,b=.73*pattern_limit_pcm(2.2*reference[bit])/2.2;
                    check(bits(a,b),"copy changed original limiter bits");
                    detail::CorrelationFit ca,cb;ca.add(p,a,1);cb.add(p,b,1);
                    const auto add=[](auto& to,const auto& from){to.xc+=from.xc;to.xs+=from.xs;to.cc+=from.cc;to.ss+=from.ss;to.cs+=from.cs;to.energy+=from.energy;to.count+=from.count;};
                    add(actual[bit],ca);add(expected[bit],cb);
                }
            }
            for(unsigned bit=0;bit<2;++bit)same(actual[bit],expected[bit]);
            std::array<Complex,2> unused;
            check(!batch->raw(41,4096,phase,first_chip,4097,1,136.5+shift,unused) &&
                  !batch->raw(42,4097,phase,first_chip,4097,1,136.5+shift,unused) &&
                  !batch->raw(42,4096,(phase+1)%c.sample_rate,first_chip,4097,1,136.5+shift,unused) &&
                  !batch->raw(42,4096,phase,first_chip+1,4097,1,136.5+shift,unused) &&
                  !batch->raw(42,4096,phase,first_chip,4097,1,std::nextafter(136.5+shift,0.),unused),
                  "stale/private/within identity accepted prepared output");
        }
        check(batch->statistics.normalized_reuses>=3,"whole-chip translated private lanes did not share public Plan");
        // Exact anchored nonnominal coordinates, halfpoint/binade neighbors,
        // original nineteen-raw-loop/zero-extra witness and partial final chip.
        for(const double start:{std::nextafter(405.,0.),256.,32768.,95968.25}) {
            batch->begin(1,0);batch->start(0,0,0);std::size_t count=0;
            for(;count<32 && start+count<code.symbol_samples();++count)
                batch->append(count,static_cast<double>(static_cast<long double>(start)+count*(1.L+3e-6L)));
            batch->end(count);check(batch->prepare(code,c),"edge original-plan fallback unavailable");
            original.set_stream_phase_samples(0);original.prepare_symbol(0);
            for(std::size_t n=0;n<count;++n) {
                const auto at=static_cast<double>(static_cast<long double>(start)+n*(1.L+3e-6L));std::array<Complex,2> value;
                check(batch->raw(1,0,0,0,n,n,at,value),"edge output missing");const auto ref=original.shaped_values(0,at);
                check(bits(value[0],ref[0])&&bits(value[1],ref[1]),"edge/nonzero callback order differs");
            }
        }
        auto partial_config=c;partial_config.integration_seconds=std::nextafter(16.0005,0.);
        PatternCode partial(partial_config,c.stream_epoch),partial_original(partial_config,c.stream_epoch);
        check(partial.symbol_samples()%partial.chip_samples()!=0,"partial final-chip oracle lost intended extent");
        batch->begin(1,0);batch->start(0,0,0);
        for(std::size_t n=0;n<32;++n)batch->append(n,partial.symbol_samples()-31.75+n);batch->end(32);
        check(batch->prepare(partial,partial_config),"partial final-chip raw batch unavailable");
        for(std::size_t n=0;n<32;++n) {
            const auto at=partial.symbol_samples()-31.75+n;std::array<Complex,2> value;
            check(batch->raw(1,0,0,0,n,n,at,value),"partial output missing");const auto ref=partial_original.shaped_values(0,at);
            check(bits(value[0],ref[0])&&bits(value[1],ref[1]),"partial final-chip weight/label changed");
        }
        batch->begin(1,0);batch->start(0,0,0);
        for(std::size_t n=0;n<8;++n)batch->append(n,150.+300*n);batch->end(8);
        check(!batch->prepare(code,c),"coefficient-union overflow did not fall back");
        batch->start(0,0,0);for(std::size_t n=0;n<32;++n)batch->append(n,150.+n);batch->end(32);
        std::stop_source cancel;cancel.request_stop();bool stopped=false;
        try{batch->prepare(code,c,cancel.get_token());}catch(const Error&){stopped=true;}
        check(stopped,"cancelled planning/gather did not stop");
        std::array<Complex,2> unused;check(!batch->raw(1,0,0,0,1,1,151.,unused),"cancelled batch exposed stale output");
    }
    static void states_equal(const State& a,const State& b) {
        check(a.sample==b.sample&&a.trials==b.trials&&a.hypotheses.size()==b.hypotheses.size(),"batch changed original trial/cursor order");
        for(std::size_t i=0;i<a.hypotheses.size();++i) {
            const auto& x=a.hypotheses[i];const auto& y=b.hypotheses[i];
            check(x.index==y.index&&x.admitted==y.admitted&&x.observed_start==y.observed_start&&x.burst.bits==y.burst.bits&&x.committed==y.committed,"batch changed source ownership/admission/prefix");
            for(unsigned bit=0;bit<2;++bit) {
                same(x.fits[bit],y.fits[bit]);same(a.scheduled->scheduled_carry[i].fits[bit],b.scheduled->scheduled_carry[i].fits[bit]);
                if(!a.chip_evidence.empty()) {
                    const auto compare=[](const auto& p,const auto& q){same(p.cell,q.cell);check(bits(p.projected_energy,q.projected_energy)&&p.projected_rank==q.projected_rank&&p.cell_index==q.cell_index&&p.nested==q.nested,"batch changed chip-chain guard");};
                    compare(a.chip_evidence[i][bit],b.chip_evidence[i][bit]);compare(a.scheduled->scheduled_carry_chips[i][bit],b.scheduled->scheduled_carry_chips[i][bit]);
                }
            }
            if(a.outer_presence) {
                const auto compare=[](const auto& p,const auto& q){check(p.first==q.first&&p.last==q.last&&p.initialized==q.initialized&&p.pulse==q.pulse&&p.dot==q.dot&&p.variance==q.variance&&p.contributing_terms==q.contributing_terms,"batch changed original ordered outer ring");};
                compare((*a.outer_evidence)[i],(*b.outer_evidence)[i]);compare(a.scheduled->scheduled_carry_outer[i],b.scheduled->scheduled_carry_outer[i]);
            }
        }
        const auto drift=[](const auto& x,const auto& y){
            check(x.size()==y.size(),"batch changed drift storage");
            for(std::size_t i=0;i<x.size();++i)for(unsigned bit=0;bit<2;++bit){same(x[i][bit].active,y[i][bit].active);check(x[i][bit].section==y[i][bit].section&&bits(x[i][bit].explained_sum,y[i][bit].explained_sum)&&bits(x[i][bit].largest_explained,y[i][bit].largest_explained),"batch changed quarter state");}
        };
        drift(a.drift_fits,b.drift_fits);drift(a.scheduled->scheduled_carry_drift,b.scheduled->scheduled_carry_drift);
        const auto differential=[](const auto& x,const auto& y){
            check(x.size()==y.size(),"batch changed differential storage");
            for(std::size_t i=0;i<x.size();++i)for(unsigned bit=0;bit<2;++bit){
                const auto& p=x[i][bit];const auto& q=y[i][bit];same(p.active,q.active);
                check(p.window_index==q.window_index&&p.expected_count==q.expected_count&&p.initialized==q.initialized&&p.evidence.sums==q.evidence.sums&&bits(p.evidence.squared_magnitudes,q.evidence.squared_magnitudes)&&bits(p.evidence.previous,q.evidence.previous)&&p.evidence.previous_index==q.evidence.previous_index&&p.evidence.pairs==q.evidence.pairs&&p.evidence.pending==q.evidence.pending,"batch changed differential evidence");
            }
        };
        differential(a.differential_fits,b.differential_fits);differential(a.scheduled->scheduled_carry_differential,b.scheduled->scheduled_carry_differential);
        check(a.scheduled->worker_template_caches.size()==b.scheduled->worker_template_caches.size(),"batch changed original template cache count");
        for(std::size_t worker=0;worker<a.scheduled->worker_template_caches.size();++worker) {
            const auto& x=a.scheduled->worker_template_caches[worker];const auto& y=b.scheduled->worker_template_caches[worker];
            check(bool(x)==bool(y),"batch changed original cache availability");if(!x)continue;
            check(x->hits==y->hits&&x->constructions==y->constructions&&x->stamp==y->stamp&&x->block==y->block,"batch changed actual cache membership/counters");
            for(std::size_t row=0;row<x->rows.size();++row) {
                const auto& p=x->rows[row];const auto& q=y->rows[row];
                check(p.occupied==q.occupied&&p.origin==q.origin&&p.rate==q.rate&&p.phase==q.phase&&p.index==q.index&&p.stamp==q.stamp&&p.valid==q.valid,"batch changed original row/LRU identity");
                for(std::size_t n=0;n<512;++n)if(p.valid[n/64]&(1ULL<<(n%64)))for(unsigned bit=0;bit<2;++bit)
                    check(bits(p.value[n][bit],q.value[n][bit]),"batch changed limited cached template bits");
            }
        }
        ++state_boundaries_;digest_state(a,"reference");digest_state(b,"temporal");
    }
    static void sampled_prefix() {
        auto c=configuration();auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=1;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);const auto start=map.source_to_physical(map.locate(c.stream_epoch*c.sample_rate).source);
        c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
        PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=15./c.sample_rate;
        search.frequency_offsets_hz={0,.13};search.clock_errors_ppm={0,3};search.worker_threads=1;search.bit_limit=4;search.chunk_bits=1;search.track_limit=64;search.differential_window_seconds=.01;
        PatternCorrelatorOptions baseline;baseline.scheduled_family_experiment=baseline.scheduled_common_shape_experiment=false;
        auto enabled=baseline;enabled.scheduled_temporal_template_experiment=true;
        PatternCorrelator a(c,search,16*1024*1024,baseline),b(c,search,16*1024*1024,enabled);
        std::array<float,512> input{};
        for(std::size_t block=0;block<8;++block) {
            for(std::size_t n=0;n<input.size();++n)input[n]=static_cast<float>(.2*std::sin(.071*(block*512+n))+.11*std::cos(.317*(block*512+n)));
            a.push(input);b.push(input);states_equal(*a.impl_,*b.impl_);
            check(a.take_bursts().empty()&&b.take_bursts().empty(),"partial sampled symbol invented admission/completion");
        }
        check(a.work().scheduled_temporal_attempts==0,"sampled original reference unexpectedly executed Temporal");
        check(b.work().scheduled_temporal_ready>0&&b.work().scheduled_temporal_samples>0,"enabled integration fell back throughout sampled prefix");
        check(a.work().scheduled_template_cache_hits==b.work().scheduled_template_cache_hits&&a.work().scheduled_template_constructions==b.work().scheduled_template_constructions,"batch changed original cache consumer accounting");
        const auto old_workers=b.impl_->worker_codes.size(),old_caches=b.impl_->scheduled->worker_template_caches.size();
        b.impl_->drop_scheduled_temporal_templates();check(b.impl_->worker_codes.size()==old_workers&&b.impl_->scheduled->worker_template_caches.size()==old_caches,"new scratch eviction discarded original workers/caches");
        states_equal(*a.impl_,*b.impl_);a.finish();b.finish();states_equal(*a.impl_,*b.impl_);check(a.take_bursts().empty()&&b.take_bursts().empty(),"EOF supplied physical absence");
        c.search_arithmetic=SearchArithmetic::fp64;PatternCorrelator forced(c,search,16*1024*1024,enabled);forced.push(input);
        check(!forced.work().scheduled_temporal_attempts,"FP64 force did not keep original template path");
    }
    struct FullCapture {
        std::vector<float> pcm;
        double start=0;
        std::uint64_t last=0,absent_end=0;
        std::vector<std::size_t> cuts;
    };
    static FullCapture full_capture(const Config& c,const Bytes& bits) {
        // Original production transmitter, private roots and capture seed.
        // M10/G9 bounds only this NEW fixture's physical dilation; it does not
        // change a pre-existing waveform, schedule, threshold or assertion.
        PatternTransmitter tx(bits,c,c.stream_epoch,0,false);
        check(tx.total_samples()<3*1024*1024,"bounded full D100 waveform exceeded fixed fixture extent");
        std::vector<Complex> analytic(tx.total_samples());
        for(std::size_t at=0;at<analytic.size();) {
            const auto n=tx.read_analytic(std::span(analytic).subspan(at,std::min<std::size_t>(503,analytic.size()-at)));
            check(n!=0,"full D100 transmitter stalled before physical endpoint");at+=n;
        }
        const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
        const SchedHopCoordinate origin{c.stream_epoch*c.sample_rate+c.stream_phase_samples,0};
        const auto source=map.locate(origin).source;const auto symbol=symbol_sample_count(c);
        constexpr double delay=3.375;
        const auto observed=[&](std::uint64_t source_samples) {
            return static_cast<std::uint64_t>(std::ceil(map.source_to_physical(source.add_samples(source_samples),true).sample-tx.physical_output_origin()+delay));
        };
        FullCapture out;out.start=static_cast<double>(origin.sample-tx.physical_output_origin())+delay;
        out.last=observed(bits.size()*symbol);
        const auto failed=(6ULL*c.sample_rate+symbol-1)/symbol;
        out.absent_end=observed((bits.size()+failed)*symbol);
        const auto lookahead=2ULL*c.scheduled_hopping->time_multiplier*c.sample_rate;
        out.pcm.resize(out.absent_end+2+lookahead);
        check(out.pcm.size()<3*1024*1024,"full D100 receive capture exceeded fixed sample bound");
        std::mt19937_64 random(529);std::normal_distribution<double> noise(0,.002);
        for(std::size_t i=0;i<out.pcm.size();++i) {
            const auto coordinate=static_cast<double>(i)-delay;Complex value{};
            if(coordinate>=0 && coordinate+1<analytic.size()) {
                const auto j=static_cast<std::size_t>(coordinate);const auto fraction=coordinate-j;
                value=((1-fraction)*analytic[j]+fraction*analytic[j+1])*std::polar(1.,.37);
            }
            out.pcm[i]=static_cast<float>(value.real()+noise(random));
        }
        // Actual latest-cell/carry edges remain in the ordinary public push;
        // these cuts force direct observation around nominal full-symbol ENDs.
        for(std::size_t index=1;index<=bits.size()+failed;++index) {
            const auto at=observed(index*symbol);
            if(at>1 && at+1<out.pcm.size())out.cuts.insert(out.cuts.end(),{static_cast<std::size_t>(at-1),static_cast<std::size_t>(at),static_cast<std::size_t>(at+1)});
        }
        std::sort(out.cuts.begin(),out.cuts.end());return out;
    }
    static void evidence_equal(const std::vector<PatternEvidence>& a,const std::vector<PatternEvidence>& b) {
        check(a.size()==b.size(),"temporal full fixture dropped completed evidence");
        for(std::size_t i=0;i<a.size();++i) {
            const auto& x=a[i];const auto& y=b[i];
            check(x.first_sample==y.first_sample&&x.end_sample==y.end_sample&&x.stream_symbol==y.stream_symbol&&
                  bits(x.frequency_hz,y.frequency_hz)&&bits(x.score,y.score)&&bits(x.alternative_score,y.alternative_score)&&
                  x.bit==y.bit&&x.stream_phase_samples==y.stream_phase_samples&&bits(x.admission_threshold,y.admission_threshold)&&
                  x.frequency_hypothesis==y.frequency_hypothesis&&x.outer_presence_active==y.outer_presence_active&&bits(x.outer_presence_score,y.outer_presence_score),
                  "full-symbol private/Gram/guard/threshold evidence changed bitwise");
        }
        ++evidence_boundaries_;digest().text("evidence");digest_range(a,digest_evidence);digest_range(b,digest_evidence);
    }
    static void bursts_equal(const std::vector<PatternBurst>& a,const std::vector<PatternBurst>& b) {
        check(a.size()==b.size(),"temporal full fixture changed next-poll event count");
        for(std::size_t i=0;i<a.size();++i) {
            const auto& x=a[i];const auto& y=b[i];
            check(x.bits==y.bits&&x.first_sample==y.first_sample&&x.end_sample==y.end_sample&&x.first_stream_symbol==y.first_stream_symbol&&
                  bits(x.frequency_hz,y.frequency_hz)&&bits(x.score,y.score)&&x.complete==y.complete&&x.stream_phase_samples==y.stream_phase_samples&&
                  x.stream_first_sample==y.stream_first_sample&&x.stream_first_symbol==y.stream_first_symbol&&x.missing_slots==y.missing_slots&&bits(x.support_samples,y.support_samples),
                  "full-symbol event/prefix/identity/absence changed bitwise");
        }
        ++burst_boundaries_;digest().text("bursts");digest_range(a,digest_burst);digest_range(b,digest_burst);
    }
    static void complete_sampled_carry_absence(bool common_outer=false) {
        auto c=configuration();auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto anchor=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        c.stream_epoch=anchor.sample/c.sample_rate;c.stream_phase_samples=anchor.sample%c.sample_rate;
        check(pattern_chip_samples(c)==15&&symbol_sample_count(c)==96000&&!packed_scheduled_symbols(c),"full temporal fixture lost C15/D100/N96000 split geometry");
        const Bytes expected{0,1};const auto input=full_capture(c,expected);
        PatternSearch search;search.start_offset_seconds=input.start/c.sample_rate;search.start_uncertainty_seconds=7.5/c.sample_rate;
        search.hypotheses={{0,0}};search.worker_threads=1;search.drift_tolerant=true;search.differential_window_seconds=.01;
        if(common_outer){search.hypotheses={{0,0},{.00001,0}};search.drift_tolerant=false;}
        // A complete certified clock cell supplies genuine latest-END padding.
        // N96000 ends inside G9's21600-source-sample active group; G4's9600
        // boundary padded only into quiet time and could not exercise carry.
        search.admitted_frequency_half_width_hz=0;search.admitted_clock_half_width_ppm=3;
        search.complex_frequency_coverage=search.complex_clock_coverage=true;
        search.chunk_bits=1;search.bit_limit=8;search.track_limit=16;search.candidate_limit=256;search.retain_score=0;
        PatternCorrelatorOptions original;original.scheduled_family_experiment=original.scheduled_common_shape_experiment=common_outer;
        auto enabled=original;enabled.scheduled_temporal_template_experiment=true;
        constexpr std::size_t budget=8*1024*1024,quantum=512;
        PatternCorrelator a(c,search,budget,original),b(c,search,budget,enabled);
        check(a.work().hypotheses>=2&&a.work().hypotheses<=(common_outer?10U:5U),"full temporal fixture escaped few-lane CPU bound");
        check(symbol_sample_count(c)%(plan->group_hops*(2ULL*c.sample_rate/5))!=0 &&
              a.impl_->scheduled->clock_radius>0 && a.impl_->scheduled->complex_geometry.clock_grid_qualified,
              "full temporal carry fixture lacks an interior source END or qualified clock cell");
        Bytes published;std::vector<std::size_t> polls;std::vector<std::uint64_t> accepted_ends;
        std::optional<std::pair<std::uint64_t,std::uint64_t>> identity;
        std::size_t terminals=0,completed_poll=0,first_poll=0;
        for(std::size_t at=0;at<input.pcm.size();) {
            auto n=std::min(quantum,input.pcm.size()-at);const auto cut=std::upper_bound(input.cuts.begin(),input.cuts.end(),at);
            if(cut!=input.cuts.end()&&*cut<at+n)n=*cut-at;
            const auto pcm=std::span(input.pcm).subspan(at,n);a.push(pcm);b.push(pcm);at+=n;
            states_equal(*a.impl_,*b.impl_);evidence_equal(a.candidates(),b.candidates());
            const auto x=a.take_bursts(),y=b.take_bursts();bursts_equal(x,y);
            for(const auto& event:y) {
                const auto id=std::pair{event.stream_first_sample,event.stream_first_symbol};if(!identity)identity=id;
                check(id==*identity&&!event.missing_slots&&event.first_stream_symbol==identity->second+published.size(),"full temporal fixture changed pending identity/ordered source positions");
                check(!terminals,"full temporal fixture reopened a physically ended reception");
                if(!event.bits.empty()) {
                    if(!first_poll)first_poll=at;published.insert(published.end(),event.bits.begin(),event.bits.end());
                    check(event.bits.size()==1,"full temporal fixture batched a newly accepted bit");
                    polls.push_back(at);accepted_ends.push_back(event.end_sample);
                    check(published.size()<=expected.size()&&std::equal(published.begin(),published.end(),expected.begin()),"full temporal fixture changed leading-zero private bit prefix");
                }
                if(event.complete){check(++terminals==1,"full temporal fixture duplicated physical END");completed_poll=at;}
            }
            check(at>=input.absent_end||!terminals,"full temporal fixture ended before fully observed failed source symbol");
            check(a.working_bytes()<=budget&&b.working_bytes()<=budget,"full temporal fixture exceeded charged workspace");
        }
        check(published==expected&&terminals==1&&first_poll<input.last&&completed_poll>=input.absent_end,
              "enabled full-symbol path lost exact bits/next-poll progress/scored physical END");
        const auto aw=a.work(),bw=b.work();
        check(aw.scheduled_temporal_attempts==0,"original reference unexpectedly executed Temporal");
        check(bw.scheduled_temporal_ready>0&&bw.scheduled_temporal_samples>0,"full enabled path fell back throughout");
        check(aw.scheduled_carry_samples>0&&aw.scheduled_carry_samples==bw.scheduled_carry_samples,
              "full temporal fixture failed to exercise original adjacent-symbol carry");
        check((common_outer?(aw.drift_sections==1&&aw.differential_window_samples==0):
              (aw.drift_sections==4&&aw.differential_window_samples>0))&&
              aw.scheduled_colored_guard_activations==bw.scheduled_colored_guard_activations&&aw.scheduled_quarter_guard_fallbacks==bw.scheduled_quarter_guard_fallbacks,
              "full temporal fixture changed or omitted original guard paths");
        if(common_outer)check(a.impl_->outer_presence&&b.impl_->outer_presence&&
            aw.scheduled_common_families>0&&bw.scheduled_common_raw_samples>0,
            "full common raw fixture omitted leader families or complete outer ring");
        const auto evidence=b.candidates();check(evidence.size()<search.candidate_limit,"full temporal fixture truncated evidence");
        for(std::size_t ordinal=0;ordinal<expected.size();++ordinal) {
            // Retained threshold-qualified timing aliases are not the selected
            // stream owner. Bind the next-poll deadline to the endpoint of the
            // actual published bit, then require matching scored evidence.
            const auto accepted=accepted_ends.at(ordinal);bool scored_owner=false;
            for(const auto& item:evidence)if(item.stream_symbol==ordinal&&item.bit==expected[ordinal]&&item.score>=item.admission_threshold&&
                item.end_sample==accepted&&item.score-item.alternative_score>=1&&
                (!item.outer_presence_active||item.outer_presence_score>=item.admission_threshold))scored_owner=true;
            const bool next_poll=scored_owner&&polls.at(ordinal)>=accepted&&polls.at(ordinal)-accepted<=quantum;
            if(!next_poll) {
                std::cerr<<"publication diagnostic ordinal="<<ordinal<<" selected_poll="<<polls.at(ordinal)<<" accepted_owner_end="<<accepted<<" common="<<common_outer<<'\n';
                for(const auto& item:evidence)if(item.stream_symbol==ordinal)
                    std::cerr<<" evidence end="<<item.end_sample<<" start="<<item.first_sample<<" phase="<<item.stream_phase_samples<<" frequency="<<item.frequency_hypothesis<<" bit="<<item.bit<<" score="<<item.score<<" alternative="<<item.alternative_score<<" threshold="<<item.admission_threshold<<'\n';
            }
            check(next_poll,"new full accepted bit missed its next original progress poll");
        }
        a.finish();b.finish();states_equal(*a.impl_,*b.impl_);bursts_equal(a.take_bursts(),b.take_bursts());
        check(!a.acquiring()&&!b.acquiring(),"full finished fixture remained acquiring");
        // Wrong PRIVATE roots keep the exact Data-key calendar and source
        // geometry, so this is a full-symbol coefficient/outer-presence
        // negative rather than a failed or shortened calendar search.
        auto wrong=c;
        for(std::size_t i=0;i<wrong.spreading_seed.size();++i){wrong.spreading_seed[i]^=0x5a;wrong.dsss_seed[i]^=0xa5;}
        PatternCode right_code(c,c.stream_epoch),wrong_code(wrong,c.stream_epoch);
        check(right_code.shaped_values(0,135.5)!=wrong_code.shaped_values(0,135.5),"wrong-private fixture did not change actual original template");
        auto one=search;one.start_uncertainty_seconds=0;
        PatternCorrelator wrong_a(wrong,one,budget,original),wrong_b(wrong,one,budget,enabled);
        const auto private_end=std::min(input.pcm.size(),input.cuts.front()+1+static_cast<std::size_t>(2ULL*plan->time_multiplier*c.sample_rate));
        for(std::size_t at=0;at<private_end;) {
            const auto n=std::min(quantum,private_end-at);const auto pcm=std::span(input.pcm).subspan(at,n);
            wrong_a.push(pcm);wrong_b.push(pcm);at+=n;
            states_equal(*wrong_a.impl_,*wrong_b.impl_);evidence_equal(wrong_a.candidates(),wrong_b.candidates());
            const auto x=wrong_a.take_bursts(),y=wrong_b.take_bursts();bursts_equal(x,y);
            check(x.empty()&&y.empty(),"full wrong-private symbol admitted a reception");
        }
        check(!wrong_b.candidates().empty()&&wrong_b.work().scheduled_temporal_ready>0,
              "wrong-private full-symbol negative did not reach scored enabled path");
        check(wrong_a.work().scheduled_temporal_attempts==0,"wrong-private original reference unexpectedly executed Temporal");
        wrong_a.finish();wrong_b.finish();states_equal(*wrong_a.impl_,*wrong_b.impl_);check(wrong_a.take_bursts().empty()&&wrong_b.take_bursts().empty(),
            "wrong private roots or EOF manufactured completed reception");
    }
    static void common_raw_prefixes() {
        struct Case {unsigned factor,sample_rate;double bandwidth;std::uint64_t chip;bool drift;unsigned workers;};
        for(const auto mode:{Case{100,6000,8,15,false,1},Case{100,6000,8,15,true,2},
                             Case{1000,32000,16,4,false,1},Case{1000,32000,6.4,10,true,2}}) {
            auto c=configuration();c.dsss_factor=mode.factor;c.sample_rate=mode.sample_rate;c.carrier_hz=mode.sample_rate/4.;c.bandwidth_hz=mode.bandwidth;
            auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
            plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;plan->ramp_seconds=.01;c.scheduled_hopping=plan;
            const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
            const auto start=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
            c.stream_epoch=start.sample/c.sample_rate;c.stream_phase_samples=start.sample%c.sample_rate;
            check(pattern_chip_samples(c)==mode.chip,"common raw fixture lost intended fine-chip geometry");
            PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=0;search.worker_threads=mode.workers;
            search.drift_tolerant=mode.drift;search.differential_window_seconds=.01;search.bit_limit=4;search.chunk_bits=1;
            search.hypotheses={{0,0},{.13,0},{0,3},{.13,3}};
            PatternCorrelatorOptions original;original.scheduled_family_experiment=original.scheduled_common_shape_experiment=true;
            auto enabled=original;enabled.scheduled_temporal_template_experiment=true;
            PatternCorrelator a(c,search,16*1024*1024,original),b(c,search,16*1024*1024,enabled);
            check(a.work().hypotheses==4,"common raw fixture escaped complete four-lane family coverage");
            std::array<float,512> pcm{};
            for(std::size_t block=0;block<12;++block) {
                for(std::size_t n=0;n<pcm.size();++n)pcm[n]=static_cast<float>(.2*std::sin(.071*(block*512+n))+.11*std::cos(.317*(block*512+n)));
                a.push(pcm);b.push(pcm);states_equal(*a.impl_,*b.impl_);evidence_equal(a.candidates(),b.candidates());
                bursts_equal(a.take_bursts(),b.take_bursts());
            }
            const auto aw=a.work(),bw=b.work();
        check(aw.scheduled_temporal_attempts==0,"original reference unexpectedly executed Temporal");
            check(aw.scheduled_common_families>0&&aw.scheduled_common_raw_samples>0&&
                  aw.scheduled_common_families==bw.scheduled_common_families&&aw.scheduled_common_lanes==bw.scheduled_common_lanes&&
                  aw.scheduled_common_raw_samples==bw.scheduled_common_raw_samples&&aw.scheduled_common_shapes==bw.scheduled_common_shapes&&
                  aw.scheduled_common_raw_lane_samples==bw.scheduled_common_raw_lane_samples&&bw.scheduled_temporal_samples>0&&bw.scheduled_temporal_ready>0,
                  "common raw integration did not consume enabled leader outputs with identical coverage");
            check(mode.drift || a.impl_->outer_presence,"coherent common raw case omitted original outer ring");
            check(!mode.drift || aw.drift_sections==4,"common raw case omitted original quarter pieces");
            check(mode.workers==1 || b.impl_->worker_codes.size()>1,"common raw case omitted worker-owned tile binding");
            a.finish();b.finish();states_equal(*a.impl_,*b.impl_);bursts_equal(a.take_bursts(),b.take_bursts());
        }
    }

    static void phase_flat_prefix() {
        // Fixed keyed group and public midpoint, with no seed search. G9 gives
        // a whole UTC second strictly inside the same active group, so all five
        // canonical1200-sample phases retain their original hypotheses. This
        // covers ACTUAL phase-family certification rather than same-phase F.
        auto c=configuration();auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        const auto group=map.locate(first);check(group.active,"fixed flat fixture group is not active");
        const auto midpoint=group.physical_begin+map.source_samples_per_group()/2;
        c.stream_epoch=midpoint/c.sample_rate;c.stream_phase_samples=midpoint%c.sample_rate;
        check(canonical_phase_step(c)==1200 && pattern_chip_samples(c)==15 && symbol_sample_count(c)==96000,
              "bounded phase-flat fixture geometry changed");
        PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=0;search.search_stream_phases=true;
        search.worker_threads=1;search.drift_tolerant=false;search.differential_window_seconds=0;search.bit_limit=4;
        search.chunk_bits=1;search.candidate_limit=256;search.track_limit=32;search.retain_score=0;
        // Nonunit family preserves original fallback and makes flat Exporter
        // execution observable even when R1 takes every eligible nominal row.
        search.hypotheses={{0,0},{.13,0},{0,3},{.13,3}};
        PatternCorrelatorOptions original;original.scheduled_family_experiment=original.scheduled_common_shape_experiment=true;
        auto enabled=original;enabled.scheduled_temporal_template_experiment=true;
        PatternCorrelator a(c,search,16*1024*1024,original),b(c,search,16*1024*1024,enabled);
        check(a.work().hypotheses==20 && b.work().hypotheses==20,"flat fixture changed complete five-phase/four-pair bank");
        std::array<float,128> pcm{};
        for(std::size_t block=0;block<12;++block) {
            for(std::size_t n=0;n<pcm.size();++n)pcm[n]=static_cast<float>(.2*std::sin(.071*(block*128+n))+.11*std::cos(.317*(block*128+n)));
            a.push(pcm);b.push(pcm);states_equal(*a.impl_,*b.impl_);evidence_equal(a.candidates(),b.candidates());
            bursts_equal(a.take_bursts(),b.take_bursts());
        }
        check(a.work().scheduled_temporal_attempts==0,"phase-family original reference unexpectedly executed Temporal");
        check(a.work().scheduled_common_phase_families>0 && b.work().scheduled_common_phase_families>0 &&
              b.work().scheduled_temporal_ready>0 && b.work().scheduled_temporal_samples>0,
              "phase-flat fixture omitted successful original phase families or Temporal outputs");
        a.finish();b.finish();states_equal(*a.impl_,*b.impl_);bursts_equal(a.take_bursts(),b.take_bursts());
    }

    static void fine_curve_cached_borrow() {
        using Cache=State::ScheduledCurveCache;using Pool=detail::SharedSourceCurves<>;
        check(detail::fine_source_curve_revision()==2&&detail::fine_source_curve_cached_borrow_requested(),
              "cached borrow fixture requires explicit fine65 cached mode");
        struct Producer {
            std::function<std::array<Complex,2>(std::uint64_t,double)> make;
            auto shaped_values(std::uint64_t first,double at){return make(first,at);}
        };
        const auto values=[](std::uint64_t,double at){return std::array<Complex,2>{Complex{at,1},Complex{-at,2}};};
        Producer ordinary{values},never{[](std::uint64_t,double)->std::array<Complex,2>{throw Error("cached borrow rebuilt private row");}};
        detail::SourceFamily family;family.key_namespace=17;family.chip_samples=15;
        family.symbol_samples=96000;family.sample_rate=6000;
        auto cache=std::make_unique<Cache>();Pool pool(Pool::minimum_working_bytes());
        Cache::Key key{1,0,9,0};const auto before=detail::fine_curve_borrow_statistics().cached.load();
        auto a=cache->get(key,ordinary,0,15,{},&pool,&family),b=cache->get(key,never,0,15,{},&pool,&family);
        check(a.row&&a.row==b.row&&a.row->is_shared()&&a.row->shared&&!a.shared&&!b.shared&&
              pool.statistics().builds==1&&pool.statistics().hits==0&&
              detail::fine_curve_borrow_statistics().cached.load()==before+1,"local cached hit touched or copied L2");
        std::array<Complex,2> out;constexpr double at=135.46875;
        check(Cache::evaluate(a,at,15,out)&&out==values(0,at),"cached borrowed arithmetic changed");
        auto exhausted=cache->get({1,0,10,0},never,0,15,{},&pool,&family);
        check(!exhausted.row&&!exhausted.shared,"cached pinned exhaustion published a row");
        pool.retire();check(Cache::evaluate(b,at,15,out)&&out==values(0,at),"retirement invalidated a cached local lease");
        a={};b={};check(!pool.try_reclaim(),"cached row did not retain its L2 pin until joined cleanup");
        cache->clear_borrowed_rows();check(pool.try_reclaim(),"joined cleanup retained a cached L2 pin");
        for(auto& row:cache->rows)if(row.is_shared())check(!row.shared&&!row.valid&&!row.readers.load(),"cached cleanup left a live row");
        auto copied=cache->get({1,0,10,0},ordinary,0,15,{});auto* copied_row=copied.row;
        check(copied_row&&!copied_row->is_shared(),"shared to copied union transition failed");copied={};
        cache->clear_borrowed_rows();
        auto hit=cache->get({1,0,10,0},never,0,15,{});
        check(hit.row==copied_row&&hit.row->valid,"joined shared cleanup invalidated a copied row");hit={};
        auto dying=std::make_unique<Pool>(Pool::minimum_working_bytes());
        auto survivor=cache->get(key,ordinary,0,15,{},dying.get(),&family);dying.reset();
        check(survivor.row&&survivor.row->is_shared()&&Cache::evaluate(survivor,at,15,out)&&out==values(0,at),
              "owner destruction invalidated cached data");survivor={};cache->clear_borrowed_rows();
        Pool cancelled_pool(Pool::minimum_working_bytes());std::stop_source stop;
        Producer cancel_last{[&](std::uint64_t index,double point){if(point==150)stop.request_stop();return values(index,point);}};
        bool failed=false;try{cache->get(key,cancel_last,0,15,stop.get_token(),&cancelled_pool,&family);}catch(const Error&){failed=true;}
        check(failed&&cancelled_pool.statistics().failed_builds==1,"cached final cancellation published a row");
        for(const auto& row:cache->rows)check(row.readers.load()!=UINT_MAX,"cached cancellation leaked writer sentinel");
        auto retry=cache->get(key,ordinary,0,15,{},&cancelled_pool,&family);
        check(retry.row&&retry.row->is_shared()&&Cache::evaluate(retry,at,15,out)&&out==values(0,at),"cached cancellation retry failed");
        retry={};cache->clear_borrowed_rows();check(cancelled_pool.try_reclaim(),"cached retry pin leaked");
        Pool busy_pool(Pool::minimum_working_bytes());auto second_cache=std::make_unique<Cache>();
        std::promise<void> entered,release;auto gate=release.get_future().share();bool first_call=true;
        Producer blocked{[&](std::uint64_t index,double point){if(first_call){first_call=false;entered.set_value();gate.wait();}return values(index,point);}};
        auto writer=std::async(std::launch::async,[&]{return cache->get(key,blocked,0,15,{},&busy_pool,&family);});
        if(entered.get_future().wait_for(std::chrono::seconds(3))!=std::future_status::ready) {
            release.set_value();writer.wait();check(false,"cached writer never entered population");
        }
        auto duplicate=std::async(std::launch::async,[&]{return second_cache->get(key,never,0,15,{},&busy_pool,&family);});
        const bool ready=duplicate.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        release.set_value();auto complete=writer.get(),pending=duplicate.get();
        check(ready&&complete.row&&complete.row->is_shared()&&!pending.row&&busy_pool.statistics().busy==1,
              "cached duplicate waited, rebuilt or exposed an incomplete row");
        for(const auto& row:second_cache->rows)check(row.readers.load()!=UINT_MAX,"cached duplicate leaked a writer sentinel");
        auto busy_retry=second_cache->get(key,never,0,15,{},&busy_pool,&family);
        check(busy_retry.row&&busy_retry.row->is_shared()&&Cache::evaluate(busy_retry,at,15,out)&&out==values(0,at),
              "cached duplicate failed to retry after writer completion");
        busy_retry={};complete={};cache->clear_borrowed_rows();second_cache->clear_borrowed_rows();
        check(busy_pool.try_reclaim(),"cached joined duplicate leaked workspace");
        Cache::Key extreme{1,0,9,119999999};extreme.set_revision(2);
        check(extreme.phase()==119999999&&extreme.table_revision()==2,"local revision tags truncated maximum valid audio phase");
        {Cache::Row plain;auto tagged=extreme;tagged.gate_phase|=Cache::Key::shared_storage;plain.set_key(tagged);
            check(!plain.is_shared()&&plain.key.phase()==119999999&&plain.key.table_revision()==2,
                  "incoming storage tag changed the active union member");}
        auto maximum_family=family;maximum_family.sample_rate=120000000;maximum_family.coefficient_omission=true;
        Pool maximum_pool(Pool::minimum_working_bytes());
        auto maximum=cache->get({1,0,9,119999999},ordinary,0,15,{},&maximum_pool,&maximum_family);
        auto identity=detail::source_key(maximum_family,SymbolStreamAddress{1,0,119999999},9);identity.table_revision=2;
        auto exact=maximum_pool.get(identity,[](auto&){throw Error("local storage tags leaked into SourceKey");});
        check(maximum.row&&maximum.row->is_shared()&&exact&&maximum_pool.statistics().builds==1&&maximum_pool.statistics().hits==1,
              "cached SourceKey lost phase/revision or gained a local storage tag");
        maximum={};exact={};cache->clear_borrowed_rows();check(maximum_pool.try_reclaim(),"maximum-phase cached lease leaked");
        std::cout<<"original layout bytes: Key="<<sizeof(Cache::Key)<<" Row="<<sizeof(Cache::Row)
                 <<" Cache="<<sizeof(Cache)<<" Lease="<<sizeof(Cache::Lease)<<'\n';
    }
    static void fine_curve_shared_borrow() {
        using Cache=State::ScheduledCurveCache;
        using Pool=detail::SharedSourceCurves<>;
        check(detail::fine_source_curves_requested()&&detail::fine_source_curve_revision()==2&&
              detail::fine_source_curve_borrow_requested(),"borrow fixture requires explicit fine65 mode");
        struct Producer {
            std::function<std::array<Complex,2>(std::uint64_t,double)> make;
            auto shaped_values(std::uint64_t first,double at){return make(first,at);}
        };
        const auto values=[](std::uint64_t,double at){return std::array<Complex,2>{Complex{at,1},Complex{-at,2}};};
        Producer ordinary{values},never{[](std::uint64_t,double)->std::array<Complex,2>{throw Error("borrowed hit rebuilt private curve");}};
        auto cache=std::make_unique<Cache>();Pool pool(Pool::minimum_working_bytes());
        const auto direct_before=detail::fine_curve_borrow_statistics().direct.load();
        const auto fallback_before=detail::fine_curve_borrow_statistics().l2_fallbacks.load();
        check(pool.capacity()==1,"borrow lifetime test did not constrain capacity");
        detail::SourceFamily family;family.key_namespace=17;family.chip_samples=15;
        family.symbol_samples=96000;family.sample_rate=6000;
        Cache::Key key{1,0,9,0};
        auto first=cache->get(key,ordinary,0,15,{},&pool,&family);
        auto second=cache->get(key,never,0,15,{},&pool,&family);
        check(!first.row&&!second.row&&first.shared&&second.shared&&pool.statistics().builds==1&&
              pool.statistics().hits==1&&detail::fine_curve_borrow_statistics().direct.load()==direct_before+2,"fine borrowed row was copied or regenerated");
        for(const auto& row:cache->rows)check(!row.valid&&!row.readers.load(),"borrow path populated local cache");
        std::array<Complex,2> output;
        constexpr double at=135.46875;
        check(Cache::evaluate(first,at,15,output)&&output==values(0,at),"borrowed row changed source arithmetic");
        auto exhausted=cache->get({1,0,10,0},never,0,15,{},&pool,&family);
        check(!exhausted.row&&!exhausted.shared&&detail::fine_curve_borrow_statistics().l2_fallbacks.load()==fallback_before+1,
              "pinned exhaustion did not retain original direct fallback");
        check(!pool.try_reclaim(),"borrowed row was reclaimed while in use");
        auto moved=std::move(first);moved=std::move(moved);
        check(!first.shared&&Cache::evaluate(moved,at,15,output)&&output==values(0,at),"borrowed move lost its pin");
        auto local=cache->get({1,0,11,0},ordinary,0,15,{});
        auto* local_row=local.row;check(local_row&&!local.shared,"mixed replacement lacks local lease");
        local=std::move(second);
        check(!local_row->readers.load()&&local.shared&&!second.row&&!second.shared,"local to shared replacement leaked a pin");
        second=std::move(local);
        auto replacement=cache->get({1,0,11,0},never,0,15,{});
        moved=std::move(replacement);
        check(moved.row==local_row&&!moved.shared&&!replacement.row&&local_row->readers.load()==1,
              "shared to local replacement leaked an owner");
        pool.retire();
        auto retired=cache->get(key,never,0,15,{},&pool,&family);
        check(!retired.row&&!retired.shared&&Cache::evaluate(second,at,15,output)&&output==values(0,at),
              "retirement invalidated a live borrowed curve or admitted a new borrower");
        moved={};second={};check(pool.try_reclaim(),"released borrowed rows prevented joined reclamation");
        Pool retry_pool(Pool::minimum_working_bytes());std::stop_source stop;
        Producer cancel_last{[&](std::uint64_t index,double point){if(point==150)stop.request_stop();return values(index,point);}};
        bool cancelled=false;try{cache->get(key,cancel_last,0,15,stop.get_token(),&retry_pool,&family);}catch(const Error&){cancelled=true;}
        check(cancelled&&retry_pool.statistics().failed_builds==1,"cancelled borrowed builder published a row");
        auto retry=cache->get(key,ordinary,0,15,{},&retry_pool,&family);
        check(retry.shared&&Cache::evaluate(retry,at,15,output)&&output==values(0,at),"borrowed builder did not recover after cancellation");
        retry={};check(retry_pool.try_reclaim(),"retried borrowed lease leaked workspace");
        auto dying=std::make_unique<Pool>(Pool::minimum_working_bytes());
        auto survivor=cache->get(key,ordinary,0,15,{},dying.get(),&family);dying.reset();
        check(survivor.shared&&Cache::evaluate(survivor,at,15,output)&&output==values(0,at),
              "owner destruction invalidated a pinned borrowed row");survivor={};
        Pool busy_pool(Pool::minimum_working_bytes());
        std::promise<void> entered,release;auto gate=release.get_future().share();bool first_call=true;
        Producer blocked{[&](std::uint64_t index,double point){if(first_call){first_call=false;entered.set_value();gate.wait();}return values(index,point);}};
        auto writer=std::async(std::launch::async,[&]{return cache->get(key,blocked,0,15,{},&busy_pool,&family);});
        if(entered.get_future().wait_for(std::chrono::seconds(3))!=std::future_status::ready) {
            release.set_value();writer.wait();check(false,"borrowed writer never entered population");
        }
        auto duplicate=std::async(std::launch::async,[&]{return cache->get(key,never,0,15,{},&busy_pool,&family);});
        const bool ready=duplicate.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        release.set_value();auto completed=writer.get(),pending=duplicate.get();
        check(ready&&completed.shared&&!pending.shared&&!pending.row&&busy_pool.statistics().busy==1,
              "borrowed duplicate waited, rebuilt or exposed an incomplete row");
        completed={};check(busy_pool.try_reclaim(),"joined borrowed writer retained workspace");
    }
    static void fine_curve_writer_lifetime() {
        using Cache=State::ScheduledCurveCache;
        struct Producer {
            std::function<std::array<Complex,2>(std::uint64_t,double)> make;
            auto shaped_values(std::uint64_t first,double at){return make(first,at);}
        };
        auto cache=std::make_unique<Cache>();
        const auto values=[](std::uint64_t,double at){return std::array<Complex,2>{Complex{at,1},Complex{-at,2}};};
        std::promise<void> entered,release;auto gate=release.get_future().share();
        bool first=true;Producer blocked{[&](std::uint64_t index,double at) {
            if(first){first=false;entered.set_value();gate.wait();}return values(index,at);
        }};
        Producer ordinary{values},never{[](std::uint64_t,double)->std::array<Complex,2>{throw Error("busy/hit unexpectedly rebuilt a curve");}};
        const Cache::Key key{1,0,9,0},other{1,0,10,0};
        auto writer=std::async(std::launch::async,[&]{return cache->get(key,blocked,0,15,{});});
        if(entered.get_future().wait_for(std::chrono::seconds(3))!=std::future_status::ready) {
            release.set_value();writer.wait();check(false,"fine writer never entered reserved population");
        }
        auto duplicate=std::async(std::launch::async,[&]{return cache->get(key,never,0,15,{});});
        auto independent=std::async(std::launch::async,[&]{return cache->get(other,ordinary,0,15,{});});
        const bool duplicate_ready=duplicate.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        const bool independent_ready=independent.wait_for(std::chrono::seconds(3))==std::future_status::ready;
        release.set_value();auto result=writer.get();auto busy=duplicate.get();auto second=independent.get();
        check(duplicate_ready && independent_ready,"fine cache serialized unrelated work behind a blocked writer");
        check(!busy.row && result.row && second.row,"fine cache published an incomplete writer or lost an independent row");
        check(result.row->key==key && second.row->key==other,"fine writer publication changed private row identity");
        for(auto k:{0U,128U,256U}) {
            check(result.row->knots[k]==values(0,(9+k/256.)*15),"fine writer published partial knot data");
            check(second.row->knots[k]==values(0,(10+k/256.)*15),"independent fine writer aliased private data");
        }
        auto* retained=result.row;result={};second={};
        constexpr auto writing=std::numeric_limits<unsigned>::max();
        retained->readers.store(writing-1);auto overflow=cache->get(key,never,0,15,{});
        check(!overflow.row && retained->readers.load()==writing-1,"fine reader collided with writer sentinel");
        retained->readers.store(0);
        const auto assert_reclaimed=[&](Cache::Key failed) {
            bool found=false;
            for(auto& row:cache->rows) {
                check(row.readers.load()!=writing,"failed fine writer leaked a reservation");
                if(row.key==failed) {
                    found=true;check(!row.valid && row.readers.load()==0,"failed fine row became readable");
                    for(const auto& pair:row.knots)for(auto value:pair)check(value==Complex{},"failed fine private row was not cleansed");
                }
            }
            check(found,"failed fine reservation lost its test identity");
        };
        const Cache::Key thrown{1,0,11,0};
        Producer fail_last{[&](std::uint64_t index,double at) {
            if(at==180)throw Error("injected final knot failure");return values(index,at);
        }};
        bool failed=false;try{cache->get(thrown,fail_last,0,15,{});}catch(const Error&){failed=true;}
        check(failed,"fine final-knot exception did not propagate");assert_reclaimed(thrown);
        const Cache::Key cancelled_key{1,0,12,0};std::stop_source stop;
        Producer cancel_last{[&](std::uint64_t index,double at) {
            if(at==195)stop.request_stop();return values(index,at);
        }};
        failed=false;try{cache->get(cancelled_key,cancel_last,0,15,stop.get_token());}catch(const Error&){failed=true;}
        check(failed,"fine final-knot cancellation published a row");assert_reclaimed(cancelled_key);
        auto retry=cache->get(cancelled_key,ordinary,0,15,{});
        check(retry.row && retry.row->valid,"fine cache failed to recover after cancelled construction");retry={};
        for(const auto& row:cache->rows)check(row.readers.load()==0,"fine cache retained a reader after joined work");
        check(cache->exhaustions>=2 && cache->misses>=cache->exhaustions,"fine busy/overflow fallbacks were not counted as misses");
    }

    static bool same_outer_counts(detail::ScheduledOuterPublicCounts a,detail::ScheduledOuterPublicCounts b) {
        return a.pieces==b.pieces && a.equal_pieces==b.equal_pieces && a.samples==b.samples &&
            a.events==b.events && a.bound_fallbacks==b.bound_fallbacks;
    }
    static detail::ScheduledOuterPublicCounts outer_count_delta(detail::ScheduledOuterPublicCounts after,
                                                               detail::ScheduledOuterPublicCounts before) {
        check(after.pieces>=before.pieces && after.equal_pieces>=before.equal_pieces && after.samples>=before.samples &&
              after.events>=before.events && after.bound_fallbacks>=before.bound_fallbacks,"outer process counters moved backwards");
        return {after.pieces-before.pieces,after.equal_pieces-before.equal_pieces,after.samples-before.samples,
                after.events-before.events,after.bound_fallbacks-before.bound_fallbacks};
    }
    static void add_outer_counts(detail::ScheduledOuterPublicCounts& a,detail::ScheduledOuterPublicCounts b) {
        a.pieces+=b.pieces;a.equal_pieces+=b.equal_pieces;a.samples+=b.samples;a.events+=b.events;a.bound_fallbacks+=b.bound_fallbacks;
    }
    static void retain_original_outer_loan(PatternCorrelator& reference) {
        auto& s=*reference.impl_;
        check(!s.options.scheduled_temporal_template_experiment,"outer reference lost original Temporal opt-out");
        s.prepare_drift(128);s.prepare_workers();
        check(s.scheduled && s.scheduled->common_scratch && s.scheduled->common_raw_scratch &&
              s.scheduled->common_raw_scratch_bytes>=detail::scheduled_raw_single_frequency_scratch_bytes,
              "outer reference original same-frequency scratch unavailable");
        static_assert(detail::scheduled_raw_single_frequency_scratch_bytes<detail::scheduled_outer_public_replay_frame);
        // Test-bound additive loan, AFTER original workers/caches are chosen.
        // No configuration/arithmetic/detector/H field changes. Both processes
        // retain original768B helper; no surplus may fund the replay2048B frame.
        // Normal push preparation remains intact and must respect this loan.
        s.scheduled->common_raw_scratch_bytes=detail::scheduled_raw_single_frequency_scratch_bytes;
        s.accounted_bytes=s.working_bytes()+s.drift_reserved+s.amplitude_scratch_reservation;
        reference.set_workspace_bytes(sizeof(PatternCorrelator)+s.accounted_bytes);
    }
    static void outer_replay_prefix() {
        // Same predeclared keyed group/midpoint as phase_flat_prefix. Retain
        // ALL five canonical phases, one F/r pair, and explicitly group phases
        // in pairs: two exact2-lane same-F families plus one original singleton.
        auto c=configuration();auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=9;
        plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
        plan->ramp_seconds=.01;c.scheduled_hopping=plan;
        const SchedHopSourceMap map(*plan,*c.data_key,c.sample_rate);
        const auto first=map.source_to_physical(map.locate(8312ULL*c.sample_rate).source);
        const auto group=map.locate(first);check(group.active,"fixed outer replay group is not active");
        const auto midpoint=group.physical_begin+map.source_samples_per_group()/2;
        c.stream_epoch=midpoint/c.sample_rate;c.stream_phase_samples=midpoint%c.sample_rate;
        check(canonical_phase_step(c)==1200 && pattern_chip_samples(c)==15 && symbol_sample_count(c)==96000,
              "outer replay fixture lost exact five-phase fine-chip geometry");
        PatternSearch search;search.start_offset_seconds=0;search.start_uncertainty_seconds=0;search.search_stream_phases=true;
        search.worker_threads=1;search.drift_tolerant=false;search.differential_window_seconds=0;search.bit_limit=4;
        search.chunk_bits=1;search.candidate_limit=256;search.track_limit=32;search.retain_score=0;search.hypotheses={{0,0}};
        PatternCorrelatorOptions original;original.scheduled_family_experiment=original.scheduled_common_shape_experiment=true;
        original.scheduled_common_phase_limit=2;auto enabled=original;enabled.scheduled_temporal_template_experiment=true;
        PatternCorrelator a(c,search,16*1024*1024,original),b(c,search,16*1024*1024,enabled);
        check(a.work().hypotheses==5 && b.work().hypotheses==5,"outer replay fixture removed an original canonical phase");
        retain_original_outer_loan(a);b.impl_->prepare_drift(128);b.impl_->prepare_workers();
        const auto& s=*a.impl_;const auto workers=s.worker_codes.size(),caches=s.scheduled->worker_template_caches.size();
        const auto* first_cache=caches?s.scheduled->worker_template_caches.front().get():nullptr;
        const auto* families=s.scheduled->families.get();const auto* hypotheses=s.hypotheses.data();
        const auto bit_limit=s.bit_limit,code_bytes=s.code.working_bytes(),reference_budget=s.budget;
        check(((caches!=0)==s.scheduled->scheduled_cache_possible) && (!caches || first_cache) &&
              families && s.outer_presence && b.impl_->outer_presence &&
              s.config.search_arithmetic==c.search_arithmetic && b.impl_->config.search_arithmetic==c.search_arithmetic &&
              !s.scheduled_affine && !s.scheduled->moments_eligible && !s.scheduled->curves && !s.scheduled->float_curves &&
              !b.impl_->scheduled_affine && !b.impl_->scheduled->moments_eligible && !b.impl_->scheduled->curves && !b.impl_->scheduled->float_curves,
              "outer reference changed original arithmetic or omitted required original worker/cache/guard");
        std::size_t pairs=0,singletons=0;
        for(std::size_t family=1;family<families->ends.size();++family) {
            const auto begin=families->ends[family-1],end=families->ends[family];
            if(end-begin==1){++singletons;continue;}
            check(end-begin==2,"outer replay fixture did not form exact2-lane families");++pairs;
            const auto& x=s.hypotheses[families->members[begin]];const auto& y=s.hypotheses[families->members[begin+1]];
            check(x.frequency==y.frequency && x.phase_lower!=y.phase_lower,"outer replay fixture did not form same-F distinct-phase pair");
        }
        check(pairs==2 && singletons==1,"outer replay fixture changed complete five-H grouping");
        if(detail::scheduled_outer_public_replay_requested())
            check(b.impl_->scheduled->common_raw_scratch_bytes>=detail::scheduled_outer_public_replay_frame,
                  "requested candidate replay frame was not actually granted");
        detail::ScheduledOuterPublicCounts reference_counts{},candidate_counts{};
        std::array<float,128> pcm{};
        for(std::size_t block=0;block<12;++block) {
            for(std::size_t n=0;n<pcm.size();++n)pcm[n]=static_cast<float>(.2*std::sin(.071*(block*128+n))+.11*std::cos(.317*(block*128+n)));
            const auto before_reference=detail::scheduled_outer_public_counts();a.push(pcm);
            const auto after_reference=detail::scheduled_outer_public_counts();
            check(same_outer_counts(before_reference,after_reference),"independent original reference executed outer replay");
            add_outer_counts(reference_counts,outer_count_delta(after_reference,before_reference));
            check(s.scheduled->common_raw_scratch_bytes==detail::scheduled_raw_single_frequency_scratch_bytes &&
                  s.worker_codes.size()==workers && s.scheduled->worker_template_caches.size()==caches &&
                  (caches?s.scheduled->worker_template_caches.front().get():nullptr)==first_cache && s.scheduled->families.get()==families &&
                  s.hypotheses.data()==hypotheses && s.hypotheses.size()==5 && s.bit_limit==bit_limit && s.code.working_bytes()==code_bytes &&
                  s.budget==reference_budget && a.working_bytes()<=reference_budget && a.reserved_workspace_bytes()<=reference_budget,
                  "reference loan displaced original workers/caches/H/bit limit or granted replay");
            b.push(pcm);const auto after_candidate=detail::scheduled_outer_public_counts();
            add_outer_counts(candidate_counts,outer_count_delta(after_candidate,after_reference));
            states_equal(*a.impl_,*b.impl_);evidence_equal(a.candidates(),b.candidates());bursts_equal(a.take_bursts(),b.take_bursts());
        }
        check(a.work().scheduled_temporal_attempts==0 && b.work().scheduled_temporal_ready>0 && b.work().scheduled_temporal_samples>0,
              "outer replay fixture lost independent original or enabled exact Temporal outputs");
        check(a.work().scheduled_common_phase_families>0 && b.work().scheduled_common_phase_families>0 &&
              a.work().scheduled_common_raw_samples==b.work().scheduled_common_raw_samples &&
              a.work().scheduled_common_raw_lane_samples==b.work().scheduled_common_raw_lane_samples,
              "outer replay fixture changed original family/sample coverage");
        if(detail::scheduled_outer_public_replay_requested())
            check(candidate_counts.pieces>0 && candidate_counts.equal_pieces>0 && candidate_counts.samples>0 && candidate_counts.events>0 &&
                  candidate_counts.bound_fallbacks==0,"requested outer replay omitted real samples/flush events or only fell back");
        else check(same_outer_counts(candidate_counts,{}),"disabled candidate executed outer replay");
        const auto before_finish=detail::scheduled_outer_public_counts();a.finish();b.finish();
        check(same_outer_counts(before_finish,detail::scheduled_outer_public_counts()),"EOF fabricated outer replay samples/events");
        states_equal(*a.impl_,*b.impl_);bursts_equal(a.take_bursts(),b.take_bursts());
        // Counter deltas/loans are deliberately OUTSIDE the semantic digest.
        std::cout<<"temporal_outer_replay_fixture scope=process_counter_deltas requested="<<detail::scheduled_outer_public_replay_requested()
                 <<" H=5 paired_families="<<pairs<<" singleton_families="<<singletons
                 <<" reference_loan="<<s.scheduled->common_raw_scratch_bytes<<" candidate_loan="<<b.impl_->scheduled->common_raw_scratch_bytes
                 <<" reference_pieces="<<reference_counts.pieces<<" reference_equal_pieces="<<reference_counts.equal_pieces
                 <<" reference_samples="<<reference_counts.samples<<" reference_events="<<reference_counts.events
                 <<" reference_bound_fallbacks="<<reference_counts.bound_fallbacks<<" candidate_pieces="<<candidate_counts.pieces
                 <<" candidate_equal_pieces="<<candidate_counts.equal_pieces<<" candidate_samples="<<candidate_counts.samples
                 <<" candidate_events="<<candidate_counts.events<<" candidate_bound_fallbacks="<<candidate_counts.bound_fallbacks<<'\n';
    }
    static void report_outer_process_comparison(detail::ScheduledOuterPublicCounts before) {
        const auto delta=outer_count_delta(detail::scheduled_outer_public_counts(),before);
        if(detail::scheduled_outer_public_replay_requested())
            check(delta.pieces>0 && delta.equal_pieces>0 && delta.samples>0 && delta.events>0,
                  "requested outer process scope had no actual replay samples/flush events");
        else check(same_outer_counts(delta,{}),"disabled outer process scope changed counters");
        std::cout<<"temporal_outer_replay_execution scope=process_counter_deltas requested="<<detail::scheduled_outer_public_replay_requested()
                 <<" pieces="<<delta.pieces<<" equal_pieces="<<delta.equal_pieces<<" samples="<<delta.samples
                 <<" events="<<delta.events<<" bound_fallbacks="<<delta.bound_fallbacks<<'\n';
    }

    static void report_process_comparison(detail::ScheduledFlatExportCounts flat_before,detail::ScheduledR1ExportCounts r1_before) {
        const auto flat=detail::scheduled_flat_export_counts();const auto r1=detail::scheduled_r1_export_counts();
        if(detail::scheduled_flat_export_requested()) {
            check(flat.rows>flat_before.rows && flat.samples>flat_before.samples,
                  "requested flat Exporter had no certified execution; all-fallback is not coverage");
            check(flat.raw_pieces>flat_before.raw_pieces && flat.raw_samples>flat_before.raw_samples,
                  "requested flat commonRaw had no certified execution; all-fallback is not coverage");
        } else check(flat.rows==flat_before.rows && flat.samples==flat_before.samples &&
                     flat.raw_pieces==flat_before.raw_pieces && flat.raw_samples==flat_before.raw_samples,
                     "disabled flat path executed a shortcut");
        if(detail::scheduled_r1_export_requested())
            check(r1.attempts>r1_before.attempts && r1.rows>r1_before.rows && r1.samples>r1_before.samples,
                  "requested R1 Exporter had no certified execution; all-fallback is not coverage");
        else check(r1.attempts==r1_before.attempts && r1.rows==r1_before.rows && r1.samples==r1_before.samples,
                   "disabled R1 path executed a shortcut");
        const auto sha256=digest().finish();
        std::cout<<"temporal_semantic_digest schema=1 sha256="<<sha256<<" bytes="<<digest().bytes()
                 <<" state_boundaries="<<state_boundaries_<<" evidence_boundaries="<<evidence_boundaries_
                 <<" burst_boundaries="<<burst_boundaries_<<'\n';
        // Process cumulative diagnostics are deliberately OUTSIDE equivalence
        // digest: shortcut runs must differ here, and match semantic digest.
        std::cout<<"temporal_execution_counters scope=process_cumulative flat_requested="<<detail::scheduled_flat_export_requested()
                 <<" flat_rows="<<flat.rows<<" flat_samples="<<flat.samples<<" flat_raw_pieces="<<flat.raw_pieces
                 <<" flat_raw_samples="<<flat.raw_samples<<" r1_requested="<<detail::scheduled_r1_export_requested()
                 <<" r1_attempts="<<r1.attempts<<" r1_rows="<<r1.rows<<" r1_samples="<<r1.samples<<'\n';
    }

    static void run() {
        static_assert(sizeof(void*)!=8 || sizeof(PatternCorrelatorOptions)==32,"prototype grew ordinary Options beyond existing padding");
        const auto* temporal_override=std::getenv("DATAPUMP_RX_TEMPORAL_TEMPLATES");
        check(!temporal_override || std::string_view(temporal_override)!="1",
              "reference provenance requires DATAPUMP_RX_TEMPORAL_TEMPLATES unset or0");
        if(!Batch::supported()){
            check(!detail::scheduled_flat_export_requested() && !detail::scheduled_r1_export_requested(),
                  "requested shortcut coverage unavailable on this arithmetic platform");
            check(!detail::scheduled_outer_public_replay_requested(),"requested outer replay coverage unavailable on this arithmetic platform");
            std::cout<<"native ordered arithmetic unavailable; direct fallback retained\n";return;
        }
        check(!State::temporal_fp32_policy().requested,"semantic comparison requires exact FP64 outputs; unset DATAPUMP_SCHED_TEMPORAL_FP32");
        const auto flat_before=detail::scheduled_flat_export_counts();const auto r1_before=detail::scheduled_r1_export_counts();
        const auto outer_before=detail::scheduled_outer_public_counts();
        fine_curve_writer_lifetime();raw_oracles();sampled_prefix();complete_sampled_carry_absence();common_raw_prefixes();complete_sampled_carry_absence(true);
        phase_flat_prefix();outer_replay_prefix();report_outer_process_comparison(outer_before);
        report_process_comparison(flat_before,r1_before);
    }
};
}
int main(int argc,char** argv){try{if(argc==2&&std::string_view(argv[1])=="--fine-cached")datapump::modem::PatternCorrelatorTimingTestAccess::fine_curve_cached_borrow();else if(argc==2&&std::string_view(argv[1])=="--fine-borrow")datapump::modem::PatternCorrelatorTimingTestAccess::fine_curve_shared_borrow();else datapump::modem::PatternCorrelatorTimingTestAccess::run();std::cout<<"temporal template checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

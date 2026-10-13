#include "pattern_shared_source_curve.hpp"
#include "pattern_projection_cache.hpp"
#include "pattern_correlator_batch.hpp"
#include "datapump/pattern_correlator.hpp"
#include "datapump/crypto.hpp"
#include <bit>
#include <iostream>
#include <memory>
#include <limits>
#include <vector>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
static void check(bool x,const char* s){if(!x)throw Error(s);}
static bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
static bool bits(std::complex<double>a,std::complex<double>b){return bits(a.real(),b.real())&&bits(a.imag(),b.imag());}
static Config configuration(unsigned d,bool partial=false) {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=(d==1000?2:8);
    c.integration_seconds=std::nextafter((96000.+unsigned(partial))/6000,0.);
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;
    c.scramble=c.dsss=true;c.dsss_factor=d;c.outer_dsss_version=OuterDsssVersion::interleaved_v2;
    c.data_key.emplace(Bytes(32,0x67));c.stream_epoch=1799999967;
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
    auto plan=std::make_shared<SchedHopPlan>();plan->time_multiplier=10;plan->group_hops=2;
    plan->clock_error_seconds=plan->peer_clock_error_seconds=plan->region_seconds=plan->audio_error_seconds=0;
    plan->ramp_seconds=.01;c.scheduled_hopping=plan;return c;
}
static void eligibility() {
    auto c=configuration(100);
    check(pattern_chip_samples(c)==15&&symbol_sample_count(c)==96000,"fine target geometry changed");
    for(auto a:{SearchArithmetic::default_mode,SearchArithmetic::fp32}) {
        c.search_arithmetic=a;check(fine_source_curve_geometry(c,true)&&!fine_source_curve_geometry(c,false),"diagnostic gate mismatch");
    }
    c.search_arithmetic=SearchArithmetic::fp64;check(!fine_source_curve_geometry(c,true),"FP64force fine curves eligible");
    c=configuration(100);c.scramble=false;check(!fine_source_curve_geometry(c,true),"public pattern admitted by private diagnostic");
    c=configuration(100);c.pulse_shaping=false;check(!fine_source_curve_geometry(c,true),"unshaped curve eligible");
    c=configuration(100);c.scheduled_hopping.reset();check(!fine_source_curve_geometry(c,true),"ordinary source admitted");
    c=configuration(100);c.integration_seconds=std::nextafter(.1,0.);
    auto packed=std::make_shared<SchedHopPlan>(*c.scheduled_hopping);packed->ramp_seconds=.1;c.scheduled_hopping=packed;
    check(packed_scheduled_symbols(c),"packed-address veto fixture is vacuous");
    check(!fine_source_curve_geometry(c,true),"fine curve reused a split address for packed calendar slots");
    c=configuration(1);check(!fine_source_curve_geometry(c,true),"fine diagnostic took coarse gate");
    const auto x=135.5L+0x1p-50L;
    if(std::numeric_limits<long double>::digits>std::numeric_limits<double>::digits)
        check(x!=static_cast<long double>(static_cast<double>(x)),"coordinate oracle lost wider source input");
    check(source_curve_coordinate(x,15)==static_cast<long double>(static_cast<double>(x)),"fine source input differs from original cast");
    check(source_curve_coordinate(x,1024)==x,"legacy coarse coordinate changed");
    // Internal C1 stress, not a production geometry (validated chips are >=4).
    check(!source_curve_cell_eligible(.5L,0,1),"C1 half-phase incurred unusable257-knot prep");
}
struct Totals {std::uint64_t rows=0,points=0,interpolated=0,endpoint_fallbacks=0,tail_fallbacks=0;double source=0,limited=0,fit=0,score=0;} totals;
static void numerical_oracle() {
    using Pool=SharedSourceCurves<double,2>;
    for(unsigned d:{10U,100U,1000U})for(bool partial:{false,true}) {
        auto c=configuration(d,partial);PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
        const auto C=code.chip_samples(),N=code.symbol_samples(),final=code.chips_per_symbol()-1;
        check(fine_source_curve_geometry(c,true),"bounded oracle geometry not fine eligible");
        if(partial)check(N%C!=0,"partial-tail oracle did not construct a partial chip");
        const auto family=source_family(c,7);Pool pool(128*1024);
        for(std::uint64_t phase:{0ULL,5999ULL})for(std::uint64_t index:{0ULL,1ULL}) {
            code.set_stream_phase_samples(phase);reference.set_stream_phase_samples(phase);
            const auto first=index*code.chips_per_symbol();code.prepare_symbol(first);reference.prepare_symbol(first);
            for(const auto local:std::array<std::uint64_t,5>{0,9,final-9,final-8,final}) {
                const bool tail=N%C && final>=local && final-local<=8;
                const auto key=source_key_from_config(family,c,c.stream_epoch,phase,index,local);
                typename Pool::Lease lease;
                if(!tail)lease=pool.get(key,[&](auto& knots){for(std::size_t k=0;k<knots.size();++k)
                    knots[k]=code.shaped_values(first,static_cast<double>((static_cast<long double>(local)+k/256.L)*C));});
                ++totals.rows;std::array<CorrelationFit,2>a{},b{};
                const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(c);
                for(unsigned row=0;row<72;++row) {
                    // Knot neighbors, support cells, nonbinary fractions,
                    // one-past-chip, and sample-clock source coordinates.
                    long double fraction=(row%64)/64.L;
                    if(row==64)fraction=127.25L/256;
                    if(row==65)fraction=128.25L/256;
                    if(row==66)fraction=std::nextafter(.5L,0.L);
                    if(row==67)fraction=std::nextafter(.5L,1.L);
                    if(row==68)fraction=1;
                    if(row==69)fraction=-0x1p-20L;
                    if(row>=70)fraction=(.123456789L+(row-70)*.999998L)/C;
                    const auto at=(static_cast<long double>(local)+fraction)*C;
                    const auto coordinate=source_curve_coordinate(at,C);
                    auto exact=reference.shaped_values(first,static_cast<double>(at)),value=exact;
                    const bool interpolated=lease && evaluate_source_curve(lease.knots(),local,coordinate,C,value);
                    if(interpolated)++totals.interpolated;else if(tail)++totals.tail_fallbacks;else ++totals.endpoint_fallbacks;
                    ++totals.points;
                    const auto carrier=std::polar(1.,.317*row);
                    const double pcm=static_cast<float>(.3*std::sin(.271*row));
                    const CorrelationProjection p{pcm*carrier.real(),pcm*carrier.imag(),carrier.real()*carrier.real(),
                        carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),pcm*pcm};
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto error=std::abs(value[bit]-exact[bit]);totals.source=std::max(totals.source,error);
                        check(error<=1e-10*std::max(1.,std::abs(exact[bit])),"fine source interpolation exceeded bounded oracle tolerance");
                        if(!interpolated)check(bits(value[bit],exact[bit]),"original direct fallback changed bits");
                        const auto drive=(row%3==0?3.:1.)*amplitude;
                        const auto v=.73*pattern_limit_pcm(drive*value[bit])/drive;
                        const auto e=.73*pattern_limit_pcm(drive*exact[bit])/drive;
                        totals.limited=std::max(totals.limited,std::abs(v-e));
                        check(std::abs(v-e)<=1e-10,"postlimiter fine curve error exceeded tolerance");
                        if(row%11){a[bit].add(p,v,1);b[bit].add(p,e,1);}
                    }
                }
                for(unsigned bit=0;bit<2;++bit) {
                    check(a[bit].count==b[bit].count&&bits(a[bit].energy,b[bit].energy),"fine source reuse changed PCM energy/count");
                    const std::array<double,5>x{a[bit].xc,a[bit].xs,a[bit].cc,a[bit].ss,a[bit].cs},y{b[bit].xc,b[bit].xs,b[bit].cc,b[bit].ss,b[bit].cs};
                    for(std::size_t j=0;j<x.size();++j){const auto delta=std::abs(x[j]-y[j]);totals.fit=std::max(totals.fit,delta);
                        check(delta<=1e-8*std::max(1.,std::abs(y[j])),"fine wider Fit exceeded bounded oracle tolerance");}
                    const auto score=a[bit].score(),original=b[bit].score();check(std::isfinite(score)&&std::isfinite(original),"fine wider score nonfinite");
                    totals.score=std::max(totals.score,std::abs(score-original));
                    check(std::abs(score-original)<=1e-7*std::max(1.,std::abs(original)),"fine score exceeded bounded oracle tolerance");
                }
            }
        }
    }
    check(totals.interpolated&&totals.endpoint_fallbacks&&totals.tail_fallbacks,"bounded oracle omitted an intended branch");
}
static void identity_oracle() {
    using Pool=SharedSourceCurves<double,2>;auto c=configuration(100);
    const auto family=source_family(c,17);const auto key=source_key_from_config(family,c,c.stream_epoch,0,0,9);
    Pool pool(128*1024);auto first=pool.get(key,[](auto& knots){for(auto& pair:knots)for(auto& v:pair)v={1,2};});
    check(bool(first),"identity oracle row unavailable");
    for(unsigned field=0;field<8;++field) {
        auto wrong=key;
        switch(field) {
            case 0:++wrong.family.key_namespace;break;
            case 1:wrong.family.pattern_root[0]^=1;break;
            case 2:++wrong.epoch;break;
            case 3:++wrong.ordinal;break;
            case 4:++wrong.local_chip;break;
            case 5:++wrong.family.symbol_samples;break;
            case 6:++wrong.family.dsss_factor;break;
            case 7:++wrong.family.chip_samples;break;
        }
        const auto before=pool.statistics().builds;
        auto other=pool.get(wrong,[](auto& knots){for(auto& pair:knots)for(auto& v:pair)v={9,4};});
        check(bool(other)&&pool.statistics().builds==before+1&&other.knots()[0][0]==std::complex<double>(9,4),
              "wrong private identity reused fine source row");
        check(first.knots()[0][0]==std::complex<double>(1,2),"pinned correct private curve changed");
    }
    const auto before=pool.statistics().builds;auto duplicate=pool.get(key,[](auto&){throw Error("same fine identity rebuilt");});
    check(bool(duplicate)&&pool.statistics().builds==before,"exact identity missed");
    auto wrong=key;wrong.operand_bits=32;bool rejected=false;
    try{pool.get(wrong,[](auto&){});}catch(const Error&){rejected=true;}
    check(rejected,"wrong operand width accepted");
}
// Separate approximation experiment. Keep every strict 257-knot assertion
// above: a coarse table must not silently relax the reference oracle.
static void sparse_revision_oracle() {
    using Pool=SharedSourceCurves<double,2>;
    Pool::Knots polynomial;
    const auto nan=std::numeric_limits<double>::quiet_NaN();
    for(auto& pair:polynomial)for(auto& value:pair)value={nan,nan};
    for(unsigned k=0;k<=256;k+=4)for(auto& value:polynomial[k])value={double(k*k),0};
    std::array<std::complex<double>,2> out;
    check(evaluate_source_curve(polynomial,0,3./256,1,out,2)&&out[0]==std::complex<double>(12,0),
          "65-node evaluation read an unpopulated knot or used the wrong spacing");
    for(long double k:{124.L,125.L,126.L,127.L,128.L,129.L,130.L,131.L})
        check(!source_curve_cell_eligible(k/256,0,1,2)&&
              !evaluate_source_curve(polynomial,0,k/256,1,out,2),"65-node support-change cell admitted");
    check(source_curve_cell_eligible(123.75L/256,0,1,2)&&
          source_curve_cell_eligible(132.L/256,0,1,2),"65-node exclusions expanded beyond their cells");
    bool rejected=false;try{evaluate_source_curve(polynomial,0,.1L,1,out,3);}catch(const Error&){rejected=true;}
    check(rejected,"unknown curve revision accepted");
    auto c=configuration(100);auto key=source_key_from_config(source_family(c,19),c,c.stream_epoch,0,0,9);
    Pool identity(128*1024);
    auto original=identity.get(key,[](auto& knots){for(auto& pair:knots)for(auto& v:pair)v={1,2};});
    auto sparse=key;sparse.table_revision=2;
    auto different=identity.get(sparse,[&](auto& knots){knots=polynomial;});
    check(bool(original)&&bool(different)&&identity.statistics().builds==2,"curve revisions shared an identity");
    check(Pool::evaluate(different,(9+3.L/256)*15,out)&&out[0]==std::complex<double>(12,0),
          "leased sparse row did not retain its revision");
    check(original.knots()[0][0]==std::complex<double>(1,2),"sparse revision replaced a pinned original row");
    double max_source=0,max_limited=0,max_fit=0,max_score=0,max_loss=0;
    std::uint64_t points=0,fallbacks=0;
    for(unsigned d:{10U,100U,1000U})for(bool partial:{false,true}) {
        c=configuration(d,partial);PatternCode code(c,c.stream_epoch),reference(c,c.stream_epoch);
        const auto C=code.chip_samples(),N=code.symbol_samples(),final=code.chips_per_symbol()-1;
        for(std::uint64_t phase:{0ULL,5999ULL})for(std::uint64_t index:{0ULL,1ULL}) {
            code.set_stream_phase_samples(phase);reference.set_stream_phase_samples(phase);
            const auto first=index*code.chips_per_symbol();code.prepare_symbol(first);reference.prepare_symbol(first);
            for(auto local:std::array<std::uint64_t,5>{0,9,final-9,final-8,final}) {
                const bool tail=N%C&&final-local<=8;
                Pool::Knots knots{};
                for(unsigned k=0;k<=256;k+=4)
                    knots[k]=code.shaped_values(first,static_cast<double>((local+k/256.L)*C));
                std::array<CorrelationFit,2>a{},b{};
                std::array<long double,2> aa{},bb{},cross{};
                const auto amplitude=std::sqrt(2*nominal_signal_power)*outer_dsss_transmit_gain(c);
                for(unsigned i=0;i<512;++i) {
                    // Exact omitted fine knots include the maximum chord error
                    // between coarse knots; the second half probes off-grid.
                    const auto fraction=i<256?i/256.L:(i-256+.371L)/256;
                    const auto at=source_curve_coordinate((local+fraction)*C,C);
                    auto exact=reference.shaped_values(first,static_cast<double>(at)),value=exact;
                    const bool used=!tail&&evaluate_source_curve(knots,local,at,C,value,2);
                    if(!used)++fallbacks;++points;
                    const auto carrier=std::polar(1.,.317*i);
                    const auto pcm=static_cast<float>(.3*std::sin(.271*i));
                    const CorrelationProjection p{pcm*carrier.real(),pcm*carrier.imag(),carrier.real()*carrier.real(),
                        carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),pcm*pcm};
                    for(unsigned bit=0;bit<2;++bit) {
                        max_source=std::max(max_source,std::abs(value[bit]-exact[bit]));
                        check(std::abs(value[bit]-exact[bit])<.001,"65-node source error exceeded experimental bound");
                        if(!used)check(bits(value[bit],exact[bit]),"65-node direct fallback changed");
                        const auto drive=(i%3==0?3.:1.)*amplitude;
                        const auto v=.73*pattern_limit_pcm(drive*value[bit])/drive;
                        const auto e=.73*pattern_limit_pcm(drive*exact[bit])/drive;
                        max_limited=std::max(max_limited,std::abs(v-e));
                        aa[bit]+=std::norm(v);bb[bit]+=std::norm(e);cross[bit]+=(v*std::conj(e)).real();
                        a[bit].add(p,v,1);b[bit].add(p,e,1);
                    }
                }
                for(unsigned bit=0;bit<2;++bit) {
                    check(a[bit].count==b[bit].count&&bits(a[bit].energy,b[bit].energy),"65-node changed observed noise energy/count");
                    const std::array<double,5>x{a[bit].xc,a[bit].xs,a[bit].cc,a[bit].ss,a[bit].cs},y{b[bit].xc,b[bit].xs,b[bit].cc,b[bit].ss,b[bit].cs};
                    for(unsigned j=0;j<x.size();++j)max_fit=std::max(max_fit,std::abs(x[j]-y[j]));
                    const auto score=a[bit].score(),old=b[bit].score();
                    check(std::isfinite(score)&&std::isfinite(old),"65-node nonfinite score");
                    max_score=std::max(max_score,std::abs(score-old));
                    if(aa[bit]>0&&bb[bit]>0&&cross[bit]>0)max_loss=std::max(max_loss,
                        static_cast<double>(-10*std::log10(std::min(1.L,cross[bit]*cross[bit]/(aa[bit]*bb[bit])))));
                }
            }
        }
    }
    check(points&&fallbacks&&fallbacks<points,"65-node oracle branch coverage missing");
    std::cout<<"65-node separate bounded oracle: points="<<points<<" fallbacks="<<fallbacks
             <<" max_source_error="<<max_source<<" max_postlimiter_error="<<max_limited
             <<" max_Fit_error="<<max_fit<<" max_score_error="<<max_score
             <<" sampled_template_angle_loss_db="<<max_loss<<" (not full-bank sensitivity)\n";
}
static void correlator_binding() {
    check(fine_source_curves_requested(),"run this diagnostic test with DATAPUMP_SCHED_FINE_CURVES=1");
    auto c=configuration(100);const SchedHopSourceMap map(*c.scheduled_hopping,*c.data_key,c.sample_rate);
    const auto physical=map.source_to_physical({100*map.source_samples_per_group(),0});
    c.stream_epoch=physical.sample/c.sample_rate;c.stream_phase_samples=physical.sample%c.sample_rate;
    PatternSearch search;search.start_offset_seconds=search.start_uncertainty_seconds=0;
    search.frequency_offsets_hz={0};search.clock_errors_ppm={0};search.drift_tolerant=false;search.worker_threads=1;
    PatternCorrelatorOptions ordinary;ordinary.scheduled_common_shape_experiment=false;
    PatternCorrelator a(c,search,16*1024*1024,ordinary),b(c,search,16*1024*1024,ordinary);
    std::vector<float> input(96);for(std::size_t i=0;i<input.size();++i)input[i]=static_cast<float>(.01*std::sin(.23*i));
    SharedSourceCurves<> pool(256*1024);const auto family=source_family(c,17);
    CorrelationProjectionCache context(input,256*1024);context.bind_source_curves(&pool,&family);
    a.push(input,{},&context);const auto built=pool.statistics().builds;
    check(built>0,"actual fine correlator did not populate shared source rows");
    b.push(input,{},&context);
    check(pool.statistics().hits>0&&pool.statistics().builds==built,"second fine parent rebuilt small shared working set");
    const auto pending=a.provisional(),second=b.provisional();
    check(pending.bits==second.bits&&pending.complete==second.complete,"shared fine reuse changed pending/physical completion");
    for(unsigned bypass=0;bypass<3;++bypass) {
        auto reference_config=c;auto options=ordinary;
        if(bypass==0)reference_config.search_arithmetic=SearchArithmetic::fp64;
        if(bypass==1)options.raw_reference=true;
        if(bypass==2)options.scheduled_moment_reference=true;
        PatternCorrelator reference(reference_config,search,16*1024*1024,options);
        const auto before=pool.statistics();reference.push(input,{},&context);const auto after=pool.statistics();
        check(after.hits==before.hits&&after.builds==before.builds,"original-template diagnostic consumed fine curves");
        const auto original=reference.provisional();
        check(original.bits==pending.bits&&original.complete==pending.complete,"fine partial fixture changed bits/completion");
    }
    context.clear_source_curves();check(!context.source_curves()&&!context.source_family(),"borrowed family survived clear");
    check(pool.try_reclaim(),"synchronous pushes retained shared source pins after joining");
    const auto before=a.reserved_workspace_bytes();a.trim_optional_workspace();
    check(a.reserved_workspace_bytes()+72*1024<=before,"fine curve/callee reservation was missing or not released");
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--disabled-policy") {
            check(!fine_source_curves_requested()&&fine_source_curve_revision()==1,
                  "65-node diagnostic enabled without fine-curve permission");
            return 0;
        }
        eligibility();numerical_oracle();identity_oracle();sparse_revision_oracle();correlator_binding();
        std::cout<<"fine FP64 source-curve bounded diagnostic passed; rows="<<totals.rows<<" points="<<totals.points
                 <<" interpolated="<<totals.interpolated<<" endpoint_fallbacks="<<totals.endpoint_fallbacks
                 <<" tail_fallbacks="<<totals.tail_fallbacks<<" max_source_error="<<totals.source
                 <<" max_postlimiter_error="<<totals.limited<<" max_Fit_error="<<totals.fit<<" max_score_error="<<totals.score<<'\n';
    } catch(const std::exception& e){std::cerr<<"fine source-curve diagnostic failed: "<<e.what()<<'\n';return 1;}
}

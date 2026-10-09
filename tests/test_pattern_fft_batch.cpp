#include "../src/pattern_fft_batch.hpp"
#include "../src/pattern_drift.hpp"
#include "../src/search_parallel.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <numeric>
#include <numbers>
#include <random>

using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
namespace {
void check(bool condition,const char* message) { if(!condition)throw Error(message); }
bool equal(FftSearchScore a,FftSearchScore b) { return a.zero==b.zero && a.one==b.one; }
constexpr FftSearchScore untouched{-19,-23};
struct Fixture {
    Config config;
    FftSearchBatch batch;
    std::vector<FftComplex> spectrum,carrier;
    std::vector<double> energy;
    std::vector<FftSearchJob> jobs;
    Fixture() :spectrum(64),carrier(64),energy(65),jobs(16387) {
        config.scramble=true;config.pulse_shaping=false;config.stream_epoch=1789312671;
        for(std::size_t i=0;i<config.spreading_seed.size();++i)
            config.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);
        PatternCode code(config,config.stream_epoch);
        auto& geometry=batch.geometry;
        geometry.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),0,1,0,
            config.spreading_seed,config.dsss_seed};
        geometry.bins_per_symbol=16;geometry.bin_samples=3;geometry.carrier_hz=config.carrier_hz;
        geometry.evidence_count=16;geometry.noise_condition=1;
        for(std::size_t i=0;i<spectrum.size();++i) {
            spectrum[i]={std::sin(static_cast<double>(i)*.19),std::cos(static_cast<double>(i)*.13)};
            energy[i+1]=energy[i]+std::norm(spectrum[i]);carrier[i]={1,0};
        }
        pattern_fft(spectrum,false,{});
        batch.spectrum=spectrum;batch.carrier_square=carrier;batch.energy_prefix=energy;
        batch.starts=23;batch.score_stride=26;
        for(std::size_t i=0;i<jobs.size();++i)
            jobs[i]={i%64,i%7,i%5,static_cast<double>(i%5)-2};
    }
    std::vector<FftSearchWorkspace> workspaces(std::size_t count) const {
        std::vector<FftSearchWorkspace> result;
        for(std::size_t i=0;i<count;++i)result.emplace_back(config,spectrum.size(),true);
        return result;
    }
};
void flat_batch_tiling_and_order() {
    Fixture fixture;
    const auto stride=fixture.batch.score_stride;
    std::vector<FftSearchScore> expected(fixture.jobs.size()*stride,untouched);
    auto serial=fixture.workspaces(1);
    execute_fft_search_cpu(fixture.batch,fixture.jobs,expected,serial);
    for(const auto tile:{std::size_t{7},std::size_t{128},std::size_t{16387}}) {
        auto workers=fixture.workspaces(std::min<std::size_t>(3,search_concurrency(3)));
        std::vector<FftSearchScore> actual(expected.size(),untouched);
        for(std::size_t offset=0;offset<fixture.jobs.size();offset+=tile) {
            const auto count=std::min(tile,fixture.jobs.size()-offset);
            execute_fft_search_cpu(fixture.batch,std::span(fixture.jobs).subspan(offset,count),
                std::span(actual).subspan(offset*stride,count*stride),workers);
        }
        check(std::equal(expected.begin(),expected.end(),actual.begin(),equal),
              "FFT batch tile or worker count changed an exact score or padding");
    }
    std::reverse(fixture.jobs.begin(),fixture.jobs.end());
    auto workers=fixture.workspaces(std::min<std::size_t>(3,search_concurrency(3)));
    std::vector<FftSearchScore> reversed(expected.size(),untouched);
    execute_fft_search_cpu(fixture.batch,fixture.jobs,reversed,workers);
    for(std::size_t job=0;job<fixture.jobs.size();++job)
        for(std::size_t start=0;start<stride;++start)
            check(equal(reversed[job*stride+start],expected[(fixture.jobs.size()-1-job)*stride+start]),
                  "FFT backend attached a result to execution order instead of its job index");
}
void prepared_template_and_cancellation() {
    Fixture fixture;
    auto& geometry=fixture.batch.geometry;
    geometry.bin_samples=1;geometry.pattern.scramble=0;
    fixture.config.scramble=false;
    PatternCode code(fixture.config,fixture.config.stream_epoch);
    std::array<std::vector<FftComplex>,2> rows;
    FftPreparedTemplate prepared;
    for(unsigned bit=0;bit<2;++bit) {
        rows[bit].resize(fixture.spectrum.size());
        for(std::size_t bin=0;bin<geometry.bins_per_symbol;++bin) {
            const auto position=static_cast<long double>(bin)/code.chip_samples();
            const auto chip=static_cast<std::uint64_t>(position);
            const auto value=code.value(chip,bit,static_cast<double>(position-chip));
            rows[bit][geometry.bins_per_symbol-1-bin]=std::conj(value);
            prepared.energy[bit]+=std::norm(value);
        }
        pattern_fft(rows[bit],false,{});prepared.rows[bit]=rows[bit];
    }
    fixture.jobs.resize(1);fixture.jobs[0]={0,0,0,0};
    auto workers=fixture.workspaces(1);
    std::vector<FftSearchScore> generated(fixture.batch.score_stride,untouched),cached(generated);
    execute_fft_search_cpu(fixture.batch,fixture.jobs,generated,workers);
    fixture.batch.prepared=std::span(&prepared,1);fixture.jobs[0].prepared_template=0;
    execute_fft_search_cpu(fixture.batch,fixture.jobs,cached,workers);
    check(std::equal(generated.begin(),generated.end(),cached.begin(),equal),
          "FFT uploaded-template view changed exact generated-template scores");
    const FftStartRange selected{7,1};fixture.batch.start_ranges=std::span(&selected,1);
    fixture.jobs[0].range_count=1;std::fill(cached.begin(),cached.end(),untouched);
    std::vector<FftSearchWorkspace> prepared_only;
    prepared_only.emplace_back(fixture.config,fixture.spectrum.size(),false);
    execute_fft_search_cpu(fixture.batch,fixture.jobs,cached,prepared_only);
    for(std::size_t j=0;j<cached.size();++j)check(equal(cached[j],j==7?generated[j]:untouched),
        "restricted prepared-only FFT row changed a selected score or wrote an excluded cell");
    std::stop_source stopped;stopped.request_stop();
    bool cancelled=false;
    try {execute_fft_search_cpu(fixture.batch,fixture.jobs,cached,workers,stopped.get_token());}
    catch(const Error&) {cancelled=true;}
    check(cancelled,"FFT batch ignored cancellation");
    fixture.batch.score_stride=fixture.batch.starts-1;
    bool rejected=false;
    try {execute_fft_search_cpu(fixture.batch,fixture.jobs,cached,workers);}
    catch(const Error&) {rejected=true;}
    check(rejected,"FFT batch accepted overlapping output slices");
    fixture.batch.score_stride=26;fixture.batch.spectrum=std::span(fixture.spectrum).first(63);
    rejected=false;
    try {execute_fft_search_cpu(fixture.batch,fixture.jobs,cached,workers);}
    catch(const Error&) {rejected=true;}
    check(rejected,"FFT batch accepted a non-power-of-two transform");
    fixture.batch.spectrum=fixture.spectrum;fixture.jobs[0].prepared_template=std::numeric_limits<std::uint64_t>::max();
    geometry.pattern.chip_samples=0;rejected=false;
    try {execute_fft_search_cpu(fixture.batch,fixture.jobs,cached,workers);}
    catch(const Error&) {rejected=true;}
    check(rejected,"FFT batch accepted a zero private-template chip size");
}
void restricted_coherent_direct_and_fft() {
    for(const bool shaped:{false,true})for(const auto bin:{std::size_t{1},std::size_t{4}}) {
        Config config;config.sample_rate=64;config.bandwidth_hz=8;config.carrier_hz=15.3;
        config.integration_seconds=257./64;config.pulse_shaping=shaped;config.scramble=true;
        for(std::size_t i=0;i<config.spreading_seed.size();++i)config.spreading_seed[i]=static_cast<std::uint8_t>(7+11*i);
        PatternCode code(config);FftSearchBatch batch;auto& g=batch.geometry;
        g.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),shaped?1U:0U,1,0,
            config.spreading_seed,config.dsss_seed};
        g.bins_per_symbol=(code.symbol_samples()+bin-1)/bin+3;g.bin_samples=bin;g.carrier_hz=config.carrier_hz;
        g.sample_fit=bin==1;g.real_rank=bin==1;g.extended_clock_window=1;
        g.evidence_count=g.sample_fit?std::min<double>(g.bins_per_symbol,4.*g.bins_per_symbol/code.chip_samples()):g.bins_per_symbol;
        const auto image=std::abs(pattern_projection_image_ratio(bin,config.carrier_hz,config.sample_rate));
        g.noise_condition=bin==1?1:(1+image)/(1-image);
        batch.starts=9;batch.score_stride=12;batch.first_bin=37;
        std::size_t transform=1;while(transform<g.bins_per_symbol+batch.starts-1)transform*=2;
        std::vector<FftComplex> observations(transform),spectrum,carrier(transform);
        std::vector<double> energy(transform+1);
        for(std::size_t i=0;i<transform;++i) {
            const auto phase=2*std::numbers::pi*config.carrier_hz*
                ((batch.first_bin+i)*bin+(bin-1)/2.)/config.sample_rate;
            observations[i]=bin==1?std::sin(.19*i+.3)*std::polar(1.,-phase):
                FftComplex{std::sin(.19*i),std::cos(.13*i)};
            carrier[i]=std::polar(1.,2*phase);energy[i+1]=energy[i]+std::norm(observations[i]);
        }
        spectrum=observations;pattern_fft(spectrum,false,{});
        batch.spectrum=spectrum;batch.carrier_square=carrier;batch.energy_prefix=energy;
        std::array<FftSearchJob,2> jobs{};
        jobs[0].symbol=3;jobs[0].phase=7;jobs[0].frequency_hz=.03125;jobs[0].clock_ratio=.9998;
        jobs[1]=jobs[0];jobs[1].symbol=1;jobs[1].clock_ratio=1.0002;
        std::vector<FftSearchWorkspace> workers;workers.emplace_back(config,transform,true);workers.emplace_back(config,transform,true);
        std::vector<FftSearchScore> baseline(jobs.size()*batch.score_stride,untouched);
        execute_fft_search_cpu(batch,jobs,baseline,workers);
        const std::array<FftStartRange,3> ranges{{{1,1},{7,2},{3,1}}};
        batch.start_ranges=ranges;batch.observations=observations;
        for(const auto selected:{std::size_t{0},std::size_t{1},std::size_t{3},std::size_t{4},std::size_t{5}}) {
            std::array<FftStartRange,1> contiguous{{{2,selected}}};
            if(selected==3) {batch.start_ranges=ranges;jobs[0].range_begin=0;jobs[0].range_count=2;
                jobs[1].range_begin=2;jobs[1].range_count=1;}
            else {batch.start_ranges=contiguous;for(auto& job:jobs){job.range_begin=0;job.range_count=selected?1:0;}}
            std::vector<FftSearchScore> actual(baseline.size(),untouched);
            execute_fft_search_cpu(batch,jobs,actual,workers);
            for(std::size_t job=0;job<jobs.size();++job)for(std::size_t start=0;start<batch.score_stride;++start) {
                const auto kept=selected==3?(job==0?(start==1||start==7||start==8):start==3):
                    start>=2&&start<2+selected;
                const auto a=actual[job*batch.score_stride+start],b=baseline[job*batch.score_stride+start];
                if(!kept)check(equal(a,untouched),"restricted coherent work overwrote an excluded start or job padding");
                else check(std::abs(a.zero-b.zero)<=1e-8*std::max({1.,a.zero,b.zero}) &&
                    std::abs(a.one-b.one)<=1e-8*std::max({1.,a.one,b.one}),
                    "restricted direct/FFT score changed template energy, clock zeros or original-start covariance");
            }
            check(pattern_fft_direct_eligible(g,true,selected,true)==(bin==4&&selected>0&&selected<=32),
                "pure direct work selector diverged from bounded coherent eligibility");
            check(pattern_fft_direct_eligible(g,true,32,true)==(bin==4)&&
                !pattern_fft_direct_eligible(g,true,33,true),"direct crossover lost its measured bound");
        }
        const auto rejects=[&](FftStartRange range) {
            batch.start_ranges=std::span(&range,1);jobs[0].range_begin=0;jobs[0].range_count=1;
            jobs[1].range_begin=0;jobs[1].range_count=0;std::vector<FftSearchScore> result(baseline.size(),untouched);
            try {execute_fft_search_cpu(batch,jobs,result,workers);}catch(const Error&){return true;}return false;
        };
        check(rejects({8,2})&&rejects({1,std::numeric_limits<std::size_t>::max()})&&rejects({1,0}),
            "restricted FFT accepted an invalid or overflowing original-start range");
        const FftStartRange first_start{0,1};batch.start_ranges=std::span(&first_start,1);
        jobs[0].range_begin=0;jobs[0].range_count=1;jobs[1].range_begin=0;jobs[1].range_count=0;
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try {execute_fft_search_cpu(batch,jobs,baseline,workers,stop.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled,"nonempty restricted job ignored cancellation");
        execute_fft_search_cpu(batch,jobs,baseline,workers);
        const std::array<FftStartRange,2> overlap{{{1,2},{2,1}}};batch.start_ranges=overlap;jobs[0].range_count=2;
        bool rejected=false;
        try {execute_fft_search_cpu(batch,jobs,baseline,workers);}catch(const Error&){rejected=true;}
        check(rejected,"restricted FFT accepted overlapping selected ranges");
    }
}
void public_nominal_reference_exactness() {
    for(const bool shaped:{false,true})for(const auto bin:{std::size_t{1},std::size_t{4}})
    for(const bool extended:{false,true}) {
        Config config;config.sample_rate=64;config.bandwidth_hz=8;config.carrier_hz=16;
        config.integration_seconds=4.015625;config.pulse_shaping=shaped;
        PatternCode code(config);
        check(code.symbol_samples()%code.chip_samples()!=0,"nominal cache fixture needs a partial final chip");
        FftSearchBatch batch;auto& geometry=batch.geometry;
        geometry.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),shaped?1U:0U,0,0,
            config.spreading_seed,config.dsss_seed};
        geometry.bins_per_symbol=(code.symbol_samples()+bin-1)/bin+3;
        geometry.bin_samples=bin;geometry.carrier_hz=config.carrier_hz;
        geometry.evidence_count=static_cast<double>(geometry.bins_per_symbol);geometry.noise_condition=1;
        geometry.sample_fit=bin==1;geometry.extended_clock_window=extended;
        batch.starts=9;batch.score_stride=12;
        std::size_t transform=1;
        while(transform<geometry.bins_per_symbol+batch.starts-1)transform*=2;
        std::vector<FftComplex> spectrum(transform),carrier(transform);
        std::vector<double> energy(transform+1);
        for(std::size_t i=0;i<transform;++i) {
            spectrum[i]={std::sin(static_cast<double>(i)*.19),std::cos(static_cast<double>(i)*.13)};
            energy[i+1]=energy[i]+std::norm(spectrum[i]);
            carrier[i]=std::polar(1.,4*std::numbers::pi*config.carrier_hz*
                (static_cast<double>(i*bin)+static_cast<double>(bin-1)/2)/config.sample_rate);
        }
        pattern_fft(spectrum,false,{});
        batch.spectrum=spectrum;batch.carrier_square=carrier;batch.energy_prefix=energy;
        std::vector<FftSearchJob> jobs;
        for(const auto symbol:{std::uint64_t{0},std::uint64_t{3}})
        for(const auto ratio:{1.,.992,1.008}) {
            FftSearchJob job;job.symbol=symbol;job.phase=7;job.frequency_hz=.03125*(static_cast<double>(jobs.size())-2);
            job.clock_ratio=ratio;jobs.push_back(job);
        }
        std::vector<FftSearchWorkspace> serial,parallel;
        serial.emplace_back(config,transform,true);
        for(std::size_t i=0;i<search_concurrency(3);++i)parallel.emplace_back(config,transform,true);
        std::vector<FftSearchScore> expected(jobs.size()*batch.score_stride,untouched),actual(expected);
        execute_fft_search_cpu(batch,jobs,expected,serial);
        std::vector<std::array<FftComplex,2>> reference(static_cast<std::size_t>(geometry.bins_per_symbol));
        prepare_fft_nominal_reference(geometry,code,reference);
        batch.nominal_reference=reference;
        execute_fft_search_cpu(batch,jobs,actual,parallel);
        check(std::equal(expected.begin(),expected.end(),actual.begin(),equal),
              "public nominal cache changed a score, partial-chip edge, clock alternative or padding");
        // A coupled clock must never borrow nominal samples, even when a
        // caller supplies a cache that would materially change its scores.
        for(auto& pair:reference)pair={FftComplex{17,23},FftComplex{-19,29}};
        execute_fft_search_cpu(batch,jobs,actual,parallel);
        for(std::size_t job=0;job<jobs.size();++job)if(jobs[job].clock_ratio!=1)
            for(std::size_t start=0;start<batch.score_stride;++start)
                check(equal(expected[job*batch.score_stride+start],actual[job*batch.score_stride+start]),
                      "coupled clock fit used public nominal reference samples");
        bool rejected=false;
        batch.nominal_reference=std::span(reference).first(reference.size()-1);
        try {execute_fft_search_cpu(batch,jobs,actual,parallel);}catch(const Error&){rejected=true;}
        check(rejected,"FFT batch accepted an incomplete nominal reference span");
        batch.nominal_reference=reference;
        std::stop_source stop;stop.request_stop();bool cancelled=false;
        try {prepare_fft_nominal_reference(geometry,code,reference,stop.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled,"nominal reference construction ignored cancellation");
        cancelled=false;
        try {execute_fft_search_cpu(batch,jobs,actual,parallel,stop.get_token());}catch(const Error&){cancelled=true;}
        check(cancelled,"cached FFT scoring ignored cancellation");
        geometry.pattern.chips_per_symbol=0;rejected=false;
        try {prepare_fft_nominal_reference(geometry,code,reference);}catch(const Error&){rejected=true;}
        check(rejected,"nominal reference accepted a zero symbol chip count");
        geometry.pattern.chips_per_symbol=code.chips_per_symbol();geometry.pattern.scramble=1;rejected=false;
        try {prepare_fft_nominal_reference(geometry,code,reference);}catch(const Error&){rejected=true;}
        check(rejected,"nominal reference accepted a private pattern");
    }
}
void drift_rank_and_partition() {
    // Independent binomial-CDF identity for integer Beta parameters, rather
    // than repeating the production rising-factorial survival polynomial.
    for(unsigned rank=1;rank<=4;++rank)for(unsigned residual:{1U,4U,30U,500U})
    for(long double q:{.0001L,.01L,.1L,.3L,.9L})for(bool real:{false,true}) {
        const unsigned trials=residual+rank-1;
        long double probability=0,choose=1;
        for(unsigned successes=0;successes<rank;++successes) {
            probability+=choose*std::pow(q,successes)*std::pow(1-q,trials-successes);
            choose*=static_cast<long double>(trials-successes)/(successes+1);
        }
        const auto reference=-std::log(probability);
        const auto actual=drift_evidence(static_cast<double>(q),1.,(real?2.:1.)*(residual+rank),rank,real);
        check(std::abs(reference-actual)<=1e-10L*std::max(1.L,std::abs(reference)),
              "section evidence disagrees with independent Beta-tail rank calibration");
    }
    check(combine_drift_evidence(11,17,1)==11 && combine_drift_evidence(0,.1,4)==0 &&
          std::abs(combine_drift_evidence(11,17,4)-(17-std::numbers::ln2))<1e-14,
          "section detector changed the coherent baseline or omitted its two-detector penalty");
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    check(drift_boundary(0,maximum,4)==0 && drift_boundary(4,maximum,4)==maximum &&
          drift_boundary(3,maximum,4)==maximum/4*3+2,
          "section boundaries overflowed wide symbol coordinates");
    Config c;c.sample_rate=256;c.bandwidth_hz=8;c.carrier_hz=64;c.spreading_factor=64;
    check(drift_section_count(c)==4 && drift_section_count(c,false)==1,
          "complete long-pattern quarters did not enable the bounded section detector");
    c.spreading_factor=65;
    check(drift_section_count(c)==1,"cut quarter edges counted incomplete chips as sixteen complete chips");
    c.spreading_factor=128;c.spreading_mode=SpreadingMode::tone;
    check(drift_section_count(c)==1,"tone frequency labels enabled pattern section fitting");
}

void drift_direct_fft_and_real_gram() {
    for(bool keyed:{false,true})for(bool shaped:{false,true})for(std::size_t bin:{1U,4U}) {
        Config config;config.sample_rate=256;config.bandwidth_hz=32;config.carrier_hz=61;
        config.integration_seconds=16.00390625;config.scramble=keyed;config.pulse_shaping=shaped;
        config.stream_epoch=1789312671;config.spreading_seed[2]=91;
        PatternCode code(config,config.stream_epoch);
        FftSearchBatch batch;auto& g=batch.geometry;
        g.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),shaped?1U:0U,keyed?1U:0U,0,
            config.spreading_seed,config.dsss_seed};
        const auto length=static_cast<std::size_t>((code.symbol_samples()+bin-1)/bin+3);
        g.bins_per_symbol=length;g.bin_samples=bin;g.carrier_hz=config.carrier_hz;
        g.evidence_count=bin==1?4.*length/code.chip_samples():static_cast<double>(length);
        g.sample_fit=g.real_rank=bin==1;g.drift_sections=4;g.extended_clock_window=1;
        batch.first_bin=37;batch.starts=3;batch.score_stride=5;
        std::size_t transform=1;while(transform<length+batch.starts-1)transform*=2;
        std::vector<FftComplex> observations(transform),spectrum;
        std::vector<double> energy(transform+1);
        code.set_stream_phase_samples(7);
        // Exact-real observations retain their absolute carrier orientation;
        // complex bins use both independent coordinates.
        for(std::size_t i=0;i<transform;++i) {
            const auto carrier=2*std::numbers::pi*config.carrier_hz*
                (static_cast<double>((batch.first_bin+i)*bin)+static_cast<double>(bin-1)/2)/config.sample_rate;
            const auto x=std::sin(.19*i)+.7*std::cos(.13*i);
            const auto within=static_cast<double>(i*bin)+static_cast<double>(bin-1)/2;
            FftComplex signal{};
            if(within<code.symbol_samples()) {
                const auto chip=static_cast<std::uint64_t>(within/code.chip_samples());
                signal=shaped?code.shaped_value(3*code.chips_per_symbol(),0,within):
                    code.value(3*code.chips_per_symbol()+chip,0,within/code.chip_samples()-chip);
                signal*=std::polar(1.,.7*static_cast<double>((i*4/length)%4));
            }
            observations[i]=bin==1?(.3*x+(signal*std::polar(1.,carrier)).real())*std::polar(1.,-carrier):
                signal+.3*FftComplex{x,std::cos(.17*i)};
            energy[i+1]=energy[i]+std::norm(observations[i]);
        }
        spectrum=observations;pattern_fft(spectrum,false,{});
        batch.spectrum=spectrum;batch.observations=observations;batch.energy_prefix=energy;
        std::array<FftSearchJob,3> jobs{};
        for(std::size_t j=0;j<jobs.size();++j) {
            jobs[j].symbol=3;jobs[j].phase=7;jobs[j].clock_ratio=1.+(static_cast<double>(j)-1)*.007;
            jobs[j].frequency_hz=.03125*(static_cast<double>(j)-1);
        }
        std::vector<FftSearchWorkspace> workers;workers.emplace_back(config,transform,true,batch.starts);
        std::vector<FftSearchScore> direct(jobs.size()*batch.score_stride,untouched),fft(direct);
        execute_fft_search_cpu(batch,jobs,direct,workers);
        batch.observations={}; // Force the FFT path over the identical samples.
        execute_fft_search_cpu(batch,jobs,fft,workers);
        bool meaningful=false;
        for(std::size_t job=0;job<jobs.size();++job)for(std::size_t start=0;start<batch.score_stride;++start) {
            const auto a=direct[job*batch.score_stride+start],b=fft[job*batch.score_stride+start];
            if(start>=batch.starts)check(equal(a,untouched)&&equal(b,untouched),"section scorer overwrote job padding");
            else {
                meaningful|=a.zero>1 || a.one>1;
                check(std::abs(a.zero-b.zero)<=1e-8*std::max({1.,a.zero,b.zero}) &&
                      std::abs(a.one-b.one)<=1e-8*std::max({1.,a.one,b.one}),
                      "direct and FFT section fits disagree with clock offsets, real Gram phase or partial chips");
            }
        }
        check(meaningful,"section equivalence fixture has only zero evidence");
        std::vector<FftComplex> product(transform);
        std::vector<FftDriftAccumulator> scratch(batch.starts);
        const auto rejected=[&] {
            try {execute_drift_search_job(batch,jobs[0],direct,product,scratch,code);}
            catch(const Error&){return true;}
            return false;
        };
        batch.energy_prefix=std::span(energy).first(length);
        check(rejected(),"serial section scorer accepted a truncated energy prefix");
        batch.energy_prefix=energy;g.pattern.chip_samples=0;
        check(rejected(),"serial section scorer accepted zero chip geometry");
        g.pattern.chip_samples=code.chip_samples();batch.starts=0;
        check(rejected(),"serial section scorer accepted an empty start range");
        batch.starts=3;batch.spectrum=std::span(spectrum).first(transform-1);product.resize(transform-1);
        check(rejected(),"serial section scorer accepted a non-power-of-two transform");
    }
}

void differential_direct_fft_and_real_gram() {
    // Exercise both raw real samples and disjoint projected bins. This symbol
    // has 256 complete short windows and a partial final chip/window; neither
    // path may incorporate the incomplete window in differential evidence.
    for(const std::size_t bin:{1U,4U}) {
        Config config;config.sample_rate=64;config.bandwidth_hz=32;config.carrier_hz=bin==1?16:15.3;
        config.integration_seconds=256.046875;config.scramble=bin==1;config.pulse_shaping=bin==4;
        config.stream_epoch=1789312671;config.spreading_seed[2]=91;
        PatternCode code(config,config.stream_epoch);code.set_stream_phase_samples(7);
        FftSearchBatch batch;auto& g=batch.geometry;
        g.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),bin==4?1U:0U,bin==1?1U:0U,0,
            config.spreading_seed,config.dsss_seed};
        const auto length=static_cast<std::size_t>((code.symbol_samples()+bin-1)/bin+3);
        g.bins_per_symbol=length;g.bin_samples=bin;g.carrier_hz=config.carrier_hz;
        g.evidence_count=bin==1?4.*length/code.chip_samples():static_cast<double>(length);
        g.sample_fit=g.real_rank=bin==1;g.drift_sections=4;g.extended_clock_window=1;
        const auto window=differential_window_samples(code.symbol_samples(),code.chip_samples(),config.sample_rate,1.);
        check(window==64,"differential FFT fixture must have sixteen-chip local windows");
        batch.first_bin=37;batch.starts=3;batch.score_stride=5;
        std::size_t transform=1;while(transform<length+batch.starts-1)transform*=2;
        std::vector<FftComplex> observations(transform),spectrum;
        std::vector<double> energy(transform+1);
        for(std::size_t i=0;i<transform;++i) {
            const auto position=static_cast<double>(i*bin)+static_cast<double>(bin-1)/2;
            const auto carrier=2*std::numbers::pi*config.carrier_hz*
                (static_cast<double>((batch.first_bin+i)*bin)+static_cast<double>(bin-1)/2)/config.sample_rate;
            const auto x=std::sin(.19*i)+.7*std::cos(.13*i);
            FftComplex signal{};
            if(position<code.symbol_samples()) {
                const auto chip=static_cast<std::uint64_t>(position/code.chip_samples());
                signal=config.pulse_shaping?code.shaped_value(3*code.chips_per_symbol(),0,position):
                    code.value(3*code.chips_per_symbol()+chip,0,position/code.chip_samples()-chip);
                signal*=std::polar(1.,.73+.3*position/window);
            }
            observations[i]=bin==1?(.15*x+(signal*std::polar(1.,carrier)).real())*std::polar(1.,-carrier):
                signal+.15*FftComplex{x,std::cos(.17*i)};
            energy[i+1]=energy[i]+std::norm(observations[i]);
        }
        spectrum=observations;pattern_fft(spectrum,false,{});
        batch.spectrum=spectrum;batch.observations=observations;batch.energy_prefix=energy;
        std::array<FftSearchJob,2> jobs{};
        for(std::size_t j=0;j<jobs.size();++j) {
            jobs[j].symbol=3;jobs[j].phase=7;jobs[j].clock_ratio=1.+static_cast<double>(j)*.00005;
            jobs[j].frequency_hz=static_cast<double>(j)*.0001;
        }
        std::vector<FftSearchWorkspace> workers;
        workers.emplace_back(config,transform,true,batch.starts,batch.starts);
        std::vector<FftSearchScore> baseline(jobs.size()*batch.score_stride,untouched),direct(baseline),fft(baseline);
        execute_fft_search_cpu(batch,jobs,baseline,workers);
        g.differential_window_samples=window;
        execute_fft_search_cpu(batch,jobs,direct,workers);
        batch.observations={};
        execute_fft_search_cpu(batch,jobs,fft,workers);
        check(direct[0].zero>30 && direct[0].zero>baseline[0].zero+20,
              "local differential fixture did not improve on the four-section baseline");
        for(std::size_t job=0;job<jobs.size();++job)for(std::size_t start=0;start<batch.score_stride;++start) {
            const auto a=direct[job*batch.score_stride+start],b=fft[job*batch.score_stride+start];
            if(start>=batch.starts)check(equal(a,untouched)&&equal(b,untouched),
                "local differential scorer overwrote job padding");
            else check(std::abs(a.zero-b.zero)<=1e-8*std::max({1.,a.zero,b.zero}) &&
                       std::abs(a.one-b.one)<=1e-8*std::max({1.,a.one,b.one}),
                "direct and FFT local fits disagree with clock offsets, real Gram phase or partial windows");
        }
        std::vector<FftComplex> product(transform);
        std::vector<FftDriftAccumulator> scratch(batch.starts);
        std::vector<DifferentialAccumulator> local(batch.starts);
        const auto rejected=[&](std::span<DifferentialAccumulator> supplied) {
            try {execute_drift_search_job(batch,jobs[0],direct,product,scratch,code,{},supplied);}
            catch(const Error&){return true;}
            return false;
        };
        check(rejected(std::span(local).first(batch.starts-1)),
              "local differential scorer accepted insufficient bounded scratch");
        g.differential_window_samples=code.chip_samples();
        check(rejected(local),"local differential scorer accepted fewer than sixteen chips per window");
        g.differential_window_samples=window+1;
        check(rejected(local),"local differential scorer accepted a fractional-chip window");
        g.differential_window_samples=window*2;
        check(rejected(local),"local differential scorer accepted fewer than 256 complete windows");
    }
}
} // namespace
namespace paired_regression {
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
namespace paired=datapump::modem::detail::partitioned_paired;
namespace {
constexpr FftSearchScore untouched{-19,-23};
constexpr FftComplex sentinel{-31,17};
void check(bool b,const char* why){if(!b)throw Error(why);}
bool close(double a,double b){return std::abs(a-b)<=1e-8*std::max({1.,std::abs(a),std::abs(b)});}
bool identical(FftComplex a,FftComplex b){return std::bit_cast<std::uint64_t>(a.real())==std::bit_cast<std::uint64_t>(b.real())&&
    std::bit_cast<std::uint64_t>(a.imag())==std::bit_cast<std::uint64_t>(b.imag());}
struct Fixture {
    Config config;
    FftSearchBatch batch;
    std::vector<FftComplex> input,spectrum;
    std::vector<double> energy;
    std::vector<FftSearchJob> jobs;
    std::vector<FftStartRange> selected;
    Fixture(bool shaped,unsigned factor,OuterDsssVersion version,bool public_pattern=false) {
        config.sample_rate=64;config.bandwidth_hz=8./factor;config.carrier_hz=15.3;
        config.integration_seconds=1025./64;config.pulse_shaping=shaped;config.scramble=!public_pattern;
        config.dsss=factor>1;config.dsss_factor=factor;config.outer_dsss_version=version;
        config.stream_epoch=1789312671;
        for(std::size_t i=0;i<32;++i) {config.spreading_seed[i]=static_cast<std::uint8_t>(11*i+7);
            config.dsss_seed[i]=static_cast<std::uint8_t>(251-3*i);}
        PatternCode code(config,config.stream_epoch);auto& g=batch.geometry;
        g.pattern={config.stream_epoch,code.chip_samples(),code.chips_per_symbol(),code.symbol_samples(),
            config.sample_rate,static_cast<std::uint32_t>(config.spreading_mode),shaped?1U:0U,
            config.scramble?1U:0U,config.dsss?1U:0U,config.spreading_seed,config.dsss_seed,
            factor,static_cast<std::uint32_t>(version)};
        g.bin_samples=4;g.bins_per_symbol=(code.symbol_samples()+3)/4+3;
        g.carrier_hz=config.carrier_hz;g.extended_clock_window=1;g.evidence_count=g.bins_per_symbol;
        const auto image=std::abs(pattern_projection_image_ratio(g.bin_samples,g.carrier_hz,config.sample_rate));
        g.noise_condition=(1+image)/(1-image);
        batch.starts=73;batch.score_stride=78;batch.first_bin=37;
        std::size_t S=1;while(S<g.bins_per_symbol+batch.starts-1)S*=2;
        input.resize(S);energy.resize(S+1);
        // Identical real PCM transformed into disjoint complex projection bins.
        // Both quadratures and its existing covariance-eigenratio are retained.
        std::mt19937_64 random(0x12345);std::normal_distribution<double> noise;
        for(std::size_t i=0;i<S;++i) {
            for(std::size_t n=0;n<g.bin_samples;++n) {
                const auto at=(batch.first_bin+i)*g.bin_samples+n;
                const auto pcm=.6*noise(random)+std::sin(.071*at)+.15*std::cos(.003*at);
                input[i]+=pcm*std::polar(1.,-2*std::numbers::pi*g.carrier_hz*at/config.sample_rate)/
                    std::sqrt(static_cast<double>(g.bin_samples));
            }
            energy[i+1]=energy[i]+std::norm(input[i]);
        }
        spectrum=input;pattern_fft(spectrum,false,{});
        batch.spectrum=spectrum;batch.observations=input;batch.energy_prefix=energy;
        selected={{0,1},{3,2},{15,3},{31,3},{63,2},{72,1}};batch.start_ranges=selected;
        jobs.resize(3);jobs[0].symbol=3;jobs[0].phase=7;jobs[0].frequency_hz=.03125;
        jobs[0].clock_ratio=.9998;jobs[0].range_count=selected.size();
        jobs[1]=jobs[0];jobs[1].symbol=1;jobs[1].phase=3;jobs[1].frequency_hz=-.03125;
        jobs[1].clock_ratio=1.0002;jobs[1].range_begin=2;jobs[1].range_count=3;
        jobs[2]=jobs[0];jobs[2].symbol=0;jobs[2].range_count=0;
    }
};

void score_equivalence() {
    for(const bool shaped:{false,true})for(unsigned variant=0;variant<4;++variant) {
        Fixture f(shaped,variant==1||variant==2?10:1,
            variant==2?OuterDsssVersion::interleaved_v2:OuterDsssVersion::legacy_v1,variant==3);
        std::vector<std::array<FftComplex,2>> nominal;
        if(variant==3){nominal.resize(f.batch.geometry.bins_per_symbol);PatternCode code(f.config,f.config.stream_epoch);
            prepare_fft_nominal_reference(f.batch.geometry,code,nominal);f.batch.nominal_reference=nominal;
            for(auto& job:f.jobs)job.clock_ratio=1;}
        std::vector<FftSearchWorkspace> workers;workers.emplace_back(f.config,f.spectrum.size(),true);
        std::vector<FftSearchScore> expected(f.jobs.size()*f.batch.score_stride,untouched);
        f.batch.observations={};execute_fft_search_cpu(f.batch,f.jobs,expected,workers);f.batch.observations=f.input;
        for(const std::size_t B:{2,16,64,256}) {
            const auto req=paired::preflight(f.batch,f.jobs,B,1024*1024);check(req.has_value(),"paired preflight unsupported");
            const auto first_size=std::max<std::size_t>(1,req->input_tiles/2)*req->transform;
            const auto second_size=req->complex_count-first_size;
            std::vector<FftComplex> first(first_size+3,sentinel),second(second_size+3,sentinel);paired::Work work;
            auto context=paired::prepare(f.batch,*req,std::span(first).first(first_size),std::span(second).first(second_size),&work);
            check(context.has_value(),"paired arena preparation failed");
            PatternCode code(f.config,f.config.stream_epoch);
            for(std::size_t ji=0;ji<f.jobs.size();++ji) {
                std::vector<FftComplex> out(f.batch.score_stride,sentinel);
                check(paired::score_job(*context,f.jobs[ji],code,out,&work),"paired job fallback despite preflight");
                for(std::size_t j=0;j<out.size();++j) {
                    const auto e=expected[ji*f.batch.score_stride+j];
                    if(e.zero==untouched.zero&&e.one==untouched.one)check(out[j]==sentinel,"paired job changed excluded original start/padding");
                    else check(close(out[j].real(),e.zero)&&close(out[j].imag(),e.one),
                        "paired shaped_values/rotation/norm/clock/covariance changed a retained score");
                }
            }
            check(first[first_size]==sentinel&&first[first_size+2]==sentinel&&
                second[second_size]==sentinel&&second[second_size+2]==sentinel,"paired split arena overrun");
            check(work.input_transforms==req->input_tiles,"paired input FFT repeated across bit/job");
            check(work.template_values==4*f.batch.geometry.bins_per_symbol,"paired job reused a secret bit/symbol row");
        }
    }
}
void direct_equivalence() {
    for(const bool shaped:{false,true})for(unsigned variant=0;variant<4;++variant){
        Fixture f(shaped,variant==1||variant==2?10:1,
            variant==2?OuterDsssVersion::interleaved_v2:OuterDsssVersion::legacy_v1,variant==3);
        std::vector<std::array<FftComplex,2>> nominal;
        if(variant==3){nominal.resize(f.batch.geometry.bins_per_symbol);PatternCode code(f.config,f.config.stream_epoch);
            prepare_fft_nominal_reference(f.batch.geometry,code,nominal);f.batch.nominal_reference=nominal;
            for(auto& job:f.jobs)job.clock_ratio=1;}
        std::vector<FftSearchWorkspace> workers;workers.emplace_back(f.config,f.spectrum.size(),true);
        PatternCode code(f.config,f.config.stream_epoch);
        // The original <=4-start direct path is a strict arithmetic control.
        f.selected={{0,1},{3,2},{15,1}};f.batch.start_ranges=f.selected;
        auto job=f.jobs[0];job.range_begin=0;job.range_count=f.selected.size();
        std::vector<FftSearchScore> old_direct(f.batch.score_stride,untouched);
        execute_fft_search_cpu(f.batch,std::span(&job,1),old_direct,workers);
        const auto spectrum=f.batch.spectrum;f.batch.spectrum={};
        std::vector<FftComplex> out(f.batch.score_stride,sentinel);paired::Work work;
        check(paired::score_direct(f.batch,job,code,out,&work),"paired direct unsupported at four selected starts");
        for(std::size_t j=0;j<out.size();++j){const auto expected=old_direct[j];
            if(expected.zero==untouched.zero&&expected.one==untouched.one)
                check(out[j]==sentinel,"paired direct changed excluded start or padding");
            else check(identical(out[j],FftComplex(expected.zero,expected.one)),
                "paired direct changed original ascending norm/dot arithmetic");}
        check(work.input_transforms==0&&work.template_transforms==0&&work.inverse_transforms==0&&
            work.template_values==2*f.batch.geometry.bins_per_symbol&&work.selected_starts==4&&
            work.complex_products==8*f.batch.geometry.bins_per_symbol,
            "paired direct unexpectedly used FFT or regenerated per-start private rows");
        // Exercise the full 32-start stack bound using old full-FFT covariance
        // and energy scoring, then remove spectrum to prove this API needs none.
        f.batch.spectrum=spectrum;f.selected={{0,7},{15,12},{47,13}};f.batch.start_ranges=f.selected;
        job.range_count=f.selected.size();f.batch.observations={};
        std::vector<FftSearchScore> old_fft(f.batch.score_stride,untouched);
        execute_fft_search_cpu(f.batch,std::span(&job,1),old_fft,workers);
        f.batch.observations=f.input;f.batch.spectrum={};std::fill(out.begin(),out.end(),sentinel);
        check(paired::score_direct(f.batch,job,code,out),"paired direct failed at 32 selected starts");
        for(std::size_t j=0;j<out.size();++j){const auto expected=old_fft[j];
            if(expected.zero==untouched.zero&&expected.one==untouched.one)
                check(out[j]==sentinel,"paired direct32 changed excluded start or padding");
            else check(close(out[j].real(),expected.zero)&&close(out[j].imag(),expected.one),
                "paired direct32 changed same-PCM FFT score/covariance");}
        std::fill(out.begin(),out.end(),sentinel);f.selected.back().count=14;
        check(!paired::score_direct(f.batch,job,code,out)&&
            std::all_of(out.begin(),out.end(),[](auto value){return value==sentinel;}),
            "paired direct exceeded fixed 32-start stack bound");
    }
}
void fallback_and_cancellation() {
    Fixture f(true,1,OuterDsssVersion::legacy_v1);PatternCode code(f.config,f.config.stream_epoch);
    check(!paired::preflight(f.batch,f.jobs,64,1),"paired preflight exceeded bounded caller arena");
    auto req=paired::preflight(f.batch,f.jobs,64,1024*1024);check(req.has_value(),"paired fixture requires supported geometry");
    std::vector<FftComplex> arena(req->complex_count,sentinel),out(f.batch.score_stride,sentinel);
    auto malformed=*req;malformed.input_tiles=malformed.chunks-1;
    check(!paired::prepare(f.batch,malformed,arena),"paired prepare accepted a truncated input-tile envelope");
    check(!paired::prepare(f.batch,*req,std::span(arena).first(arena.size()-1)),"paired prepare accepted a non-P-aligned arena");
    auto context=paired::prepare(f.batch,*req,arena);check(context.has_value(),"paired fixture preparation failed");
    std::stop_source stop;stop.request_stop();bool cancelled=false;
    try{paired::score_job(*context,f.jobs[0],code,out,nullptr,stop.get_token());}catch(const Error&){cancelled=true;}
    check(cancelled&&std::all_of(out.begin(),out.end(),[](auto v){return v==sentinel;}),"paired stopped job wrote candidate evidence");
    cancelled=false;
    try{paired::score_direct(f.batch,f.jobs[0],code,out,nullptr,stop.get_token());}catch(const Error&){cancelled=true;}
    check(cancelled&&std::all_of(out.begin(),out.end(),[](auto v){return v==sentinel;}),
        "paired stopped nonempty direct job wrote candidate evidence");
    check(paired::score_job(*context,f.jobs[0],code,out),"paired cancellation broke later reuse");
    check(!paired::score_job(*context,f.jobs[0],code,std::span(arena).first(f.batch.starts)),"paired scores aliased input cache arena");
    f.batch.geometry.sample_fit=1;check(!paired::preflight(f.batch,f.jobs,64,1024*1024),"paired sample-fit fallback missing");
    f.batch.geometry.sample_fit=0;const auto saved=f.batch.observations;
    f.batch.observations=std::span(arena).first(f.input.size());check(!paired::prepare(f.batch,*req,arena),"paired prepare overwrote source observations");
    f.batch.observations=saved;
    f.jobs[0].prepared_template=0;check(!paired::preflight(f.batch,f.jobs,64,1024*1024),"paired prepared-template fallback missing");
    check(!paired::score_direct(f.batch,f.jobs[0],code,out),"paired direct prepared-template fallback missing");
    f.jobs[0].prepared_template=std::numeric_limits<std::uint64_t>::max();
    for(auto& job:f.jobs)job.range_count=0;req=paired::preflight(f.batch,f.jobs,64,0);
    check(req&&req->complex_count==0,"paired empty selection reserved memory");
    context=paired::prepare(f.batch,*req,{});check(context.has_value(),"paired empty selection preparation failed");
    std::fill(out.begin(),out.end(),sentinel);check(paired::score_job(*context,f.jobs[0],code,out)&&
        std::all_of(out.begin(),out.end(),[](auto v){return v==sentinel;}),"paired empty job scored data");
    std::stop_source direct_stop;direct_stop.request_stop();cancelled=false;
    try{paired::score_direct(f.batch,f.jobs[0],code,out,nullptr,direct_stop.get_token());}
    catch(const Error&){cancelled=true;}
    check(cancelled&&std::all_of(out.begin(),out.end(),[](auto v){return v==sentinel;}),
        "paired direct cancellation changed evidence");
    check(paired::score_direct(f.batch,f.jobs[0],code,out),"paired direct empty selection failed");
    f.jobs[0].range_count=f.selected.size();
    check(!paired::score_direct(f.batch,f.jobs[0],code,std::span(f.input).first(f.batch.starts)),
        "paired direct overwrote observations");
    f.batch.geometry.sample_fit=1;
    check(!paired::score_direct(f.batch,f.jobs[0],code,out),"paired direct sample-fit fallback missing");
    f.batch.geometry.sample_fit=0;
    check(!paired::score_job(*context,f.jobs[0],code,out)&&
        std::all_of(out.begin(),out.end(),[](auto v){return v==sentinel;}),
        "paired empty context accepted a later nonempty job");
}
}
void planner_geometry_equivalence() {
    Fixture f(true,10,OuterDsssVersion::interleaved_v2,false);
    for(const auto starts:{73U,128U})for(const auto jobs:{1U,5U}) {
        f.batch.starts=starts;
        const auto planned=paired::choose_geometry(f.batch.geometry.bins_per_symbol,starts,
            f.selected,jobs,f.spectrum.size(),1024*1024);
        const auto runtime=paired::choose(f.batch,f.jobs[0],jobs,f.spectrum.size(),1024*1024);
        check(planned.has_value()==runtime.has_value(),"planner/runtime partition crossover differs");
        if(!planned)continue;
        const auto checked=paired::preflight(f.batch,std::span(f.jobs).first(1),planned->tile_size,1024*1024);
        check(checked && checked->input_tiles==planned->input_tiles &&
            checked->max_job_output_tiles==planned->max_job_output_tiles &&
            checked->complex_count==planned->complex_count && runtime->transform==planned->transform &&
            paired::operations(*checked,jobs)==paired::operations(*planned,jobs),
            "size-only partition plan disagrees with validated runtime scratch/work");
    }
}
void run(){score_equivalence();direct_equivalence();fallback_and_cancellation();planner_geometry_equivalence();}
}
int main() {
    try {paired_regression::run();flat_batch_tiling_and_order();prepared_template_and_cancellation();restricted_coherent_direct_and_fft();public_nominal_reference_exactness();
        drift_rank_and_partition();drift_direct_fft_and_real_gram();differential_direct_fft_and_real_gram();}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"pattern FFT batch tests passed\n";
}

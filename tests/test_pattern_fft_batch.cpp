#include "../src/pattern_fft_batch.hpp"
#include "../src/search_parallel.hpp"
#include <algorithm>
#include <iostream>
#include <numeric>
#include <numbers>

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
} // namespace
int main() {
    try {flat_batch_tiling_and_order();prepared_template_and_cancellation();public_nominal_reference_exactness();}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
    std::cout<<"pattern FFT batch tests passed\n";
}

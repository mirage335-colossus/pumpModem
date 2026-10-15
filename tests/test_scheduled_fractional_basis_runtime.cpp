// Proposed bounded identical-PCM receiver regression. Uncompiled/unrun artifact.
#include "scheduled_lazy_test_fixture.hpp"
int main(){try {
    setenv("DATAPUMP_SCHED_LAZY_ACQUISITION","1",1);
    setenv("DATAPUMP_SCHED_LAZY_BATCH","0",1);
    for(const unsigned multiplier:{3U,5U}) {
        auto c=configuration(multiplier,10,false); // retain the established 64-coefficient private geometry
        const Bytes bits{0,1};const auto pcm=capture(c,bits,true,10,.002);
        auto s=search(c,pcm,true);s.start_uncertainty_seconds=.02;s.worker_threads=1;
        s.retain_score=0;s.candidate_limit=4096;
        for(const auto frequency:s.frequency_offsets_hz)for(const auto clock:s.clock_errors_ppm)
            s.hypotheses.push_back({frequency,clock});
        unsetenv("DATAPUMP_SCHED_FRACTIONAL_BASIS");
        const auto reference=receive(c,pcm,s,113,false,false,false,false,false,false,true);
        setenv("DATAPUMP_SCHED_FRACTIONAL_BASIS","1",1);
        const auto actual=receive(c,pcm,s,113,false,false,false,false,false,false,true);
        compare(reference,actual);
        if(actual.bits!=bits || !actual.complete || actual.completed<pcm.absent_end) {
            std::cerr<<"fixture M="<<multiplier<<" bits=";for(auto bit:actual.bits)std::cerr<<unsigned(bit);
            std::cerr<<" complete="<<actual.complete<<" end="<<actual.completed<<" absent="<<pcm.absent_end
                <<" reference_complete="<<reference.complete<<" evidence="<<actual.evidence.size()<<"\n";
        }
        check(actual.bits==bits && actual.complete && actual.completed>=pcm.absent_end,
            "fractional basis lost bits or manufactured physical completion");
        check(actual.work.implicit_fp32_ranges>0,
            "fractional runtime fixture did not execute native FP32 screening");
        auto forced=c;forced.search_arithmetic=modem::SearchArithmetic::fp64;
        const auto diagnostic=receive(forced,pcm,s,113,false,false,false,false,false,false,true);
        compare(reference,diagnostic);
        check(diagnostic.work.implicit_fp32_ranges==0,
            "FP64 force silently executed fractional FP32 template operands");
    }
    unsetenv("DATAPUMP_SCHED_FRACTIONAL_BASIS");unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    std::cout<<"fractional runtime identical-PCM decisions/history/publish/END parity\n";
    return 0;
} catch(const std::exception& e) {
    unsetenv("DATAPUMP_SCHED_FRACTIONAL_BASIS");unsetenv("DATAPUMP_SCHED_LAZY_ACQUISITION");
    std::cerr<<e.what()<<'\n';return 1;
}}

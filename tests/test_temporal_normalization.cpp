// Standalone ORIGINAL public-Plan oracle proposal; root owns compilation/runs.
#include "../src/scheduled_temporal_normalization.hpp"
#include "../src/scheduled_ordered_pulse_candidates.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <random>
#include <vector>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using Tile=ScheduledOrderedPulseCandidatesT<1,8,32>;
struct Tap {std::uint64_t index=0,weight=0;};
struct Row {std::array<Tap,18> taps{};std::size_t count=0;};
void check(bool value,const char* message){if(!value)throw Error(message);}
Row original(double s,std::uint64_t N,std::uint64_t C) {
    Row row;pattern_pulse_each(s,N,C,[&](std::uint64_t k,double w) {
        check(row.count<row.taps.size(),"unexpected nonzero callback count");
        row.taps[row.count++]={k,std::bit_cast<std::uint64_t>(w)};
    });return row;
}
std::uint64_t vectors=0,coordinates=0;
void verify(const std::array<double,4>& input,std::uint64_t N,std::uint64_t C) {
    const auto normalized=scheduled_temporal_normalization(input,N,C);
    check(normalized.eligible,"eligible generalized vector rejected");
    const auto shift=normalized.chip_translation;const auto T=C*shift;
    std::array<double,4> output;
    for(std::size_t i=0;i<input.size();++i) {
        output[i]=input[i]-static_cast<double>(T);
        const auto a=original(input[i],N,C),b=original(output[i],N,C);
        check(a.count==b.count,"normalized callback count differs");
        for(std::size_t j=0;j<a.count;++j)
            check(a.taps[j].index==b.taps[j].index+shift && a.taps[j].weight==b.taps[j].weight,
                "normalized ORIGINAL callback index/weight/order differs");
        ++coordinates;
    }
    Tile::Plan a,b;Tile::Statistics statistics;
    check(a.build_original(input,N,C,statistics) && b.build_original(output,N,C,statistics),
        "bounded four-sample original Plan unavailable");
    check(a.same_weights(b) && a.first_local==b.first_local+shift,
        "Plan layout/active masks/private relabel changed");
    ++vectors;
}
int main() {
    try {
        // This oracle builds public Plans and compares native pulse callbacks;
        // it does not gather or execute the architecture-specific Tile backend.
        for(std::uint64_t C=1;C<=1023;++C) {
            const auto qmax=((std::uint64_t{1}<<31)-32)/C;
            for(const auto q:{std::uint64_t{9},std::uint64_t{10},qmax-2})
                for(double residue:{0.,static_cast<double>(C)/2,std::nextafter(static_cast<double>(C),0.)}) {
                    const auto s=static_cast<double>(q*C)+residue;
                    const std::array<double,4> input{s,s+.125,s+.5,s+1};
                    for(const auto tail:{std::uint64_t{0},C/2,C-1}) {
                        const auto N=(static_cast<std::uint64_t>(input.back())/C+12)*C+tail;
                        verify(input,N,C);
                    }
                }
            const auto boundary_predecessor=std::nextafter(27.*C,0.);
            verify({boundary_predecessor,boundary_predecessor+.125,boundary_predecessor+.5,boundary_predecessor+1},100*C+C/2,C);
            // Inclusive source margin, inclusive final-partial margin and
            // half-integer centers for odd C / integer centers for even C.
            for(const auto tail:{std::uint64_t{0},C/2,C-1}) {
                const auto N=80*C+tail;const auto right=static_cast<double>(N-9*C);
                verify({right-.75,right-.5,std::nextafter(right,0.),right},N,C);
                verify({9.*C,std::nextafter(9.*C,std::numeric_limits<double>::infinity()),9.*C+.5,9.*C+1},N,C);
            }
        }
        // Binade neighbors, arbitrary fractional mantissas and the ORIGINAL
        // static_cast<double>(long-double source coordinate) at nonnominal rates.
        std::mt19937_64 random(713);
        for(const std::uint64_t C:{1ULL,3ULL,4ULL,10ULL,15ULL,17ULL,511ULL,1023ULL}) {
            for(unsigned exponent=4;exponent<=30;++exponent) {
                const auto s=std::ldexp(1.,exponent);
                if(s<10.*C)continue;
                for(double at:{std::nextafter(s,0.),s,std::nextafter(s,std::numeric_limits<double>::infinity())}) {
                    const auto N=(static_cast<std::uint64_t>(at+2)/C+12)*C+C/2;
                    verify({at,at+.125,at+.5,at+1},N,C);
                }
            }
            for(const long double ppm:{-200.L,-3.L,0.L,3.L,200.L})for(unsigned i=0;i<32;++i) {
                const auto residue=std::ldexp(static_cast<double>(random()>>12),-52)*C;
                const auto base=static_cast<long double>(32*C)+residue;
                std::array<double,4> input;
                for(std::size_t j=0;j<input.size();++j)input[j]=static_cast<double>(base+j*(1+ppm*1e-6L));
                const auto N=(static_cast<std::uint64_t>(input.back())/C+12)*C+C/2;
                verify(input,N,C);
            }
        }
        const auto s=std::nextafter(405.,0.);
        verify({s,s+.125,s+.5,s+1},1000,15);
        check(!scheduled_temporal_normalization(std::span<const double>{},1000,15).eligible,"empty vector normalized");
        check(!scheduled_temporal_normalization(std::array<double,4>{9,10,11,12},1000,0).eligible,"zero chip normalized");
        check(!scheduled_temporal_normalization(std::array<double,4>{10000,10001,10002,10003},100000,1024).eligible,"unqualified chip normalized");
        check(!scheduled_temporal_normalization(std::array<double,4>{135,134,136,137},1000,15).eligible,"nonmonotone vector normalized");
        check(!scheduled_temporal_normalization(std::array<double,4>{134.5,135,136,137},1000,15).eligible,"lower margin normalized");
        check(!scheduled_temporal_normalization(std::array<double,4>{864,865,865.5,866},1000,15).eligible,"partial-tail margin exceeded");
        std::cout<<"general temporal ORIGINAL callback+Plan oracle vectors="<<vectors<<" coordinates="<<coordinates<<" C1..1023\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}

// Artifact source only; root owns every compilation/execution.
#define DATAPUMP_ORDERED_PULSE_CONTRACT_OFF 1
#include "scheduled_ordered_pulse_candidates.hpp"
#include "pattern_correlator_batch.hpp"
#include "datapump/sched_hop.hpp"
#include <iostream>
#include <memory>
#include <vector>
using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using Tile=ScheduledOrderedPulseCandidatesT<1,512,96,false>;
static_assert(static_cast<unsigned>(Tile::Backend::scalar)==0 &&
              static_cast<unsigned>(Tile::Backend::sse2)==1);
static_assert(Tile::component_storage_bytes==0 || Tile::component_storage_bytes==3072);
namespace {
void check(bool value,const char* message){if(!value)throw Error(message);}
bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool bits(std::complex<double> a,std::complex<double> b){return bits(a.real(),b.real())&&bits(a.imag(),b.imag());}
void fit_equal(const CorrelationFit& a,const CorrelationFit& b) {
    check(bits(a.xc,b.xc)&&bits(a.xs,b.xs)&&bits(a.cc,b.cc)&&bits(a.ss,b.ss)&&
          bits(a.cs,b.cs)&&bits(a.energy,b.energy)&&a.count==b.count,"real FP64 Fit changed bits");
}
void merge(CorrelationFit& a,const CorrelationFit& b) {
    a.xc+=b.xc;a.xs+=b.xs;a.cc+=b.cc;a.ss+=b.ss;a.cs+=b.cs;a.energy+=b.energy;a.count+=b.count;
}
Config configuration() {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
    c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x67));
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=static_cast<std::uint8_t>(3*i+7);c.dsss_seed[i]=static_cast<std::uint8_t>(5*i+11);}
    c.stream_epoch=1799999967;c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,9});return c;
}
void inaccessible(const Tile& tile) {
    bool threw=false;try{(void)tile.raw(0,0);}catch(const Error&){threw=true;}
    check(threw && tile.samples()==0 && tile.lanes()==0,"failed borrow exposed stale numeric output");
}
std::uint64_t numerical_oracles() {
    const auto c=configuration();PatternCode code(c,c.stream_epoch),original(c,c.stream_epoch);
    check(code.chip_samples()==15,"component oracle lost D100/C15 geometry");
    std::uint64_t avx_rows=0;
    for(unsigned scenario=0;scenario<3;++scenario) {
        auto plan=std::make_unique<Tile::Plan>();Tile::Statistics building;
        std::vector<double> coordinates;const auto count=scenario==0?512U:31U;
        const auto start=scenario==0?135.5:scenario==1?std::nextafter(405.,0.):95968.25;
        for(unsigned n=0;n<count;++n)coordinates.push_back(static_cast<double>(
            static_cast<long double>(start)+n*(scenario==0?1.L:1.L+3e-6L)));
        check(plan->build_original(coordinates,code.symbol_samples(),code.chip_samples(),building),"bounded Plan rejected");
        const std::uint64_t phase=scenario*17U,first_chip=scenario*code.chips_per_symbol();
        const Tile::Lane lane{&code,&c,phase,first_chip,plan->first_local};
        auto scalar=std::make_unique<Tile>(),legacy=std::make_unique<Tile>(),automatic=std::make_unique<Tile>();
        check(scalar->gather_borrowed({&lane,1},*plan) && scalar->evaluate(Tile::Backend::scalar),"scalar oracle unavailable");
        check(legacy->gather_borrowed({&lane,1},*plan) && legacy->evaluate(Tile::Backend::sse2),"legacy tail unavailable");
        check(automatic->gather_borrowed({&lane,1},*plan) && automatic->evaluate(Tile::Backend::automatic),"automatic unavailable");
        const bool native=Tile::component_avx2_available();
        check((automatic->evaluated_backend()==Tile::Backend::avx2)==native,"automatic backend witness wrong");
        check(scalar->statistics.value_pairs==automatic->statistics.value_pairs &&
              scalar->statistics.prepares==automatic->statistics.prepares,"AVX2 added private callbacks");
        check(scalar->statistics.output_pairs==automatic->statistics.output_pairs &&
              scalar->statistics.shape_terms==automatic->statistics.shape_terms,"AVX2 changed output/term count");
        check(automatic->statistics.packed_doubles==scalar->statistics.packed_doubles+
              (native?4*plan->active_taps.count():0),"AoS pack work omitted or duplicated");
        std::unique_ptr<Tile> forced;
        if(native) {
            forced=std::make_unique<Tile>();check(forced->gather_borrowed({&lane,1},*plan)&&
                forced->evaluate(Tile::Backend::avx2),"explicit AVX2 unavailable");avx_rows+=count;
        } else {
            const auto prior=automatic->raw(0,0);bool rejected=false;
            try{(void)automatic->evaluate(Tile::Backend::avx2);}catch(const Error&){rejected=true;}
            const auto after=automatic->raw(0,0);
            check(rejected && bits(prior[0],after[0])&&bits(prior[1],after[1])&&automatic->samples()==count,
                  "unsupported explicit AVX2 mutated prior output");
        }
        original.set_stream_phase_samples(phase);original.prepare_symbol(first_chip);
        std::array<CorrelationFit,2> expected{},actual{};
        for(unsigned n=0;n<count;++n) {
            const auto s=scalar->raw(n,0),l=legacy->raw(n,0),v=automatic->raw(n,0);
            const auto reference=original.shaped_values(first_chip,coordinates[n]);
            const auto f=forced?forced->raw(n,0):v;
            const auto carrier=std::polar(1.,.317*n);const double x=static_cast<float>(.6*std::sin(.27*n));
            const CorrelationProjection p{x*carrier.real(),x*carrier.imag(),carrier.real()*carrier.real(),
                carrier.imag()*carrier.imag(),carrier.real()*carrier.imag(),x*x};
            const double gain=n<4?.25+.1875*n:1.;
            for(unsigned bit=0;bit<2;++bit) {
                check(bits(s[bit],l[bit])&&bits(s[bit],v[bit])&&bits(s[bit],f[bit])&&bits(s[bit],reference[bit]),
                      "ordered component raw sum changed original bits");
                const auto a=gain*pattern_limit_pcm(2.2*v[bit])/2.2;
                const auto b=gain*pattern_limit_pcm(2.2*reference[bit])/2.2;
                check(bits(a,b),"original limiter changed bits");
                CorrelationFit ca,cb;ca.add(p,a,1);cb.add(p,b,1);merge(actual[bit],ca);merge(expected[bit],cb);
            }
        }
        for(unsigned bit=0;bit<2;++bit)fit_equal(actual[bit],expected[bit]);
        automatic->detach_evaluated_plan();scalar->detach_evaluated_plan();legacy->detach_evaluated_plan();
        if(forced)forced->detach_evaluated_plan();
        const auto retained=automatic->raw(0,0);plan.reset();
        const auto detached=automatic->raw(0,0);check(bits(retained[0],detached[0])&&bits(retained[1],detached[1]),"detach lost output");
        check(!automatic->evaluate(Tile::Backend::automatic),"detached numeric pointer reused dead Plan");inaccessible(*automatic);
    }
    return avx_rows;
}
void zeros_and_cancel() {
    const auto c=configuration();PatternCode code(c,c.stream_epoch);
    auto plan=std::make_unique<Tile::Plan>();plan->valid=true;plan->samples=17;
    plan->symbol_samples=code.symbol_samples();plan->chip_samples=code.chip_samples();
    check(plan->well_formed(),"zero-atom Plan malformed");
    const Tile::Lane lane{&code,&c,0,0,0};auto tile=std::make_unique<Tile>();
    check(tile->gather_borrowed({&lane,1},*plan)&&tile->evaluate(Tile::Backend::automatic),"zero-atom contraction failed");
    check(tile->statistics.value_pairs==0 && tile->statistics.shape_terms==0&&tile->statistics.packed_doubles==0,
          "zero atoms added private lookup or copied coefficients");
    for(std::size_t n=0;n<17;++n)for(const auto value:tile->raw(n,0))
        check(bits(value.real(),0.)&&bits(value.imag(),0.),"zero atoms lost positive-zero sums");
    tile->detach_evaluated_plan();
    std::array<double,31> coordinates{};for(unsigned n=0;n<coordinates.size();++n)coordinates[n]=135.5+n;
    Tile::Statistics building;check(plan->build_original(coordinates,code.symbol_samples(),code.chip_samples(),building),"cancel Plan rejected");
    Tile::Lane active=lane;active.first_local=plan->first_local;
    check(tile->gather_borrowed({&active,1},*plan),"cancel gather failed");std::stop_source cancel;cancel.request_stop();
    bool stopped=false;try{tile->evaluate(Tile::Backend::automatic,cancel.get_token());}catch(const Error&){stopped=true;}
    check(stopped,"auto packing/evaluate ignored cancellation");inaccessible(*tile);
    check(tile->gather_borrowed({&active,1},*plan)&&tile->evaluate(Tile::Backend::automatic),"retry after cancellation failed");
    check(!Tile::component_avx2_available()||tile->evaluated_backend()==Tile::Backend::avx2,"retry skipped native backend");
    const auto rounding=std::fegetround();check(std::fesetround(FE_UPWARD)==0,"cannot set unsupported rounding");
    const bool unsafe=tile->evaluate(Tile::Backend::automatic);
    check(std::fesetround(rounding)==0,"cannot restore rounding");
    check(!unsafe,"unsupported arithmetic entered AVX2 output path");inaccessible(*tile);
}
}
int main(){try {
    if(!Tile::arithmetic_supported()) {std::cout<<"component numeric coverage omitted: original arithmetic unavailable\n";return 77;}
    const auto rows=numerical_oracles();zeros_and_cancel();
    std::cout<<"component ordered FP64 oracles passed; avx_rows="<<rows<<" compiled="<<Tile::component_avx2_compiled
        <<" payload_bytes="<<Tile::component_storage_bytes<<" tile_bytes="<<sizeof(Tile)
        <<" callee_allowance="<<Tile::external_callee_allowance<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

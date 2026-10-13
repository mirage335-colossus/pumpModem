// Root owns compilation/execution. This artifact has not been run by its author.
#include "../src/scheduled_ordered_pulse_candidates.hpp"
#include "datapump/sched_hop.hpp"
#include <iostream>
#include <memory>
#include <type_traits>

using namespace datapump;
using namespace datapump::modem;
using namespace datapump::modem::detail;
using Owned=ScheduledOrderedPulseCandidatesT<2,512,96>;
using Borrowed=ScheduledOrderedPulseCandidatesT<2,512,96,false>;
static_assert(std::is_copy_constructible_v<Owned> && std::is_move_constructible_v<Owned>);
static_assert(!std::is_copy_constructible_v<Borrowed> && !std::is_move_constructible_v<Borrowed>);
static_assert(sizeof(Owned)>sizeof(Borrowed) &&
    sizeof(Owned)-sizeof(Borrowed)+128>=sizeof(Borrowed::Plan),
    "borrowed Tile retained a hidden full Plan");
static_assert(noexcept(std::declval<Borrowed&>().reset_binding()) &&
    noexcept(std::declval<Borrowed&>().detach_evaluated_plan()));
namespace {
void check(bool ok,const char* message){if(!ok)throw Error(message);}
bool bits(double a,double b){return std::bit_cast<std::uint64_t>(a)==std::bit_cast<std::uint64_t>(b);}
bool bits(std::complex<double> a,std::complex<double> b){return bits(a.real(),b.real())&&bits(a.imag(),b.imag());}
Config configuration() {
    Config c;c.sample_rate=6000;c.carrier_hz=1500;c.bandwidth_hz=8;c.integration_seconds=16;
    c.pattern_symbols=true;c.constellation_bits=1;c.spreading_factor=64;c.scramble=c.dsss=true;c.dsss_factor=100;
    c.outer_dsss_version=OuterDsssVersion::interleaved_v2;c.data_key.emplace(Bytes(32,0x67));
    for(std::size_t i=0;i<c.spreading_seed.size();++i){c.spreading_seed[i]=3*i+7;c.dsss_seed[i]=5*i+11;}
    c.scheduled_hopping=std::make_shared<SchedHopPlan>(SchedHopPlan{10,9});c.stream_epoch=1799999967;return c;
}
template<class Tile>void inaccessible(const Tile& tile) {
    bool threw=false;try{(void)tile.raw(0,0);}catch(const Error&){threw=true;}
    check(threw,"invalidated borrowed output remained readable");
    check(tile.samples()==0 && tile.lanes()==0,"failed/reset borrow retained sample/lane metadata");
    check(tile.binding(0).code==nullptr && tile.binding(0).original_config==nullptr,
          "failed/reset borrow retained private context pointers");
}
void run() {
    auto c=configuration();PatternCode code0(c,c.stream_epoch),code1(c,c.stream_epoch),reference(c,c.stream_epoch);
    check(code0.chip_samples()==15,"borrow test lost original D100/C15 geometry");
    auto owned=std::make_unique<Owned>();auto borrowed=std::make_unique<Borrowed>();
    auto own_plan=std::make_unique<Owned::Plan>();auto borrow_plan=std::make_unique<Borrowed::Plan>();
    std::array<double,512> at{};
    for(std::size_t n=0;n<at.size();++n)at[n]=static_cast<double>(135.5L+n*(1.L+3e-6L));
    Owned::Statistics own_work;Borrowed::Statistics borrow_work;
    check(own_plan->build_original(at,code0.symbol_samples(),code0.chip_samples(),own_work) &&
          borrow_plan->build_original(at,code0.symbol_samples(),code0.chip_samples(),borrow_work),
          "bounded original Plan build failed");
    const auto first=2*code0.chips_per_symbol();
    const std::array<Owned::Lane,2> own_lanes{{{&code0,&c,0,0,own_plan->first_local},
        {&code1,&c,17,first,own_plan->first_local}}};
    const std::array<Borrowed::Lane,2> borrow_lanes{{{&code0,&c,0,0,borrow_plan->first_local},
        {&code1,&c,17,first,borrow_plan->first_local}}};
    check(!borrowed->gather(borrow_lanes,*borrow_plan),"false Tile silently borrowed through owned gather API");
    inaccessible(*borrowed);
    check(owned->gather(own_lanes,*own_plan),"default owned gather unavailable");
    *own_plan={};own_plan.reset(); // default gather must own an independent copy
    check(owned->samples()==512 && owned->evaluate(Owned::Backend::scalar),
          "owned gather still depended on caller's Plan");
    check(borrowed->gather_borrowed(borrow_lanes,*borrow_plan) && borrowed->samples()==512,
          "borrowed gather did not expose its pinned sample extent");
    check(borrowed->evaluate(Borrowed::Backend::sse2),"borrowed SSE2 evaluation unavailable");
    for(std::size_t lane=0;lane<2;++lane) {
        reference.set_stream_phase_samples(lane?17:0);reference.prepare_symbol(lane?first:0);
        for(std::size_t n=0;n<at.size();++n) {
            const auto a=owned->raw(n,lane),b=borrowed->raw(n,lane),r=reference.shaped_values(lane?first:0,at[n]);
            for(unsigned bit=0;bit<2;++bit)
                check(bits(a[bit],b[bit])&&bits(a[bit],r[bit]),"borrowed original-order private raw sum changed bits");
        }
    }
    // A copied owned Tile must evaluate its OWN embedded Plan after source death.
    auto copied=std::make_unique<Owned>(*owned);owned.reset();
    check(copied->evaluate(Owned::Backend::sse2),"owned Tile copy retained a self pointer into destroyed source");
    borrowed->detach_evaluated_plan();borrow_plan.reset();
    check(borrowed->samples()==512 && borrowed->lanes()==2,"detach lost evaluated scalar/output extent");
    for(std::size_t lane=0;lane<2;++lane) {
        borrowed->activate_lane(lane);
        for(std::size_t n=0;n<at.size();++n) {
            const auto a=copied->raw(n,lane),b=borrowed->raw(n,lane);
            for(unsigned bit=0;bit<2;++bit)check(bits(a[bit],b[bit]),"detached raw read released Plan memory");
        }
    }
    // Detached evaluation cannot dereference the now released Plan.
    check(!borrowed->evaluate(Borrowed::Backend::scalar),"detached Tile re-evaluated an unpinned Plan");
    borrowed->reset_binding();inaccessible(*borrowed);
    borrow_plan=std::make_unique<Borrowed::Plan>();
    check(borrow_plan->build_original(at,code0.symbol_samples(),code0.chip_samples(),borrow_work),"retry Plan failed");
    std::stop_source cancel;cancel.request_stop();bool stopped=false;
    try{(void)borrowed->gather_borrowed(borrow_lanes,*borrow_plan,cancel.get_token());}catch(const Error&){stopped=true;}
    check(stopped,"borrowed gather ignored cancellation");inaccessible(*borrowed);
    check(borrowed->gather_borrowed(borrow_lanes,*borrow_plan),"borrowed retry after gather cancellation failed");
    stopped=false;try{(void)borrowed->evaluate(Borrowed::Backend::scalar,cancel.get_token());}catch(const Error&){stopped=true;}
    check(stopped,"borrowed evaluation ignored cancellation");inaccessible(*borrowed);
    check(borrowed->gather_borrowed(borrow_lanes,*borrow_plan),"borrowed retry after evaluation cancellation failed");
    const auto original_round=std::fegetround();
    check(std::fesetround(FE_UPWARD)==0,"unable to install unsupported rounding mode");
    const auto unsupported=borrowed->evaluate(Borrowed::Backend::scalar);
    check(std::fesetround(original_round)==0,"unable to restore original rounding mode");
    check(!unsupported,"unsupported arithmetic consumed borrowed Plan");inaccessible(*borrowed);
    borrow_plan->valid=false;
    check(!borrowed->gather_borrowed(borrow_lanes,*borrow_plan),"malformed borrowed Plan accepted");
    borrow_plan.reset();inaccessible(*borrowed);
    std::cout<<"borrowed Plan original-order/lifetime checks passed; owned_bytes="<<sizeof(Owned)
        <<" borrowed_bytes="<<sizeof(Borrowed)<<'\n';
}
}
int main(){try{
    if(!Borrowed::arithmetic_supported()){
        std::cout<<"borrowed Plan numeric coverage omitted: native ordered arithmetic unavailable; original fallback retained\n";return 0;
    }
    run();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}

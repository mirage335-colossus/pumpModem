#include "shared_scheduled_capture.hpp"
#include <atomic>
#include <complex>
#include <exception>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace datapump::modem::detail::scheduled_capture;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F&& f) {
    bool rejected=false;try{f();}catch(const Error&){rejected=true;}
    require(rejected,"required rejection absent");
}
constexpr Identity identity{17,9};
Limits limits(std::size_t chunks=128,std::size_t consumers=128,std::size_t samples=64) {
    return {8*1024*1024,chunks,consumers,samples};
}
std::vector<float> samples(std::size_t count,std::uint64_t first=0) {
    std::vector<float> result(count);
    for(std::size_t i=0;i<count;++i)result[i]=float(int((first+i)%97)-48)*.03125f;
    return result;
}
std::vector<float> copy(const SharedScheduledCapture::RangeLease& lease) {
    std::vector<float> result;
    auto first=lease.first();
    lease.visit([&](const View& view){
        require(view.absolute_first==first,"borrow changed original coordinates");
        result.insert(result.end(),view.pcm.begin(),view.pcm.end());first+=view.pcm.size();
    });
    require(first==lease.end(),"borrow range incomplete");return result;
}
void small_feeds_shared_and_pinned() {
    SharedScheduledCapture owner(limits(),identity,100);
    auto input=samples(1000,100);
    for(std::size_t i=0;i<input.size();++i)owner.append_copy(100+i,std::span(input).subspan(i,1),identity);
    require(owner.accounting().chunks==17,"one-sample feeds allocated one descriptor per feed");
    std::vector<SharedScheduledCapture::Retention> banks;
    std::vector<SharedScheduledCapture::RangeLease> readers;
    const auto before=owner.accounting();
    for(unsigned bank=0;bank<93;++bank) {
        banks.push_back(owner.subscribe(100));readers.push_back(owner.borrow(101,997));
        require(copy(readers.back())==std::vector<float>(input.begin()+1,input.end()-2),"bank copy changed original PCM");
    }
    require(owner.accounting().reserved_bytes==before.reserved_bytes,"bank borrows duplicated PCM or uncharged owner metadata");
    auto future=owner.subscribe(100,true);
    future.certify({identity,1100,1100});
    for(auto& bank:banks)bank.retain_from(1100);
    require(owner.accounting().chunks==17,"retention floor overwrote pinned readers");
    readers.clear();
    require(owner.accounting().chunks==0,"retired readers prevented certified eviction");
    rejects([&]{owner.borrow(100,1);});
    rejects([&]{owner.subscribe(100);});
    auto next=samples(64,1100);owner.append_copy(1100,next,identity);
    require(copy(owner.borrow(1100,64))==next,"append after eviction changed coordinates");
}
void future_certificate_and_workspace() {
    SharedScheduledCapture owner(limits(2,4),identity);
    auto block=samples(64);owner.append_copy(0,block,identity);owner.append_copy(64,block,identity);
    auto bank=owner.subscribe(0);bank.retain_from(128);
    rejects([&]{owner.append_copy(128,block,identity);}); // no future-consumer certificate
    rejects([&]{owner.subscribe(64,true);});
    auto future=owner.subscribe(0,true);
    rejects([&]{future.retain_from(128);});
    rejects([&]{future.certify({{18,9},128,128});});
    rejects([&]{future.certify({{17,10},128,128});});
    rejects([&]{future.certify({identity,128,127});});
    rejects([&]{future.certify({identity,129,129});});
    future.certify({identity,64,128});
    rejects([&]{future.certify({identity,63,128});});
    owner.append_copy(128,block,identity);
    require(owner.accounting().retained_first==64,"future-consumer floor not respected");
    // Limit proof occurs before any payload allocation or coordinate change.
    SharedScheduledCapture metadata(limits(2,4),identity);
    const auto fixed=metadata.accounting().fixed_bytes;
    auto small=limits(2,4);small.workspace_bytes=fixed+64*sizeof(float)+128;
    SharedScheduledCapture bounded(small,identity);
    bounded.append_copy(0,block,identity);
    const auto frozen=bounded.accounting();
    rejects([&]{bounded.append_copy(64,block,identity);});
    require(bounded.accounting().captured_end==frozen.captured_end &&
            bounded.accounting().payload_bytes==frozen.payload_bytes,"workspace failure partially committed input");
    small.workspace_bytes=fixed-1;rejects([&]{SharedScheduledCapture impossible(small,identity);});
}
void discontinuity_revision_and_cancellation() {
    SharedScheduledCapture owner(limits(),identity,1000);auto block=samples(20);
    owner.append_copy(1000,block,identity);
    rejects([&]{owner.append_copy(1021,block,identity);});
    rejects([&]{owner.append_copy(1019,block,identity,{1,true,true});});
    rejects([&]{owner.append_copy(1020,block,{18,9});});
    rejects([&]{owner.append_copy(1020,{},identity,{5,true,false});});
    rejects([&]{owner.append_copy(1020,{},Identity{18,9});});
    owner.append_copy(1020,block,identity,{81,true,false});
    bool found=false;
    owner.borrow(1010,25).visit([&](const View& view){
        if(view.event_before.evidence==81) {found=true;require(view.event_coordinate==1020,"timing event coordinate lost");}
    });
    require(found,"contiguous timing discontinuity disappeared");
    owner.append_copy(1040,std::span(block).first(3),identity,{83,false,false});
    bool evidence_only=false;
    owner.borrow(1040,3).visit([&](const View& view){
        evidence_only|=view.event_before.evidence==83 && view.event_coordinate==1040;
    });
    require(evidence_only,"evidence-only timing boundary disappeared during coalescing");
    owner.append_copy(1050,block,identity,{82,true,true});
    rejects([&]{owner.borrow(1035,20);}); // missing input is not inserted zero observations
    require(copy(owner.borrow(1050,20))==block,"capture after explicit gap unavailable");
    auto current=owner.subscribe(1000);
    rejects([&]{owner.reset({18,10},2000);});
    current={};
    auto lease=owner.borrow(1050,20);
    rejects([&]{owner.reset({18,10},2000);});
    lease={};owner.reset({18,10},2000);
    require(owner.accounting().captured_end==2000 && owner.accounting().chunks==0,"retired capture reset failed");
    rejects([&]{owner.append_copy(2000,block,identity);});
    // Cancel/retire wrapper does not invalidate a queued worker's pinned span.
    SharedScheduledCapture::RangeLease surviving;
    { SharedScheduledCapture local(limits(),identity);local.append_copy(0,block,identity);surviving=local.borrow(0,20); }
    require(copy(surviving)==block,"owner retirement invalidated queued reader");
}
void overflow_and_transactional_cross_grid() {
    const auto maximum=std::numeric_limits<std::uint64_t>::max();
    SharedScheduledCapture owner(limits(),identity,maximum-20);auto block=samples(20);
    owner.append_copy(maximum-20,block,identity);
    require(copy(owner.borrow(maximum-20,20))==block,"last representable exclusive endpoint failed");
    rejects([&]{owner.append_copy(maximum,block,identity);});
    rejects([&]{owner.borrow(maximum-5,20);});
    rejects([&]{coordinate_product(maximum,2);});
    SharedScheduledCapture one(limits(1,2,64),identity,63);
    const auto before=one.accounting();
    rejects([&]{one.append_copy(63,samples(2),identity);});
    require(one.accounting().captured_end==before.captured_end && one.accounting().payload_bytes==0,
            "cross-grid capacity failure consumed prefix");
    auto nonfinite=samples(1);nonfinite[0]=std::numeric_limits<float>::infinity();
    rejects([&]{one.append_copy(63,nonfinite,identity);});
    rejects([&]{one.append_copy(63,samples(65),identity);});
    auto claim=one.subscribe(63);auto second=one.subscribe(63);rejects([&]{one.subscribe(63);});
}
void original_fences_and_physical_carrier() {
    OriginalCaptureFences fences(8);
    std::vector<std::pair<std::uint64_t,std::uint64_t>> original;
    std::uint64_t local=0;
    for(auto count:{512ULL,512ULL,1ULL,1ULL,511ULL,511ULL,7ULL}) {
        original.emplace_back(local,count);fences.append(local,count);local+=count;
    }
    require(fences.run_count()==4,"equal original fences not compressed");
    require(fences.replay_floor(1027)==1026,"fractional replay changed oscillator seed fence");
    const std::uint64_t absolute_origin=60013;
    auto input=samples(std::size_t(local),absolute_origin);
    SharedScheduledCapture owner(limits(128,8,64),identity,absolute_origin);
    for(std::size_t offset=0;offset<input.size();) {
        const auto count=std::min<std::size_t>(53,input.size()-offset);
        owner.append_copy(absolute_origin+offset,std::span(input).subspan(offset,count),identity);offset+=count;
    }
    // Exact original reference recurrence, including physical source gaps.  The
    // sharing layer does not dewarp time or reset oscillator at slab boundaries.
    constexpr double angular=.413752687310421;
    const auto step=std::polar(1.,angular);
    std::vector<std::complex<double>> original_mix(input.size()),shared_mix(input.size());
    for(auto [first,count]:original) {
        auto oscillator=std::polar(1.,std::remainder(double(first)*angular,6.283185307179586));
        for(std::uint64_t i=0;i<count;++i) {original_mix[first+i]=double(input[first+i])*oscillator;oscillator*=step;}
    }
    fences.visit(0,local,[&](OriginalCaptureFences::Fence fence){
        auto oscillator=std::polar(1.,std::remainder(double(fence.original_first)*angular,6.283185307179586));
        owner.borrow(absolute_origin+fence.original_first,std::size_t(fence.original_count)).visit([&](const View& view){
            for(std::size_t i=0;i<view.pcm.size();++i) {
                const auto index=view.absolute_first-absolute_origin+i;
                shared_mix[index]=double(view.pcm[i])*oscillator;oscillator*=step;
            }
        });
    });
    require(original_mix==shared_mix,"owner chunk clipping changed original physical oscillator recurrence");
    std::vector<OriginalCaptureFences::Fence> clipped;
    fences.visit(510,7,[&](auto f){clipped.push_back(f);});
    require(clipped.size()==2 && clipped[0].original_first==0 && clipped[0].original_count==512 &&
            clipped[0].selected_count==2 && clipped[1].original_first==512 && clipped[1].selected_count==5,
            "partial replay did not preserve full original fences");
    fences.discard_before(513);require(fences.replay_floor(513)==512,"discard dropped partial original fence");
    rejects([&]{fences.discard_before(local+1);});
    rejects([&]{fences.visit(511,2,[](auto){});});
    OriginalCaptureFences tiny(1);
    for(std::uint64_t i=0;i<10000;++i)tiny.append(i,1);
    require(tiny.run_count()==1,"one-sample original fences caused metadata explosion");
    rejects([&]{tiny.append(10000,2);});
    rejects([&]{OriginalCaptureFences insufficient(8,0,OriginalCaptureFences::required_bytes(8)-1);});
    OriginalCaptureFences edge(2,std::numeric_limits<std::uint64_t>::max()-1);
    edge.append(std::numeric_limits<std::uint64_t>::max()-1,1);
    rejects([&]{edge.append(std::numeric_limits<std::uint64_t>::max(),1);});
}
void concurrent_committed_prefix() {
    SharedScheduledCapture owner(limits(128,8,64),identity);
    auto input=samples(5000);owner.append_copy(0,std::span(input).first(17),identity);
    auto lease=owner.borrow(0,17);
    std::atomic<bool> started=false;
    std::exception_ptr failure;
    std::jthread producer([&]{try {
        started=true;
        for(std::size_t i=17;i<input.size();++i)owner.append_copy(i,std::span(input).subspan(i,1),identity);
    }catch(...){failure=std::current_exception();}});
    while(!started.load())std::this_thread::yield();
    for(unsigned repetition=0;repetition<1000;++repetition)
        require(copy(lease)==std::vector<float>(input.begin(),input.begin()+17),"append mutated pinned committed prefix");
    producer.join();if(failure)std::rethrow_exception(failure);
    require(copy(owner.borrow(0,input.size()))==input,"concurrent appended original PCM changed");
    OriginalCaptureFences fences(4);
    for(std::uint64_t i=0;i<17;++i)fences.append(i,1);
    std::jthread append_fences([&]{for(std::uint64_t i=17;i<1000;++i)fences.append(i,1);});
    for(unsigned iteration=0;iteration<100;++iteration) {
        std::size_t observed=0;
        fences.visit(0,17,[&](auto f){observed+=f.selected_count;});
        require(observed==17,"concurrent fence append changed original traversal");
    }
    append_fences.join();require(fences.run_count()==1,"concurrent equal fences did not remain bounded");
}
void opted_fence_growth() {
    OriginalCaptureFences fences(2);
    std::vector<OriginalCaptureFences::Fence> expected,actual;
    std::uint64_t end=0;unsigned growths=0;
    for(unsigned i=0;i<1024;++i) {
        const auto count=std::uint64_t{i%2?1U:10U};
        for(;;)try{fences.append(end,count);break;}
            catch(const OriginalCaptureFences::CapacityError&) {
                const auto old=fences.reserved_bytes();std::size_t replacement=0;
                require(fences.grow([&](std::size_t bytes){replacement=bytes;
                    require(fences.reserved_bytes()==old,"reserve recursively locked or double charged fences");
                }),"capture fence growth failed");
                require(fences.reserved_bytes()>old && fences.reserved_bytes()<old+replacement,
                    "capture replacement charged freed backing or omitted new bytes");++growths;
            }
        expected.push_back({end,count,end,count});end+=count;
    }
    // Reservation/accounting can be inspected inside a traversal callback.
    fences.visit(0,end,[&](auto f){require(fences.reserved_bytes()>0,"missing fence workspace");actual.push_back(f);});
    require(growths==9 && expected.size()==actual.size(),"alternating fences lost or duplicated metadata");
    for(std::size_t i=0;i<actual.size();++i)require(
        actual[i].original_first==expected[i].original_first && actual[i].original_count==expected[i].original_count &&
        actual[i].selected_first==expected[i].selected_first && actual[i].selected_count==expected[i].selected_count,
        "replacement changed oscillator seed or input boundaries");
    std::size_t expected_index=0;
    for(std::uint64_t p=0;p<end;++p) {
        while(p>=expected[expected_index].original_first+expected[expected_index].original_count)++expected_index;
        const auto seed=expected[expected_index].original_first;
        require(fences.replay_floor(p)==seed,"indexed seek changed a retained partial-fence seed");
        unsigned count=0;fences.visit(p,1,[&](auto f){++count;
            require(f.original_first==seed && f.selected_first==p && f.selected_count==1,
                "indexed one-sample visit lost original containing fence");});
        require(count==1,"indexed visit duplicated a half-open boundary");
    }
    const auto before=fences.reserved_bytes();
    rejects([&]{fences.grow([](auto){throw Error("workspace full");});});
    std::stop_source stop;
    require(!fences.grow([&](auto){stop.request_stop();},stop.get_token()),"cancelled capture replacement published");
    require(fences.reserved_bytes()==before && fences.run_count()==1024 && fences.replay_floor(5)==0,
        "failed/cancelled capture replacement mutated original history");
    OriginalCaptureFences ring(2);ring.append(0,10);ring.append(10,1);ring.discard_before(10);ring.append(11,2);
    require(ring.grow([](auto){}),"wrapped capture replacement failed");
    require(ring.replay_floor(12)==11,"wrapped capture replacement changed partial fence");
    std::vector<std::pair<std::uint64_t,std::uint64_t>> tail;
    ring.visit(10,3,[&](auto f){tail.emplace_back(f.original_first,f.original_count);});
    require(tail==std::vector<std::pair<std::uint64_t,std::uint64_t>>{{10,1},{11,2}},
        "wrapped capture replacement changed order");
}
}
int main() {
    try {
        small_feeds_shared_and_pinned();future_certificate_and_workspace();
        discontinuity_revision_and_cancellation();overflow_and_transactional_cross_grid();
        original_fences_and_physical_carrier();concurrent_committed_prefix();
        opted_fence_growth();
        std::cout<<"shared scheduled capture bounded ownership/fence tests passed\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

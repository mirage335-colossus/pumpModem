#include "datapump/recovery.hpp"
#include "datapump/boundary_sync.hpp"
#include "datapump/crypto.hpp"
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace datapump;
using namespace datapump::transfer;
namespace {
void check(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
Bytes area(std::size_t n) {Bytes out(n);for(std::size_t i=0;i<n;++i)out[i]=static_cast<std::uint8_t>(i*73+0xa7);return out;}
Bytes as_bits(const Bytes& bytes) {
    Bytes bits;for(auto byte:bytes)for(unsigned i=0;i<8;++i)bits.push_back((byte>>(7-i))&1U);return bits;
}
IntervalOptions keyed(std::uint64_t address) {
    Crypto key(Bytes(32,0x37));IntervalOptions result;result.fec=FecMode::rs60;
    result.authenticator=[key,address](const Bytes& data) {
        Bytes message;for(unsigned i=0;i<8;++i)message.push_back(static_cast<std::uint8_t>(address>>(56-8*i)));
        message.insert(message.end(),data.begin(),data.end());return key.mac(message);
    };
    result.verifier=[authenticate=result.authenticator](const Bytes& data,const Bytes& tag) {return authenticate(data)==tag;};
    return result;
}
RecoveryInput fixture(bool encrypted=false) {
    RecoveryInput input;input.first_symbol=100;input.fec=FecMode::rs60;
    input.interval_options=[encrypted](std::uint64_t start) {return encrypted?keyed(start):IntervalOptions{FecMode::rs60,{},{}};};
    input.established_starts={292};
    const auto options=input.interval_options(292);
    input.bits=boundary_sync::insert(as_bits(encode_interval(area(interval_data_bytes(input.fec,encrypted)),options)));
    return input;
}
void sparse_unknowns_and_threads() {
    for(const bool encrypted:{false,true})for(const unsigned reserved:{0U,2U}) {
        auto input=fixture(encrypted);
        for(unsigned i=0;i<49;++i)input.bits[192+i*8]=2;
        if(reserved) {input.bits[192+100*8]^=1;input.bits[192+101*8]^=1;}
        RecoveryOptions options;options.extra_errors=reserved;options.workers=1;
        RecoveryJob one(input,options);one.run();
        check(one.progress().state==RecoveryState::recovered,"sparse unknowns beyond byte-erasure capacity must recover");
        check(one.progress().attempts==one.progress().total,"recovery must exhaust every assignment");
        const auto result=one.result();
        check(result.size()==1 && result[0].data==area(interval_data_bytes(input.fec,encrypted)),"recovery returns exact opaque source area");
        check(result[0].authenticated==encrypted,"authentication must retain public/keyed distinction");
        check(result[0].fec_stats.data.missing_bits+result[0].fec_stats.integrity.missing_bits==49,
              "guessed bits must still count as originally missing");
        options.workers=4;RecoveryJob many(input,options);many.run();
        check(many.progress().state==one.progress().state && many.progress().attempts==one.progress().attempts &&
              many.result()[0].data==result[0].data,"worker scheduling must not change outcome or coverage");
        check(many.working_bytes()<=many.workspace_bound(),"workspace reservation must cover retained allocations");
    }
}
void resume_and_cancel() {
    auto input=fixture();for(unsigned i=0;i<49;++i)input.bits[192+i*8]=2;
    RecoveryOptions options;options.workers=1;options.extra_errors=2;
    std::stop_source stop;bool requested=false;
    input.interval_options=[&](std::uint64_t) {
        if(!requested){requested=true;stop.request_stop();}
        return IntervalOptions{FecMode::rs60,{},{}};
    };
    RecoveryJob job(input,options);job.run(stop.get_token());
    check(job.progress().state==RecoveryState::cancelled && job.progress().attempts>0 &&
          job.progress().attempts<job.progress().total && job.result().empty(),"cancel must preserve an unfinished search without accepting provisional matches");
    const auto before=job.progress().attempts;job.run();
    check(job.progress().state==RecoveryState::recovered && job.progress().attempts==job.progress().total &&
          job.progress().attempts>before,"cancelled search must resume its untested assignments");
    input.interval_options=[](std::uint64_t) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));return IntervalOptions{FecMode::rs60,{},{}};
    };
    options.budget=std::chrono::milliseconds(1);RecoveryJob timed(input,options);timed.run();
    check(timed.progress().state==RecoveryState::incomplete && timed.result().empty(),"deadline is incomplete, not exhaustion or recovery");
    for(unsigned i=0;i<20 && timed.progress().state==RecoveryState::incomplete;++i)timed.run();
    check(timed.progress().state==RecoveryState::recovered,"deadline-limited batches must resume to the same exhaustive result");
}
void substantial_exhaustive_search() {
    auto input=fixture(true);
    for(unsigned i=0;i<57;++i)input.bits[192+i*8]=2;
    RecoveryOptions options;options.workers=4;
    RecoveryJob job(std::move(input),options);job.run();
    const auto progress=job.progress();const auto decoded=job.result();
    check(progress.state==RecoveryState::recovered && progress.total==8192 && progress.attempts==8192,
          "hard-bit recovery must exhaust all 8192 assignments without an artificial small attempt cap");
    check(decoded.size()==1 && decoded[0].authenticated && decoded[0].data==area(48),
          "large exhaustive search must recover the exact authenticated source area");
    check(decoded[0].fec_stats.data.missing_bits+decoded[0].fec_stats.integrity.missing_bits+
          decoded[0].fec_stats.parity.missing_bits==57,
          "all originally missing bits must remain missing in exhaustive-search diagnostics");
}
void alignment_and_authentication() {
    auto input=fixture(true);input.established_starts.clear();
    input.bits.erase(input.bits.begin(),input.bits.begin()+192);input.first_symbol+=192;
    RecoveryOptions options;options.workers=3;options.extra_errors=0;
    RecoveryJob recovered(input,options);recovered.run();
    check(recovered.progress().state==RecoveryState::recovered && recovered.result()[0].authenticated,
          "missing marker must permit authenticated exhaustive alignment recovery at its real address");
    ++input.first_symbol;RecoveryJob shifted(input,options);shifted.run();
    check(shifted.progress().state==RecoveryState::exhausted && shifted.result().empty(),"valid RS at wrong canonical address must fail authentication");
    input.bits.assign(1024,0);input.first_symbol=0;input.interval_options=[](auto){return IntervalOptions{FecMode::rs60,{},{}};};
    RecoveryJob ambiguous(input,options);ambiguous.run();
    check(ambiguous.progress().state==RecoveryState::ambiguous && ambiguous.result().empty(),"competing credible public alignments must reject");
}
void chain_and_observation_constraints() {
    auto input=fixture();const auto interval=input.bits;
    input.bits.insert(input.bits.end(),interval.begin(),interval.end());
    input.bits.insert(input.bits.end(),interval.begin(),interval.end());
    input.established_starts={100+2*1216+192};
    RecoveryOptions options;options.workers=2;options.extra_errors=0;
    RecoveryJob complete(input,options);complete.run();
    check(complete.progress().state==RecoveryState::recovered && complete.result().size()==3,
          "later anchor must require every captured interval on its fixed cadence");
    auto unanchored=input;unanchored.established_starts.clear();
    RecoveryJob searched(unanchored,options);searched.run();
    check(searched.progress().state==RecoveryState::recovered && searched.result().size()==3,
          "later intervals on the same cadence cannot veto their own initial alignment");
    auto marker_tail=fixture();marker_tail.bits.insert(marker_tail.bits.end(),interval.begin(),interval.begin()+192);
    RecoveryJob trailer(marker_tail,options);trailer.run();
    check(trailer.progress().state==RecoveryState::recovered && trailer.result().size()==1,
          "marker-only trailer must not manufacture a wholly unobserved coding interval");
    std::fill(input.bits.begin()+1216+192,input.bits.begin()+2*1216,2);
    RecoveryJob hole(input,options);hole.run();
    check(hole.progress().state==RecoveryState::exhausted && hole.result().empty(),"a missing middle interval cannot disappear");
    input=fixture();input.bits[192]=2;input.bits[193]^=1;
    RecoveryJob partial_error(input,options);partial_error.run();
    check(partial_error.progress().state==RecoveryState::exhausted,"public evidence rule must reject altered known bits inside a partial byte");
    input=fixture();options.retained_bits=100;RecoveryJob limited(input,options);limited.run();
    check(limited.progress().state==RecoveryState::unavailable,"capture exceeding retention quota is unavailable, never a recovered suffix");
    options.retained_bits=65536;options.workspace_bytes=1024;RecoveryJob workspace(input,options);workspace.run();
    check(workspace.progress().state==RecoveryState::unavailable,"insufficient workspace must reject before unbounded allocation");
    options.workspace_bytes=16*1024*1024;options.extra_errors=24;
    std::fill(input.bits.begin()+192,input.bits.begin()+192+112,2);
    RecoveryJob overflow(input,options);overflow.run();
    check(overflow.progress().state==RecoveryState::unavailable && overflow.result().empty(),
          "an unrepresentable exhaustive assignment domain cannot accept a provisional candidate");
}
void planning_workspace_reservation() {
    RecoveryInput input;input.bits.assign(32769,2);
    input.interval_options=[](auto){return IntervalOptions{FecMode::rs60,{},{}};};
    RecoveryOptions options;options.workers=1;options.budget=std::chrono::milliseconds(1);
    RecoveryJob estimate(input,options);options.workspace_bytes=estimate.workspace_bound();
    RecoveryJob job(std::move(input),options);
    std::stop_source stopped;stopped.request_stop();job.run(stopped.get_token());
    check(job.progress().state==RecoveryState::cancelled && job.progress().attempts==0,
          "pre-cancelled planning must stay resumable without evaluating candidates");
    const auto reserved=job.working_bytes();
    check(reserved<=job.workspace_bound() && job.workspace_bound()<=options.workspace_bytes,
          "power-of-two boundary capture must fit its tight stable workspace reservation");
    job.run();
    check(job.progress().state==RecoveryState::incomplete && job.result().empty(),
          "large plan preparation must honor its deadline without launching an exhaustive search");
    check(job.working_bytes()==reserved,
          "resumed plan preparation must use its original allocation without geometric vector growth");
    for(const auto budget:{std::chrono::milliseconds::max(),std::chrono::milliseconds::min()}) {
        options.budget=budget;RecoveryJob invalid(fixture(),options);invalid.run();
        check(invalid.progress().state==RecoveryState::unavailable && !invalid.progress().attempts && invalid.result().empty(),
              "unrepresentable deadline must reject without overflowing clock arithmetic");
    }
}
void guarded_capture_boundaries() {
    RecoveryOptions options;options.workers=1;options.extra_errors=0;
    for(const auto missing:{1U,7U,8U,9U}) {
        auto input=fixture(true);input.bits.resize(input.bits.size()-missing);
        RecoveryJob job(std::move(input),options);job.run();
        const auto result=job.result();
        check(job.progress().state==RecoveryState::recovered && result.size()==1 &&
              result[0].data==area(48),"bounded tail loads must retain exact partial-byte recovery");
        const auto& stats=result[0].fec_stats;
        check(stats.data.missing_bits+stats.integrity.missing_bits+stats.parity.missing_bits==missing,
              "guarded fallback loads must not turn absent tail bits into observed input");
    }
    for(const auto invalid:{std::uint8_t{3},std::uint8_t{255}}) {
        for(const auto position:{std::size_t{0},std::size_t{192},std::size_t{1215}}) {
            auto input=fixture();input.bits[position]=invalid;
            RecoveryJob job(std::move(input),options);job.run();
            check(job.progress().state==RecoveryState::unavailable && !job.progress().attempts && job.result().empty(),
                  "out-of-domain recovery bits reject before bounded fallback loads");
        }
    }
    auto input=fixture();input.bits.clear();
    RecoveryJob empty(std::move(input),options);empty.run();
    check(empty.progress().state==RecoveryState::unavailable && !empty.progress().attempts && empty.result().empty(),
          "empty recovery capture rejects without a fallback element");
    input=fixture();input.established_starts.clear();input.bits.resize(1);
    RecoveryJob single(std::move(input),options);single.run();
    check(single.progress().state==RecoveryState::exhausted && !single.progress().attempts && single.result().empty(),
          "single observed bit cannot manufacture a recoverable interval from bounded tail loads");
}
}
int main() {
    try {sparse_unknowns_and_threads();resume_and_cancel();substantial_exhaustive_search();alignment_and_authentication();chain_and_observation_constraints();planning_workspace_reservation();guarded_capture_boundaries();
        std::cout<<"post-end hard-bit recovery passed\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

#include "../src/live_receptions.hpp"
#include "../src/live_reception_support.hpp"
#include "../src/live_source_handoff.hpp"
#include "../src/live_receiver_layout.hpp"
#include "../src/live_deferred_pattern_history.hpp"
#include "datapump/pattern_receiver.hpp"
#include "datapump/pattern_code.hpp"
#include <iostream>
#include <array>
#include <limits>
#include <stdexcept>

namespace {
using History = datapump::live::detail::ReceptionHistory;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
History::Candidate candidate(std::uint64_t id, std::uint64_t first, std::uint64_t end, double score) {
    History::Candidate value;
    value.id=id;value.family=1;value.first=first;value.end=end;
    value.frequency=1500;value.frequency_tolerance=10;value.absence_samples=6000;
    value.score=score;return value;
}
void source_address_sequence_geometry() {
    using datapump::live::detail::ReceptionSourcePublication;
    using datapump::live::detail::reception_source_conflict;
    // Complete small modular periods, including T shorter/longer than one
    // second. The oracle is the actual Data address helper, not test payload.
    for(std::uint32_t fs=2;fs<=10;++fs)for(std::uint64_t t=1;t<=2*fs;++t)
        for(std::uint32_t a=0;a<fs;++a)for(std::uint32_t b=0;b<fs;++b)
            for(std::uint64_t n=1;n<=2*fs;++n) {
                ReceptionSourcePublication x,y;
                x.epoch=y.epoch=100;x.phase=a;y.phase=b;
                x.sample_rate=y.sample_rate=fs;x.symbol_samples=y.symbol_samples=t;
                bool different=false;
                for(std::uint64_t i=0;i<n;++i) {
                    const auto p=datapump::modem::symbol_stream_address(100,a,i,t,fs);
                    const auto q=datapump::modem::symbol_stream_address(100,b,i,t,fs);
                    different|=p.epoch!=q.epoch || p.ordinal!=q.ordinal;
                }
                const auto result=reception_source_conflict(x,y,n);
                check(result && *result==different,"closed-form Data-address sequence disagrees with actual address helper");
            }
    ReceptionSourcePublication a,b;
    a.epoch=b.epoch=1792949976;a.phase=0;b.phase=4080;
    a.sample_rate=b.sample_rate=6000;a.symbol_samples=b.symbol_samples=15360;
    check(reception_source_conflict(a,b,1)==false && reception_source_conflict(a,b,2)==true,
          "actual phase4080 alias did not preserve first Data epoch and conflict at second");
    a.epoch=b.epoch=UINT64_MAX;
    a.phase=b.phase=0;a.symbol_samples=b.symbol_samples=3;
    a.sample_rate=b.sample_rate=10;
    check(!reception_source_conflict(a,b,UINT64_MAX),"overflowing Data-address sequence acquired equality authority");
    a.symbol_samples=b.symbol_samples=30;a.sample_rate=b.sample_rate=10;
    check(!reception_source_conflict(a,b,2),"T>=Fs equality skipped overflowing endpoint validation");
    b=a;b.sample_rate=0;check(!reception_source_conflict(a,b,1),"invalid source rate acquired address authority");
}
void helper_source_handoff() {
    using datapump::live::detail::ReceptionSourcePublication;
    using datapump::live::detail::reception_source_handoff_allowed;
    constexpr std::uint64_t t=15360;
    const auto publication=[](std::uint32_t phase,std::uint64_t count,std::uint64_t first,
                             std::uint64_t end,double raw,double score) {
        ReceptionSourcePublication p;p.epoch=1792949976;p.phase=phase;p.count=count;
        p.sample_rate=6000;p.symbol_samples=t;p.first=first;p.end=end;p.raw_support=raw;
        p.support=score;p.timing_tolerance=24;p.helper_assisted=true;return p;
    };
    // Values from the actual failed GPS/helper001 trace: c7's already exposed
    // correct2 prefix, c2's same-birth weak middle plus strong third, then c3.
    auto old=publication(0,2,86497,117277,30780,30720);
    auto wrong=publication(4080,3,86488,132628,31495.7,31373.17606067473);
    // Derive the raw total from the actually logged normalized candidate score.
    wrong.raw_support=wrong.support*(wrong.end-wrong.first)/(3.*t);
    auto correct=publication(0,3,86512,132652,46140,46080);
    // Controlled exact pre-append evidence: the middle symbol is weak and
    // the following symbol is strong. Total geometry/score is actual traced;
    // this split exercises the native checkpoint contract, not recorded data.
    datapump::modem::PatternPrefixSupport wrong_prefix{wrong.first,3,2,117248,0,16000};
    datapump::modem::PatternPrefixSupport correct_prefix{correct.first,2,2,117292,5280,30780};
    History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto fit=[](std::uint64_t id,const ReceptionSourcePublication& p) {
        auto c=candidate(id,p.first,p.end,p.support);c.family=0;c.symbol_samples=t;
        c.timing_tolerance=24;c.helper_assisted=true;return c;
    };
    auto established=fit(7,old);const auto first=h.observe(established,allocate);
    auto alias=fit(2,wrong);alias.signal_id=first.signal_id;
    const auto eligible=[&](const ReceptionSourcePublication& incoming,
                            std::optional<datapump::modem::PatternPrefixSupport> prefix) {
        return [&,incoming,prefix](const auto&,std::uint64_t id,std::span<const std::uint64_t>,bool completed) {
            check(id==first.signal_id,"source guard was applied to an unrelated reception");
            if(completed)return true;
#if defined(DATAPUMP_SOURCE_HANDOFF_COUNTERFACTUAL)
            (void)incoming;(void)prefix;return true;
#else
            return reception_source_handoff_allowed(old,incoming,prefix).value_or(true);
#endif
        };
    };
    auto provisional=alias;provisional.complete=false;
    const auto attempted=h.observe(provisional,allocate,eligible(wrong,wrong_prefix));
    check(!attempted.selected && attempted.signal_id==first.signal_id,
          "weak differently-addressed source reinterpreted an established pending prefix");
    const auto final_attempt=h.observe(alias,allocate,eligible(wrong,wrong_prefix));
    check(!final_attempt.selected && final_attempt.revision==first.revision,
          "rejected source alias changed provisional ownership or revision");
    auto right=fit(3,correct);right.signal_id=first.signal_id;
    const auto corrected=h.observe(right,allocate,eligible(correct,correct_prefix));
    check(corrected.selected && corrected.signal_id==first.signal_id,
          "canonical alias could not expose the actual valid final bit");
    right.complete=true;
    check(h.observe(right,allocate,eligible(correct,correct_prefix)).selected,
          "source ownership guard suppressed the owner's physical END");
    // A consistent-source extension may have arbitrarily weak new-last-bit
    // support. It must not be subjected to the conservative tail subtraction.
    auto weak_last=correct;weak_last.raw_support=30780+10;
    weak_last.support=weak_last.raw_support/(weak_last.end-weak_last.first)*3.*t;
    check(reception_source_handoff_allowed(old,weak_last,correct_prefix)==true,
          "consistent-source weak final bit was delayed by a prefix support lower bound");
    // A genuinely stronger source can correct a weak false early address.
    auto false_early=old;false_early.phase=4080;false_early.support=1000;
    false_early.raw_support=1000*(false_early.end-false_early.first)/(2.*t);
    check(reception_source_handoff_allowed(false_early,correct,correct_prefix)==true,
          "strong source correction was frozen behind a weak false early prefix");
    // A FULL false early source must not freeze a full correct source, or a
    // correct full prefix with a weak final symbol. Actual prefix support ties
    // honestly; no B>T tail ceiling or total deficit can destroy that tie.
    auto full_false=old;full_false.phase=4080;
    check(reception_source_handoff_allowed(full_false,correct,correct_prefix)==true &&
          reception_source_handoff_allowed(full_false,weak_last,correct_prefix)==true,
          "full false early source froze a correct growing prefix or weak final bit");
    auto absent=correct_prefix;absent.prefix_symbols=1;
    check(!reception_source_handoff_allowed(full_false,correct,absent),
          "a different prefix count was extrapolated into handoff authority");
    auto ordinary=old;ordinary.helper_assisted=false;auto ordinary_new=wrong;ordinary_new.helper_assisted=false;
    check(!reception_source_handoff_allowed(ordinary,ordinary_new,wrong_prefix),
          "ordinary helper-off policy was given a source handoff restriction");
    check(!reception_source_handoff_allowed(old,wrong,{}),
          "unknown support geometry silently froze a candidate");
    auto later=wrong;later.first+=t;later.end+=t;
    check(!reception_source_handoff_allowed(old,later,wrong_prefix),
          "later independent physical source was treated as same-birth handoff");
    auto shorter=wrong;shorter.symbol_samples=t/2;
    check(!reception_source_handoff_allowed(old,shorter,wrong_prefix),
          "source guard changed competing profile policy");
    // The tagged union replaces the existing inactive Burst storage exactly;
    // ordinary construction allocates nothing and Burst coordinates survive.
    using Presentation=datapump::live::detail::ReceiverState::Presentation;
    Presentation display;
    display.coordinates.burst.first=datapump::modem::SymbolBurstCoordinate{100,3};
    check(display.coordinates.burst.first->epoch==100 && !display.source_publication(),
          "ordinary source storage affected Burst coordinate ownership");
    display.source_publication(old);
    check(display.source_publication() && display.source_publication()->count==2,
          "source metadata did not activate its existing-coordinate union");
    display.clear_source_publication();
    check(display.source_publication() && !display.source_publication()->count,
          "source metadata invalidation retained authority");
}
void helper_missing_prefix_handoff() {
    using datapump::live::detail::ReceptionSourcePublication;
    using datapump::live::detail::reception_first_missing_slot;
    using datapump::live::detail::reception_source_gap_withdraws_prefix;
    using datapump::live::detail::nominal_reception_support;
    constexpr std::uint64_t t=15360;
    // R13 Default-clock trace, seq983 ->1065/1067: the true source-zero
    // owner exposed two known bits. A differently addressed middle slot then
    // became unknown while its found third symbol raised normalized support.
    ReceptionSourcePublication published;
    published.epoch=1792949971;published.phase=0;published.count=2;
    published.first=56484;published.end=87264;published.symbol_samples=t;
    published.sample_rate=6000;published.timing_tolerance=120;
    published.support=30720;published.helper_assisted=true;
    datapump::modem::PatternBurst gap,tail;
    gap.stream_first_sample=tail.stream_first_sample=27540;
    gap.stream_first_symbol=tail.stream_first_symbol=1;
    gap.first_stream_symbol=2;gap.end_sample=58260;gap.missing_slots=1;
    tail.first_stream_symbol=3;tail.end_sample=73680;tail.bits={1};
    gap.support_samples=tail.support_samples=30840;
    std::array drained{gap,tail};
    const auto first_missing=reception_first_missing_slot(drained,27540,1);
    const auto support=nominal_reception_support(gap,drained,t);
    check(first_missing==1 && support && std::abs(*support-30799.895968790635)<1e-8,
          "R13 explicit middle gap or actual competing support changed");
    const auto fit=[](std::uint64_t id,std::uint64_t first,std::uint64_t end,double score) {
        auto c=candidate(id,first,end,score);c.family=0;c.symbol_samples=c.absence_samples=t;
        c.timing_tolerance=120;c.helper_assisted=true;return c;
    };
    auto clean=fit(4,56484,87264,published.support);
    auto gapped=fit(1,56492,102632,*support);
    const auto eligible=[&](std::uint64_t missing,std::uint64_t owner=4) {
        return [&,missing,owner](const auto& incoming,std::uint64_t,std::span<const std::uint64_t>,bool completed) {
            if(completed || incoming.id==owner)return true;
#if !defined(DATAPUMP_SOURCE_GAP_COUNTERFACTUAL)
            if(incoming.uniform_source && reception_source_gap_withdraws_prefix(published,incoming.first,
                incoming.symbol_samples,incoming.timing_tolerance,incoming.helper_assisted,missing))return false;
#else
            (void)missing;
#endif
            return true; // Unavailable optional source proof retains ordinary arbitration.
        };
    };
    History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto first=h.observe(clean,allocate);
    const auto attempted=h.observe(gapped,allocate,eligible(first_missing));
    check(!attempted.selected && attempted.signal_id==first.signal_id && attempted.revision==first.revision,
          "R13 competing gap withdrew an already exposed known prefix slot");
    gapped.complete=true;
    check(!h.observe(gapped,allocate,eligible(first_missing)).selected,
          "competing physical END authorized withdrawal of another owner's known prefix");
    auto canonical=fit(3,56520,102660,46080);
    const auto corrected=h.observe(canonical,allocate,eligible(UINT64_MAX));
    check(corrected.selected && corrected.signal_id==first.signal_id,
          "explicit gap protection froze a stronger fully known canonical correction");
    canonical.complete=true;
    check(h.observe(canonical,allocate,eligible(UINT64_MAX,3)).selected,
          "canonical owner's fully scored END was held by an older prefix");
    for(const bool completed:{false,true}) {
        History after;ids=0;const auto old=after.observe(clean,allocate);
        auto later_gap=gapped;later_gap.complete=completed;
        const auto admitted=after.observe(later_gap,allocate,eligible(published.count));
        check(admitted.selected && admitted.signal_id==old.signal_id,
              "a gap after the known prefix changed ordinary handoff/END policy");
    }
    History own;ids=0;const auto own_first=own.observe(clean,allocate);clean.complete=true;
    const auto own_end=own.observe(clean,allocate,eligible(first_missing));
    check(own_end.selected && own_end.signal_id==own_first.signal_id,
          "same owner's physical END acquired a competing-source gap requirement");
    auto ordinary=published;ordinary.helper_assisted=false;
    check(!reception_source_gap_withdraws_prefix(ordinary,56492,t,120,false,1) &&
          !reception_source_gap_withdraws_prefix(published,56484+121,t,120,true,1) &&
          !reception_source_gap_withdraws_prefix(published,56484,t*2,120,true,1) &&
          !reception_source_gap_withdraws_prefix(published,56484,t,120,true,UINT64_MAX),
          "gap protection reached ordinary, unrelated-birth/profile or unsupported historical geometry");
    // Save explicit placeholders before an earlier chunk moves into content
    // processing. The same bounded scalar protects every later chunk's turn.
    auto placeholder=gap;placeholder.missing_slots=0;placeholder.first_stream_symbol=1;
    placeholder.bits={0,datapump::modem::missing_pattern_bit};
    std::array moved{placeholder,tail};
    const auto saved=reception_first_missing_slot(moved,27540,1);
    auto consumed=std::move(moved[0]);(void)consumed;moved[0].bits.clear();
    check(saved==1 && reception_first_missing_slot(moved,27540,1)==UINT64_MAX &&
          reception_source_gap_withdraws_prefix(published,56492,t,120,true,saved),
          "consuming an earlier placeholder erased the saved current-drain gap witness");
    placeholder.first_stream_symbol=0;
    check(reception_first_missing_slot(std::span(&placeholder,1),27540,1)==UINT64_MAX,
          "an invalid earlier source coordinate acquired gap authority");
    gap.first_stream_symbol=UINT64_MAX;gap.missing_slots=2;
    check(reception_first_missing_slot(std::span(&gap,1),27540,1)==UINT64_MAX,
          "an overflowing missing run acquired gap authority");
}
void helper_nominal_support_geometry() {
    using datapump::modem::PatternBurst;
    using datapump::live::detail::nominal_reception_support;
    using datapump::live::detail::reception_arbitration_support;
    const auto burst=[](std::uint64_t first,std::uint64_t end,std::uint64_t offset,
                        std::size_t bits,double support) {
        PatternBurst b;b.stream_first_sample=first;b.end_sample=end;
        b.first_stream_symbol=offset;b.bits.assign(bits,0);b.support_samples=support;return b;
    };
    constexpr std::uint64_t t=87040;
    // Actual traced first/last sample dates. A native drain may split seven
    // confirmed symbols into earlier chunks which ALL carry seven-symbol support.
    auto first=burst(24088,285225,0,3,609297);
    auto second=burst(24088,459305,3,2,609297);
    auto third=burst(24088,633385,5,2,609297);
    auto end=burst(24088,633385,7,0,609297);end.complete=true;
    std::array chunks{first,second,third,end};
    check(!nominal_reception_support(first,std::span(&first,1),t),
          "earlier chunk's cumulative support was treated as local media");
    for(const auto& b:chunks)check(nominal_reception_support(b,chunks,t)==7.*t,
          "complete drain did not normalize every cumulative chunk identically");
    // Live consumes earlier chunks before later ones. Their scalar coordinates
    // survive moves; each unconsumed current/future chunk still supplies the end.
    chunks[0].bits.clear();
    check(nominal_reception_support(chunks[1],chunks,t)==7.*t,"consuming an earlier chunk changed support units");
    chunks[1].bits.clear();chunks[2].bits.clear();
    check(nominal_reception_support(chunks[3],chunks,t)==7.*t,"physical END changed the confirmed-data denominator");
    for(const auto& b:std::array{first,second,third,end})
        check(reception_arbitration_support(b,chunks,t)==b.support_samples,
              "ordinary/unsupported path changed its raw support");
    auto unrelated=burst(1,UINT64_MAX,0,1,1e12);
    std::array peers{first,second,third,end,unrelated};
    check(nominal_reception_support(first,peers,t)==7.*t,"another physical stream changed the drain envelope");
    auto data=burst(100,100+3*t,0,3,3.*t);
    auto gap=burst(100,100+5*t,3,0,3.*t);gap.missing_slots=2;
    auto tail=burst(100,100+7*t,5,2,5.*t);
    std::array gapped{data,gap,tail};
    check(nominal_reception_support(gap,gapped,t)==5.*t,"unknown source slots added presentation support");
    auto weak=burst(100,100+7*t+17,0,7,(7.*t+17)*.2);
    const auto weak_support=nominal_reception_support(weak,std::span(&weak,1),t);
    check(weak_support && std::abs(*weak_support-7.*t*.2)<1e-8,
          "weak coverage was promoted to full-symbol support");
    auto invalid=first;
    for(unsigned variant=0;variant<8;++variant) {
        invalid=first;
        switch(variant) {
        case 0:invalid.end_sample=invalid.stream_first_sample;break;
        case 1:invalid.support_samples=-1;break;
        case 2:invalid.support_samples=std::numeric_limits<double>::quiet_NaN();break;
        case 3:invalid.support_samples=std::numeric_limits<double>::infinity();break;
        case 4:invalid.support_samples=1e9;break;
        case 5:invalid.first_stream_symbol=UINT64_MAX;break;
        case 6:invalid.missing_slots=1;break;
        case 7:invalid.stream_first_symbol=4;break;
        }
        check(!nominal_reception_support(invalid,std::span(&invalid,1),t),"malformed support envelope was normalized");
        const auto raw=reception_arbitration_support(invalid,std::span(&invalid,1),t,true);
        check(std::isnan(invalid.support_samples)?std::isnan(raw):raw==invalid.support_samples,
              "malformed envelope did not retain the ordinary raw fallback");
    }
    auto huge=burst(0,10,0,2,10);
    check(!nominal_reception_support(huge,std::span(&huge,1),UINT64_MAX),"nominal duration overflow was normalized");
    auto limit=burst(UINT64_MAX-50,UINT64_MAX,UINT64_MAX-1,1,50);
    limit.stream_first_symbol=UINT64_MAX-1;
    check(nominal_reception_support(limit,std::span(&limit,1),50)==50.,"ordered large sample/source differences overflowed");
    check(!nominal_reception_support(first,{},t) && !nominal_reception_support(first,peers,0),
          "missing envelope or zero duration acquired support");
}
// Optional field keeps the same counterfactual source buildable against the
// unmodified History. Live explicitly excludes scheduled/nonuniform sources.
template<class Candidate> void uniform_source(Candidate& value,bool uniform) {
    if constexpr(requires{value.uniform_source=uniform;})value.uniform_source=uniform;
}
void helper_physical_birth_ownership() {
    using datapump::modem::PatternSourceIdentity;
    constexpr std::uint64_t t=87040;
    const auto fit=[](std::uint64_t id,std::uint64_t first,std::uint64_t end,double support) {
        auto c=candidate(id,first,end,support);c.family=0;c.symbol_samples=t;
        c.absence_samples=t;c.timing_tolerance=34;c.helper_assisted=true;return c;
    };
    // Exact first bad R9 transition: c1 already published 11; c6 is the real
    // later suffix 10, born T-13 samples later. Its larger support cannot make
    // those suffix bits the first two bits of c1's physical reception.
    for(const bool assisted:{false,true})for(const bool known:{false,true}) {
        History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto prefix=fit(1,49793,223890,11305.251769);
        auto suffix=fit(6,136820,310917,11976.190939);
        prefix.helper_assisted=suffix.helper_assisted=assisted;
        if(known) {
            prefix.source_identity=PatternSourceIdentity{.epoch=1799999994,.first_symbol=1,.sample_rate=6120};
            suffix.source_identity=PatternSourceIdentity{.epoch=1800000022,.sample_rate=6120};
        }
        const auto first=h.observe(prefix,allocate);
        auto weak=suffix;weak.score=1;
        const auto losing=h.observe(weak,allocate);
        check(!losing.selected && losing.signal_id==first.signal_id,
              "R9 losing suffix did not establish the actual signal_id shortcut setup");
        suffix.signal_id=losing.signal_id;
        const auto next=h.observe(suffix,allocate);
        check(next.selected && next.signal_id!=first.signal_id && next.superseded_ids.empty() && ids==2,
              "R9 stronger one-symbol-later suffix rewrote the established prefix row");
        check(h.observe(prefix,allocate).selected,
              "independent suffix suppressed the earlier owner's accepted progress");
        auto canonical=fit(2,49793,659056,36118.630785);
        canonical.helper_assisted=assisted;
        if(known)canonical.source_identity=PatternSourceIdentity{
            .epoch=1799999993,.first_symbol=1,.phase_lower=4760,.phase_upper=4760,.sample_rate=6120};
        const auto seven=h.observe(canonical,allocate);
        check(seven.selected && seven.signal_id==first.signal_id && seven.superseded_ids.empty(),
              "same-birth canonical hypothesis failed to continue the original seven-bit prefix");
        suffix.end+=t;suffix.score+=1; // A weaker established owner's next bit.
        const auto suffix_progress=h.observe(suffix,allocate);
        check(suffix_progress.selected && suffix_progress.signal_id==next.signal_id &&
              suffix_progress.superseded_ids.empty(),
              "strong canonical progress swallowed a weaker established suffix's next bit");
        canonical.end=746079;canonical.score=40990.176726;canonical.complete=true;
        const auto eight=h.observe(canonical,allocate);
        check(eight.selected && eight.signal_id==first.signal_id && eight.superseded_ids.empty(),
              "source-birth separation withheld original eight-bit physical completion");
        suffix.complete=true;
        const auto suffix_end=h.observe(suffix,allocate);
        check(suffix_end.selected && suffix_end.signal_id==next.signal_id && ids==2,
              "later physical suffix completion lost its own established identity");
    }
    for(const bool assisted:{false,true})for(const int jitter:{-34,0,34}) {
        History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto weak=fit(1,49793,223890,100);
        weak.helper_assisted=assisted;
        const auto first=h.observe(weak,allocate);
        auto correct=fit(2,static_cast<std::uint64_t>(49793+jitter),310917,3.*t);
        correct.helper_assisted=assisted;
        const auto recovered=h.observe(correct,allocate);
        check(recovered.selected && recovered.signal_id==first.signal_id && ids==1,
              "same-birth stronger recovery was frozen by helper source ownership");
        // Weak final evidence on the same owner still publishes physical END.
        correct.score+=1;correct.end+=t;correct.complete=true;
        check(h.observe(correct,allocate).selected,"same-birth weak final progress lost physical END");
    }
    // Different-duration and nonuniform paths retain scalar arbitration.
    for(unsigned excluded=1;excluded<5;++excluded) {
        History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto old=fit(1,49793,223890,11305.251769);
        auto next=fit(6,136820,310917,11976.190939);
        switch(excluded) {
        case 1:next.symbol_samples*=2;break;
        case 2:uniform_source(old,false);uniform_source(next,false);break;
        case 3:old.source_identity=History::BurstIdentity{};next.source_identity=History::BurstIdentity{};break;
        case 4:old.amplitude_low_hz=next.amplitude_low_hz=1490;
               old.amplitude_high_hz=next.amplitude_high_hz=1510;break;
        }
        const auto first=h.observe(old,allocate),second=h.observe(next,allocate);
        check(second.selected && second.signal_id==first.signal_id && ids==1,
              "source-birth protection changed a different-duration or nonuniform arbitration path");
    }
}
void helper_physical_birth_capacity_boundary() {
    // This is an explicit coverage boundary, not an unbounded alias guarantee.
    // 64 established active physical births retain their existing allocation;
    // a newly discovered earlier birth at that bound is UNSUPPORTED/limited.
    constexpr std::uint64_t t=1000;
    for(const bool assisted:{false,true}) {
        // Keep the same fixed capacity for ordinary and assisted receptions.
        History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        std::array<History::Candidate,History::capacity> owners;
        std::array<std::uint64_t,History::capacity> rows{};
        for(std::size_t i=0;i<owners.size();++i) {
            auto c=candidate(i+1,(i+1)*t,(1000+i)*t,static_cast<double>(i+1));
            c.symbol_samples=c.absence_samples=t;c.timing_tolerance=10;c.helper_assisted=assisted;
            owners[i]=c;const auto admitted=h.observe(c,allocate);rows[i]=admitted.signal_id;
            check(admitted.selected && !admitted.limited && admitted.superseded_ids.empty(),
                  "active-birth capacity fixture merged an established physical prefix");
        }
        check(h.size()==History::capacity && ids==History::capacity,
              "active-birth fixture did not reach its existing fixed resource bound");
        auto late=candidate(1000,0,2000*t,10000);late.symbol_samples=late.absence_samples=t;
        late.timing_tolerance=10;late.helper_assisted=assisted;
        const auto unsupported=h.observe(late,allocate);
        check(!unsupported.selected && unsupported.limited && !unsupported.signal_id &&
              unsupported.superseded_ids.empty() && h.size()==History::capacity && ids==History::capacity,
              "65th late canonical birth evicted an active prefix or exceeded the fixed History bound");
        for(std::size_t i=0;i<owners.size();++i) {
            owners[i].end+=t;owners[i].score+=20000;
            const auto progress=h.observe(owners[i],allocate);
            check(progress.selected && !progress.limited && progress.signal_id==rows[i] &&
                  progress.superseded_ids.empty() && h.size()==History::capacity && ids==History::capacity,
                  "capacity refusal mutated an established canonical/suffix owner's next-bit identity");
        }
        for(std::size_t i=0;i<owners.size();++i) {
            owners[i].complete=true;
            const auto end=h.observe(owners[i],allocate);
            check(end.selected && !end.limited && end.signal_id==rows[i] && end.superseded_ids.empty() &&
                  h.size()==History::capacity && ids==History::capacity,
                  "fixed history pressure withheld an established owner's physical END");
        }
    }
}
void ordinary_physical_birth_ownership() {
    constexpr std::uint64_t t=87040;
    const auto fit=[](std::uint64_t id,std::uint64_t first,std::uint64_t end,double score) {
        auto c=candidate(id,first,end,score);c.family=0;c.symbol_samples=t;
        c.absence_samples=t;c.timing_tolerance=34;return c;
    };
    History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    // Actual RF-only control: c6 is two symbols later than the exposed c2
    // prefix, within 13 samples of its clock, and overlaps c5 as well.
    auto prefix=fit(2,31365,205462,19246.89031400221);
    auto middle=fit(5,292452,379526,8534.748368459339);
    auto suffix=fit(6,205432,379529,20703.391189714675);
    const auto first=h.observe(prefix,allocate);const auto first_id=first.signal_id;
    const auto second=h.observe(middle,allocate);const auto second_id=second.signal_id;
    check(first.selected && second.selected && first_id!=second_id && ids==2,
          "ordinary retained prefixes did not establish independent rows");
    auto weak=suffix;weak.score=1;
    const auto losing=h.observe(weak,allocate);
    check(!losing.selected && losing.signal_id==first_id && ids==2,
          "ordinary losing suffix missed the old signal-id shortcut");
    suffix.signal_id=losing.signal_id;
    const auto entered=h.observe(suffix,allocate);const auto suffix_id=entered.signal_id;
    check(entered.selected && suffix_id!=first_id && suffix_id!=second_id &&
          entered.superseded_ids.empty() && ids==3 && h.size()==3,
          "ordinary stronger suffix rewrote or merged accepted prefixes");
    prefix.end=466565;prefix.score=49384.753330355765;
    const auto progress=h.observe(prefix,allocate);
    check(progress.selected && progress.signal_id==first_id && progress.superseded_ids.empty(),
          "ordinary canonical progress stole an established suffix row");
    suffix.end=553592;suffix.score=40000;
    const auto suffix_progress=h.observe(suffix,allocate);
    check(suffix_progress.selected && suffix_progress.signal_id==suffix_id &&
          suffix_progress.superseded_ids.empty(),"ordinary suffix lost its subsequent progress");
    middle.end=466566;middle.score=17000;
    const auto middle_progress=h.observe(middle,allocate);
    check(middle_progress.selected && middle_progress.signal_id==second_id &&
          middle_progress.superseded_ids.empty(),"ordinary middle source lost its row");
    // History routes an already scored native END; this is not an absence test.
    prefix.end=727651;prefix.score=76963.44369070149;prefix.complete=true;
    const auto end=h.observe(prefix,allocate);
    check(end.selected && end.signal_id==first_id && end.superseded_ids.empty() &&
          h.size()==3 && !h.observe(prefix,allocate).selected,
          "ordinary END routing merged another source or repeated completion");
    suffix.complete=true;
    const auto suffix_end=h.observe(suffix,allocate);
    check(suffix_end.selected && suffix_end.signal_id==suffix_id && suffix_end.superseded_ids.empty(),
          "ordinary separate suffix lost its own END routing");
}
void helper_nominal_suffix_arbitration() {
    using datapump::modem::PatternBurst;
    using datapump::modem::PatternSourceIdentity;
    using datapump::live::detail::reception_arbitration_support;
    constexpr std::uint64_t t=87040;
    const auto fit=[](std::uint64_t id,std::uint64_t first,std::uint64_t end,unsigned symbols,
                      double fraction,bool known,std::uint64_t epoch) {
        auto c=candidate(id,first,end,(end-first)*fraction);c.family=0;
        c.symbol_samples=c.absence_samples=t;c.timing_tolerance=34;c.helper_assisted=true;
        c.frequency_tolerance=6120./t;
        if(known)c.source_identity=PatternSourceIdentity{.epoch=epoch,.sample_rate=6120};
        PatternBurst b;b.stream_first_sample=first;b.end_sample=end;
        b.bits.assign(symbols,0);b.support_samples=c.score;
        c.score=reception_arbitration_support(b,std::span(&b,1),t,true);
        return c;
    };
    for(const bool known:{false,true})for(const int offset:{-17,0,17}) {
        History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto full=fit(2,24088,633385,7,1,known,1793065127);
        const auto initial=h.observe(full,allocate);
        const auto first=static_cast<std::uint64_t>(111117+offset);
        auto suffix=fit(4,first,720414,7,1,known,1793065141);
        suffix.signal_id=initial.signal_id; // A previously losing suffix aliases this row.
        const auto later=h.observe(suffix,allocate);
        check(initial.selected && !later.selected && later.signal_id==initial.signal_id,
              "rounding-only later suffix replaced an established complete prefix");
        full.end=720425;full.score=8.*t;full.complete=true;
        check(h.observe(full,allocate).selected,"normalization withheld the actual owner's completion");
    }
    for(const bool known:{false,true}) {
        std::array<unsigned,3> order{0,1,2};
        do {
            History h;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
            std::array fits{fit(2,24088,633385,7,1,known,1793065127),
                fit(4,111100,720414,7,1,known,1793065141),
                fit(5,198140,807454,7,.8,known,1793065155)};
            // Separately published physical births retain their own prefix;
            // weaker unpublished suffixes may still be suppressed as aliases.
            std::array<std::uint64_t,4> births{};
            const auto observe=[&](unsigned i) {
                const auto d=h.observe(fits[i],allocate);
                if(d.selected) {
                    check(d.signal_id<births.size(),"helper permutation grew beyond its three candidates");
                    auto& birth=births[d.signal_id];
                    check(!birth || birth==fits[i].first,
                          "three-way helper arbitration reassigned a published physical birth");
                    birth=fits[i].first;
                }
                return d;
            };
            for(auto i:order)observe(i);
            check(observe(0).selected,"the strongest complete-prefix birth was suppressed");
            for(auto i:order)observe(i);
        }while(std::next_permutation(order.begin(),order.end()));
    }
    History stronger;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    auto weak=fit(2,24088,633385,7,.2,false,1793065127);
    const auto old=stronger.observe(weak,allocate);
    auto better=fit(4,111100,720414,7,.8,false,1793065141);
    const auto improved=stronger.observe(better,allocate);
    check(improved.selected && improved.signal_id!=old.signal_id && improved.superseded_ids.empty(),
          "a stronger different-birth helper stream rewrote a weak earlier prefix");
    check(stronger.observe(weak,allocate).selected,
          "separating a stronger later stream suppressed the earlier owner");
    better.complete=true;stronger.observe(better,allocate);
    auto independent=fit(6,900000,987040,1,1,false,1793065300);
    check(stronger.observe(independent,allocate).selected && ids==3,
          "normalization swallowed a later independent reception");
}

void amplitude_passband_aliases() {
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    auto first=candidate(1,100,1100,80);first.frequency=1499;first.frequency_tolerance=.002;
    first.amplitude_low_hz=1497;first.amplitude_high_hz=1501;
    const auto initial=history.observe(first,allocate);
    auto overlap=first;overlap.id=2;overlap.frequency=1501;overlap.score=90;
    overlap.amplitude_low_hz=1499;overlap.amplitude_high_hz=1503;
    const auto alias=history.observe(overlap,allocate);
    check(alias.selected&&alias.signal_id==initial.signal_id&&ids==1,
          "overlapping amplitude centers duplicated one physical reception");
    auto weaker=overlap;weaker.score=1;weaker.frequency=1505;
    weaker.amplitude_low_hz=1503;weaker.amplitude_high_hz=1507;
    check(history.observe(weaker,allocate).signal_id==initial.signal_id,
          "weaker owner update abandoned its original row");
    auto separate=overlap;separate.id=3;separate.frequency=1505;
    separate.amplitude_low_hz=1503;separate.amplitude_high_hz=1507;
    check(history.observe(separate,allocate).signal_id!=initial.signal_id&&ids==2,
          "nonoverlapping amplitude passbands merged separate carriers");
    auto coherent=first;coherent.id=4;coherent.frequency=1500;coherent.score=100;
    coherent.amplitude_low_hz=coherent.amplitude_high_hz=0;
    check(history.observe(coherent,allocate).signal_id!=initial.signal_id&&ids==3,
          "coarse amplitude uncertainty widened a resolved coherent carrier");
    overlap.complete=true;
    check(history.observe(overlap,allocate).signal_id==initial.signal_id,
          "amplitude physical completion abandoned its pending identity");
}
void optional_history_priority() {
    using datapump::live::detail::optional_pattern_history_ready;
    check(optional_pattern_history_ready(2048,2048,2048,true),"caught-up diagnostics were suppressed");
    check(!optional_pattern_history_ready(2048,1024,2048,true),"numeric backlog ran optional history");
    check(!optional_pattern_history_ready(2048,2048,4096,true),"popped block tail was ignored");
    check(!optional_pattern_history_ready(2048,2048,2048,false),"queued input was ignored");
    check(optional_pattern_history_ready(8192,8192,8192,true),"prospective null coordinates were treated as old PCM ingress");
    check(!optional_pattern_history_ready(8192,8192,4096,true),"uncaptured future frontier admitted diagnostics");
}
void deferred_provenance_growth() {
    using O=datapump::live::detail::DeferredPatternHistoryOrdinals;
    O value;std::uint64_t next_id=1;
    const auto ids=[&](std::uint64_t n){const auto first=next_id;next_id+=n;return first;};
    const auto time=O::Clock::time_point{}+std::chrono::seconds(19);
    for(std::uint64_t i=1;i<=64;++i)value.record(i,time+std::chrono::milliseconds(i),ids);
    check(!value.can_record() && value.heap_bytes()==0,"inline provenance did not reach original bound");
    const auto old=value.observation(17,42.);std::size_t charged=0;
    check(value.grow(128,[&](auto n){charged=n;}),"funded provenance growth failed");
    check(charged==O::growth_bytes(128) && value.heap_bytes()==charged && value.capacity()==128,
          "provenance omitted full replacement/retained storage charge");
    const auto same=value.observation(17,42.);
    check(same.id==old.id && same.observed_at==old.observed_at && same.admission_threshold==old.admission_threshold,
          "provenance growth changed original observation identity or time");
    value.record(67,time+std::chrono::seconds(1),ids);
    check(value.observation(65,9.).id==65 && value.observation(67,9.).id==67 && next_id==68,
          "growth rewound IDs or omitted a completed generation");
    value.discard_resolved_through(65);
    check(value.oldest_recorded()==66 && value.observation(66,9.).id==66,"partial provenance retirement changed IDs");
    std::stop_source stop;
    check(!value.grow(256,[&](auto){stop.request_stop();},stop.get_token()) && value.capacity()==128 &&
          value.oldest_recorded()==66 && value.observation(67,9.).id==67,"cancelled provenance growth published partial state");
    bool failed=false;try{value.grow(256,[&](auto){throw std::bad_alloc();});}catch(const std::bad_alloc&){failed=true;}
    check(failed && value.capacity()==128 && value.observation(67,9.).id==67,"allocation failure damaged provenance");
    O moved(std::move(value));
    check(moved.capacity()==128 && moved.observation(67,9.).id==67 && value.capacity()==64 && value.generation()==0,
          "moving grown provenance left dangling inline storage or invalid source state");
    moved={};check(moved.heap_bytes()==0 && moved.capacity()==64 && moved.generation()==0,"provenance reset retained heap or generation");
}
void immediate_progress_and_physical_completion() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto stream = candidate(1, 100, 1100, 80);
    const auto first = history.observe(stream, allocate);
    check(first.selected && !first.limited && first.signal_id == 1, "first accepted bit was withheld");
    // These are the next polls for 0, 00 and 001; no byte/token boundary is
    // needed to select each accepted prefix under the same pending identity.
    stream.end = 2100; stream.score = 160;
    const auto second = history.observe(stream, allocate);
    stream.end = 3100; stream.score = 240;
    const auto third = history.observe(stream, allocate);
    check(second.selected && third.selected && second.signal_id == first.signal_id &&
          third.signal_id == first.signal_id && ids == 1, "accepted short prefixes lost their shared pending identity");
    check(first.revision && first.revision == second.revision && second.revision == third.revision,
          "ordinary bit progress changed the selected interpretation revision");
    stream.complete = true;
    const auto complete = history.observe(stream, allocate);
    check(complete.selected && complete.signal_id == first.signal_id, "physical completion did not update the pending row");
    check(!history.observe(stream, allocate).selected, "the same physical completion was reported twice");
}
void incompatible_same_duration_clocks_stay_independent() {
    datapump::modem::Config geometry;geometry.sample_rate=512;geometry.bandwidth_hz=128;
    geometry.carrier_hz=128;geometry.spreading_factor=16;
    const auto tolerance=datapump::modem::pattern_chip_samples(geometry);
    check(tolerance==8,"compact raw fixture changed its actual chip tolerance");
    const auto raw=[&](std::uint64_t id,std::uint64_t first,std::uint64_t end,double score) {
        auto c=candidate(id,first,end,score);c.frequency=128;c.frequency_tolerance=4;
        c.symbol_samples=128;c.timing_tolerance=tolerance;return c;
    };
    for(const bool rival_first:{false,true}) {
        History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto exact=raw(1,1164,1292,128);
        auto rival=raw(2,488,1520,789.72522131542394);rival.frequency=127;
        if(rival_first)history.observe(rival,allocate);
        const auto initial=history.observe(exact,allocate);
        exact.end=1548;exact.score=384;
        // Match the bank's all-candidate pre-observation before publishing an
        // earlier chunk. A rival's longer span cannot buy this clock's row.
        const auto exact_row=history.observe(exact,allocate);
        const auto rival_row=history.observe(rival,allocate);
        check(initial.selected && exact_row.selected && rival_row.selected && ids==2 &&
              initial.signal_id==exact_row.signal_id && exact_row.signal_id!=rival_row.signal_id,
              "an incompatible overlapping clock replaced an accepted raw prefix");
        auto earlier=raw(1,1164,1292,128);
        check(history.observe(earlier,allocate).selected,
              "bank pre-observation withheld an independent clock's earlier chunk");
        exact.complete=true;rival.complete=true;
        const auto completed=history.observe(exact,allocate);
        const auto other_completed=history.observe(rival,allocate);
        check(completed.selected && other_completed.selected &&
              completed.signal_id==initial.signal_id && completed.signal_id!=other_completed.signal_id,
              "a competing clock suppressed exact physical completion");
    }
    for(const std::uint64_t offset:std::array<std::uint64_t,6>{0,tolerance,tolerance+1,127-tolerance,128-tolerance,128}) {
        History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        const auto initial=history.observe(raw(1,1164,1676,128),allocate);
        const auto next=history.observe(raw(2,1164+offset,1804,256),allocate);
        const bool aligned=offset<=tolerance;
        check(next.selected && (next.signal_id==initial.signal_id)==aligned && ids==(aligned?1U:2U),
              "ordinary overlap changed physical-prefix ownership beyond its timing cell");
    }
}
void stronger_profile_replaces_prefix() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto prefix = candidate(1, 100, 1100, 80);
    const auto initial = history.observe(prefix, allocate);
    auto full = candidate(2, 100, 2100, 180);
    const auto replacement = history.observe(full, allocate);
    check(replacement.selected && replacement.signal_id == initial.signal_id && ids == 1,
          "a stronger duration created a second reception");
    prefix.complete = true;
    check(!history.observe(prefix, allocate).selected, "weaker completion finished a stronger pending reception");
    // A bank first observes every burst, then publishes their original chunks.
    // The owner must remain selected when an earlier chunk has a lower score.
    auto earlier = full; earlier.end = 1100; earlier.score = 90;
    check(history.observe(earlier, allocate).selected, "bank pre-observation withheld an earlier accepted owner chunk");
    prefix.complete = false; prefix.score = 170;
    check(!history.observe(prefix, allocate).selected, "an earlier owner chunk lowered the retained winning evidence");
    full.complete = true;
    check(history.observe(full, allocate).selected, "winning profile did not finish");
    prefix.complete = true;
    check(!history.observe(prefix, allocate).selected && ids == 1, "late profile completion duplicated a finished message");
    auto late = candidate(3, 1800, 2600, 170); late.complete = true;
    check(!history.observe(late, allocate).selected && ids == 1,
          "delayed overlapping profile reopened already reported physical completion");
    late.id = 4; late.end = 1000000;
    check(!history.observe(late, allocate).selected, "long delayed rival escaped completed overlap suppression");
    const auto next = history.observe(candidate(5, 10000, 11000, 200), allocate);
    check(next.selected && next.signal_id != initial.signal_id && ids == 2,
          "a delayed rival extended completed suppression into a separate later message");
}
void stronger_symbol_after_prefix_completion() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto prefix = candidate(1, 64, 576, 98); prefix.complete = true;
    const auto short_row = history.observe(prefix, allocate);
    auto long_symbol = candidate(2, 64, 287382, 1000);
    const auto long_row = history.observe(long_symbol, allocate);
    check(long_row.selected && long_row.signal_id == short_row.signal_id && ids == 1 &&
          long_row.revision > short_row.revision,
          "a stronger fully scored long symbol did not revise its completed short-prefix row");
    auto weaker_alias = candidate(3, 64, 576, 97);
    const auto alias = history.observe(weaker_alias, allocate);
    check(!alias.selected && alias.signal_id == long_row.signal_id && ids == 1,
          "a late weak alias matched completed history before the active long reception");
    check(!history.observe(prefix, allocate).selected && ids == 1,
          "the earlier completed owner reported a duplicate final");
    long_symbol.end += 287318; long_symbol.score += 1000;
    check(history.observe(long_symbol, allocate).selected,
          "the longer profile did not retain immediate progress after its delayed discovery");
}
void bridges_retire_every_fragment_in_any_order() {
    std::array<unsigned, 3> order{0, 1, 2};
    do {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        std::array<History::Candidate, 3> fragments{
            candidate(10, 100, 1100, 80), candidate(20, 10000, 11000, 90),
            candidate(30, 20000, 21000, 100)};
        std::array<std::uint64_t, 3> fragment_ids{};
        for (const auto index : order) {
            fragments[index].complete = true;
            fragment_ids[index] = history.observe(fragments[index], allocate).signal_id;
        }
        check(history.size() == 3, "disjoint completed fragments were prematurely merged");
        auto whole = candidate(40, 100, 21000, 500);
        whole.symbol_samples = 21000;
        const auto joined = history.observe(whole, allocate);
        check(joined.selected && joined.signal_id == 1 && history.size() == 1 && ids == 3 &&
              joined.superseded_ids.size() == 2 && joined.superseded_ids[0] == 2 && joined.superseded_ids[1] == 3,
              "a stronger bridge failed to retire every completed fragment under the oldest row");
        const auto revision = joined.revision;
        whole.signal_id = fragment_ids[2]; whole.end += 21000; whole.score += 500;
        const auto continued = history.observe(whole, allocate);
        check(continued.selected && continued.signal_id == 1 && continued.revision == revision &&
              continued.superseded_ids.size() == 2,
              "merged aliases or immediate selected-owner progress were lost");
        auto late = fragments[0]; late.id = 50; late.signal_id = fragment_ids[1];
        late.first = 90000; late.end = 91000;
        const auto suppressed = history.observe(late, allocate);
        check(!suppressed.selected && suppressed.signal_id == 1 && ids == 3,
              "a remembered alias created a new row after its original fragment was retired");
        whole.complete = true;
        const auto done = history.observe(whole, allocate);
        check(done.selected && done.signal_id == 1 && done.revision == revision,
              "the selected bridge's own physical completion was lost");
        whole.complete = false; whole.score += 500;
        check(!history.observe(whole, allocate).selected,
              "a stale pending observation reopened its own completed interpretation");
    } while (std::next_permutation(order.begin(), order.end()));
}
void equal_evidence_uses_intrinsic_geometry() {
    for (const bool reverse : {false, true}) {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        auto shorter = candidate(reverse ? 2 : 1, 100, 1100, 100);
        auto longer = candidate(reverse ? 1 : 2, 100, 1100, 100);
        shorter.symbol_samples = 100; longer.symbol_samples = 1000;
        const auto first = history.observe(reverse ? longer : shorter, allocate);
        const auto second = history.observe(reverse ? shorter : longer, allocate);
        check(first.signal_id == second.signal_id && ids == 1 && second.selected == !reverse,
              "equal evidence selected an owner according to RX target order");
        check(history.observe(longer, allocate).selected && !history.observe(shorter, allocate).selected,
              "the intrinsic equal-evidence owner did not remain selected");
    }
}
void supported_media_outweighs_overconfident_short_prefixes() {
    // These media lengths/native scores were observed by sampled cross-target
    // probes. SF16/32 use exact sample fits here; SF64 uses compact bins, whose
    // conservative covariance bound gives perfect fits much smaller scores.
    constexpr std::uint64_t chip = 8;
    const auto full = 3 * datapump::modem::pattern_symbol_support(512, 293./3, chip);
    const auto short32 = 5 * datapump::modem::pattern_symbol_support(256, 893./5, chip);
    const auto short16 = 9 * datapump::modem::pattern_symbol_support(128, 434./9, chip);
    check(full == 1536 && short32 == 1280 && short16 == 1152,
          "native short-fit evidence claimed support beyond its admitted media");
    std::array<unsigned, 3> order{0, 1, 2};
    do {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        std::array<History::Candidate, 3> fits{
            candidate(1, 100, 1636, full), candidate(2, 100, 1380, short32),
            candidate(3, 100, 1252, short16)};
        fits[0].symbol_samples = 512; fits[1].symbol_samples = 256; fits[2].symbol_samples = 128;
        for (const auto index : order) history.observe(fits[index], allocate);
        check(history.observe(fits[0], allocate).selected &&
              !history.observe(fits[1], allocate).selected && !history.observe(fits[2], allocate).selected && ids == 1,
              "short exact-fit prefixes defeated the full admitted stream under an RX target permutation");
    } while (std::next_permutation(order.begin(), order.end()));

    const auto single = datapump::modem::pattern_symbol_support(512, 98, chip);
    const auto exact_prefix = datapump::modem::pattern_symbol_support(256, 372, chip);
    check(single == 512 && exact_prefix == 256 && single > exact_prefix,
          "an overconfident half-symbol prefix defeated a complete one-bit message");
    const auto correct_long = datapump::modem::pattern_symbol_support(4096, 790, chip);
    const auto weak_long = datapump::modem::pattern_symbol_support(16384, 150, chip);
    check(correct_long == 4096 && weak_long == 1200 && correct_long > weak_long,
          "a weak long partial fit gained confidence merely from its configured duration");
    const auto keyed_correct = 3 * datapump::modem::pattern_symbol_support(512, 293.496/3, chip);
    const auto keyed_alias = datapump::modem::pattern_symbol_support(1024, 196.557, chip) +
                             datapump::modem::pattern_symbol_support(1024, 56.061, chip);
    check(keyed_correct == 1536 && keyed_alias > 1472 && keyed_alias < 1473 && keyed_correct > keyed_alias,
          "a keyed alias borrowed its strong first symbol's surplus to inflate a weak next symbol");
}
void unobserved_positions_add_no_selection_support() {
    constexpr std::uint64_t chip = 8;
    check(datapump::modem::pattern_symbol_support(0, 10000, chip) == 0 &&
          datapump::modem::pattern_symbol_support(4096, 0, chip) == 0 &&
          datapump::modem::pattern_symbol_support(4096, -100, chip) == 0,
          "empty media or absent evidence acquired arbitration support");
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto known = candidate(1, 100, 1636, 3 * datapump::modem::pattern_symbol_support(512, 293./3, chip));
    const auto original = history.observe(known, allocate);
    // Compact missing runs and missing_pattern_bit placeholders occupy sample
    // positions but leave the known-media count and admitted evidence unchanged.
    auto missing = candidate(2, 100, 1380, 5 * datapump::modem::pattern_symbol_support(256, 893./5, chip));
    check(!history.observe(missing, allocate).selected, "the erasure fixture unexpectedly won");
    missing.end = 1000000000;
    const auto extended = history.observe(missing, allocate);
    check(!extended.selected && extended.signal_id == original.signal_id && ids == 1,
          "a long unobserved run increased a losing interpretation's selection support");
    known.end += 512; known.score = 4 * datapump::modem::pattern_symbol_support(512, 391./4, chip);
    check(history.observe(known, allocate).selected,
          "unknown rival positions withheld a selected owner's next known symbol");
}
void losing_spans_cannot_poison_later_receptions() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    const auto first = history.observe(candidate(1, 100, 1100, 1000), allocate);
    auto losing = candidate(2, 100, 1000000, 100); losing.absence_samples = 1000000;
    check(!history.observe(losing, allocate).selected, "the weak long fixture unexpectedly won");
    const auto later = history.observe(candidate(3, 10000, 11000, 900), allocate);
    check(later.selected && later.signal_id != first.signal_id && history.size() == 2,
          "a losing long span/silence allowance swallowed an independent transmission");
    // The same weak observation touches both stronger rows. It cannot use its
    // unselected envelope as a connection between their independent evidence.
    const auto weak_bridge = history.observe(losing, allocate);
    check(!weak_bridge.selected && history.size() == 2 && weak_bridge.superseded_ids.empty(),
          "a weak bridge merged disjoint stronger receptions");
    auto winner = candidate(1, 100, 11000, 2000);
    const auto actual_bridge = history.observe(winner, allocate);
    check(actual_bridge.selected && actual_bridge.signal_id == first.signal_id &&
          history.size() == 1 && actual_bridge.superseded_ids.size() == 1,
          "the growing selected owner's real envelope failed to merge an overlapping fragment");
}
void alias_storage_is_bounded_without_hiding_owner_progress() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto winner = candidate(1, 100, 1100, 1000);
    const auto original = history.observe(winner, allocate);
    for (std::size_t i = 0; i < History::capacity; ++i) {
        auto fragment = candidate(100 + i, winner.end + 10000, winner.end + 11000, 80);
        fragment.complete = true;
        check(history.observe(fragment, allocate).selected, "alias storage fixture merged too early");
        winner.end = fragment.end; winner.score += 1000;
        const auto joined = history.observe(winner, allocate);
        check(joined.selected && !joined.limited && joined.superseded_ids.size() == i + 1,
              "bounded alias history lost a permitted merge");
    }
    winner.end += 1; winner.score += 1000;
    const auto progress = history.observe(winner, allocate);
    check(progress.selected && !progress.limited && progress.signal_id == original.signal_id &&
          progress.superseded_ids.size() == History::capacity,
          "full alias storage withheld an existing owner's newly accepted bits");
    auto fragment = candidate(1000, winner.end + 10000, winner.end + 11000, 80);
    fragment.complete = true;
    const auto separate = history.observe(fragment, allocate);
    auto contender = candidate(2000, 100, fragment.end, winner.score + 1000);
    const auto refused = history.observe(contender, allocate);
    check(refused.limited && !refused.selected && history.size() == 2,
          "alias overflow silently exceeded its bound or forgot a retired identity");
    const auto retained = history.observe(winner, allocate);
    check(retained.selected && !retained.limited && retained.signal_id != separate.signal_id &&
          retained.superseded_ids.size() == History::capacity,
          "refused alias overflow corrupted existing owner progress");
    winner.end = fragment.end; winner.score += 2000;
    const auto extended = history.observe(winner, allocate);
    check(extended.selected && extended.limited && extended.signal_id == original.signal_id &&
          extended.superseded_ids.size() == History::capacity && history.size() == 2,
          "an existing owner's accepted bit was hidden by an impossible additional alias merge");
}
void separate_messages_and_families() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    const auto initial = history.observe(candidate(1, 100, 1100, 80), allocate);
    // A repeated long symbol's short prefix can look like a disjoint fragment.
    auto fragment = candidate(2, 2100, 3100, 90);
    check(history.observe(fragment, allocate).signal_id == initial.signal_id && ids == 1,
          "a fragment inside the pending absence window created a separate row");
    auto independent = candidate(3, 9100, 10100, 100);
    check(history.observe(independent, allocate).signal_id != initial.signal_id && ids == 2,
          "a full physical absence did not separate a later message");
    auto other_frequency = independent; other_frequency.id = 4; other_frequency.frequency += 30;
    check(history.observe(other_frequency, allocate).signal_id == 3,
          "resolvable frequencies were merged");
    auto other_family = independent; other_family.id = 5; other_family.family = 2;
    check(history.observe(other_family, allocate).signal_id == 4,
          "independent key families were merged");
    independent.complete = true;
    check(history.observe(independent, allocate).selected, "independent message could not finish");
    auto following = candidate(6, independent.end + 1, independent.end + 1001, 110);
    check(history.observe(following, allocate).signal_id == 5,
          "completed history bridged a nonoverlapping independent reception");
}
void sample_clock_and_long_absence() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    const auto origin = std::numeric_limits<std::uint64_t>::max() - 10000;
    auto first = candidate(1, origin, origin + 1000, 80);
    const auto id = history.observe(first, allocate).signal_id;
    auto second = candidate(2, origin + 6999, origin + 7999, 90);
    check(history.observe(second, allocate).signal_id == id && ids == 1,
          "large global sample coordinates overflowed the pending absence comparison");

    History long_symbols; ids = 0;
    constexpr std::uint64_t duration = 4ULL * 60 * 60 * 8000;
    first = candidate(3, 100, 100 + duration, 80);
    first.absence_samples = first.symbol_samples = duration;
    first.timing_tolerance = 8;
    const auto long_id = long_symbols.observe(first, allocate).signal_id;
    second = candidate(4, first.end + 4, first.end + 1004, 70);
    second.absence_samples = 6 * 8000; second.symbol_samples = 1000; second.timing_tolerance = 8;
    const auto aligned = long_symbols.observe(second, allocate);
    check(!aligned.selected && aligned.signal_id == long_id && ids == 1,
          "a timing-aligned next-slot prefix separated a pending long reception");
    auto independent = second; independent.id = 5;
    independent.first = first.end + 8000 * 60; independent.end = independent.first + 1000;
    check(long_symbols.observe(independent, allocate).signal_id != long_id && ids == 2,
          "partial long-symbol absence swallowed a distinct later short reception");
    first.end += duration; first.score += 1000;
    const auto progress = long_symbols.observe(first, allocate);
    check(progress.selected && progress.signal_id == long_id,
          "a fully accepted hours-long owner symbol lost immediate pending progress");
}
void pending_fragments_must_share_the_long_symbol_clock() {
    for (const bool reverse : {false, true}) {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        auto long_symbol = candidate(1, 64, 2112, 1000);
        long_symbol.symbol_samples = long_symbol.absence_samples = 2048;
        long_symbol.timing_tolerance = 8;
        auto separate = candidate(2, 3000, 3128, 100);
        separate.symbol_samples = 128; separate.absence_samples = 384; separate.timing_tolerance = 8;
        const auto first = history.observe(reverse ? separate : long_symbol, allocate);
        const auto second = history.observe(reverse ? long_symbol : separate, allocate);
        check(first.selected && second.selected && first.signal_id != second.signal_id && history.size() == 2,
              "a long receiver's partial absence swallowed an independent later short send");
    }
    for (const std::uint64_t drift : {0, 4, 8}) {
        History history; std::uint64_t ids = 0;
        const auto allocate = [&] { return ++ids; };
        auto long_symbol = candidate(1, 64, 2112, 1000);
        long_symbol.symbol_samples = long_symbol.absence_samples = 2048;
        long_symbol.timing_tolerance = 8;
        const auto original = history.observe(long_symbol, allocate);
        auto prefix = candidate(2, long_symbol.end + drift, long_symbol.end + drift + 128, 100);
        prefix.symbol_samples = 128; prefix.absence_samples = 384; prefix.timing_tolerance = 8;
        const auto aligned = history.observe(prefix, allocate);
        check(!aligned.selected && aligned.signal_id == original.signal_id && ids == 1,
              "a contiguous short prefix escaped its pending long-symbol reception");
        prefix.id = 3; prefix.first = long_symbol.end + long_symbol.symbol_samples - 1;
        prefix.end = prefix.first + 128;
        check(history.observe(prefix, allocate).signal_id != original.signal_id && ids == 2,
              "timing tolerance crossed a complete long-symbol absence boundary");
    }
    // A later profile's own absence allowance cannot reach backward past the
    // earlier short receiver's complete six-second failure window.
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto earlier = candidate(1, 64, 192, 100);
    earlier.symbol_samples = 128; earlier.absence_samples = 384;
    const auto first = history.observe(earlier, allocate);
    auto later = candidate(2, 2112, 4160, 1000);
    later.symbol_samples = later.absence_samples = 2048;
    check(history.observe(later, allocate).signal_id != first.signal_id && ids == 2,
          "a later long profile retroactively expanded a shorter stream's absence window");
    // Completed future fragments still require actual overlap with admitted
    // long-symbol media, even when their starts are exactly clock-aligned.
    History completed; ids = 0;
    auto future = candidate(3, 2112, 2240, 100); future.complete = true;
    future.symbol_samples = 128; future.absence_samples = 384;
    const auto closed = completed.observe(future, allocate);
    later.first = 64; later.end = 2112;
    check(completed.observe(later, allocate).signal_id != closed.signal_id && ids == 2,
          "unscored future media erased an independently completed fragment");
    later.end = 4160; later.score += 1000;
    const auto known_overlap = completed.observe(later, allocate);
    check(known_overlap.selected && known_overlap.signal_id == closed.signal_id &&
          completed.size() == 1 && known_overlap.superseded_ids.size() == 1,
          "an actually accepted next long symbol failed to merge the overlapping completed prefix");
}
void retired_history_preserves_active_aliases() {
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto first=history.observe(candidate(1,100,1100,80),allocate).signal_id;
    const auto alias=history.observe(candidate(2,10100,11100,90),allocate).signal_id;
    auto bridge=candidate(3,100,11100,300);
    check(history.observe(bridge,allocate).signal_id==first && first!=alias,"alias retention fixture did not merge histories");
    // A losing profile may still publish later under a superseded alias.
    history.retire_unreferenced([&](auto id){return id==alias;});
    auto continuation=candidate(4,12100,13100,400);
    check(history.observe(continuation,allocate).signal_id==first,"active losing alias lost pending gap continuity");
    history.retire_unreferenced([](auto){return false;});
    auto later=candidate(5,14100,15100,500);
    check(history.observe(later,allocate).signal_id!=first,"retired evidence bridged an unrelated later gap");

    History overlap;std::uint64_t next=0;const auto new_id=[&]{return ++next;};
    const auto original=overlap.observe(candidate(10,100,1100,100),new_id).signal_id;
    overlap.retire_unreferenced([](auto){return false;});
    check(overlap.observe(candidate(11,200,1000,50),new_id).signal_id==original,
          "retired overlap lost its existing row identity");
    check(overlap.observe(candidate(12,2100,3100,200),new_id).signal_id==original,
          "losing overlap observation failed to reactivate its group");

    History bounded;std::uint64_t allocated=0;const auto alloc=[&]{return ++allocated;};
    const auto active=bounded.observe(candidate(1,100,1100,80),alloc).signal_id;
    for(std::uint64_t i=2;i<=3*History::capacity;++i) {
        bounded.retire_unreferenced([&](auto id){return id==active;});
        check(bounded.observe(candidate(i,i*20000,i*20000+1000,90),alloc).selected && bounded.size()<=History::capacity,
              "abandoned incomplete history exhausted the bounded table");
    }
    check(bounded.observe(candidate(1,100,1100,80),alloc).signal_id==active,"retirement evicted an active pending row");
}
void bounded_history_preserves_active_rows() {
    History history; std::uint64_t ids = 0;
    const auto allocate = [&] { return ++ids; };
    auto active = candidate(1, 100, 1100, 80);
    const auto active_id = history.observe(active, allocate).signal_id;
    for (std::uint64_t i = 1; i < History::capacity; ++i) {
        auto finished = candidate(i + 1, i * 10000, i * 10000 + 1000, 80); finished.complete = true;
        check(history.observe(finished, allocate).selected, "completed history fixture was unexpectedly merged");
    }
    auto next = candidate(1000, 1000000, 1001000, 100);
    check(history.observe(next, allocate).selected && history.size() == History::capacity,
          "completed history was not evicted at the fixed bound");
    check(history.observe(active, allocate).signal_id == active_id,
          "history eviction discarded an active pending reception");

    History pending; ids = 0;
    for (std::uint64_t i = 0; i < History::capacity; ++i)
        check(pending.observe(candidate(i + 1, i * 10000, i * 10000 + 1000, 80), allocate).selected,
              "active capacity fixture was unexpectedly merged");
    const auto refused = pending.observe(next, allocate);
    check(refused.limited && !refused.selected && refused.signal_id == 0 &&
          pending.size() == History::capacity && ids == History::capacity,
          "full active history grew or evicted a pending row");
}
void burst_calendar_continuation() {
    using datapump::modem::SymbolBurstCoordinate;
    const auto earlier=[] {
        auto c=candidate(1,1000,2000,100);c.symbol_samples=1000;c.absence_samples=9000;
        c.source_identity=History::BurstIdentity{{100,3},{190,3},true,true};return c;
    };
    const auto later=[] {
        auto c=candidate(2,10407,11407,80);c.symbol_samples=1000;c.absence_samples=9000;
        c.source_identity=History::BurstIdentity{{190,3},{280,3},true,true};return c;
    };
    for(unsigned variant=0;variant<10;++variant) {
        History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
        auto first=earlier(),next=later();
        switch(variant) {
        case 1:next.burst_identity()->first.epoch+=30;break;
        case 2:++next.burst_identity()->first.phase;break;
        case 3:next.symbol_samples*=2;break;
        case 4:++next.family;break;
        case 5:next.frequency+=100;break;
        case 6:first.complete=true;break;
        case 7:next.source_identity=std::monostate{};break;
        case 9:next.score=120;break;
        default:break;
        }
        const auto original=history.observe(first,allocate);
        if(variant==8)history.retire_unreferenced([](auto){return false;});
        const auto result=history.observe(next,allocate);
        check((result.signal_id==original.signal_id)==(variant==0),
              "Burst history did not require the exact active same-profile next calendar address");
    }
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto original=history.observe(earlier(),allocate);
    auto advanced=earlier();advanced.end=11000;advanced.score=300;
    advanced.burst_identity()->next=SymbolBurstCoordinate{280,3};history.observe(advanced,allocate);
    history.observe(earlier(),allocate); // A provisional replay must not rewind nextC.
    auto continuation=later();continuation.id=3;continuation.first=19407;continuation.end=20407;
    continuation.score=200;continuation.burst_identity()->first=SymbolBurstCoordinate{280,3};
    continuation.burst_identity()->next=SymbolBurstCoordinate{370,3};
    check(history.observe(continuation,allocate).signal_id==original.signal_id && ids==1,
          "A stale Burst observation rewound its owner's next calendar address");
    auto partial=earlier();partial.source_identity=History::BurstIdentity{{100,3},{190,3},false,true};
    check(!partial.burst_first() && partial.burst_next() && partial.burst_next()->epoch==190,
          "typed Burst identity lost independent first/next validity");
    partial.burst_identity()->first_valid=true;partial.burst_identity()->next_valid=false;
    check(partial.burst_first() && !partial.burst_next(),"typed Burst identity invented an absent next calendar address");
}
void helper_source_domains_remain_independent() {
    using datapump::modem::PatternSourceIdentity;
    if constexpr(sizeof(void*)==8 && sizeof(std::optional<datapump::modem::SymbolBurstCoordinate>)==24) {
        check(sizeof(History::Candidate)==160 && sizeof(History)==45072,
              "source identity changed the ordinary candidate/history workspace layout");
    }
    const auto first=[] {
        auto c=candidate(1,1000,40000,80);c.symbol_samples=1000;c.timing_tolerance=10;
        c.helper_assisted=true;c.source_identity=PatternSourceIdentity{.epoch=100,.first_symbol=0,.sample_rate=1000};return c;
    };
    const auto later=[] {
        auto c=candidate(2,11000,12000,100);c.symbol_samples=1000;c.timing_tolerance=10;
        c.source_identity=PatternSourceIdentity{.epoch=100,.first_symbol=0,.sample_rate=1000};return c;
    };
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    auto old=first(),next=later();
    const auto a=history.observe(old,allocate),b=history.observe(next,allocate);
    check(a.selected && b.selected && a.signal_id!=b.signal_id && history.size()==2 && ids==2 &&
          b.superseded_ids.empty(),"helper-born independent keyed address withdrew or merged an established row");
    old.end=45000;old.score=120;old.helper_assisted=false; // A refresh cannot erase past helper involvement.
    check(history.observe(old,allocate).signal_id==a.signal_id,"helper source progress changed its original pending identity");
    next.complete=true;next.end=13000;next.score=200;
    const auto ended=history.observe(next,allocate);
    check(ended.selected && ended.signal_id==b.signal_id && ended.superseded_ids.empty() && history.size()==2,
          "independent helper source completion withdrew another row's prefix");
    for(unsigned variant=0;variant<8;++variant) {
        History ambiguous;ids=0;old=first();next=later();
        switch(variant) {
        case 0:old.helper_assisted=false;break;
        case 1:old.source_identity=std::monostate{};break;
        case 2:std::get<PatternSourceIdentity>(old.source_identity).clock_half_width_ppm=
                   std::numeric_limits<double>::quiet_NaN();break;
        case 3:std::get<PatternSourceIdentity>(next.source_identity).sample_rate=500;break;
        case 4:next.symbol_samples=2000;break;
        case 5:std::get<PatternSourceIdentity>(next.source_identity).first_symbol=10;break;
        case 6:std::get<PatternSourceIdentity>(old.source_identity).epoch=std::numeric_limits<std::uint64_t>::max();break;
        case 7:std::get<PatternSourceIdentity>(old.source_identity).clock_half_width_ppm=1e6;break;
        }
        const auto original=ambiguous.observe(old,allocate),observed=ambiguous.observe(next,allocate);
        const bool separate=variant!=4;
        check((observed.signal_id!=original.signal_id)==separate && ids==(separate?2U:1U),
              "physical births depended on Helper/source metadata or changed profile competition");
    }
    History rebased;ids=0;old=first();const auto original=rebased.observe(old,allocate);
    old.first=11000;old.end=45000;rebased.observe(old,allocate);
    next=later();next.first=21000;next.end=22000;
    check(rebased.observe(next,allocate).signal_id!=original.signal_id && ids==2,
          "a rebased helper source replaced a different established physical birth");
    History replaced;ids=0;old=first();const auto helper_row=replaced.observe(old,allocate);
    auto alias=old;alias.id=3;alias.helper_assisted=false;alias.score=100;
    check(replaced.observe(alias,allocate).signal_id==helper_row.signal_id && ids==1,
          "a stronger same-source alias abandoned its original GUI row");
    next=later();next.score=200;
    const auto independent=replaced.observe(next,allocate);
    check(independent.selected && independent.signal_id!=helper_row.signal_id && ids==2 &&
          independent.superseded_ids.empty(),"alias owner replacement erased durable helper provenance");
}
void established_helper_rows_survive_widening_clock_cones() {
    using datapump::modem::PatternSourceIdentity;
    const PatternSourceIdentity source{.epoch=100,.first_symbol=0,.clock_half_width_ppm=1000,.sample_rate=1000};
    auto a=candidate(1,1000,20000,80),b=candidate(2,11000,12000,100);
    a.symbol_samples=b.symbol_samples=1000;a.timing_tolerance=b.timing_tolerance=10;
    a.helper_assisted=true;a.source_identity=b.source_identity=source;
    check(datapump::modem::pattern_source_identities_disjoint(source,a.first,a.end,1000,source,b.first,b.end,1000),
          "long helper row fixture did not begin with independently proved source domains");
    History history;std::uint64_t ids=0;const auto allocate=[&]{return ++ids;};
    const auto first=history.observe(a,allocate),second=history.observe(b,allocate);
    check(first.signal_id!=second.signal_id && ids==2,"independent helper sources failed to publish separate rows");
    a.end=1000000000;a.score=120;
    check(history.observe(a,allocate).signal_id==first.signal_id,"long helper progress lost its original row");
    b.end=a.end;b.score=200;
    check(!datapump::modem::pattern_source_identities_disjoint(source,a.first,a.end,1000,source,b.first,b.end,1000),
          "long helper row fixture did not reach an inconclusive clock-cone overlap");
    const auto progressed=history.observe(b,allocate);
    check(progressed.selected && progressed.signal_id==second.signal_id && progressed.superseded_ids.empty() &&
          history.size()==2 && ids==2,"widening helper clock cones merged established rows or withdrew an old prefix");
    a.complete=true;a.score=300;const auto ended_a=history.observe(a,allocate);
    b.complete=true;b.score=400;const auto ended_b=history.observe(b,allocate);
    check(ended_a.signal_id==first.signal_id && ended_b.signal_id==second.signal_id &&
          ended_a.superseded_ids.empty() && ended_b.superseded_ids.empty() && history.size()==2,
          "independent established helper rows lost their identities at completion");

    // Affirmatively identical source anchors retain ordinary alias merging,
    // even when separate initial RF measurements first produced two rows.
    History aliases;ids=0;a=candidate(3,1000,20000,80);b=candidate(4,1000,12000,100);
    a.symbol_samples=b.symbol_samples=1000;a.source_identity=b.source_identity=source;a.helper_assisted=true;
    a.frequency=1480;b.frequency=1520;
    const auto original=aliases.observe(a,allocate),other=aliases.observe(b,allocate);
    check(original.signal_id!=other.signal_id,"same-source control did not begin with distinct RF observations");
    b.frequency=a.frequency;b.score=200;
    const auto merged=aliases.observe(b,allocate);
    check(merged.selected && merged.signal_id==original.signal_id && aliases.size()==1,
          "affirmatively identical helper source anchors stopped merging ordinary aliases");
    for(unsigned invalid=0;invalid<3;++invalid) {
        History unknown;ids=0;a=candidate(5,1000,20000,80);b=candidate(6,1000,12000,100);
        auto identity=source;
        if(invalid==0){identity.epoch=std::numeric_limits<std::uint64_t>::max();identity.first_symbol=1;}
        if(invalid==1)identity.first_symbol=std::numeric_limits<std::uint64_t>::max();
        if(invalid==2)a.end=b.end=std::numeric_limits<std::uint64_t>::max();
        a.symbol_samples=b.symbol_samples=1000;a.source_identity=b.source_identity=identity;a.helper_assisted=true;
        a.frequency=1480;b.frequency=1520;
        const auto old=unknown.observe(a,allocate),separate=unknown.observe(b,allocate);
        b.frequency=a.frequency;b.score=200;
        const auto still_separate=unknown.observe(b,allocate);
        check(old.signal_id!=separate.signal_id && still_separate.signal_id==separate.signal_id &&
              still_separate.superseded_ids.empty() && unknown.size()==2,
              "identical but overflowing source addresses merged previously established rows");
    }
}
}
int main() {
    try {
        source_address_sequence_geometry();
        helper_source_handoff();
        helper_missing_prefix_handoff();
        helper_nominal_support_geometry();
        helper_physical_birth_ownership();
        helper_physical_birth_capacity_boundary();
        ordinary_physical_birth_ownership();
        helper_nominal_suffix_arbitration();
        amplitude_passband_aliases();
        optional_history_priority();
        deferred_provenance_growth();
        immediate_progress_and_physical_completion();
        incompatible_same_duration_clocks_stay_independent();
        stronger_profile_replaces_prefix();
        stronger_symbol_after_prefix_completion();
        bridges_retire_every_fragment_in_any_order();
        equal_evidence_uses_intrinsic_geometry();
        supported_media_outweighs_overconfident_short_prefixes();
        unobserved_positions_add_no_selection_support();
        losing_spans_cannot_poison_later_receptions();
        alias_storage_is_bounded_without_hiding_owner_progress();
        separate_messages_and_families();
        sample_clock_and_long_absence();
        pending_fragments_must_share_the_long_symbol_clock();
        bounded_history_preserves_active_rows();
        retired_history_preserves_active_aliases();
        burst_calendar_continuation();
        helper_source_domains_remain_independent();
        established_helper_rows_survive_widening_clock_cones();
        std::cout << "live reception arbitration tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

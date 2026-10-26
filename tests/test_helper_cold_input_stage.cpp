#include "../src/live_cold_input_stage.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace ci=datapump::live::detail::cold_input;
namespace {
void check(bool value,const char* message) {
    if(!value){std::cerr<<message<<'\n';std::abort();}
}
struct Prepared {std::uint64_t serial;std::string key_profile;};
struct Block {
    std::vector<float> pcm;
    std::shared_ptr<const Prepared> prepared;
    std::uint64_t original_revision=0,clear_generation=0,source_origin=0,view_id=0;
    double observed_epoch=0,seconds_per_frame=0,timing_uncertainty=0;
    std::uint64_t original_helper_date=0,stop_association=0;
    std::vector<std::uint8_t> immutable_metadata;
    std::shared_ptr<unsigned> moves;
    Block()=default;
    Block(Block&& other) noexcept
        :pcm(std::move(other.pcm)),prepared(std::move(other.prepared)),
         original_revision(other.original_revision),clear_generation(other.clear_generation),
         source_origin(other.source_origin),view_id(other.view_id),observed_epoch(other.observed_epoch),
         seconds_per_frame(other.seconds_per_frame),timing_uncertainty(other.timing_uncertainty),
         original_helper_date(other.original_helper_date),stop_association(other.stop_association),
         immutable_metadata(std::move(other.immutable_metadata)),moves(std::move(other.moves)) {if(moves)++*moves;}
    Block(const Block&)=delete;
    Block& operator=(const Block&)=delete;
    Block& operator=(Block&&)=delete;
};
using Stage=ci::Stage<Block>;
const ci::Identity identity{11,29,31,43};
const ci::JoinProof joined{true,true};
const ci::Eligibility cold{29,true,128ULL*6000};
ci::Limits limits(std::size_t grant=Stage::fixed_bytes()+300000) {
    return {identity,6000,100,{50400,48000,60000},grant};
}
Block block(std::size_t samples,float value,std::shared_ptr<const Prepared> prepared={}) {
    Block result;result.pcm.assign(samples,value);result.prepared=std::move(prepared);
    result.original_revision=identity.revision;result.clear_generation=identity.clear_generation;
    result.source_origin=555;result.view_id=777;result.observed_epoch=1800000000.125;
    result.seconds_per_frame=1./6000;result.timing_uncertainty=.003;
    result.original_helper_date=73;result.stop_association=81;
    result.immutable_metadata={9,8,7,6};result.moves=std::make_shared<unsigned>(0);
    return result;
}
ci::Shape shape(const Block& b,std::uint64_t first) {
    // The shared Prepared/move counter allocations pre-exist this fixture's
    // grant and remain caller-owned. All newly retained vector capacities are
    // included; no size-only approximation or hidden nested view allocation.
    return {identity,first,b.pcm.size(),b.pcm.capacity()*sizeof(float)+b.immutable_metadata.capacity(),0};
}
void finish_broad(Stage& stage) {
    stage.release_broad(ci::Reason::explicit_fallback);
    while(auto detached=stage.take(joined))check(!detached->block.pcm.empty(),"drain lost a block");
    check(stage.close(joined),"joined empty broad stage did not close");
}
void ordered_frequency_release_preserves_all_associations() {
    Stage stage(limits());auto prepared=std::make_shared<Prepared>(Prepared{17,"key2/profile3"});
    auto first=block(24000,.25f,prepared);auto second=block(26400,-.5f,prepared);
    const auto* first_data=first.pcm.data();const auto* second_data=second.pcm.data();
    const auto first_charge=shape(first,100);const auto second_charge=shape(second,24100);
    check(stage.append(std::move(first),first_charge,cold)==ci::Append::retained,"first append failed");
    const auto deadline=stage.deadline();
    check(stage.append(std::move(second),second_charge,cold)==ci::Append::retained,"second append failed");
    check(deadline==stage.deadline()&&deadline==60100,"deadline renewed on more captured input");
    check(stage.captured_end()==50500&&stage.processed_frontier_at_entry()==100,"staging advanced processed frontier");
    check(stage.front()->block.pcm.data()==first_data,"staging copied or mutated PCM ownership");
    check(!stage.take(joined),"holding input was drained before release");
    ci::FrequencyCertificate frequency{identity,6000,2500,50500,2250.58,.08,true,true};
    check(stage.startup_ready(),"actual observer startup aperture was not retained");
    check(stage.release_frequency(frequency,cold),"valid independently qualified frequency was declined");
    check(stage.frequency()&&stage.frequency()->first==2500&&stage.frequency()->end==50500,
          "actual frequency support was replaced with staged extent");
    check(!stage.take({false,true})&&!stage.take({true,false}),"unjoined worker or numeric borrower permitted drain");
    auto a=stage.take(joined);auto b=stage.take(joined);
    check(a&&b&&a->shape.first==100&&b->shape.first==24100,"drain reordered retained capture");
    check(a->block.pcm.data()==first_data&&b->block.pcm.data()==second_data,"drain copied PCM");
    for(const auto& item:{&*a,&*b}) {
        check(item->block.prepared==prepared&&item->block.prepared->key_profile=="key2/profile3","Prepared/key/profile association changed");
        check(item->block.original_revision==29&&item->block.clear_generation==31&&item->block.source_origin==555&&
              item->block.view_id==777&&item->block.stop_association==81,"capture/source/view/stop provenance changed");
        check(item->block.observed_epoch==1800000000.125&&item->block.seconds_per_frame==1./6000&&
              item->block.timing_uncertainty==.003&&item->block.original_helper_date==73,
              "later frequency authority redated original timing/phase evidence");
        check(item->block.immutable_metadata==std::vector<std::uint8_t>({9,8,7,6}),"metadata changed");
    }
    check(stage.owned_bytes()==Stage::fixed_bytes()&&stage.processed_frontier_at_entry()==100,"handoff freed fixed reservation or scored input");
    check(stage.close(joined)&&!stage.frequency(),"frequency authority escaped stage lifetime");
}
void invalid_certificates_cannot_release_or_renew() {
    Stage stage(limits());auto value=block(50400,1);auto s=shape(value,100);
    check(stage.append(std::move(value),s,cold)==ci::Append::retained,"setup append failed");
    const auto deadline=stage.deadline();
    const ci::FrequencyCertificate good{identity,6000,2500,50500,2250.58,.08,true,true};
    auto bad=good;bad.identity.revision++;check(!stage.release_frequency(bad,cold),"foreign revision qualified");
    bad=good;bad.identity.capture_domain++;check(!stage.release_frequency(bad,cold),"foreign physical capture clock qualified");
    bad=good;bad.end++;check(!stage.release_frequency(bad,cold),"future fit support qualified");
    bad=good;bad.first=99;check(!stage.release_frequency(bad,cold),"fit before preserved capture qualified");
    bad=good;bad.first=100;check(!stage.release_frequency(bad,cold),"fit exceeding descriptor aperture qualified");
    bad=good;bad.independently_qualified=false;check(!stage.release_frequency(bad,cold),"predicted frequency qualified");
    bad=good;bad.useful=false;check(!stage.release_frequency(bad,cold),"unhelpful frequency qualified");
    bad=good;bad.uncertainty_hz=-1;check(!stage.release_frequency(bad,cold),"invalid uncertainty qualified");
    bad=good;bad.frequency_hz=std::numeric_limits<double>::infinity();check(!stage.release_frequency(bad,cold),"nonfinite frequency qualified");
    check(stage.state()==ci::State::holding&&stage.deadline()==deadline,"declined evidence released or renewed stage");
    finish_broad(stage);
}
void fixed_deadline_drains_every_sample_broad() {
    Stage stage(limits());std::uint64_t first=100,total=0;
    for(unsigned i=0;i<10;++i) {
        auto value=block(6000,static_cast<float>(i));auto s=shape(value,first);
        check(stage.append(std::move(value),s,cold)==ci::Append::retained,"bounded deadline append failed");
        first+=6000;check(stage.deadline()==60100,"cold deadline renewed");
    }
    check(stage.state()==ci::State::broad_drain&&stage.reason()==ci::Reason::deadline,"deadline did not force broad fallback");
    ci::FrequencyCertificate late{identity,6000,12100,60100,2250,.1,true,true};
    check(!stage.release_frequency(late,cold),"late qualification reopened broad fallback");
    first=100;unsigned i=0;
    while(auto value=stage.take(joined)) {
        check(value->shape.first==first&&value->block.pcm.front()==static_cast<float>(i++),"deadline drain lost order/data");
        total+=value->shape.count;first+=value->shape.count;
    }
    check(total==60000&&stage.captured_end()==60100&&stage.processed_frontier_at_entry()==100,"deadline lost PCM or credited processing");
    check(stage.close(joined),"deadline stage did not close");
}
void pressure_does_not_consume_rejected_input() {
    auto a=block(16,1);auto sa=shape(a,100);
    Stage stage(limits(Stage::fixed_bytes()+sa.new_dynamic_bytes));
    check(stage.append(std::move(a),sa,cold)==ci::Append::retained,"exact grant refused first block");
    auto b=block(16,2);auto sb=shape(b,116);const auto* b_data=b.pcm.data();const auto moves=*b.moves;
    check(stage.append(std::move(b),sb,cold)==ci::Append::release_broad,"pressure failed to release optional stage");
    check(b.pcm.data()==b_data&&b.pcm.size()==16&&*b.moves==moves,"pressure discarded/moved rejected PCM");
    check(stage.reason()==ci::Reason::storage_pressure&&stage.size()==1,"pressure lost retained input");
    stage.release_broad(ci::Reason::reference_lost);
    check(stage.reason()==ci::Reason::storage_pressure,"broad release reason renewed");
    auto detached=stage.take(joined);check(detached&&detached->block.pcm.front()==1,"pressure did not preserve first block");
    check(stage.close(joined),"pressure stage did not close");
    // The caller must feed detached then b through ordinary BROAD acquisition.
}
void crossing_deadline_preserves_unstaged_suffix() {
    Stage stage(limits());auto a=block(59000,1);auto sa=shape(a,100);
    check(stage.append(std::move(a),sa,cold)==ci::Append::retained,"deadline setup append failed");
    check(stage.input_room_samples()==1000,"remaining fixed aperture room changed");
    auto b=block(2000,2);auto sb=shape(b,59100);const auto* data=b.pcm.data();
    check(stage.append(std::move(b),sb,cold)==ci::Append::release_broad&&b.pcm.data()==data&&b.pcm.size()==2000,
          "deadline crossing consumed or truncated original suffix");
    check(stage.reason()==ci::Reason::deadline&&stage.captured_end()==59100&&stage.deadline()==60100,
          "deadline crossing renewed or falsely advanced retained capture");
    auto first=stage.take(joined);
    check(first&&first->shape.count==59000&&stage.close(joined),"deadline crossing lost retained prefix");
    // Ordinary caller receives first, then all 2000 untouched samples in b.
}
void mismatch_and_owner_changes_yield_without_drop() {
    Stage stage(limits());auto a=block(16,1);auto sa=shape(a,100);
    check(stage.append(std::move(a),sa,cold)==ci::Append::retained,"setup append failed");
    auto b=block(16,2);auto sb=shape(b,117);const auto* data=b.pcm.data();
    check(stage.append(std::move(b),sb,cold)==ci::Append::release_broad&&b.pcm.data()==data,
          "capture discontinuity was silently concatenated or consumed");
    check(stage.reason()==ci::Reason::capture_mismatch,"capture mismatch reason missing");finish_broad(stage);
    Stage short_owner(limits());auto c=block(16,3);auto sc=shape(c,100);
    check(short_owner.append(std::move(c),sc,{30,true,59ULL*6000})==ci::Append::release_broad&&!c.pcm.empty(),
          "one short owner was staged behind helper acquisition");finish_broad(short_owner);
    Stage active_owner(limits());auto d=block(16,4);auto sd=shape(d,100);
    check(active_owner.append(std::move(d),sd,{29,false,128ULL*6000})==ci::Append::release_broad&&!d.pcm.empty(),
          "active payload/partial owner was staged");finish_broad(active_owner);
    Stage changing(limits());auto e=block(50400,5);auto se=shape(e,100);
    check(changing.append(std::move(e),se,cold)==ci::Append::retained,"setup append failed");
    check(changing.release_frequency({identity,6000,2500,50500,2250,.1,true,true},cold),"setup frequency failed");
    changing.reevaluate({30,false,128ULL*6000});
    check(changing.state()==ci::State::broad_drain&&!changing.frequency(),"new mandatory owner retained frequency drain authority");
    finish_broad(changing);
}
void finite_slots_and_unfunded_stage_preserve_caller_input() {
    using Tiny=ci::Stage<Block,1>;auto l=limits(Tiny::fixed_bytes()+1000);Tiny tiny(l);
    auto a=block(16,1);auto sa=shape(a,100);check(tiny.append(std::move(a),sa,cold)==ci::Append::retained,"tiny append failed");
    auto b=block(16,2);auto sb=shape(b,116);check(tiny.append(std::move(b),sb,cold)==ci::Append::release_broad&&!b.pcm.empty(),"slot pressure consumed input");
    check(tiny.take(joined).has_value()&&tiny.close(joined),"tiny drain failed");
    Stage unfunded(limits(Stage::fixed_bytes()-1));auto c=block(16,3);auto sc=shape(c,100);
    check(unfunded.append(std::move(c),sc,cold)==ci::Append::already_releasing&&!c.pcm.empty(),"unfunded stage consumed PCM");
    check(unfunded.close(joined),"unfunded stage did not close");
}
void native_queue_charge_is_transferred_without_duplicate_storage() {
    Stage stage(limits(Stage::fixed_bytes()));auto a=block(64,1);auto s=shape(a,100);
    s.externally_charged_bytes=s.new_dynamic_bytes;s.new_dynamic_bytes=0;const auto* data=a.pcm.data();
    check(stage.append(std::move(a),s,cold)==ci::Append::retained,"already funded queue ownership was double charged");
    check(stage.owned_bytes()==Stage::fixed_bytes()&&stage.externally_charged_bytes()==s.externally_charged_bytes,
          "external queue charge disappeared or became free memory");
    stage.release_broad(ci::Reason::reference_lost);auto detached=stage.take(joined);
    check(detached&&detached->block.pcm.data()==data&&detached->shape.externally_charged_bytes==s.externally_charged_bytes,
          "native queue allocation copied/lost at handoff");check(stage.close(joined),"native stage close failed");
}
void invalidation_is_explicit_joined_and_never_completion() {
    Stage stage(limits());auto a=block(16,1);auto s=shape(a,100);
    check(stage.append(std::move(a),s,cold)==ci::Append::retained,"setup append failed");
    check(!stage.abandon_invalidated({identity,false},joined),"uninvalidated capture discarded");
    auto wrong=identity;wrong.revision++;
    check(!stage.abandon_invalidated({wrong,true},joined),"foreign capture invalidation discarded input");
    check(!stage.abandon_invalidated({identity,true},{false,true}),"unjoined observer allowed invalidated destruction");
    check(stage.size()==1&&stage.captured_end()==116&&stage.processed_frontier_at_entry()==100,"failed invalidation altered capture");
    check(stage.abandon_invalidated({identity,true},joined)&&stage.empty()&&stage.state()==ci::State::closed&&
          stage.reason()==ci::Reason::capture_invalidated&&stage.processed_frontier_at_entry()==100,
          "ordinary invalidation fabricated processing/completion or failed release");
}
void malformed_apertures_and_lifetime_diagnostics() {
    auto l=limits();l.aperture.maximum_fit_samples=l.aperture.startup_samples+1;
    bool rejected=false;try {Stage stage(l);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"inconsistent aperture accepted");
    l=limits();l.first_sample=UINT64_MAX-10;rejected=false;
    try {Stage stage(l);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected,"deadline overflow accepted");
    unsigned violations=0;
    {Stage stage(limits(),{&violations,[](void* p)noexcept{++*static_cast<unsigned*>(p);}});}
    check(violations==1,"unclosed lifecycle violation was not diagnosed");
}
void descriptor_bounded_policy_is_explicit_and_uses_actual_fixed_plan() {
    constexpr auto bounded=ci::EligibilityPolicy::descriptor_bounded;
    const ci::Eligibility a{29,true,87040,bounded,true}; // actual14.222...s at6120Hz
    const ci::Eligibility eme{29,true,768000,bounded,true}; //128s at6000Hz
    check(!ci::Eligibility{29,true,87040}.accepts(6120,61200),"default60s policy silently broadened");
    check(ci::Eligibility{1,true,60ULL*6000}.accepts(6000),"default60s boundary changed");
    check(!ci::Eligibility{1,true,60ULL*6000-1}.accepts(6000),"default sub60s owner accepted");
    check(a.accepts(6120,61200)&&eme.accepts(6000,60000),"explicit A/EME descriptor policy declined");
    check(ci::Eligibility{1,true,61200,bounded,true}.accepts(6120,61200)&&
          !ci::Eligibility{1,true,61199,bounded,true}.accepts(6120,61200),"descriptor deadline boundary changed");
    check(!a.accepts(6120,90000),"actual longer plan was replaced with a10s assumption");
    check(!a.accepts(6120,0)&&!a.accepts(0,61200),"zero deadline/rate accepted");
    check(!ci::Eligibility{29,true,5120,bounded,true}.accepts(14400,144000),"B short profile accepted");
    const ci::Eligibility controls[]{
        {0,true,87040,bounded,true}, {29,false,87040,bounded,true},
        {29,true,0,bounded,true}, {30,true,59000,bounded,true},
        {29,true,87040,bounded,false},
    };
    for(const auto& owners:controls) {
        Stage stage(limits());auto value=block(16,1);auto s=shape(value,100);const auto* data=value.pcm.data();
        check(stage.append(std::move(value),s,owners)==ci::Append::release_broad&&value.pcm.data()==data,
              "zero/active/mixed/unsupported owner control consumed PCM");
        check(stage.reason()==ci::Reason::owner_no_longer_cold&&stage.empty(),"ineligible bounded policy did not yield broad");
        finish_broad(stage);
    }
    auto plan=limits();plan.sample_rate=6120;plan.aperture={51408,48960,61200};
    Stage stage(plan);auto first=block(24000,.25f);auto second=block(27408,-.5f);
    first.seconds_per_frame=second.seconds_per_frame=1./6120;
    const auto* first_data=first.pcm.data();const auto* second_data=second.pcm.data();
    auto sf=shape(first,100);auto ss=shape(second,24100);
    check(stage.append(std::move(first),sf,a)==ci::Append::retained,"A first cold input was declined");
    check(stage.append(std::move(second),ss,a)==ci::Append::retained&&stage.startup_ready(),"A actual startup aperture failed");
    check(stage.deadline()==61300&&stage.captured_end()==51508&&stage.processed_frontier_at_entry()==100,
          "A policy changed nonrenewable deadline or processed frontier");
    check(stage.release_frequency({identity,6120,2548,51508,2062.5,.1,true,true},a),"A actual-support frequency release failed");
    auto unsupported=a;unsupported.all_current_and_admissible_supported_and_cold=false;
    stage.reevaluate(unsupported);
    check(stage.state()==ci::State::broad_drain&&!stage.frequency(),"new unsupported admissible profile retained conditional authority");
    auto left=stage.take(joined);auto right=stage.take(joined);
    check(left&&right&&left->block.pcm.data()==first_data&&right->block.pcm.data()==second_data&&
          left->shape.first==100&&right->shape.first==24100&&stage.close(joined),
          "descriptor policy change lost/reordered retained A input");
    auto longer=plan;longer.aperture.cold_samples=90000;
    Stage actual_plan(longer);auto value=block(16,1);auto sv=shape(value,100);
    check(actual_plan.append(std::move(value),sv,a)==ci::Append::release_broad&&!value.pcm.empty(),
          "Stage failed to pass its actual fixed plan limit into eligibility");
    finish_broad(actual_plan);
    Stage release_check(plan);auto pending=block(16,1);auto sp=shape(pending,100);
    check(release_check.append(std::move(pending),sp,a)==ci::Append::retained,"release control setup failed");
    check(!release_check.release_frequency({identity,6120,100,116,2062.5,.1,true,true},unsupported)&&
          release_check.state()==ci::State::broad_drain&&release_check.size()==1,
          "frequency release ignored current/admissible supported-cold proof");
    finish_broad(release_check);
}
void raw_support_lattice_enclosure_preserves_fit_and_deadline() {
    const auto maximum=ci::maximum_support_image_samples(192000,24000,6000);
    check(maximum&&*maximum==48001,"8s raw aperture lost its outward logical edge sample");
    check(ci::maximum_support_image_samples(48000,6000,6000)==48000,
          "equal sample grids added an unnecessary rounding sample");
    check(ci::maximum_support_image_samples(12000,6000,24000)==48000,
          "aligned integer upsampling added an unnecessary rounding sample");
    check(ci::maximum_support_image_samples(5,6,4)==4,
          "noninteger grid image bound rounded twice");
    check(ci::maximum_support_image_samples(UINT64_MAX,1,1)==UINT64_MAX&&
          !ci::maximum_support_image_samples(UINT64_MAX,1,2)&&
          !ci::maximum_support_image_samples(1,0,6000)&&
          !ci::maximum_support_image_samples(0,24000,6000),"support image overflow/unknown grid did not decline");
    // Independent actual failure oracle: raw [1581,193581) contains192000
    // samples. /4 gives395.25..48395.25, so outward dates are [395,48396).
    // Neither fit endpoint may be moved inward to make the nominal48000 fit.
    auto plan=limits();plan.first_sample=0;plan.aperture={50401,*maximum,60000};
    Stage stage(plan);auto input=block(50401,1);const auto* pcm=input.pcm.data();
    const auto original_shape=shape(input,0);
    check(stage.append(std::move(input),original_shape,cold)==ci::Append::retained,
          "lattice fit setup failed");
    const auto deadline=stage.deadline();
    const ci::FrequencyCertificate actual{identity,6000,395,48396,2450.89347667642,.024410784723386317,true,true};
    auto overlong=actual;overlong.end=48397; // raw [1581,193585), beyond the8s aperture.
    check(!stage.release_frequency(overlong,cold)&&stage.state()==ci::State::holding,
          "genuinely overlong support used the rounding allowance");
    check(stage.release_frequency(actual,cold)&&stage.frequency()->first==395&&stage.frequency()->end==48396,
          "actual EME raw fit could not release its exact outward logical support");
    check(stage.deadline()==deadline&&deadline==60000&&stage.processed_frontier_at_entry()==0,
          "support enclosure renewed deadline or advanced ordinary processing");
    auto detached=stage.take(joined);
    check(detached&&detached->block.pcm.data()==pcm&&detached->shape.count==50401&&stage.close(joined),
          "support enclosure altered ordinary PCM ownership");
    auto nominal=plan;nominal.aperture.maximum_fit_samples=48000;
    Stage original(nominal);auto same_input=block(50401,1);const auto s=shape(same_input,0);
    check(original.append(std::move(same_input),s,cold)==ci::Append::retained&&
          !original.release_frequency(actual,cold),"original EME support-size failure was not reproduced");
    finish_broad(original);
}
}
int main() {
    ordered_frequency_release_preserves_all_associations();
    invalid_certificates_cannot_release_or_renew();
    fixed_deadline_drains_every_sample_broad();
    pressure_does_not_consume_rejected_input();
    crossing_deadline_preserves_unstaged_suffix();
    mismatch_and_owner_changes_yield_without_drop();
    finite_slots_and_unfunded_stage_preserve_caller_input();
    native_queue_charge_is_transferred_without_duplicate_storage();
    invalidation_is_explicit_joined_and_never_completion();
    malformed_apertures_and_lifetime_diagnostics();
    descriptor_bounded_policy_is_explicit_and_uses_actual_fixed_plan();
    raw_support_lattice_enclosure_preserves_fit_and_deadline();
    std::cout<<"cold input stage contract passed\n";
}

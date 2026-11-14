#include "appraisal/hf_public_diagnostic.hpp"
#include "appraisal/hf_public_receiver.hpp"
#include "triad_receiver_coarse.hpp"
#include "triad_receiver_executor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <numeric>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace datapump;
using namespace datapump::triad::v3;
using namespace datapump::triad::v3::receiver_detail;
using datapump::triad::probe::Json;
constexpr std::size_t workspace_limit=256*1024*1024;
unsigned checks=0;
void check(bool value,const char* text) {++checks;if (!value) throw std::runtime_error(text);}
template<class F> void rejects(F&& f,const char* text) {
    bool rejected=false;
    try {f();} catch (const Error&) {rejected=true;}
    check(rejected,text);
}
const Json& field(const Json& value,const char* key) {return std::get<Json::Object>(value.value).at(key);}
std::uint64_t number(const Json& value,const char* key) {
    const auto& member=field(value,key).value;
    if (const auto* unsigned_value=std::get_if<std::uint64_t>(&member)) return *unsigned_value;
    const auto signed_value=std::get<std::int64_t>(member);
    check(signed_value>=0,"negative count in public receiver report");
    return static_cast<std::uint64_t>(signed_value);
}
bool boolean(const Json& value,const char* key) {return std::get<bool>(field(value,key).value);}

struct Fixture {
    ReceiverConfig config;
    ReceiverSlotPlan slot;
    Crypto key=Crypto::amateur();
    std::vector<Complex> iq;
};
Fixture fixture() {
    Fixture f;
    f.config.sample_rate_hz=497;
    f.config.data_if_hz=1725;
    f.config.noise_variance=.25;
    auto& s=f.slot;
    // Same compact canonical fixture geometry as the generic hook tests.
    // A nonzero slot and sample origin expose accidental telemetry gating.
    s.first_sample=20000;s.index=37;
    s.window_capture_lead_seconds=-17.7;s.source_start_seconds=18.1;
    s.format.sample_rate=497;s.format.inner_rate_hz=120;s.format.added_symbols=1;
    s.format.bit_seconds=.001;s.format.pulse_shape=PulseShape::TaperedRrc;
    s.format.epoch=1793850000;s.format.symbol_ordinal=7000037;s.format.encrypt=true;
    const auto g=geometry(s.format);
    const auto rows=g.source_samples+403;
    check(rows<6000,"public receiver fixture exceeds sample budget");
    s.stop_sample=s.first_sample+rows;
    s.quiet_first_sample=s.first_sample+230;
    s.quiet_stop_sample=s.first_sample+g.source_samples+180;
    f.iq.resize(rows);
    std::uint64_t state=37;
    const auto uniform=[&] {
        state+=0x9e3779b97f4a7c15ULL;
        auto x=state;x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
        x=(x^(x>>27))*0x94d049bb133111ebULL;x^=x>>31;
        return (static_cast<double>(x>>11)+.5)/9007199254740992.;
    };
    for (auto& sample:f.iq) sample=std::polar(std::sqrt(-f.config.noise_variance*std::log(uniform())),
                                            2*std::numbers::pi*uniform());
    return f;
}

void public_support(const Fixture& f) {
    appraisal::HfPublicPhaseReceiver candidate(0);
    const auto extension=candidate.supplement();
    check(extension.supported(f.config,f.slot),"ordinary public fixture unsupported");
    check(appraisal::hf_public_search_workspace(f.config,f.slot,1)>0&&
          appraisal::hf_public_search_workspace(f.config,f.slot,2)>0,
          "ordinary supported lane shape has no positive workspace");
    check(extension.supported(f.config,f.slot),"lane quote changed public support");

    auto low_clock=f.config;auto short_slot=f.slot;
    low_clock.sample_rate_hz=1;low_clock.time_varying_atmospheric_hz=25;
    short_slot.stop_sample=short_slot.first_sample+16;
    check(!extension.supported(low_clock,short_slot),
          "derived block Nyquist failure was deferred past public support");
    auto large=f.config;auto long_slot=f.slot;
    large.sample_rate_hz=1920;large.time_varying_atmospheric_hz=25;
    long_slot.stop_sample=long_slot.first_sample+300*1920;
    long_slot.arrival_upper_seconds=.030;
    check(!extension.supported(large,long_slot),
          "derived full public FFT work failure was deferred past public support");
    auto impossible=f.slot;
    impossible.arrival_lower_seconds=-std::numeric_limits<double>::max();
    impossible.arrival_upper_seconds=std::numeric_limits<double>::max();
    check(!extension.supported(f.config,impossible),"overflowing public arrival span threw or was supported");
    auto huge_clock=f.config;
    huge_clock.sample_rate_hz=std::numeric_limits<double>::max();
    check(!extension.supported(huge_clock,f.slot),"block-size conversion overflow was supported");
}

void small_public_arrival_closure(const Fixture& f) {
    for (const std::size_t cells:{2U,3U,4U}) {
        auto config=f.config;auto slot=f.slot;
        config.clock_bound_ppm=100;
        slot.arrival_lower_seconds=.001;
        slot.arrival_upper_seconds=.001+.001*(cells-1);
        FinePreparation prepared{AdmissionGrid{slot.arrival_lower_seconds,slot.arrival_upper_seconds,
            0,config.clock_bound_ppm,.01},{},{},{},{}};
        check(static_cast<std::size_t>(prepared.grid.arrival_intervals)+1==cells,
              "closure test did not construct the intended public arrival grid");
        prepared.coordinates={{-2,slot.arrival_lower_seconds,0,-100},{3,slot.arrival_upper_seconds,0,100}};
        prepared.varying_coordinates={prepared.coordinates.front()}; // Shared cell must not gain extra mass.
        const auto selected=appraisal::select_hf_public_timing(config,slot,prepared,false,false);
        const auto closed=appraisal::select_hf_public_timing(config,slot,prepared,false,true);
        check(selected.coordinates.size()==2&&selected.canonical_selected_timing_atoms==2&&
              !selected.arrival_grid_closed,"ordinary shadow selection changed under closure support");
        check(closed.canonical_selected_timing_atoms==2&&closed.arrival_grid_closed==(cells<=3),
              "closure lost the deduplicated canonical selection count");
        check(closed.coordinates.size()==(cells<=3?2*cells:2),
              "closure expanded an unsupported arrival grid or omitted a supported cell");
        if (cells<=3) for (const auto& key:prepared.coordinates)
            for (std::size_t a=0;a<cells;++a) {
                const FineCoordinate expected{key.epoch_offset_seconds,
                    prepared.grid.lower+a*((prepared.grid.upper-prepared.grid.lower)/prepared.grid.arrival_intervals),
                    0,key.clock_ppm};
                check(std::ranges::find(closed.coordinates,expected)!=closed.coordinates.end(),
                      "closure omitted a fixed public cell for a retained epoch/clock key");
            }
        for (const auto& coordinate:closed.coordinates)
            check((coordinate.epoch_offset_seconds==-2&&coordinate.clock_ppm==-100)||
                  (coordinate.epoch_offset_seconds==3&&coordinate.clock_ppm==100),
                  "arrival closure introduced a new epoch/clock hypothesis");
        const auto full=appraisal::select_hf_public_timing(config,slot,prepared,true,false);
        check(full.coordinates.size()==13*cells*9&&full.canonical_selected_timing_atoms==2&&
              !full.arrival_grid_closed,"full timing diagnostic changed its public lattice");
        if (cells==3) {
            // A cheap actual native score checks the unchanged full-grid prior,
            // independently of this synthetic public-coordinate selection unit.
            // The Gaussian IQ has no planted arrival/epoch/clock truth.
            ReceiverExecutor executor(2);
            const auto quote=appraisal::hf_public_search_workspace(config,slot,2,false,true);
            const auto result=appraisal::score_hf_public_search(config,slot,f.key,f.iq,prepared,2,quote,
                false,true,false,{},executor);
            check(result.arrival_grid_closed&&result.canonical_selected_timing_atoms==2&&
                  result.timing.size()==6&&result.public_timing_atoms==351,
                  "actual score did not retain closed selection and full public prior");
            check(result.statistics_phase_values_prepared==result.plan.prepared_phase_values&&
                  result.statistics_phase_values_prepared==result.plan.unique_phase_models*result.plan.config.block_count,
                  "slot phase preparation was omitted or counted once per timing atom");
            check(result.statistics_shared_workspace_bytes>0&&
                  result.statistics_shared_workspace_bytes<=result.plan.shared_workspace_bytes&&
                  result.statistics_actual_workspace_peak_bytes<=result.plan.lane_workspace_bytes,
                  "shared/private statistics allocations exceeded their separate quotes");
            const auto extra_lane=2*result.plan.config.block_count*sizeof(Complex)+
                std::min<std::size_t>(4096,f.iq.size())*(2*sizeof(Complex)+sizeof(double));
            check(result.workspace_bytes==result.metadata_reserve_bytes+result.basis_workspace_bytes+
                  result.plan.shared_workspace_bytes+result.stage_workers*(result.plan.lane_workspace_bytes+extra_lane+result.basis_pair_workspace_bytes_per_lane),
                  "slot quote multiplied shared phase preparation by worker count");
            check(result.search_seconds>=result.statistics_preparation_seconds&&
                  result.statistics_preparation_seconds>=0,
                  "slot service interval excluded shared preparation");
            for (const auto& evidence:result.evidence) {
                check(evidence.statistics.preparation_wall_seconds==0&&evidence.statistics.phase_values_prepared==0,
                      "timing evaluator repeated immutable phase preparation");
                check(evidence.statistics.actual_shared_workspace_bytes==result.statistics_shared_workspace_bytes&&
                      evidence.statistics.actual_lane_workspace_bytes<=result.plan.lane_workspace_bytes,
                      "timing evaluator failed to borrow the shared prepared geometry");
            }
            // A different concurrent lane shape must observe the same immutable
            // table and preserve every selected-cell score and mixture order.
            const auto single_quote=appraisal::hf_public_search_workspace(config,slot,1,false,true);
            const auto serial=appraisal::score_hf_public_search(config,slot,f.key,f.iq,prepared,1,single_quote,
                false,true,false,{},executor);
            check(serial.timing==result.timing&&serial.bit_log_e==result.bit_log_e,
                  "shared statistics changed evidence across admitted lane shapes");
            for (std::size_t i=0;i<result.evidence.size();++i)
                for (const std::size_t family:{0U,2U})
                    for (std::size_t bit=0;bit<2;++bit)
                        check(result.evidence[i].statistics.families[family].hypotheses[bit].log_likelihood_ratio==
                              serial.evidence[i].statistics.families[family].hypotheses[bit].log_likelihood_ratio,
                              "shared timing workers changed a selected-cell likelihood");
            for (std::size_t bit=0;bit<2;++bit) {
                double peak=-std::numeric_limits<double>::infinity();
                for (const auto& evidence:result.evidence)
                    peak=std::max(peak,evidence.statistics.families[2].hypotheses[bit].log_likelihood_ratio);
                long double sum=0;
                for (const auto& evidence:result.evidence)
                    sum+=std::exp(static_cast<long double>(evidence.statistics.families[2].hypotheses[bit].log_likelihood_ratio)-peak);
                const auto expected=peak+std::log(sum)-std::log(351.L);
                check(std::abs(result.bit_log_e[2][bit]-expected)<1e-12,
                      "arrival closure renormalized selected timing prior mass");
            }
        }
    }
}

void paired_native_renderer(const Fixture& f) {
    auto config=f.config;auto slot=f.slot;
    slot.format.pulse_shape=PulseShape::Rrc;
    config.data_basis_gain=.73;
    constexpr std::size_t tile=512;
    const std::array<Tap,1> taps{{{0,0}}};
    const auto source_rows=geometry(slot.format).source_samples;
    std::uint64_t used=0,fallbacks=0;
    const auto compare=[&](const ReceiverConfig& c,const ReceiverSlotPlan& s,const MappedBasisRow& row,
                           std::uint64_t first,std::vector<double> offsets,std::span<const Tap> paths,
                           bool must_fallback=false,bool coordinate_guard=false) {
        MappedBasis reference(c,s,f.key,tile,1);
        appraisal::HfPublicBasisPair bank(c,s,f.key,row),canonical(c,s,f.key,row,false);
        std::vector<Complex> a(2*paths.size()*offsets.size()),b(a.size());
        bank.render(reference,paths,first,offsets,a,{});
        canonical.render(reference,paths,first,offsets,b,{});
        check(a==b,"paired renderer changed an exact mapped sample (apart from signed zero)");
        const auto report=bank.report();
        used+=report.bank_calls;fallbacks+=report.fallback_calls;
        if (must_fallback) check(report.fallback_calls==1&&report.bank_calls==0,
                                "unsupported pair shape did not use canonical fallback");
        if (coordinate_guard) check(report.coordinate_guard_fallbacks==1,
                                   "per-sample coordinate equality guard was not exercised");
        check(report.private_source_bytes<=2*geometry(s.format).source_workspace_bytes,
              "native pair sources exceeded their workspace bounds");
    };
    const auto offsets=[](std::size_t n) {std::vector<double> v(n);std::iota(v.begin(),v.end(),0.);return v;};
    for (const auto epoch:{-6,0,6})
        for (const double arrival:{.001,.002,.003,-.001,.0001234567890123456})
            for (const std::uint64_t first:{UINT64_C(0),UINT64_C(257),source_rows-3,source_rows+8})
                compare(config,slot,{epoch,arrival,0,0},first,offsets(first==257?257:31),taps);
    auto no_if=config;no_if.data_if_hz=0;no_if.data_basis_gain=1.37;
    compare(no_if,slot,{3,.003,0,0},1,offsets(1),taps);
    compare(config,slot,{0,.002,.01,0},0,offsets(31),taps,true);
    compare(config,slot,{0,.002,0,1},0,offsets(31),taps,true);
    auto changed_rate=config;changed_rate.sample_rate_hz=496;
    compare(changed_rate,slot,{0,.002,0,0},0,offsets(31),taps,true);
    auto tapered=slot;tapered.format.pulse_shape=PulseShape::TaperedRrc;
    compare(config,tapered,{0,.002,0,0},0,offsets(31),taps,true);
    const std::array<Tap,1> delayed{{{.001,.1}}};
    compare(config,slot,{0,.002,0,0},0,offsets(31),delayed,true);
    auto irregular=offsets(31);for (std::size_t i=1;i<irregular.size();++i) irregular[i]+=.1;
    compare(config,slot,{0,.002,0,0},0,std::move(irregular),taps,true);
    bool found=false;
    for (unsigned exponent=16;exponent<=63&&!found;++exponent)
        for (const double arrival:{.001,.003,.123456789,.3/497}) {
            const auto first=(UINT64_C(1)<<exponent)-4;
            const auto shift=static_cast<long double>(arrival)*config.sample_rate_hz;
            const auto origin=static_cast<long double>(first)-shift;
            for (unsigned j=0;j<31;++j) if (origin+static_cast<long double>(j)!=
                    (static_cast<long double>(first)+static_cast<double>(j))-shift) {
                compare(config,slot,{0,arrival,0,0},first,offsets(31),taps,true,true);
                found=true;break;
            }
            if (found) break;
        }
    check(found,"bounded cases did not exercise nonassociative coordinate fallback");
    check(used>0&&fallbacks>=7,"renderer parity omitted bank or fallback coverage");
    {
        MappedBasis reference(config,slot,f.key,tile,1);
        appraisal::HfPublicBasisPair pair(config,slot,f.key,{0,.002,0,0});
        auto index=offsets(2);std::vector<Complex> output(4);
        rejects([&]{pair.render(reference,taps,UINT64_MAX,index,output,{});},
                "native pair accepted overflowing observation coordinates");
    }
    // Fixed cost discriminator only: 8192 input rows per path, including
    // private source preparation. No timing-based correctness assertion.
    const auto measure=[&](bool enable) {
        MappedBasis reference(config,slot,f.key,tile,1);
        appraisal::HfPublicBasisPair pair(config,slot,f.key,{0,.002,0,0},enable);
        const auto offset=offsets(tile);std::vector<Complex> output(2*tile);
        const auto start=std::chrono::steady_clock::now();
        for (unsigned repeat=0;repeat<4;++repeat)
            for (unsigned chunk=0;chunk<4;++chunk)
                pair.render(reference,taps,chunk*tile,offset,output,{});
        return std::pair{std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count(),pair.report()};
    };
    const auto [old_seconds,old_report]=measure(false);
    const auto [new_seconds,new_report]=measure(true);
    check(old_report.fallback_calls==16&&new_report.bank_calls+new_report.fallback_calls==16,
          "fixed renderer timing omitted declared native work");
    std::cout<<"paired renderer microtiming: reference_seconds="<<old_seconds
             <<" bank_seconds="<<new_seconds<<" bank_calls="<<new_report.bank_calls
             <<" fallback_calls="<<new_report.fallback_calls<<" rows_per_path=8192\n";
    ReceiverExecutor executor(2);
    const FrequencyInterval frequency{0,0};
    slot.arrival_lower_seconds=.001;slot.arrival_upper_seconds=.003;
    const auto coarse=score_coarse(config,slot,f.key,f.iq,{&frequency,1},2,workspace_limit,{},&executor);
    const auto prepared=prepare_fine_coordinates(config,slot,coarse);
    const auto quote=appraisal::hf_public_search_workspace(config,slot,2,false,true);
    const auto fast=appraisal::score_hf_public_search(config,slot,f.key,f.iq,prepared,2,quote,
        false,true,false,{},executor,true);
    const auto reference=appraisal::score_hf_public_search(config,slot,f.key,f.iq,prepared,2,quote,
        false,true,false,{},executor,false);
    check(fast.timing==reference.timing&&fast.bit_log_e==reference.bit_log_e,
          "paired rendering changed complete component evidence or public timing selection");
    check(fast.basis_pair.bank_calls>0&&reference.basis_pair.bank_calls==0,
          "component parity did not compare bank against the existing mapped reference");
    for (std::size_t i=0;i<fast.evidence.size();++i)
        check(fast.evidence[i].energy==reference.evidence[i].energy,
              "paired rendering changed a scored native template energy");
}

void telemetry_cannot_gate_evidence(const Fixture& f) {
    ReceiverExecutor executor(2);
    const FrequencyInterval frequency{0,0};
    const auto coarse=score_coarse(f.config,f.slot,f.key,f.iq,{&frequency,1},2,workspace_limit,{},&executor);
    const auto prepared=prepare_fine_coordinates(f.config,f.slot,coarse);
    check(!prepared.coordinates.empty(),"native acquisition did not produce prepared timing");
    appraisal::HfPublicPhaseReceiver zero_storage(0),bounded_storage(40);
    const auto zero=zero_storage.supplement(),bounded=bounded_storage.supplement();
    check(zero.supported(f.config,f.slot)==bounded.supported(f.config,f.slot),
          "telemetry horizon changes candidate support");
    const auto workspace=bounded.workspace(f.config,f.slot,2);
    check(workspace==zero.workspace(f.config,f.slot,2)&&workspace<workspace_limit,
          "telemetry horizon changes DSP workspace");
    rejects([&] {zero.score(f.config,f.slot,f.key,f.iq,prepared,2,1,{},executor);},
            "actual workspace shortage became an unsupported fallback");
    check(zero.supported(f.config,f.slot),"workspace shortage changed public support");
    const auto a=zero.score(f.config,f.slot,f.key,f.iq,prepared,2,workspace,{},executor);
    const auto b=bounded.score(f.config,f.slot,f.key,f.iq,prepared,2,workspace,{},executor);
    const auto duplicate=bounded.score(f.config,f.slot,f.key,f.iq,prepared,2,workspace,{},executor);
    check(a==b&&b==duplicate,"telemetry bounds or duplicates change scored evidence");
    check(std::isfinite(a[0])&&std::isfinite(a[1]),"actual candidate failed to score finite evidence");
    const auto empty_report=zero_storage.report(),kept_report=bounded_storage.report();
    check(number(empty_report,"executed_slots")==0&&number(empty_report,"telemetry_overflow_reports")==1&&
          std::get<Json::Array>(field(empty_report,"slots").value).empty(),
          "zero storage fabricated a report or lost the overflow count");
    check(number(kept_report,"executed_slots")==1&&number(kept_report,"telemetry_overflow_reports")==0&&
          number(kept_report,"telemetry_duplicate_reports")==1,
          "bounded storage duplicate/overflow counts are incorrect");
    const auto& slots=std::get<Json::Array>(field(kept_report,"slots").value);
    check(slots.size()==1&&number(slots[0],"slot_index")==37&&boolean(slots[0],"component_complete"),
          "candidate telemetry lost the actual nonzero public slot");
    check(number(slots[0],"actual_fft_row_stages")>0&&number(slots[0],"phase_models")==97,
          "telemetry parity bypassed the actual phase family");
}
} // namespace

int main() {
    try {
        const auto f=fixture();public_support(f);small_public_arrival_closure(f);paired_native_renderer(f);telemetry_cannot_gate_evidence(f);
        std::cout<<"HF public receiver passed "<<checks<<" checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"HF public receiver failed after "<<checks<<" checks: "<<e.what()<<'\n';
        return 1;
    }
}

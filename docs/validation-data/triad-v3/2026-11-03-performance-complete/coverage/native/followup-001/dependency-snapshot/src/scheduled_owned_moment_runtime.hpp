#pragma once
#include "scheduled_sparse_owned_spans.hpp"
#include "scheduled_owned_moment_correction.hpp"
#include "scheduled_owned_field_errors.hpp"
#include "scheduled_polynomial_chip_runtime.hpp"
#include <optional>

namespace datapump::modem::detail::scheduled_owned_moment_runtime {
namespace sparse=scheduled_sparse_owned_spans;
namespace orig=scheduled_original_partition;
namespace corr=scheduled_owned_moment_correction;
namespace poly=scheduled_polynomial_chip;
namespace color=scheduled_cold_colored;
namespace cert=scheduled_acquisition_certificate;
namespace field=scheduled_owned_field_errors;
using Complex=std::complex<double>;
struct Counters {std::uint64_t polynomial_cells=0,direct_cells=0,direct_rows=0,edit_rows=0,source_segments=0,polynomial_failures=0,polynomial_refinements=0;};
enum class Failure {none,contract,owner_order,edit_source,edit_pcm,owner_deltas,owner_spans,direct_source,
    original_row,template_radius,direct_pcm,row_fields,direct_count,original_errors,original_fields,
    projection_trace,projection_rank,projection_numeric,projection_nonnested,summary_join,whole_math,whole_fields,whole_summary};
inline const char* failure_name(Failure f) {
    switch(f) {
#define OWNED_FAILURE(name) case Failure::name:return #name;
    OWNED_FAILURE(none) OWNED_FAILURE(contract) OWNED_FAILURE(owner_order) OWNED_FAILURE(edit_source) OWNED_FAILURE(edit_pcm)
    OWNED_FAILURE(owner_deltas) OWNED_FAILURE(owner_spans) OWNED_FAILURE(direct_source) OWNED_FAILURE(original_row)
    OWNED_FAILURE(template_radius) OWNED_FAILURE(direct_pcm) OWNED_FAILURE(row_fields) OWNED_FAILURE(direct_count)
    OWNED_FAILURE(original_errors) OWNED_FAILURE(original_fields) OWNED_FAILURE(projection_trace) OWNED_FAILURE(projection_rank)
    OWNED_FAILURE(projection_numeric) OWNED_FAILURE(projection_nonnested) OWNED_FAILURE(summary_join)
    OWNED_FAILURE(whole_math) OWNED_FAILURE(whole_fields) OWNED_FAILURE(whole_summary)
#undef OWNED_FAILURE
    }
    return "invalid_failure";
}
struct CellDiagnostic {
    Failure failure=Failure::none;std::uint64_t owner=color::no_owner,native_count=0,original_count=0;
    unsigned bit=0;bool polynomial=false,errors_complete=false,originalized=false;
    std::array<double,2> q0_square{},residual_square{};
    std::array<cert::ReferenceErrors,2> errors{};
    std::array<color::FitIntervals,2> fields{};
};
struct Bounds {
    double qmax=0,derivative=0,jump=0,union_energy=0,carrier_radius=0,carrier_magnitude=2;
    std::size_t prefix=512;
};
struct Result {
    std::array<color::Summary,2> chips{};
    std::array<color::FitIntervals,2> whole{};
    Counters counters;
    Failure failure=Failure::contract;std::uint64_t failure_owner=color::no_owner;
    CellDiagnostic terminal,first_refinement;
    bool valid=false;
};
inline cert::Interval ld_point(long double x) {
    return {cert::down(static_cast<double>(x)),cert::up(static_cast<double>(x))};
}
inline double magnitude(cert::Interval a){return std::max(std::abs(a.lo),std::abs(a.hi));}
inline std::optional<std::size_t> source_row(const color::SourceRows& source,std::uint64_t physical,long double translation) {
    const auto value=static_cast<long double>(physical)-translation;
    if(!std::isfinite(value) || value<0 || value>=0x1p52L || std::trunc(value)!=value)return {};
    const auto p=static_cast<std::uint64_t>(value);
    const auto it=std::upper_bound(source.runs.begin(),source.runs.end(),p,[](auto n,const auto& run){return n<run.physical_offset;});
    if(it==source.runs.begin())return {};
    const auto& run=*std::prev(it);
    if(p-run.physical_offset>=run.count)return {};
    return run.first+static_cast<std::size_t>(p-run.physical_offset);
}
inline const poly::SourceModels::Cell* source_cell(const poly::SourceModels& models,std::size_t row) {
    const auto it=std::upper_bound(models.cells.begin(),models.cells.end(),row,[](auto n,const auto& cell){return n<cell.first;});
    if(it==models.cells.begin())return nullptr;
    const auto& cell=*std::prev(it);return row<cell.end?&cell:nullptr;
}
inline bool original_flat(const orig::Piece& piece,std::uint64_t first,std::uint64_t end) {
    if(first>=end || !piece.location0.active)return false;
    if(piece.location0.continuous)return true;
    const auto a=piece.tick0.add_signed(static_cast<long double>(first-piece.anchor_first)*piece.rate);
    const auto b=piece.tick0.add_signed(static_cast<long double>(end-1-piece.anchor_first)*piece.rate);
    return a.sample>=piece.location0.flat_begin && b.sample<piece.location0.flat_end;
}
// Whole-cell source compression proof. Cast-double shaper arguments were
// enclosed once in SourceModels. Query work is O(source segments), never N.
inline bool segment_residual(const color::SourceRows& source,const poly::SourceModels& models,
        const orig::Event& event,const orig::Piece& piece,std::uint64_t first,std::uint64_t end,
        long double translation,const Bounds& b,double& square,std::uint64_t& segments) {
    while(first<end) {
        const auto at=source_row(source,first,translation);if(!at)return false;
        const auto* cell=source_cell(models,*at);
        if(!cell || !cell->full || !cell->unit_gain || !cell->argument_intercept.bounded())return false;
        const auto real=static_cast<long double>(cell->physical)+translation;
        if(std::trunc(real)!=real || real<0 || real>=0x1p52L)return false;
        const auto limit=std::min(end,static_cast<std::uint64_t>(real)+34);
        if(limit<=first || !original_flat(piece,first,limit))return false;
        const auto j0=static_cast<std::uint64_t>(static_cast<long double>(first)-real);
        const auto j1=j0+limit-first-1;
        const auto anchor=cert::plus(ld_point(piece.source0),cert::times(
            ld_point(real-piece.anchor_first),ld_point(piece.rate)));
        const auto rate=cert::minus(ld_point(piece.rate),ld_point(source.key.rate));
        auto difference=cert::plus(cert::minus(anchor,cell->argument_intercept),
            cert::times({static_cast<double>(j0),static_cast<double>(j1)},rate));
        // RN binary64 interval arithmetic already dominates the LD arithmetic;
        // this explicit reserve also covers both final LD→double casts and
        // cancellation at exact representable interval endpoints.
        const auto reserve=cert::mul(16*std::numeric_limits<double>::epsilon(),cert::add(1,
            cert::add(static_cast<double>(event.symbol_samples),cert::add(std::abs(static_cast<double>(piece.source0)),
                std::abs(static_cast<double>((real-piece.anchor_first)*piece.rate))))));
        const auto delta=cert::add(magnitude(difference),reserve);
        if(!std::isfinite(delta) || delta>1)return false;
        const auto fp=cert::mul(2048*std::numeric_limits<double>::epsilon(),cert::add(1,b.qmax));
        const auto smooth=cert::add(cert::mul(color::smooth_gain_upper(),cert::mul(delta,cert::divide(b.derivative,34))),fp);
        const auto count=limit-first;
        auto contribution=cert::mul(static_cast<double>(count),cert::mul(smooth,smooth));
        const auto begin_index=*at,end_index=begin_index+static_cast<std::size_t>(count);
        if(end_index>cell->end)return false;
        // Public sinc endpoints jump at source/C-.5 integer. Only the actual
        // cached rows in the small boundary interval incur that jump. Source
        // arguments are strictly ordered in a qualified full cell.
        const auto margin=static_cast<long double>(cert::add(delta,cert::mul(2*std::numeric_limits<double>::epsilon(),
            cert::add(1,static_cast<double>(event.symbol_samples)))))+1e-10L;
        const auto s0=source.source[begin_index],s1=source.source[end_index-1];
        auto k=static_cast<long long>(std::floor(s0/34-.5L))-1;
        const auto last=static_cast<long long>(std::ceil(s1/34-.5L))+1;
        if(last-k>6)return false;
        std::uint64_t jumps=0;
        for(;k<=last;++k) {
            const auto center=(static_cast<long double>(k)+.5L)*34;
            const auto a=std::lower_bound(source.source.begin()+begin_index,source.source.begin()+end_index,center-margin);
            const auto z=std::upper_bound(a,source.source.begin()+end_index,center+margin);
            jumps+=static_cast<std::uint64_t>(z-a);
        }
        if(jumps>count)return false;
        contribution=cert::add(contribution,cert::mul(static_cast<double>(jumps),cert::add(
            cert::mul(cert::mul(2,smooth),b.jump),cert::mul(b.jump,b.jump))));
        square=cert::add(square,contribution);++segments;first=limit;
    }
    return std::isfinite(square);
}
inline double row_residual(const color::SourceRows& source,std::size_t k,const orig::Evaluation& value,
        const orig::Event& event,const Bounds& b) {
    const auto s=source.source[k];
    const auto d=cert::minus(cert::Interval::point(static_cast<double>(value.within)),cert::Interval::point(static_cast<double>(s)));
    const auto delta=magnitude(d);
    if(!std::isfinite(delta) || delta>1)return std::numeric_limits<double>::infinity();
    const auto fp=cert::mul(2048*std::numeric_limits<double>::epsilon(),cert::add(1,b.qmax));
    auto shape=cert::mul(delta,cert::divide(b.derivative,34));
    const auto margin=static_cast<long double>(cert::add(delta,cert::mul(2*std::numeric_limits<double>::epsilon(),
        cert::add(1,static_cast<double>(event.symbol_samples)))))+1e-10L;
    const auto edge=s/34-.5L;
    if(std::abs(edge-std::round(edge))*34<=margin)shape=cert::add(shape,b.jump);
    const auto gd=cert::minus(cert::Interval::point(value.gain),cert::Interval::point(source.gain[k]));
    return cert::add(cert::add(cert::mul(source.gain[k],shape),cert::mul(magnitude(gd),cert::add(b.qmax,fp))),fp);
}
inline bool valid_bounds(const Bounds& b) {
    return cert::environment<double>() && std::isfinite(b.qmax) && b.qmax>=0 && b.qmax<=32 &&
        std::isfinite(b.derivative) && b.derivative>=0 && std::isfinite(b.jump) && b.jump>=0 &&
        std::isfinite(b.union_energy) && b.union_energy>=0 && std::isfinite(b.carrier_radius) && b.carrier_radius>=0 &&
        std::isfinite(b.carrier_magnitude) && b.carrier_magnitude>0 && b.carrier_magnitude<=2 && b.prefix && b.prefix<=512;
}
// fetch(cell,physical) returns contract_math on the SAME immutable physical
// moments as sample(physical), or unsupported. Its cache borrow is consumed
// before this call returns. sample returns a center/radius enclosing the SAME
// stored raw-block oscillator; row_fields carries that uncertainty directly.
// All result fields are provisional until valid. Unknown returns to Direct.
template<class Fetch,class Sample>
Result contract(const color::SourceRows& source,const poly::SourceModels& models,const orig::Event& event,
        std::span<const orig::Piece> pieces,const sparse::Proof& proof,long double translation,
        corr::Identity identity,const Bounds& b,Fetch&& fetch,Sample&& sample,Counters& work,std::stop_token stop={},bool refine_polynomial=true) {
    Result result;
    if(!valid_bounds(b) || !identity.valid() || !models.matches_native(source) || !proof.complete ||
       proof.identity!=event.identity || proof.native_rate!=source.key.rate || proof.translation!=translation ||
       event.chip_samples!=34 || event.symbol_samples%34 || !proof.original_count ||
       source.key.phase!=event.phase || source.key.index!=event.index || source.key.anchor_sample!=event.anchor.sample ||
       source.key.anchor_fraction!=event.anchor.fraction)return result;
    const auto fail=[&](Failure failure){result.failure=failure;return result;};
    for(std::size_t i=1;i<proof.spans.size();++i)
        if(proof.spans[i].owner<proof.spans[i-1].owner)return fail(Failure::owner_order);
    for(auto& summary:result.chips)summary.owner_set_certified=true;
    std::vector<corr::OwnerCount> owners;
    owners.reserve(proof.owners.size()+proof.native_owners.size());
    std::size_t ai=0,bi=0;
    while(ai<proof.native_owners.size() || bi<proof.owners.size()) {
        const auto a=ai<proof.native_owners.size()?proof.native_owners[ai].owner:color::no_owner;
        const auto z=bi<proof.owners.size()?proof.owners[bi].owner:color::no_owner;
        const auto owner=std::min(a,z);corr::OwnerCount count{owner,0,0};
        if(a==owner)count.native_count=proof.native_owners[ai++].count;
        if(z==owner)count.original_count=proof.owners[bi++].count;
        owners.push_back(count);
    }
    std::vector<corr::RowEdit> edits;edits.reserve(static_cast<std::size_t>(proof.edit_rows));
    for(const auto& edit:proof.edits)for(auto p=edit.first;p<edit.end;++p) {
        orig::check_stop(stop);const auto row=source_row(source,p,translation);if(!row)return fail(Failure::edit_source);
        auto raw=sample(p);if(!raw)return fail(Failure::edit_pcm);raw->q0=source.rows[*row];
        edits.push_back({identity,p,edit.native_owner,edit.original_owner,*raw});++work.edit_rows;
    }
    const corr::SupportProof support{identity,proof.native_count,proof.original_count,true,true};
    std::vector<corr::OwnerDelta> deltas(owners.size());
    if(!corr::build_owner_deltas(support,owners,edits,deltas))return fail(Failure::owner_deltas);
    std::vector<corr::MathCell> cells;cells.reserve(proof.owners.size());
    std::size_t span_at=0;std::array<double,2> whole_qsquare{},whole_residual{};
    struct Candidate {
        corr::Result final;std::array<color::Summary,2> summaries{};
        CellDiagnostic diagnostic;bool valid=false;
    };
    const auto prepare=[&](const corr::MathCell& math,const std::array<double,2>& q0,const std::array<double,2>& residual,
            std::uint64_t native_count,bool polynomial) {
        Candidate out;auto& diagnostic=out.diagnostic;
        diagnostic.owner=math.owner;diagnostic.native_count=native_count;diagnostic.original_count=math.fields[0].count;
        diagnostic.polynomial=polynomial;diagnostic.q0_square=q0;diagnostic.residual_square=residual;
        field::Bounds bounds;bounds.selected_energy_upper=math.fields[0].energy.hi;bounds.union_energy_upper=b.union_energy;
        bounds.q0_square_upper=q0;bounds.residual_square_upper=residual;
        bounds.original_qmax={std::min(b.qmax,cert::add(cert::root(q0[0]),cert::root(residual[0]))),
            std::min(b.qmax,cert::add(cert::root(q0[1]),cert::root(residual[1])))};
        bounds.carrier_radius=b.carrier_radius;bounds.carrier_magnitude=b.carrier_magnitude;bounds.prefix_samples=b.prefix;
        const auto errors=field::make(identity,math.owner,math.fields[0].count,bounds,true);
        diagnostic.errors=errors.fields;diagnostic.errors_complete=errors.complete;
        if(!errors.complete){diagnostic.failure=Failure::original_errors;return out;}
        out.final=corr::originalize(math,errors);diagnostic.originalized=out.final.valid;diagnostic.fields=out.final.fields;
        if(!out.final.valid){diagnostic.failure=Failure::original_fields;return out;}
        for(unsigned bit=0;bit<2;++bit) {
            auto& summary=out.summaries[bit];summary.owner_set_certified=true;summary.retain(out.final.fields[bit]);
            if(!summary.valid || !summary.nested) {
                diagnostic.bit=bit;const auto& fields=out.final.fields[bit];
                const auto trace=cert::plus(fields.cc,fields.ss),det=color::determinant(fields);
                const auto cutoff=cert::times(cert::times(cert::Interval::point(1e-12),trace),trace);
                diagnostic.failure=!summary.nested?Failure::projection_nonnested:
                    (!trace.bounded() || trace.lo<=1e-30?Failure::projection_trace:
                     (!det.bounded() || !cutoff.bounded() || det.lo<=cutoff.hi?Failure::projection_rank:Failure::projection_numeric));
                return out;
            }
        }
        out.valid=true;return out;
    };
    const auto commit=[&](corr::MathCell math,const Candidate& candidate,const std::array<double,2>& q0,const std::array<double,2>& residual) {
        // BOTH bits must pass before cumulative chip/whole state changes.
        auto joined=result.chips;
        for(unsigned bit=0;bit<2;++bit)poly::join(joined[bit],candidate.summaries[bit]);
        if(!joined[0].valid || !joined[1].valid || !joined[0].nested || !joined[1].nested)return false;
        result.chips=joined;
        for(unsigned bit=0;bit<2;++bit){whole_qsquare[bit]=cert::add(whole_qsquare[bit],q0[bit]);
            whole_residual[bit]=cert::add(whole_residual[bit],residual[bit]);}
        cells.push_back(std::move(math));return true;
    };
    for(std::size_t oi=0;oi<owners.size();++oi) {
        orig::check_stop(stop);const auto& owner=owners[oi];if(!owner.original_count)continue;result.failure_owner=owner.owner;
        while(span_at<proof.spans.size() && proof.spans[span_at].owner<owner.owner)++span_at;
        const auto first_span=span_at;while(span_at<proof.spans.size() && proof.spans[span_at].owner==owner.owner)++span_at;
        if(first_span==span_at)return fail(Failure::owner_spans);
        const auto model_it=std::lower_bound(models.cells.begin(),models.cells.end(),owner.owner,[](const auto& c,auto id){return c.owner<id;});
        const auto* cell=model_it!=models.cells.end() && model_it->owner==owner.owner?&*model_it:nullptr;
        corr::MathCell math;std::array<double,2> q0square{},residual{};bool fast=false;
        if(cell && cell->full && owner.native_count==34) {
            const auto real=static_cast<long double>(cell->physical)+translation;
            const bool domain=std::trunc(real)==real && real>=event.native_begin && real+34<=event.native_end &&
                real>=event.history_first && real+34<=event.capture_end;
            double r2=0;bool source_ok=domain;
            for(auto s=first_span;s<span_at && source_ok;++s) {
                const auto& span=proof.spans[s];source_ok=span.piece<pieces.size() && segment_residual(source,models,event,
                    pieces[span.piece],span.first,span.end,translation,b,r2,work.source_segments);
            }
            if(source_ok) {
                const auto native=fetch(*cell,static_cast<std::uint64_t>(real));
                if(native.supported) {
                    math=corr::corrected_math_cell({identity,owner.owner,native.intervals},deltas[oi]);
                    if(math.valid) {
                        for(unsigned bit=0;bit<2;++bit)q0square[bit]=cert::add(cell->q_square[bit],
                            cert::mul(static_cast<double>(deltas[oi].added),cert::mul(b.qmax,b.qmax)));
                        residual={r2,r2};fast=true;++work.polynomial_cells;
                    }
                }
            }
        }
        if(fast) {
            const auto candidate=prepare(math,q0square,residual,owner.native_count,true);
            if(candidate.valid) {
                if(!commit(std::move(math),candidate,q0square,residual))return fail(Failure::summary_join);
                continue;
            }
            if(result.first_refinement.failure==Failure::none)result.first_refinement=candidate.diagnostic;
            ++work.polynomial_failures;
            if(!refine_polynomial){result.terminal=candidate.diagnostic;return fail(candidate.diagnostic.failure);}
            ++work.polynomial_refinements;fast=false;q0square={};residual={};
        }
        if(!fast) {
            ++work.direct_cells;math={identity,owner.owner,{},true};for(auto& f:math.fields)f.valid=true;
            std::uint64_t count=0;
            for(auto s=first_span;s<span_at;++s) {
                const auto& span=proof.spans[s];if(span.piece>=pieces.size())return fail(Failure::owner_spans);
                for(auto p=span.first;p<span.end;++p) {
                    orig::check_stop(stop);const auto k=source_row(source,p,translation);if(!k)return fail(Failure::direct_source);
                    const auto value=sparse::evaluate(event,pieces[span.piece],p);
                    if(!value.valid() || !value.current || value.owner!=owner.owner)return fail(Failure::original_row);
                    const auto radius=row_residual(source,*k,value,event,b);if(!std::isfinite(radius))return fail(Failure::template_radius);
                    auto raw=sample(p);if(!raw)return fail(Failure::direct_pcm);raw->q0=source.rows[*k];
                    std::array<corr::Fields,2> row;if(!corr::row_fields(*raw,row))return fail(Failure::row_fields);
                    ++count;++work.direct_rows;
                    for(unsigned bit=0;bit<2;++bit) {
                        const auto sum=corr::combine(corr::fields(math.fields[bit]),row[bit],true);
                        math.fields[bit]={sum.xc,sum.xs,sum.cc,sum.ss,sum.cs,sum.energy,count,count,count,true,false};
                        q0square[bit]=cert::add(q0square[bit],cert::mul(source.norms[*k][bit],source.norms[*k][bit]));
                        residual[bit]=cert::add(residual[bit],cert::mul(radius,radius));
                    }
                }
            }
            if(count!=owner.original_count)return fail(Failure::direct_count);
        }
        const auto candidate=prepare(math,q0square,residual,owner.native_count,false);
        if(!candidate.valid){result.terminal=candidate.diagnostic;return fail(candidate.diagnostic.failure);}
        if(!commit(std::move(math),candidate,q0square,residual))return fail(Failure::summary_join);
    }
    result.failure_owner=color::no_owner;
    auto whole_identity=identity; // separately stamped support, same Original partition
    whole_identity.native_support=identity.native_support==1?2:1;
    const corr::SupportProof whole_support{whole_identity,proof.original_count,proof.original_count,true,true};
    auto whole=corr::combine_math_whole(whole_support,cells);
    if(!whole.valid)return fail(Failure::whole_math);
    field::Bounds bounds;bounds.selected_energy_upper=whole.fields[0].energy.hi;bounds.union_energy_upper=b.union_energy;
    bounds.q0_square_upper=whole_qsquare;bounds.residual_square_upper=whole_residual;bounds.original_qmax={b.qmax,b.qmax};
    bounds.carrier_radius=b.carrier_radius;bounds.carrier_magnitude=b.carrier_magnitude;bounds.prefix_samples=b.prefix;
    const auto errors=field::make(whole_identity,color::no_owner,proof.original_count,bounds,true);
    const auto final=corr::originalize(whole,errors);
    if(!final.valid) {
        result.terminal.failure=Failure::whole_fields;result.terminal.original_count=proof.original_count;
        result.terminal.errors_complete=errors.complete;result.terminal.errors=errors.fields;result.terminal.fields=final.fields;
        result.terminal.q0_square=whole_qsquare;result.terminal.residual_square=whole_residual;
        result.terminal.bit=color::finite(final.fields[0])?1:0;
        return fail(Failure::whole_fields);
    }
    orig::check_stop(stop);
    result.whole=final.fields;
    result.valid=result.chips[0].valid && result.chips[1].valid && result.chips[0].nested && result.chips[1].nested &&
        result.chips[0].count==proof.original_count && result.chips[1].count==proof.original_count;
    result.failure=result.valid?Failure::none:Failure::whole_summary;
    if(result.valid)result.failure_owner=color::no_owner;
    result.counters=work;return result;
}
} // namespace datapump::modem::detail::scheduled_owned_moment_runtime

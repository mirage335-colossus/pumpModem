#pragma once
// OWNED reference0 control recipe for the already chosen Default chord.
// No live mapping/journal/Pieces/code/PCM borrow or future Event is retained.
#include "scheduled_default_linear_runtime.hpp"
#include "scheduled_default_point_family.hpp"
#include "scheduled_default_compact_chords.hpp"
#include <type_traits>
#include <utility>

namespace datapump::modem::detail::scheduled_default_point_reference_recipe {
namespace runtime=scheduled_default_linear_runtime;
namespace family=scheduled_default_point_family;
namespace compact=scheduled_default_compact_chords;
namespace fft=scheduled_linear_grid_fft;
// The caller binds this value to its immutable PatternCode/private key/config
// and template model for the bank's lifetime. Callback identity is a contract,
// not a proof inferred from nonzero classification or a borrowed code address.
struct SourceIdentity {
    std::uint64_t owner=0,revision=0,epoch_sample=0,phase=0,index=0,symbol=0,chip=0;
    unsigned subdivisions=0,model=1;
    double amplitude=0;
    bool operator==(const SourceIdentity&)const=default;
};
// This is a value token, never dereferenced by a payload tile. A tile using it
// MUST be destroyed before its one FrozenReference lifetime ends (or moves).
// Thus allocator address reuse cannot alias a live cache. SourceIdentity alone
// is insufficient: the same private source may have a different Event/grid.
struct LookupIdentity {
    std::uintptr_t instance=0;
    SourceIdentity source;
    bool operator==(const LookupIdentity&)const=default;
};
class FrozenReference {
    runtime::Context context_;
    family::Geometry geometry_;
    SourceIdentity source_;
    static void valid(const runtime::Context& context,const family::Geometry& g,const SourceIdentity& source) {
        fft::require(context.lane_count()==1&&context.sections()==1&&!context.differential_width(),"frozen reference requires one coherent reference0 lane");
        const auto& event=context.event(0);
        fft::require(family::valid_geometry(g)&&source.owner&&source.owner==event.identity.source&&source.revision==event.identity.source_revision&&
            source.epoch_sample==event.epoch_sample&&source.phase==event.phase&&source.index==event.index&&source.symbol==event.symbol_samples&&
            source.chip==event.chip_samples&&(source.subdivisions==2||source.subdivisions==4||source.subdivisions==8)&&source.model==1&&
            std::isfinite(source.amplitude)&&source.amplitude>0&&source.amplitude<=0x1p128,"frozen reference identity/grid mismatch");
    }
    static runtime::Context move_precharged(runtime::Context&& context,const family::Geometry& g,const SourceIdentity& source,
                                            std::size_t budget,std::stop_token stop) {
        fft::stop(stop);valid(context,g,source);
        fft::require(reservation(context)<=budget,"frozen reference capacity was not precharged");
        return std::move(context);
    }
public:
    static_assert(std::is_nothrow_move_constructible_v<runtime::Context>,"frozen context must move owned vectors without a deep copy");
    static std::size_t reservation(const runtime::Context& context) {
        return fft::add(sizeof(FrozenReference)+1024,context.working_bytes()-sizeof(runtime::Context));
    }
    FrozenReference(runtime::Context&& context,family::Geometry geometry,SourceIdentity source,std::size_t budget,std::stop_token stop={})
        :context_(move_precharged(std::move(context),geometry,source,budget,stop)),geometry_(geometry),source_(source) {
        fft::require(working_bytes()<=budget,"frozen reference retained capacity changed");fft::stop(stop);
    }
    FrozenReference(const FrozenReference&)=delete;FrozenReference& operator=(const FrozenReference&)=delete;
    FrozenReference(FrozenReference&&)=default;FrozenReference& operator=(FrozenReference&&)=delete;
    std::size_t working_bytes()const{return reservation(context_);}
    const family::Geometry& geometry()const noexcept{return geometry_;}
    const SourceIdentity& identity()const noexcept{return source_;}
    LookupIdentity lookup_identity()const noexcept{return {reinterpret_cast<std::uintptr_t>(this),source_};}
    const runtime::Context& context()const noexcept{return context_;}
    // Reacquire exact scoped PatternCode/source-node pins OUTSIDE this call.
    // Their callbacks must use frozen phase/index/config; no receiver state,
    // private source address, processing control or frame may be rewound here.
    template<class Coefficients,class Shape>
    scheduled_default_linear_events::Cell cell(std::size_t k,const SourceIdentity& bound,
            Coefficients&& coefficients,Shape&& shape,std::stop_token stop={})const {
        fft::stop(stop);fft::require(bound==source_&&k<geometry_.template_cells,"frozen reference lookup identity/index mismatch");
        const auto physical=[&](std::size_t node){const auto p=geometry_.physical_base+static_cast<long double>(node)*geometry_.node_spacing;
            fft::require(std::isfinite(p)&&p>=-0x1p40L&&p<=0x1p41L,"frozen reference physical endpoint invalid");return p;};
        auto value=context_.cell(0,physical(k),physical(k+1),source_.subdivisions,source_.amplitude,
            std::forward<Coefficients>(coefficients),std::forward<Shape>(shape),stop);
        // Same normalization executed by bulk::prepare. Quiet is a frozen
        // ZERO choice; actual current observations remain caller-owned.
        if(value.chord.supported&&!value.chord.observed){value.chord.left={};value.chord.right={};value.chord.observed=true;}
        fft::stop(stop);return value;
    }
    template<class Coefficients,class Shape>
    compact::Chord chord(std::size_t k,const SourceIdentity& bound,Coefficients&& coefficients,Shape&& shape,std::stop_token stop={})const {
        const auto value=cell(k,bound,std::forward<Coefficients>(coefficients),std::forward<Shape>(shape),stop);
        return {value.chord.left,value.chord.right,value.chord.supported};
    }
};
} // namespace datapump::modem::detail::scheduled_default_point_reference_recipe

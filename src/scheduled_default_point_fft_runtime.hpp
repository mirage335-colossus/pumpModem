#pragma once
#include "scheduled_default_point_observation.hpp"
#include "scheduled_default_point_piece_cache.hpp"
#include "scheduled_default_linear_runtime.hpp"
#include "scheduled_default_point_family.hpp"
#include "scheduled_default_point_bulk.hpp"
#include "scheduled_default_point_guard_summary.hpp"
#include "scheduled_default_point_reference_recipe.hpp"
#include <array>
#include <memory>
#include <vector>
namespace datapump::modem::detail::scheduled_default_point_fft_runtime {
namespace family=scheduled_default_point_family;
namespace observation=scheduled_default_point_observation;
namespace runtime=scheduled_default_linear_runtime;
inline constexpr std::size_t cache_slots=128;
struct Key {
    std::uint64_t capture=0,processing=0,raw_capture=0,index=0;
    std::size_t family=0,first=0,end=0;
    unsigned subdivisions=0;
    long double physical_base=0,node_spacing=0;
    bool pending_bulk=false;
    unsigned bulk_scalar_bytes=0;
    bool compact_bulk=false; // representation identity, not a different statistic
    bool operator==(const Key&)const=default;
};
struct Entry {
    Key key;
    std::uint64_t processing_end=0,touched=0;
    std::vector<std::array<CorrelationFit,2>> whole;
    std::vector<std::uint8_t> valid;
    std::unique_ptr<scheduled_default_point_bulk::PendingBulk> pending;
    // Included once in reserved; original reference0 Context/Pieces move without copy.
    std::unique_ptr<scheduled_default_point_reference_recipe::FrozenReference> reference_recipe;
    // Completed numerical guards owned by this exact immutable whole identity.
    // Counts/crops remain stamped per Record; serial scoring supplies current T.
    std::unique_ptr<scheduled_default_point_guard_summary::Store<Key>> guard_summaries;
    std::size_t finalized_prefix=0;
    std::size_t reserved=0;
    std::size_t working_bytes()const{return scheduled_acquisition_tile::add_size(reserved,guard_summaries?guard_summaries->working_bytes():0);}
    std::size_t core_payload_bytes()const {
        namespace tile=scheduled_acquisition_tile;
        auto n=tile::add_size(sizeof(Entry),tile::add_size(tile::multiply_size(whole.capacity(),sizeof(whole[0])),valid.capacity()));
        if(pending)n=tile::add_size(n,pending->working_bytes());
        if(reference_recipe)n=tile::add_size(n,reference_recipe->working_bytes());
        return n; // guard_summaries remains its separate working_bytes addend
    }
    static std::size_t reservation(std::size_t outputs){return family::retained_reservation(outputs)+sizeof(Entry)+128;}
};
struct GuardPrepassWork {
    std::uint64_t calls=0,hits=0,candidates=0,completed=0,workspace_fallbacks=0,
        guard_fallbacks=0,rebuilds=0,rebuild_rows=0;
    std::size_t peak_summary_bytes=0;
};
struct Pool {
    GuardPrepassWork guard_work;
    std::array<std::unique_ptr<Entry>,cache_slots> entries;
    std::uint64_t sequence=0;
    std::size_t working_bytes()const {std::size_t n=0;for(const auto& e:entries)if(e)n+=e->working_bytes();return n;}
    void clear(){for(auto& e:entries)e.reset();}
    std::size_t find(const Key& key)const {for(std::size_t i=0;i<entries.size();++i)if(entries[i]&&entries[i]->key==key)return i;return entries.size();}
    std::size_t victim()const {
        std::size_t choice=0;for(std::size_t i=0;i<entries.size();++i){if(!entries[i])return i;if(entries[i]->touched<entries[choice]->touched)choice=i;}return choice;
    }
    void touch(Entry& e){if(sequence==UINT64_MAX){sequence=0;for(auto& item:entries)if(item)item->touched=0;}e.touched=++sequence;}
};
inline runtime::Decision score(const Entry& entry,std::size_t origin) {
    runtime::Decision out;if((entry.key.pending_bulk && origin>=entry.finalized_prefix)||origin>=entry.whole.size()||origin>=entry.valid.size()||!entry.valid[origin])return out;
    for(unsigned bit=0;bit<2;++bit){const auto value=entry.whole[origin][bit].score();if(!std::isfinite(value)||value<0)return {};out.scores.score[bit]=value;}
    out.scores.bit=out.scores.score[1]>out.scores.score[0]?1U:0U;out.scores.valid=out.ready=true;return out;
}
} // namespace datapump::modem::detail::scheduled_default_point_fft_runtime

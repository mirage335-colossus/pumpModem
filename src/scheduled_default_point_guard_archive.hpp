#pragma once
// Owned native POINT centers only. No Original numerical certificate, PCM,
// plan, source borrow, admission or receiver state is retained here.
#include "scheduled_default_linear_events.hpp"
#include "scheduled_default_linear_cellstats.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <stop_token>
#include <openssl/crypto.h>

namespace datapump::modem::detail::scheduled_default_point_guard_archive {
namespace events=scheduled_default_linear_events;
namespace point=scheduled_default_linear_cellstats;
namespace fft=scheduled_linear_grid_fft;
struct Layout {
    long double physical_base=0,node_spacing=1;
    std::uint64_t capture_first=0,capture_end=0;
    std::size_t template_cells=0,input_first=0,input_cells=0,origin_first=0,outputs=0,step=1;
};
struct EdgeRow {
    std::uint64_t physical=0;
    CorrelationProjection projection{};
    double t=0; // EXACT executed producer double fraction within this node cell
};
struct EdgeCell {
    std::uint64_t first=0,end=0,count=0;
    std::array<EdgeRow,2> head{},tail{};
    unsigned head_count=0,tail_count=0;
};
inline bool same_projection(const CorrelationProjection& a,const CorrelationProjection& b) {
    return a.xc==b.xc&&a.xs==b.xs&&a.cc==b.cc&&a.ss==b.ss&&a.cs==b.cs&&a.energy==b.energy;
}
inline bool valid_projection(const CorrelationProjection& p) {
    return std::isfinite(p.xc)&&std::isfinite(p.xs)&&std::isfinite(p.cc)&&std::isfinite(p.ss)&&
        std::isfinite(p.cs)&&std::isfinite(p.energy)&&p.cc>=0&&p.ss>=0&&p.cc<=4&&p.ss<=4&&std::abs(p.cs)<=4&&
        std::abs(p.xc)<=0x1p128&&std::abs(p.xs)<=0x1p128&&p.energy>=0&&p.energy<=0x1p256;
}
// Width0 validates the general disjoint endpoint representation. Archive
// installation additionally requires EXACT configured-width endpoint coverage.
inline bool valid_edges(const EdgeCell& e,const Layout& l,std::size_t cell,unsigned width=0) {
    if(width>2||cell<l.input_first||cell-l.input_first>=l.input_cells||e.first>e.end||
       e.first<l.capture_first||e.end>l.capture_end||e.count!=e.end-e.first||
       e.head_count>2||e.tail_count>2||e.head_count>e.count||e.tail_count>e.count-e.head_count||
       !std::isfinite(l.node_spacing)||l.node_spacing<=0||!std::isfinite(l.physical_base))return false;
    if(width) {
        const auto h=static_cast<unsigned>(std::min<std::uint64_t>(width,e.count));
        const auto t=static_cast<unsigned>(std::min<std::uint64_t>(width,e.count-h));
        if(e.head_count!=h||e.tail_count!=t)return false;
    }
    if(e.count&&(!e.head_count||(!e.tail_count&&e.head_count!=e.count)))return false;
    const auto node_of=[&](std::uint64_t n){return (static_cast<long double>(n)-l.physical_base)/l.node_spacing;};
    const auto below=[&](std::uint64_t n,long double boundary){const auto u=node_of(n);return std::isfinite(u)&&u<boundary;};
    if(e.first>l.capture_first&&!below(e.first-1,static_cast<long double>(cell)))return false;
    if(e.end<l.capture_end) {
        const auto u=node_of(e.end);if(!std::isfinite(u)||u<static_cast<long double>(cell)+1)return false;
    }
    if(e.count) {
        const auto a=node_of(e.first),b=node_of(e.end-1);
        if(!std::isfinite(a)||!std::isfinite(b)||a<cell||b>=static_cast<long double>(cell)+1)return false;
    }
    const auto check=[&](const EdgeRow& row,std::uint64_t physical) {
        const auto u=node_of(physical)-static_cast<long double>(cell);
        return row.physical==physical&&valid_projection(row.projection)&&std::isfinite(u)&&u>=0&&u<1&&
            std::isfinite(row.t)&&row.t>=0&&row.t<1&&row.t==static_cast<double>(u);
    };
    for(unsigned i=0;i<e.head_count;++i)if(!check(e.head[i],e.first+i))return false;
    for(unsigned i=0;i<e.tail_count;++i)if(!check(e.tail[i],e.end-e.tail_count+i))return false;
    return true;
}
inline const EdgeRow* find_edge(const EdgeCell& e,std::uint64_t physical) {
    if(physical<e.first||physical>=e.end)return nullptr;
    if(physical-e.first<e.head_count)return &e.head[static_cast<std::size_t>(physical-e.first)];
    if(physical>=e.end-e.tail_count)return &e.tail[static_cast<std::size_t>(physical-(e.end-e.tail_count))];
    return nullptr;
}
class Archive {
    friend class EdgeCapture;
public:
    inline static constexpr std::size_t maximum_bytes=32U*1024U*1024U;
    struct View {const fft::InputRow* row=nullptr;long double energy=0;std::uint64_t count=0;};
private:
    struct CellSlot {events::Cell value{};bool present=false;};
    struct MomentSlot {fft::InputRow row{};long double energy=0;std::uint64_t count=0;bool present=false;};
    struct EdgeSlot {EdgeCell value{};bool present=false;};
    enum class State {building,sealed,failed};
    Layout layout_;
    std::size_t bytes_=0,cells_present_=0;
    std::size_t active_captures_=0;
    std::uint64_t stored_edge_rows_=0;
    unsigned edge_width_=0;
    State state_=State::building;
    // Fixed arrays have known capacities before allocation and never grow.
    std::unique_ptr<CellSlot[]> cells_;
    std::unique_ptr<MomentSlot[]> moments_;
    std::unique_ptr<EdgeSlot[]> edges_;
    static void require(bool ok,const char* message){if(!ok)throw Error(message);}
    static void check_stop(std::stop_token stop){require(!stop.stop_requested(),"point guard archive cancelled");}
    static bool valid_layout(const Layout& l) {
        constexpr std::size_t limit=0x40000000ULL;
        if(!std::isfinite(l.physical_base)||!std::isfinite(l.node_spacing)||l.node_spacing<=0||l.node_spacing>0x1p32L||
           l.capture_first>l.capture_end||l.capture_end>0x10000000000ULL||!l.template_cells||l.template_cells>limit||
           !l.outputs||!l.step||l.step>64||l.origin_first>limit/l.step||l.outputs-1>limit/l.step-l.origin_first)return false;
        const auto first=l.origin_first*l.step,halo=(l.outputs-1)*l.step;
        return l.input_first==first&&halo<=SIZE_MAX-l.template_cells&&l.input_cells==l.template_cells+halo;
    }
    static bool finite(const fft::Wide& z){return std::isfinite(z.real())&&std::isfinite(z.imag());}
    static bool valid_cell(const events::Cell& c) {
        for(unsigned bit=0;bit<2;++bit) {
            if(!finite(c.chord.left[bit])||!finite(c.chord.right[bit])||
               std::isnan(c.chord.residual[bit])||c.chord.residual[bit]<0)return false;
        }
        return std::isfinite(c.outer_gain_left)&&c.outer_gain_left>=0&&
            std::isfinite(c.outer_gain_right)&&c.outer_gain_right>=0;
    }
    static bool same_cell(const events::Cell& a,const events::Cell& b) {
        return a.chord.left==b.chord.left&&a.chord.right==b.chord.right&&a.chord.residual==b.chord.residual&&
            a.chord.observed==b.chord.observed&&a.chord.supported==b.chord.supported&&a.source_node==b.source_node&&
            a.outer_gain_left==b.outer_gain_left&&a.outer_gain_right==b.outer_gain_right&&a.outer_zero==b.outer_zero;
    }
    static bool valid_moment(const fft::InputRow& row,long double energy,std::uint64_t count,const Layout& l) {
        if(!std::isfinite(energy)||energy<0||count>point::maximum_exact_count||count>l.capture_end-l.capture_first)return false;
        for(const auto& value:row)if(!finite(value))return false;
        for(unsigned k=5;k<8;++k)if(row[k].imag()!=0)return false;
        if(!count) {
            if(energy!=0)return false;
            for(const auto& value:row)if(value!=fft::Wide{})return false;
        }
        return true;
    }
    void cleanse()noexcept {
        if(cells_)OPENSSL_cleanse(cells_.get(),layout_.template_cells*sizeof(CellSlot));
        if(moments_)OPENSSL_cleanse(moments_.get(),layout_.input_cells*sizeof(MomentSlot));
        if(edges_)OPENSSL_cleanse(edges_.get(),layout_.input_cells*sizeof(EdgeSlot));
    }
    void poison()noexcept{state_=State::failed;cleanse();}
public:
    static std::size_t reservation(const Layout& l,unsigned edge_width=0) {
        require(valid_layout(l),"point guard archive layout invalid");
        require(edge_width<=2,"point guard archive edge width invalid");
        auto bytes=sizeof(Archive)+4096;
        require(l.template_cells<=(maximum_bytes-bytes)/sizeof(CellSlot),"point guard archive cell cap exceeded");
        bytes+=l.template_cells*sizeof(CellSlot);
        require(l.input_cells<=(maximum_bytes-bytes)/sizeof(MomentSlot),"point guard archive moment cap exceeded");
        bytes+=l.input_cells*sizeof(MomentSlot);
        if(edge_width) {
            require(l.input_cells<=(maximum_bytes-bytes)/sizeof(EdgeSlot),"point guard archive edge cap exceeded");
            bytes+=l.input_cells*sizeof(EdgeSlot);
        }
        return bytes;
    }
    Archive(Layout l,std::size_t budget,std::stop_token stop={},unsigned edge_width=0):layout_(l),edge_width_(edge_width) {
        check_stop(stop);bytes_=reservation(l,edge_width);
        require(bytes_<=budget,"point guard archive workspace preflight");
        cells_=std::make_unique<CellSlot[]>(layout_.template_cells);check_stop(stop);
        moments_=std::make_unique<MomentSlot[]>(layout_.input_cells);check_stop(stop);
        if(edge_width_){edges_=std::make_unique<EdgeSlot[]>(layout_.input_cells);check_stop(stop);}
    }
    Archive(const Archive&)=delete;Archive& operator=(const Archive&)=delete;
    Archive(Archive&&)=delete;Archive& operator=(Archive&&)=delete;
    ~Archive(){cleanse();}
    std::size_t working_bytes()const noexcept{return bytes_;}
    const Layout& layout()const noexcept{return layout_;}
    unsigned edge_width()const noexcept{return edge_width_;}
    std::uint64_t stored_edge_rows()const noexcept{return sealed()?stored_edge_rows_:0;}
    bool sealed()const noexcept{return state_==State::sealed;}
    void install_cells(std::size_t offset,std::span<const events::Cell> cells,std::stop_token stop={}) {
        try {
            require(state_==State::building,"point guard archive is immutable or failed");check_stop(stop);
            require(offset<=layout_.template_cells&&cells.size()<=layout_.template_cells-offset,"point guard archive cell extent invalid");
            for(std::size_t i=0;i<cells.size();++i) {
                if((i&255U)==0)check_stop(stop);
                const auto& value=cells[i];require(valid_cell(value),"point guard archive cell invalid");
                auto& slot=cells_[offset+i];
                if(slot.present)require(same_cell(slot.value,value),"point guard archive cell duplicate differs");
                else {slot.value=value;slot.present=true;++cells_present_;}
            }
            check_stop(stop);
        }catch(...){poison();throw;}
    }
    void install_moments(std::size_t first,std::span<const fft::InputRow> rows,
            std::span<const long double> energies,std::span<const std::uint64_t> counts,std::stop_token stop={}) {
        try {
            require(state_==State::building,"point guard archive is immutable or failed");check_stop(stop);
            require(rows.size()==energies.size()&&rows.size()==counts.size()&&first>=layout_.input_first,
                "point guard archive moment shape invalid");
            const auto offset=first-layout_.input_first;
            require(offset<=layout_.input_cells&&rows.size()<=layout_.input_cells-offset,"point guard archive moment extent invalid");
            for(std::size_t i=0;i<rows.size();++i) {
                if((i&255U)==0)check_stop(stop);
                require(valid_moment(rows[i],energies[i],counts[i],layout_),"point guard archive moment invalid");
                auto& slot=moments_[offset+i];
                require(!edges_||!edges_[offset+i].present||edges_[offset+i].value.count==counts[i],
                    "point guard archive moments disagree with edges");
                // Numeric equality deliberately avoids long-double/struct
                // padding. A different executed center requires fallback.
                if(slot.present)require(slot.row==rows[i]&&slot.energy==energies[i]&&slot.count==counts[i],
                    "point guard archive moment duplicate differs");
                else {slot.row=rows[i];slot.energy=energies[i];slot.count=counts[i];slot.present=true;}
            }
            check_stop(stop);
        }catch(...){poison();throw;}
    }
    void seal(std::stop_token stop={}) {
        try {
            require(state_==State::building,"point guard archive is immutable or failed");check_stop(stop);
            require(!active_captures_,"point guard archive has unfinished edge capture");
            require(cells_present_==layout_.template_cells,"point guard archive cell coverage incomplete");
            if(edges_)for(std::size_t i=0;i<layout_.input_cells;++i) {
                if((i&255U)==0)check_stop(stop);
                require(!edges_[i].present||(moments_[i].present&&moments_[i].count==edges_[i].value.count),
                    "point guard archive edges lack matching moments");
            }
            // Missing moments are intentional holes, never invented zeros.
            state_=State::sealed;
        }catch(...){poison();throw;}
    }
    void install_edges(std::size_t first,std::span<const EdgeCell> edges,std::stop_token stop={}) {
        try {
            require(state_==State::building&&edge_width_,"point guard archive edges disabled, immutable or failed");check_stop(stop);
            require(first>=layout_.input_first,"point guard archive edge extent invalid");
            const auto offset=first-layout_.input_first;
            require(offset<=layout_.input_cells&&edges.size()<=layout_.input_cells-offset,"point guard archive edge extent invalid");
            for(std::size_t i=0;i<edges.size();++i) {
                if((i&255U)==0)check_stop(stop);
                const auto& e=edges[i];const auto at=offset+i;
                require((!moments_[at].present||moments_[at].count==e.count)&&valid_edges(e,layout_,first+i,edge_width_),
                    "point guard archive edge support invalid");
                auto& slot=edges_[at];
                if(slot.present) {
                    const auto& a=slot.value;
                    require(a.first==e.first&&a.end==e.end&&a.count==e.count&&a.head_count==e.head_count&&a.tail_count==e.tail_count,
                        "point guard archive edge duplicate differs");
                    const auto same=[](const EdgeRow& a,const EdgeRow& b){return a.physical==b.physical&&a.t==b.t&&same_projection(a.projection,b.projection);};
                    for(unsigned k=0;k<e.head_count;++k)require(same(a.head[k],e.head[k]),"point guard archive head duplicate differs");
                    for(unsigned k=0;k<e.tail_count;++k)require(same(a.tail[k],e.tail[k]),"point guard archive tail duplicate differs");
                }else {slot.value=e;slot.present=true;stored_edge_rows_+=e.head_count+e.tail_count;}
            }
            check_stop(stop);
        }catch(...){poison();throw;}
    }
    const EdgeCell* edge(std::size_t absolute_cell)const noexcept {
        if(!sealed()||!edges_||absolute_cell<layout_.input_first)return nullptr;
        const auto offset=absolute_cell-layout_.input_first;
        return offset<layout_.input_cells&&edges_[offset].present?&edges_[offset].value:nullptr;
    }
    const events::Cell* cell(std::size_t k)const noexcept {
        return sealed()&&k<layout_.template_cells&&cells_[k].present?&cells_[k].value:nullptr;
    }
    View moment(std::size_t absolute_cell)const noexcept {
        if(!sealed()||absolute_cell<layout_.input_first)return {};
        const auto offset=absolute_cell-layout_.input_first;
        if(offset>=layout_.input_cells||!moments_[offset].present)return {};
        const auto& slot=moments_[offset];return {&slot.row,slot.energy,slot.count};
    }
    // This establishes GRID/COHORT compatibility only. The owning cache node
    // must bind immutable private template/source, raw stream, carrier/restart,
    // processing and capture identities. Equal spacing cannot prove that bind.
    // A covering capture may include rows beyond a guard's current capture.
    // Consumer must prove nested physical row intervals and cached N==the
    // exact requested cell row count before contracting; otherwise row fallback
    // uses the SAME archived chord. A nonnull View alone proves no support.
    bool accepts(const events::Geometry& g)const noexcept {
        if(!sealed()||g.physical_base!=layout_.physical_base||g.node_spacing!=layout_.node_spacing||
           g.capture_first<layout_.capture_first||g.capture_end>layout_.capture_end||g.capture_first>g.capture_end||
           g.template_cells!=layout_.template_cells||g.step!=layout_.step||!g.outputs||g.origin_first<layout_.origin_first)return false;
        const auto delta=g.origin_first-layout_.origin_first;
        return delta<layout_.outputs&&g.outputs<=layout_.outputs-delta;
    }
};
// One packet per contiguous physical fragment; no extent-sized scratch. Every
// add is the SAME row/t given to Moments. Finish before installing Moments is
// allowed; seal crossvalidates counts. Never carry this across halo rewinds.
class EdgeCapture {
    Archive& archive_;
    EdgeCell packet_{};
    std::size_t cell_=0;
    std::uint64_t rows_=0;
    bool have_=false,finished_=false,registered_=false;
    std::stop_token stop_;
    void flush(std::stop_token stop) {
        if(have_)archive_.install_edges(cell_,std::span<const EdgeCell>(&packet_,1),stop);
        have_=false;
    }
    void release()noexcept{if(registered_){--archive_.active_captures_;registered_=false;}}
public:
    explicit EdgeCapture(Archive& archive,std::stop_token stop={}):archive_(archive),stop_(stop) {
        try {
            Archive::require(archive_.state_==Archive::State::building&&archive_.edge_width_,"point guard edge capture disabled");Archive::check_stop(stop_);
            Archive::require(archive_.active_captures_<SIZE_MAX,"point guard edge capture count overflow");
            ++archive_.active_captures_;registered_=true;
        }
        catch(...){archive_.poison();throw;}
    }
    EdgeCapture(const EdgeCapture&)=delete;EdgeCapture& operator=(const EdgeCapture&)=delete;
    ~EdgeCapture(){if(!finished_)archive_.poison();release();OPENSSL_cleanse(&packet_,sizeof(packet_));}
    void add(std::size_t cell,std::uint64_t physical,double t,const CorrelationProjection& p) {
        try {
            Archive::require(!finished_&&archive_.state_==Archive::State::building,"point guard edge capture is finished or failed");
            if((rows_++&255U)==0)Archive::check_stop(stop_);
            if(have_&&cell!=cell_) {
                Archive::require(cell>cell_&&physical==packet_.end,"point guard edge capture rewound or skipped raw rows");flush(stop_);
            }
            if(!have_) {packet_={};packet_.first=packet_.end=physical;cell_=cell;have_=true;}
            Archive::require(physical==packet_.end&&physical<UINT64_MAX,"point guard edge capture raw gap");
            const EdgeRow row{physical,p,t};const auto width=archive_.edge_width_;
            if(packet_.head_count<width)packet_.head[packet_.head_count++]=row;
            else if(packet_.tail_count<width)packet_.tail[packet_.tail_count++]=row;
            else {if(width==2)packet_.tail[0]=packet_.tail[1];packet_.tail[width-1]=row;}
            ++packet_.end;++packet_.count;
        }catch(...){archive_.poison();release();throw;}
    }
    void finish(std::stop_token stop={}) {
        try {Archive::require(!finished_,"point guard edge capture already finished");Archive::check_stop(stop_);Archive::check_stop(stop);flush(stop);finished_=true;release();}
        catch(...){archive_.poison();release();throw;}
    }
};
} // namespace datapump::modem::detail::scheduled_default_point_guard_archive

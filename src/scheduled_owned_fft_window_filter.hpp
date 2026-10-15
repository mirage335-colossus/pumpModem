#pragma once
// Optional cache-publication inventory, never an observation/ownership proof.
#include "scheduled_polynomial_chip_runtime.hpp"
#include <optional>

namespace datapump::modem::detail::scheduled_owned_fft_window_filter {
namespace poly=scheduled_polynomial_chip;
namespace color=scheduled_cold_colored;
namespace cert=scheduled_acquisition_certificate;
inline constexpr std::size_t maximum_input=4096,maximum_selected=256;
inline void check_stop(std::stop_token stop){if(stop.stop_requested())throw Error("owned FFT window selection cancelled");}
inline std::optional<std::uint64_t> translate(std::uint64_t physical,std::int64_t offset) {
    if(offset>=0) {
        const auto delta=static_cast<std::uint64_t>(offset);
        if(physical>std::numeric_limits<std::uint64_t>::max()-delta)return {};
        return physical+delta;
    }
    const auto delta=static_cast<std::uint64_t>(-(offset+1))+1;
    if(physical<delta)return {};
    return physical-delta;
}
struct Crop {
    std::uint64_t native_first=0,native_end=0,available_first=0,available_end=0;
    bool operator==(const Crop&)const=default;
};

// One bounded index per pinned CURRENT event/model. Sealed SourceRows and the
// model's constructor guarantee source-cell physical chronology. This extra
// full-cell index avoids repeatedly scanning partial cells or all K owners.
// No q/PCM/carrier is copied. Caller retains the source/model owner pin.
class View {
    const color::SourceRows* source_=nullptr;
    const poly::SourceModels* models_=nullptr;
    color::SourceRows::Key key_;
    const poly::SourceModels::Cell* cells_=nullptr;
    std::size_t cells_count_=0;
    std::int64_t translation_=0;
    Crop crop_;
    bool next17_=true;
    std::vector<std::uint64_t> starts_;
public:
    static std::size_t reservation(std::size_t cells) {
        if(cells>color::SourceRows::maximum_rows/34+2)throw Error("owned FFT window inventory cap exceeded");
        return poly::add_size(sizeof(View)+1024,poly::mul_size(cells,sizeof(std::uint64_t)));
    }
    std::size_t working_bytes()const{return poly::add_size(sizeof(*this)+1024,poly::mul_size(starts_.capacity(),sizeof(starts_[0])));}
    View(const color::SourceRows& source,const poly::SourceModels& models,long double translation,
         Crop crop,bool next17=true,std::stop_token stop={})
        :source_(&source),models_(&models),key_(source.key),cells_(models.cells.data()),cells_count_(models.cells.size()),crop_(crop),next17_(next17) {
        check_stop(stop);
        if(!models.matches_native(source) || !std::isfinite(translation) || std::trunc(translation)!=translation ||
           translation< -0x1p63L || translation>=0x1p63L || crop.native_first>=crop.native_end ||
           crop.available_first>=crop.available_end)throw Error("owned FFT window source/translation/crop unsupported");
        translation_=static_cast<std::int64_t>(translation);
        const auto reserved=reservation(cells_count_);starts_.reserve(cells_count_);
        std::uint64_t previous=0,previous_full=0;bool have_previous=false,have_full=false;
        for(std::size_t i=0;i<models.cells.size();++i) {
            if((i&255U)==0)check_stop(stop);const auto& cell=models.cells[i];
            if(have_previous && cell.physical<=previous)throw Error("owned FFT source cells reverse");
            previous=cell.physical;have_previous=true;
            if(!cell.full || !cell.native_template_only)continue;
            if(have_full && (previous_full>std::numeric_limits<std::uint64_t>::max()-34 || cell.physical<previous_full+34))
                throw Error("owned FFT full source cells overlap");
            previous_full=cell.physical;have_full=true;
            const auto physical=translate(cell.physical,translation_);
            if(!physical || *physical>std::numeric_limits<std::uint64_t>::max()-34)continue;
            if(*physical<crop.native_first || *physical+34>crop.native_end ||
               *physical<crop.available_first || *physical+34>crop.available_end)continue;
            starts_.push_back(*physical);
        }
        check_stop(stop);if(!matches() || working_bytes()>reserved)throw Error("owned FFT window inventory changed or exceeded reservation");
    }
    View(const View&)=delete;View& operator=(const View&)=delete;
    bool matches()const {
        return source_ && models_ && source_->key==key_ && models_->matches_native(*source_) &&
            models_->cells.data()==cells_ && models_->cells.size()==cells_count_;
    }
    std::span<const std::uint64_t> starts()const{return starts_;}
    const Crop& crop()const{return crop_;}
    bool next17()const{return next17_;}
    std::int64_t translation()const{return translation_;}
};

struct Selection {std::size_t count=0,search_probes=0,visited=0;bool filtered=false;};
inline std::size_t lower(std::span<const std::uint64_t> starts,std::uint64_t target,Selection& work) {
    std::size_t a=0,b=starts.size();while(a<b){++work.search_probes;const auto m=a+(b-a)/2;
        if(starts[m]<target)a=m+1;else b=m;}return a;
}

// nullptr is exactly the historical dense requested-residue policy. A View
// intersects that requested residue with CURRENT native cells and optionally
// their +17 physical windows. Other residues are handled on later requests;
// this is not a global17-lattice claim. +17 has no ownership implication.
// Request is retained even when absent from the auxiliary model inventory.
// O(log K + full windows in this tile), two sorted streams, no per-fetch K scan.
inline Selection select(const View* view,std::uint64_t request,std::uint64_t first,std::size_t count,
                        std::span<std::size_t> output,std::stop_token stop={}) {
    check_stop(stop);Selection result;
    if(count<34 || count>maximum_input || first>std::numeric_limits<std::uint64_t>::max()-count ||
       request<first || request>first+count-34 || output.empty())throw Error("owned FFT window tile/request invalid");
    const auto publish=[&](std::uint64_t physical) {
        const auto at=static_cast<std::size_t>(physical-first);
        if(result.count && output[result.count-1]==at)return;
        if(result.count==output.size())throw Error("owned FFT selected-window cap exceeded");
        output[result.count++]=at;
    };
    if(!view) {
        for(auto at=static_cast<std::size_t>((request-first)%17);at+34<=count;at+=17) {
            check_stop(stop);publish(first+at);++result.visited;
        }
        return result;
    }
    if(!view->matches())throw Error("owned FFT window source/model identity changed");
    const auto& crop=view->crop();
    if(request<crop.available_first || request>crop.available_end || crop.available_end-request<34)
        throw Error("owned FFT requested physical capture unavailable");
    result.filtered=true;
    const auto lo=std::max(first,crop.available_first),hi=std::min(first+count-34,crop.available_end-34);
    const auto starts=view->starts();auto a=lower(starts,lo,result);
    auto b=view->next17()?lower(starts,lo>=17?lo-17:0,result):starts.size();
    bool request_pending=true;
    const auto advance=[&](std::size_t& i,std::uint64_t shift)->std::optional<std::uint64_t> {
        while(i<starts.size()) {
            check_stop(stop);const auto p=starts[i];
            if(p>std::numeric_limits<std::uint64_t>::max()-shift || p+shift>hi){i=starts.size();break;}
            ++result.visited;
            if(p+shift>=lo && (p+shift)%17==request%17)return p+shift;
            ++i;
        }
        return {};
    };
    auto pa=advance(a,0),pb=advance(b,17);
    while(pa || pb || request_pending) {
        check_stop(stop);auto p=request_pending?request:std::numeric_limits<std::uint64_t>::max();
        if(pa)p=std::min(p,*pa);if(pb)p=std::min(p,*pb);publish(p);
        if(request_pending && p==request)request_pending=false;
        if(pa && p==*pa){++a;pa=advance(a,0);}if(pb && p==*pb){++b;pb=advance(b,17);}
    }
    check_stop(stop);if(!view->matches())throw Error("owned FFT window source/model changed during selection");
    return result;
}
} // namespace datapump::modem::detail::scheduled_owned_fft_window_filter

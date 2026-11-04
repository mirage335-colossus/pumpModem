#pragma once
#include "control_interactions.hpp"
#include <limits>
#include <string>
#include <vector>

namespace datapump::gui::ui {
enum class RecordKey { up, down, enter, space };
struct RecordInteraction {
    std::string id;
    std::size_t index=std::numeric_limits<std::size_t>::max();
    bool activate=false;
    explicit operator bool() const {return index!=std::numeric_limits<std::size_t>::max();}
    template<class Select,class Activate> bool dispatch(Select&& select,Activate&& activation) const {
        if(!*this)return false;
        select(id);
        if(activate)activation(id);
        return true;
    }
};
// Native lists supply the focused record ID, keys and logical pointer position.
// Selection, eligibility, activation and click history are shared policy; the
// returned index only tells a toolkit which native row to focus and reveal.
class RecordInteractions {
public:
    using Clock=PointerClicks::Clock;
    explicit RecordInteractions(bool activate_on_select=false):activate_on_select_(activate_on_select) {}
    void configure(bool activate_on_select) {
        if(activate_on_select_!=activate_on_select)clicks_.reset();
        activate_on_select_=activate_on_select;
    }
    void apply(const FieldState& state) {
        enabled_=state.enabled&&state.visible;
        records_.clear();records_.reserve(state.records.size());
        for(const auto& record:state.records)records_.push_back({record.id,record.enabled,record.activatable});
        if(!enabled_)clicks_.reset();
    }
    RecordInteraction pointer(std::string_view id,float x,float y,Clock::time_point now=Clock::now()) {
        const auto index=find(id);
        if(!eligible(index))return {};
        return interaction(index,clicks_.press(x,y,id,now));
    }
    RecordInteraction key(std::string_view current,RecordKey key) {
        clicks_.reset();
        if(!enabled_)return {};
        const auto found=find(current);
        if(key==RecordKey::enter||key==RecordKey::space)
            return eligible(found)?interaction(found,key==RecordKey::enter):RecordInteraction{};
        const int direction=key==RecordKey::up?-1:1;
        auto index=found==records_.size()?0:static_cast<std::ptrdiff_t>(found)+direction;
        for(;index>=0&&index<static_cast<std::ptrdiff_t>(records_.size());index+=direction)
            if(eligible(static_cast<std::size_t>(index)))return interaction(static_cast<std::size_t>(index),false);
        return {};
    }
private:
    struct RecordState {std::string id;bool enabled,activatable;};
    bool activate_on_select_,enabled_=true;
    std::vector<RecordState> records_;
    PointerClicks clicks_;
    std::size_t find(std::string_view id) const {
        const auto found=std::find_if(records_.begin(),records_.end(),[&](const auto& record){return record.id==id;});
        return static_cast<std::size_t>(found-records_.begin());
    }
    bool eligible(std::size_t index) const {return enabled_&&index<records_.size()&&records_[index].enabled;}
    RecordInteraction interaction(std::size_t index,bool explicit_activation) const {
        const auto& record=records_[index];
        return {record.id,index,record.activatable&&(activate_on_select_||explicit_activation)};
    }
};
}

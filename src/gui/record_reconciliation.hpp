#pragma once
#include "ui_contract.hpp"
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace datapump::gui::ui {
struct RecordChanges {
    std::vector<std::string> added,removed,updated;
    bool order=false,selection=false,availability=false;
    bool content() const {return !added.empty()||!removed.empty()||!updated.empty();}
    explicit operator bool() const {return content()||order||selection||availability;}
};
// Retained record identity, declaration order and change detection belong to
// the shared presentation. Native lists own handles and measured glyph widths;
// they consume this plan without maintaining another copy of the record model.
class RecordReconciliation {
public:
    RecordChanges apply(const FieldState& state) {
        std::set<std::string,std::less<>> retained;
        std::vector<std::string> next_order;next_order.reserve(state.records.size());
        for(const auto& record:state.records) {
            if(!retained.insert(record.id).second)throw std::invalid_argument("record IDs must be unique within a list");
            next_order.push_back(record.id);
        }
        RecordChanges changes;
        changes.order=order_!=next_order;changes.selection=selected_!=state.selected;
        changes.availability=enabled_!=state.enabled||visible_!=state.visible;
        for(const auto& id:order_)if(!retained.contains(id)) {changes.removed.push_back(id);records_.erase(id);}
        for(const auto& record:state.records) {
            auto [item,inserted]=records_.try_emplace(record.id,record);
            if(inserted)changes.added.push_back(record.id);
            else if(item->second!=record) {item->second=record;changes.updated.push_back(record.id);}
        }
        order_=std::move(next_order);selected_=state.selected;enabled_=state.enabled;visible_=state.visible;
        return changes;
    }
    const std::vector<std::string>& order() const {return order_;}
    const Record& record(std::string_view id) const {
        const auto found=records_.find(id);
        if(found==records_.end())throw std::out_of_range("record identity is not retained");
        return found->second;
    }
    bool empty() const {return order_.empty();}
    std::size_t size() const {return order_.size();}
    const std::string& selected_id() const {return selected_;}
    void select(std::string id) {selected_=std::move(id);}
    bool selected(std::string_view id) const {return selected_==id;}
    bool enabled(std::string_view id) const {return enabled_&&visible_&&record(id).enabled;}
private:
    std::map<std::string,Record,std::less<>> records_;
    std::vector<std::string> order_;
    std::string selected_;
    bool enabled_=true,visible_=true;
};
}

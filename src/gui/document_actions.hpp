#pragma once
#include "ui_document.hpp"
#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>

namespace datapump::gui::ui {
struct DocumentActionIdentity {
    Command command=Command::none;
    std::size_t occurrence=0;
    unsigned instance=0;
    bool operator==(const DocumentActionIdentity&) const = default;
};
struct DocumentAction {
    const DocumentNode* node=nullptr;
    DocumentActionIdentity identity;
    bool enabled=false;
};
// Identity and action eligibility are properties of the shared description.
// Native adapters retain handles for these identities, translate focus/input,
// and separately check whether their containing native view accepts input.
class DocumentActions {
public:
    void reset(const DocumentNode* document) {
        actions_.clear();
        std::map<Command,std::size_t> occurrences;
        if(document)visit(*document,true,occurrences);
    }
    const DocumentAction* find(const DocumentNode* node) const {
        const auto found=std::find_if(actions_.begin(),actions_.end(),[&](const auto& action){return action.node==node;});
        return found==actions_.end()?nullptr:&*found;
    }
    const DocumentAction* find(DocumentActionIdentity identity) const {
        const auto found=std::find_if(actions_.begin(),actions_.end(),[&](const auto& action){return action.identity==identity;});
        return found==actions_.end()?nullptr:&*found;
    }
    bool enabled(DocumentActionIdentity identity) const {
        const auto* action=find(identity);return action&&action->enabled;
    }
    std::optional<DocumentActionIdentity> restore_focus(std::optional<DocumentActionIdentity> previous) const {
        return previous&&enabled(*previous)?previous:std::nullopt;
    }
private:
    std::vector<DocumentAction> actions_;
    void visit(const DocumentNode& node,bool parent_enabled,std::map<Command,std::size_t>& occurrences) {
        const bool enabled=parent_enabled&&node.enabled;
        if(node.kind==DocumentKind::action) {
            const DocumentActionIdentity identity{node.command,node.instance?0:occurrences[node.command]++,node.instance};
            if(node.instance && find(identity))throw std::invalid_argument("document action instances must be unique within a command");
            actions_.push_back({&node,identity,enabled});
        }
        // Only containers materialize children. A leaf's unused child values
        // must not change the identity or eligibility of rendered actions.
        if(node.kind==DocumentKind::row||node.kind==DocumentKind::column)
            for(const auto& child:node.children)visit(child,enabled,occurrences);
    }
};
}

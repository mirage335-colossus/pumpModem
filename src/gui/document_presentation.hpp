#pragma once
#include "document_actions.hpp"
#include "document_layout.hpp"
#include <memory>
#include <optional>

namespace datapump::gui::ui {
// The shared document presentation owns rendered structure, action association
// and inherited availability. Native renderers retain their own widget handles
// and may reconcile or rebuild them according to their toolkit's ownership.
class DocumentPresentation {
public:
    struct Node {
        const DocumentNode* source=nullptr;
        std::optional<DocumentActionIdentity> action;
        bool enabled=true;
        std::vector<Node> children;
    };
    struct Placement {
        const Node* node=nullptr;
        DocumentRect relative,absolute,content;
        bool allocated=false,enabled=false;
    };
    struct Layout {int height=0;std::vector<Placement> nodes;};
    bool reset(std::shared_ptr<const DocumentNode> source) {
        if(source_==source)return false;
        DocumentActions actions;actions.reset(source.get());
        std::optional<Node> root;if(source)root=describe(*source,true,actions);
        source_=std::move(source);actions_=std::move(actions);root_=std::move(root);return true;
    }
    const Node* root() const {return root_?&*root_:nullptr;}
    const DocumentActions& actions() const {return actions_;}
    template<class MeasureText> Layout layout(int width,MeasureText measure,int x=0,int y=0) const {
        Layout result;if(!root_)return result;
        const auto geometry=layout_document(*source_,width,std::move(measure));result.height=geometry.height;
        const auto& bounds=geometry.root.bounds;
        append(result,*root_,geometry.root,x,y,x,y,
            {x+bounds.x,y+bounds.y,bounds.width,bounds.height});return result;
    }
private:
    std::shared_ptr<const DocumentNode> source_;
    DocumentActions actions_;
    std::optional<Node> root_;
    static Node describe(const DocumentNode& source,bool parent_enabled,const DocumentActions& actions) {
        Node node;node.source=&source;node.enabled=parent_enabled&&source.enabled;
        if(const auto* action=actions.find(&source))node.action=action->identity;
        if(source.kind==DocumentKind::column||source.kind==DocumentKind::row) {
            node.children.reserve(source.children.size());
            for(const auto& child:source.children)node.children.push_back(describe(child,node.enabled,actions));
        }
        return node;
    }
    static DocumentRect intersect(DocumentRect first,DocumentRect second) {
        const auto x=std::max(first.x,second.x),y=std::max(first.y,second.y);
        const auto right=std::min(static_cast<long long>(first.x)+first.width,static_cast<long long>(second.x)+second.width);
        const auto bottom=std::min(static_cast<long long>(first.y)+first.height,static_cast<long long>(second.y)+second.height);
        return {x,y,static_cast<int>(std::max(0LL,right-x)),static_cast<int>(std::max(0LL,bottom-y))};
    }
    static void append(Layout& layout,const Node& node,const DocumentBox& box,
                       int origin_x,int origin_y,int parent_x,int parent_y,DocumentRect parent_clip) {
        const auto& bounds=box.bounds;
        const DocumentRect relative{origin_x+bounds.x,origin_y+bounds.y,bounds.width,bounds.height};
        const DocumentRect absolute{parent_x+bounds.x,parent_y+bounds.y,bounds.width,bounds.height};
        // Fixed-height containers clip their descendants in both toolkits.
        // A positive child rectangle can still be entirely outside that clip;
        // it must not retain keyboard focus or accept a queued activation.
        const auto clip=intersect(absolute,parent_clip);
        const bool allocated=clip.width>0&&clip.height>0;
        layout.nodes.push_back({&node,relative,absolute,box.content,allocated,node.enabled&&allocated});
        for(std::size_t index=0;index<node.children.size();++index)
            append(layout,node.children[index],box.children[index],0,0,absolute.x,absolute.y,clip);
    }
};
}

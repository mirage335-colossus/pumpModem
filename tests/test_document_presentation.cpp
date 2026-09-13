#include "document_presentation.hpp"
#include <iostream>

using namespace datapump::gui::ui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void structure_actions_and_allocation() {
    auto source=std::make_shared<DocumentNode>();source->padding=3;source->top=2;
    DocumentNode text;text.kind=DocumentKind::text;text.text="Leaf";
    DocumentNode action;action.kind=DocumentKind::action;action.command=Command::clear_received;action.instance=7;action.text="Action";
    text.children.push_back(action); // Unrendered leaf children never acquire handles or action identities.
    DocumentNode nested;nested.kind=DocumentKind::column;nested.enabled=false;nested.padding=2;nested.children={action};
    source->children={text,nested};
    DocumentPresentation presentation;
    check(presentation.reset(source)&&!presentation.reset(source),"Immutable document identity did not suppress unchanged replacement");
    const auto* root=presentation.root();
    check(root&&root->children.size()==2&&root->children[0].children.empty(),"Document render structure included leaf children");
    const auto& child=root->children[1].children[0];
    check(child.action&&child.action->instance==7&&!child.enabled&&!presentation.actions().enabled(*child.action),"Rendered action association lost inherited availability");
    unsigned measured=0;const auto layout=presentation.layout(100,[&](const auto&,int){++measured;return 10;},11,13);
    check(measured==2&&layout.nodes.size()==4,"Native measurement/traversal included an unrendered leaf child");
    check(layout.nodes[0].relative.x==11&&layout.nodes[0].relative.y==15&&layout.nodes[0].absolute==layout.nodes[0].relative,"Root placement lost native origin or document margin");
    check(layout.nodes[1].relative.x==3&&layout.nodes[1].absolute.x==14&&layout.nodes[1].absolute.y==18,"Nested native coordinates did not share accumulated placement");
    check(layout.nodes.back().allocated&&!layout.nodes.back().enabled,"Geometry incorrectly enabled a disabled document action");
    const auto zero=presentation.layout(0,[](const auto&,int){return 10;});
    for(const auto& item:zero.nodes)check(!item.allocated&&!item.enabled,"Zero-width ancestor left descendant native input allocated");
    auto replacement=std::make_shared<DocumentNode>(*source);replacement->children[1].enabled=true;
    const auto identity=*child.action;presentation.reset(replacement);
    check(presentation.actions().restore_focus(identity)==identity,"Shared action identity failed to survive document replacement");
    presentation.reset({});check(!presentation.root()&&presentation.layout(100,[](const auto&,int){return 10;}).nodes.empty(),"Clearing a document retained native presentation nodes");
}
}
int main() {
    try {structure_actions_and_allocation();std::cout<<"Shared document presentation passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

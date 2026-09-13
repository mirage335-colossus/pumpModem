#include "document_layout.hpp"
#include "document_actions.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
namespace ui=datapump::gui::ui;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
ui::DocumentNode fixed(int width,int height) {
    ui::DocumentNode node;node.kind=ui::DocumentKind::bitmap;node.width=width;node.height=height;return node;
}
ui::DocumentNode text(std::string value) {
    ui::DocumentNode node;node.kind=ui::DocumentKind::text;node.text=std::move(value);return node;
}
// Deliberately deterministic glyph metrics: geometry must be testable without
// either toolkit, and native adapters may supply different font measurements.
int glyph_height(const ui::DocumentNode& node,int width) {
    return ((static_cast<int>(node.text.size())*10+width-1)/width)*12;
}
void relative_widths() {
    ui::DocumentNode row;row.kind=ui::DocumentKind::row;
    row.children={fixed(100,20),fixed(0,30)};
    auto layout=ui::layout_document(row,400,glyph_height);
    require(layout.root.children[0].bounds==ui::DocumentRect{0,0,100,20},"Explicit row width changed");
    require(layout.root.children[1].bounds==ui::DocumentRect{100,0,300,30},"Auto row width did not take the remainder");
    row.children[0].right=20;row.children[1].right=30;
    layout=ui::layout_document(row,400,glyph_height);
    require(layout.root.children[1].bounds==ui::DocumentRect{120,0,250,30},"Row margins were not reserved in the remainder");
    row.children={fixed(500,20),fixed(20,30),fixed(0,40)};
    layout=ui::layout_document(row,400,glyph_height);
    require(layout.root.children[0].bounds.width==400,"Oversized row child was not clamped");
    for(std::size_t i=1;i<3;++i)require(layout.root.children[i].bounds.x==400&&layout.root.children[i].bounds.width==0,"Exhausted row children overflowed");
    row.width=300;row.right=50;row.children={fixed(0,20)};
    require(ui::layout_document(row,250,glyph_height).root.bounds.width==200,"Root width did not reserve right margin");
}
void padding_and_columns() {
    ui::DocumentNode column;column.padding=10;column.top=3;column.bottom=7;
    auto first=fixed(0,30);first.top=2;first.bottom=4;first.right=20;first.padding=6;
    auto second=fixed(500,25);second.top=5;second.bottom=6;
    column.children={first,second};
    auto layout=ui::layout_document(column,200,glyph_height);
    require(layout.root.bounds==ui::DocumentRect{0,3,200,92},"Column did not include padding and vertical margins");
    require(layout.height==102,"Document height omitted root margins");
    require(layout.root.children[0].bounds==ui::DocumentRect{10,12,160,30},"Column child geometry ignored padding or margins");
    require(layout.root.children[0].content==ui::DocumentRect{6,6,148,18},"Padded bitmap framebuffer did not use the inner rectangle");
    require(layout.root.children[1].bounds==ui::DocumentRect{10,51,180,25},"Column flow or width clamp changed");
    column.height=40;layout=ui::layout_document(column,200,glyph_height);
    require(layout.root.bounds.height==40&&layout.height==50,"Explicit height failed to clip overflowing content");
    auto bordered=fixed(100,50);bordered.border=true;
    const auto bordered_layout=ui::layout_document(bordered,100,glyph_height);
    require(bordered_layout.root.content==ui::DocumentRect{1,1,98,48},
        "Zero-padding bitmap content covered its explicit border");
    bordered.padding=7;
    require(ui::layout_document(bordered,100,glyph_height).root.content==ui::DocumentRect{7,7,86,36},
        "Explicit padding was increased instead of including the border inset");
    first.padding=100;column.children={first};layout=ui::layout_document(column,15,glyph_height);
    require(layout.root.content.width==0&&layout.root.children[0].content.width==0&&layout.root.children[0].content.height==0,"Excessive padding produced a negative framebuffer");
}
void text_and_equal_height() {
    ui::DocumentNode row;row.kind=ui::DocumentKind::row;row.equal_height=true;row.padding=5;
    auto short_text=text("short");short_text.width=100;short_text.top=3;short_text.bottom=7;short_text.padding=2;
    auto long_text=text(std::string(40,'W'));long_text.width=100;long_text.top=5;long_text.bottom=1;
    row.children={short_text,long_text};
    auto layout=ui::layout_document(row,210,glyph_height);
    require(layout.root.bounds.height==66,"Native glyph height was not propagated into row height");
    require(layout.root.children[0].bounds.height==46&&layout.root.children[1].bounds.height==50,"Equal-height siblings did not account for individual margins");
    const auto narrow=ui::layout_document(row,160,glyph_height);
    require(narrow.root.bounds.height>layout.root.bounds.height,"Narrower allocated text width did not increase content height");
    require(ui::layout_document(row,210,glyph_height).root.bounds.height==layout.root.bounds.height,"Restoring width did not shrink text content again");
    row.children[0].height=12;layout=ui::layout_document(row,210,glyph_height);
    require(layout.root.children[0].bounds.height==12,"Equal-height row overrode an explicit child height");
    row.height=40;row.children[0].height=0;layout=ui::layout_document(row,210,glyph_height);
    require(layout.root.children[0].bounds.height==20&&layout.root.children[1].bounds.height==24,"Fixed row inner height did not constrain equal-height siblings");
    ui::DocumentNode nested; nested.kind=ui::DocumentKind::row;nested.equal_height=true;nested.width=100;
    auto nested_text=text("Nested");nested_text.top=3;nested_text.bottom=2;nested.children={nested_text};
    ui::DocumentNode outer;outer.kind=ui::DocumentKind::row;outer.equal_height=true;
    outer.children={nested,fixed(100,100)};
    layout=ui::layout_document(outer,200,glyph_height);
    require(layout.root.children[0].bounds.height==100&&layout.root.children[0].children[0].bounds.height==95,
        "Final equal-height allocation did not propagate through a nested row");
    ui::DocumentNode action;action.kind=ui::DocumentKind::action;
    require(ui::layout_document(action,100,glyph_height).height==25,"Empty action lost the shared minimum hit area");
    action.text="Action";action.padding=3;
    require(ui::layout_document(action,100,glyph_height).height==31,"Action clearance or padding became toolkit policy");
}
void action_identity_and_eligibility() {
    ui::DocumentNode action;action.kind=ui::DocumentKind::action;action.command=ui::Command::clear_received;
    ui::DocumentNode root;root.children={action,text("heading"),action};
    // Unrendered leaf children cannot reserve occurrences or dispatch actions.
    root.children[1].children={action};
    ui::DocumentActions actions;actions.reset(&root);
    const ui::DocumentActionIdentity first{action.command,0},second{action.command,1};
    require(actions.find(&root.children[0])->identity==first && actions.find(&root.children[2])->identity==second,
        "Document action identity depended on non-rendered leaf children");
    require(!actions.find(&root.children[1].children[0]),"Leaf descendants became actionable");
    require(actions.enabled(first)&&actions.restore_focus(second)==second,"Enabled repeated action lost its identity");
    // Different commands and non-action tree insertions retain occurrence IDs.
    auto another=action;another.command=ui::Command::reset_zoom;
    ui::DocumentNode nested;nested.children={another,root.children[2]};root.children[2]=nested;
    actions.reset(&root);
    require(actions.find(&root.children[2].children[1])->identity==second && actions.restore_focus(second)==second,
        "Nested action focus changed after an unrelated insertion");
    root.children[2].enabled=false;actions.reset(&root);
    require(actions.enabled(first)&&!actions.enabled(second)&&!actions.restore_focus(second),
        "Disabled document ancestor retained an actionable or focusable descendant");
    root.enabled=false;actions.reset(&root);
    require(!actions.enabled(first)&&!actions.restore_focus(first),"Disabled root retained action input");
    root.enabled=true;root.children.resize(1);actions.reset(&root);
    require(!actions.find(second)&&!actions.restore_focus(second),"Removed command occurrence retained focus");
    root.children[0].command=ui::Command::reset_zoom;actions.reset(&root);
    require(!actions.restore_focus(first),"Replacing a command transferred document focus");
    auto stable_first=action;stable_first.instance=17;
    auto stable_second=action;stable_second.instance=28;
    root.children={stable_first,stable_second};actions.reset(&root);
    const auto stable_focus=actions.find(&root.children[1])->identity;
    std::swap(root.children[0],root.children[1]);actions.reset(&root);
    require(actions.find(&root.children[0])->identity==stable_focus && actions.restore_focus(stable_focus)==stable_focus,
        "Reordering repeated document actions changed explicit identity");
    root.children.pop_back();actions.reset(&root);
    require(actions.restore_focus(stable_focus)==stable_focus,"Removing another command instance changed document focus");
    root.children={stable_first};actions.reset(&root);
    require(!actions.restore_focus(stable_focus),"Removed explicit action transferred focus to another instance");
    root.children={stable_first,stable_first};bool duplicate_rejected=false;
    try {actions.reset(&root);}catch(const std::invalid_argument&) {duplicate_rejected=true;}
    require(duplicate_rejected,"Duplicate explicit document action identity was accepted");
    actions.reset(nullptr);
    require(!actions.find(first)&&!actions.restore_focus(std::nullopt),"Clearing a document retained actions");
}
}
int main() {
    try {relative_widths();padding_and_columns();text_and_equal_height();action_identity_and_eligibility();
        std::cout<<"Shared document layout checks passed.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

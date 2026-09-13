#pragma once
#include "ui_document.hpp"
#include <array>

namespace datapump::gui::test {
// The exact same mixed geometry declaration and expected rectangles run
// through both native document factories. No application subjects are needed.
inline ui::DocumentNode document_geometry_fixture() {
    const auto bitmap=[](float width,float height) {
        ui::DocumentNode node;node.kind=ui::DocumentKind::bitmap;node.width=width;node.height=height;return node;
    };
    ui::DocumentNode root;root.width=400;
    ui::DocumentNode remainder;remainder.kind=ui::DocumentKind::row;
    remainder.children={bitmap(100,20),bitmap(0,30)};
    ui::DocumentNode margins=remainder;
    margins.children[0].right=20;margins.children[1].right=30;
    ui::DocumentNode equal;equal.kind=ui::DocumentKind::row;equal.equal_height=true;equal.padding=5;
    ui::DocumentNode short_card;short_card.width=100;short_card.top=3;short_card.bottom=7;short_card.padding=2;
    short_card.children={bitmap(0,16)};
    auto tall_card=short_card;tall_card.top=5;tall_card.bottom=1;tall_card.padding=0;tall_card.children={bitmap(0,50)};
    equal.children={short_card,tall_card};
    auto padded=bitmap(100,50);padded.padding=7;
    auto overflow=remainder;overflow.children={bitmap(500,20),bitmap(20,30)};
    ui::DocumentNode action;action.kind=ui::DocumentKind::action;action.width=100;action.padding=8;
    action.text="A wrapped action label with several native words";action.command=ui::Command::clear_received;
    action.fill=ui::DocumentFill::parity;action.border=true;
    auto bordered=bitmap(100,50);bordered.border=true;
    root.children={remainder,margins,equal,padded,overflow,action,bordered};
    return root;
}
// Coordinates relative to each parent, independent of native font metrics.
inline constexpr std::array<std::array<int,4>,5> document_geometry_rows{{
    {{0,0,400,30}},{{0,30,400,30}},{{0,60,400,66}},{{0,126,100,50}},{{0,176,400,30}}
}};
inline constexpr std::array<std::array<int,4>,2> document_geometry_remainder{{{{0,0,100,20}},{{100,0,300,30}}}};
inline constexpr std::array<std::array<int,4>,2> document_geometry_margins{{{{0,0,100,20}},{{120,0,250,30}}}};
inline constexpr std::array<std::array<int,4>,2> document_geometry_equal{{{{5,8,100,46}},{{105,10,100,50}}}};
inline constexpr std::array<std::array<int,4>,2> document_geometry_overflow{{{{0,0,400,20}},{{400,0,0,30}}}};
inline ui::DocumentNode document_actions_fixture() {
    ui::DocumentNode first;first.kind=ui::DocumentKind::action;first.text="First occurrence";
    first.command=ui::Command::clear_received;first.height=30;first.instance=1;
    auto second=first;second.text="Second occurrence";second.instance=2;
    ui::DocumentNode heading;heading.kind=ui::DocumentKind::text;heading.text="Literal heading";
    heading.children={first}; // Leaf descendants do not materialize.
    ui::DocumentNode nested;nested.children={second};
    ui::DocumentNode root;root.children={first,heading,nested};return root;
}
inline ui::DocumentNode document_empty_action_fixture() {
    ui::DocumentNode bitmap;bitmap.kind=ui::DocumentKind::bitmap;bitmap.width=50;bitmap.height=30;
    ui::DocumentNode action;action.kind=ui::DocumentKind::action;action.text="Remainder action";
    action.command=ui::Command::clear_received;action.instance=3;
    ui::DocumentNode row;row.kind=ui::DocumentKind::row;row.width=100;row.children={bitmap,action};return row;
}
}

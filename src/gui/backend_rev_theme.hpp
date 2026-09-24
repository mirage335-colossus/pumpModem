#pragma once
// Include after Rev Appearance, Color, Element, Box, Text and Button imports.
// Native color conversion and state binding only; palette roles remain shared.
#include "theme.hpp"
#include <utility>

namespace datapump::gui::theme {
inline bool rev_color_enabled=false;
inline auto rev_color(Rgb color,float opacity=1) {
    return Rev::Appearance::rgba(color.red,color.green,color.blue,opacity);
}
inline auto rev_color(WidgetRole role,float opacity=1) {
    return rev_color(widget_rgb(role,rev_color_enabled),opacity);
}
inline Rev::Core::Color rev_primitive_color(WidgetRole role,float opacity=1) {
    const auto color=widget_rgb(role,rev_color_enabled);
    return {color.red/255.0f,color.green/255.0f,color.blue/255.0f,opacity};
}
inline Rev::Appearance::Style rev_disabled_text={.applies={.disabled=true},.text={.color=rev_color(WidgetRole::disabled_text)}};
inline Rev::Appearance::Style rev_disabled_control={.applies={.disabled=true},
    .background={.color=rev_color(WidgetRole::disabled_background)},.border={.color=rev_color(WidgetRole::disabled_border)}};
// Rev applies an element's inline style after conditional styles. Native parent
// disabling must still reach semantic foregrounds and borders supplied inline.
// Resolve those effective native states after its normal style/layout pass.
template<class Native> struct RevDecoration : Native {
    using Native::Native;
    void resolveStyle(Rev::Element::Event& event) override {
        Native::resolveStyle(event);
        if(this->resolved.disabled)this->resolved.style.border.color=rev_color(WidgetRole::disabled_border);
    }
};
using RevBox=RevDecoration<Rev::Element::Box>;
struct RevText : RevDecoration<Rev::Element::Text> {
    using RevDecoration<Rev::Element::Text>::RevDecoration;
    void resolveStyle(Rev::Element::Event& event) override {
        RevDecoration<Rev::Element::Text>::resolveStyle(event);
        if(resolved.disabled)resolved.style.text.color=rev_color(WidgetRole::disabled_text);
    }
};
struct RevButton : RevDecoration<Rev::Element::Button> {
    RevButton(Rev::Element::Element* parent,Rev::Element::Button::Params params,Rev::Appearance::StyleList styles={})
        :RevDecoration<Rev::Element::Button>(parent,std::move(params),std::move(styles)) {
        // Preserve the native button, callbacks and label styles; its child text
        // uses the same effective disabled-state binding as every other label.
        const auto content=labelText->content.get();const auto label_styles=labelText->styles;
        delete labelText;labelText=new RevText(this,content,label_styles);
    }
    void resolveStyle(Rev::Element::Event& event) override {
        RevDecoration<Rev::Element::Button>::resolveStyle(event);
        if(resolved.disabled)resolved.style.background.color=rev_color(WidgetRole::disabled_background);
    }
};
}

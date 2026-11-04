#pragma once
#include "service_queue.hpp"
#include <algorithm>
#include <cmath>

namespace datapump::gui::ui {
// Application-owned chrome preferences. A native chooser or popup may apply
// additional host fitting; adapters do not invent a second set of preferences.
inline constexpr int chrome_margin=16,chrome_gap=12,chrome_font_size=13;
inline constexpr int bitmap_caption_font_size=11;
inline constexpr int popup_min_width=220,tooltip_max_width=600;
inline constexpr int tooltip_padding=8,tooltip_font_size=12,tooltip_gap=4;
inline constexpr float tooltip_delay=0,tooltip_hover_delay=0,tooltip_hide_delay=12;
inline constexpr const char* choice_placeholder="None";
inline constexpr const char* preset_indicator="v";
inline constexpr const char* service_cancel_label="Cancel";

// Seconds on the native monotonic clock. A toolkit with its own tooltip timer
// consumes the same settings directly; retained renderers use this tiny state.
class TooltipTiming {
public:
    TooltipTiming(double delay=tooltip_delay,double hover_delay=tooltip_hover_delay,double hide_delay=tooltip_hide_delay)
        :delay_(delay),hover_delay_(hover_delay),hide_delay_(hide_delay) {}
    void enter(double now) {
        const bool recent=visible(now)||(recent_until_&&now<=*recent_until_);
        show_at_=now+(recent?hover_delay_:delay_);hide_at_=*show_at_+hide_delay_;
    }
    void leave(double now) {
        if(visible(now)||(recent_until_&&now<=*recent_until_))recent_until_=now+hover_delay_;
        show_at_.reset();hide_at_.reset();
    }
    void cancel() {show_at_.reset();hide_at_.reset();recent_until_.reset();}
    bool visible(double now) const {return show_at_&&hide_at_&&now>=*show_at_&&now<*hide_at_;}
private:
    double delay_,hover_delay_,hide_delay_;
    std::optional<double> show_at_,hide_at_,recent_until_;
};

struct ServiceDialogPresentation {
    std::string title,body,value,accept_label,cancel_label;
    ServiceInputPolicy input;
};
inline ServiceDialogPresentation service_dialog(const ServiceRequest& request) {
    return {request.title,request.kind==ServiceKind::prompt?"":"Enter a path on this computer.",request.value,
        request.kind==ServiceKind::open_file?"Open":request.kind==ServiceKind::save_file?"Save":"Continue",
        service_cancel_label,service_input_policy(request).value_or(ServiceInputPolicy{})};
}
struct ServiceDialogLayout {
    Rect frame,title,body,input,accept,cancel;
};
enum class ServiceTextRole { title,body };
template<class Measure> ServiceDialogLayout service_dialog_layout(const ServiceDialogPresentation& dialog,
        int available_width,int available_height,Measure measure) {
    ServiceDialogLayout out;
    const int width=std::max(1,std::min(560,available_width-2*chrome_margin));
    const int padding=std::min(chrome_margin,width/2),inner=std::max(0,width-2*padding);
    int cursor=padding;
    const auto text=[&](const std::string& value,int size,ServiceTextRole role) {
        const int height=value.empty()?0:std::max(size+2,static_cast<int>(std::ceil(measure(value,size,std::max(1,inner),role)))+2);
        const Rect rect{padding,cursor,inner,height};if(height)cursor+=height+chrome_gap;return rect;
    };
    out.title=text(dialog.title,chrome_font_size,ServiceTextRole::title);
    out.body=text(dialog.body,tooltip_font_size,ServiceTextRole::body);
    out.input={padding,cursor,inner,30};cursor+=out.input.h+chrome_gap;
    const int button_width=std::min(100,std::max(0,(inner-8)/2));
    out.cancel={width-padding-button_width,cursor,button_width,30};
    out.accept={std::max(padding,out.cancel.x-8-button_width),cursor,button_width,30};
    const int height=cursor+30+padding;
    out.frame={std::max(0,(available_width-width)/2),
        std::clamp(available_height/5,0,std::max(0,available_height-height)),width,height};
    return out;
}
struct PopupLayout {
    int width=0,left=0;
    bool open_upward=false;
};
inline PopupLayout popup_layout(Rect owner,int viewport_width,bool prefer_upward) {
    const int inset=std::min(chrome_margin,std::max(0,viewport_width)/2);
    const int available=std::max(0,viewport_width-2*inset);
    const int width=std::min(available,std::max(owner.w,popup_min_width));
    const int x=std::clamp(owner.x,inset,std::max(inset,viewport_width-inset-width));
    return {width,x-owner.x,prefer_upward};
}
inline int tooltip_width(int viewport_width) {
    return std::max(0,std::min(tooltip_max_width,viewport_width-2*chrome_margin));
}
inline Rect tooltip_layout(Rect owner,int viewport_width,int viewport_height,int measured_height) {
    const int width=tooltip_width(viewport_width);
    const int height=std::min(std::max(0,viewport_height-2*tooltip_padding),std::max(0,measured_height)+2*tooltip_padding);
    const int inset=std::min(chrome_margin,std::max(0,viewport_width-width)/2);
    const int x=std::clamp(owner.x,inset,std::max(inset,viewport_width-width-inset));
    const int below=owner.y+owner.h+tooltip_gap,above=owner.y-height-tooltip_gap;
    const int y=below+height<=viewport_height-tooltip_padding?below:above;
    return {x,std::clamp(y,0,std::max(0,viewport_height-height)),width,height};
}
struct CheckboxLayout {Rect box,label;};
inline CheckboxLayout checkbox_layout(int width,int height) {
    const int box=std::min({20,std::max(0,width),std::max(0,height)});
    const int left=std::min(std::max(0,width),box+5);
    const int text_height=std::min(20,std::max(0,height));
    return {{0,std::max(0,(height-box)/2),box,box},{left,std::max(0,(height-text_height)/2),std::max(0,width-left),text_height}};
}
inline Rect empty_record_rect(int width,int height) {
    const int left=std::min(12,std::max(0,width)/2),top=std::min(8,std::max(0,height)/2);
    return {left,top,std::max(0,width-2*left),std::max(0,height-2*top)};
}
}

#include "../src/gui/chrome_layout.hpp"
#include <iostream>
#include <stdexcept>

using namespace datapump::gui::ui;
namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
bool contains(Rect outer,Rect inner) {
    return inner.w>=0&&inner.h>=0&&inner.x>=outer.x&&inner.y>=outer.y&&
        inner.x+inner.w<=outer.x+outer.w&&inner.y+inner.h<=outer.y+outer.h;
}
void service_chrome() {
    const auto prompt=service_dialog({1,ServiceKind::prompt,"Rename","initial",7});
    require(prompt.title=="Rename"&&prompt.body.empty()&&prompt.value=="initial"&&
        prompt.accept_label=="Continue"&&prompt.cancel_label==service_cancel_label&&
        !prompt.input.multiline&&prompt.input.byte_limit==7,"Prompt presentation lost request wording or input policy");
    for(const auto kind:{ServiceKind::open_file,ServiceKind::save_file}) {
        const auto dialog=service_dialog({2,kind,"File location","/tmp",1024});
        require(!dialog.body.empty()&&dialog.input.multiline&&dialog.input.byte_limit==1024&&
            dialog.accept_label==(kind==ServiceKind::open_file?"Open":"Save"),"File service presentation lost its shared action or path policy");
        for(const auto width:{240,560,1200}) {
            const auto short_layout=service_dialog_layout(dialog,width,700,[](const auto&,int,int,ServiceTextRole){return 18;});
            const auto wrapped_layout=service_dialog_layout(dialog,width,700,[](const auto&,int,int,ServiceTextRole){return 54;});
            for(const auto& layout:{short_layout,wrapped_layout}) {
                require(contains({0,0,width,700},layout.frame),"Service dialog falls outside its host");
                const Rect content{0,0,layout.frame.w,layout.frame.h};
                for(const auto rect:{layout.title,layout.body,layout.input,layout.accept,layout.cancel})
                    require(contains(content,rect),"Service dialog child falls outside its frame");
                require(layout.title.y+layout.title.h<=layout.body.y&&layout.body.y+layout.body.h<=layout.input.y&&
                    layout.input.y+layout.input.h<=layout.accept.y&&layout.accept.x+layout.accept.w<=layout.cancel.x,
                    "Service dialog text, input or actions overlap");
            }
            require(wrapped_layout.frame.h>short_layout.frame.h&&wrapped_layout.input.y>short_layout.input.y,
                "Measured wrapping did not move service input and enlarge its dialog");
        }
    }
}
void popup_chrome() {
    const auto normal=popup_layout({30,20,60,27},900,false);
    require(normal.width==popup_min_width&&normal.left==0&&!normal.open_upward,"Narrow selector lost the shared popup minimum");
    const auto large=popup_layout({30,20,350,27},900,true);
    require(large.width==350&&large.open_upward,"Wide selector or upward preference was ignored");
    const auto right=popup_layout({850,20,50,27},900,false);
    require(right.left<0&&850+right.left+right.width<=900-chrome_margin,"Popup did not fit against the right edge");
    const auto narrow=popup_layout({70,20,25,27},120,true);
    require(narrow.width<popup_min_width&&70+narrow.left>=0&&70+narrow.left+narrow.width<=120,
        "Popup minimum overrode a constrained viewport");
}
void tooltip_chrome() {
    const auto below=tooltip_layout({40,40,80,27},1000,700,48);
    const auto above=tooltip_layout({940,660,40,27},1000,700,48);
    require(below.y>67&&above.y+above.h<660,"Tooltip did not choose available space above or below its owner");
    for(const auto size:{Rect{0,0,1000,700},Rect{0,0,240,120},Rect{0,0,20,12}}) {
        const auto tooltip=tooltip_layout({size.w-5,size.h-5,5,5},size.w,size.h,400);
        require(contains(size,tooltip),"Tooltip falls outside its viewport");
        require(tooltip.w<=tooltip_max_width,"Tooltip ignored shared wrapping width");
    }
    TooltipTiming timing(1,.25,2);
    timing.enter(10);require(!timing.visible(10.9)&&timing.visible(11)&&!timing.visible(13),
        "Tooltip initial delay or visible lifetime was ignored");
    timing.enter(20);timing.leave(20.5);require(!timing.visible(21),"Leaving before the delay allowed a tooltip to reappear");
    timing.enter(30);timing.leave(31.5);timing.enter(31.6);
    require(!timing.visible(31.8)&&timing.visible(31.9),"Moving between recently shown tooltips ignored the hover delay");
    timing.leave(32);timing.enter(33);
    require(!timing.visible(33.5)&&timing.visible(34),"Expired hover grace skipped the ordinary tooltip delay");
    timing.cancel();require(!timing.visible(34.5),"Modal/page/close cancellation retained a tooltip timer");
}
void control_chrome() {
    for(const auto area:{Rect{0,0,300,28},Rect{0,0,10,8},Rect{0,0,0,0}}) {
        const auto checkbox=checkbox_layout(area.w,area.h);
        require(contains(area,checkbox.box)&&contains(area,checkbox.label)&&checkbox.box.x+checkbox.box.w<=checkbox.label.x,
            "Checkbox chrome clips or overlaps within its declared area");
        require(contains(area,empty_record_rect(area.w,area.h)),"Empty record message extends beyond its area");
    }
}
}
int main() {
    try {service_chrome();popup_chrome();tooltip_chrome();control_chrome();std::cout<<"gui chrome contract passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

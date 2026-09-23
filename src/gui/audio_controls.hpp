#pragma once
#include "ui_contract.hpp"
#include "datapump/audio.hpp"
#include <algorithm>
#include <stdexcept>

namespace datapump::gui::audio_controls {
inline std::vector<ui::Option> volume_options() {
    std::vector<ui::Option> result;
    for(const auto* value:{"0.01","0.1","0.5","1","2","3","4","5","10","20","30","40","50","60","70","75","80","85","90","95","100","105","110","115","125","150","175"})
        result.push_back({value,std::string(value)+"%"});
    return result;
}
inline double volume_gain(const std::string& id) {
    const auto options=volume_options();
    if(std::none_of(options.begin(),options.end(),[&](const auto& option){return option.id==id;}))
        throw std::invalid_argument("Select an available transmit volume");
    return std::stod(id)/100.0;
}
inline bool exclusive_supported() {return audio::exclusive_supported();}
inline void set_device_options(ui::FieldState& state,const std::vector<audio::Device>& devices) {
    if(state.selected.empty())state.selected=state.text.empty()?"default":state.text;
    state.text=state.selected;
    state.options={{"default","System default"}};
    for(const auto& device:devices) {
        if(device.id.empty()||device.id=="default")continue;
        auto description=device.description;
        std::replace(description.begin(),description.end(),'\n',' ');
        const auto label=description.empty()||description==device.id?device.id:device.id+" — "+description;
        const auto existing=std::find_if(state.options.begin(),state.options.end(),[&](const auto& option){return option.id==device.id;});
        if(existing==state.options.end())state.options.push_back({device.id,label});
        else if(existing->label.find(description)==std::string::npos)existing->label+=" / "+description;
    }
    if(std::none_of(state.options.begin(),state.options.end(),[&](const auto& option){return option.id==state.selected;}))
        state.options.push_back({state.selected,state.selected});
}
}

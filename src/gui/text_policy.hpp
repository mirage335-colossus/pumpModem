#pragma once
#include "ui_contract.hpp"
#include "utf8_policy.hpp"
#include <string_view>

namespace datapump::gui::ui {
// Native editors validate the proposed replacement before touching selection;
// the authoritative controller repeats the same check for every input source.
inline std::string edit_error(std::string_view text,bool multiline,std::size_t limit) {
    if(text.size()>limit)return "Text exceeds this field's byte limit";
    if(!valid_clipboard_text(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()),text.size())))
        return "Text must be valid UTF-8 without embedded zero bytes";
    if(!multiline&&text.find_first_of("\r\n")!=std::string_view::npos)return "This field accepts one line";
    return {};
}
inline std::string edit_error(const Control& control,std::string_view text) {
    return edit_error(text,control.multiline,control.byte_limit);
}
}

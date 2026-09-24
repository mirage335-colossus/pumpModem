#pragma once
#include "ui_contract.hpp"
#include "utf8_policy.hpp"
#include <algorithm>
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
// Native cursors use byte offsets. A model update may replace the character
// under an existing offset, so clamp both selection ends and the caret using
// the same UTF-8 rule before any toolkit applies them.
inline int text_boundary(std::string_view text,int position) {
    position=static_cast<int>(std::clamp<std::ptrdiff_t>(position,0,static_cast<std::ptrdiff_t>(text.size())));
    while(position>0&&position<static_cast<int>(text.size())&&
          (static_cast<unsigned char>(text[static_cast<std::size_t>(position)])&0xc0)==0x80)--position;
    return position;
}
struct TextSelection {
    int cursor=0,anchor=0,end=0;
    TextSelection clamped(std::string_view text) const {
        return {text_boundary(text,cursor),text_boundary(text,anchor),text_boundary(text,end)};
    }
};
struct TextEdit {
    std::string text,error;
    int start=0,end=0,cursor=0;
    bool changed=false;
    explicit operator bool() const {return error.empty()&&changed;}
};
// Construct and validate the complete replacement before a native editor
// changes its buffer, selection, undo history or emits a controller callback.
// Replacing text with identical bytes is a silent no-op in every backend.
inline TextEdit text_edit(std::string_view current,TextSelection selection,std::string_view inserted,
                          bool multiline,std::size_t limit) {
    TextEdit result;result.text=current;
    selection=selection.clamped(current);
    result.start=std::min(selection.anchor,selection.end);result.end=std::max(selection.anchor,selection.end);
    if(result.start==result.end)result.start=result.end=selection.cursor;
    result.text.replace(static_cast<std::size_t>(result.start),static_cast<std::size_t>(result.end-result.start),inserted);
    result.error=edit_error(result.text,multiline,limit);
    result.cursor=result.start+static_cast<int>(inserted.size());
    result.changed=result.error.empty()&&result.text!=current;
    return result;
}
}

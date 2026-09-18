#pragma once
#include "ui_contract.hpp"
#include "bitmap.hpp"
#include <string>
#include <optional>
#include <tuple>
#include <vector>

namespace datapump::gui::ui {
// Native text and controls surrounding opaque bitmaps. This vocabulary is
// independent of the document's subject and of every widget toolkit.
enum class DocumentKind { column, row, text, bitmap, action, control };
enum class DocumentTone { text, muted, accent, comparison, positive, caution, negative };
enum class DocumentFill { none, surface, alternate, parity };
// Identity survives document replacement and reordering. Repeated controls
// use distinct instances, just as ordinary native bindings do.
inline auto document_control_identity(const Control& c) {
    return std::tuple(c.surface,c.page,c.kind,c.field,c.command,c.bitmap,c.menu,c.instance);
}
// Editable text (with optional presets) and choices reuse native controls.
// Actions and bitmaps retain their existing document nodes.
inline bool document_control_supported(const Control& c) {
    return c.menu==Menu::none&&(c.kind==Kind::text||c.kind==Kind::choice);
}
struct DocumentNode {
    DocumentKind kind=DocumentKind::column;
    std::string text,plot_name;
    float width=0,height=0,padding=0,top=0,bottom=0,right=0,font_size=12;
    DocumentTone tone=DocumentTone::muted;
    DocumentFill fill=DocumentFill::none;
    bool bold=false,border=false,equal_height=false,enabled=true;
    Command command=Command::none;
    // Nonzero action instances are stable within a command across insertions,
    // removals and reordering. Zero retains declaration-occurrence identity.
    unsigned instance=0;
    BitmapSource plot;
    std::optional<Control> control; // Text/choice only; unsupported kinds are inert.
    std::vector<DocumentNode> children;
};
}

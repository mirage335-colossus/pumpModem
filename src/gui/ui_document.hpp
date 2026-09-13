#pragma once
#include "ui_contract.hpp"
#include "plot_render.hpp"
#include <string>
#include <vector>

namespace datapump::gui::ui {
// Native text and controls surrounding opaque bitmaps. This vocabulary is
// independent of the document's subject and of every widget toolkit.
enum class DocumentKind { column, row, text, bitmap, action };
enum class DocumentTone { text, muted, accent };
enum class DocumentFill { none, surface, alternate, parity };
struct DocumentNode {
    DocumentKind kind=DocumentKind::column;
    std::string text,plot_name;
    float width=0,height=0,padding=0,top=0,bottom=0,right=0,font_size=12;
    DocumentTone tone=DocumentTone::muted;
    DocumentFill fill=DocumentFill::none;
    bool bold=false,border=false,equal_height=false,enabled=true;
    Command command=Command::none;
    plots::PlotSnapshot plot;
    std::vector<DocumentNode> children;
};
}

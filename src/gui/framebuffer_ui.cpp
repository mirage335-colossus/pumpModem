#include "framebuffer_ui.hpp"
#include "binding_state.hpp"
#include "control_binding.hpp"
#include "chrome_layout.hpp"
#include "document_layout.hpp"
#include "document_presentation.hpp"
#include "record_interactions.hpp"
#include "record_scroll.hpp"
#include "service_queue.hpp"
#include "text_policy.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace datapump::gui::framebuffer {
using namespace surface;
namespace {
using ControlIdentity=decltype(ui::document_control_identity(std::declval<ui::Control>()));
using Identity=std::tuple<ControlIdentity,bool,std::size_t>;
bool contains(ui::Rect r,int x,int y) {return x>=r.x&&y>=r.y&&x-r.x<r.w&&y-r.y<r.h;}
int next_character(std::string_view text,int position) {
    position=ui::text_boundary(text,position);
    if(position<static_cast<int>(text.size()))++position;
    while(position<static_cast<int>(text.size())&&(static_cast<unsigned char>(text[position])&0xc0)==0x80)++position;
    return position;
}

int previous_character(std::string_view text,int position) {return ui::text_boundary(text,std::max(0,position-1));}
struct Line {std::string text;int begin=0,end=0;};
// One UTF-8 scalar occupies one logical glyph cell. Hosts may substitute a
// missing glyph, but byte offsets always address the unchanged UTF-8 source.
std::vector<Line> lines(std::string_view text,int width) {
    width=std::max(1,width);std::vector<Line> result;int begin=0,column=0;
    for(int i=0;i<static_cast<int>(text.size());) {
        if(text[i]=='\n') {result.push_back({std::string(text.substr(begin,i-begin)),begin,i});begin=++i;column=0;continue;}
        if(column==width) {result.push_back({std::string(text.substr(begin,i-begin)),begin,i});begin=i;column=0;}
        i=next_character(text,i);++column;
    }
    result.push_back({std::string(text.substr(begin)),begin,static_cast<int>(text.size())});return result;
}
int glyphs(std::string_view text) {int result=0;for(int i=0;i<static_cast<int>(text.size());i=next_character(text,i))++result;return result;}
int glyph_offset(std::string_view text,int count) {int i=0;while(count-->0&&i<static_cast<int>(text.size()))i=next_character(text,i);return i;}
Tone tone(ui::TextTone value) {
    switch(value) {
    case ui::TextTone::muted:return Tone::muted;case ui::TextTone::data:return Tone::data;
    case ui::TextTone::inverse:return Tone::inverse;case ui::TextTone::negative:return Tone::negative;
    default:return Tone::normal;
    }
}
Tone field_tone(ui::TextTone value) {return value==ui::TextTone::normal?Tone::data:tone(value);}
Tone tone(ui::DocumentTone value) {
    switch(value) {
    case ui::DocumentTone::muted:return Tone::muted;case ui::DocumentTone::accent:case ui::DocumentTone::comparison:return Tone::accent;
    case ui::DocumentTone::positive:return Tone::positive;case ui::DocumentTone::caution:return Tone::caution;
    case ui::DocumentTone::negative:return Tone::negative;default:return Tone::normal;
    }
}
ui::Key overlay_key(Key key) {
    switch(key) {
    case Key::escape:return ui::Key::escape;case Key::enter:return ui::Key::enter;case Key::space:return ui::Key::space;
    case Key::tab:return ui::Key::tab;case Key::left:return ui::Key::left;case Key::right:return ui::Key::right;
    case Key::up:return ui::Key::up;case Key::down:return ui::Key::down;case Key::backspace:return ui::Key::backspace;
    case Key::del:return ui::Key::del;default:return ui::Key::other;
    }
}
}

struct Session::Impl {
    struct Editor {
        std::string text;
        ui::TextSelection selection;
        std::uint64_t cursor_revision=0,history_revision=0;
        int first_line=0,first_column=0;
    };
    struct Item {
        ui::Control control;
        ui::ControlLayout geometry;
        std::vector<const ui::Control*> menu;
        bool enabled=true;
        std::string label;
        std::optional<ui::Rect> clip;
        std::optional<ui::DocumentActionIdentity> document_action;
    };
    static Identity identity(const Item& item) {
        return {ui::document_control_identity(item.control),item.document_action.has_value(),item.document_action?item.document_action->occurrence:0};
    }
    struct Hit {
        enum class Kind { item,suggestion,tab,option,accept,cancel,prompt };
        Kind kind;ui::Rect bounds;int index;
    };
    Application& app;
    Viewport viewport{1200,1048,{8,18}};
    Viewport outer_viewport=viewport;
    bool mfd=true;
    unsigned mfd_buttons=5;
    ui::Rect interior{};
    struct MfdEntry {
        ui::Control control{};
        std::optional<ui::Page> page;
        std::string label;
        bool enabled=false;
    };
    struct MfdKey {ui::Rect bounds;unsigned number;bool enabled;};
    std::array<std::vector<MfdEntry>,3> mfd_entries;
    std::array<std::size_t,3> mfd_selected{};
    std::map<ControlIdentity,std::pair<std::string,std::string>> mfd_presets;
    std::vector<MfdKey> mfd_keys;
    unsigned mfd_bank=0;
    Scene scene;
    std::vector<Item> items;
    std::vector<Hit> hits;
    std::vector<ui::Page> pages;
    std::map<Identity,Editor> editors;
    std::map<Identity,ui::RecordScroll> list_scroll;
    std::map<Identity,ui::RecordInteractions> list_events;
    std::optional<Identity> focus,saved_focus,popup,drag;
    std::optional<ui::TextSelection> saved_selection;
    std::uint64_t saved_history_revision=0,saved_cursor_revision=0;
    bool restore_selection=false;
    std::optional<ui::Rect> paint_clip;
    std::uint64_t overlay_generation=0,presented_revision=0;
    int focus_tab=-1,pan_x=0,pan_y=0,document_scroll=0,document_height=0;
    int saved_focus_tab=-1,saved_pan_x=0,saved_pan_y=0;
    int popup_selected=0,popup_scroll=0;
    std::vector<ui::Option> options;
    bool dirty=true,help=false,restore_overlay_focus=true;
    std::string notice;
    std::deque<ui::ServiceRequest> requests;
    std::optional<ui::ServiceRequest> dialog,host_wait;
    std::vector<ui::ServiceRequest> host_services;
    Editor prompt;
    int prompt_button=0;
    ui::Rect prompt_bounds;
    explicit Impl(Application& application,bool bezel,unsigned buttons):app(application),mfd(bezel),mfd_buttons(buttons) {
        if(buttons!=3&&buttons!=5)throw std::invalid_argument("MFD button count must be 3 or 5");
        resize(outer_viewport);
    }
    void resize(Viewport value) {
        outer_viewport=value;
        const int x=mfd?std::min(std::max(104,value.metrics.cell_width*17),value.width/4):0;
        const int y=mfd?std::min(2*value.metrics.line_height+46,value.height/4):0;
        interior={x,y,value.width-2*x,value.height-2*y};
        viewport={interior.w,interior.h,value.metrics};dirty=true;
    }
    int width() const {return std::max(ui::min_width,viewport.width);}
    int height() const {return std::max(ui::min_height,viewport.height-22);}
    int cw() const {return viewport.metrics.cell_width;}
    int lh() const {return viewport.metrics.line_height;}
    ui::Rect screen(ui::Rect r) const {r.x-=pan_x;r.y-=pan_y;return r;}
    static ui::Rect intersection(ui::Rect a,ui::Rect b) {
        auto result=ui::document_intersection({a.x,a.y,a.w,a.h},{b.x,b.y,b.w,b.h});return {result.x,result.y,result.width,result.height};
    }
    int find(const std::optional<Identity>& wanted) const {
        if(!wanted)return -1;
        for(int n=0;n<static_cast<int>(items.size());++n)if(identity(items[n])==*wanted)return n;
        return -1;
    }
    void primitive(Primitive::Kind kind,ui::Rect r,std::string value={},Tone tone=Tone::normal,
                   bool focused=false,bool selected=false,bool enabled=true,bool border=false,BitmapSource source={}) {
        if(r.w<=0||r.h<=0)return;
        Primitive p{kind,r,std::move(value),std::move(source),tone,focused,selected,enabled,border,paint_clip};
        scene.primitives.push_back(std::move(p));
    }
    void fill(ui::Rect r,bool selected=false,bool focused=false,bool border=false,bool enabled=true,Fill color=Fill::surface) {
        if(r.w<=0||r.h<=0)return;
        primitive(Primitive::Kind::fill,r,{},Tone::normal,focused,selected,enabled,border);
        scene.primitives.back().fill=color;
    }
    void icon(ui::Rect r,Icon shape,bool enabled=true,Tone color=Tone::normal) {
        if(r.w<=0||r.h<=0)return;
        primitive(Primitive::Kind::icon,r,{},color,false,false,enabled);
        scene.primitives.back().icon=shape;
    }
    void text(ui::Rect r,std::string_view value,Tone color=Tone::normal,bool enabled=true) {
        const auto wrapped=lines(value,std::max(1,r.w/cw()));
        for(int n=0;n<static_cast<int>(wrapped.size())&&n*lh()<r.h;++n)
            primitive(Primitive::Kind::text,{r.x,r.y+n*lh(),r.w,std::min(lh(),r.h-n*lh())},wrapped[n].text,color,false,false,enabled);
    }
    // Control chrome never wraps into a second baseline. Preserve scalar
    // boundaries when shortening a label; editable bytes are never truncated.
    void line_text(ui::Rect r,std::string_view value,Tone color=Tone::normal,bool enabled=true,bool centered=false) {
        const int columns=r.w/cw();if(columns<1||r.h<=0)return;
        const auto first=value.substr(0,value.find('\n'));std::string label(first);
        if(glyphs(first)>columns) {
            const int dots=std::min(3,columns);
            label=std::string(first.substr(0,glyph_offset(first,columns-dots)))+std::string(static_cast<std::size_t>(dots),'.');
        }
        const int height=std::min(lh(),r.h),offset=centered?std::max(0,(r.w-glyphs(label)*cw())/2):0;
        primitive(Primitive::Kind::text,{r.x+offset,r.y+(r.h-height)/2,r.w-offset,height},std::move(label),color,false,false,enabled);
    }
    void dropdown(ui::Rect rect,std::string_view value,bool focused,bool enabled,Tone color,bool menu=false) {
        fill(rect,false,focused,true,enabled,menu?Fill::surface:Fill::canvas);
        const int gutter=std::min(23,rect.w);
        const ui::Rect arrow{rect.x+rect.w-gutter,rect.y,gutter,rect.h};
        line_text({rect.x+5,rect.y+2,std::max(0,rect.w-gutter-9),std::max(0,rect.h-4)},value,color,enabled,menu);
        if(gutter>2&&rect.h>8)fill({arrow.x,rect.y+4,1,rect.h-8},true,false,false,enabled);
        const int width=std::min(10,std::max(0,gutter-6)),height=std::min(6,std::max(0,rect.h-8));
        icon({arrow.x+(gutter-width)/2,rect.y+(rect.h-height)/2,width,height},Icon::chevron_down,enabled);
    }
    void hit(Hit::Kind kind,ui::Rect rect,int index) {
        if(paint_clip)rect=intersection(rect,*paint_clip);
        if(rect.w>0&&rect.h>0)hits.push_back({kind,rect,index});
    }
    Editor& editor(const Item& item) {
        auto& e=editors[identity(item)];const auto& state=app.control(item.control).state;
        if(e.text!=state.text){e.text=state.text;e.selection=e.selection.clamped(e.text);}
        if(e.cursor_revision!=state.text_cursor_end_revision) {
            e.cursor_revision=state.text_cursor_end_revision;const int end=static_cast<int>(e.text.size());e.selection={end,end,end};
        }
        // No private undo copy survives revocation. Authoritative text is read
        // before every event, including pointer motion during a selection drag.
        e.history_revision=state.text_history_revision;return e;
    }
    ui::Rect editor_content(ui::Rect r,bool multiline=true) const {
        const int height=multiline?std::max(1,r.h-4):std::min(lh(),std::max(1,r.h-4));
        return {r.x+4,r.y+(multiline?2:(r.h-height)/2),std::max(1,r.w-8),height};
    }
    std::vector<Line> editor_lines(const Editor& e,ui::Rect r,bool multiline) const {
        return lines(e.text,multiline?std::max(1,r.w/cw()):std::max(1,static_cast<int>(e.text.size())+1));
    }
    void render_editor(Editor& e,ui::Rect outer,bool focused,bool enabled,bool multiline,Tone color) {
        fill(outer,false,focused,true,enabled,Fill::canvas);auto r=editor_content(outer,multiline);
        const auto old_clip=paint_clip;paint_clip=old_clip?intersection(r,*old_clip):r;
        const auto wrapped=editor_lines(e,r,multiline);int caret_line=0;
        for(int n=0;n<static_cast<int>(wrapped.size());++n)if(e.selection.cursor>=wrapped[n].begin)caret_line=n;
        const int rows=std::max(1,r.h/lh()),columns=std::max(1,r.w/cw());
        const int caret_column=glyphs(std::string_view(e.text).substr(wrapped[caret_line].begin,e.selection.cursor-wrapped[caret_line].begin));
        if(focused) {
            if(caret_line<e.first_line)e.first_line=caret_line;
            if(caret_line>=e.first_line+rows)e.first_line=caret_line-rows+1;
            if(!multiline) {
                if(caret_column<e.first_column)e.first_column=caret_column;
                if(caret_column>=e.first_column+columns)e.first_column=caret_column-columns+1;
            }
        }
        if(multiline)e.first_column=0;
        e.first_line=std::clamp(e.first_line,0,std::max(0,static_cast<int>(wrapped.size())-rows));
        const int lo=std::min(e.selection.anchor,e.selection.end),hi=std::max(e.selection.anchor,e.selection.end);
        for(int n=e.first_line;n<std::min(static_cast<int>(wrapped.size()),e.first_line+rows);++n) {
            const auto& line=wrapped[n];const int y=r.y+(n-e.first_line)*lh();
            const int first=glyph_offset(line.text,e.first_column);const auto visible=std::string_view(line.text).substr(first);
            if(hi>lo&&line.end>lo&&line.begin<hi) {
                const int a=glyphs(std::string_view(e.text).substr(line.begin,std::max(lo,line.begin)-line.begin));
                const int b=glyphs(std::string_view(e.text).substr(line.begin,std::min(hi,line.end)-line.begin));
                fill({r.x+(a-e.first_column)*cw(),y,(b-a)*cw(),lh()},true);
            }
            text({r.x,y,r.w,lh()},visible,color,enabled);
        }
        if(focused&&caret_line>=e.first_line&&caret_line<e.first_line+rows) {
            auto caret=ui::Rect{r.x+(caret_column-e.first_column)*cw(),r.y+(caret_line-e.first_line)*lh(),1,lh()};
            if(paint_clip)caret=intersection(caret,*paint_clip);
            if(caret.w>0&&caret.h>0)scene.caret=caret;
        }
        paint_clip=old_clip;
    }
    int append(const ui::Control& control,const ui::ControlLayout& geometry,std::vector<const ui::Control*> menu={},bool enabled=true) {
        auto presentation=resolve_binding(control,app.control(control),geometry,menu.empty()?std::nullopt:std::optional{app.menu(menu)});
        if(!presentation.visible)return -1;
        if(paint_clip) {const auto visible=intersection(geometry.frame,*paint_clip);if(visible.w<=0||visible.h<=0)return -1;}
        items.push_back({control,geometry,std::move(menu),enabled&&presentation.enabled,presentation.control.label,paint_clip,{}});
        return static_cast<int>(items.size())-1;
    }
    ui::ControlLayout screen_layout(ui::ControlLayout geometry) const {
        for(auto r:{&geometry.frame,&geometry.widget,&geometry.label,&geometry.suggestions,&geometry.caption})*r=screen(*r);
        return geometry;
    }
    void declarations(std::span<const ui::Control> controls,bool enabled,bool overlay) {
        for(const auto& group:ui::control_groups(controls)) {
            const auto& control=*group.control;
            if(control.document_only||(!overlay&&!control.persistent&&control.page!=app.page()))continue;
            auto geometry=app.control_layout(control,width(),height(),controls);
            append(control,screen_layout(geometry),group.menu_items,enabled);
        }
    }
    void document(const ui::DocumentPresentation::Node& presented,const ui::DocumentBox& box,int parent_x,int parent_y,bool interaction_enabled) {
        const auto& node=*presented.source;
        const bool enabled=interaction_enabled&&presented.enabled;
        const ui::Rect outer{parent_x+box.bounds.x,parent_y+box.bounds.y,box.bounds.width,box.bounds.height};
        const ui::Rect inner{outer.x+box.content.x,outer.y+box.content.y,box.content.width,box.content.height};
        const auto before=paint_clip;paint_clip=before?intersection(outer,*before):outer;
        if(node.fill!=ui::DocumentFill::none||node.border)fill(outer,node.fill==ui::DocumentFill::parity,false,node.border);
        switch(node.kind) {
        case ui::DocumentKind::column:case ui::DocumentKind::row:
            for(std::size_t n=0;n<node.children.size()&&n<box.children.size();++n)document(presented.children[n],box.children[n],outer.x,outer.y,interaction_enabled);
            break;
        case ui::DocumentKind::text:text(inner,node.text,tone(node.tone),enabled);break;
        case ui::DocumentKind::bitmap:primitive(Primitive::Kind::bitmap,inner,{},Tone::normal,false,false,enabled,false,node.plot);break;
        case ui::DocumentKind::action: {
            ui::Control c{};c.kind=ui::Kind::action;c.page=app.page();c.command=node.command;c.instance=presented.action?presented.action->instance:node.instance;c.scope=ui::ScreenScope::shared;
            auto geometry=ui::control_content_layout(c,app.control(c).state,inner);const int n=append(c,geometry,{},enabled);
            if(n>=0){items[n].label=node.text;items[n].document_action=presented.action;}
            break;
        }
        case ui::DocumentKind::control:
            if(node.control&&ui::document_control_supported(*node.control))append(*node.control,ui::document_control_layout(*node.control,app.control(*node.control).state,inner),{},enabled);
            break;
        }
        paint_clip=before;
    }
    void render_item(int index) {
        auto& item=items[index];const auto& c=item.control;const auto view=app.control(c);const auto& state=view.state;const auto& geometry=item.geometry;
        const auto plot=c.kind==ui::Kind::bitmap?std::optional{app.bitmap(c,static_cast<unsigned>(std::max(1,geometry.widget.w)))}:std::nullopt;
        const auto& label=plot&&!plot->title.empty()?plot->title:item.label;
        const bool focused=focus&&*focus==identity(item);paint_clip=item.clip;
        if(c.kind!=ui::Kind::label&&item.enabled)hit(Hit::Kind::item,geometry.widget,index);
        if(geometry.has_label&&!label.empty()&&item.menu.empty()) {
            if(c.kind==ui::Kind::label)text(geometry.label,label,tone(state.text_tone),item.enabled);
            else line_text(geometry.label,label,Tone::normal,item.enabled);
        }
        if(!item.menu.empty()) {dropdown(geometry.widget,item.label,focused,item.enabled,Tone::normal,true);paint_clip.reset();return;}
        switch(c.kind) {
        case ui::Kind::label:if(!geometry.has_label)text(geometry.widget,item.label,tone(state.text_tone),item.enabled);break;
        case ui::Kind::action:fill(geometry.widget,false,focused,true,item.enabled);line_text(editor_content(geometry.widget),item.label,Tone::normal,item.enabled,true);break;
        case ui::Kind::toggle: {
            const auto chrome=ui::checkbox_layout(geometry.widget.w,geometry.widget.h);
            const ui::Rect box{geometry.widget.x+chrome.box.x,geometry.widget.y+chrome.box.y,chrome.box.w,chrome.box.h};
            fill(box,state.checked,focused,true,item.enabled,Fill::canvas);
            if(state.checked)icon({box.x+4,box.y+4,std::max(0,box.w-8),std::max(0,box.h-8)},Icon::check,item.enabled);
            line_text({geometry.widget.x+chrome.label.x,geometry.widget.y+chrome.label.y,chrome.label.w,chrome.label.h},item.label,Tone::normal,item.enabled);break;
        }
        case ui::Kind::choice: {
            std::string value=state.display_text;
            for(const auto& option:state.options)if(option.id==state.selected&&state.display_text.empty())value=option.label;
            dropdown(geometry.widget,value.empty()?ui::choice_placeholder:value,focused,item.enabled,field_tone(state.text_tone));break;
        }
        case ui::Kind::text: {
            render_editor(editor(item),geometry.widget,focused,item.enabled,c.multiline,field_tone(state.text_tone));
            if(state.text.empty()&&c.empty_text[0]) {
                if(c.multiline)text(editor_content(geometry.widget),c.empty_text,Tone::muted,item.enabled);
                else line_text(editor_content(geometry.widget,false),c.empty_text,Tone::muted,item.enabled);
            }
            if(geometry.has_suggestions) {
                const auto r=geometry.suggestions;fill(r,false,focused,true,item.enabled);
                const int width=std::min(10,std::max(0,r.w-6)),height=std::min(6,std::max(0,r.h-8));
                icon({r.x+(r.w-width)/2,r.y+(r.h-height)/2,width,height},Icon::chevron_down,item.enabled);
                if(item.enabled)hit(Hit::Kind::suggestion,r,index);
            }break;
        }
        case ui::Kind::list: {
            const auto rect=geometry.widget;fill(rect,false,focused,true,item.enabled,Fill::canvas);paint_clip=item.clip?intersection(rect,*item.clip):rect;
            auto& scrolling=list_scroll[identity(item)];const int row_height=std::max(1,c.list_row_height);
            const auto maximum=std::max(0,static_cast<int>(state.records.size())*row_height-rect.h);
            const auto offset=static_cast<int>(scrolling.target(maximum,c.follow_tail));scrolling.capture(offset,maximum);
            for(int n=offset/row_height;n<static_cast<int>(state.records.size())&&n*row_height-offset<rect.h;++n) {
                const auto& row=state.records[n];const int y=rect.y+n*row_height-offset;
                if(row.id==state.selected)fill({rect.x,y,rect.w,row_height},true);
                for(const auto& cell:row.cells) {auto r=ui::record_cell_rect(cell,rect.w);r.x+=rect.x;r.y+=y;text(r,cell.text,tone(cell.tone),item.enabled&&row.enabled);}
            }
            break;
        }
        case ui::Kind::bitmap: {
            if(geometry.border)fill(geometry.frame,false,focused,true);
            primitive(Primitive::Kind::bitmap,geometry.widget,{},Tone::normal,focused,false,item.enabled,false,plot->source);
            if(geometry.has_caption) {
                if(geometry.caption_overlay)text(geometry.caption,plot->caption,tone(plot->caption_tone),item.enabled);
                else line_text(geometry.caption,plot->caption,tone(plot->caption_tone),item.enabled);
            }
            break;
        }
        }
        paint_clip.reset();
    }
    void render_popup() {
        const int index=find(popup);if(index<0)return;
        const auto& item=items[index];
        if(!item.enabled){popup.reset();options.clear();return;}
        const auto selected_id=popup_selected>=0&&popup_selected<static_cast<int>(options.size())?options[popup_selected].id:std::string{};
        options=item.menu.empty()?app.control(item.control).state.options:app.menu(item.menu).options;
        if(options.empty()){popup.reset();return;}
        const auto selected=std::find_if(options.begin(),options.end(),[&](const auto& option){return option.id==selected_id;});
        popup_selected=selected==options.end()?0:static_cast<int>(selected-options.begin());
        const int row_height=lh()+8,padding=4;
        const int margin_x=std::min(4,viewport.width/2),margin_y=std::min(4,viewport.height/2);
        const int available_width=std::max(1,viewport.width-2*margin_x),available_height=std::max(1,viewport.height-2*margin_y);
        const int check_width=std::max(16,cw()+8);
        int label_width=0;
        for(const auto& option:options)label_width=std::max(label_width,std::min(glyphs(option.label),available_width/cw()+1)*cw());
        const int desired_height=std::min(12,static_cast<int>(options.size()))*row_height+2*padding;
        const auto anchor=item.geometry.frame;
        const int above=std::clamp(anchor.y-margin_y-2,0,available_height);
        const int below=std::clamp(viewport.height-margin_y-anchor.y-anchor.h-2,0,available_height);
        bool upward=item.geometry.popup_upward;
        if((upward?above:below)<desired_height&&(upward?below:above)>(upward?above:below))upward=!upward;
        const int room=std::max(row_height+2*padding,upward?above:below);
        const int rows=std::max(1,std::min(12,(std::min(available_height,room)-2*padding)/row_height));
        const int count=std::min(rows,static_cast<int>(options.size())),h=std::min(available_height,count*row_height+2*padding);
        popup_scroll=std::max(0,std::min(popup_scroll,popup_selected));if(popup_selected>=popup_scroll+count)popup_scroll=popup_selected-count+1;
        const bool scrolling=static_cast<int>(options.size())>count;const int scroll_width=scrolling?8:0;
        const int w=std::min(available_width,std::max(anchor.w,label_width+check_width+2*padding+8+scroll_width));
        const int x=std::clamp(anchor.x,margin_x,std::max(margin_x,viewport.width-w-margin_x));
        const int y=std::clamp(upward?anchor.y-h-2:anchor.y+anchor.h+2,margin_y,std::max(margin_y,viewport.height-h-margin_y));
        paint_clip.reset();fill({x,y,w,h},false,false,true);
        paint_clip=ui::Rect{x+1,y+1,std::max(0,w-2),std::max(0,h-2)};
        const auto& current_id=app.control(item.control).state.selected;
        for(int n=popup_scroll;n<std::min(static_cast<int>(options.size()),popup_scroll+count);++n) {
            const ui::Rect row{x+padding,y+padding+(n-popup_scroll)*row_height,std::max(0,w-2*padding-scroll_width),row_height};
            const bool chosen=!current_id.empty()&&options[n].id==current_id;
            if(chosen||n==popup_selected)fill(row,chosen,false,false,true,Fill::hover);
            if(chosen)icon({row.x+3,row.y+(row_height-10)/2,10,10},Icon::check,options[n].enabled);
            line_text({row.x+check_width,row.y,std::max(0,row.w-check_width-4),row.h},options[n].label,Tone::normal,options[n].enabled);
            hit(Hit::Kind::option,row,n);
        }
        if(scrolling) {
            const ui::Rect rail{x+w-padding-4,y+padding,3,std::max(1,h-2*padding)};
            fill(rail,false,false,false,true,Fill::canvas);
            const int thumb=std::min(rail.h,std::max(8,rail.h*count/static_cast<int>(options.size())));
            const int offset=(rail.h-thumb)*popup_scroll/std::max(1,static_cast<int>(options.size())-count);
            fill({rail.x,rail.y+offset,rail.w,thumb},true);
        }
        paint_clip.reset();scene.caret.reset();
    }
    void render_dialog() {
        if(!dialog)return;
        paint_clip.reset();const int w=std::max(80,std::min(720,viewport.width-32)),h=std::min(220,viewport.height-24);
        const int x=(viewport.width-w)/2,y=(viewport.height-h)/2;fill({x,y,w,h},false,true,true);
        text({x+12,y+10,w-24,2*lh()},dialog->title,Tone::accent);
        prompt_bounds={x+12,y+2*lh()+20,w-24,2*lh()+4};render_editor(prompt,prompt_bounds,prompt_button==0,true,false,Tone::normal);hit(Hit::Kind::prompt,prompt_bounds,0);
        const ui::Rect accept{x+12,y+h-2*lh()-12,110,lh()+6},cancel{x+134,y+h-2*lh()-12,110,lh()+6};
        fill(accept,false,prompt_button==1,true);line_text(editor_content(accept),"Accept",Tone::normal,true,true);
        fill(cancel,false,prompt_button==2,true);line_text(editor_content(cancel),"Cancel",Tone::normal,true,true);
        hit(Hit::Kind::accept,accept,0);hit(Hit::Kind::cancel,cancel,0);
        text({x+12,y+h-lh()-4,w-24,lh()},notice.empty()?"Enter accepts; Escape cancels":notice,notice.empty()?Tone::muted:Tone::negative);
    }
    // Bezel pages project the same opaque declarations as ordinary widgets.
    // No field/command IDs, label matching or modem-setting calculations live here.
    static std::optional<double> numeric(std::string_view value) {
        double number=0;
        const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
        if(parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size()&&std::isfinite(number))return number;
        return std::nullopt;
    }
    void collect_mfd() {
        auto previous=std::move(mfd_entries);mfd_entries={};
        for(const auto& c:ui::console_screen()) {
            if(c.document_only||(!c.persistent&&c.page!=app.page())||c.menu!=ui::Menu::none)continue;
            const auto view=app.control(c);if(!view.visible)continue;
            const bool tune=c.kind==ui::Kind::choice||
                (c.kind==ui::Kind::text&&!c.multiline&&!c.read_only&&view.state.options.size()>1);
            if(!tune&&c.kind!=ui::Kind::action)continue;
            std::string label=view.label;
            if(label.empty()) {
                for(const auto& option:view.state.options)if(option.id==view.state.selected){label=option.label;break;}
                if(label.empty())label=view.state.text;
            }
            if(label.empty())continue;
            mfd_entries[tune?0:1].push_back({c,{},std::move(label),view.enabled});
        }
        // Persistent controls and the action row containing an editor's submit
        // command come first. This is declaration structure, not radio policy.
        auto& actions=mfd_entries[1];
        const auto rank=[&](const MfdEntry& entry) {
            if(entry.control.persistent)return 0;
            for(const auto& c:ui::console_screen())if(c.submit!=ui::Command::none&&app.control(c).visible&&c.submit==entry.control.command)return 1;
            for(const auto& c:ui::console_screen())if(c.submit!=ui::Command::none&&app.control(c).visible)
                for(const auto& action:actions)if(action.control.command==c.submit&&action.control.row==entry.control.row)return 2;
            return 3;
        };
        // Compute priorities before sorting: the comparator must not inspect a
        // vector while the sort is moving its elements.
        std::vector<std::pair<int,MfdEntry>> ranked;
        for(const auto& entry:actions)ranked.emplace_back(rank(entry),entry);
        std::stable_sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.first<b.first;});
        actions.clear();for(auto& entry:ranked)actions.push_back(std::move(entry.second));
        for(const auto& tab:app.tab_layout(width(),height()))if(tab.visible) {
            const auto found=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& page){return page.id==tab.page;});
            if(found!=ui::pages().end())mfd_entries[2].push_back({{},tab.page,found->title,true});
        }
        for(unsigned bank=0;bank<3;++bank) {
            auto& selected=mfd_selected[bank];const auto& entries=mfd_entries[bank];
            if(selected<previous[bank].size()) {
                const auto& old=previous[bank][selected];
                const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto& entry) {
                    return bank==2?entry.page==old.page:ui::document_control_identity(entry.control)==ui::document_control_identity(old.control);
                });
                selected=found==entries.end()?0:static_cast<std::size_t>(found-entries.begin());
            } else selected=0;
        }
        // Bound normalization memory to declarations still available in TUNE.
        for(auto it=mfd_presets.begin();it!=mfd_presets.end();) {
            const auto& entries=mfd_entries[0];
            if(std::none_of(entries.begin(),entries.end(),[&](const auto& e){return ui::document_control_identity(e.control)==it->first;}))it=mfd_presets.erase(it);
            else ++it;
        }
    }
    const MfdEntry* mfd_entry() const {
        const auto& entries=mfd_entries[mfd_bank];const auto at=mfd_selected[mfd_bank];
        return at<entries.size()?&entries[at]:nullptr;
    }
    std::optional<ui::Option> mfd_target(int direction) const {
        const auto* entry=mfd_entry();if(!entry||mfd_bank!=0||!entry->enabled)return {};
        const auto& c=entry->control;const auto& state=app.control(c).state;
        auto values=state.options;
        values.erase(std::remove_if(values.begin(),values.end(),[](const auto& option){return !option.enabled;}),values.end());
        if(values.empty())return {};
        const bool numbers=c.kind==ui::Kind::text&&std::all_of(values.begin(),values.end(),[](const auto& o){return numeric(o.label).has_value();});
        if(numbers)std::stable_sort(values.begin(),values.end(),[](const auto& a,const auto& b){return *numeric(a.label)<*numeric(b.label);});
        std::string current=c.kind==ui::Kind::choice?state.selected:state.text;
        const auto saved=mfd_presets.find(ui::document_control_identity(c));
        if(c.kind==ui::Kind::text&&saved!=mfd_presets.end()&&saved->second.second==state.text)current=saved->second.first;
        const auto found=std::find_if(values.begin(),values.end(),[&](const auto& o){return o.id==current||(c.kind==ui::Kind::text&&o.label==current);});
        if(found!=values.end()) {
            const auto at=found-values.begin()+direction;
            if(at>=0&&at<static_cast<std::ptrdiff_t>(values.size()))return values[static_cast<std::size_t>(at)];
            return {};
        }
        // A keyboard-edited number may lie between presets. Choose the next
        // greater/lower declared value; never invent an application value.
        if(numbers)if(const auto value=numeric(state.text)) {
            if(direction>0) {for(const auto& option:values)if(*numeric(option.label)>*value)return option;}
            else {for(auto it=values.rbegin();it!=values.rend();++it)if(*numeric(it->label)<*value)return *it;}
            return {};
        }
        return direction>0?values.front():values.back();
    }
    bool mfd_available() const {
        return mfd&&outer_viewport.width>=480&&outer_viewport.height>=320&&
            !dialog&&!host_wait&&!help&&!popup&&app.overlay_layers().enable_background;
    }
    bool mfd_back() const {
        const auto overlay=app.overlay();
        return dialog||host_wait||help||popup||
            ui::overlay_key_action(overlay.get(),{ui::Key::escape,false,false,false}).command!=ui::Command::none;
    }
    void mfd_move(int direction) {
        const auto count=mfd_entries[mfd_bank].size();if(!count)return;
        auto& index=mfd_selected[mfd_bank];
        index=static_cast<std::size_t>((static_cast<int>(index)+direction+static_cast<int>(count))%static_cast<int>(count));
    }
    void mfd_press(unsigned number) {
        if(mfd&&outer_viewport.width>=480&&outer_viewport.height>=320&&number==1&&mfd_back()) {
            if(dialog)finish_dialog(true);
            else if(host_wait) {
                app.complete_service({host_wait->id,true,{},{}});host_wait.reset();host_services.clear();app.set_service_active(false);
            } else if(help)help=false;
            else if(popup)popup.reset();
            else app.overlay_key({ui::Key::escape,false,false,false},false,false);
            dirty=true;return;
        }
        if(!mfd_available()||number<1||number>mfd_buttons)return;
        if(number==1) {
            if(mfd_buttons==3&&mfd_selected[mfd_bank]+1<mfd_entries[mfd_bank].size())++mfd_selected[mfd_bank];
            else {mfd_bank=(mfd_bank+1)%3;if(mfd_buttons==3)mfd_selected[mfd_bank]=0;}
        } else if(mfd_buttons==5&&(number==2||number==3))mfd_move(number==2?-1:1);
        else if(const auto* entry=mfd_entry();entry&&entry->enabled) {
            const bool next=number==mfd_buttons;
            if(mfd_bank==0) {
                if(const auto option=mfd_target(next?1:-1)) {
                    const auto control=entry->control;
                    if(control.kind==ui::Kind::choice)app.select(control,option->id);
                    else {app.preset(control,option->id);mfd_presets[ui::document_control_identity(control)]={option->id,app.control(control).state.text};}
                }
            } else if(mfd_bank==2) {
                if(next) {app.navigate(*entry->page);document_scroll=0;}
                else if(mfd_buttons==3)mfd_move(1);
            } else if(next)app.activate(entry->control);
        }
        dirty=true;
    }
    void compose_mfd() {
        mfd_keys.clear();if(!mfd)return;
        // Ordinary widgets retain local coordinates, including popup placement,
        // pan, modal dialogs and caret. Only composition sees the outer display.
        for(auto& p:scene.primitives) {
            p.bounds.x+=interior.x;p.bounds.y+=interior.y;
            if(p.clip){p.clip->x+=interior.x;p.clip->y+=interior.y;p.clip=intersection(*p.clip,interior);}
            else p.clip=interior;
        }
        if(scene.caret){scene.caret->x+=interior.x;scene.caret->y+=interior.y;scene.caret=intersection(*scene.caret,interior);}
        scene.width=outer_viewport.width;scene.height=outer_viewport.height;paint_clip.reset();
        const int w=scene.width,h=scene.height,x=interior.x,y=interior.y;
        fill({0,0,w,y});fill({0,h-y,w,y});fill({0,y,x,h-2*y});fill({w-x,y,x,h-2*y});
        if(w<480||h<320) {line_text({0,0,w,std::min(y,lh()+4)},"Enlarge MFD (480 x 320)",Tone::caution,true,true);return;}
        collect_mfd();const bool enabled=mfd_available(),back=mfd_back();const auto* entry=mfd_entry();
        const char* banks[]={"TUNE","ACTIONS","VIEWS"};
        const int button=32,pad=8,label_w=std::max(1,x-button-3*pad);
        const auto key=[&](unsigned number,ui::Rect box,ui::Rect label,std::string caption,bool active,bool centered=false) {
            const bool available=active&&(enabled||(number==1&&back));
            fill(box,false,false,true,available,Fill::canvas);
            line_text(box,std::to_string(number),Tone::normal,available,true);
            if(centered)line_text(label,caption,Tone::accent,available,true);
            else text(label,caption,Tone::accent,available);
            mfd_keys.push_back({box,number,available});
        };
        key(1,{w/2-button/2,pad,button,button},{w/2-140,pad+button+4,280,2*lh()},
            back?(dialog||host_wait||help||popup?"BACK / CANCEL":"ESCAPE"):
                std::string(mfd_buttons==3?"NEXT FUNCTION | ":"PAGE | ")+banks[mfd_bank],true,true);
        if(mfd_buttons==5) {
            key(2,{pad,h/2-button/2,button,button},{pad*2+button,h/2-lh(),label_w,2*lh()},"PREV\nITEM",entry);
            key(3,{w-pad-button,h/2-button/2,button,button},{w-x+pad,h/2-lh(),label_w,2*lh()},"NEXT\nITEM",entry);
        }
        const int bottom=h-y;
        std::string title=entry?entry->label:"No controls on this page";
        if(entry&&mfd_bank==0) {
            const auto& state=app.control(entry->control).state;std::string value=state.text;
            if(entry->control.kind==ui::Kind::choice)for(const auto& option:state.options)if(option.id==state.selected){value=option.label;break;}
            if(value!=title&&!value.empty())title+=" | "+value;
        }
        line_text({x,bottom+2,w-2*x,lh()},std::string(banks[mfd_bank])+" "+
            std::to_string(mfd_selected[mfd_bank]+(entry?1:0))+"/"+std::to_string(mfd_entries[mfd_bank].size())+" | "+title,Tone::data,true,true);
        const int left=w/3,right=w*2/3;const int label_width=std::max(1,w/3-12);
        const auto down=[&](unsigned number,int center,std::string caption,bool active) {
            key(number,{center-button/2,h-pad-button,button,button},
                {center-label_width/2,bottom+lh()+4,label_width,lh()},std::move(caption),active,true);
        };
        if(mfd_bank==0) {
            const auto previous=mfd_target(-1),next=mfd_target(1);
            down(mfd_buttons-1,left,previous?"< "+previous->label:"< LIMIT",previous.has_value());
            down(mfd_buttons,right,next?next->label+" >":"LIMIT >",next.has_value());
        } else {
            if(mfd_bank==2&&mfd_buttons==3)down(2,left,"NEXT VIEW",entry);
            down(mfd_buttons,right,mfd_bank==2?"OPEN":"EXECUTE",entry&&entry->enabled);
        }
    }
    void rebuild() {
        dirty=false;scene={viewport.width,viewport.height,{},{}};items.clear();hits.clear();pages.clear();paint_clip.reset();document_height=0;
        pan_x=std::clamp(pan_x,0,std::max(0,width()-viewport.width));pan_y=std::clamp(pan_y,0,std::max(0,height()-viewport.height+22));
        const auto overlay=app.overlay();const auto generation=overlay?overlay->generation:0;
        if(generation!=overlay_generation) {
            if(generation) {
                if(!overlay_generation) {
                    saved_focus=focus;saved_selection.reset();
                    saved_focus_tab=focus_tab;saved_pan_x=pan_x;saved_pan_y=pan_y;
                    if(focus)if(const auto found=editors.find(*focus);found!=editors.end()) {
                        saved_selection=found->second.selection;
                        saved_history_revision=found->second.history_revision;saved_cursor_revision=found->second.cursor_revision;
                    }
                }
                restore_overlay_focus=overlay->policy.restore_focus;focus.reset();focus_tab=-1;
            } else {
                focus=restore_overlay_focus?saved_focus:std::nullopt;
                focus_tab=restore_overlay_focus?saved_focus_tab:-1;
                if(restore_overlay_focus){pan_x=saved_pan_x;pan_y=saved_pan_y;}
                restore_selection=focus.has_value()&&saved_selection.has_value();
                saved_focus.reset();
                if(!restore_selection)saved_selection.reset();
            }
            overlay_generation=generation;popup.reset();drag.reset();
        }
        const auto layers=app.overlay_layers(dialog.has_value()||host_wait.has_value());
        if(layers.show_background) {
            for(const auto& tab:app.tab_layout(width(),height()))if(tab.visible) {
                const auto p=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& page){return page.id==tab.page;});
                if(p==ui::pages().end())continue;
                const auto r=screen(tab.frame);const int n=static_cast<int>(pages.size());pages.push_back(tab.page);
                fill(r,app.page()==tab.page,focus_tab==n,true,layers.enable_background);line_text(editor_content(r),p->name,Tone::normal,layers.enable_background,true);
                if(layers.enable_background)hit(Hit::Kind::tab,r,n);
            }
            declarations(ui::console_screen(),layers.enable_background,false);
            const auto p=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& page){return page.id==app.page();});
            if(p!=ui::pages().end()&&p->document) {
                const auto bounds=screen(app.page_bounds(width(),height()));const int content=ui::document_content_width(bounds.w);
                const auto doc=app.document(app.page(),content);
                if(doc) {
                    ui::DocumentPresentation presentation;presentation.reset(doc);
                    const auto layout=ui::layout_document(*doc,content,[&](const auto& node,int w){return static_cast<int>(lines(node.text,std::max(1,w/cw())).size())*lh();});
                    document_height=layout.height+ui::document_top_padding+ui::document_bottom_padding;
                    document_scroll=std::clamp(document_scroll,0,std::max(0,document_height-bounds.h));
                    paint_clip=bounds;document(*presentation.root(),layout.root,bounds.x+ui::document_side_padding,bounds.y+ui::document_top_padding-document_scroll,layers.enable_background);paint_clip.reset();
                }
            }
        }
        if(restore_selection) {
            const int selected=find(focus);
            if(selected>=0&&items[selected].control.kind==ui::Kind::text) {
                auto& restored=editor(items[selected]);
                // Save only selection metadata, never old text. Shared policy
                // revocation/cursor requests take precedence over restored carets.
                if(restored.history_revision==saved_history_revision&&restored.cursor_revision==saved_cursor_revision)
                    restored.selection=saved_selection->clamped(restored.text);
            }
            restore_selection=false;saved_selection.reset();
        }
        const int background_count=static_cast<int>(items.size());
        for(int n=0;n<background_count;++n)render_item(n);
        if(overlay&&layers.show_overlay) {
            paint_clip.reset();if(overlay->policy.hide_background)fill({0,0,viewport.width,viewport.height});
            declarations(overlay->controls,layers.enable_overlay,true);
            for(int n=background_count;n<static_cast<int>(items.size());++n)render_item(n);
        }
        paint_clip.reset();fill({0,viewport.height-22,viewport.width,22});
        std::string footer=notice.empty()?"Tab focus | Alt+Down presets | F1 help | Ctrl+Q quit":notice;
        const int selected=find(focus);if(notice.empty()&&selected>=0&&items[selected].control.help[0])footer=items[selected].control.help;
        line_text({4,viewport.height-20,viewport.width-8,20},footer,notice.empty()?Tone::muted:Tone::negative);
        render_popup();
        if(help) {
            const ui::Rect r{24,24,std::max(1,viewport.width-48),std::max(1,viewport.height-48)};fill(r,false,true,true);
            text({r.x+12,r.y+12,r.w-24,r.h-24},"Framebuffer keyboard and mouse\n\nTab / Shift+Tab moves focus. Ctrl+Left/Right changes page. Arrows, Home/End, Ctrl+Home/End edit text; Shift extends selection. Ctrl+A selects all. Enter submits according to the shared send-key setting. Alt+Down opens editable presets; Escape leaves the draft intact. Mouse clicks and drags select editor text. Double-click activates a record. Mouse wheel scrolls lists and documents; Shift+wheel pans small viewports horizontally. Page Up/Down scrolls a document or small viewport. File dialogs accept a typed path. F1 or Escape closes help. Ctrl+Q closes the application.",Tone::normal);scene.caret.reset();
        }
        render_dialog();
        std::set<Identity> current;for(const auto& item:items)current.insert(identity(item));
        for(auto it=editors.begin();it!=editors.end();)if(!current.contains(it->first))it=editors.erase(it);else ++it;
        if(focus&&find(focus)<0)focus.reset();
        if(popup&&find(popup)<0)popup.reset();
        presented_revision=app.revision();
        compose_mfd();
    }
    void reveal(int index) {
        if(index<0||index>=static_cast<int>(items.size()))return;
        const auto r=items[index].geometry.widget;
        if(r.x<0)pan_x+=r.x;
        else if(r.x+r.w>viewport.width)pan_x+=r.x+r.w-viewport.width;
        if(r.y<0)pan_y+=r.y;
        else if(r.y+r.h>viewport.height-22)pan_y+=r.y+r.h-viewport.height+22;
        dirty=true;
    }
    void move_focus(int direction) {
        std::vector<int> order;
        if(app.overlay_layers().enable_background)for(int n=0;n<static_cast<int>(pages.size());++n)order.push_back(-n-1);
        for(int n=0;n<static_cast<int>(items.size());++n)if(items[n].enabled&&items[n].control.kind!=ui::Kind::label)order.push_back(n);
        if(order.empty())return;
        const int selected=find(focus),current=focus_tab>=0?-focus_tab-1:selected;
        const auto it=focus_tab<0&&selected<0?order.end():std::find(order.begin(),order.end(),current);
        int at=it==order.end()?(direction>0?-1:0):static_cast<int>(it-order.begin());
        at=(at+direction+static_cast<int>(order.size()))%static_cast<int>(order.size());const int target=order[at];
        if(target<0){focus.reset();focus_tab=-target-1;}else{focus_tab=-1;focus=identity(items[target]);reveal(target);}
        popup.reset();drag.reset();dirty=true;
    }
    void open_popup(int index) {
        if(index<0||index>=static_cast<int>(items.size())||!items[index].enabled)return;
        const auto& item=items[index];options=item.menu.empty()?app.control(item.control).state.options:app.menu(item.menu).options;
        if(options.empty())return;
        popup=identity(item);popup_selected=popup_scroll=0;
        const auto& selected=app.control(item.control).state.selected;
        for(int n=0;n<static_cast<int>(options.size());++n)if(options[n].id==selected)popup_selected=n;
        dirty=true;
    }
    void choose_popup() {
        const int index=find(popup);
        if(index<0||!items[index].enabled||popup_selected<0||popup_selected>=static_cast<int>(options.size())||!options[popup_selected].enabled)return;
        const auto item=items[index];const auto id=options[popup_selected].id;popup.reset();options.clear();
        if(!item.menu.empty())app.select_menu(item.menu,id);
        else if(item.control.kind==ui::Kind::text)app.preset(item.control,id);
        else app.select(item.control,id);
        dirty=true;
    }
    void activate(int index,bool twice=false) {
        if(index<0||index>=static_cast<int>(items.size())||!items[index].enabled)return;
        const auto item=items[index];const auto& c=item.control;
        if(!item.menu.empty()){open_popup(index);return;}
        switch(c.kind) {
        case ui::Kind::action:app.activate(c);break;
        case ui::Kind::toggle:app.toggle(c,!app.control(c).state.checked);break;
        case ui::Kind::choice:open_popup(index);break;
        case ui::Kind::bitmap:app.gesture(c,twice&&c.double_click!=ui::Command::none?c.double_click:c.click);break;
        default:break;
        }
        dirty=true;
    }
    void replace(Editor& e,std::string_view value,bool multiline,std::size_t limit,const ui::Control* control) {
        const auto result=ui::text_edit(e.text,e.selection,value,multiline,limit);
        if(!result.error.empty()){notice=result.error;dirty=true;return;}
        if(result.changed) {e.text=result.text;e.selection={result.cursor,result.cursor,result.cursor};if(control)app.edit(*control,result.text);}
        dirty=true;
    }
    void edit_key(Editor& e,const Event& event,int columns,bool multiline,std::size_t limit,const ui::Control* control,bool read_only=false) {
        if(event.type==Event::Type::text) {if(!read_only)replace(e,event.text,multiline,limit,control);return;}
        if(event.ctrl&&(event.text=="a"||event.text=="A")){e.selection={static_cast<int>(e.text.size()),0,static_cast<int>(e.text.size())};dirty=true;return;}
        int target=e.selection.cursor;bool move=false;
        switch(event.key) {
        case Key::left:target=previous_character(e.text,target);move=true;break;
        case Key::right:target=next_character(e.text,target);move=true;break;
        case Key::home: {
            const auto found=target?e.text.rfind('\n',static_cast<std::size_t>(target-1)):std::string::npos;
            target=event.ctrl?0:(found==std::string::npos?0:static_cast<int>(found+1));move=true;break;
        }
        case Key::end: {
            const auto found=e.text.find('\n',static_cast<std::size_t>(target));target=event.ctrl?static_cast<int>(e.text.size()):(found==std::string::npos?static_cast<int>(e.text.size()):static_cast<int>(found));move=true;break;
        }
        case Key::up:case Key::down: {
            const auto wrapped=lines(e.text,multiline?columns:std::max(1,static_cast<int>(e.text.size())+1));int line=0;
            for(int n=0;n<static_cast<int>(wrapped.size());++n)if(target>=wrapped[n].begin)line=n;
            const int column=glyphs(std::string_view(e.text).substr(wrapped[line].begin,target-wrapped[line].begin));
            line=std::clamp(line+(event.key==Key::up?-1:1),0,static_cast<int>(wrapped.size())-1);
            target=wrapped[line].begin+glyph_offset(wrapped[line].text,column);move=true;break;
        }
        case Key::backspace:case Key::del:
            if(read_only)break;
            if(e.selection.anchor==e.selection.end){e.selection.anchor=e.selection.cursor;e.selection.end=event.key==Key::backspace?previous_character(e.text,e.selection.cursor):next_character(e.text,e.selection.cursor);}
            replace(e,{},multiline,limit,control);break;
        case Key::enter:
            if(!read_only) {
                if(control&&app.submit(*control,event.ctrl,event.shift))dirty=true;
                else if(multiline&&!event.ctrl)replace(e,"\n",true,limit,control);
            }break;
        default:break; // Text, including Space, comes only from the text event.
        }
        if(move){if(!event.shift)e.selection.anchor=target;e.selection.cursor=target;e.selection.end=target;dirty=true;}
    }
    void editor_pointer(Editor& e,ui::Rect outer,const Event& event,bool multiline,bool extend) {
        const auto r=editor_content(outer,multiline);const auto wrapped=editor_lines(e,r,multiline);
        const int row=std::clamp((event.y-r.y)/lh()+e.first_line,0,static_cast<int>(wrapped.size())-1);
        const int column=std::max(0,(event.x-r.x)/cw()+e.first_column);
        const int target=wrapped[row].begin+glyph_offset(wrapped[row].text,column);
        if(!extend)e.selection.anchor=target;
        e.selection.cursor=e.selection.end=target;dirty=true;
    }
    void record_event(int index,const Event& event) {
        auto& item=items[index];const auto& state=app.control(item.control).state;auto record_identity=identity(item);
        auto [entry,inserted]=list_events.try_emplace(record_identity,item.control.activate_on_select);(void)inserted;
        auto& handler=entry->second;handler.configure(item.control.activate_on_select);handler.apply(state);
        ui::RecordInteraction action;
        if(event.type==Event::Type::pointer) {
            const auto& bounds=item.geometry.widget;const int row_height=std::max(1,item.control.list_row_height);
            const int offset=static_cast<int>(list_scroll[record_identity].position());const int n=(event.y-bounds.y+offset)/row_height;
            if(n>=0&&n<static_cast<int>(state.records.size()))action=handler.pointer(state.records[n].id,static_cast<float>(event.x),static_cast<float>(event.y));
        } else {
            std::optional<ui::RecordKey> key;
            if(event.key==Key::up)key=ui::RecordKey::up;else if(event.key==Key::down)key=ui::RecordKey::down;
            else if(event.key==Key::enter)key=ui::RecordKey::enter;else if(event.key==Key::space)key=ui::RecordKey::space;
            if(key)action=handler.key(state.selected,*key);
        }
        action.dispatch([&](const auto& id){app.select(item.control,id);},[&](const auto& id){app.activate_record(item.control,id);});
        if(action) {
            const int row=std::max(1,item.control.list_row_height);const auto maximum=std::max(0,static_cast<int>(state.records.size())*row-item.geometry.widget.h);
            auto& scroll=list_scroll[record_identity];scroll.capture(ui::RecordScroll::reveal(scroll.position(),static_cast<double>(action.index)*row,row,item.geometry.widget.h,maximum),maximum);
            dirty=true;
        }
    }
    void initialize_prompt() {
        prompt={};prompt.text=dialog->value;const int end=static_cast<int>(prompt.text.size());prompt.selection={end,0,end};prompt_button=0;popup.reset();
    }
    void finish_dialog(bool cancelled) {
        if(!dialog)return;
        if(!cancelled) {const auto error=ui::service_input_error(*dialog,prompt.text);if(!error.empty()){notice=error;dirty=true;return;}}
        app.complete_service({dialog->id,cancelled,prompt.text,{}});dialog.reset();prompt={};notice.clear();app.set_service_active(false);dirty=true;
    }
    void services() {
        for(auto& request:app.take_services())requests.push_back(std::move(request));
        if(dialog&&((dialog->valid&&!*dialog->valid)||app.closing()))finish_dialog(true);
        if(host_wait&&((host_wait->valid&&!*host_wait->valid)||app.closing())) {
            app.complete_service({host_wait->id,true,{},{}});host_wait.reset();host_services.clear();app.set_service_active(false);dirty=true;
        }
        if(app.closing()){requests.clear();return;}
        if(dialog||host_wait||!app.overlay_layers().present_services)return;
        while(!requests.empty()) {
            auto request=std::move(requests.front());requests.pop_front();
            if(request.valid&&!*request.valid){app.complete_service({request.id,true,{},{}});continue;}
            app.set_service_active(true);
            if(request.kind==ui::ServiceKind::clipboard||request.kind==ui::ServiceKind::open_folder){host_wait=request;host_services.push_back(std::move(request));}
            else{dialog=std::move(request);initialize_prompt();}
            dirty=true;break;
        }
    }
    void input(Event event) {
        app.tick();services();rebuild();
        if(event.type==Event::Type::input_rejected){notice=event.text;dirty=true;return;}
        if(event.type==Event::Type::pointer_up){drag.reset();return;}
        if(mfd&&(event.type==Event::Type::pointer||event.type==Event::Type::pointer_move||event.type==Event::Type::wheel)) {
            if(!contains(interior,event.x,event.y)) {
                if(event.type==Event::Type::pointer)for(const auto& key:mfd_keys)
                    if(key.enabled&&contains(key.bounds,event.x,event.y)){mfd_press(key.number);break;}
                return; // Inactive bezel/labels never fall through to the GUI.
            }
            event.x-=interior.x;event.y-=interior.y;
        }
        if(host_wait)return;
        if(dialog) {
            if(event.type==Event::Type::pointer) {
                for(auto h=hits.rbegin();h!=hits.rend();++h)if(contains(h->bounds,event.x,event.y)) {
                    if(h->kind==Hit::Kind::accept){finish_dialog(false);return;}
                    if(h->kind==Hit::Kind::cancel){finish_dialog(true);return;}
                    if(h->kind==Hit::Kind::prompt){prompt_button=0;editor_pointer(prompt,prompt_bounds,event,false,event.shift);return;}
                }return;
            }
            if(event.type==Event::Type::key&&event.key==Key::escape){finish_dialog(true);return;}
            if(event.type==Event::Type::key&&event.key==Key::tab){prompt_button=(prompt_button+(event.shift?2:1))%3;dirty=true;return;}
            if(event.type==Event::Type::key&&event.key==Key::enter){finish_dialog(prompt_button==2);return;}
            if(prompt_button==0)edit_key(prompt,event,std::max(1,prompt_bounds.w/cw()),ui::service_input_policy(*dialog)->multiline,dialog->byte_limit,nullptr);
            return;
        }
        if(help){if(event.type==Event::Type::key&&(event.key==Key::help||event.key==Key::escape)){help=false;dirty=true;}return;}
        if(event.type==Event::Type::key&&event.key==Key::help){help=true;dirty=true;return;}
        if(popup) {
            if(event.type==Event::Type::pointer_move) {
                for(auto h=hits.rbegin();h!=hits.rend();++h)
                    if(h->kind==Hit::Kind::option&&contains(h->bounds,event.x,event.y)&&options[h->index].enabled) {
                        if(popup_selected!=h->index){popup_selected=h->index;dirty=true;}break;
                    }
                return;
            }
            if(event.type==Event::Type::pointer) {
                for(auto h=hits.rbegin();h!=hits.rend();++h)if(h->kind==Hit::Kind::option&&contains(h->bounds,event.x,event.y)){popup_selected=h->index;choose_popup();return;}
                popup.reset();dirty=true;return;
            }
            if(event.type==Event::Type::wheel) {popup_selected=std::clamp(popup_selected-event.wheel,0,std::max(0,static_cast<int>(options.size())-1));dirty=true;return;}
            if(event.type==Event::Type::key) {
                if(event.key==Key::escape){popup.reset();dirty=true;return;}
                if(event.key==Key::enter){choose_popup();return;}
                if(event.key==Key::up||event.key==Key::down){popup_selected=std::clamp(popup_selected+(event.key==Key::up?-1:1),0,std::max(0,static_cast<int>(options.size())-1));dirty=true;return;}
                if(event.key==Key::tab){popup.reset();move_focus(event.shift?-1:1);return;}
            }
            return;
        }
        if(event.type==Event::Type::key&&app.overlay_key({overlay_key(event.key),event.ctrl,event.shift,event.alt},false,false)){dirty=true;return;}
        if(event.type==Event::Type::key&&event.key==Key::tab){move_focus(event.shift?-1:1);return;}
        if(event.type==Event::Type::key&&event.ctrl&&(event.key==Key::left||event.key==Key::right)&&!pages.empty()&&app.overlay_layers().enable_background) {
            auto p=std::find(pages.begin(),pages.end(),app.page());int n=p==pages.end()?0:static_cast<int>(p-pages.begin());n=(n+(event.key==Key::left?-1:1)+static_cast<int>(pages.size()))%static_cast<int>(pages.size());app.navigate(pages[n]);document_scroll=0;dirty=true;return;
        }
        if(event.type==Event::Type::pointer_move) {
            const int n=find(drag);if(n>=0&&items[n].control.kind==ui::Kind::text)editor_pointer(editor(items[n]),items[n].geometry.widget,event,items[n].control.multiline,true);
            return;
        }
        if(event.type==Event::Type::pointer) {
            // A miss must not fall back to the previously focused action.
            for(auto h=hits.rbegin();h!=hits.rend();++h)if(contains(h->bounds,event.x,event.y)) {
                if(h->kind==Hit::Kind::tab){app.navigate(pages[h->index]);focus.reset();focus_tab=h->index;document_scroll=0;dirty=true;return;}
                if(h->kind!=Hit::Kind::item&&h->kind!=Hit::Kind::suggestion)continue;
                const int n=h->index;auto& item=items[n];if(!item.enabled)return;
                focus=identity(item);focus_tab=-1;
                if(h->kind==Hit::Kind::suggestion){open_popup(n);return;}
                if(item.control.kind==ui::Kind::text) {editor_pointer(editor(item),item.geometry.widget,event,item.control.multiline,event.shift);drag=focus;}
                else if(item.control.kind==ui::Kind::list)record_event(n,event);
                else activate(n,event.double_click);
                dirty=true;return;
            }
            return;
        }
        if(event.type==Event::Type::wheel) {
            for(auto h=hits.rbegin();h!=hits.rend();++h)if(h->kind==Hit::Kind::item&&contains(h->bounds,event.x,event.y)) {
                const auto& item=items[h->index];const auto& c=item.control;if(!item.enabled)return;
                if(c.kind==ui::Kind::list) {
                    auto& scroll=list_scroll[identity(item)];const auto& state=app.control(c).state;const int row=std::max(1,c.list_row_height);
                    const int maximum=std::max(0,static_cast<int>(state.records.size())*row-item.geometry.widget.h);scroll.capture(scroll.position()-event.wheel*3.0*row,maximum);dirty=true;return;
                }
                const auto commands=ui::ControlInteractions::wheel(c,event.wheel);
                if(commands.dispatch([&](auto command){app.gesture(c,command);})){dirty=true;return;}
            }
            if(event.shift)pan_x-=event.wheel*36;
            else if(contains(screen(app.page_bounds(width(),height())),event.x,event.y)&&document_height>0)document_scroll=std::max(0,document_scroll-event.wheel*3*lh());
            else pan_y-=event.wheel*3*lh();
            dirty=true;return;
        }
        if(event.type==Event::Type::key&&(event.key==Key::page_up||event.key==Key::page_down)) {
            const int amount=(event.key==Key::page_up?-1:1)*std::max(lh(),viewport.height/2);
            if(document_height>0)document_scroll=std::max(0,document_scroll+amount);else pan_y+=amount;dirty=true;return;
        }
        if(focus_tab>=0&&event.type==Event::Type::key&&(event.key==Key::enter||event.key==Key::space)) {
            if(focus_tab<static_cast<int>(pages.size())){app.navigate(pages[focus_tab]);document_scroll=0;dirty=true;}return;
        }
        const int n=find(focus);if(n<0||!items[n].enabled)return;auto& item=items[n];const auto& c=item.control;
        if(event.type==Event::Type::key&&event.alt&&(event.key==Key::down||event.key==Key::up)){open_popup(n);return;}
        if(c.kind==ui::Kind::text)edit_key(editor(item),event,std::max(1,editor_content(item.geometry.widget).w/cw()),c.multiline,c.byte_limit,&c,c.read_only);
        else if(c.kind==ui::Kind::list&&event.type==Event::Type::key)record_event(n,event);
        else if(event.type==Event::Type::key&&(event.key==Key::enter||event.key==Key::space))activate(n);
        else if(c.kind==ui::Kind::choice&&event.type==Event::Type::key&&(event.key==Key::up||event.key==Key::down))open_popup(n);
    }
};
Session::Session(Application& app,bool mfd,unsigned buttons):impl_(std::make_unique<Impl>(app,mfd,buttons)){}
Session::~Session()=default;
void Session::resize(Viewport viewport) {
    if(viewport.width<1||viewport.height<1||viewport.metrics.cell_width<1||viewport.metrics.line_height<1)throw std::invalid_argument("Invalid framebuffer viewport");
    impl_->resize(viewport);
}
bool Session::tick(){const bool changed=impl_->app.tick();impl_->services();if(changed||impl_->dirty||impl_->app.revision()!=impl_->presented_revision){impl_->rebuild();return true;}return false;}
void Session::input(const Event& event){impl_->input(event);}
void Session::press_mfd_button(unsigned number) {
    impl_->app.tick();impl_->services();impl_->rebuild();impl_->mfd_press(number);
}
const Scene& Session::scene() const{return impl_->scene;}
std::vector<ui::ServiceRequest> Session::take_host_services(){auto result=std::move(impl_->host_services);impl_->host_services.clear();return result;}
void Session::complete_host_service(ui::ServiceResult result) {
    if(!impl_->host_wait||impl_->host_wait->id!=result.id)return;
    if(impl_->host_wait->valid&&!*impl_->host_wait->valid){result.cancelled=true;result.value.clear();result.error.clear();}
    if(!result.error.empty())impl_->notice=result.error;
    impl_->app.complete_service(std::move(result));impl_->host_wait.reset();impl_->app.set_service_active(false);impl_->dirty=true;
}
}

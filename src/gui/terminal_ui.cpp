#include "terminal_ui.hpp"
#include "control_binding.hpp"
#include "cell_layout.hpp"
#include "terminal_record_layout.hpp"
#include "document_presentation.hpp"
#include "service_queue.hpp"
#include "text_policy.hpp"
#include <algorithm>
#include <deque>
#include <map>
#include <set>
#include <string_view>
#include <utility>

namespace datapump::gui::terminal {
namespace {
using Identity=decltype(ui::document_control_identity(std::declval<ui::Control>()));
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
        int first_line=0;
    };
    struct Item {
        ui::Control control;
        ui::Rect bounds; // In logical cells, before page scroll.
        std::vector<const ui::Control*> menu;
        bool enabled=true;
        std::string label;
        std::optional<ui::Rect> clip;
    };
    struct Hit {enum class Kind { item,tab,option,accept,cancel };Kind kind;ui::Rect bounds;int index;};
    Application& app;
    Viewport viewport;
    Scene scene;
    std::vector<Item> items;
    std::vector<Hit> hits;
    std::map<Identity,Editor> editors;
    struct ListPosition {
        int offset=0,horizontal=0,chosen_top=-1,row_height=0,viewport_height=0;
        bool tail=true,focused=false,reveal=false;
        std::string selected;
    };
    std::map<Identity,ListPosition> list_positions;
    std::map<std::tuple<ui::Page,ui::Command,unsigned,std::size_t>,unsigned> document_instances;
    std::optional<Identity> focus,expanded;
    std::optional<Identity> saved_focus,popup_identity;
    struct SavedEditor {ui::TextSelection selection;std::uint64_t cursor_revision=0,history_revision=0;};
    std::optional<SavedEditor> saved_editor,restore_editor;
    int saved_focus_tab=-1,saved_scroll=0;
    bool restore_overlay_focus=true;
    std::uint64_t overlay_generation=0,application_revision=0;
    std::vector<ui::Page> pages;
    std::vector<std::string> page_names;
    int focus_tab=-1,scroll=0,content_height=0,header_height=1;
    int popup=-1,popup_selected=0,popup_scroll=0;
    std::vector<ui::Option> options;
    bool dirty=true,help=false,content_clip=false;
    std::optional<ui::Rect> document_clip;
    std::string notice;
    std::deque<ui::ServiceRequest> dialogs;
    std::deque<ui::ServiceRequest> pending_services;
    std::optional<ui::ServiceRequest> host_wait;
    std::vector<ui::ServiceRequest> host_services;
    Editor prompt;
    int prompt_button=0;
    explicit Impl(Application& application):app(application) {}
    int columns() const {return std::max(1,viewport.width/viewport.metrics.cell_width);}
    int rows() const {return std::max(1,viewport.height/viewport.metrics.line_height);}
    ui::Rect pixels(ui::Rect r) const {
        return {r.x*viewport.metrics.cell_width,r.y*viewport.metrics.line_height,r.w*viewport.metrics.cell_width,r.h*viewport.metrics.line_height};
    }
    int focused() const {
        if(!focus)return -1;
        for(std::size_t i=0;i<items.size();++i)if(ui::document_control_identity(items[i].control)==*focus)return static_cast<int>(i);
        return -1;
    }
    bool focusable(const Item& item) const {return item.enabled&&item.control.kind!=ui::Kind::label;}
    void primitive(Primitive::Kind kind,ui::Rect r,std::string text={},Tone color=Tone::normal,bool focused=false,bool selected=false,bool enabled=true,bool border=false,BitmapSource bitmap={},bool bold=false) {
        if(r.w<=0||r.h<=0||r.x>=columns()||r.y>=rows()||r.y+r.h<=0)return;
        auto clip=content_clip?std::optional<ui::Rect>({0,header_height,columns(),std::max(0,rows()-header_height-1)}):std::nullopt;
        if(document_clip) {
            const auto c=*document_clip;
            const auto bounds=clip.value_or(ui::Rect{0,0,columns(),rows()});
            const auto x=std::max(c.x,bounds.x),y=std::max(c.y,bounds.y);
            clip=ui::Rect{x,y,std::max(0,std::min(c.x+c.w,bounds.x+bounds.w)-x),std::max(0,std::min(c.y+c.h,bounds.y+bounds.h)-y)};
        }
        scene.primitives.push_back({kind,pixels(r),std::move(text),std::move(bitmap),color,focused,selected,enabled,border,
            clip?std::optional<ui::Rect>(pixels(*clip)):std::nullopt});
        scene.primitives.back().bold=bold;
    }
    void fill(ui::Rect r,Tone color=Tone::normal,bool selected=false,bool focused=false,bool border=false) {
        primitive(Primitive::Kind::fill,r,{},color,focused,selected,true,border);
    }
    void text(ui::Rect r,std::string_view value,Tone color=Tone::normal,bool focused=false,bool selected=false,bool enabled=true,bool bold=false) {
        auto wrapped=lines(value,r.w);
        for(int i=0;i<std::min(r.h,static_cast<int>(wrapped.size()));++i)
            primitive(Primitive::Kind::text,{r.x,r.y+i,r.w,1},std::move(wrapped[i].text),color,focused,selected,enabled,false,{},bold);
    }
    Editor& editor(const Item& item) {
        auto& e=editors[ui::document_control_identity(item.control)];const auto& s=app.control(item.control).state;
        if(e.text!=s.text) {e.text=s.text;e.selection=e.selection.clamped(e.text);}
        if(e.cursor_revision!=s.text_cursor_end_revision) {
            e.cursor_revision=s.text_cursor_end_revision;e.selection={static_cast<int>(e.text.size()),static_cast<int>(e.text.size()),static_cast<int>(e.text.size())};
        }
        // There is no native undo buffer: revoked bytes cannot be recovered by
        // an adapter undo command or by a stale whole-buffer editor callback.
        e.history_revision=s.text_history_revision;return e;
    }
    int append_control(const ui::Control& control,ui::Rect bounds,std::vector<const ui::Control*> menu={}) {
        const auto presentation=app.control(control);
        if(!presentation.visible)return 0;
        std::string label=presentation.label;bool enabled=presentation.enabled;
        if(!menu.empty()) {const auto value=app.menu(menu);if(!value.visible)return 0;label=control.menu_label;enabled=value.enabled;}
        items.push_back({control,bounds,std::move(menu),enabled,std::move(label),{}});
        if(control.kind==ui::Kind::text)editor(items.back());
        return bounds.h;
    }
    int document(const ui::DocumentPresentation& presentation,int x,int y,int width,ui::Page page) {
        const auto layout=presentation.layout(width,[](const ui::DocumentNode& node,int available) {
            return static_cast<int>(lines(node.text,available).size());
        },x,y,{8,18,12});
        for(const auto& placed:layout.nodes) {
            if(!placed.allocated)continue;
            const auto& presented=*placed.node;const auto& node=*presented.source;
            const auto& outer=placed.absolute;const auto& inner=placed.content;
            const ui::Rect clip{placed.clip.x,placed.clip.y-scroll,placed.clip.width,placed.clip.height};
            document_clip=clip;
            const ui::Rect content{outer.x+inner.x,outer.y+inner.y-scroll,inner.width,inner.height};
            if(node.border)fill({outer.x,outer.y-scroll,outer.width,outer.height},Tone::muted,false,false,true);
            if(node.kind==ui::DocumentKind::text)text(content,node.text,tone(node.tone),false,false,placed.enabled);
            else if(node.kind==ui::DocumentKind::bitmap)
                primitive(Primitive::Kind::bitmap,content,{},Tone::normal,false,false,placed.enabled,false,node.plot);
            else if(node.kind==ui::DocumentKind::action||node.kind==ui::DocumentKind::control) {
                ui::Control control{};
                if(node.kind==ui::DocumentKind::control) {
                    if(!node.control||!ui::document_control_supported(*node.control))continue;
                    control=*node.control;
                } else {
                    control.kind=ui::Kind::action;control.page=page;control.command=node.command;control.scope=ui::ScreenScope::shared;
                    if(presented.action) {
                        const auto& identity=*presented.action;
                        const auto key=std::tuple(page,identity.command,identity.instance,identity.occurrence);
                        auto [entry,inserted]=document_instances.try_emplace(key,static_cast<unsigned>(document_instances.size()+1));
                        (void)inserted;control.instance=entry->second;
                    }
                }
                const bool has_label=control.kind!=ui::Kind::label&&control.kind!=ui::Kind::action&&control.kind!=ui::Kind::toggle&&control.label[0];
                const int label_lines=has_label?static_cast<int>(lines(app.control(control).label,content.w).size()):0;
                const int top=std::max(content.y+label_lines,clip.y),bottom=std::min(content.y+content.h,clip.y+clip.h);
                const bool usable=content.w>0&&content.h>0&&top<bottom&&std::max(content.x,clip.x)<std::min(content.x+content.w,clip.x+clip.w);
                if(usable&&append_control(control,{content.x,content.y+scroll,content.w,content.h})) {
                    if(node.kind==ui::DocumentKind::action)items.back().label=node.text;
                    items.back().enabled=items.back().enabled&&placed.enabled;
                    items.back().clip=ui::Rect{placed.clip.x,placed.clip.y,placed.clip.width,placed.clip.height};
                }
            }
        }
        document_clip.reset();return layout.height;
    }
    void declarations(std::span<const ui::Control> controls,int& y,bool overlay,const ui::DocumentPresentation* page_document=nullptr) {
        const int logical_width=std::max(ui::min_width,columns()*8);
        // Extra terminal rows reveal more content; they must not recursively
        // enlarge flexible desktop panels and push the footer out of reach.
        const int logical_height=std::max(ui::min_height,std::min(ui::default_height,rows()*18));
        std::vector<ui::ControlGroup> visible;
        std::vector<ui::CellLayoutItem> projected;
        for(const auto& group:ui::control_groups(controls)) {
            const auto& c=*group.control;
            if(c.document_only||(!overlay&&!c.persistent&&c.page!=app.page()))continue;
            const auto state=app.control(c);
            if(group.menu_items.empty()?!state.visible:!app.menu(group.menu_items).visible)continue;
            const auto geometry=app.control_layout(c,logical_width,logical_height,controls);
            auto rect=geometry.frame;
            if(geometry.has_label) {
                rect.h+=std::max(0,rect.y-geometry.label.y);
            }
            rect.x=std::max(0,rect.x-ui::margin);
            if(rect.w<=0||rect.h<=0)continue;
            const int label_width=glyphs(group.menu_items.empty()?state.label:c.menu_label);
            const int minimum_width=std::max(8,std::min(24,label_width+4));
            const int minimum_height=(geometry.has_label?1:0)+(c.kind==ui::Kind::bitmap||c.kind==ui::Kind::list?3:1);
            const auto label=group.menu_items.empty()?state.label:std::string(c.menu_label);
            const auto kind=c.kind;const int base=(rect.h+17)/18;
            const bool above=kind!=ui::Kind::label&&kind!=ui::Kind::action&&kind!=ui::Kind::toggle&&!label.empty();
            visible.push_back(group);projected.push_back({rect,minimum_width,minimum_height,[label,kind,base,above](int width) {
                const int count=static_cast<int>(lines((kind==ui::Kind::action||kind==ui::Kind::toggle?"[ ] ":"")+label,width).size());
                return above?base+count-1:std::max(base,count);
            }});
        }
        if(page_document) {
            auto rect=app.page_bounds(logical_width,logical_height);rect.x=std::max(0,rect.x-ui::margin);
            projected.push_back({rect,std::max(1,columns()-2),1,[page_document](int width) {
                return page_document->layout(width,[](const ui::DocumentNode& node,int available){return static_cast<int>(lines(node.text,available).size());},0,0,{8,18,12}).height;
            }});
        }
        const auto positions=ui::cell_layout(projected,std::max(1,columns()-2),logical_width-2*ui::margin);
        // Geometry determines reading/focus order; declaration order breaks ties.
        std::vector<std::size_t> order(positions.size());std::iota(order.begin(),order.end(),0);
        std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return std::tie(positions[a].y,positions[a].x)<std::tie(positions[b].y,positions[b].x);});
        int bottom=y;
        for(auto index:order) {
            auto rect=positions[index];rect.x+=1;rect.y+=y;
            if(index==visible.size())document(*page_document,rect.x,rect.y,rect.w,app.page());
            else append_control(*visible[index].control,rect,visible[index].menu_items);
            bottom=std::max(bottom,rect.y+rect.h+1);
        }
        y=bottom;
    }
    void render_editor(Editor& e,ui::Rect r,bool focused,bool enabled,Tone color) {
        const auto wrapped=lines(e.text,r.w);int caret_line=0;
        for(int n=0;n<static_cast<int>(wrapped.size());++n)if(e.selection.cursor>=wrapped[n].begin)caret_line=n;
        if(focused) {
            if(caret_line<e.first_line)e.first_line=caret_line;
            if(caret_line>=e.first_line+r.h)e.first_line=caret_line-r.h+1;
        }
        e.first_line=std::clamp(e.first_line,0,std::max(0,static_cast<int>(wrapped.size())-r.h));
        const int lo=std::min(e.selection.anchor,e.selection.end),hi=std::max(e.selection.anchor,e.selection.end);
        fill(r,Tone::muted,false,focused);
        for(int n=e.first_line;n<std::min(static_cast<int>(wrapped.size()),e.first_line+r.h);++n) {
            const auto& line=wrapped[n];const int ypos=r.y+n-e.first_line;
            text({r.x,ypos,r.w,1},line.text,color,focused,false,enabled);
            if(hi>lo&&line.end>lo&&line.begin<hi) {
                const int begin=std::max(lo,line.begin),end=std::min(hi,line.end);
                const int start=glyphs(std::string_view(e.text).substr(line.begin,begin-line.begin));
                text({r.x+start,ypos,r.w-start,1},std::string_view(e.text).substr(begin,end-begin),Tone::inverse,focused,true,enabled);
            }
        }
        if(focused&&caret_line>=e.first_line&&caret_line<e.first_line+r.h) {
            const int column=glyphs(std::string_view(e.text).substr(wrapped[caret_line].begin,e.selection.cursor-wrapped[caret_line].begin));
            auto caret=ui::Rect{r.x+std::min(column,std::max(0,r.w-1)),r.y+caret_line-e.first_line,1,1};
            if(caret.y>=header_height&&caret.y<rows()-1&&(!document_clip||contains(*document_clip,caret.x,caret.y)))scene.caret=pixels(caret);
        }
    }
    void render_item(int index) {
        auto& item=items[index];const auto& c=item.control;const auto view=app.control(c);const auto& state=view.state;
        const bool selected=focus&&*focus==ui::document_control_identity(c);auto r=item.bounds;r.y-=scroll;
        document_clip=item.clip;
        if(document_clip)document_clip->y-=scroll;
        auto hit=r;
        if(document_clip) {
            const auto clip=*document_clip;const int x=std::max(hit.x,clip.x),y=std::max(hit.y,clip.y);
            hit={x,y,std::max(0,std::min(hit.x+hit.w,clip.x+clip.w)-x),std::max(0,std::min(hit.y+hit.h,clip.y+clip.h)-y)};
        }
        hits.push_back({Hit::Kind::item,hit,index});
        if(r.y+r.h<=header_height||r.y>=rows()-1)return;
        std::optional<BitmapPresentation> bitmap;
        if(c.kind==ui::Kind::bitmap)bitmap=app.bitmap(c,static_cast<unsigned>(std::max(1,r.w*viewport.metrics.cell_width)));
        const auto& label_text=bitmap?bitmap->title:item.label;
        const bool label=c.kind!=ui::Kind::label&&c.kind!=ui::Kind::toggle&&c.kind!=ui::Kind::action&&!label_text.empty();
        if(label){const int height=std::min(std::max(0,r.h-1),static_cast<int>(lines(label_text,r.w).size()));
            text({r.x,r.y,r.w,height},label_text,Tone::muted,selected,false,item.enabled);r.y+=height;r.h-=height;}
        if(!item.menu.empty()) {text(r,"[ "+item.label+" v ]",Tone::accent,selected,false,item.enabled);return;}
        switch(c.kind) {
        case ui::Kind::label:text(r,item.label,tone(state.text_tone),false,false,item.enabled);break;
        case ui::Kind::action:text(r,"[ "+item.label+" ]",Tone::accent,selected,false,item.enabled);break;
        case ui::Kind::toggle:text(r,std::string(state.checked?"[x] ":"[ ] ")+item.label,Tone::normal,selected,false,item.enabled);break;
        case ui::Kind::choice: {
            std::string value=state.display_text.empty()?state.selected:state.display_text;
            for(const auto& option:state.options)if(option.id==state.selected&&state.display_text.empty())value=option.label;
            fill(r,Tone::muted,false,selected);text(r,value+" v",tone(state.text_tone),selected,false,item.enabled);break;
        }
        case ui::Kind::text: {
            if(!state.options.empty()&&!c.read_only&&r.w>3) {text({r.x+r.w-3,r.y,3,1},"[v]",Tone::accent,selected);r.w-=3;}
            auto& e=editor(item);render_editor(e,r,selected,item.enabled,tone(state.text_tone));
            if(e.text.empty()&&c.empty_text[0])text(r,c.empty_text,Tone::muted,false,false,item.enabled);
            break;
        }
        case ui::Kind::list: {
            fill(r,Tone::muted,false,selected);
            if(state.records.empty()&&c.empty_text[0])text(r,c.empty_text,Tone::muted,selected,false,item.enabled);
            auto& position=list_positions[ui::document_control_identity(c)];
            const int height=record_height(c.list_row_height);
            const int maximum=std::max(0,static_cast<int>(state.records.size())*height-r.h);
            const int width=record_columns(state.records,r.w);
            position.horizontal=std::clamp(position.horizontal,0,std::max(0,width-r.w));
            int chosen=-1;for(int n=0;n<static_cast<int>(state.records.size());++n)if(state.records[n].id==state.selected)chosen=n;
            if(c.follow_tail&&position.tail&&!selected)position.offset=maximum;
            if(selected&&chosen>=0&&(!position.focused||position.selected!=state.selected||position.reveal||
                position.chosen_top!=chosen*height||position.row_height!=height||position.viewport_height!=r.h)) {
                const int top=chosen*height;
                // Oversized records reveal their beginning once; subsequent
                // page/wheel scrolling must be able to reach the rest.
                if(top<position.offset||height>r.h)position.offset=top;
                else if(top+height>position.offset+r.h)position.offset=top+height-r.h;
                position.tail=position.offset>=maximum;
            }
            position.offset=std::clamp(position.offset,0,maximum);
            if(maximum==0)position.tail=true;
            position.focused=selected;position.selected=state.selected;position.reveal=false;
            position.chosen_top=chosen*height;position.row_height=height;position.viewport_height=r.h;
            const auto previous_clip=document_clip;
            const auto intersect=[](ui::Rect a,ui::Rect b) {
                const int x=std::max(a.x,b.x),y=std::max(a.y,b.y);
                return ui::Rect{x,y,std::max(0,std::min(a.x+a.w,b.x+b.w)-x),std::max(0,std::min(a.y+a.h,b.y+b.h)-y)};
            };
            const auto list_clip=previous_clip?intersect(r,*previous_clip):r;
            for(int n=position.offset/height;n<static_cast<int>(state.records.size())&&n*height-position.offset<r.h;++n) {
                const auto& record=state.records[n];const int y=r.y+n*height-position.offset;
                document_clip=intersect(list_clip,{r.x,y,r.w,height});
                if(n==chosen)fill({r.x,y,r.w,height},Tone::normal,true);
                for(const auto& cell:record.cells) {
                    auto box=record_cell_bounds(cell,width);box.x+=r.x-position.horizontal;box.y+=y;
                    text(box,cell.text,tone(cell.tone),false,n==chosen,item.enabled&&record.enabled,cell.bold);
                }
            }
            document_clip=previous_clip;
            break;
        }
        case ui::Kind::bitmap: {
            const auto& plot=*bitmap;
            primitive(Primitive::Kind::bitmap,{r.x,r.y,r.w,std::max(1,r.h-1)},{},Tone::normal,selected,false,item.enabled,false,plot.source);
            text({r.x,r.y+r.h-1,r.w,1},plot.caption,tone(plot.caption_tone),selected);break;
        }
        }
    }
    ui::Rect list_bounds(const Item& item) const {
        auto r=item.bounds;r.y-=scroll;
        if(!item.label.empty()) {const int label=std::min(std::max(0,r.h-1),static_cast<int>(lines(item.label,r.w).size()));r.y+=label;r.h-=label;}
        return r;
    }
    void scroll_list(const Item& item,int amount) {
        const auto& c=item.control;const auto& records=app.control(c).state.records;
        auto& position=list_positions[ui::document_control_identity(c)];
        const int maximum=std::max(0,static_cast<int>(records.size())*record_height(c.list_row_height)-list_bounds(item).h);
        position.offset=std::clamp(position.offset+amount,0,maximum);
        position.tail=position.offset==maximum;dirty=true;
    }
    void render_popup() {
        if(popup<0||popup>=static_cast<int>(items.size()))return;
        const auto& item=items[popup];const int height=std::min(std::max(1,rows()-header_height-2),std::max(1,static_cast<int>(options.size())));
        popup_scroll=std::max(0,std::min(popup_scroll,popup_selected));if(popup_selected>=popup_scroll+height)popup_scroll=popup_selected-height+1;
        int y=std::clamp(item.bounds.y+item.bounds.h-scroll,header_height,std::max(header_height,rows()-height-1));
        const ui::Rect r{1,y,std::max(1,columns()-2),height};fill(r,Tone::inverse);
        for(int n=popup_scroll;n<std::min(static_cast<int>(options.size()),popup_scroll+height);++n) {
            auto row=ui::Rect{r.x,y+n-popup_scroll,r.w,1};hits.push_back({Hit::Kind::option,row,n});
            text(row,options[n].label,Tone::normal,n==popup_selected,n==popup_selected,options[n].enabled);
        }
        scene.caret.reset();
    }
    void render_dialog() {
        if(dialogs.empty())return;
        const auto& request=dialogs.front();const int w=std::max(1,columns()-4),h=std::min(8,std::max(1,rows()-2));
        const int y=std::max(0,(rows()-h)/2);fill({1,y,w+2,h},Tone::inverse,false,false,true);
        text({2,y,w,2},request.title,Tone::accent);
        render_editor(prompt,{2,y+2,w,2},prompt_button==0,true,Tone::normal);
        ui::Rect accept{2,y+4,std::min(12,w),1},cancel{std::min(columns()-1,16),y+4,std::max(1,std::min(12,w-14)),1};
        text(accept,"[ Accept ]",Tone::accent,prompt_button==1);text(cancel,"[ Cancel ]",Tone::accent,prompt_button==2);
        hits.push_back({Hit::Kind::accept,accept,0});hits.push_back({Hit::Kind::cancel,cancel,0});
        text({2,y+5,w,2},notice.empty()?"Enter accepts; Escape cancels. Tab selects buttons.":notice,notice.empty()?Tone::muted:Tone::negative);
    }
    void rebuild() {
        dirty=false;scene={viewport.width,viewport.height,{},{}};items.clear();hits.clear();pages.clear();page_names.clear();
        const auto overlay=app.overlay();const auto generation=overlay?overlay->generation:0;
        if(generation!=overlay_generation) {
            if(generation) {
                if(!overlay_generation) {
                    saved_focus=focus;saved_focus_tab=focus_tab;saved_scroll=scroll;saved_editor.reset();
                    if(focus)if(const auto found=editors.find(*focus);found!=editors.end())
                        saved_editor=SavedEditor{found->second.selection,found->second.cursor_revision,found->second.history_revision};
                }
                restore_overlay_focus=overlay->policy.restore_focus;focus.reset();focus_tab=-1;scroll=0;
            } else {
                focus=restore_overlay_focus?saved_focus:std::nullopt;
                focus_tab=restore_overlay_focus?saved_focus_tab:-1;scroll=saved_scroll;
                if(restore_overlay_focus)restore_editor=saved_editor;
                saved_focus.reset();saved_focus_tab=-1;saved_editor.reset();
            }
            overlay_generation=generation;popup=-1;popup_identity.reset();expanded.reset();
        }
        const auto layers=app.overlay_layers(!dialogs.empty());
        int x=1;header_height=1;
        for(const auto& tab:app.tab_layout(1200,1000))if(tab.visible) {
            const auto found=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& page){return page.id==tab.page;});
            if(found==ui::pages().end())continue;
            pages.push_back(tab.page);page_names.push_back(found->name);
            const int w=std::min(columns(),glyphs(found->name)+4);
            if(x>1&&x+w>columns()){x=1;++header_height;}x+=w;
        }
        content_clip=true;int y=header_height+1;
        if(layers.show_background) {
            const auto page=std::find_if(ui::pages().begin(),ui::pages().end(),[&](const auto& p){return p.id==app.page();});
            ui::DocumentPresentation presentation;
            if(page!=ui::pages().end()&&page->document)
                presentation.reset(app.document(app.page(),std::max(220,(columns()-2)*8)));
            declarations(ui::console_screen(),y,false,presentation.root()?&presentation:nullptr);
        }
        if(overlay&&layers.show_overlay)declarations(overlay->controls,y,true);
        content_height=y;
        const int clamped_scroll=std::clamp(scroll,0,std::max(0,content_height-rows()+1));
        if(scroll!=clamped_scroll) {
            // Documents were painted during layout using the previous offset.
            // A larger viewport can shorten content and clamp that offset; redo
            // the scene once so document primitives and controls stay aligned.
            scroll=clamped_scroll;rebuild();return;
        }
        if(restore_editor) {
            const int index=focused();
            if(index>=0&&items[index].control.kind==ui::Kind::text) {
                auto& e=editor(items[index]);
                if(e.cursor_revision==restore_editor->cursor_revision&&e.history_revision==restore_editor->history_revision)
                    e.selection=restore_editor->selection.clamped(e.text);
            }
            restore_editor.reset();
        }
        if(popup>=0) {
            const std::string selected=popup_selected>=0&&popup_selected<static_cast<int>(options.size())?options[popup_selected].id:std::string{};
            popup=-1;
            if(popup_identity)for(int index=0;index<static_cast<int>(items.size());++index)
                if(items[index].enabled&&ui::document_control_identity(items[index].control)==*popup_identity){popup=index;break;}
            if(popup>=0) {
                const auto& item=items[popup];options=item.menu.empty()?app.control(item.control).state.options:app.menu(item.menu).options;
                popup_selected=0;
                for(int index=0;index<static_cast<int>(options.size());++index)if(options[index].id==selected)popup_selected=index;
                if(options.empty())popup=-1;
            }
            if(popup<0){options.clear();popup_identity.reset();}
        }
        for(int n=0;n<static_cast<int>(items.size());++n)render_item(n);
        document_clip.reset();
        if(expanded) {
            const auto found=std::find_if(items.begin(),items.end(),[&](const auto& item){return ui::document_control_identity(item.control)==*expanded;});
            if(found!=items.end()) {
                fill({0,header_height,columns(),std::max(1,rows()-header_height-1)});
                const auto plot=app.bitmap(found->control,static_cast<unsigned>(std::max(1,viewport.width)));
                text({0,header_height,columns(),1},plot.title,Tone::muted);
                primitive(Primitive::Kind::bitmap,{0,header_height+1,columns(),std::max(1,rows()-header_height-3)},{},Tone::normal,false,false,true,false,plot.source);
                text({0,rows()-2,columns(),1},plot.caption,tone(plot.caption_tone));
            }else expanded.reset();
        }
        content_clip=false;fill({0,0,columns(),header_height});x=1;int tab_y=0;
        for(int n=0;n<static_cast<int>(pages.size());++n) {
            const int w=std::min(columns(),glyphs(page_names[n])+4);if(x>1&&x+w>columns()){x=1;++tab_y;}
            const ui::Rect r{x,tab_y,w,1};text(r,"["+page_names[n]+"]",Tone::accent,focus_tab==n,app.page()==pages[n],layers.enable_background);
            if(layers.enable_background)hits.push_back({Hit::Kind::tab,r,n});
            x+=w;
        }
        fill({0,rows()-1,columns(),1});
        std::string footer=notice.empty()?"Tab focus  Alt+Up/Down presets  PgUp/PgDn scroll  F1 help":notice;
        const int selected=focused();if(notice.empty()&&selected>=0&&items[selected].control.help[0])footer=items[selected].control.help;
        text({0,rows()-1,columns(),1},footer,notice.empty()?Tone::muted:Tone::negative);
        render_popup();
        if(help) {
            fill({0,header_height,columns(),std::max(1,rows()-header_height-1)},Tone::inverse);
            text({1,header_height+1,std::max(1,columns()-2),std::max(1,rows()-header_height-2)},
                "Keyboard: Tab / Shift+Tab moves focus and stops at either end. Arrow keys edit text or select records; Left/Right scrolls long record rows. Enter activates the focused control. Space toggles checkboxes. Alt+Down or F4 opens choices or editable presets; Enter accepts, Escape preserves the draft. Ctrl+A selects editor text. Shift+arrows extends selection. Home/End moves within a line; Ctrl+Home/End moves to text boundaries. PgUp/PgDn scrolls about 10% of the visible page, at least one line. Ctrl+Left/Right changes page. Space expands a focused plot; Escape returns. Pasted text is inserted as data and never submits. Mouse clicks focus and activate; wheel scrolls. File dialogs accept a typed path. F1 or Escape closes help. Ctrl+Enter follows the focused editor's send policy; F2 provides the same key on terminals that cannot distinguish it. F3 sends Shift+Enter. Ctrl+Q closes the application.",Tone::normal);
            scene.caret.reset();
        }
        render_dialog();
        // Hidden or removed document controls must not retain sensitive bytes.
        std::set<Identity> current;for(const auto& item:items)current.insert(ui::document_control_identity(item.control));
        for(auto it=editors.begin();it!=editors.end();)if(!current.contains(it->first))it=editors.erase(it);else ++it;
        if(focus&&focused()<0)focus.reset();
    }
    void ensure_visible(int index) {
        if(index<0||index>=static_cast<int>(items.size()))return;
        auto r=items[index].bounds;
        if(items[index].clip) {
            const auto clip=*items[index].clip;const int x=std::max(r.x,clip.x),y=std::max(r.y,clip.y);
            r={x,y,std::max(0,std::min(r.x+r.w,clip.x+clip.w)-x),std::max(0,std::min(r.y+r.h,clip.y+clip.h)-y)};
        }
        if(r.y-scroll<header_height)scroll=std::max(0,r.y-header_height);
        if(r.y+r.h-scroll>rows()-1)scroll=std::max(0,r.y+r.h-rows()+1);
        dirty=true;
    }
    void move_focus(int direction) {
        std::vector<int> order;if(app.overlay_layers().enable_background)for(int n=0;n<static_cast<int>(pages.size());++n)order.push_back(-n-1);
        for(int n=0;n<static_cast<int>(items.size());++n)if(focusable(items[n]))order.push_back(n);
        if(order.empty())return;
        const int current=focus_tab>=0?-focus_tab-1:(focus?focused():-1000000);auto found=std::find(order.begin(),order.end(),current);
        const int last=static_cast<int>(order.size())-1;
        const int index=found==order.end()?(direction>0?0:last):std::clamp(static_cast<int>(found-order.begin())+direction,0,last);
        const int target=order[index];
        if(target<0){focus.reset();focus_tab=-target-1;}else{focus_tab=-1;focus=ui::document_control_identity(items[target].control);ensure_visible(target);}
        popup=-1;dirty=true;
    }
    void open_popup(int index) {
        if(index<0||index>=static_cast<int>(items.size())||!items[index].enabled)return;
        const auto& item=items[index];options=item.menu.empty()?app.control(item.control).state.options:app.menu(item.menu).options;
        if(options.empty())return;
        popup=index;popup_identity=ui::document_control_identity(item.control);popup_selected=0;popup_scroll=0;
        const auto& selected=app.control(item.control).state.selected;
        for(int n=0;n<static_cast<int>(options.size());++n)if(options[n].id==selected)popup_selected=n;
        dirty=true;
    }
    void choose_popup() {
        if(popup<0||popup>=static_cast<int>(items.size())||popup_selected<0||popup_selected>=static_cast<int>(options.size()))return;
        if(!options[popup_selected].enabled)return;
        const auto item=items[popup];const auto id=options[popup_selected].id;
        popup=-1;options.clear();
        if(!item.menu.empty())app.select_menu(item.menu,id);
        else if(item.control.kind==ui::Kind::text)app.preset(item.control,id);
        else app.select(item.control,id);
        dirty=true;
    }
    void activate(int index,bool double_click=false) {
        if(index<0||index>=static_cast<int>(items.size()))return;
        const auto item=items[index];if(!item.enabled)return;
        const auto& c=item.control;
        if(!item.menu.empty()){open_popup(index);return;}
        switch(c.kind) {
        case ui::Kind::action:app.activate(c);break;
        case ui::Kind::toggle:app.toggle(c,!app.control(c).state.checked);break;
        case ui::Kind::choice:open_popup(index);break;
        case ui::Kind::bitmap:app.gesture(c,double_click?c.double_click:c.click);break;
        case ui::Kind::list:app.activate_record(c,app.control(c).state.selected);break;
        default:break;
        }
        dirty=true;
    }
    void replace(Editor& editor,std::string_view value,bool multiline,std::size_t limit,const ui::Control* control) {
        const auto result=ui::text_edit(editor.text,editor.selection,value,multiline,limit);
        if(!result.error.empty()){notice=result.error;dirty=true;return;}
        if(result.changed) {
            editor.text=result.text;editor.selection={result.cursor,result.cursor,result.cursor};
            if(control)app.edit(*control,result.text);
        }
        dirty=true;
    }
    void edit_key(Editor& e,const Event& event,int width,bool multiline,std::size_t limit,const ui::Control* control,bool read_only=false) {
        if(event.ctrl&&(event.text=="a"||event.text=="A")){e.selection={static_cast<int>(e.text.size()),0,static_cast<int>(e.text.size())};dirty=true;return;}
        if(event.type==Event::Type::text) {
            if(!event.ctrl&&!event.alt&&!read_only)replace(e,event.text,multiline,limit,control);
            return;
        }
        int target=e.selection.cursor;bool move=false;
        switch(event.key) {
        case Key::left:target=previous_character(e.text,target);move=true;break;
        case Key::right:target=next_character(e.text,target);move=true;break;
        case Key::home: {
            auto found=target?e.text.rfind('\n',static_cast<std::size_t>(target-1)):std::string::npos;
            target=event.ctrl?0:(found==std::string::npos?0:static_cast<int>(found+1));move=true;break;
        }
        case Key::end: {
            auto found=e.text.find('\n',static_cast<std::size_t>(target));target=event.ctrl?static_cast<int>(e.text.size()):(found==std::string::npos?static_cast<int>(e.text.size()):static_cast<int>(found));move=true;break;
        }
        case Key::up:case Key::down: {
            const auto wrapped=lines(e.text,width);int line=0;for(int n=0;n<static_cast<int>(wrapped.size());++n)if(target>=wrapped[n].begin)line=n;
            const int column=glyphs(std::string_view(e.text).substr(wrapped[line].begin,target-wrapped[line].begin));
            line=std::clamp(line+(event.key==Key::up?-1:1),0,static_cast<int>(wrapped.size())-1);
            target=wrapped[line].begin+glyph_offset(wrapped[line].text,column);move=true;break;
        }
        case Key::backspace:case Key::del:
            if(read_only)break;
            if(e.selection.anchor==e.selection.end) {
                e.selection.anchor=e.selection.cursor;e.selection.end=event.key==Key::backspace?previous_character(e.text,e.selection.cursor):next_character(e.text,e.selection.cursor);
            }
            replace(e,{},multiline,limit,control);break;
        case Key::enter:
            if(!read_only) {
                if(control&&app.submit(*control,event.ctrl,event.shift))dirty=true;
                else if(multiline&&!event.ctrl)replace(e,"\n",true,limit,control);
            }break;
        case Key::space:if(!read_only)replace(e," ",multiline,limit,control);break;
        default:break;
        }
        if(move) {
            if(!event.shift)e.selection.anchor=target;
            e.selection.cursor=target;e.selection.end=target;dirty=true;
        }
    }
    void finish_dialog(bool cancelled) {
        if(dialogs.empty())return;
        const auto request=dialogs.front();
        if(!cancelled) {
            const auto error=ui::service_input_error(request,prompt.text);
            if(!error.empty()){notice=error;dirty=true;return;}
        }
        app.complete_service({request.id,cancelled,prompt.text,{}});dialogs.pop_front();prompt={};notice.clear();prompt_button=0;
        app.set_service_active(!dialogs.empty());if(!dialogs.empty())initialize_prompt();dirty=true;
    }
    void initialize_prompt() {
        prompt={};prompt.text=dialogs.front().value;const int end=static_cast<int>(prompt.text.size());prompt.selection={end,0,end};prompt_button=0;
    }
    void services() {
        auto requests=app.take_services();
        for(auto& request:requests)pending_services.push_back(std::move(request));
        while(!dialogs.empty()&&((dialogs.front().valid&&!*dialogs.front().valid)||app.closing()))finish_dialog(true);
        if(host_wait&&((host_wait->valid&&!*host_wait->valid)||app.closing())) {
            app.complete_service({host_wait->id,true,{},{}});host_wait.reset();host_services.clear();app.set_service_active(false);
        }
        if(app.closing()){pending_services.clear();return;}
        if(!dialogs.empty()||host_wait||!app.overlay_layers().present_services)return;
        while(!pending_services.empty()) {
            auto request=std::move(pending_services.front());pending_services.pop_front();
            if(request.valid&&!*request.valid){app.complete_service({request.id,true,{},{}});continue;}
            app.set_service_active(true);
            if(request.kind==ui::ServiceKind::clipboard||request.kind==ui::ServiceKind::open_folder) {host_wait=request;host_services.push_back(std::move(request));}
            else {dialogs.push_back(std::move(request));initialize_prompt();}
            dirty=true;break;
        }
    }
    void input(const Event& event) {
        // Refresh from the authoritative presentation before applying an edit.
        // A policy revocation cannot be overwritten by cached editor bytes.
        app.tick();services();rebuild();notice.clear();
        if(event.type==Event::Type::input_rejected){notice=event.text;dirty=true;return;}
        if(!dialogs.empty()) {
            if(event.type==Event::Type::pointer) {
                const int x=event.x/viewport.metrics.cell_width,y=event.y/viewport.metrics.line_height;
                for(auto hit=hits.rbegin();hit!=hits.rend();++hit)if(contains(hit->bounds,x,y)) {
                    if(hit->kind==Hit::Kind::accept){finish_dialog(false);return;}
                    if(hit->kind==Hit::Kind::cancel){finish_dialog(true);return;}
                }
                prompt_button=0;dirty=true;return;
            }
            if(event.type==Event::Type::key&&event.key==Key::escape){finish_dialog(true);return;}
            if(event.type==Event::Type::key&&event.key==Key::tab){prompt_button=std::clamp(prompt_button+(event.shift?-1:1),0,2);dirty=true;return;}
            if(event.type==Event::Type::key&&event.key==Key::enter){finish_dialog(prompt_button==2);return;}
            if(prompt_button==0) {
                const auto policy=ui::service_input_policy(dialogs.front());
                if(policy)edit_key(prompt,event,std::max(1,columns()-4),policy->multiline,policy->byte_limit,nullptr);
            }
            return;
        }
        if(event.type==Event::Type::key&&event.key==Key::help){help=!help;dirty=true;return;}
        if(help){if(event.type==Event::Type::key&&event.key==Key::escape){help=false;dirty=true;}return;}
        if(popup>=0) {
            if(event.type==Event::Type::key) {
                if(event.key==Key::escape){popup=-1;options.clear();dirty=true;return;}
                if(event.key==Key::enter){choose_popup();return;}
                if(event.key==Key::up||event.key==Key::down) {popup_selected=std::clamp(popup_selected+(event.key==Key::up?-1:1),0,std::max(0,static_cast<int>(options.size())-1));dirty=true;return;}
                if(event.key==Key::tab){popup=-1;move_focus(event.shift?-1:1);return;}
            }
            if(event.type==Event::Type::pointer) {
                const int x=event.x/viewport.metrics.cell_width,y=event.y/viewport.metrics.line_height;
                for(auto hit=hits.rbegin();hit!=hits.rend();++hit)if(hit->kind==Hit::Kind::option&&contains(hit->bounds,x,y)){popup_selected=hit->index;choose_popup();return;}
                popup=-1;dirty=true;return;
            }
            // Typing in an editable dropdown closes suggestions and edits the
            // existing draft. Merely browsing options never changes that draft.
            if(event.type==Event::Type::text&&items[popup].control.kind==ui::Kind::text)popup=-1;else return;
        }
        if(event.type==Event::Type::key&&app.overlay_key({overlay_key(event.key),event.ctrl,event.shift,event.alt})){dirty=true;return;}
        if(expanded) {
            if(event.type==Event::Type::key&&(event.key==Key::escape||event.key==Key::space)){expanded.reset();dirty=true;return;}
            if(event.type!=Event::Type::wheel)return;
        }
        if(event.type==Event::Type::key) {
            if(event.key==Key::tab){move_focus(event.shift?-1:1);return;}
            if(event.key==Key::page_up||event.key==Key::page_down) {
                const int active=focused();
                if(active>=0&&items[active].control.kind==ui::Kind::list) {
                    scroll_list(items[active],(event.key==Key::page_up?-1:1)*std::max(1,list_bounds(items[active]).h-1));return;
                }
                const int step=std::max(1,(rows()-header_height-1)/10);
                scroll=std::max(0,scroll+(event.key==Key::page_up?-1:1)*step);dirty=true;return;
            }
            if(event.ctrl&&(event.key==Key::left||event.key==Key::right)&&!pages.empty()) {
                auto found=std::find(pages.begin(),pages.end(),app.page());int n=found==pages.end()?0:static_cast<int>(found-pages.begin());
                n=(n+(event.key==Key::left?-1:1)+static_cast<int>(pages.size()))%static_cast<int>(pages.size());
                app.navigate(pages[n]);scroll=0;focus.reset();focus_tab=n;dirty=true;return;
            }
            if(focus_tab>=0) {
                if(event.key==Key::left||event.key==Key::right){focus_tab=std::clamp(focus_tab+(event.key==Key::left?-1:1),0,std::max(0,static_cast<int>(pages.size())-1));dirty=true;return;}
                if((event.key==Key::enter||event.key==Key::space)&&focus_tab<static_cast<int>(pages.size())){app.navigate(pages[focus_tab]);scroll=0;dirty=true;return;}
            }
        }
        int index=focused();
        if(event.type==Event::Type::pointer||event.type==Event::Type::wheel) {
            index=-1;
            const int x=event.x/viewport.metrics.cell_width,y=event.y/viewport.metrics.line_height;
            for(auto hit=hits.rbegin();hit!=hits.rend();++hit)if(contains(hit->bounds,x,y)) {
                if(hit->kind==Hit::Kind::tab&&event.type==Event::Type::pointer){app.navigate(pages[hit->index]);focus.reset();focus_tab=hit->index;scroll=0;dirty=true;return;}
                if(hit->kind==Hit::Kind::item&&y>=header_height&&y<rows()-1){index=hit->index;break;}
            }
            if(event.type==Event::Type::wheel) {
                if(index>=0&&items[index].control.kind==ui::Kind::list&&contains(list_bounds(items[index]),x,y)) {
                    scroll_list(items[index],-event.wheel*3);return;
                }
                if(index>=0&&items[index].control.kind==ui::Kind::bitmap) {
                    const auto& c=items[index].control;const auto command=event.wheel>0?c.wheel_up:c.wheel_down;
                    if(command!=ui::Command::none){app.gesture(c,command);dirty=true;return;}
                }
                scroll=std::max(0,scroll-event.wheel*3);dirty=true;return;
            }
            if(index<0||!focusable(items[index]))return;
            focus=ui::document_control_identity(items[index].control);focus_tab=-1;
            const auto& item=items[index];const auto& c=item.control;const auto& state=app.control(c).state;
            int top=item.bounds.y-scroll;const bool label=c.kind!=ui::Kind::action&&c.kind!=ui::Kind::toggle&&!item.label.empty();if(label)top+=std::min(std::max(0,item.bounds.h-1),static_cast<int>(lines(item.label,item.bounds.w).size()));
            if(c.kind==ui::Kind::text) {
                if(!state.options.empty()&&x>=item.bounds.x+item.bounds.w-3){open_popup(index);return;}
                auto& e=editor(item);const auto wrapped=lines(e.text,item.bounds.w-(state.options.empty()?0:3));
                const int line=std::clamp(y-top+e.first_line,0,static_cast<int>(wrapped.size())-1);
                const int at=wrapped[line].begin+glyph_offset(wrapped[line].text,std::max(0,x-item.bounds.x));e.selection={at,at,at};
            }else if(c.kind==ui::Kind::list) {
                if(!contains(list_bounds(item),x,y)){dirty=true;return;}
                const int row=(y-top+list_positions[ui::document_control_identity(c)].offset)/record_height(c.list_row_height);
                if(row>=0&&row<static_cast<int>(state.records.size())&&state.records[row].enabled) {
                    const auto id=state.records[row].id;app.select(c,id);if(c.activate_on_select||event.double_click)app.activate_record(c,id);
                }
            }else activate(index,event.double_click);
            dirty=true;return;
        }
        if(index<0||index>=static_cast<int>(items.size())||!items[index].enabled)return;
        const auto item=items[index];const auto& c=item.control;const auto& state=app.control(c).state;
        if(event.type==Event::Type::key&&event.alt&&(event.key==Key::down||event.key==Key::up)){open_popup(index);return;}
        if(c.kind==ui::Kind::text) {auto& e=editor(item);edit_key(e,event,item.bounds.w-(state.options.empty()?0:3),c.multiline,c.byte_limit,&c,c.read_only);return;}
        if(event.type==Event::Type::text&&!event.paste&&event.text==" "&&!event.ctrl&&!event.alt){activate(index);return;}
        if(event.type!=Event::Type::key)return;
        if(c.kind==ui::Kind::list&&(event.key==Key::left||event.key==Key::right)) {
            auto& offset=list_positions[ui::document_control_identity(c)].horizontal;offset=std::clamp(offset+(event.key==Key::left?-8:8),0,std::max(0,record_columns(state.records,item.bounds.w)-item.bounds.w));dirty=true;return;
        }
        if(c.kind==ui::Kind::list&&(event.key==Key::up||event.key==Key::down||event.key==Key::home||event.key==Key::end)) {
            if(state.records.empty())return;
            list_positions[ui::document_control_identity(c)].reveal=true;
            int n=-1;for(int i=0;i<static_cast<int>(state.records.size());++i)if(state.records[i].id==state.selected)n=i;
            const int direction=event.key==Key::up||event.key==Key::end?-1:1;
            if(event.key==Key::home)n=0;else if(event.key==Key::end)n=static_cast<int>(state.records.size())-1;else n=std::clamp(n+direction,0,static_cast<int>(state.records.size())-1);
            for(int tries=0;tries<static_cast<int>(state.records.size());++tries) {
                if(state.records[n].enabled){const auto id=state.records[n].id;app.select(c,id);if(c.activate_on_select)app.activate_record(c,id);break;}
                const int next=n+direction;if(next<0||next>=static_cast<int>(state.records.size()))break;n=next;
            }
            dirty=true;return;
        }
        if(c.kind==ui::Kind::choice&&(event.key==Key::up||event.key==Key::down)){open_popup(index);return;}
        if(c.kind==ui::Kind::bitmap&&event.key==Key::space){expanded=ui::document_control_identity(c);dirty=true;return;}
        if(event.key==Key::enter||event.key==Key::space)activate(index);
    }
};
Session::Session(Application& app):impl_(std::make_unique<Impl>(app)) {impl_->rebuild();}
Session::~Session()=default;
void Session::resize(Viewport viewport) {
    viewport.width=std::clamp(viewport.width,1,32768);viewport.height=std::clamp(viewport.height,1,32768);
    viewport.metrics.cell_width=std::clamp(viewport.metrics.cell_width,1,256);viewport.metrics.line_height=std::clamp(viewport.metrics.line_height,1,256);
    impl_->viewport=viewport;impl_->dirty=true;impl_->rebuild();
}
bool Session::tick() {
    const bool changed=impl_->app.tick();impl_->services();
    const auto revision=impl_->app.revision();
    if(changed||impl_->dirty||revision!=impl_->application_revision){impl_->rebuild();impl_->application_revision=revision;return true;}return false;
}
void Session::input(const Event& event) {impl_->input(event);if(impl_->dirty)impl_->rebuild();}
const Scene& Session::scene() const {return impl_->scene;}
std::vector<ui::ServiceRequest> Session::take_host_services() {return std::exchange(impl_->host_services,{});}
void Session::complete_host_service(ui::ServiceResult result) {
    if(!impl_->host_wait||impl_->host_wait->id!=result.id)return;
    if(impl_->host_wait->valid&&!*impl_->host_wait->valid){result.cancelled=true;result.value.clear();result.error.clear();}
    impl_->app.complete_service(std::move(result));impl_->host_wait.reset();impl_->app.set_service_active(false);impl_->dirty=true;
}
}

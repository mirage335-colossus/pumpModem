#pragma once
#include "document_layout.hpp"
#include "bitmap_fltk.hpp"
#include "theme_fltk.hpp"
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Group.H>
#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

namespace datapump::gui {
// A document renderer owns native widgets, text measurement and geometry only.
// The containing backend owns its scrollbar and supplies document revisions.
class FltkDocumentView : public Fl_Group {
public:
    using Action=std::function<void(ui::Command)>;
    FltkDocumentView(int x,int y,int width,int height,Action action={})
        :Fl_Group(x,y,width,height),action_(std::move(action)) {box(FL_NO_BOX);end();}

    void update(const ui::DocumentNode& document) {
        const auto focus=focused_action();
        document_=document;
        reconcile(root_,this,document);
        layout(w());
        if(focus) {
            std::size_t occurrence=focus->occurrence;
            if(auto* target=find_action(root_.get(),focus->command,occurrence);target && target->active_r()) {
                if(Fl::focus()!=target)target->take_focus();
            } else Fl::focus(nullptr);
        }
        redraw();
    }
    void set_document(const ui::DocumentNode& document) {update(document);}
    void action(Action callback) {action_=std::move(callback);}
    int content_height() const {return content_height_;}

    // Explicit dimensions in a document are logical widget units. The caller
    // supplies a new width-dependent document when its grouping must change.
    int layout(int width) {
        width=std::max(1,width);
        content_height_=1;
        ui::DocumentLayout geometry;
        if(root_ && document_) {
            geometry=ui::layout_document(*document_,width,measure_text);
            content_height_=geometry.height;
        }
        content_height_=std::max(1,content_height_);
        Fl_Widget::resize(x(),y(),width,content_height_);
        if(root_)place(*root_,geometry.root,x(),y());
        init_sizes();
        return content_height_;
    }
    void resize(int x,int y,int width,int height) override {
        Fl_Widget::resize(x,y,width,height);
        layout(width);
    }

private:
    using Kind=ui::DocumentKind;
    using Tone=ui::DocumentTone;
    using Fill=ui::DocumentFill;
    struct Decoration {
        Fill fill=Fill::none;
        bool border=false;
        void draw(int x,int y,int width,int height) const {
            if(fill!=Fill::none) {
                const auto level=fill==Fill::surface?theme::surface:fill==Fill::parity?theme::grid:theme::background;
                fl_color(theme::fltk_color(level));fl_rectf(x,y,width,height);
            }
            if(border) {fl_color(theme::fltk_color(theme::grid));fl_rect(x,y,width,height);}
        }
    };
    struct Group : Fl_Group {
        Decoration decoration;
        Group():Fl_Group(0,0,1,1) {box(FL_NO_BOX);end();}
        void draw() override {
            decoration.draw(x(),y(),w(),h());
            fl_push_clip(x(),y(),w(),h());draw_children();fl_pop_clip();
        }
    };
    struct Text : Fl_Box {
        Decoration decoration;
        ui::DocumentRect content;
        Text():Fl_Box(0,0,1,1) {box(FL_NO_BOX);}
        void draw() override {
            decoration.draw(x(),y(),w(),h());
            fl_push_clip(x(),y(),w(),h());
            fl_font(labelfont(),labelsize());fl_color(active_r()?labelcolor():fl_inactive(labelcolor()));
            // Use FLTK's glyph measurement/wrapping, with literal text rather
            // than interpreting an application's '@' characters as symbols.
            if(content.width>0 && content.height>0)
                fl_draw(label()?label():"",x()+content.x,y()+content.y,content.width,content.height,
                    FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
            fl_pop_clip();
        }
    };
    struct Button : Fl_Button {
        ui::DocumentRect content;
        bool border=false;
        Button():Fl_Button(0,0,1,1) {}
        void draw() override {
            draw_box(value()?(down_box()?down_box():fl_down(box())):box(),value()?selection_color():color());
            fl_push_clip(x(),y(),w(),h());
            fl_font(labelfont(),labelsize());fl_color(active_r()?labelcolor():fl_inactive(labelcolor()));
            if(content.width>0 && content.height>0)
                fl_draw(label()?label():"",x()+content.x,y()+content.y,content.width,content.height,
                    FL_ALIGN_CENTER|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
            if(border) {fl_color(theme::fltk_color(theme::grid));fl_rect(x(),y(),w(),h());}
            if(Fl::focus()==this)draw_focus();
            fl_pop_clip();
        }
    };
    struct Bitmap : Fl_Widget {
        BitmapSource source;
        Decoration decoration;
        ui::DocumentRect content;
        Bitmap():Fl_Widget(0,0,1,1) {}
        void draw() override {
            decoration.draw(x(),y(),w(),h());
            fl_push_clip(x(),y(),w(),h());
            widgets::draw_bitmap(source,x()+content.x,y()+content.y,content.width,content.height);
            fl_pop_clip();
        }
    };
    struct Item {
        ui::DocumentNode node;
        Fl_Widget* widget=nullptr;
        FltkDocumentView* owner=nullptr;
        std::vector<std::unique_ptr<Item>> children;
        // Delete descendants before the native group so ownership is singular.
        ~Item() {children.clear();delete widget;}
    };
    struct Focus {ui::Command command;std::size_t occurrence;};
    Action action_;
    std::unique_ptr<Item> root_;
    std::optional<ui::DocumentNode> document_;
    int content_height_=1;

    static int extent(float value) {return ui::document_extent(value);}
    static Fl_Color text_color(Tone tone) {
        return tone==Tone::accent?theme::data_color():tone==Tone::muted?theme::text_color(theme::muted):theme::text_color();
    }
    static void activated(Fl_Widget*,void* data) {
        auto& item=*static_cast<Item*>(data);
        if(item.node.enabled && item.widget->active_r() && item.owner->action_)item.owner->action_(item.node.command);
    }
    void reconcile(std::unique_ptr<Item>& item,Fl_Group* parent,const ui::DocumentNode& node) {
        if(item && item->node.kind!=node.kind)item.reset();
        if(!item) {
            item=std::make_unique<Item>();item->owner=this;
            auto* previous=Fl_Group::current();Fl_Group::current(parent);
            switch(node.kind) {
            case Kind::column:case Kind::row:item->widget=new Group;break;
            case Kind::text:item->widget=new Text;break;
            case Kind::bitmap:item->widget=new Bitmap;break;
            case Kind::action:item->widget=new Button;item->widget->callback(activated,item.get());break;
            }
            Fl_Group::current(previous);
        }
        // The recursive widget records retain their own node values; avoid a
        // second recursively copied tree in each ancestor.
        item->node=node;item->node.children.clear();
        auto& widget=*item->widget;
        if(node.enabled)widget.activate();else widget.deactivate();
        widget.labelfont(node.bold?theme::bold_font:theme::font);
        widget.labelsize(std::max(1,extent(node.font_size)));
        widget.labelcolor(text_color(node.tone));widget.selection_color(theme::fltk_color(theme::grid));
        const Decoration decoration{node.fill,node.border};
        if(auto* group=dynamic_cast<Group*>(&widget))group->decoration=decoration;
        if(auto* text=dynamic_cast<Text*>(&widget))text->decoration=decoration;
        if(auto* bitmap=dynamic_cast<Bitmap*>(&widget)) {
            bitmap->decoration=decoration;bitmap->source=node.plot;
            if(!bitmap->tooltip() || node.plot_name!=bitmap->tooltip())bitmap->copy_tooltip(node.plot_name.c_str());
        }
        if(node.kind==Kind::text || node.kind==Kind::action) {
            if(!widget.label() || node.text!=widget.label())widget.copy_label(node.text.c_str());
        }
        if(node.kind==Kind::action) {
            const auto fill=node.fill==Fill::none||node.fill==Fill::surface?theme::surface:
                node.fill==Fill::parity?theme::grid:theme::background;
            widget.box(FL_UP_BOX);widget.color(theme::fltk_color(fill));
            static_cast<Button&>(widget).border=node.border;
            widget.align(FL_ALIGN_CENTER|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
        }
        auto* group=dynamic_cast<Fl_Group*>(&widget);
        const auto count=group?node.children.size():std::size_t{0};
        while(item->children.size()>count)item->children.pop_back();
        while(item->children.size()<count)item->children.push_back(nullptr);
        for(std::size_t i=0;i<count;++i)reconcile(item->children[i],group,node.children[i]);
        // Replacements append to FLTK's child list; restore declaration order.
        if(group)for(std::size_t i=0;i<count;++i)group->insert(*item->children[i]->widget,static_cast<int>(i));
    }
    static int measure_text(const ui::DocumentNode& node,int width) {
        fl_font(node.bold?theme::bold_font:theme::font,std::max(1,extent(node.font_size)));
        int height=0;fl_measure(node.text.c_str(),width,height,0);return height;
    }
    static void place(Item& item,const ui::DocumentBox& geometry,int x,int y) {
        const auto& box=geometry.bounds;x+=box.x;y+=box.y;
        item.widget->resize(x,y,box.width,box.height);
        if(auto* text=dynamic_cast<Text*>(item.widget))text->content=geometry.content;
        if(auto* bitmap=dynamic_cast<Bitmap*>(item.widget))bitmap->content=geometry.content;
        if(auto* button=dynamic_cast<Button*>(item.widget))button->content=geometry.content;
        for(std::size_t index=0;index<geometry.children.size();++index)
            place(*item.children[index],geometry.children[index],x,y);
        if(auto* group=dynamic_cast<Fl_Group*>(item.widget))group->init_sizes();
    }
    std::optional<Focus> focused_action() const {
        std::unordered_map<ui::Command,std::size_t> occurrences;
        std::optional<Focus> result;
        const std::function<void(const Item*)> visit=[&](const Item* item) {
            if(!item)return;
            if(item->node.kind==Kind::action) {
                const auto occurrence=occurrences[item->node.command]++;
                if(Fl::focus()==item->widget)result=Focus{item->node.command,occurrence};
            }
            for(const auto& child:item->children)visit(child.get());
        };
        visit(root_.get());return result;
    }
    static Fl_Widget* find_action(Item* item,ui::Command command,std::size_t& occurrence) {
        if(!item)return nullptr;
        if(item->node.kind==Kind::action && item->node.command==command) {
            if(occurrence==0)return item->widget;
            --occurrence;
        }
        for(auto& child:item->children)if(auto* target=find_action(child.get(),command,occurrence))return target;
        return nullptr;
    }
};
}

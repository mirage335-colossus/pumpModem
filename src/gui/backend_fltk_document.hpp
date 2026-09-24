#pragma once
#include "document_presentation.hpp"
#include "presentation_palette.hpp"
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
        presentation_.reset(std::make_shared<const ui::DocumentNode>(document));
        native_.clear();reconcile(root_,this,*presentation_.root());
        layout(w());
        if(focus) {
            const auto restored=presentation_.actions().restore_focus(focus);
            auto* target=restored?find_action(root_.get(),*restored):nullptr;
            if(target && target->active_r() && target->visible_r()) {
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
        ui::DocumentPresentation::Layout geometry;
        if(root_ && presentation_.root()) {
            geometry=presentation_.layout(width,measure_text,x(),y());
            content_height_=geometry.height;
        }
        content_height_=std::max(1,content_height_);
        Fl_Widget::resize(x(),y(),width,content_height_);
        for(const auto& placement:geometry.nodes)place(*native_.at(placement.node),placement);
        for(auto item=geometry.nodes.rbegin();item!=geometry.nodes.rend();++item)
            if(auto* group=dynamic_cast<Fl_Group*>(native_.at(item->node)))group->init_sizes();
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
        void draw(int x,int y,int width,int height,bool enabled) const {
            if(const auto color=theme::document_fill_rgb(fill)) {
                fl_color(theme::fltk_color(*color));fl_rectf(x,y,width,height);
            }
            if(border) {fl_color(theme::fltk_color(enabled?theme::WidgetRole::border:theme::WidgetRole::disabled_border));fl_rect(x,y,width,height);}
        }
    };
    struct Group : Fl_Group {
        Decoration decoration;
        Group():Fl_Group(0,0,1,1) {box(FL_NO_BOX);end();}
        void draw() override {
            decoration.draw(x(),y(),w(),h(),active_r());
            fl_push_clip(x(),y(),w(),h());draw_children();fl_pop_clip();
        }
    };
    struct Text : Fl_Box {
        Decoration decoration;
        ui::DocumentRect content;
        Text():Fl_Box(0,0,1,1) {box(FL_NO_BOX);}
        void draw() override {
            decoration.draw(x(),y(),w(),h(),active_r());
            fl_push_clip(x(),y(),w(),h());
            fl_font(labelfont(),labelsize());fl_color(active_r()?labelcolor():theme::fltk_color(theme::WidgetRole::disabled_text));
            // Use FLTK's glyph measurement/wrapping, with literal text rather
            // than interpreting an application's '@' characters as symbols.
            if(content.width>0 && content.height>0)
                fl_draw(label()?label():"",x()+content.x,y()+content.y,content.width,content.height,
                    FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
            fl_pop_clip();
        }
    };
    struct Button : theme::Widget<Fl_Button> {
        ui::DocumentRect content;
        bool border=false;
        Button():theme::Widget<Fl_Button>(0,0,1,1) {}
        void draw() override {
            theme::DrawStyle style(*this);
            draw_box(value()?(down_box()?down_box():fl_down(box())):box(),value()?selection_color():color());
            fl_push_clip(x(),y(),w(),h());
            fl_font(labelfont(),labelsize());fl_color(active_r()?labelcolor():theme::fltk_color(theme::WidgetRole::disabled_text));
            if(content.width>0 && content.height>0)
                fl_draw(label()?label():"",x()+content.x,y()+content.y,content.width,content.height,
                    FL_ALIGN_CENTER|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
            if(border) {fl_color(theme::fltk_color(active_r()?theme::WidgetRole::border:theme::WidgetRole::disabled_border));fl_rect(x(),y(),w(),h());}
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
            decoration.draw(x(),y(),w(),h(),active_r());
            fl_push_clip(x(),y(),w(),h());
            widgets::draw_bitmap(source,x()+content.x,y()+content.y,content.width,content.height);
            fl_pop_clip();
        }
    };
    struct Item {
        Kind kind=Kind::column;
        Fl_Widget* widget=nullptr;
        FltkDocumentView* owner=nullptr;
        std::optional<ui::DocumentActionIdentity> action;
        std::vector<std::unique_ptr<Item>> children;
        // Delete descendants before the native group so ownership is singular.
        ~Item() {children.clear();delete widget;}
    };
    Action action_;
    ui::DocumentPresentation presentation_;
    std::unique_ptr<Item> root_;
    std::unordered_map<const ui::DocumentPresentation::Node*,Fl_Widget*> native_;
    int content_height_=1;

    static int extent(float value) {return ui::document_extent(value);}
    static Fl_Color text_color(Tone tone,bool enabled) {
        return theme::fltk_color(theme::text_rgb(tone,theme::color_enabled,enabled));
    }
    static void activated(Fl_Widget*,void* data) {
        auto& item=*static_cast<Item*>(data);
        if(item.action && item.owner->presentation_.actions().enabled(*item.action) && item.widget->active_r() && item.widget->visible_r() && item.owner->action_)
            item.owner->action_(item.action->command);
    }
    void reconcile(std::unique_ptr<Item>& item,Fl_Group* parent,const ui::DocumentPresentation::Node& presented) {
        const auto& node=*presented.source;
        if(item && item->kind!=node.kind)item.reset();
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
        item->kind=node.kind;item->action=presented.action;native_[&presented]=item->widget;
        auto& widget=*item->widget;
        if(presented.enabled)widget.activate();else widget.deactivate();
        widget.labelfont(node.bold?theme::bold_font:theme::font);
        widget.labelsize(std::max(1,extent(node.font_size)));
        widget.labelcolor(text_color(node.tone,presented.enabled));widget.selection_color(theme::fltk_color(theme::WidgetRole::selection));
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
            widget.box(FL_UP_BOX);widget.color(theme::fltk_color(*theme::document_fill_rgb(node.fill,true,presented.enabled)));
            static_cast<Button&>(widget).border=node.border;
            widget.align(FL_ALIGN_CENTER|FL_ALIGN_INSIDE|FL_ALIGN_CLIP);
        }
        auto* group=dynamic_cast<Fl_Group*>(&widget);
        const auto count=presented.children.size();
        while(item->children.size()>count)item->children.pop_back();
        while(item->children.size()<count)item->children.push_back(nullptr);
        for(std::size_t i=0;i<count;++i)reconcile(item->children[i],group,presented.children[i]);
        // Replacements append to FLTK's child list; restore declaration order.
        if(group)for(std::size_t i=0;i<count;++i)group->insert(*item->children[i]->widget,static_cast<int>(i));
    }
    static int measure_text(const ui::DocumentNode& node,int width) {
        fl_font(node.bold?theme::bold_font:theme::font,std::max(1,extent(node.font_size)));
        int height=0;fl_measure(node.text.c_str(),width,height,0);return height;
    }
    static void place(Fl_Widget& widget,const ui::DocumentPresentation::Placement& placement) {
        const auto& box=placement.absolute;
        widget.resize(box.x,box.y,box.width,box.height);
        if(placement.allocated)widget.show();else widget.hide();
        if(placement.enabled)widget.activate();else widget.deactivate();
        if(auto* text=dynamic_cast<Text*>(&widget))text->content=placement.content;
        if(auto* bitmap=dynamic_cast<Bitmap*>(&widget))bitmap->content=placement.content;
        if(auto* button=dynamic_cast<Button*>(&widget))button->content=placement.content;
    }
    std::optional<ui::DocumentActionIdentity> focused_action() const {
        std::optional<ui::DocumentActionIdentity> result;
        const std::function<void(const Item*)> visit=[&](const Item* item) {
            if(!item)return;
            if(item->action && Fl::focus()==item->widget)result=item->action;
            for(const auto& child:item->children)visit(child.get());
        };
        visit(root_.get());return result;
    }
    static Fl_Widget* find_action(Item* item,ui::DocumentActionIdentity identity) {
        if(!item)return nullptr;
        if(item->action==identity)return item->widget;
        for(auto& child:item->children)if(auto* target=find_action(child.get(),identity))return target;
        return nullptr;
    }
};
}

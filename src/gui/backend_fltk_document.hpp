#pragma once
#include "ui_document.hpp"
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
        reconcile(root_,this,document);
        layout(w());
        if(focus) {
            std::size_t occurrence=focus->occurrence;
            if(auto* target=find_action(root_.get(),focus->command,occurrence);target && target->active_r()) {
                if(Fl::focus()!=target)target->take_focus();
            }
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
        if(root_) {
            measure(*root_,std::min(width,node_width(root_->node,width)));
            content_height_=extent(root_->node.top)+root_->height+extent(root_->node.bottom);
        }
        content_height_=std::max(1,content_height_);
        Fl_Widget::resize(x(),y(),width,content_height_);
        if(root_)place(*root_,x(),y()+extent(root_->node.top));
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
        int padding=0;
        Text():Fl_Box(0,0,1,1) {box(FL_NO_BOX);}
        void draw() override {
            decoration.draw(x(),y(),w(),h());
            fl_push_clip(x(),y(),w(),h());
            fl_font(labelfont(),labelsize());fl_color(active_r()?labelcolor():fl_inactive(labelcolor()));
            // Use FLTK's glyph measurement/wrapping, with literal text rather
            // than interpreting an application's '@' characters as symbols.
            fl_draw(label()?label():"",x()+padding,y()+padding,std::max(1,w()-2*padding),std::max(1,h()-2*padding),
                FL_ALIGN_LEFT|FL_ALIGN_TOP|FL_ALIGN_INSIDE|FL_ALIGN_WRAP,nullptr,0);
            fl_pop_clip();
        }
    };
    struct Bitmap : Fl_Widget {
        plots::PlotSnapshot source;
        Decoration decoration;
        int padding=0;
        Bitmap():Fl_Widget(0,0,1,1) {}
        void draw() override {
            decoration.draw(x(),y(),w(),h());
            fl_push_clip(x(),y(),w(),h());
            widgets::draw_bitmap(source,x()+padding,y()+padding,std::max(0,w()-2*padding),std::max(0,h()-2*padding));
            fl_pop_clip();
        }
    };
    struct Item {
        ui::DocumentNode node;
        Fl_Widget* widget=nullptr;
        FltkDocumentView* owner=nullptr;
        std::vector<std::unique_ptr<Item>> children;
        int width=0,height=0;
        // Delete descendants before the native group so ownership is singular.
        ~Item() {children.clear();delete widget;}
    };
    struct Focus {ui::Command command;std::size_t occurrence;};
    Action action_;
    std::unique_ptr<Item> root_;
    int content_height_=1;

    static int extent(float value) {return std::max(0,static_cast<int>(std::lround(value)));}
    static int node_width(const ui::DocumentNode& node,int available) {
        return node.width>0?std::max(1,std::min(available,extent(node.width))):std::max(1,available);
    }
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
            case Kind::action:item->widget=new Fl_Button(0,0,1,1);item->widget->callback(activated,item.get());break;
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
        if(auto* text=dynamic_cast<Text*>(&widget)) {text->decoration=decoration;text->padding=extent(node.padding);}
        if(auto* bitmap=dynamic_cast<Bitmap*>(&widget)) {
            bitmap->decoration=decoration;bitmap->padding=extent(node.padding);bitmap->source=node.plot;
            if(!bitmap->tooltip() || node.plot_name!=bitmap->tooltip())bitmap->copy_tooltip(node.plot_name.c_str());
        }
        if(node.kind==Kind::text || node.kind==Kind::action) {
            if(!widget.label() || node.text!=widget.label())widget.copy_label(node.text.c_str());
        }
        if(node.kind==Kind::action) {
            widget.box(FL_UP_BOX);widget.color(theme::fltk_color(theme::surface));
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
    static void measure(Item& item,int width) {
        item.width=std::max(1,width);
        const auto& node=item.node;
        const int padding=extent(node.padding),inner=std::max(1,item.width-2*padding);
        int needed=0;
        if(node.kind==Kind::text || node.kind==Kind::action) {
            if(!node.text.empty()) {
                fl_font(node.bold?theme::bold_font:theme::font,std::max(1,extent(node.font_size)));
                int measured_width=inner,height=0;fl_measure(node.text.c_str(),measured_width,height,0);
                needed=height+2;
            }
            if(node.kind==Kind::action)needed=std::max(25,needed+8);
        } else if(node.kind==Kind::row) {
            float cursor=0;
            for(auto& child:item.children) {
                const float requested=child->node.width>0?child->node.width:static_cast<float>(inner)-cursor;
                const float end=std::min(static_cast<float>(inner),cursor+requested);
                measure(*child,std::max(1,extent(end)-extent(cursor)));
                needed=std::max(needed,extent(child->node.top)+child->height+extent(child->node.bottom));
                cursor=end+child->node.right;
            }
            if(node.equal_height)for(auto& child:item.children)
                child->height=std::max(0,needed-extent(child->node.top)-extent(child->node.bottom));
        } else if(node.kind==Kind::column) {
            for(auto& child:item.children) {
                measure(*child,node_width(child->node,std::max(1,inner-extent(child->node.right))));
                needed+=extent(child->node.top)+child->height+extent(child->node.bottom);
            }
        }
        item.height=node.height>0?extent(node.height):needed+2*padding;
    }
    static void place(Item& item,int x,int y) {
        item.widget->resize(x,y,item.width,item.height);
        const int padding=extent(item.node.padding);
        float cursor=0;
        for(auto& child:item.children) {
            const int top=extent(child->node.top);
            if(item.node.kind==Kind::row) {
                place(*child,x+padding+extent(cursor),y+padding+top);
                const float requested=child->node.width>0?child->node.width:static_cast<float>(item.width-2*padding)-cursor;
                cursor=std::min(static_cast<float>(item.width-2*padding),cursor+requested)+child->node.right;
            } else {
                place(*child,x+padding,y+padding+extent(cursor)+top);
                cursor+=static_cast<float>(top+child->height+extent(child->node.bottom));
            }
        }
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

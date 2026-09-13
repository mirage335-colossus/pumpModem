#include "backend_fltk_document.hpp"
#include "gui_extension_fixture.hpp"
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Scroll.H>
#include <iostream>
#include <stdexcept>

namespace {
namespace ui=datapump::gui::ui;
using datapump::gui::FltkDocumentView;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
ui::DocumentNode label(std::string text,float width) {
    ui::DocumentNode node;node.kind=ui::DocumentKind::text;node.text=std::move(text);node.width=width;return node;
}
ui::DocumentNode fixture() {
    ui::DocumentNode root;root.width=400;
    ui::DocumentNode row;row.kind=ui::DocumentKind::row;row.width=400;row.equal_height=true;
    ui::DocumentNode left;left.width=190;left.padding=10;left.right=20;left.fill=ui::DocumentFill::surface;left.border=true;
    left.children.push_back(label("A short card",170));
    auto right=left;right.right=0;
    right.children[0].text="A longer card with native UTF-8 caf\xc3\xa9 text that wraps across several lines.";
    row.children={left,right};root.children.push_back(row);
    ui::DocumentNode action;action.kind=ui::DocumentKind::action;action.text="Next";action.width=90;action.height=28;
    action.command=ui::Command::pattern_next;action.top=12;root.children.push_back(action);
    auto paragraph=label(std::string(1000,'W'),400);paragraph.top=20;root.children.push_back(paragraph);
    return root;
}
Fl_Button* find_button(Fl_Group& group) {
    for(int i=0;i<group.children();++i) {
        if(auto* button=dynamic_cast<Fl_Button*>(group.child(i)))return button;
        if(auto* nested=dynamic_cast<Fl_Group*>(group.child(i)))if(auto* button=find_button(*nested))return button;
    }
    return nullptr;
}
}

int main() {
    try {
        datapump::gui::theme::apply_palette();
        Fl_Double_Window window(450,250,"FLTK generic document regression");
        Fl_Scroll scroll(10,10,430,230);scroll.type(Fl_Scroll::VERTICAL);
        unsigned actions=0;
        auto* view=new FltkDocumentView(10,10,400,1,[&](ui::Command command){require(command==ui::Command::pattern_next,"Wrong action command");++actions;});
        scroll.end();window.end();window.show();Fl::check();
        auto document=fixture();view->update(document);Fl::check();
        require(actions==0,"Applying a document emitted an action");
        auto* root=dynamic_cast<Fl_Group*>(view->child(0));
        auto* row=dynamic_cast<Fl_Group*>(root->child(0));
        require(row && row->children()==2,"Document row lost its native cards");
        require(row->child(0)->h()==row->child(1)->h() && row->child(0)->h()>40,"Cards were not equalized after native text measurement");
        require(row->child(0)->x()+row->child(0)->w()+20==row->child(1)->x(),"Document row gap changed");
        require(view->content_height()==view->h() && view->h()>=root->h(),"Document content extent does not contain its native widgets");
        auto* button=find_button(*view);require(button,"Native action button is missing");
        button->take_focus();button->do_callback();require(actions==1,"Enabled native action did not dispatch");
        view->update(document);
        require(find_button(*view)==button && Fl::focus()==button && actions==1,"Unchanged update replaced native focus or emitted an action");
        document.children[1].enabled=false;view->update(document);button->do_callback();
        require(actions==1 && !button->active_r(),"Disabled native action remained active");
        document.children[1].enabled=true;view->update(document);button->take_focus();
        document.children.insert(document.children.begin()+1,label("Inserted native heading",400));
        view->update(document);
        require(Fl::focus()==find_button(*view),"Action focus was not restored after a document insertion");
        const int old_height=view->content_height();
        view->layout(250);
        require(view->content_height()>=old_height,"Narrower document width reduced wrapped content height");
        const int old_x=view->x(),old_y=view->y();
        const auto child_x=view->child(0)->x(),child_y=view->child(0)->y();
        view->resize(old_x+7,old_y-31,250,view->h());
        require(view->child(0)->x()==child_x+7 && view->child(0)->y()==child_y-31,"Moving the scrolled document displaced native content incorrectly");
        view->action([&](ui::Command command){require(command==ui::Command::clear_received,"Shared extension document action changed identity");++actions;});
        view->update(datapump::gui::test::extension_document());
        auto* extension_root=dynamic_cast<Fl_Group*>(view->child(0));
        require(extension_root&&std::string(extension_root->child(0)->label())=="Added document field","Shared extension document text did not render through the unchanged factory");
        button=find_button(*view);require(button&&std::string(button->label())=="Added document action","Shared extension document action did not render");
        const auto before=actions;button->do_callback();require(actions==before+1,"Shared extension document action did not dispatch");
        window.hide();
        std::cout<<"FLTK generic document checks passed.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

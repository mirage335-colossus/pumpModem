#include "backend_fltk_document.hpp"
#include "gui_extension_fixture.hpp"
#include "document_geometry_fixture.hpp"
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
        auto replacement_document=document;replacement_document.children[2].command=ui::Command::clear_received;
        view->update(replacement_document);
        require(Fl::focus()!=find_button(*view),"Replacing a document action transferred focus to a different command");
        view->update(document);find_button(*view)->take_focus();
        replacement_document=document;replacement_document.children.erase(replacement_document.children.begin()+2);
        view->update(replacement_document);
        require(!find_button(*view),"Removing a document action retained its native button");
        view->update(document);
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
        auto bitmap_probe=std::make_shared<datapump::gui::test::BitmapProbe>();
        auto border_probe=std::make_shared<datapump::gui::test::BitmapProbe>();
        auto geometry_document=datapump::gui::test::document_geometry_fixture();
        geometry_document.children[3].plot=datapump::gui::test::rectangle_bitmap(bitmap_probe);
        geometry_document.children[6].plot=datapump::gui::test::rectangle_bitmap(border_probe);
        view->layout(400);view->update(geometry_document);Fl::check();
        require(!bitmap_probe->requests.empty()&&bitmap_probe->requests.back().width==86&&bitmap_probe->requests.back().height==36,
            "FLTK document bitmap did not paint the shared padded framebuffer extent");
        auto* geometry_root=dynamic_cast<Fl_Group*>(view->child(0));
        const auto matches=[](Fl_Widget* widget,Fl_Widget* parent,const auto& expected) {
            return widget->x()-parent->x()==expected[0] && widget->y()-parent->y()==expected[1] &&
                widget->w()==expected[2] && widget->h()==expected[3];
        };
        for(std::size_t index=0;index<datapump::gui::test::document_geometry_rows.size();++index)
            require(matches(geometry_root->child(static_cast<int>(index)),geometry_root,datapump::gui::test::document_geometry_rows[index]),"FLTK document parent geometry diverged from the shared fixture");
        const auto verify_row=[&](int index,const auto& expected) {
            auto* group=dynamic_cast<Fl_Group*>(geometry_root->child(index));
            for(std::size_t child=0;child<expected.size();++child)
                require(matches(group->child(static_cast<int>(child)),group,expected[child]),"FLTK document child geometry diverged from the shared fixture");
        };
        verify_row(0,datapump::gui::test::document_geometry_remainder);
        verify_row(1,datapump::gui::test::document_geometry_margins);
        verify_row(2,datapump::gui::test::document_geometry_equal);
        verify_row(4,datapump::gui::test::document_geometry_overflow);
        button=find_button(*view);
        require(button&&button->h()>60,"FLTK document action did not measure its wrapped padded label");
        require(button->color()==datapump::gui::theme::fltk_color(datapump::gui::theme::grid),
            "FLTK document action ignored its explicit semantic fill");
        geometry_document.children[5].fill=ui::DocumentFill::none;geometry_document.children[5].border=false;
        view->update(geometry_document);
        require(find_button(*view)->color()==datapump::gui::theme::fltk_color(datapump::gui::theme::surface),
            "Clearing document action fill did not restore the native default");
        // Reveal the fixture's final node even on this small test viewport.
        view->update(geometry_document.children[6]);Fl::check();
        require(!border_probe->requests.empty()&&border_probe->requests.back().width==98&&border_probe->requests.back().height==48,
            "FLTK zero-padding document bitmap painted over its declared border");
        auto action_document=datapump::gui::test::document_actions_fixture();
        const auto second_action=[&] {
            auto* body=dynamic_cast<Fl_Group*>(view->child(0));
            auto* nested=dynamic_cast<Fl_Group*>(body->child(body->children()-1));
            return dynamic_cast<Fl_Button*>(nested->child(0));
        };
        view->update(action_document);button=second_action();button->take_focus();
        const auto actions_before=actions;
        action_document.children.insert(action_document.children.begin(),label("Inserted heading",100));
        view->update(action_document);
        require(Fl::focus()==second_action()&&actions==actions_before,
            "Repeated document command focus changed after a shared tree insertion");
        second_action()->do_callback();require(actions==actions_before+1,"Repeated document action lost dispatch");
        // Move the first command after the nested second command, then remove
        // it. Explicit action identity must survive both changes.
        auto first_action=action_document.children[1];action_document.children.erase(action_document.children.begin()+1);
        action_document.children.back().children.push_back(first_action);view->update(action_document);
        require(Fl::focus()==second_action(),"Reordering repeated document actions transferred native focus");
        action_document.children.back().children.pop_back();view->update(action_document);
        require(Fl::focus()==second_action(),"Removing another document action instance lost native focus");
        action_document.children.back().enabled=false;view->update(action_document);
        second_action()->do_callback();
        require(Fl::focus()!=second_action()&&actions==actions_before+1,
            "Disabled document ancestor retained focus or allowed a command");
        action_document.children.back().enabled=true;view->update(action_document);second_action()->take_focus();
        view->deactivate();view->update(action_document);second_action()->do_callback();
        require(Fl::focus()!=second_action()&&actions==actions_before+1,
            "Disabled native document host retained focus or allowed a command");
        view->activate();view->update(action_document);second_action()->take_focus();
        action_document.children.back().children={first_action};view->update(action_document);
        require(Fl::focus()!=second_action(),"Removing a focused document action transferred focus to another instance");
        view->hide();second_action()->do_callback();
        require(actions==actions_before+1,"Hidden document host allowed a stale native action callback");
        view->show();
        second_action()->deactivate();second_action()->do_callback();
        require(actions==actions_before+1,"Disabled native document action dispatched a command");
        auto empty_action_document=datapump::gui::test::document_empty_action_fixture();view->update(empty_action_document);
        find_button(*view)->take_focus();empty_action_document.width=50;view->update(empty_action_document);
        button=find_button(*view);button->do_callback();
        require(button->w()==0&&!button->visible_r()&&Fl::focus()!=button&&!button->take_focus()&&actions==actions_before+1,
            "Empty document action retained native focus or dispatched a command");
        auto clipped_document=datapump::gui::test::document_clipped_action_fixture();view->update(clipped_document);
        button=find_button(*view);require(button->take_focus(),"Visible document action could not take focus");
        clipped_document.height=20;view->update(clipped_document);button=find_button(*view);button->do_callback();
        require(button->w()>0&&button->h()>0&&!button->visible_r()&&Fl::focus()!=button&&!button->take_focus()&&actions==actions_before+1,
            "Fully clipped document action retained native focus or dispatched a command");
        clipped_document.height=60;view->update(clipped_document);button=find_button(*view);
        require(button->visible_r()&&button->active_r()&&button->take_focus(),
            "Restoring the document clip did not restore native action availability");
        button->do_callback();require(actions==actions_before+2,"Restored document action did not dispatch");
        window.hide();
        std::cout<<"FLTK generic document checks passed.\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

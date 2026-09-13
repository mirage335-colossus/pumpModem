// Included inside RevApp only by the native conformance executable.
    void verify_updated_labels() {
        for(std::size_t index=0;index<4;++index) {
            const auto& binding=bindings[index];
            const auto label=application.control(binding.control).label;
            auto* native=binding.toggle?binding.toggle->label:binding.button?binding.button->labelText:binding.label;
            if(!native||native->content.get()!=label)throw std::runtime_error("Rev extension retained a stale shared control label");
        }
    }
    void verify_service_shutdown() {
        services.enqueue({{0,ui::ServiceKind::prompt,"Shutdown prompt",""},
            {1,ui::ServiceKind::clipboard,"Queued copy","must not copy"}});
        process_services();
        if(!dialog)throw std::runtime_error("Rev shutdown fixture did not open a prompt");
        application.close();apply();
        if(dialog||modal||prompt||!services.closed()||services.next())
            throw std::runtime_error("Rev closing retained a native dialog or queued platform request");
    }
    void verify_editor_contract() {
        for(auto& binding:bindings)if(binding.control.kind==ui::Kind::text&&binding.control.multiline&&binding.editor) {
            const auto field=binding.control.field;auto& editor=*binding.editor;
            const auto saved=application.field(field).text;
            application.edit(field,"A\xc3\xa9\xf0\x9f\x8c\x8d\nsecond line");apply();
            const auto revision=application.revision();apply();
            if(application.revision()!=revision)throw std::runtime_error("Rev smoke: applying state emitted an input event");
            editor.targetFlags.focus=true;editor.cursor=7;editor.selectAnchor=editor.selectEnd=7;
            re::Event key;
            key.keyboard.backspace.id=1;editor.keyDown(key);
            if(application.field(field).text!="A\xc3\xa9\nsecond line" || editor.cursor!=3)
                throw std::runtime_error("Rev smoke: backspace split a UTF-8 character");
            key.keyboard.backspace.id=-1;key.keyboard.arrows.left.id=1;editor.keyDown(key);
            if(editor.cursor!=1)throw std::runtime_error("Rev smoke: left arrow split a UTF-8 character");
            editor.cursor=3;editor.selectAnchor=1;editor.selectEnd=3;
            const auto before_paste=application.revision();
            auto original_error=std::move(editor.error);bool reported=false;
            editor.error=[&](std::string text){reported=text=="Clipboard unavailable";};
            editor.complete_paste({std::nullopt,"Clipboard unavailable"});
            editor.error=std::move(original_error);
            if(!reported || editor.content.get()!="A\xc3\xa9\nsecond line" || editor.cursor!=3 || editor.selectAnchor!=1 || editor.selectEnd!=3 || application.revision()!=before_paste)
                throw std::runtime_error("Rev smoke: a failed clipboard read changed selected UTF-8 text");
            editor.complete_paste({std::string{}, {}});
            if(editor.content.get()!="A\nsecond line" || editor.cursor!=1 || editor.selectAnchor!=1 || editor.selectEnd!=1 || application.revision()==before_paste)
                throw std::runtime_error("Rev smoke: successful empty clipboard text did not replace the selection");
            const auto before_rejected=editor.content.get();
            editor.cursor=1;editor.selectAnchor=0;editor.selectEnd=1;
            const auto byte_limit=editor.limit;editor.limit=before_rejected.size();
            auto error_handler=std::move(editor.error);unsigned rejected=0;editor.error=[&](std::string){++rejected;};
            const bool malformed=editor.replace("\xc3"),oversized=editor.replace("too much replacement");
            editor.limit=byte_limit;editor.error=std::move(error_handler);
            if(malformed||oversized||rejected!=2||editor.content.get()!=before_rejected||editor.cursor!=1||editor.selectAnchor!=0||editor.selectEnd!=1)
                throw std::runtime_error("Rev smoke: rejected edit changed UTF-8 text or native selection");
            editor.targetFlags.focus=false;application.edit(field,saved);apply();
            return;
        }
        throw std::runtime_error("Rev smoke: missing semantic message editor");
    }
    void verify_choice_contract() {
        const auto state=test::effective_choice();
        auto* choice=new ChoiceView(this,{.label="",.value=state.selected});
        for(const auto& option:state.options)choice->params.options.push_back({option.label,option.id,!option.enabled});
        re::Event event;choice->font_size=19;choice->display_text=state.display_text;choice->computeChildren(event);
        if(choice->params.value!=state.selected||choice->dropdownText->content.get()!=state.display_text||choice->targetFlags.disabled)
            throw std::runtime_error("Rev smoke: effective choice text disabled or changed the saved option");
        if(choice->dropdownText->style->text.size.val!=19||choice->options.front()->style->text.size.val!=19)
            throw std::runtime_error("Rev smoke: dropdown field or menu ignored the declared font size");
        unsigned changes=0;choice->onChange=[&](re::Event&){++changes;};
        choice->options[1]->click(event);choice->computeChildren(event);
        if(choice->params.value!=state.options[1].id||changes!=1)
            throw std::runtime_error("Rev smoke: effective choice did not allow selecting another saved option");
        choice->display_text.clear();choice->computeChildren(event);
        if(choice->params.value!=state.options[1].id||choice->dropdownText->content.get()!=state.options[1].label)
            throw std::runtime_error("Rev smoke: clearing effective choice text did not restore the saved option");
        delete choice;
    }
    void verify_record_contract() {
        ui::Control declaration{ui::Kind::list};declaration.list_row_height=54;declaration.activate_on_select=true;
        auto* probe=new ListView(this,declaration,launch.color);probe->resize_content(400,110);
        if(probe->empty->style->text.size.val!=declaration.font_size)throw std::runtime_error("Rev smoke: list empty-state text ignored its declared font size");
        unsigned selections=0,activations=0;std::string selected;
        probe->select=[&](std::string id){++selections;selected=std::move(id);};
        probe->activate=[&](std::string){++activations;};
        auto state=test::extension_records();const auto active_id=state.records[0].id,inactive_id=state.records[1].id;
        probe->apply(state);auto* retained=probe->rows.at(active_id).button;retained->targetFlags.focus=true;
        const auto initial_cells=state.records[0].cells.size();
        state.records[0].cells.push_back({"Late metadata",8,24,-8,24,11,ui::TextTone::muted});
        std::swap(state.records[0],state.records[1]);
        state.records.push_back({"disabled",{{"Disabled",8,0,-8,24,13}},false,false});
        probe->apply(state);
        if(selections||activations||probe->rows.at(active_id).button!=retained||!retained->targetFlags.focus||
           probe->rows.at(active_id).cells.size()!=initial_cells+1||probe->children[2]!=retained)
            throw std::runtime_error("Rev smoke: record replacement lost stable identity, focus, cell metadata or silent update");
        re::Event click;retained->click(click);
        if(selected!=active_id||selections!=1||activations!=1)throw std::runtime_error("Rev smoke: record activation did not dispatch its stable ID");
        probe->rows.at(inactive_id).button->click(click);
        if(selected!=inactive_id||selections!=2||activations!=1)throw std::runtime_error("Rev smoke: incomplete record was activated");
        probe->rows.at("disabled").button->click(click);
        if(selections!=2||activations!=1)throw std::runtime_error("Rev smoke: disabled record was selected");
        state.records.erase(state.records.begin());probe->apply(state);
        if(probe->rows.contains(inactive_id)||probe->rows.at(active_id).button!=retained||!retained->targetFlags.focus)
            throw std::runtime_error("Rev smoke: record removal changed another record's focus");
        click.keyboard.space.id=1;retained->click(click);click.keyboard.space.id=-1;
        if(selections!=3||activations!=2)throw std::runtime_error("Rev smoke: Space did not apply shared activate-on-select policy");
        click.keyboard.enter.id=1;retained->click(click);click.keyboard.enter.id=-1;
        if(selections!=4||activations!=3)throw std::runtime_error("Rev smoke: Enter duplicated activate-on-select activation");
        delete probe;
        declaration.activate_on_select=false;probe=new ListView(this,declaration,launch.color);
        selections=activations=0;probe->select=[&](std::string){++selections;};probe->activate=[&](std::string){++activations;};
        probe->apply(test::extension_records());retained=probe->rows.at(active_id).button;
        click.propagate=true;retained->click(click);
        if(selections!=1||activations)throw std::runtime_error("Rev smoke: selecting a record ignored its separate activation policy");
        click.keyboard.enter.id=1;retained->click(click);click.keyboard.enter.id=-1;
        if(selections!=2||activations!=1)throw std::runtime_error("Rev smoke: Enter did not activate a selected record");
        retained->click(click);retained->click(click);
        if(selections!=4||activations!=2)throw std::runtime_error("Rev smoke: double-click did not activate a record");
        click.keyboard.space.id=1;retained->click(click);click.keyboard.space.id=-1;
        if(selections!=5||activations!=2)throw std::runtime_error("Rev smoke: Space bypassed separate record activation policy");
        state=test::extension_records();state.enabled=false;probe->apply(state);
        retained->click(click);click.keyboard.enter.id=1;retained->click(click);click.keyboard.enter.id=-1;
        if(probe->navigate(retained,1)||selections!=5||activations!=2)
            throw std::runtime_error("Rev smoke: disabled list permitted shared pointer or keyboard selection");
        delete probe;
        const auto paint=[&]{refresh(event);for(unsigned frame=0;frame<4;++frame){Rev::NativeWindow::pumpEvents();std::this_thread::sleep_for(std::chrono::milliseconds(4));}};
        declaration.follow_tail=true;declaration.activate_on_select=true;
        auto* probe_parent=new re::Box(this);place(probe_parent,{20,200,400,600});
        probe=new ListView(probe_parent,declaration,launch.color);place(probe,{0,0,400,180});probe->resize_content(400,180);
        state=test::extension_records();const auto template_record=state.records.front();state.records.clear();
        for(unsigned index=0;index<8;++index) {auto record=template_record;record.id="tail-"+std::to_string(index);state.records.push_back(std::move(record));}
        state.records[1].enabled=false;probe->apply(state);paint();
        if(!probe->at_tail())throw std::runtime_error("Rev smoke: new records did not follow the declared tail policy");
        probe->resolved.scroll.y=static_cast<float>(probe->maximum_scroll()-2);
        auto near_tail=template_record;near_tail.id="near-tail";state.records.push_back(std::move(near_tail));probe->apply(state);paint();
        if(std::abs(probe->resolved.scroll.y-probe->maximum_scroll())>1)
            throw std::runtime_error("Rev smoke: native list did not use the shared two-unit tail tolerance");
        place(probe,{0,0,400,80});probe->resize_content(400,80);paint();
        if(!probe->at_tail())throw std::runtime_error("Rev smoke: shrinking a record viewport lost the tail");
        probe->resolved.scroll.y=54;place(probe,{0,0,400,100});probe->resize_content(400,100);paint();
        if(std::abs(probe->resolved.scroll.y-54)>1)throw std::runtime_error("Rev smoke: resizing a historical record viewport moved the reader: scroll="+std::to_string(probe->resolved.scroll.y)+" content="+std::to_string(probe->layout.rect.h)+" inner="+std::to_string(probe->resolved.getInner(Axis::Vertical)));
        selections=activations=0;probe->select=[&](std::string id){++selections;selected=std::move(id);};probe->activate=[&](std::string){++activations;};
        focus_control(probe->rows.at("tail-0").button);
        re::Event key;key.keyboard.arrows.down.id=1;keyDown(key);
        if(selected!="tail-2"||selections!=1||activations!=1||focused_control()!=probe->rows.at("tail-2").button)
            throw std::runtime_error("Rev smoke: Down did not select/focus the next enabled stable record");
        key.keyboard.arrows.down.id=-1;key.keyboard.arrows.up.id=1;key.propagate=true;keyDown(key);
        if(selected!="tail-0"||selections!=2||activations!=2||focused_control()!=probe->rows.at("tail-0").button||probe->resolved.scroll.y>1)
            throw std::runtime_error("Rev smoke: Up did not reveal and select the previous enabled stable record");
        focus_control(nullptr);probe->resolved.scroll.y=10000;paint();
        probe_parent->style->visibility=Visibility::Hidden;paint();
        auto appended=template_record;appended.id="tail-appended";state.records.push_back(std::move(appended));probe->apply(state);
        probe_parent->style->visibility=Visibility::Visible;paint();
        if(!probe->at_tail())throw std::runtime_error("Rev smoke: appending records on a hidden page lost the tail");
        for(bool follows:{true,false}) {
            probe->follow_tail=follows;probe->resolved.scroll.y=60;paint();
            probe_parent->style->visibility=Visibility::Hidden;paint();
            appended=template_record;appended.id=follows?"history-with-tail":"history-without-tail";state.records.push_back(std::move(appended));probe->apply(state);
            probe_parent->style->visibility=Visibility::Visible;paint();
            if(std::abs(probe->resolved.scroll.y-60)>1)throw std::runtime_error("Rev smoke: hidden record updates lost the reader's historical position");
        }
        delete probe_parent;paint();
    }
    void verify_extension_contract() {
        verify_document_clipping();
        const auto& fixture=test::extension_controls();
        if(bindings.size()!=ui::control_groups(fixture).size()||!bindings[0].label||bindings[0].label->content.get()!=fixture[0].label||!bindings[1].button)
            throw std::runtime_error("Rev smoke: declarative extension was not materialized literally");
        if(bindings[1].button->labelText->content.get()!=fixture[1].label||!bindings[3].toggle||bindings[3].toggle->label->content.get()!=fixture[3].label)
            throw std::runtime_error("Rev smoke: action/toggle literal labels were changed by the native factory");
        unsigned calls=0;command_observer=[&](ui::Command command){if(command==fixture[1].command)++calls;};
        re::Event click;bindings[1].button->click(click);command_observer={};
        if(calls!=1)throw std::runtime_error("Rev smoke: declarative extension action was not dispatched");
        std::vector<ui::Command> gestures;command_observer=[&](ui::Command command){gestures.push_back(command);};
        auto* target=bindings[0].element;re::Event pointer;pointer.mouse.lb.id=1;pointer.mouse.pos={8,8};
        target->mouseDown(pointer);pointer.propagate=true;target->mouseDown(pointer);
        pointer.propagate=true;pointer.mouse.wheel.y=360;target->mouseWheel(pointer);
        pointer.propagate=true;pointer.mouse.wheel.y=-240;target->mouseWheel(pointer);
        const std::vector<ui::Command> expected{fixture[0].click,fixture[0].double_click,fixture[0].wheel_up,fixture[0].wheel_up,fixture[0].wheel_up,fixture[0].wheel_down,fixture[0].wheel_down};
        if(gestures!=expected)throw std::runtime_error("Rev smoke: generic label gestures lost shared command or wheel repetition policy");
        target->setDisabled(true);pointer.propagate=true;target->mouseDown(pointer);pointer.propagate=true;target->mouseWheel(pointer);
        if(gestures!=expected)throw std::runtime_error("Rev smoke: disabled generic control dispatched gestures");
        target->setDisabled(false);command_observer={};
        auto& text_binding=bindings[2];auto* presets=text_binding.suggestions;
        if(!text_binding.editor||!presets||!text_binding.has_suggestions||presets->params.options.size()<2)
            throw std::runtime_error("Rev smoke: multiline text lost its declared presets");
        if(text_binding.editor->style->text.size.val!=fixture[2].font_size||text_binding.label->style->text.size.val!=fixture[2].font_size)
            throw std::runtime_error("Rev smoke: multiline text lost its declared font size");
        const auto preset=application.field(fixture[2].field).options[1].id;
        presets->params.value=preset;presets->onChange(click);
        if(application.field(fixture[2].field).text!=preset)throw std::runtime_error("Rev smoke: multiline preset did not dispatch an ordinary shared edit");
        const auto original=application.field(fixture[2].field).text;
        auto* limited=bindings[4].suggestions;limited->params.value=preset;limited->onChange(click);
        if(application.field(fixture[2].field).text!=original||application.field(ui::Field::status).text.find("byte")==std::string::npos)
            throw std::runtime_error("Rev smoke: native preset bypassed shared control validation");
        auto& menu=bindings[5];
        if(!menu.menu||menu.element->style->visibility==Visibility::Hidden||menu.element->targetFlags.disabled||
           menu.menu->params.options.size()!=1||menu.menu->params.options[0].name!="Clear from shared menu")
            throw std::runtime_error("Rev smoke: hidden leading menu item changed the visible shared menu");
        if(!bindings[6].menu||!bindings[7].menu)throw std::runtime_error("Rev smoke: menus from distinct shared scopes were merged");
        menu.menu->params.value=menu.menu->params.options[0].value;menu.menu->onChange(click);
        if(application.field(ui::Field::status).text.find("cleared")==std::string::npos)
            throw std::runtime_error("Rev smoke: filtered native menu dispatched the wrong shared entry");
        auto document=std::make_shared<const ui::DocumentNode>(test::extension_document());
        auto* document_parent=new re::Box(this);place(document_parent,{0,0,600,800});
        auto* view=new RevDocumentView(document_parent,launch.color,
            [this](re::Element* parent,const BitmapSource& source)->re::Element* {auto* bitmap=new BitmapView(parent,launch.color,[this]{return details.scale;});bitmap->set(source);return bitmap;},
            [&](ui::Command command){if(command==document->children[1].command)++calls;});
        view->apply(document);
        auto* root=view->children.front();auto* button=dynamic_cast<re::Button*>(root->children[1]);
        auto* text=dynamic_cast<re::Text*>(root->children[0]);
        if(!button||!text||text->content.get()!=document->children[0].text)throw std::runtime_error("Rev smoke: document extension did not retain native fields/actions");
        button->click(click);if(calls!=2)throw std::runtime_error("Rev smoke: document extension action was not dispatched");
        auto disabled_document=std::make_shared<ui::DocumentNode>(*document);disabled_document->enabled=false;
        view->apply(disabled_document);button=dynamic_cast<re::Button*>(view->children.front()->children[1]);
        click.propagate=true;button->click(click);
        if(allows_input(button)||calls!=2)throw std::runtime_error("Rev smoke: disabled document parent allowed action input");
        view->apply(document);button=dynamic_cast<re::Button*>(view->children.front()->children[1]);focus_control(button);
        auto replacement=std::make_shared<ui::DocumentNode>(*document);replacement->children[1].command=ui::Command::reset_zoom;
        view->apply(replacement);button=dynamic_cast<re::Button*>(view->children.front()->children[1]);
        if(button->targetFlags.focus)throw std::runtime_error("Rev smoke: replacing a document command transferred native focus");
        view->apply(document);button=dynamic_cast<re::Button*>(view->children.front()->children[1]);focus_control(button);
        replacement=std::make_shared<ui::DocumentNode>(*document);replacement->children.erase(replacement->children.begin()+1);view->apply(replacement);
        if(view->children.front()->children.size()!=1)throw std::runtime_error("Rev smoke: removing a document action retained its native button");
        view->apply(document);button=dynamic_cast<re::Button*>(view->children.front()->children[1]);focus_control(button);view->apply(disabled_document);
        button=dynamic_cast<re::Button*>(view->children.front()->children[1]);
        if(button->targetFlags.focus)throw std::runtime_error("Rev smoke: disabled document action regained focus");
        auto bitmap_probe=std::make_shared<test::BitmapProbe>();auto empty_probe=std::make_shared<test::BitmapProbe>();auto border_probe=std::make_shared<test::BitmapProbe>();auto geometry_document=test::document_geometry_fixture();
        geometry_document.children[3].plot=test::rectangle_bitmap(bitmap_probe);
        geometry_document.children[4].children[1].plot=test::rectangle_bitmap(empty_probe);
        geometry_document.children[6].plot=test::rectangle_bitmap(border_probe);
        place(view,{20,100,400,600});view->apply(std::make_shared<const ui::DocumentNode>(geometry_document));
        for(unsigned frame=0;frame<6;++frame){view->update();refresh(event);Rev::NativeWindow::pumpEvents();}
        auto* geometry_root=view->children.front();
        const auto matches=[](re::Element* element,re::Element* parent,const auto& expected) {
            return std::abs(element->rect.x-parent->rect.x-expected[0])<1&&std::abs(element->rect.y-parent->rect.y-expected[1])<1&&
                std::abs(element->rect.w-expected[2])<1&&std::abs(element->rect.h-expected[3])<1;
        };
        for(std::size_t index=0;index<test::document_geometry_rows.size();++index)
            if(!matches(geometry_root->children[index],geometry_root,test::document_geometry_rows[index]))
                throw std::runtime_error("Rev smoke: document parent geometry diverged from shared fixture");
        const auto verify_row=[&](int index,const auto& expected) {
            auto* line=geometry_root->children[index];
            for(std::size_t child=0;child<expected.size();++child)if(!matches(line->children[child],line,expected[child]))
                throw std::runtime_error("Rev smoke: document child geometry diverged from shared fixture");
        };
        verify_row(0,test::document_geometry_remainder);verify_row(1,test::document_geometry_margins);
        verify_row(2,test::document_geometry_equal);verify_row(4,test::document_geometry_overflow);
        auto* bitmap=dynamic_cast<BitmapView*>(geometry_root->children[3]);const auto area=bitmap->drawing_rect();
        if(std::abs(area.w-86)>1||std::abs(area.h-36)>1||std::abs(area.x-bitmap->rect.x-7)>1||std::abs(area.y-bitmap->rect.y-7)>1)
            throw std::runtime_error("Rev smoke: document bitmap painted outside its shared padding");
        if(bitmap_probe->requests.empty()||bitmap_probe->requests.back().width!=bitmap->sample_width()||bitmap_probe->requests.back().height!=static_cast<unsigned>(std::round(36*details.scale)))
            throw std::runtime_error("Rev smoke: document bitmap did not request its physical inner framebuffer dimensions");
        if(!empty_probe->requests.empty())throw std::runtime_error("Rev smoke: zero-width document bitmap requested synthetic pixels");
        auto* bordered_bitmap=dynamic_cast<BitmapView*>(geometry_root->children[6]);const auto bordered_area=bordered_bitmap->drawing_rect();
        if(std::abs(bordered_area.w-98)>1||std::abs(bordered_area.h-48)>1||std::abs(bordered_area.x-bordered_bitmap->rect.x-1)>1||std::abs(bordered_area.y-bordered_bitmap->rect.y-1)>1||
           border_probe->requests.empty()||border_probe->requests.back().width!=bordered_bitmap->sample_width()||border_probe->requests.back().height!=static_cast<unsigned>(std::round(48*details.scale)))
            throw std::runtime_error("Rev smoke: opaque bitmap pixels did not preserve the declared border inset");
        auto* wrapped_action=dynamic_cast<re::Button*>(geometry_root->children[5]);
        if(!wrapped_action||wrapped_action->rect.h<=60)throw std::runtime_error("Rev smoke: document action lost native wrapped label measurement");
        ui::DocumentNode wrapped_root;wrapped_root.width=460;
        ui::DocumentNode cards;cards.kind=ui::DocumentKind::row;cards.width=460;cards.equal_height=true;cards.bottom=12;
        for(unsigned index=0;index<2;++index) {
            ui::DocumentNode card;card.width=220;card.padding=10;card.right=index?0:20;
            auto paragraph=document->children.front();paragraph.width=200;
            if(index)for(unsigned repeat=0;repeat<12;++repeat)paragraph.text+=" wrapped native text";
            card.children.push_back(std::move(paragraph));cards.children.push_back(std::move(card));
        }
        wrapped_root.children={cards,document->children.front()};
        const auto verify_wrapped=[&](int width) {
            auto layout=wrapped_root;layout.width=static_cast<float>(width);layout.children[0].width=layout.width;
            for(auto& card:layout.children[0].children) {card.width=static_cast<float>(width-20)/2;card.children[0].width=card.width-20;}
            place(view,{20,200,width,600});view->apply(std::make_shared<const ui::DocumentNode>(std::move(layout)));
            for(unsigned frame=0;frame<6;++frame) {view->update();refresh(event);Rev::NativeWindow::pumpEvents();std::this_thread::sleep_for(std::chrono::milliseconds(4));}
            auto* body=view->children.front();auto* line=body->children.front();auto* following=body->children[1];
            float bottom=0;
            for(auto* card:line->children) {
                if(card->rect.h+1<card->layout.rect.h+card->resolved.pad.t.val+card->resolved.pad.b.val)
                    throw std::runtime_error("Rev smoke: equal-height document card clipped native wrapped content");
                if(std::abs(card->rect.h-line->children.front()->rect.h)>1)
                    throw std::runtime_error("Rev smoke: document row did not equalize native card heights");
                bottom=std::max(bottom,card->rect.y+card->rect.h);
            }
            if(following->rect.y<bottom)throw std::runtime_error("Rev smoke: following document section overlapped wrapped cards");
            return line->children.front()->rect.h;
        };
        const auto wide_height=verify_wrapped(460),narrow_height=verify_wrapped(360),restored_height=verify_wrapped(460);
        if(narrow_height<=wide_height||std::abs(restored_height-wide_height)>1)
            throw std::runtime_error("Rev smoke: wrapped document did not grow and shrink with native width");
        document_parent->style->visibility=Visibility::Hidden;
        for(unsigned frame=0;frame<4;++frame){view->update();refresh(event);Rev::NativeWindow::pumpEvents();}
        document_parent->style->visibility=Visibility::Visible;
        for(unsigned frame=0;frame<6;++frame){view->update();refresh(event);Rev::NativeWindow::pumpEvents();}
        const auto shown_height=view->children.front()->children.front()->children.front()->rect.h;
        if(std::abs(shown_height-wide_height)>1)throw std::runtime_error("Rev smoke: hiding a document parent changed its settled card geometry");
        delete document_parent;verify_record_contract();
    }
    void verify_document_clipping() {
        // Geometry alone cannot detect a leaked native stencil. Exit a deeply
        // clipped branch into several document siblings, then a persistent
        // action outside the document, and inspect the rendered color buffer.
        ui::DocumentNode document;document.width=360;
        ui::DocumentNode row;row.kind=ui::DocumentKind::row;row.width=360;row.height=64;
        ui::DocumentNode deep;deep.width=64;deep.height=64;deep.right=8;deep.fill=ui::DocumentFill::surface;
        for(unsigned level=0;level<4;++level) {
            auto child=deep;child.right=0;deep.children={std::move(child)};
        }
        row.children.push_back(deep);
        for(unsigned index=0;index<3;++index) {
            ui::DocumentNode sibling;sibling.width=64;sibling.height=64;sibling.right=8;sibling.fill=ui::DocumentFill::parity;
            row.children.push_back(std::move(sibling));
        }
        row.children.back().kind=ui::DocumentKind::action;row.children.back().text="Action";
        row.children.back().fill=ui::DocumentFill::alternate;row.children.back().border=true;
        ui::DocumentNode footer;footer.width=360;footer.height=96;footer.fill=ui::DocumentFill::parity;
        document.children={row,footer};
        auto* backdrop=new re::Box(this);place(backdrop,{20,180,360,144});backdrop->style->zIndex=9;backdrop->style->background.color=rgba(0,0,0,1);
        auto* viewport=new re::Box(this);place(viewport,{20,180,360,112});viewport->style->overflow=Overflow::Hide;
        viewport->style->zIndex=10;viewport->style->background.color=rgba(0,0,0,1);
        auto* view=new RevDocumentView(viewport,launch.color,
            [this](re::Element* parent,const BitmapSource& source)->re::Element* {auto* bitmap=new BitmapView(parent,launch.color,[this]{return details.scale;});bitmap->set(source);return bitmap;},{});
        place(view,{0,0,360,112});view->apply(std::make_shared<const ui::DocumentNode>(document));
        auto* outside=new re::Button(this,re::Button::Params::Secondary("Persistent outside document"));
        place(outside,{450,180,260,48});outside->style->zIndex=20;outside->style->background.color=rgba(120,35,75,1);
        for(unsigned frame=0;frame<12;++frame) {view->update();refresh(event);Rev::NativeWindow::pumpEvents();std::this_thread::sleep_for(std::chrono::milliseconds(8));}
        view->update();draw(event);
        const auto pixel=[&](float x,float y) {
            window->makeContextCurrent();GLint previous_framebuffer=0,previous_buffer=0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_framebuffer);glGetIntegerv(GL_READ_BUFFER,&previous_buffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,shared->canvas->frameBuffer->buffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
            std::array<unsigned char,4> value{};
            glReadPixels(static_cast<int>(std::floor(x*details.scale)),window->size.h-1-static_cast<int>(std::floor(y*details.scale)),1,1,GL_RGBA,GL_UNSIGNED_BYTE,value.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(previous_framebuffer));glReadBuffer(static_cast<GLenum>(previous_buffer));
            return value;
        };
        const auto expect=[&](re::Element* target,int red,int green,int blue,const char* failure) {
            const auto value=pixel(target->rect.x+8,target->rect.y+8);
            if(std::abs(static_cast<int>(value[0])-red)>2||std::abs(static_cast<int>(value[1])-green)>2||std::abs(static_cast<int>(value[2])-blue)>2)
                throw std::runtime_error(std::string(failure)+": pixel="+std::to_string(value[0])+","+std::to_string(value[1])+","+std::to_string(value[2])+
                    " position="+std::to_string(target->rect.x)+","+std::to_string(target->rect.y)+" size="+std::to_string(target->rect.w)+","+std::to_string(target->rect.h)+
                    " scale="+std::to_string(details.scale)+" glError="+std::to_string(glGetError()));
        };
        auto* body=view->children.front();auto* native_row=body->children.front();
        for(unsigned frame=0;frame<12;++frame) {
            // Unchanged draws exercise cached native primitives and verify that
            // stencil state cannot survive a frame and hide later controls.
            draw(event);
            expect(native_row->children.front(),theme::surface,theme::surface,theme::surface,"Rev clipping probe: first deep branch did not render");
            for(std::size_t index=1;index+1<native_row->children.size();++index)
                expect(native_row->children[index],theme::grid,theme::grid,theme::grid,"Rev clipping probe: deep branch clipped a later sibling");
            auto* action=native_row->children.back();expect(action,theme::background,theme::background,theme::background,"Rev clipping probe: native action ignored its declared fill");
            const auto border=pixel(action->rect.x+.5f,action->rect.y+8);
            if(border[0]<10||border[0]>theme::grid+2||border[0]!=border[1]||border[1]!=border[2])
                throw std::runtime_error("Rev clipping probe: native action lost its declared border");
            expect(body->children[1],theme::grid,theme::grid,theme::grid,"Rev clipping probe: row clipped the following section");
            expect(outside,120,35,75,"Rev clipping probe: document stencil clipped a persistent outside control");
            const auto below=pixel(viewport->rect.x+40,viewport->rect.y+viewport->rect.h+16);
            if(below[0]>2||below[1]>2||below[2]>2)throw std::runtime_error("Rev clipping probe: overflowing document content escaped its viewport");
        }
        delete outside;delete viewport;delete backdrop;refresh(event);Rev::NativeWindow::pumpEvents();
    }
    void verify_native_resize() {
        const auto initial_page=application.page();
        const auto resize=[&](int width,int height) {
            const auto physical_width=static_cast<int>(std::ceil(width*details.scale)),physical_height=static_cast<int>(std::ceil(height*details.scale));
            window->setSize(physical_width,physical_height);
            // The native setter updates its cached dimensions before X11's
            // ConfigureNotify and therefore may suppress the matching callback.
            // Forward the resize as the adapter constructor does, then render
            // the same resize handler's pending geometry before verifying it.
            onResize(physical_width,physical_height);
            const auto deadline=Clock::now()+std::chrono::seconds(2);
            do {
                Rev::NativeWindow::pumpEvents();
                if(window->size.w==physical_width&&window->size.h==physical_height&&details.size.width==width&&details.size.height==height)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }while(Clock::now()<deadline);
            if(details.size.width!=width||details.size.height!=height)throw std::runtime_error("Rev resize probe: native size change did not arrive; expected logical="+
                std::to_string(width)+","+std::to_string(height)+" native="+std::to_string(physical_width)+","+std::to_string(physical_height)+
                " actual logical="+std::to_string(details.size.width)+","+std::to_string(details.size.height)+" native="+std::to_string(window->size.w)+","+std::to_string(window->size.h)+
                " scale="+std::to_string(details.scale)+" nativeScale="+std::to_string(window->scale));
            apply();scroll_page(1);draw(event);verify_layout();
        };
        for(const auto& page:ui::pages()) {
            select_page(page.id);resize(ui::min_width,ui::min_height);resize(ui::default_width,ui::default_height);
        }
        select_page(initial_page);apply();draw(event);verify_layout();
    }
    void verify_prompt_focus() {
        auto found=std::find_if(bindings.begin(),bindings.end(),[](const auto& binding){return binding.button&&!binding.button->disabled;});
        if(found==bindings.end())throw std::runtime_error("Rev smoke: missing action control");
        auto* button=found->button;focus_control(button);service_probe=true;
        services.enqueue({{0,ui::ServiceKind::prompt,"Native prompt focus probe",""}});process_services();
        if(!prompt||focused_control()!=prompt||allows_input(button))throw std::runtime_error("Rev smoke: prompt did not isolate keyboard focus");
        re::Event key;key.keyboard.tab.id=1;keyDown(key);
        if(!focused_control()||!allows_input(focused_control()))throw std::runtime_error("Rev smoke: Tab escaped the prompt");
        key.keyboard.tab.id=-1;key.keyboard.escape.id=1;key.propagate=true;keyDown(key);apply();
        if(dialog||focused_control()!=button||!allows_input(button))throw std::runtime_error("Rev smoke: prompt cancellation did not restore focus");
        focus_control(nullptr);
    }

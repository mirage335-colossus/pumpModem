// Included inside RevApp only by the native conformance executable.
    void verify_fast_mode_visibility() {
        const auto require=[](bool value,const char* message) {if(!value)throw std::runtime_error(message);};
        const auto settle=[&] {apply();for(unsigned frame=0;frame<3;++frame)native_frame();};
        const auto by_field=[&](ui::Field field) -> Binding& {
            const auto found=std::find_if(bindings.begin(),bindings.end(),[&](const auto& b){return b.control.field==field;});
            if(found==bindings.end())throw std::runtime_error("Rev Fast field was not materialized");
            return *found;
        };
        auto* selector=by_field(ui::Field::fast_mode).choice;
        auto* encryption=by_field(ui::Field::fast_encryption).toggle;
        auto* source=by_field(ui::Field::fast_source).choice;
        require(selector&&encryption&&source&&by_field(ui::Field::fast_text).editor,"Rev Fast native controls missing");
        require(selector->params.options.size()==3&&selector->params.options[0].name=="Robust Modem"&&
            selector->params.options[0].value=="robust"&&selector->params.options[1].name=="Fast Modem"&&
            selector->params.options[1].value=="fast"&&selector->params.value=="robust"&&
            application.field(ui::Field::fast_mode).selected=="robust","Rev modem selector did not default to the declared Robust Modem choice");
        application.toggle(ui::Field::fast_mode,true);
        application.select(ui::Field::fast_mode,"invalid-mode");
        require(application.field(ui::Field::fast_mode).selected=="robust","Rev obsolete toggle or invalid selection changed the modem");
        application.edit(ui::Field::binary,"001");settle();
        const auto resize=[&](int width,int height) {
            const int physical_width=static_cast<int>(std::ceil(width*details.scale));
            const int physical_height=static_cast<int>(std::ceil(height*details.scale));
            window->setSize(physical_width,physical_height);onResize(physical_width,physical_height);
            const auto deadline=Clock::now()+std::chrono::seconds(2);
            do {
                Rev::NativeWindow::pumpEvents();
                if(window->size.w==physical_width&&window->size.h==physical_height&&details.size.width==width&&details.size.height==height)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }while(Clock::now()<deadline);
            require(details.size.width==width&&details.size.height==height,"Rev modem fixture did not reach the requested viewport");
            settle();
        };
        for(const auto size:{std::pair{ui::default_width,ui::default_height},std::pair{ui::min_width,ui::min_height}}) {
            resize(size.first,size.second);
            for(bool fast:{true,false}) {
                re::Event click;selector->params.value=fast?"fast":"robust";selector->onChange(click);settle();
                require(application.field(ui::Field::fast_mode).selected==(fast?"fast":"robust")&&allows_input(selector),
                    "Rev modem selector callback failed or hid itself");
                application.toggle(ui::Field::fast_mode,!fast);
                require(application.field(ui::Field::fast_mode).selected==(fast?"fast":"robust"),"Rev obsolete toggle changed the selected modem");
                if(fast) {
                    require(!encryption->value&&application.enabled(ui::Command::fast_listen)&&source->params.value=="text"&&
                        !by_field(ui::Field::fast_text).element->resolved.hidden&&by_field(ui::Field::fast_file).element->resolved.hidden,
                        "Rev Fast did not default to plain text and keyless receive");
                    unsigned plot_count=0;
                    for(const auto& binding:bindings)if(binding.control.scope==ui::ScreenScope::fast&&binding.control.kind==ui::Kind::bitmap) {
                        ++plot_count;
                        require(binding.bitmap&&!binding.element->resolved.hidden&&binding.bitmap->width&&binding.bitmap->height&&
                            binding.bitmap->video->texture,"Rev Fast signal plot did not materialize and upload pixels");
                        require(binding.label&&binding.caption,"Rev Fast plot omitted native title or caption roles");
                        const auto presentation=application.bitmap(binding.control,binding.bitmap->width);
                        require(!presentation.title.empty()&&!presentation.caption.empty(),"Rev Fast bitmap lost its shared title or idle caption");
                        std::size_t painted=0;
                        binding.bitmap->snapshot.paint(full_bitmap_request(128,48,false,true),[&](unsigned x,unsigned y,PixelBlock pixels) {
                            validate_pixel_block(pixels);
                            require(x+pixels.width<=128&&y+pixels.height<=48,"Rev Fast bitmap painted beyond its requested native dimensions");
                            painted+=static_cast<std::size_t>(pixels.width)*pixels.height;
                        });
                        require(painted==128*48,"Rev Fast idle bitmap did not paint a complete native image");
                    }
                    require(plot_count==3,"Rev Fast did not materialize all three native signal plots");
                    application.edit(ui::Field::fast_text,"Rev fast café\nSecond line");settle();
                    encryption->checkbox->click(click);settle();
                    require(!application.enabled(ui::Command::fast_listen)&&!application.enabled(ui::Command::fast_transmit),
                        "Rev encrypted actions accepted a missing key");
                    encryption->checkbox->click(click);settle();
                    source->params.value="file";source->onChange(click);settle();
                    require(by_field(ui::Field::fast_text).element->resolved.hidden&&!by_field(ui::Field::fast_file).element->resolved.hidden&&
                        application.command_label(ui::Command::fast_transmit)=="Transmit file","Rev file choice did not replace text controls");
                    for(const auto& binding:bindings)if(binding.control.scope==ui::ScreenScope::fast&&binding.bitmap)
                        require(!binding.element->resolved.hidden,"Rev File view hid a Fast signal plot");
                    application.edit(ui::Field::fast_file,"/tmp/rev-fast-source.bin");
                    source->params.value="text";source->onChange(click);settle();
                    require(application.field(ui::Field::fast_text).text=="Rev fast café\nSecond line"&&
                        application.field(ui::Field::fast_file).text=="/tmp/rev-fast-source.bin"&&application.command_label(ui::Command::fast_transmit)=="Transmit text",
                        "Rev source switching discarded independent drafts");
                }
                for(auto& b:bindings) {
                    if(b.control.scope==ui::ScreenScope::fast) {
                        require(b.element->resolved.hidden==!application.control(b.control).visible,"Rev Fast surface did not reconcile conditional control visibility");
                        if(fast&&!b.element->resolved.hidden)require(b.element->rect.x>=0&&b.element->rect.y>=0&&
                            b.element->rect.x+b.element->rect.w<=details.size.width&&b.element->rect.y+b.element->rect.h<=details.size.height,
                            "Rev Fast native control escaped its viewport");
                        else if(!fast&&b.button) {re::Event stale;b.button->click(stale);}
                    } else if(fast&&b.control.scope==ui::ScreenScope::regular)
                        require(b.element->resolved.hidden,"Rev Fast retained a regular native control");
                }
                if(fast)for(const auto& [id,button]:tabs)require(button->resolved.hidden,"Rev Fast retained regular tabs");
                require(application.field(ui::Field::binary).text=="001"&&application.take_services().empty(),
                    "Rev mode switching or stale callbacks changed the regular source or opened a service");
            }
        }
        resize(ui::default_width,ui::default_height);
        re::Event legacy_selection;selector->params.value="legacy";selector->onChange(legacy_selection);settle();
        auto* transcript=by_field(ui::Field::legacy_transcript).editor;
        auto* draft=by_field(ui::Field::legacy_text).editor;
        require(transcript&&draft&&transcript->read_only&&!transcript->editable&&transcript->selectable&&
            !transcript->targetFlags.disabled&&transcript->scroll_content&&draft->editable&&
            !by_field(ui::Field::legacy_transcript).element->resolved.hidden&&by_field(ui::Field::legacy_carrier).editor->content.get()=="1500",
            "Rev Legacy text terminal did not retain selectable read-only received text");
        const auto submit_legacy=[&](bool ctrl,bool shift) {
            re::Event enter;enter.keyboard.enter.id=1;
            enter.keyboard.ctrl.id=ctrl?1:0;enter.keyboard.shift.id=shift?1:0;
            return draft->submit_event(enter);
        };
        require(draft->submit_event&&draft->content.get().empty()&&!submit_legacy(false,false)&&
            !submit_legacy(false,true)&&!submit_legacy(true,true)&&submit_legacy(true,false)&&
            application.field(ui::Field::legacy_text).text.empty(),
            "Rev Legacy draft did not reserve only Ctrl+Enter for transmission");
        transcript->apply("Received text\nSent text");
        require(!transcript->replace("unwanted")&&transcript->content.get()=="Received text\nSent text","Rev Legacy transcript accepted editing");
        for(const auto& binding:bindings)if(binding.control.scope==ui::ScreenScope::regular||binding.control.scope==ui::ScreenScope::fast)
            require(binding.element->resolved.hidden,"Rev Legacy exposed another modem's native controls");
        selector->params.value="robust";selector->onChange(legacy_selection);settle();
    }
    void verify_developer_mode_visibility() {
        const auto require=[](bool condition,const char* message) {if(!condition)throw std::runtime_error(message);};
        const auto settle=[&] {apply();for(unsigned frame=0;frame<3;++frame)native_frame();};
        settle();
        auto toggle_binding=std::find_if(bindings.begin(),bindings.end(),[](const auto& binding) {
            return binding.control.field==ui::Field::developer_mode;
        });
        auto clear_binding=std::find_if(bindings.begin(),bindings.end(),[](const auto& binding) {
            return binding.control.command==ui::Command::clear_received;
        });
        require(toggle_binding!=bindings.end()&&toggle_binding->toggle&&clear_binding!=bindings.end(),
            "Rev developer mode fixture lacks its toggle or Clear received button");
        auto* toggle=toggle_binding->toggle;
        require(!toggle->value&&allows_input(toggle),"Rev developer mode did not start unchecked and available");
        require(toggle->rect.x+toggle->rect.w<clear_binding->element->rect.x&&toggle->rect.y==clear_binding->element->rect.y,
            "Rev developer mode is not immediately left of Clear received");
        const auto frame=[](const re::Element* element) {
            return std::array<float,4>{element->rect.x,element->rect.y,element->rect.w,element->rect.h};
        };
        const auto geometry=[](re::Element* element) {
            return std::array<float,4>{element->style->position.left.val,element->style->position.top.val,
                element->style->size.width.val,element->style->size.height.val};
        };
        std::vector<std::pair<re::Element*,std::array<float,4>>> preserved;
        for(auto& binding:bindings)if(binding.control.persistent)preserved.push_back({binding.element,geometry(binding.element)});
        for(const auto& [id,button]:tabs)preserved.push_back({button,geometry(button)});
        const auto set_mode=[&](bool checked) {
            if(toggle->value!=checked) {re::Event click;toggle->checkbox->click(click);}
            settle();
        };
        set_mode(true);
        std::vector<std::pair<re::Element*,std::array<float,4>>> visible_bounds;
        for(const auto& [element,bounds]:preserved)if(!element->resolved.hidden)visible_bounds.push_back({element,frame(element)});
        set_mode(false);
        for(bool checked:{false,true,false,true,false}) {
            set_mode(checked);
            require(application.field(ui::Field::developer_mode).checked==checked&&allows_input(toggle),
                "Rev developer mode callback failed or hid its own toggle");
            for(const auto& binding:bindings)if(binding.control.developer_only&&binding.control.persistent)
                require(binding.element->resolved.hidden==!checked,
                    "Rev developer mode did not hide/show an entire advanced control in place");
            for(const auto& definition:ui::pages()) {
                auto* button=tabs.at(definition.id);
                require(button->resolved.hidden==(!checked&&definition.developer_only),
                    "Rev developer mode did not refresh native tab visibility without resizing");
                if(button->resolved.hidden) {
                    require(!allows_input(button),"Rev hidden advanced tab retained keyboard input eligibility");
                    re::Event click;button->click(click);
                    require(application.page()==ui::Page::console&&displayed_page==ui::Page::console,
                        "Rev stale native callback selected a hidden advanced tab");
                }
            }
            for(const auto& [element,bounds]:preserved)require(geometry(element)==bounds,
                "Rev toggling developer mode moved an existing native control or tab");
            for(const auto& [element,bounds]:visible_bounds)if(!element->resolved.hidden)require(frame(element)==bounds,
                "Rev developer mode changed the rendered position of a visible control or tab");
        }
        for(const auto& definition:ui::pages())if(definition.developer_only) {
            set_mode(true);re::Event click;tabs.at(definition.id)->click(click);settle();
            require(application.page()==definition.id&&displayed_page==definition.id,
                "Rev enabled advanced tab could not be selected");
            focus_control(tabs.at(definition.id));set_mode(false);
            require(application.page()==ui::Page::console&&displayed_page==ui::Page::console&&
                !pages.at(ui::Page::console)->resolved.hidden&&pages.at(definition.id)->resolved.hidden&&
                !tabs.at(definition.id)->targetFlags.focus,
                "Rev hiding the selected advanced tab did not display Console and clear hidden focus");
        }
        select_page(ui::Page::compression);settle();
        require(application.page()==ui::Page::console&&displayed_page==ui::Page::console,
            "Rev displayed a requested page rejected by the shared application");
    }
    void verify_document_editor_scrolling() {
        auto editor=std::make_unique<Editor>(this,true,8192,platform,true);
        place(editor.get(),{40,280,210,56});editor->style->text.size=11_px;
        const std::string value="zero\none\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nlast";
        editor->apply(value);focus_control(editor.get());
        const auto settle=[&] {for(unsigned frame=0;frame<3;++frame)native_frame();};
        settle();
        if(editor->height<=editor->resolved.getInner(Axis::Vertical)||editor->resolved.scroll.y!=0)
            throw std::runtime_error("Rev document editor did not retain a scrollable multiline extent at its first line");
        re::Event wheel;wheel.mouse.wheel.y=-120;editor->mouseWheel(wheel);settle();
        const float offset=editor->resolved.scroll.y;
        if(wheel.propagate||offset<=0||editor->text->lines.front().rect.y>=editor->rect.y)
            throw std::runtime_error("Rev document editor wheel did not move native glyph geometry");
        settle();
        if(editor->resolved.scroll.y!=offset)
            throw std::runtime_error("Rev document editor repaint undid an intentional wheel scroll");
        const float top=editor->rect.y+editor->resolved.pad.t.val;
        const auto visible=std::find_if(editor->text->lines.begin(),editor->text->lines.end(),[&](const auto& row) {
            return row.rect.y>=top&&row.rect.y+row.rect.h<=editor->rect.y+editor->rect.h-editor->resolved.pad.b.val;
        });
        if(visible==editor->text->lines.end())throw std::runtime_error("Rev document editor has no visible scrolled text line");
        const auto expected=static_cast<int>(visible->start);
        re::Event click;click.mouse.lb.pressTimeDiff=1000;
        click.mouse.pos={visible->rect.x,visible->rect.y+visible->rect.h*.5f};
        editor->mouseDown(click);settle();
        if(editor->cursor!=expected||expected==0)
            throw std::runtime_error("Rev document editor hit testing did not follow the scrolled line: expected="+
                std::to_string(expected)+" actual="+std::to_string(editor->cursor)+" scroll="+
                std::to_string(editor->resolved.scroll.y)+" point="+std::to_string(click.mouse.pos.x)+","+
                std::to_string(click.mouse.pos.y));
        editor->apply(value,1);settle();
        const auto caret_visible=[&] {
            if(editor->line->lines.empty())return false;
            const auto& caret=editor->line->lines.front();
            return caret.points.size()==2&&caret.points.front().x>=editor->rect.x+editor->resolved.pad.l.val-1&&
                caret.points.front().x<=editor->rect.x+editor->rect.w-editor->resolved.pad.r.val+1&&
                caret.points.front().y>=editor->rect.y+editor->resolved.pad.t.val-1&&
                caret.points.back().y<=editor->rect.y+editor->rect.h-editor->resolved.pad.b.val+1;
        };
        if(editor->cursor!=static_cast<int>(value.size())||editor->resolved.scroll.y<=offset||!caret_visible())
            throw std::runtime_error("Rev document editor did not reveal its ending caret with the final line");
        wheel.propagate=true;wheel.mouse.wheel.y=10000;editor->mouseWheel(wheel);settle();
        if(wheel.propagate||editor->resolved.scroll.y!=0)
            throw std::runtime_error("Rev document editor could not scroll back to its first line");
        wheel.propagate=true;editor->mouseWheel(wheel);
        if(!wheel.propagate)throw std::runtime_error("Rev document editor swallowed wheel input at its scroll boundary");
        editor->apply("short");settle();
        if(editor->resolved.scroll.x||editor->resolved.scroll.y)
            throw std::runtime_error("Rev document editor retained scroll beyond shortened content");
        editor->style->text.wrap=Wrap::False;
        const std::string wide(100,'W');editor->apply(wide,2);settle();
        if(editor->resolved.scroll.x<=0||!caret_visible())
            throw std::runtime_error("Rev document editor did not reveal an unwrapped horizontal caret");
        const auto& row=editor->text->lines.front();
        if(editor->getCursorPos({editor->rect.x+editor->resolved.pad.l.val,row.rect.y+row.rect.h*.5f})<=0)
            throw std::runtime_error("Rev document editor horizontal hit testing ignored its scroll offset");
        wheel.propagate=true;wheel.keyboard.shift.id=1;wheel.mouse.wheel.y=10000;editor->mouseWheel(wheel);settle();
        if(wheel.propagate||editor->resolved.scroll.x!=0)
            throw std::runtime_error("Rev document editor Shift+wheel did not reveal its horizontal start");
        focus_control(nullptr);editor.reset();native_frame();
    }
    void verify_launch_command_editor() {
        select_page(ui::Page::planner);
        const auto settle=[&] {for(unsigned frame=0;frame<4;++frame){apply();native_frame();}};
        settle();
        const auto find_host=[&]() -> DocumentControlHost* {
            for(auto* host:document_control_bindings)if(host->control.field==ui::Field::planner_command)return host;
            throw std::runtime_error("Rev launch command has no retained native editor");
        };
        auto* host=find_host();auto* editor=host->bindings.front().editor;
        if(!editor||!editor->multiline||!editor->scroll_content||editor->limit!=8192||
           !host->control.tab_navigation||!allows_input(editor))
            throw std::runtime_error("Rev launch command lacks native multiline scrolling or focus navigation");
        application.edit(ui::Field::message,"e");settle();
        const auto old_rate=application.field(ui::Field::bandwidth).text;
        const auto old_target=application.field(ui::Field::snr).text;
        std::string pasted="--unsupported-setting 1";
        for(unsigned line=0;line<12;++line)pasted+="\n--line "+std::to_string(line);
        focus_control(editor);editor->selectAll();
        if(!editor->replace(pasted))throw std::runtime_error("Rev launch command paste was rejected");
        settle();
        if(find_host()!=host||editor->content.get()!=pasted||
           application.field(ui::Field::planner_command).text!=pasted||editor->resolved.scroll.y<=0)
            throw std::runtime_error("Rev launch command lost its pasted text, host or ending caret scroll");
        editor->cursor=7;editor->selectAnchor=3;editor->selectEnd=7;settle();
        application.dispatch(ui::Command::planner_toggle_details);settle();
        if(find_host()!=host||!editor->targetFlags.focus||editor->cursor!=7||editor->selectAnchor!=3||editor->selectEnd!=7)
            throw std::runtime_error("Rev launch command document refresh lost native selection or focus");
        editor->cursor=editor->selectAnchor=editor->selectEnd=static_cast<int>(pasted.size());
        re::Event enter;enter.keyboard.enter.id=1;editor->keyDown(enter);settle();
        if(editor->content.get()!=pasted+"\n"||application.field(ui::Field::bandwidth).text!=old_rate||
           application.field(ui::Field::snr).text!=old_target||application.field(ui::Field::message).text!="e"||
           application.enabled(ui::Command::cancel))
            throw std::runtime_error("Rev command Enter applied settings, transmitted or failed to insert a newline");
        auto* document=documents.at(ui::Page::planner);const auto& order=document->tab_elements();
        const auto position=std::find(order.begin(),order.end(),host->element());
        if(position==order.end()||std::next(position)==order.end())
            throw std::runtime_error("Rev command editor has no following Load action in native Tab order");
        re::Event tab;tab.keyboard.tab.id=1;keyDown(tab);
        if(!(*std::next(position))->targetFlags.focus)
            throw std::runtime_error("Rev command Tab did not reach its following Load action");
        application.dispatch(ui::Command::planner_load_command);settle();
        if(application.field(ui::Field::planner_command).text!=pasted+"\n"||
           application.field(ui::Field::status).text.find("unsupported-setting")==std::string::npos||
           application.field(ui::Field::bandwidth).text!=old_rate||application.enabled(ui::Command::cancel))
            throw std::runtime_error("Rev command Load did not preserve malformed input and existing settings");
    }
    void verify_inline_document_editor() {
        select_page(ui::Page::planner);
        const auto settle=[&] {
            for(unsigned frame=0;frame<4;++frame){apply();native_frame();}
        };
        settle();
        const auto find_host=[&]() -> DocumentControlHost* {
            for(auto* host:document_control_bindings)if(host->control.field==ui::Field::planner_target)return host;
            throw std::runtime_error("Rev inline document fixture has no retained planner control");
        };
        auto* host=find_host();auto* editor=host->bindings.front().editor;
        auto* presets=host->bindings.front().suggestions;auto* document=documents.at(ui::Page::planner);
        if(!editor||!presets||presets->params.options.size()<3||!allows_input(editor)||
           editor->rect.y<application.page_bounds(details.size.width,details.size.height).y||
           document->tab_elements().empty()||document->tab_elements().front()!=host->element())
            throw std::runtime_error("Rev inline editor/presets did not use the first document control position: editor="+
                std::to_string(editor!=nullptr)+" presets="+std::to_string(presets?presets->params.options.size():0)+
                " input="+std::to_string(editor&&allows_input(editor))+" y="+std::to_string(editor?editor->rect.y:0)+
                " page_y="+std::to_string(application.page_bounds(details.size.width,details.size.height).y)+
                " tab_first="+std::to_string(!document->tab_elements().empty()&&document->tab_elements().front()==host->element()));
        if(std::any_of(bindings.begin(),bindings.end(),[](const auto& binding){return binding.control.field==ui::Field::planner_target;}))
            throw std::runtime_error("Rev document-only editor was duplicated in the persistent desktop");
        show_help(host->control.help,host->bindings.front().element);help_timing.enter(help_now()-2);update_help();native_frame();
        if(help_text->resolved.hidden)throw std::runtime_error("Rev inline popup tooltip fixture did not become visible");
        presets->openMenu();update_help();native_frame();
        if(!presets->open||!help_text->resolved.hidden||help_owner)
            throw std::runtime_error("Rev native preset menu remained obscured by its previous hover tooltip");
        presets->closeMenu();native_frame();
        focus_control(editor);editor->apply("-");editor->changed("-");settle();
        if(find_host()!=host||host->bindings.front().editor!=editor||!editor->targetFlags.focus||
           editor->content.get()!="-"||application.field(ui::Field::planner_target).text!="-")
            throw std::runtime_error("Rev invalid inline prefix replaced the native editor or lost focus/text");
        editor->apply("-18");editor->cursor=2;editor->selectAnchor=1;editor->selectEnd=2;editor->changed("-18");settle();
        if(!editor->targetFlags.focus||editor->cursor!=2||editor->selectAnchor!=1||editor->selectEnd!=2)
            throw std::runtime_error("Rev document replacement changed the inline editor caret or selection");
        re::Event enter;enter.keyboard.enter.id=1;
        if(!editor->submit_event(enter))throw std::runtime_error("Rev inline editor did not retain Enter submission");
        settle();
        if(editor->content.get()!=application.field(ui::Field::planner_target).text||
           !application.field(ui::Field::planner_target).display_text.empty())
            throw std::runtime_error("Rev inline Enter failed to display the accepted target");
        const auto before=editor->content.get();
        const auto option=std::find_if(presets->params.options.begin(),presets->params.options.end(),
            [&](const auto& item){return item.value!=before;});
        if(option==presets->params.options.end())throw std::runtime_error("Rev inline preset fixture has no alternate value");
        presets->params.value=option->value;re::Event picked;presets->onChange(picked);settle();
        if(editor->content.get()==before||!application.field(ui::Field::planner_target).display_text.empty())
            throw std::runtime_error("Rev inline preset did not edit and commit through its normal binding");
        focus_control(editor);re::Event tab;tab.keyboard.tab.id=1;keyDown(tab);
        if(!presets->dropdown->targetFlags.focus)throw std::runtime_error("Rev inline editor Tab order skipped its preset control");
        if(document->tab_elements().size()<3)throw std::runtime_error("Rev inline document lacks the adjacent target actions");
        tab.propagate=true;keyDown(tab);
        if(!document->tab_elements()[1]->targetFlags.focus)throw std::runtime_error("Rev inline preset Tab order skipped the next document action");
        tab.propagate=true;keyDown(tab);
        if(!document->tab_elements()[2]->targetFlags.focus)throw std::runtime_error("Rev document actions did not retain their visual Tab order");
        tab.keyboard.shift.id=1;tab.propagate=true;keyDown(tab);tab.propagate=true;keyDown(tab);
        if(!presets->dropdown->targetFlags.focus)throw std::runtime_error("Rev reverse Tab order skipped the inline preset control");
        focus_control(editor);application.dispatch(ui::Command::planner_toggle_details);settle();
        if(find_host()!=host||!editor->targetFlags.focus)
            throw std::runtime_error("Rev document details replaced the inline editor or stole focus");
        const auto old_y=editor->rect.y;auto* page=pages.at(ui::Page::planner);
        page->resolved.scroll.y=230;shared->layoutDirty=true;settle();
        if(editor->rect.y>=old_y||allows_input(editor)||editor->targetFlags.focus)
            throw std::runtime_error("Rev scrolled-out inline editor remained interactive or focused");
        page->resolved.scroll.y=0;shared->layoutDirty=true;settle();focus_control(editor);
        if(!allows_input(editor)||!editor->targetFlags.focus)
            throw std::runtime_error("Rev scrolling back did not restore the retained inline editor");
        // A visible label must not leave its completely clipped editor active.
        ui::DocumentNode clipped;clipped.height=ui::label_height;
        ui::DocumentNode input;input.kind=ui::DocumentKind::control;input.height=45;input.control=host->control;
        clipped.children.push_back(input);document->apply(std::make_shared<const ui::DocumentNode>(clipped));
        for(unsigned frame=0;frame<3;++frame){document->update();native_frame();}
        if(allows_input(editor)||editor->targetFlags.focus)
            throw std::runtime_error("Rev label-only document clip retained editable input or focus");
        settle();host=find_host();editor=host->bindings.front().editor;focus_control(editor);
        const auto stale_edit=editor->changed;const auto saved=application.field(ui::Field::planner_target).text;
        const auto previous_count=document_control_bindings.size();
        const auto owned_count=static_cast<std::size_t>(std::count_if(document_control_bindings.begin(),document_control_bindings.end(),
            [&](const auto* retained){return retained->element()->parent==document;}));
        auto empty=std::make_shared<const ui::DocumentNode>();
        {
            NativeCallback callback(native_callback_depth);document->apply(empty);
            if(document_control_bindings.size()!=previous_count||allows_input(editor)||editor->targetFlags.focus)
                throw std::runtime_error("Rev document retirement destroyed an active callback or retained input");
        }
        document->apply(empty);
        if(!owned_count||document_control_bindings.size()+owned_count!=previous_count)
            throw std::runtime_error("Rev absent document control was not reclaimed and unregistered");
        stale_edit("-99");
        if(application.field(ui::Field::planner_target).text!=saved)
            throw std::runtime_error("Rev retired document control accepted a queued edit");
        settle();editor=find_host()->bindings.front().editor;focus_control(editor);
        select_page(ui::Page::console);settle();
        if(allows_input(editor)||editor->targetFlags.focus)
            throw std::runtime_error("Rev hidden document page retained inline editor input or focus");
    }
    void native_frame() {
        // refresh queues an OS paint; an immediate pump can observe no event
        // until that request has completed its native round trip. Preserve the
        // probes' frame budgets by awaiting an actual visible surface paint.
        const auto previous=surface->draws;
        const auto deadline=Clock::now()+std::chrono::seconds(2);
        refresh(event);
        while(surface->draws==previous) {
            Rev::NativeWindow::pumpEvents();
            if(surface->draws!=previous)return;
            if(Clock::now()>=deadline)throw std::runtime_error("Rev native probe timed out awaiting its requested paint");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void verify_estimate_warning_colors() {
        test::estimate_warning_fields(application,[&] {
            application.tick();apply();native_frame();std::this_thread::sleep_for(std::chrono::milliseconds(5));
        },[&](ui::Field field,ui::TextTone tone) {
            const auto binding=std::find_if(bindings.begin(),bindings.end(),[&](const auto& item){return item.control.field==field;});
            if(application.field(field).text_tone!=tone||binding==bindings.end()||!binding->label)
                throw std::runtime_error("Rev estimate warning fixture has no matching shared label");
            const auto color=theme::text_rgb(tone,launch.color);
            const Rev::Core::Color expected{color.red/255.0f,color.green/255.0f,color.blue/255.0f,1};
            const auto actual=binding->label->resolved.style.text.color;
            if(actual.r!=expected.r||actual.g!=expected.g||actual.b!=expected.b||actual.a!=expected.a)
                throw std::runtime_error("Rev estimate label ignored or retained a shared warning tone");
        });
    }
    void verify_palette_roles() {
        native_frame();
        const auto matches=[](const auto& color,theme::WidgetRole role,float opacity=1.0f) {
            const auto expected=theme::rev_primitive_color(role,opacity);
            return color.r==expected.r&&color.g==expected.g&&color.b==expected.b&&color.a==expected.a;
        };
        namespace t=re::ControlTheme;
        if(!matches(surface->resolved.style.background.color,theme::WidgetRole::surface_fill)||
           !matches(t::Field.border.color,theme::WidgetRole::border)||
           !matches(t::FieldFocus.border.color,theme::WidgetRole::focus)||
           !matches(t::FieldTextDisabled.text.color,theme::WidgetRole::disabled_text)||
           !matches(t::ButtonSecondaryHover.background.color,theme::WidgetRole::hover)||
           !matches(t::CheckboxChecked.background.color,theme::WidgetRole::checked)||
           !matches(t::CheckboxMark.text.color,theme::WidgetRole::checked_text))
            throw std::runtime_error("Rev native controls bypassed a shared palette role");
        for(auto& binding:bindings)if(binding.toggle) {
            auto& toggle=*binding.toggle;const bool checked=toggle.value.get(),disabled=toggle.targetFlags.disabled;
            const bool parent_disabled=binding.element->targetFlags.disabled;
            binding.element->setDisabled(false);toggle.setDisabled(false);
            for(const bool value:{false,true,false}) {
                toggle.value=value;native_frame();
                if(toggle.check->opacity!=(value?1.0f:0.0f)||
                   !matches(toggle.checkbox->resolved.style.background.color,value?theme::WidgetRole::checked:theme::WidgetRole::canvas))
                    throw std::runtime_error("Rev checkbox paint did not follow the shared false/true/false value sequence");
            }
            toggle.value=true;toggle.setDisabled(true);native_frame();
            const bool themed=matches(toggle.checkbox->resolved.style.background.color,theme::WidgetRole::disabled_background)&&
                matches(toggle.checkbox->resolved.style.border.color,theme::WidgetRole::disabled_border)&&
                matches(toggle.label->resolved.style.text.color,theme::WidgetRole::disabled_text)&&
                matches(toggle.check->resolved.style.text.color,theme::WidgetRole::disabled_text);
            toggle.value=checked;toggle.setDisabled(disabled);binding.element->setDisabled(parent_disabled);native_frame();
            if(!themed)throw std::runtime_error("Rev checked/disabled style ordering bypassed shared palette roles");
            break;
        }
        for(auto& binding:bindings)if(binding.choice) {
            auto& choice=*binding.choice;
            const bool disabled=choice.targetFlags.disabled,parent_disabled=binding.element->targetFlags.disabled;
            choice.setDisabled(false);
            for(const bool parent_state:{false,true,false}) {
                binding.element->setDisabled(parent_state);native_frame();
                if(!matches(choice.dropdown->resolved.style.background.color,parent_state?theme::WidgetRole::disabled_background:theme::WidgetRole::canvas)||
                   !matches(choice.dropdown->resolved.style.border.color,parent_state?theme::WidgetRole::disabled_border:theme::WidgetRole::border)||
                   !matches(choice.dropdownText->resolved.style.text.color,parent_state?theme::WidgetRole::disabled_text:theme::WidgetRole::data)||
                   !matches(choice.dropdownArrow->resolved.style.text.color,parent_state?theme::WidgetRole::disabled_text:theme::WidgetRole::foreground))
                    throw std::runtime_error("Rev native choice failed active/parent-disabled/re-enabled shared palette states");
            }
            choice.setDisabled(disabled);binding.element->setDisabled(parent_disabled);native_frame();
            break;
        }
        {
            struct Sample {re::Element* element;Rev::Core::Color border,text;bool label;};
            std::vector<Sample> samples;
            const auto resolve=[&](auto&& self,re::Element* element,bool capture)->void {
                element->resolveStyle(event);
                const bool label=dynamic_cast<theme::RevText*>(element)!=nullptr;
                if(capture&&(label||dynamic_cast<theme::RevBox*>(element)||dynamic_cast<theme::RevButton*>(element)))
                    samples.push_back({element,element->resolved.style.border.color,element->resolved.style.text.color,label});
                for(auto* child:element->children)self(self,child,capture);
            };
            const bool disabled=surface->targetFlags.disabled;surface->cascadeStyle();resolve(resolve,surface,true);
            surface->setDisabled(true);surface->cascadeStyle();resolve(resolve,surface,false);
            for(const auto& sample:samples)
                if(!matches(sample.element->resolved.style.border.color,theme::WidgetRole::disabled_border)||
                   (sample.label&&!matches(sample.element->resolved.style.text.color,theme::WidgetRole::disabled_text)))
                    throw std::runtime_error("Rev inline captions, records or document decoration bypassed a disabled native ancestor");
            surface->setDisabled(disabled);surface->cascadeStyle();resolve(resolve,surface,false);
            const auto same=[](const auto& a,const auto& b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;};
            for(const auto& sample:samples)
                if(!same(sample.element->resolved.style.border.color,sample.border)||
                   (sample.label&&!same(sample.element->resolved.style.text.color,sample.text)))
                    throw std::runtime_error("Rev re-enabling an ancestor retained transient disabled colors");
            if(samples.empty())throw std::runtime_error("Rev parent-disabled palette fixture lacked native primitives");
        }
        for(auto& binding:bindings)if(binding.editor&&binding.editor->editable) {
            auto& editor=*binding.editor;const auto saved=editor.content.get();
            const auto cursor=editor.cursor,anchor=editor.selectAnchor,end=editor.selectEnd;
            editor.apply("palette");editor.cursor=3;editor.selectAnchor=0;editor.selectEnd=3;
            native_frame();
            bool caret=false,selection=false;
            for(const auto& strip:editor.line->lines) {
                const bool vertical=strip.points.front().x==strip.points.back().x;
                if(vertical)caret=matches(strip.color,theme::WidgetRole::data);
                else selection=matches(strip.color,theme::WidgetRole::selection,theme::text_selection_opacity);
            }
            editor.apply(saved);editor.cursor=cursor;editor.selectAnchor=anchor;editor.selectEnd=end;
            if(!caret||!selection)throw std::runtime_error("Rev native caret/selection retained toolkit colors outside the shared palette: color="+
                std::to_string(launch.color)+" caret="+std::to_string(caret)+" selection="+std::to_string(selection)+
                " strips="+std::to_string(editor.line->lines.size())+" glyph_lines="+std::to_string(editor.text->lines.size())+
                " rect="+std::to_string(editor.rect.w)+","+std::to_string(editor.rect.h));
            return;
        }
        throw std::runtime_error("Rev palette fixture lacked an editor");
    }
    void verify_layout_lifecycle(std::span<ui::Control> controls) {
        auto* editor=bindings[0].editor;auto* heading=bindings[0].label;
        auto* bitmap=bindings[1].bitmap;auto* caption=bindings[1].caption;
        if(!editor||!heading||!bitmap||!caption)throw std::runtime_error("Rev lifecycle fixture lost native presentation roles");
        for(unsigned stage=0;stage<3;++stage) {
            test::layout_lifecycle_stage(controls,stage);apply();
            for(unsigned frame=0;frame<3;++frame)native_frame();
            if(bindings[0].editor!=editor||bindings[0].label!=heading||bindings[1].bitmap!=bitmap||bindings[1].caption!=caption)
                throw std::runtime_error("Rev shared layout update replaced existing native controls");
            for(auto& binding:bindings) {
                const auto geometry=application.control_layout(binding.control,details.size.width,details.size.height,declarations);
                if((binding.label->style->visibility==Visibility::Visible)!=(geometry.has_label&&ui::drawable(geometry.label)))
                    throw std::runtime_error("Rev failed to add or remove a shared heading after native construction");
                if(binding.element->rect.x!=geometry.frame.x||binding.element->rect.y!=geometry.frame.y||
                   binding.element->rect.w!=geometry.frame.w||binding.element->rect.h!=geometry.frame.h)
                    throw std::runtime_error("Rev did not consume changed shared control geometry");
                if(binding.element->style->border.width.val!=(geometry.border?1:0))
                    throw std::runtime_error("Rev retained a stale shared control border");
            }
            const auto editor_geometry=application.control_layout(controls[0],details.size.width,details.size.height,declarations);
            if(editor->rect.h!=editor_geometry.widget.h||editor->style->text.size.val!=controls[0].font_size||heading->style->text.size.val!=controls[0].font_size)
                throw std::runtime_error("Rev retained stale shared editor geometry or typography");
            const auto bitmap_geometry=application.control_layout(controls[1],details.size.width,details.size.height,declarations);
            if(caption->style->zIndex!=(bitmap_geometry.caption_overlay?1:0))throw std::runtime_error("Rev retained a stale caption overlay layer");
        }
    }
    void verify_policy_lifecycle(std::span<ui::Control> controls) {
        auto* input=bindings[0].editor;auto* editor=bindings[1].editor;auto* list=bindings[2].list;
        auto* gestures=bindings[3].element;
        if(!input||!editor||!list||!gestures)throw std::runtime_error("Rev policy lifecycle lost a native primitive");
        unsigned activated=0;list->activate=[&](const std::string&){++activated;};
        std::vector<ui::Command> commands;command_observer=[&](ui::Command command){commands.push_back(command);};
        for(unsigned stage=0;stage<3;++stage) {
            test::policy_lifecycle_stage(controls,stage);apply();
            for(unsigned frame=0;frame<3;++frame)native_frame();
            const bool active=stage==1;
            if(bindings[0].editor!=input||bindings[1].editor!=editor||bindings[2].list!=list||bindings[3].element!=gestures)
                throw std::runtime_error("Rev policy update replaced an existing native control");
            input->apply("");editor->apply("");
            if(input->replace("12345")!=active||editor->replace("12345")!=active)
                throw std::runtime_error("Rev retained editors ignored updated shared byte limits");
            re::Event submit;
            if(!input->submit_event||!editor->submit_event||input->submit_event(submit)!=active||editor->submit_event(submit)!=active)
                throw std::runtime_error("Rev retained editor ignored adding or removing shared submit policy");
            list->apply(test::extension_records());for(unsigned frame=0;frame<3;++frame)native_frame();
            if(list->row_height!=controls[2].list_row_height||list->rows.at("record-a").button->rect.h!=controls[2].list_row_height||
                list->empty->content.get()!=controls[2].empty_text||list->empty->style->text.size.val!=controls[2].font_size)
                throw std::runtime_error("Rev retained list ignored shared row height or empty-state presentation");
            activated=0;re::Event space;space.keyboard.space.id=1;list->rows.at("record-a").button->click(space);
            if(activated!=static_cast<unsigned>(active))throw std::runtime_error("Rev retained list ignored changed activation-on-selection");
            if(active&&list->resolved.scroll.y<1)throw std::runtime_error("Rev retained list ignored newly enabled tail following");
            commands.clear();re::Event pointer;pointer.mouse.lb.id=1;pointer.mouse.pos={8,8};gestures->mouseDown(pointer);
            pointer.propagate=true;pointer.mouse.wheel.y=120;gestures->mouseWheel(pointer);
            if(commands!=(active?std::vector<ui::Command>{controls[3].click,controls[3].wheel_up}:std::vector<ui::Command>{}))
                throw std::runtime_error("Rev retained control ignored adding or removing shared gestures");
            gestures->mouseEnter(pointer);
            if(active&&(help_owner!=gestures||help_text->content.get()!=controls[3].help))
                throw std::runtime_error("Rev retained control ignored newly declared help");
            if(!active&&help_owner==gestures)throw std::runtime_error("Rev retained removed shared help");
        }
        for(bool readonly:{true,false}) {
            test::read_only_stage(controls,readonly);apply();
            for(unsigned frame=0;frame<3;++frame)native_frame();
            input->apply("");editor->apply("");
            if(input->targetFlags.disabled||editor->targetFlags.disabled||input->editable==readonly||editor->editable==readonly||
               input->replace("a")==readonly||editor->replace("a")==readonly)
                throw std::runtime_error("Rev retained editors ignored selectable read-only policy");
        }
        command_observer={};list->activate={};
        auto* toggle=bindings[4].toggle;
        if(!toggle)throw std::runtime_error("Rev unbound-control fixture lost its toggle");
        const auto revision=application.revision();focus_control(toggle->checkbox);
        re::Event enter;enter.keyboard.enter.id=1;keyDown(enter);
        if(!toggle->value.get()||application.revision()!=revision)
            throw std::runtime_error("Rev unbound toggle keyboard input bypassed declaration-aware dispatch");
        focus_control(nullptr);
    }
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
            const auto before_identical=application.revision();
            if(editor.replace(before_rejected.substr(0,1))||application.revision()!=before_identical||editor.cursor!=1||editor.selectAnchor!=0||editor.selectEnd!=1)
                throw std::runtime_error("Rev smoke: identical replacement changed selection or emitted an edit");
            editor.apply("abcd");editor.cursor=2;editor.selectAnchor=1;editor.selectEnd=4;editor.apply("\xf0\x9f\x8c\x8d");
            if(editor.cursor!=0||editor.selectAnchor!=0||editor.selectEnd!=4)
                throw std::runtime_error("Rev smoke: model update retained offsets inside a replacement UTF-8 character");
            editor.targetFlags.focus=false;application.edit(field,saved);apply();
            return;
        }
        throw std::runtime_error("Rev smoke: missing semantic message editor");
    }
    void verify_clipboard_shortcuts() {
        for(const bool multiline:{false,true}) {
            auto source=std::make_unique<Editor>(this,multiline,128,platform);
            auto target=std::make_unique<Editor>(this,multiline,64,platform);
            source->targetFlags.focus=target->targetFlags.focus=true;
            const auto shortcut=[](Editor& editor,const char* key) {
                re::Event event;event.keyboard.ctrl.id=1;event.keyboard.key=key;editor.keyDown(event);
                if(event.propagate)throw std::runtime_error("Rev editor ignored a clipboard shortcut");
                const auto text=editor.content.get();const auto cursor=editor.cursor,anchor=editor.selectAnchor,end=editor.selectEnd;
                re::Event input;input.keyboard.ctrl.id=1;input.keyboard.input=std::string(1,static_cast<char>(key[0]&31));editor.textInput(input);
                if(editor.content.get()!=text||editor.cursor!=cursor||editor.selectAnchor!=anchor||editor.selectEnd!=end||input.propagate)
                    throw std::runtime_error("Rev shortcut text event inserted a control byte or changed the selection");
            };
            const auto wait=[&](auto completed) {
                const auto deadline=Clock::now()+std::chrono::seconds(3);
                while(!completed()&&Clock::now()<deadline) {
                    platform.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                if(!completed())throw std::runtime_error("Rev editor clipboard shortcut timed out");
            };
            unsigned changes=0,errors=0;
            target->changed=[&](std::string){++changes;};target->error=[&](std::string){++errors;};
            const std::string greeting="CQ \xc3\xa9 ";
            target->apply(greeting);target->cursor=target->selectEnd=1;target->selectAnchor=0;
            target->apply(greeting,1);
            if(target->cursor!=static_cast<int>(greeting.size())||target->selectAnchor!=target->cursor||target->selectEnd!=target->cursor||changes||!target->targetFlags.focus)
                throw std::runtime_error("Rev explicit cursor request did not silently select the text end");
            target->cursor=target->selectEnd=1;target->selectAnchor=0;target->apply(greeting,1);
            if(target->cursor!=1||target->selectAnchor!=0||target->selectEnd!=1)
                throw std::runtime_error("Rev repeated cursor request reset a later selection");
            target->apply(greeting+"!",1);
            if(target->cursor!=1||target->selectAnchor!=0||target->selectEnd!=1||changes)
                throw std::runtime_error("Rev ordinary text update replayed a consumed cursor request");
            target->apply(greeting+"!",2);
            if(target->cursor!=static_cast<int>(greeting.size()+1)||target->selectAnchor!=target->cursor||target->selectEnd!=target->cursor)
                throw std::runtime_error("Rev new cursor revision ignored unchanged text");
            target->apply({},3);
            if(target->cursor||target->selectAnchor||target->selectEnd||changes||!target->targetFlags.focus)
                throw std::runtime_error("Rev empty cursor reset retained a selection or changed focus");
            const std::string value=multiline?"caf\xc3\xa9\n\xf0\x9f\x8c\x8d":"01000001 01000010";
            source->apply(value);source->selectAnchor=source->selectEnd=source->cursor=static_cast<int>(value.size());shortcut(*source,"a");
            shortcut(*source,"c");target->apply("replace");target->selectAnchor=0;target->selectEnd=target->cursor=7;
            shortcut(*target,"v");wait([&]{return changes||errors;});
            if(target->content.get()!=value||changes!=1||errors||source->content.get()!=value||source->selectAnchor!=0||source->selectEnd!=static_cast<int>(value.size()))
                throw std::runtime_error("Rev Ctrl+C / Ctrl+V changed selected binary or UTF-8 text or emitted multiple edits");
            source->apply(std::string(65,'1'));source->selectAnchor=0;source->selectEnd=source->cursor=65;shortcut(*source,"c");
            target->selectAnchor=0;target->selectEnd=target->cursor=static_cast<int>(value.size());shortcut(*target,"v");
            wait([&]{return errors>0;});
            if(target->content.get()!=value||changes!=1||errors!=1||target->selectAnchor!=0||target->selectEnd!=static_cast<int>(value.size())||target->cursor!=static_cast<int>(value.size()))
                throw std::runtime_error("Rev rejected keyboard paste changed selected text or emitted an edit");
            re::Event deletion;deletion.keyboard.input=std::string(1,'\x7f');target->textInput(deletion);
            if(target->content.get()!=value||changes!=1||deletion.propagate)
                throw std::runtime_error("Rev Delete text event inserted a control byte");
        }
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
        const auto paint=[&]{for(unsigned frame=0;frame<4;++frame)native_frame();};
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
            throw std::runtime_error("Rev smoke: native list did not use the shared two-unit tail tolerance: scroll="+std::to_string(probe->resolved.scroll.y)+" maximum="+std::to_string(probe->maximum_scroll())+" inner="+std::to_string(probe->resolved.getInner(Axis::Vertical))+" content="+std::to_string(probe->layout.rect.h));
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
        state.records.front().cells={{std::string(500,'W'),8,0,-8,24,13}};probe->apply(state);paint();
        auto& wide_row=probe->rows.at(state.records.front().id);
        if(probe->content_width<=400||wide_row.button->rect.w<probe->content_width-1||wide_row.cells.front()->style->scroll!=Scroll::None)
            throw std::runtime_error("Rev smoke: long record text did not use the shared horizontal list extent");
        probe->resolved.scroll.x=120;paint();
        if(std::abs(probe->resolved.scroll.x-120)>1)
            throw std::runtime_error("Rev smoke: record list could not retain horizontal scroll");
        state.records.front().cells={{"Fixed extent",8,0,900,24,13}};probe->apply(state);paint();
        if(probe->content_width<908||std::abs(probe->resolved.scroll.x-120)>1)
            throw std::runtime_error("Rev smoke: fixed record cell lost extent or horizontal scroll on replacement");
        state.records.front().cells={{std::string(500,'W'),8,0,0,24,13}};probe->apply(state);paint();
        if(probe->content_width<=400)
            throw std::runtime_error("Rev smoke: zero-inset remaining-width record cell clipped long text");
        probe_parent->style->visibility=Visibility::Hidden;paint();probe->apply(state);
        probe_parent->style->visibility=Visibility::Visible;paint();
        if(std::abs(probe->resolved.scroll.x-120)>1)
            throw std::runtime_error("Rev smoke: hidden record updates lost horizontal reading position");
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
        if(!text_binding.editor||!presets||presets->style->visibility!=Visibility::Visible||presets->params.options.size()<2)
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
        for(auto& binding:bindings)if(binding.control.instance>=31&&binding.control.instance<=34) {
            if(binding.control.instance==31 && (!binding.button||binding.element->rect.w!=0||allows_input(binding.button)))
                throw std::runtime_error("Rev smoke: zero-stretch action retained native width or input eligibility");
            if(binding.control.instance==32 && (!binding.editor||binding.editor->rect.w!=0||allows_input(binding.editor)||
                !binding.suggestions||binding.suggestions->rect.w<=0||!allows_input(binding.suggestions)))
                throw std::runtime_error("Rev smoke: empty editor retained input or hid its allocated preset button");
            if(binding.control.instance==34) {
                if(!binding.bitmap||binding.bitmap->rect.w!=0||allows_input(binding.bitmap))
                    throw std::runtime_error("Rev smoke: empty bitmap retained native width or input eligibility");
                auto probe=std::make_shared<test::BitmapProbe>();binding.bitmap->set(test::rectangle_bitmap(probe));
                for(unsigned frame=0;frame<3;++frame)native_frame();
                if(!probe->requests.empty())throw std::runtime_error("Rev smoke: empty bitmap requested synthetic backing pixels");
            }
        }
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
        for(unsigned frame=0;frame<6;++frame){view->update();native_frame();}
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
        const auto sample_height=[&](float top,float height) {
            return static_cast<unsigned>(std::round((top+height)*details.scale)-std::round(top*details.scale));
        };
        const auto expected_height=sample_height(area.y,36);
        if(bitmap_probe->requests.empty()||bitmap_probe->requests.back().width!=bitmap->sample_width()||bitmap_probe->requests.back().height!=expected_height)
            throw std::runtime_error("Rev smoke: document bitmap did not request its physical inner framebuffer dimensions: actual="+
                (bitmap_probe->requests.empty()?std::string("none"):std::to_string(bitmap_probe->requests.back().width)+"x"+std::to_string(bitmap_probe->requests.back().height))+
                " expected="+std::to_string(bitmap->sample_width())+"x"+std::to_string(expected_height)+" scale="+std::to_string(details.scale));
        if(!empty_probe->requests.empty())throw std::runtime_error("Rev smoke: zero-width document bitmap requested synthetic pixels");
        unsigned attempts=0;const auto retained_texture=bitmap->video->texture;
        const auto retry_source=test::rectangle_bitmap(std::make_shared<test::BitmapProbe>());
        bitmap->set(BitmapSource([&](const BitmapRequest& request,const BitmapSink& sink,bool color) {
            if(++attempts==1)throw std::runtime_error("Transient bitmap producer failure");
            retry_source.paint(request,sink,color);
        }));
        bool failed=false;
        try {bitmap->computePrimitives(*shared->event);}catch(const std::runtime_error&) {failed=true;}
        if(!failed||!bitmap->needs_upload||bitmap->video->texture!=retained_texture)
            throw std::runtime_error("Rev smoke: failed bitmap paint discarded the retained frame or pending upload");
        bitmap->computePrimitives(*shared->event);
        if(attempts!=2||bitmap->needs_upload||bitmap->video->texture!=retained_texture)
            throw std::runtime_error("Rev smoke: unchanged bitmap revision/size failed to retry or reuse its texture");
        // Discard the probe's borrowing callable before its local captures end.
        bitmap->set(retry_source);bitmap->computePrimitives(*shared->event);
        auto* bordered_bitmap=dynamic_cast<BitmapView*>(geometry_root->children[6]);const auto bordered_area=bordered_bitmap->drawing_rect();
        if(std::abs(bordered_area.w-98)>1||std::abs(bordered_area.h-48)>1||std::abs(bordered_area.x-bordered_bitmap->rect.x-1)>1||std::abs(bordered_area.y-bordered_bitmap->rect.y-1)>1||
           border_probe->requests.empty()||border_probe->requests.back().width!=bordered_bitmap->sample_width()||border_probe->requests.back().height!=sample_height(bordered_area.y,48))
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
            for(unsigned frame=0;frame<6;++frame) {view->update();native_frame();}
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
        for(unsigned frame=0;frame<4;++frame){view->update();native_frame();}
        document_parent->style->visibility=Visibility::Visible;
        for(unsigned frame=0;frame<6;++frame){view->update();native_frame();}
        const auto shown_height=view->children.front()->children.front()->children.front()->rect.h;
        if(std::abs(shown_height-wide_height)>1)throw std::runtime_error("Rev smoke: hiding a document parent changed its settled card geometry");
        auto action_document=test::document_actions_fixture();
        const auto second_action=[&] {return dynamic_cast<re::Button*>(view->children.front()->children.back()->children.front());};
        view->apply(std::make_shared<const ui::DocumentNode>(action_document));focus_control(second_action());
        const auto calls_before=calls;
        ui::DocumentNode inserted;inserted.kind=ui::DocumentKind::text;inserted.text="Inserted heading";
        action_document.children.insert(action_document.children.begin(),inserted);
        view->apply(std::make_shared<const ui::DocumentNode>(action_document));
        if(!second_action()->targetFlags.focus||calls!=calls_before)
            throw std::runtime_error("Rev smoke: repeated document command focus changed after a shared tree insertion");
        click.propagate=true;second_action()->click(click);
        if(calls!=calls_before+1)throw std::runtime_error("Rev smoke: repeated document action lost dispatch");
        auto first_action=action_document.children[1];action_document.children.erase(action_document.children.begin()+1);
        action_document.children.back().children.push_back(first_action);
        view->apply(std::make_shared<const ui::DocumentNode>(action_document));
        if(!second_action()->targetFlags.focus)throw std::runtime_error("Rev smoke: reordering repeated document actions transferred native focus");
        action_document.children.back().children.pop_back();view->apply(std::make_shared<const ui::DocumentNode>(action_document));
        if(!second_action()->targetFlags.focus)throw std::runtime_error("Rev smoke: removing another document action instance lost native focus");
        action_document.children.back().enabled=false;
        view->apply(std::make_shared<const ui::DocumentNode>(action_document));click.propagate=true;second_action()->click(click);
        if(second_action()->targetFlags.focus||calls!=calls_before+1)
            throw std::runtime_error("Rev smoke: disabled document ancestor retained focus or allowed a command");
        action_document.children.back().enabled=true;view->apply(std::make_shared<const ui::DocumentNode>(action_document));
        focus_control(second_action());view->setDisabled(true);view->apply(std::make_shared<const ui::DocumentNode>(action_document));
        click.propagate=true;second_action()->click(click);
        if(second_action()->targetFlags.focus||calls!=calls_before+1)
            throw std::runtime_error("Rev smoke: disabled native document host retained focus or allowed a command");
        view->setDisabled(false);view->apply(std::make_shared<const ui::DocumentNode>(action_document));focus_control(second_action());
        action_document.children.back().children={first_action};view->apply(std::make_shared<const ui::DocumentNode>(action_document));
        if(second_action()->targetFlags.focus)throw std::runtime_error("Rev smoke: removing a focused document action transferred focus to another instance");
        view->style->visibility=Visibility::Hidden;click.propagate=true;second_action()->click(click);
        if(calls!=calls_before+1)throw std::runtime_error("Rev smoke: hidden document host allowed a stale native action callback");
        view->style->visibility=Visibility::Visible;
        second_action()->setDisabled(true);click.propagate=true;second_action()->click(click);
        if(calls!=calls_before+1)throw std::runtime_error("Rev smoke: disabled native document action dispatched a command");
        auto empty_action_document=test::document_empty_action_fixture();view->apply(std::make_shared<const ui::DocumentNode>(empty_action_document));
        for(unsigned frame=0;frame<3;++frame){view->update();native_frame();}
        focus_control(view->children.front()->children.back());place(view,{20,200,50,600});
        for(unsigned frame=0;frame<3;++frame){view->update();native_frame();}
        auto* empty_action=dynamic_cast<re::Button*>(view->children.front()->children.back());click.propagate=true;empty_action->click(click);
        if(empty_action->rect.w!=0||allows_input(empty_action)||empty_action->targetFlags.focus||calls!=calls_before+1)
            throw std::runtime_error("Rev smoke: empty document action retained native focus or dispatched a command");
        place(view,{20,200,100,600});
        for(unsigned frame=0;frame<3;++frame){view->update();native_frame();}
        if(empty_action->rect.w<=0||!allows_input(empty_action)) {
            std::ostringstream detail;detail<<"Rev smoke: restoring document allocation did not re-enable its retained action";
            detail<<" view="<<view->rect.x<<','<<view->rect.y<<','<<view->rect.w<<','<<view->rect.h
                  <<" pads="<<view->resolved.pad.l.val<<','<<view->resolved.pad.r.val<<','<<view->resolved.pad.t.val<<','<<view->resolved.pad.b.val;
            for(auto* ancestor=static_cast<re::Element*>(empty_action);ancestor;ancestor=ancestor->parent) {
                detail<<" ancestor="<<ancestor->name<<" rect="<<ancestor->rect.x<<','<<ancestor->rect.y<<','<<ancestor->rect.w<<','<<ancestor->rect.h
                      <<" disabled="<<ancestor->targetFlags.disabled<<'/'<<ancestor->resolved.disabled<<" hidden="<<ancestor->resolved.hidden
                      <<" visibility="<<static_cast<int>(ancestor->style->visibility.mode)<<" dirty="<<static_cast<bool>(ancestor->dirty.style);
                if(ancestor==ancestor->parent)break;
            }
            throw std::runtime_error(detail.str());
        }
        empty_action_document.children.back().enabled=false;view->apply(std::make_shared<const ui::DocumentNode>(empty_action_document));
        for(const int width:{50,100}) {
            place(view,{20,200,width,600});
            for(unsigned frame=0;frame<3;++frame){view->update();native_frame();}
        }
        if(allows_input(view->children.front()->children.back()))
            throw std::runtime_error("Rev smoke: restoring document allocation enabled a declared-disabled action");
        auto clipped_document=test::document_clipped_action_fixture();place(view,{20,200,100,600});
        view->apply(std::make_shared<const ui::DocumentNode>(clipped_document));
        for(unsigned frame=0;frame<3;++frame){view->update();native_frame();}
        const auto clipped_action=[&] {return dynamic_cast<re::Button*>(view->children.front()->children.back()->children.front());};
        focus_control(clipped_action());clipped_document.height=20;
        view->apply(std::make_shared<const ui::DocumentNode>(clipped_document));
        for(unsigned frame=0;frame<3;++frame){view->update();native_frame();}
        auto* clipped_button=clipped_action();click.propagate=true;clipped_button->click(click);
        if(clipped_button->rect.w<=0||clipped_button->rect.h<=0||allows_input(clipped_button)||clipped_button->targetFlags.focus||calls!=calls_before+1)
            throw std::runtime_error("Rev smoke: fully clipped document action retained native focus or dispatched a command");
        clipped_document.height=60;view->apply(std::make_shared<const ui::DocumentNode>(clipped_document));
        for(unsigned frame=0;frame<3;++frame){view->update();native_frame();}
        clipped_button=clipped_action();click.propagate=true;clipped_button->click(click);
        if(!allows_input(clipped_button)||calls!=calls_before+2)
            throw std::runtime_error("Rev smoke: restoring the document clip did not restore native action dispatch");
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
        // An unrelated sibling can have a greater numeric paint depth than
        // the clip just left. Its ancestry must still release that clip.
        auto* ancestry=new re::Box(this);place(ancestry,{450,300,260,64});ancestry->style->zIndex=30;
        auto* clipped=new re::Box(ancestry);place(clipped,{0,0,48,48});
        clipped->style->overflow=Overflow::Hide;clipped->style->background.color=rgba(35,75,120,1);
        auto* deeper_sibling=new re::Box(ancestry);place(deeper_sibling,{64,0,64,48});
        deeper_sibling->style->zIndex=-2;deeper_sibling->style->background.color=rgba(75,120,35,1);
        for(unsigned frame=0;frame<12;++frame) {view->update();native_frame();}
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
            if(deeper_sibling->resolved.depth<=clipped->resolved.depth)
                throw std::runtime_error("Rev clipping probe: unrelated branch did not exercise increasing numeric depth");
            expect(clipped,35,75,120,"Rev clipping probe: ancestry fixture did not render");
            expect(deeper_sibling,75,120,35,"Rev clipping probe: unrelated deeper sibling inherited a stale stencil");
            const auto below=pixel(viewport->rect.x+40,viewport->rect.y+viewport->rect.h+16);
            if(below[0]>2||below[1]>2||below[2]>2)throw std::runtime_error("Rev clipping probe: overflowing document content escaped its viewport");
        }
        delete ancestry;delete outside;delete viewport;delete backdrop;native_frame();
    }
    void native_overlay_frame() {
        if(!overlay_view)throw std::runtime_error("Rev fixture has no native overlay");
        const auto draws=overlay_view->element->draws;
        const auto deadline=Clock::now()+std::chrono::seconds(2);refresh(event);
        while(overlay_view->element->draws==draws) {
            Rev::NativeWindow::pumpEvents();
            if(Clock::now()>=deadline)throw std::runtime_error("Rev shared overlay did not render");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    Binding& overlay_binding(unsigned instance) {
        if(overlay_view)for(auto& binding:overlay_view->bindings)if(binding.control.instance==instance)return binding;
        throw std::runtime_error("Rev shared overlay lost a declared control instance");
    }
    void verify_overlay_composition() {
        const auto page=application.page();const auto windows=group->size();auto* native_window=window;
        const auto callsign=application.field(ui::Field::callsign).text;
        const auto brightness=application.field(ui::Field::qr_brightness).selected;
        const bool repeatable=application.field(ui::Field::repeatable).checked;
        const auto fixture=[](unsigned stage=0) {
            auto definition=test::overlay_fixture(stage);
            ui::Control toggle{ui::Kind::toggle,ui::Field::repeatable};toggle.instance=706;toggle.label="Overlay repeatable";
            toggle.placement={.left=268,.bottom=16,.width=200,.height=28,.anchor_bottom=true};
            definition.controls.push_back(toggle);return definition;
        };
        const auto geometry=[&] {
            for(auto& binding:overlay_view->bindings) {
                const auto expected=application.control_layout(binding.control,details.size.width,details.size.height,overlay_view->definition->controls);
                if(binding.element->rect.x!=expected.frame.x||binding.element->rect.y!=expected.frame.y||
                   binding.element->rect.w!=expected.frame.w||binding.element->rect.h!=expected.frame.h)
                    throw std::runtime_error("Rev shared overlay did not consume declared control placement");
            }
            if(group->size()!=windows||window!=native_window)throw std::runtime_error("Rev shared composition created a native window");
        };
        application.show_overlay(fixture());apply();native_overlay_frame();geometry();
        if(overlay_view->element->resolved.style.background.color.a!=1)
            throw std::runtime_error("Rev hidden-background overlay did not use opaque shared canvas color");
        if(!overlay_binding(701).bitmap||!overlay_binding(702).choice||!overlay_binding(703).editor||
           !overlay_binding(704).button||!overlay_binding(705).menu||!overlay_binding(706).toggle||overlay_view->bindings.size()!=6)
            throw std::runtime_error("Rev shared overlay did not use the ordinary control factory and menu grouping");
        auto* choice=overlay_binding(702).choice;
        choice->params.value="normal";re::Event changed;choice->onChange(changed);
        if(application.field(ui::Field::qr_brightness).selected!="normal")throw std::runtime_error("Rev overlay choice did not dispatch its declared field");
        auto* editor=overlay_binding(703).editor;focus_control(editor);
        editor->apply("");editor->cursor=editor->selectAnchor=editor->selectEnd=0;
        re::Event typed;typed.keyboard.input="OVR42";textInput(typed);
        if(application.field(ui::Field::callsign).text!="OVR42")throw std::runtime_error("Rev overlay editor did not receive native text input");
        auto* toggle=overlay_binding(706).toggle;focus_control(toggle->checkbox);
        re::Event space;space.keyboard.space.id=1;keyDown(space);
        if(application.field(ui::Field::repeatable).checked==repeatable)throw std::runtime_error("Rev overlay toggle did not receive keyboard activation");
        focus_control(choice->dropdown);choice->openMenu();native_overlay_frame();
        re::Event escape;escape.keyboard.escape.id=1;keyDown(escape);
        if(choice->open||!application.overlay())throw std::runtime_error("Rev overlay policy bypassed an open native popup's Escape handling");
        const auto generation=application.overlay()->generation;
        std::weak_ptr<const ui::OverlayDefinition> old_definition=application.overlay();
        auto stale_choice=choice->onChange;
        auto stale_menu=overlay_binding(705).menu->onChange;
        auto stale_editor=editor->changed;
        choice->params.value="off";
        // Replacing from within a native callback must not destroy its sender.
        command_observer=[&](ui::Command){application.show_overlay(fixture(1));apply();};
        re::Event close;overlay_binding(704).button->click(close);command_observer={};
        native_overlay_frame();geometry();
        if(application.overlay()->generation==generation||overlay_binding(703).label->content.get()!="Updated callsign"||
           overlay_binding(702).label->content.get()!="Preview brightness"||surface->resolved.hidden||
           overlay_view->element->resolved.style.background.color.a!=0)
            throw std::runtime_error("Rev overlay replacement retained old labels, order, geometry or background policy");
        const auto field_before=application.field(ui::Field::callsign).text;
        const auto choice_before=application.field(ui::Field::qr_brightness).selected;
        re::Event stale;stale_choice(stale);stale_menu(stale);stale_editor("STALE");
        if(old_definition.expired()||application.field(ui::Field::callsign).text!=field_before||
           application.field(ui::Field::qr_brightness).selected!=choice_before)
            throw std::runtime_error("Rev overlay replacement invalidated callback declarations or accepted a retired callback");
        stale_choice={};stale_menu={};stale_editor={};
        if(!old_definition.expired())throw std::runtime_error("Rev retained an obsolete overlay after native callbacks completed");
        focus_control(overlay_binding(703).editor);
        re::Event blocked;blocked.keyboard.input="BLOCKED";textInput(blocked);
        if(application.field(ui::Field::callsign).text!=field_before)throw std::runtime_error("Rev overlay consume policy leaked native text input");
        re::Event remapped;remapped.keyboard.escape.id=1;keyDown(remapped);
        if(!application.overlay())throw std::runtime_error("Rev hard-coded Escape instead of using the shared overlay key map");
        dispatch(ui::Command::dismiss_overlay);
        re::Event tab_click;tabs.at(ui::pages().back().id)->click(tab_click);
        if(!application.overlay()||application.page()!=page)throw std::runtime_error("Rev blocked overlay leaked desktop document or tab actions");
        service_probe=true;services.enqueue({{710,ui::ServiceKind::prompt,"Deferred shared overlay prompt",""}});process_services();
        if(dialog||services.current())throw std::runtime_error("Rev overlay ignored shared service deferral");
        re::Event return_key;return_key.keyboard.enter.id=1;return_key.keyboard.ctrl.id=1;keyDown(return_key);apply();
        if(application.overlay()||overlay_view||!dialog||focused_control()!=prompt)
            throw std::runtime_error("Rev shared key dismissal did not release the deferred service");
        dialog_result=ui::ServiceResult{710,true,{},{}};process_services();native_frame();
        application.show_overlay(fixture());apply();native_overlay_frame();
        editor=overlay_binding(703).editor;focus_control(editor);
        service_probe=true;services.enqueue({{711,ui::ServiceKind::prompt,"Service above shared overlay",""}});process_services();
        native_overlay_frame();
        if(!dialog||focused_control()!=prompt||allows_input(editor)||
           modal->style->zIndex<=overlay_view->element->style->zIndex)
            throw std::runtime_error("Rev shared service priority did not isolate the overlay's controls");
        re::Event cancel;cancel.keyboard.escape.id=1;keyDown(cancel);apply();native_overlay_frame();
        if(dialog||!application.overlay()||focused_control()!=editor||!allows_input(editor))
            throw std::runtime_error("Rev service cancellation did not resume its shared overlay");
        auto* menu=overlay_binding(705).menu;menu->params.value=menu->params.options.back().value;
        re::Event picked;menu->onChange(picked);apply();native_frame();
        if(application.overlay()||overlay_view)throw std::runtime_error("Rev overlay menu did not dispatch its declared close action");
        auto unblocked=fixture();unblocked.policy.hide_background=false;unblocked.policy.block_background=false;
        unblocked.policy.restore_focus=false;unblocked.policy.dismiss_on_page_change=false;
        application.show_overlay(std::move(unblocked));apply();native_overlay_frame();
        const auto next=ui::pages().back().id;re::Event navigate;tabs.at(next)->click(navigate);apply();native_overlay_frame();
        if(application.page()!=next||!application.overlay()||surface->targetFlags.disabled||overlay_view->element->interceptHits)
            throw std::runtime_error("Rev overlay ignored shared background eligibility or page persistence");
        focus_control(overlay_binding(703).editor);dispatch(ui::Command::dismiss_overlay);apply();native_frame();
        if(application.overlay()||focused_control())throw std::runtime_error("Rev overlay ignored disabled focus restoration");
        application.edit(ui::Field::callsign,callsign);application.select(ui::Field::qr_brightness,brightness);
        application.toggle(ui::Field::repeatable,repeatable);select_page(page);apply();native_frame();
    }
    void verify_expanded_bitmap() {
        const auto found=std::find_if(bindings.begin(),bindings.end(),[](const auto& binding){
            return binding.bitmap&&binding.control.click==ui::Command::toggle_qr_expanded;
        });
        if(found==bindings.end())throw std::runtime_error("Rev expansion probe has no declared toggle bitmap");
        const auto initial_page=application.page();
        const auto original_message=application.field(ui::Field::message).text;
        const auto original_brightness=application.field(ui::Field::qr_brightness).selected;
        application.edit(ui::Field::message,"Native QR tooltip hover regression");
        application.select(ui::Field::qr_brightness,"normal");
        std::this_thread::sleep_for(std::chrono::milliseconds(45));application.tick();
        select_page(found->control.page);apply();native_frame();
        auto* original=found->bitmap;const auto original_rect=original->rect;
        const auto input=std::find_if(bindings.begin(),bindings.end(),[&](const auto& binding){
            return binding.editor&&binding.editor->editable&&allows_input(binding.editor);
        });
        if(input==bindings.end())throw std::runtime_error("Rev expansion probe has no active editor");
        focus_control(input->editor);
        auto* original_window=window;
        int original_x=0,original_y=0;window->getClientPos(original_x,original_y);
        const int original_width=window->size.w,original_height=window->size.h;
        const auto group_size=group->size();
        const auto click=[&](float x,float y) {
            int left=0,top=0;window->getClientPos(left,top);
            const auto px=left+static_cast<int>(std::round(x*details.scale));
            const auto py=top+static_cast<int>(std::round(y*details.scale));
            onMouseButton(Rev::NativeWindow::Left,Rev::NativeWindow::Press,px,py);
            onMouseButton(Rev::NativeWindow::Left,Rev::NativeWindow::Release,px,py);
        };
        const auto overlay_bitmap=[&]() -> Binding& {
            if(!overlay_view)throw std::runtime_error("Rev QR expansion has no shared overlay");
            const auto binding=std::find_if(overlay_view->bindings.begin(),overlay_view->bindings.end(),[](const auto& value){return value.bitmap!=nullptr;});
            if(binding==overlay_view->bindings.end())throw std::runtime_error("Rev QR overlay has no declared bitmap control");
            return *binding;
        };
        const auto render_expanded=[&] {
            if(!overlay_view)throw std::runtime_error("Rev declared bitmap click did not expand within the app");
            const auto draws=overlay_bitmap().bitmap->draws;
            const auto deadline=Clock::now()+std::chrono::seconds(2);
            refresh(event);
            while(overlay_bitmap().bitmap->draws==draws) {
                Rev::NativeWindow::pumpEvents();
                if(Clock::now()>=deadline)throw std::runtime_error("Rev expanded bitmap did not paint");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        const auto verify_window=[&](int width,int height) {
            int x=0,y=0;window->getClientPos(x,y);
            if(window!=original_window||group->size()!=group_size||window->size.w!=width||window->size.h!=height||
               x!=original_x||y!=original_y||!details.decorated||details.borderless)
                throw std::runtime_error("Rev bitmap expansion changed the native app window or its frame");
        };
        const auto verify_coverage=[&](int width,int height) {
            render_expanded();verify_window(width,height);
            if(overlay_view->element->parent!=this||overlay_view->element->shared!=shared||overlay_view->element->rect.x!=0||overlay_view->element->rect.y!=0||
               overlay_view->element->rect.w!=details.size.width||overlay_view->element->rect.h!=details.size.height||
               overlay_bitmap().bitmap->width!=static_cast<unsigned>(width)||overlay_bitmap().bitmap->height!=static_cast<unsigned>(height))
                throw std::runtime_error("Rev expanded bitmap does not fill the existing app client area: overlay="+
                    std::to_string(overlay_view->element->rect.x)+","+std::to_string(overlay_view->element->rect.y)+","+
                    std::to_string(overlay_view->element->rect.w)+","+std::to_string(overlay_view->element->rect.h)+
                    " client="+std::to_string(details.size.width)+","+std::to_string(details.size.height)+
                    " bitmap="+std::to_string(overlay_bitmap().bitmap->width)+","+std::to_string(overlay_bitmap().bitmap->height)+
                    " expected="+std::to_string(width)+","+std::to_string(height));
        };
        const auto verify_visible_pixels=[&](const char* phase) {
            auto& binding=overlay_bitmap();auto* bitmap=binding.bitmap;
            BitmapImage expected(bitmap->width,bitmap->height);
            bitmap->snapshot.paint(full_bitmap_request(bitmap->width,bitmap->height,false,true),
                [&](unsigned x,unsigned y,PixelBlock block){expected.blit(x,y,block);},launch.color);
            window->makeContextCurrent();GLint previous_framebuffer=0,previous_buffer=0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_framebuffer);glGetIntegerv(GL_READ_BUFFER,&previous_buffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER,shared->canvas->frameBuffer->buffer);glReadBuffer(GL_COLOR_ATTACHMENT0);
            std::vector<unsigned char> actual(static_cast<std::size_t>(window->size.w)*window->size.h*4);
            glReadPixels(0,0,window->size.w,window->size.h,GL_RGBA,GL_UNSIGNED_BYTE,actual.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER,static_cast<GLuint>(previous_framebuffer));glReadBuffer(static_cast<GLenum>(previous_buffer));
            const auto covered=[&](re::Element* element,unsigned x,unsigned y) {
                return element&&!element->resolved.hidden&&x>=std::floor(element->rect.x*details.scale)&&
                    y>=std::floor(element->rect.y*details.scale)&&x<std::ceil((element->rect.x+element->rect.w)*details.scale)&&
                    y<std::ceil((element->rect.y+element->rect.h)*details.scale);
            };
            const auto& source=expected.pixels();std::size_t checked=0;
            for(unsigned y=0;y<bitmap->height;++y)for(unsigned x=0;x<bitmap->width;++x) {
                if(covered(help_text,x,y)||(binding.caption&&!binding.caption->content.get().empty()&&covered(binding.caption,x,y)))continue;
                const auto target=(static_cast<std::size_t>(window->size.h-1-y)*window->size.w+x)*4;
                const auto sample=(static_cast<std::size_t>(y)*bitmap->width+x)*3;
                for(unsigned channel=0;channel<3;++channel)
                    if(std::abs(static_cast<int>(actual[target+channel])-source[sample+channel])>2)
                        throw std::runtime_error(std::string("Rev expanded QR pixels changed during ")+phase+" at "+
                            std::to_string(x)+","+std::to_string(y)+": expected="+std::to_string(source[sample+channel])+
                            " actual="+std::to_string(actual[target+channel]));
                ++checked;
            }
            if(checked<static_cast<std::size_t>(bitmap->width)*bitmap->height*3/4)
                throw std::runtime_error("Rev expanded QR hover probe did not inspect most of the visible bitmap");
            if(!help_text->resolved.hidden) {
                const auto x=static_cast<unsigned>(std::floor((help_text->rect.x+3)*details.scale));
                const auto y=static_cast<unsigned>(std::floor((help_text->rect.y+3)*details.scale));
                const auto offset=(static_cast<std::size_t>(window->size.h-1-y)*window->size.w+x)*4;
                for(unsigned channel=0;channel<3;++channel)
                    if(std::abs(static_cast<int>(actual[offset+channel])-theme::surface)>2)
                        throw std::runtime_error("Rev expanded QR painted over its tooltip");
            }
        };
        const auto verify_hover=[&] {
            const auto move=[&](float x,float y) {
                int left=0,top=0;window->getClientPos(left,top);
                onCursorPos(left+x*details.scale,top+y*details.scale);render_expanded();
            };
            move(-8,-8);verify_visible_pixels("pointer leave");
            for(unsigned cycle=0;cycle<2;++cycle) {
                move(details.size.width/2,details.size.height/2);
                if(help_owner!=overlay_bitmap().element||help_text->resolved.hidden)
                    throw std::runtime_error("Rev expanded QR native hover did not show its tooltip");
                verify_visible_pixels("tooltip show");
                for(const auto point:std::array<std::array<float,2>,3>{{{.15f,.3f},{.85f,.8f},{.4f,.6f}}}) {
                    move(details.size.width*point[0],details.size.height*point[1]);
                    verify_visible_pixels("pointer motion with tooltip");
                }
                move(-8,-8);
                if(!help_text->resolved.hidden)throw std::runtime_error("Rev expanded QR tooltip remained visible after pointer leave");
                verify_visible_pixels("tooltip hide");
            }
        };
        const auto verify_restore=[&](int width,int height) {
            apply();native_frame();verify_window(width,height);
            const auto geometry=ui::control_layout(found->control,state(found->control),details.size.width,details.size.height,declarations);
            if(overlay_view||application.overlay()||found->bitmap!=original||surface->resolved.hidden||
               original->rect.x!=geometry.widget.x||original->rect.y!=geometry.widget.y||
               original->rect.w!=geometry.widget.w||original->rect.h!=geometry.widget.h)
                throw std::runtime_error("Rev collapse did not restore the original retained bitmap and app layout");
        };
        click(original_rect.x+original_rect.w/2,original_rect.y+original_rect.h/2);apply();
        verify_coverage(original_width,original_height);
        verify_hover();
        onKeyboard(static_cast<int>(Rev::NativeWindow::Key::Tab),Rev::NativeWindow::Press);
        onKeyboard(static_cast<int>(Rev::NativeWindow::Key::Tab),Rev::NativeWindow::Release);
        if(focused_control())throw std::runtime_error("Rev expanded bitmap allowed keyboard focus behind the overlay");
        const auto pixels=[&] {
            BitmapImage image(100,100);
            overlay_bitmap().bitmap->snapshot.paint(full_bitmap_request(100,100,false,true),
                [&](unsigned x,unsigned y,PixelBlock block){image.blit(x,y,block);},launch.color);
            return image.pixels();
        };
        const auto brightness=application.field(ui::Field::qr_brightness).selected;
        application.select(ui::Field::qr_brightness,"normal");
        std::this_thread::sleep_for(std::chrono::milliseconds(45));application.tick();apply();render_expanded();
        const auto normal=pixels();
        application.select(ui::Field::qr_brightness,"dark");
        std::this_thread::sleep_for(std::chrono::milliseconds(45));application.tick();apply();render_expanded();
        if(normal==pixels()||overlay_bitmap().caption->content.get()!=application.bitmap(found->control,100).caption)
            throw std::runtime_error("Rev expanded bitmap did not retain live brightness and caption presentation");
        application.select(ui::Field::qr_brightness,brightness);
        click(details.size.width/2,details.size.height/2);verify_restore(original_width,original_height);
        if(focused_control()!=input->editor)throw std::runtime_error("Rev collapse did not restore editor focus after displaying the overlay");
        {
            const auto text=input->editor->content.get();
            const auto cursor=input->editor->cursor,anchor=input->editor->selectAnchor,end=input->editor->selectEnd;
            re::Event typed;typed.keyboard.input="x";input->editor->textInput(typed);
            if(input->editor->content.get()==text)throw std::runtime_error("Rev restored editor did not accept typing");
            application.edit(input->control,text);input->editor->apply(text);
            input->editor->cursor=cursor;input->editor->selectAnchor=anchor;input->editor->selectEnd=end;
        }
        click(original_rect.x+original_rect.w/2,original_rect.y+original_rect.h/2);apply();render_expanded();
        const int resized_width=static_cast<int>(std::ceil(ui::min_width*details.scale));
        const int resized_height=static_cast<int>(std::ceil(ui::min_height*details.scale));
        window->setSize(resized_width,resized_height);onResize(resized_width,resized_height);
        verify_coverage(resized_width,resized_height);
        verify_hover();
        click(details.size.width/2,details.size.height/2);verify_restore(resized_width,resized_height);
        window->setSize(original_width,original_height);onResize(original_width,original_height);apply();native_frame();
        click(original_rect.x+original_rect.w/2,original_rect.y+original_rect.h/2);apply();render_expanded();
        onKeyboard(static_cast<int>(Rev::NativeWindow::Key::Escape),Rev::NativeWindow::Press);
        onKeyboard(static_cast<int>(Rev::NativeWindow::Key::Escape),Rev::NativeWindow::Release);
        verify_restore(original_width,original_height);
        application.edit(ui::Field::message,original_message);application.select(ui::Field::qr_brightness,original_brightness);
        std::this_thread::sleep_for(std::chrono::milliseconds(45));application.tick();
        select_page(initial_page);apply();native_frame();
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
            apply();scroll_page(1);native_frame();verify_layout();
        };
        for(const auto& page:ui::pages()) {
            select_page(page.id);resize(ui::min_width,ui::min_height);resize(ui::default_width,ui::default_height);
        }
        select_page(initial_page);apply();native_frame();verify_layout();verify_expanded_bitmap();
    }
    void verify_prompt_focus() {
        auto found=std::find_if(bindings.begin(),bindings.end(),[](const auto& binding){return binding.button&&!binding.button->disabled;});
        if(found==bindings.end())throw std::runtime_error("Rev smoke: missing action control");
        auto* button=found->button;focus_control(button);service_probe=true;
        show_help("Shared tooltip geometry must fit measured text within the native viewport.",button);
        native_frame();
        if(help_text->rect.w<=0||help_text->rect.h<=0||help_text->rect.x<0||help_text->rect.y<0||help_text->rect.x+help_text->rect.w>details.size.width||
           help_text->rect.y+help_text->rect.h>details.size.height||help_text->style->text.size.val!=ui::tooltip_font_size)
            throw std::runtime_error("Rev tooltip did not consume shared chrome fitting or typography");
        services.enqueue({{0,ui::ServiceKind::prompt,"Native prompt focus probe",""}});process_services();
        if(!prompt||focused_control()!=prompt||allows_input(button))throw std::runtime_error("Rev smoke: prompt did not isolate keyboard focus");
        native_frame();layout_service_dialog();native_frame();
        const auto presentation=ui::service_dialog({0,ui::ServiceKind::prompt,"Native prompt focus probe",""});
        const auto geometry=ui::service_dialog_layout(presentation,details.size.width,details.size.height,[this](const auto&,int,int width,ui::ServiceTextRole role) {
            return native_text_height(role==ui::ServiceTextRole::title?dialog_title_text:dialog_body_text,width);
        });
        if(dialog_title_text->content.get()!=presentation.title||dialog_accept->labelText->content.get()!=presentation.accept_label||
           dialog_cancel->labelText->content.get()!=presentation.cancel_label||dialog->rect.w!=geometry.frame.w||dialog->rect.h!=geometry.frame.h||
           prompt->rect.x-dialog->rect.x!=geometry.input.x||prompt->rect.y-dialog->rect.y!=geometry.input.y)
            throw std::runtime_error("Rev prompt did not consume the shared chrome presentation and layout");
        re::Event key;key.keyboard.tab.id=1;keyDown(key);
        if(!focused_control()||!allows_input(focused_control()))throw std::runtime_error("Rev smoke: Tab escaped the prompt");
        key.keyboard.tab.id=-1;key.keyboard.escape.id=1;key.propagate=true;keyDown(key);apply();
        if(dialog||focused_control()!=button||!allows_input(button))throw std::runtime_error("Rev smoke: prompt cancellation did not restore focus");
        service_probe=true;services.enqueue({{1,ui::ServiceKind::prompt,"Declared prompt input limit","ab",3}});process_services();
        if(!prompt||prompt->limit!=3||prompt->multiline)throw std::runtime_error("Rev prompt ignored shared input constraints");
        unsigned errors=0;prompt->error=[&](std::string){++errors;};prompt->cursor=prompt->selectAnchor=prompt->selectEnd=2;
        if(prompt->replace("cd")||errors!=1||prompt->content.get()!="ab"||prompt->cursor!=2||prompt->selectAnchor!=2||prompt->selectEnd!=2)
            throw std::runtime_error("Rev prompt changed text or selection before rejecting an over-limit edit");
        if(!prompt->replace("c")||prompt->content.get()!="abc")throw std::runtime_error("Rev prompt rejected an edit within its shared input limit");
        dialog_result=ui::ServiceResult{1,true,{},{}};process_services();
        if(dialog||focused_control()!=button)throw std::runtime_error("Rev limited prompt failed to restore native focus");
        service_probe=true;services.enqueue({{2,ui::ServiceKind::save_file,"Save path","/tmp",1024}});process_services();
        const auto file_presentation=ui::service_dialog({2,ui::ServiceKind::save_file,"Save path","/tmp",1024});
        if(!prompt||!prompt->multiline||prompt->limit!=file_presentation.input.byte_limit||
           dialog_body_text->content.get()!=file_presentation.body||dialog_accept->labelText->content.get()!=file_presentation.accept_label)
            throw std::runtime_error("Rev file prompt did not consume shared path policy and wording");
        dialog_result=ui::ServiceResult{2,true,{},{}};process_services();
        focus_control(nullptr);
    }

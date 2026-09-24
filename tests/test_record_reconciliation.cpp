#include "record_reconciliation.hpp"
#include <iostream>

using namespace datapump::gui::ui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void changes_and_identity() {
    RecordReconciliation model;
    FieldState state;state.records={{"a",{{"First"}},true,true},{"b",{{"Second"}},false,false}};state.selected="a";
    auto change=model.apply(state);
    check(change.added==std::vector<std::string>{"a","b"}&&change.order&&change.selection,"Initial records lost declaration order or selection");
    const auto* first=&model.record("a");
    check(model.selected("a")&&model.enabled("a")&&!model.enabled("b"),"Record presentation did not combine selection and availability");
    check(!model.apply(state),"Unchanged records generated native work");
    std::swap(state.records[0],state.records[1]);state.records.push_back({"c",{{"Third"}},true,true});
    change=model.apply(state);
    check(change.order&&change.added==std::vector<std::string>{"c"}&&change.updated.empty()&& &model.record("a")==first,"Reorder/add replaced retained record identity or changed settled rows");
    state.records[1].cells.push_back({"Added cell"});change=model.apply(state);
    check(change.updated==std::vector<std::string>{"a"}&&!change.order&&model.record("a").cells.size()==2&& &model.record("a")==first,"Changed cells lost stable record identity or precise invalidation");
    state.records.erase(state.records.begin());change=model.apply(state);
    check(change.removed==std::vector<std::string>{"b"}&&model.order()==std::vector<std::string>{"a","c"},"Record removal lost remaining declaration order");
    state.selected="c";change=model.apply(state);
    check(change.selection&&!change.content()&&!change.order&&model.selected("c"),"Selection-only update invalidated record content");
    model.select("a");check(model.selected("a")&&model.apply(state).selection,"Native selection did not participate in later snapshot reconciliation");
    for(bool hidden:{false,true}) {
        state.enabled=hidden;state.visible=!hidden;change=model.apply(state);
        check(change.availability&&!model.enabled("a")&&!model.enabled("c")&&!change.content(),"List availability did not update retained rows independently of content");
    }
    state.records.clear();change=model.apply(state);
    check(change.removed==std::vector<std::string>{"a","c"}&&model.empty(),"Clearing records did not release every native identity");
}
void invalid_snapshot_is_atomic() {
    RecordReconciliation model;FieldState state;state.records={{"",{{"Empty ID is still an identity"}}}};
    model.apply(state);const auto* retained=&model.record("");
    state.records.push_back(state.records.front());state.records[0].cells[0].text="must not apply";
    bool rejected=false;try {model.apply(state);}catch(const std::invalid_argument&){rejected=true;}
    check(rejected&&model.size()==1&& &model.record("")==retained&&model.record("").cells[0].text=="Empty ID is still an identity","Duplicate IDs partially changed the retained model");
}
}
int main() {
    try {changes_and_identity();invalid_snapshot_is_atomic();std::cout<<"Shared record reconciliation passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

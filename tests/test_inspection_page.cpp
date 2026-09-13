#include "../src/gui/inspection_page.hpp"
#include <iostream>

using namespace datapump;
using namespace datapump::gui;
namespace document=datapump::gui::inspection_page;
namespace {
void check(bool value,const char* message) {if(!value)throw Error(message);}
void flatten(const document::Node& node,std::vector<const document::Node*>& nodes) {
    nodes.push_back(&node);for(const auto& child:node.children)flatten(child,nodes);
}
std::size_t find(const std::vector<const document::Node*>& nodes,const std::string& label) {
    for(std::size_t i=0;i<nodes.size();++i)if(nodes[i]->text==label||nodes[i]->plot_name==label)return i;
    throw Error("missing inspection node: "+label);
}
Inspection fixture() {
    Inspection model;model.title="Configured model";model.summary="Summary";
    model.lanes={{"Transmit",{{"Packet","Packet details",InspectionState::active},{"Encryption","Encryption details",InspectionState::off},{"Coding","Coding details",InspectionState::active},{"Modulation","Modulation details",InspectionState::active},{"Audio","Audio details",InspectionState::unavailable}}}};
    model.constellations={{"Payload APSK","Ideal payload points",{{1,0},{-1,0}}}};
    inspection::PatternSpace pattern;pattern.code.resize(133,1);pattern.chip_weights.resize(133,1);
    pattern.coefficients={{1,0},{-1,0}};pattern.chip_samples=1;pattern.symbol_samples=133;pattern.symbol_seconds=1;
    pattern.unused_pattern=inspection::PatternEvidence{"unused",std::vector<int>(133,-1),-.5,.75,1};
    model.pattern_space=pattern;model.preamble_description="Preamble description";model.chip_description="Chip description";
    model.sections={{"Physical","Physical detail",24,std::nullopt,std::nullopt,false,false},
                    {"Logical","Logical detail",12,std::nullopt,std::nullopt,true,false},
                    {"Coding note","Coding detail",std::nullopt,std::nullopt,std::nullopt,false,true}};
    model.fields={{"Carrier","1500 Hz"},{"Modem mode","Differential phase"}};
    model.packet_layout=PacketLayout{};model.packet_layout->header_bytes=16;model.packet_layout->header_parity_bytes=8;
    model.packet_layout->block_count=2;model.packet_layout->block_capacity=192;model.packet_layout->full_block_parity=32;
    model.packet_layout->last_block_data=48;model.packet_layout->last_block_parity=16;
    return model;
}
void semantic_order_and_geometry() {
    const auto model=fixture();const auto flow=document::build(&model,true,900,70);
    check(flow.page_size==56&&flow.first==56,"responsive chip paging must be aligned and bounded");
    std::vector<const document::Node*> nodes;flatten(flow.root,nodes);
    check(find(nodes,"Transmit")<find(nodes,"Chosen phase / amplitude alphabets"),"lanes must precede alphabets");
    check(find(nodes,"constellation/0")<find(nodes,"Full pattern / scrambler symbol space"),"alphabets must precede pattern inspection");
    check(find(nodes,"Next")<find(nodes,"pattern/chips"),"chip navigation must precede chip plot");
    check(find(nodes,"pattern/distances")<find(nodes,"pattern/evidence"),"distance section must precede evidence");
    check(find(nodes,"pattern/evidence")<find(nodes,"Preamble symbols"),"closing notes must follow pattern inspection");
    check(nodes[find(nodes,"constellation/0")]->width==nodes[find(nodes,"constellation/0")]->height,"constellation must use square logical plot geometry");
    check(nodes[find(nodes,"pattern/distances")]->width==nodes[find(nodes,"pattern/distances")]->height,"distance matrix must use square logical plot geometry");
    check(nodes[find(nodes,"pattern/chips")]->height==36,"chip rows must retain their logical height");
    check(nodes[find(nodes,"Transmit")+1]->children.size()==3,"900px flow must use three step cards per row");
    const auto narrow=document::build(&model,true,300);nodes.clear();flatten(narrow.root,nodes);
    check(nodes[find(nodes,"Transmit")+1]->children.size()==1,"narrow flow must stack cards");
    check(narrow.page_size==13,"narrow chip paging must follow available width");
    const auto end=document::build(&model,true,900,99999);check(end.first==112,"last chip window must clamp to the final partial page");
    nodes.clear();flatten(end.root,nodes);check(!nodes[find(nodes,"Next")]->enabled&&!nodes[find(nodes,"Last")]->enabled,"end-page actions must be disabled");
}
void transmission_and_pending() {
    const auto model=fixture();const auto page=document::build(&model,false,900);
    std::vector<const document::Node*> nodes;flatten(page.root,nodes);
    check(find(nodes,"Physical")<find(nodes,"Packet before body interleaving"),"physical structure must appear first");
    check(find(nodes,"Logical")<find(nodes,"Reed-Solomon codewords"),"logical structure must precede coding diagrams");
    check(find(nodes,"Final body block")<find(nodes,"Coding note"),"codeword bars must precede coding notes");
    check(find(nodes,"Preamble and coding structure")<find(nodes,"Current packet and modem parameters"),"parameter table must appear after structural notes");
    check(nodes.back()->text=="Differential phase","parameter fields must be last");
    const auto data=nodes[find(nodes,"16 data bytes")],parity=nodes[find(nodes,"8 parity bytes")];
    check(data->width==2*parity->width&&data->width+parity->width==900,"codeword blocks must be proportional to byte counts");
    const auto pending=document::build(nullptr,true,900,0,"Estimate pending");
    check(pending.root.children.size()==2&&pending.root.children.back().text=="Estimate pending","pending state must keep page title and native message");
}
}
int main() {
    try {semantic_order_and_geometry();transmission_and_pending();std::cout<<"inspection page tests passed\n";return 0;}
    catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}

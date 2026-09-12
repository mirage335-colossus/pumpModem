#include "ui_contract.hpp"
namespace datapump::gui::ui {
const std::vector<Control>& inspection_screen() {
    static const std::vector<Control> controls{
        {Kind::label,Field::inspection,Command::none,Bitmap::none,Page::flow,0,"Modem flow"},
        {Kind::label,Field::flow_detail,Command::none,Bitmap::none,Page::flow,1,"Processing lanes"},
        {Kind::bitmap,Field::count,Command::none,Bitmap::pattern,Page::flow,2,"Pattern space"},
        {Kind::action,Field::count,Command::pattern_first,Bitmap::none,Page::flow,3,"First"},
        {Kind::action,Field::count,Command::pattern_previous,Bitmap::none,Page::flow,3,"Previous"},
        {Kind::action,Field::count,Command::pattern_next,Bitmap::none,Page::flow,3,"Next"},
        {Kind::action,Field::count,Command::pattern_last,Bitmap::none,Page::flow,3,"Last"},
        {Kind::bitmap,Field::count,Command::none,Bitmap::pattern_distances,Page::flow,4,"Symbol distance matrix"},
        {Kind::bitmap,Field::count,Command::none,Bitmap::pattern_evidence,Page::flow,4,"Pattern correlation / residual evidence"},
        {Kind::bitmap,Field::payload_alphabet,Command::none,Bitmap::payload_alphabet,Page::flow,5,"Payload APSK alphabet"},
        {Kind::bitmap,Field::reference_alphabet,Command::none,Bitmap::reference_alphabet,Page::flow,5,"Training / final subset alphabet"},
        {Kind::label,Field::inspection,Command::none,Bitmap::none,Page::transmission,0,"Transmission layout"},
        {Kind::label,Field::transmission_detail,Command::none,Bitmap::none,Page::transmission,1,"Fields and sections"}
    };
    return controls;
}
}

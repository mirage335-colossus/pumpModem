#pragma once
#include "inspection_model.hpp"
#include <FL/Fl_Widget.H>
#include <memory>
#include <string>

namespace datapump::gui::widgets {
// Native, scrollable vector diagrams. The model contains only descriptions and
// counts; message contents and key material never reach the drawing layer.
class InspectionDiagram : public Fl_Widget {
public:
    explicit InspectionDiagram(bool flow);
    void set_model(std::shared_ptr<const Inspection> model);
    void set_pending(std::string message);
    int content_height(int width) const;
private:
    void draw() override;
    int render(int width, bool paint) const;
    bool flow_;
    std::shared_ptr<const Inspection> model_;
    std::string pending_="Calculating proposed transmission...";
};
}

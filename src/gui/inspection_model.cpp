#include "inspection_model.hpp"

namespace datapump::gui {
Inspection inspect(const InspectionRequest& request,std::stop_token) {
    Inspection result;
    result.title="Robust transmission inspection";
    result.summary="Detailed transmission inspection is not yet connected to the native Robust session. Live pattern evidence and steps show receiver observations.";
    result.binary=request.binary.has_value();
    result.preview_only=request.binary?request.binary->empty():
        request.message.kind==MessageKind::text&&request.message.data.empty();
    result.lanes.push_back({"Robust modem",{{"Detailed inspection",result.summary,InspectionState::unavailable}}});
    result.preamble_description=result.summary;
    result.chip_description=result.summary;
    return result;
}
}

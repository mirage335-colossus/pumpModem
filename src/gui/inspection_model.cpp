#include "inspection_model.hpp"

namespace datapump::gui {
Inspection inspect(const InspectionRequest& request,std::stop_token) {
    Inspection result;
    result.title="Robust transmission inspection";
    result.summary="Unavailable until the triad backend is connected.";
    result.binary=request.binary.has_value();
    result.preview_only=request.binary?request.binary->empty():
        request.message.kind==MessageKind::text&&request.message.data.empty();
    result.lanes.push_back({"Robust modem",{{"Triad backend",result.summary,InspectionState::unavailable}}});
    result.preamble_description=result.summary;
    result.chip_description=result.summary;
    return result;
}
}

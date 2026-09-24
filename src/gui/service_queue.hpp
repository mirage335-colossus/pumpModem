#pragma once
#include "text_policy.hpp"
#include <deque>
#include <optional>
#include <utility>

namespace datapump::gui::ui {
struct ServiceInputPolicy {bool multiline=false;std::size_t byte_limit=ServiceRequest{}.byte_limit;};
inline std::optional<ServiceInputPolicy> service_input_policy(const ServiceRequest& request) {
    switch(request.kind) {
    case ServiceKind::prompt:return ServiceInputPolicy{false,request.byte_limit};
    case ServiceKind::open_file:case ServiceKind::save_file:return ServiceInputPolicy{true,request.byte_limit};
    case ServiceKind::clipboard:case ServiceKind::open_folder:return std::nullopt;
    }
    return std::nullopt;
}
inline std::string service_input_error(const ServiceRequest& request,std::string_view value) {
    if(const auto input=service_input_policy(request))return edit_error(value,input->multiline,input->byte_limit);
    return {};
}
// One in-flight platform request per adapter. Queue order, reply identity and
// input validation/shutdown belong to the shared contract; dialogs and OS calls
// remain native. Even native selectors that cannot constrain editing validate
// their final result through the same request policy before releasing a reply.
class ServiceQueue {
public:
    void enqueue(std::vector<ServiceRequest> requests) {
        if(closed_)return;
        for(auto& request:requests)pending_.push_back(std::move(request));
    }
    void synchronize(std::vector<ServiceRequest> requests,bool closing) {
        if(closing)cancel();else enqueue(std::move(requests));
    }
    const ServiceRequest* current() const {return current_?&*current_:nullptr;}
    const ServiceRequest* next() {
        if(!current_&&!pending_.empty()) {
            current_=std::move(pending_.front());pending_.pop_front();
        }
        return current();
    }
    bool complete(ServiceResult& result) {
        if(!current_||current_->id!=result.id)return false;
        if(!result.cancelled&&result.error.empty()) {
            result.error=service_input_error(*current_,result.value);
            if(!result.error.empty())result.value.clear();
        }
        current_.reset();return true;
    }
    void cancel() {closed_=true;pending_.clear();current_.reset();}
    bool closed() const {return closed_;}
private:
    bool closed_=false;
    std::deque<ServiceRequest> pending_;
    std::optional<ServiceRequest> current_;
};
}

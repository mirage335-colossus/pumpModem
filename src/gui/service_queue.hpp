#pragma once
#include "ui_contract.hpp"
#include <deque>
#include <optional>
#include <utility>

namespace datapump::gui::ui {
// One in-flight platform request per adapter. Queue order, reply identity and
// shutdown belong to the shared contract; dialogs and OS calls remain native.
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
    bool complete(std::uint64_t id) {
        if(!current_||current_->id!=id)return false;
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

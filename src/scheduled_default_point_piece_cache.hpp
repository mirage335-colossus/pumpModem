#pragma once
#include "scheduled_sparse_owned_spans.hpp"
#include "scheduled_default_linear_runtime.hpp"
#include <optional>
namespace datapump::modem::detail::scheduled_default_point_observation {
// One copied public Piece, no Event/Context borrow. Original direct addback may
// visit fragment->lane order without rebuilding an entire lane's Piece table.
class PieceCache {
    std::optional<scheduled_original_partition::Piece> piece_;
    scheduled_original_partition::Identity identity_{};
    long double origin_=0,rate_=0;
    std::uint64_t begin_=0,end_=0;
public:
    std::uint64_t fence_lookups=0,piece_builds=0;
    template<class Mapping,class Fences>
    scheduled_original_partition::Evaluation evaluate(const scheduled_original_partition::Event& event,
            std::uint64_t row,Mapping& map,Fences&& fences,std::stop_token stop={}) {
        namespace original=scheduled_original_partition;namespace runtime=scheduled_default_linear_runtime;
        original::check_stop(stop);
        if(!runtime::valid_event(event)||row<event.begin||row>=event.end||row<event.history_first||row>=event.capture_end)
            throw Error("Default point direct Original row unavailable");
        if(piece_&&identity_==event.identity&&origin_==event.origin&&rate_==event.rate&&begin_==event.begin&&end_==event.end&&
           row>=piece_->first&&row<piece_->end)return runtime::evaluate_row(event,*piece_,row);
        piece_.reset();++fence_lookups;bool found=false;
        fences(row,row+1,[&](const auto& raw) {
            original::check_stop(stop);
            if(found||raw.original_count>UINT64_MAX-raw.original_first||raw.original_first>row||
               row>=raw.original_first+raw.original_count)throw Error("Default point direct processing fence invalid");
            found=true;auto cursor=std::max(event.begin,raw.original_first);
            const auto finish=std::min(event.end,raw.original_first+raw.original_count);
            const original::ProcessingFence fence{raw.original_first+1,event.identity.capture,cursor,finish};
            for(std::size_t n=0;cursor<=row;++n) {
                original::check_stop(stop);if(n==8192)throw Error("Default point direct Piece cap");
                auto piece=runtime::make_runtime_piece(event,fence,cursor,map,stop);++piece_builds;
                cursor=piece.end;if(row<piece.end){piece_=std::move(piece);break;}
            }
        });
        if(!found||!piece_)throw Error("Default point direct processing metadata missing");
        identity_=event.identity;origin_=event.origin;rate_=event.rate;begin_=event.begin;end_=event.end;
        return runtime::evaluate_row(event,*piece_,row);
    }
};
} // namespace datapump::modem::detail::scheduled_default_point_observation

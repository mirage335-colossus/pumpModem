#pragma once
#include "../ui_contract.hpp"
#include "../text_policy.hpp"
#include "datapump/legacy/session.hpp"
#include <algorithm>
#include <string>
namespace datapump::gui::legacy_ui {
// Session event serials preserve RX/TX order and survive bounded history trims.
// Draft progress is local bookkeeping: it never feeds any receiver or codec.
class TextPresentation {
public:
    static constexpr std::size_t transcript_limit=65536;
    void submitted(std::string text,std::uint64_t transmission) {
        submitted_=std::move(text);transmission_=transmission;sent_=0;clear_prefix_=true;
    }
    void edited(std::string_view draft) {
        if(!draft.starts_with(std::string_view(submitted_).substr(sent_)))clear_prefix_=false;
    }
    bool update(const legacy::Snapshot& snapshot,ui::FieldState& transcript,ui::FieldState& draft) {
        bool changed=false;
        for(const auto& event:snapshot.events)if(event.serial>event_) {
            // Text may arrive one UTF-8 byte at a time. Keep incomplete sequences
            // until the next event so every native presentation stays valid.
            pending_+=event.text;event_=event.serial;
            std::size_t consumed=0;
            while(consumed<pending_.size()) {
                const auto first=static_cast<unsigned char>(pending_[consumed]);
                const std::size_t length=first<0x80?1:first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
                bool malformed=length==0;
                for(std::size_t n=1;n<std::min(length,pending_.size()-consumed);++n) {
                    const auto byte=static_cast<unsigned char>(pending_[consumed+n]);
                    if((byte&0xc0)!=0x80||(n==1&&((first==0xe0&&byte<0xa0)||(first==0xed&&byte>=0xa0)||
                        (first==0xf0&&byte<0x90)||(first==0xf4&&byte>=0x90))))malformed=true;
                }
                if(malformed) {transcript.text+="\xEF\xBF\xBD";++consumed;changed=true;continue;}
                if(pending_.size()-consumed<length)break;
                const auto piece=std::string_view(pending_).substr(consumed,length);
                if(ui::edit_error(piece,true,4).empty())transcript.text+=piece;
                else if(first)transcript.text+="\xEF\xBF\xBD";
                consumed+=length;changed=true;
            }
            pending_.erase(0,consumed);
        }
        if(transcript.text.size()>transcript_limit) {
            auto remove=transcript.text.size()-transcript_limit;
            while(remove<transcript.text.size()&&(static_cast<unsigned char>(transcript.text[remove])&0xc0)==0x80)++remove;
            transcript.text.erase(0,remove);
        }
        if(changed)++transcript.text_cursor_end_revision;
        if(snapshot.transmission==transmission_) {
            auto next=std::min(snapshot.sent_bytes,submitted_.size());
            // A progress callback can split a UTF-8 sequence; never install an
            // invalid native draft while waiting for its final source byte.
            next=static_cast<std::size_t>(ui::text_boundary(submitted_,static_cast<int>(next)));
            if(next>sent_) {
                if(clear_prefix_&&draft.text.starts_with(std::string_view(submitted_).substr(sent_))) {
                    draft.text.erase(0,next-sent_);changed=true;
                } else clear_prefix_=false;
                sent_=next;
            }
        }
        return changed;
    }
private:
    std::uint64_t event_=0,transmission_=0;
    std::size_t sent_=0;
    bool clear_prefix_=true;
    std::string submitted_,pending_;
};
}

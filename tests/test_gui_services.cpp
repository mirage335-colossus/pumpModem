#include "service_queue.hpp"
#include <iostream>
#include <stdexcept>

using namespace datapump::gui::ui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
bool complete(ServiceQueue& queue,std::uint64_t id) {
    ServiceResult result{id,false,"valid",{}};return queue.complete(result);
}
void serial_requests() {
    ServiceQueue queue;
    queue.enqueue({{0,ServiceKind::prompt,"Prompt","original"},
        {2,ServiceKind::save_file,"Save","retained.bin"}});
    const auto* first=queue.next();
    check(first&&first->id==0,"First request or zero-valued identity was lost");
    queue.synchronize({{3,ServiceKind::clipboard,"Copy","retained text"}},false);
    check(queue.next()==first&&first->value=="original","Polling or adding work replaced the active request");
    check(!complete(queue,2)&&queue.current()==first,"Out-of-order reply released the active dialog");
    check(complete(queue,0)&&!complete(queue,0),"A completed dialog accepted a duplicate reply");
    check(queue.next()->id==2&&queue.current()->value=="retained.bin","Save payload/order changed while another dialog was open");
    check(!complete(queue,0)&&complete(queue,2),"Stale dialog reply completed a later request");
    check(queue.next()->id==3&&queue.current()->value=="retained text","Clipboard payload was not retained until dispatch");
    check(complete(queue,3)&&!queue.next(),"Completed queue repeated a platform operation");
}
void input_policy() {
    for(auto kind:{ServiceKind::prompt,ServiceKind::open_file,ServiceKind::save_file}) {
        ServiceRequest request{12,kind,"Shared input","",5};
        const auto reply=[&](std::string value,bool cancelled=false,std::string error={}) {
            ServiceQueue queue;queue.enqueue({request});queue.next();
            ServiceResult result{12,cancelled,std::move(value),std::move(error)};
            check(queue.complete(result)&&!queue.current(),"Validated service reply did not release its request");
            return result;
        };
        const auto valid=reply("caf\xc3\xa9");
        check(valid.error.empty()&&valid.value=="caf\xc3\xa9","Valid bounded UTF-8 service input changed");
        for(const auto& invalid:{std::string("123456"),std::string("\xc3",1),std::string("a\0b",3)}) {
            const auto result=reply(invalid);
            check(!result.error.empty()&&result.value.empty(),"Native service input bypassed shared byte/UTF-8 validation");
        }
        const auto newline=reply("a\nb");
        check((kind==ServiceKind::prompt?!newline.error.empty():newline.error.empty()),"Prompt and OS filename line-break policies were confused");
        const auto cancelled=reply("123456",true);
        check(cancelled.cancelled&&cancelled.error.empty(),"Cancellation was replaced by input validation failure");
        check(reply("123456",false,"Native failure").error=="Native failure","Input validation hid a native error");
        request.byte_limit=40000;
        check(reply(std::string(33000,'a')).error.empty(),"Service adapter retained a hardcoded input limit");
        request.byte_limit=0;
        check(reply("").error.empty()&&!reply("x").error.empty(),"Zero-byte service input limit was not honored");
    }
    ServiceQueue queue;queue.enqueue({{7,ServiceKind::prompt,"Current","",3}});queue.next();
    ServiceResult stale{8,false,"too long",{}};
    check(!queue.complete(stale)&&stale.value=="too long"&&stale.error.empty()&&queue.current(),"Stale reply applied another request's validation policy");
    for(auto kind:{ServiceKind::clipboard,ServiceKind::open_folder})
        check(service_input_error({1,kind,"","",0},"not a text reply").empty(),"Output-only platform service acquired text-input policy");
}
void shutdown() {
    for(bool open_dialog:{false,true}) {
        ServiceQueue queue;
        queue.enqueue({{1,ServiceKind::prompt,"Prompt",""},
            {2,ServiceKind::clipboard,"Copy","must not copy"},
            {3,ServiceKind::open_folder,"Open","must not open"}});
        if(open_dialog)check(queue.next()->id==1,"Dialog did not start");
        queue.synchronize({{4,ServiceKind::save_file,"Save","must not save"}},true);
        check(queue.closed()&&!queue.current()&&!queue.next(),"Shutdown retained an active or queued platform operation");
        check(!complete(queue,1),"Reply from a dismissed dialog survived shutdown");
        queue.synchronize({{5,ServiceKind::prompt,"Late",""}},false);
        queue.enqueue({{6,ServiceKind::clipboard,"Late copy",""}});
        check(!queue.next(),"A late event reopened the service queue after shutdown");
    }
}
}
int main() {
    try {serial_requests();input_policy();shutdown();std::cout<<"Shared GUI service ordering, input policy and shutdown passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

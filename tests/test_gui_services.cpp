#include "service_queue.hpp"
#include <iostream>
#include <stdexcept>

using namespace datapump::gui::ui;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void serial_requests() {
    ServiceQueue queue;
    queue.enqueue({{0,ServiceKind::prompt,"Prompt","original"},
        {2,ServiceKind::save_file,"Save","retained.bin"}});
    const auto* first=queue.next();
    check(first&&first->id==0,"First request or zero-valued identity was lost");
    queue.synchronize({{3,ServiceKind::clipboard,"Copy","retained text"}},false);
    check(queue.next()==first&&first->value=="original","Polling or adding work replaced the active request");
    check(!queue.complete(2)&&queue.current()==first,"Out-of-order reply released the active dialog");
    check(queue.complete(0)&&!queue.complete(0),"A completed dialog accepted a duplicate reply");
    check(queue.next()->id==2&&queue.current()->value=="retained.bin","Save payload/order changed while another dialog was open");
    check(!queue.complete(0)&&queue.complete(2),"Stale dialog reply completed a later request");
    check(queue.next()->id==3&&queue.current()->value=="retained text","Clipboard payload was not retained until dispatch");
    check(queue.complete(3)&&!queue.next(),"Completed queue repeated a platform operation");
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
        check(!queue.complete(1),"Reply from a dismissed dialog survived shutdown");
        queue.synchronize({{5,ServiceKind::prompt,"Late",""}},false);
        queue.enqueue({{6,ServiceKind::clipboard,"Late copy",""}});
        check(!queue.next(),"A late event reopened the service queue after shutdown");
    }
}
}
int main() {
    try {serial_requests();shutdown();std::cout<<"Shared GUI service ordering and shutdown passed\n";}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}

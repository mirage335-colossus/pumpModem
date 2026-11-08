#include "datapump/transfer.hpp"
namespace datapump::transfer {
bool uses_raw_message(const Message& message) noexcept {
    return message.kind==MessageKind::text&&!message.data.empty()&&message.data.size()<=short_message_bytes;
}
}

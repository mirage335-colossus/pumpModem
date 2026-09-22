#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include "datapump/speculation.h"

namespace datapump {

// The only byte-to-text boundary for received content. Deliberately avoid
// locale, Unicode decoding, escaping, and interpretation of source syntax.
// Each rejected byte becomes one safe ASCII placeholder; output never grows.
// Callers may enable printable ASCII only while both Developer and Shellcode
// modes are enabled. Raw received bytes remain separate for explicit saving.
inline std::string received_text(std::span<const std::uint8_t> bytes,bool shellcode=false) {
    std::string text(bytes.size(),'_');
    for(std::size_t i=0;i<bytes.size();++i) {
        const auto byte=bytes[i];
        const bool allowed=shellcode ? byte>=0x20 && byte<=0x7e :
            (byte>='a' && byte<='z') || (byte>='A' && byte<='Z') ||
            (byte>='0' && byte<='9') || byte==',' || byte=='.' || byte=='@' ||
            byte==' ' || byte=='-' || byte=='_' || byte=='/' || byte=='=';
        if(allowed)text[i]=static_cast<char>(byte);
    }
    // Resolve the validation/filtering path before a more complex native text
    // consumer sees the result. No per-byte serialization is needed here.
    datapump_speculation_barrier();
    return text;
}

inline std::string received_text(std::string_view bytes,bool shellcode=false) {
    return received_text(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()),bytes.size()),shellcode);
}

}

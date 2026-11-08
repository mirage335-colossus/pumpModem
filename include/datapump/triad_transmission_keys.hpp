#pragma once

#include "datapump/crypto.hpp"

namespace datapump::triad {

enum class TransmissionMode { Message, Noise };

struct TransmissionKeys {
    Crypto data;
    Crypto helper;
    // DATA payload confidentiality setting. Helper metadata keying is a
    // separate policy and remains configured independently.
    bool encrypt_data;
};

// Resolve the independent key policy for one transmission. Noise is always
// confidential on DATA, while its callsign metadata uses the public Amateur
// key. Message mode preserves the caller's configured key and encryption flag.
inline TransmissionKeys resolve_transmission_keys(const Crypto& selected,
                                                   bool encrypt_data,
                                                   TransmissionMode mode) {
    if (mode == TransmissionMode::Noise)
        return {Crypto::temporary(), Crypto::amateur(), true};
    return {selected, selected, encrypt_data};
}

} // namespace datapump::triad

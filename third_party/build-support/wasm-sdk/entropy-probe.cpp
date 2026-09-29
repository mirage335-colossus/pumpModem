// SDK qualification: use the same OpenSSL APIs as the C++ application.
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstring>
#include <initializer_list>

extern "C" int datapump_entropy_probe(int reseed)
{
    unsigned char public_bytes[64], private_bytes[64];
    static unsigned char previous[64];
    static bool have_previous = false;
    if (reseed) {
        // Prediction resistance requires fresh source entropy, even when the
        // DRBG has already been instantiated and can otherwise generate bytes.
        for (EVP_RAND_CTX *context : {RAND_get0_primary(nullptr),
                                    RAND_get0_public(nullptr),
                                    RAND_get0_private(nullptr)}) {
            if (context == nullptr ||
                EVP_RAND_reseed(context, 1, nullptr, 0, nullptr, 0) != 1)
                return 0;
        }
    }
    if (RAND_bytes(public_bytes, sizeof public_bytes) != 1 ||
        RAND_priv_bytes(private_bytes, sizeof private_bytes) != 1 ||
        std::memcmp(public_bytes, private_bytes, sizeof public_bytes) == 0 ||
        (have_previous && std::memcmp(public_bytes, previous, sizeof previous) == 0))
        return 0;
    std::memcpy(previous, public_bytes, sizeof previous);
    have_previous = true;
    return 1;
}

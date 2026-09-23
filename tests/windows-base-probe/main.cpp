#include <openssl/crypto.h>
#include <GL/glew.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <cstdio>

int main() {
    FT_Library font = nullptr;
    if (FT_Init_FreeType(&font)) return 1;
    const auto* glew = glewGetString(GLEW_VERSION);
    const auto openssl = OpenSSL_version_num();
    FT_Done_FreeType(font);
    if (!glew || openssl < 0x30000000L) return 2;
    std::printf("Relocated static dependencies: OpenSSL %lx, GLEW %s, FreeType initialized\n",
                openssl, reinterpret_cast<const char*>(glew));
}

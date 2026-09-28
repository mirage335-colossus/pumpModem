#include <openssl/crypto.h>
#include <GL/glew.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <cstdio>

int main() {
    FT_Library font = nullptr;
    if (FT_Init_FreeType(&font)) return 1;
    const auto* glew = glewGetString(GLEW_VERSION);
    const auto openssl = OpenSSL_version_num();
    FT_Done_FreeType(font);
    if (!glew || openssl < 0x30000000L) return 2;
    SDL_SetMainReady();
    if (SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) || SDL_Init(SDL_INIT_VIDEO)) return 3;
    SDL_Window* window = SDL_CreateWindow("DataPump dependency probe", 0, 0, 32, 24, SDL_WINDOW_HIDDEN);
    SDL_Surface* surface = window ? SDL_GetWindowSurface(window) : nullptr;
    const bool software_ok = surface && SDL_FillRect(surface, nullptr,
        SDL_MapRGB(surface->format, 16, 128, 240)) == 0 && SDL_UpdateWindowSurface(window) == 0;
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (!software_ok) return 4;
    std::printf("Relocated static dependencies: OpenSSL %lx, GLEW %s, FreeType initialized, SDL2 software window presented\n",
                openssl, reinterpret_cast<const char*>(glew));
}

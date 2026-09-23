#pragma once
// Only the non-Windows event-pump regression sees this Win32 queue surface.
#include <cstdint>
using HWND = void*;
using UINT = unsigned;
using WPARAM = std::uintptr_t;
using LPARAM = std::intptr_t;
using LRESULT = std::intptr_t;
using BOOL = int;
struct MSG { HWND hwnd{}; UINT message{}; WPARAM wParam{}; LPARAM lParam{}; };
inline constexpr UINT WM_QUIT = 0x12, WM_PAINT = 0xf, WM_CLOSE = 0x10;
inline constexpr UINT WM_KEYDOWN = 0x100, WM_CHAR = 0x102, WM_APP = 0x8000;
inline constexpr UINT PM_REMOVE = 1;
inline constexpr WPARAM VK_SPACE = 0x20;
BOOL PeekMessageW(MSG*, HWND, UINT, UINT, UINT);
BOOL TranslateMessage(const MSG*);
LRESULT DispatchMessageW(const MSG*);
BOOL PostMessageW(HWND, UINT, WPARAM, LPARAM);
void PostQuitMessage(int);

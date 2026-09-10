#pragma once
// Test-only Win32 surface. Never place this include path on production targets.
#include <cstdint>
using DWORD=std::uint32_t;
using DWORD_PTR=std::uintptr_t;
using UINT=unsigned int;
using WORD=std::uint16_t;
using HANDLE=void*;
using LPSTR=char*;
using BOOL=int;
inline constexpr BOOL FALSE=0;
inline constexpr DWORD WAIT_OBJECT_0=0;
inline constexpr DWORD WAIT_TIMEOUT=258;
HANDLE CreateEventA(void*,BOOL,BOOL,const char*);
BOOL CloseHandle(HANDLE);
DWORD WaitForSingleObject(HANDLE,DWORD);

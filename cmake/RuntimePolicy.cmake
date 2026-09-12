# Operating-system ABI and host graphics-driver libraries are excluded. In particular, Visual C++
# redistributable DLLs are application dependencies even when found in System32.
if(WIN32)
  set(DATAPUMP_OS_LIBRARIES
    "^(api-ms-win-|ext-ms-win-).*\\.dll$"
    "^(aclui|advapi32|apphelp|authz|avrt|bcrypt|bcryptprimitives|cabinet|cfgmgr32|clbcatq|comctl32|comdlg32|crypt32|cryptbase|cryptsp|cryptui|dbgcore|dbghelp|dhcpcsvc|dhcpcsvc6|dnsapi|dsound|dwmapi|fwpuclnt|gdi32|gdi32full|gdiplus|imagehlp|imm32|iphlpapi|kernel32|kernelappcore|kernelbase|mpr|msacm32|msimg32|msvcp_win|msvcrt|mswsock|ncrypt|netapi32|normaliz|ntdll|ole32|oleacc|oleaut32|opengl32|powrprof|profapi|propsys|psapi|rpcrt4|rsaenh|secur32|setupapi|shcore|shell32|shlwapi|sspicli|ucrtbase|user32|userenv|usp10|uxtheme|version|windowscodecs|winhttp|wininet|winmm|winmmbase|winrnr|wintrust|wldap32|ws2_32|wtsapi32)\\.dll$"
    "^winspool\\.drv$")
else()
  set(DATAPUMP_OS_LIBRARIES
    # The target computer supplies its GL loader, vendor selection and drivers.
    # Copying a build host's GLX/NVIDIA/Mesa chain can break another machine.
    "^lib(GL|GLX|OpenGL|GLdispatch|EGL|GLESv1_CM|GLESv2)(_[^.]+)?\\.so(\\..*)?$"
    "^lib(nvidia|cuda|drm|gbm)[^.]*\\.so(\\..*)?$"
    "^ld-linux.*" "^ld64.*"
    "^lib(c|m|mvec|dl|pthread|rt|util|resolv|anl|thread_db|BrokenLocale|nss_[^.]+)\\.so(\\..*)?$")
endif()

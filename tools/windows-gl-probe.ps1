param(
    [ValidateRange(1,30)][int]$TimeoutSeconds = 30
)
$ErrorActionPreference = 'Stop'
if (!$IsWindows) { throw 'This diagnostic requires Windows and Visual Studio 2022.' }

$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio 2022 x64 C++ tools are required.' }
$vcvars = Join-Path $vs 'VC/Auxiliary/Build/vcvarsall.bat'
$temporaryRoot = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [System.IO.Path]::GetTempPath() }
$work = Join-Path $temporaryRoot ('datapump-wgl-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null

function Invoke-BoundedProbeProcess {
    param([string]$FilePath, [string]$Arguments, [int]$Seconds, [string]$Label)
    $stdout = Join-Path $work ($Label + '.stdout.log')
    $stderr = Join-Path $work ($Label + '.stderr.log')
    $process = $null
    $timedOut = $false
    try {
        $start = @{ FilePath=$FilePath; WorkingDirectory=$work; PassThru=$true;
            RedirectStandardOutput=$stdout; RedirectStandardError=$stderr }
        if ($Arguments) { $start.ArgumentList = $Arguments }
        $process = Start-Process @start
        if (!$process.WaitForExit($Seconds * 1000)) {
            $timedOut = $true
            $process.Kill($true)
            if (!$process.WaitForExit(5000)) { throw "$Label process did not exit after termination." }
        }
        $process.Refresh()
        $code = $process.ExitCode
    }
    finally {
        Write-Host "--- $Label stdout ---"
        if (Test-Path -LiteralPath $stdout) { Get-Content -LiteralPath $stdout -Raw | Write-Host }
        Write-Host "--- $Label stderr ---"
        if (Test-Path -LiteralPath $stderr) { Get-Content -LiteralPath $stderr -Raw | Write-Host }
        if ($process) { $process.Dispose() }
    }
    if ($timedOut) { throw "$Label exceeded its $Seconds-second deadline." }
    if ($code -ne 0) { throw "$Label exited with code $code; diagnostic output is above." }
}

try {
    # Standalone Windows/OpenGL 1.1 headers suffice to inspect the bootstrap;
    # modern WGL functions are looked up exactly as in NativeWindow.win.ixx.
    $source = @'
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <string.h>

typedef const char* (WINAPI *GetExtensionsARB)(HDC);
typedef const char* (WINAPI *GetExtensionsEXT)(void);

static void print_wide(const char* label, const WCHAR* text) {
    char encoded[4096];
    int result = WideCharToMultiByte(CP_UTF8, 0, text, -1, encoded, sizeof(encoded), NULL, NULL);
    printf("%s=%s\n", label, result ? encoded : "<conversion failed>");
}

static void print_error(const char* stage) {
    DWORD code = GetLastError();
    WCHAR message[2048] = L"";
    printf("FAILURE_STAGE=%s\nWIN32_ERROR=%lu\n", stage, (unsigned long)code);
    if (FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       NULL, code, 0, message, sizeof(message) / sizeof(message[0]), NULL))
        print_wide("WIN32_MESSAGE", message);
}

static int available(PROC address) {
    return address && address != (PROC)(INT_PTR)1 && address != (PROC)(INT_PTR)2
        && address != (PROC)(INT_PTR)3 && address != (PROC)(INT_PTR)-1;
}

static void print_pfd(const char* label, int number, const PIXELFORMATDESCRIPTOR* pfd) {
    printf("%s number=%d flags=0x%08lx RGBA=%u/%u/%u/%u color=%u depth=%u stencil=%u type=%u layer=%u\n",
           label, number, (unsigned long)pfd->dwFlags, pfd->cRedBits, pfd->cGreenBits,
           pfd->cBlueBits, pfd->cAlphaBits, pfd->cColorBits, pfd->cDepthBits,
           pfd->cStencilBits, pfd->iPixelType, (unsigned char)pfd->iLayerType);
    printf("%s DRAW_TO_WINDOW=%d SUPPORT_OPENGL=%d DOUBLEBUFFER=%d GENERIC_FORMAT=%d GENERIC_ACCELERATED=%d\n",
           label, !!(pfd->dwFlags & PFD_DRAW_TO_WINDOW), !!(pfd->dwFlags & PFD_SUPPORT_OPENGL),
           !!(pfd->dwFlags & PFD_DOUBLEBUFFER), !!(pfd->dwFlags & PFD_GENERIC_FORMAT),
           !!(pfd->dwFlags & PFD_GENERIC_ACCELERATED));
}

static const char* gl_string(GLenum name) {
    const GLubyte* value = glGetString(name);
    return value ? (const char*)value : "<unavailable>";
}

static int extension_present(const char* list, const char* wanted) {
    size_t length = strlen(wanted);
    const char* found = list;
    while ((found = strstr(found, wanted)) != NULL) {
        if ((found == list || found[-1] == ' ') && (found[length] == '\0' || found[length] == ' '))
            return 1;
        found += length;
    }
    return 0;
}

static void display_inventory(void) {
    DWORD index;
    printf("REMOTE_SESSION=%d\n", GetSystemMetrics(SM_REMOTESESSION));
    for (index = 0; index < 32; ++index) {
        DISPLAY_DEVICEW device;
        ZeroMemory(&device, sizeof(device));
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(NULL, index, &device, EDD_GET_DEVICE_INTERFACE_NAME)) break;
        printf("DISPLAY_DEVICE index=%lu flags=0x%08lx\n", (unsigned long)index, (unsigned long)device.StateFlags);
        print_wide("DISPLAY_NAME", device.DeviceName);
        print_wide("DISPLAY_DESCRIPTION", device.DeviceString);
        print_wide("DISPLAY_ID", device.DeviceID);
        print_wide("DISPLAY_REGISTRY_KEY", device.DeviceKey);
    }
}

int main(void) {
    const WCHAR* class_name = L"DataPumpWglBootstrapProbe";
    HINSTANCE instance = GetModuleHandleW(NULL);
    HMODULE module = GetModuleHandleW(L"opengl32.dll");
    WCHAR module_path[32768];
    WNDCLASSW window_class;
    PIXELFORMATDESCRIPTOR requested, selected;
    ATOM registered = 0;
    HWND window = NULL;
    HDC dc = NULL;
    HGLRC context = NULL;
    PROC create_context = NULL, choose_format = NULL, extensions_arb = NULL, extensions_ext = NULL;
    int pixel_format = 0, current = 0, result = 1;
    const char* extensions;

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("PROBE=Rev legacy WGL bootstrap, x64, hidden 1x1 CS_OWNDC window\n");
    printf("SCOPE=Required WGL entrypoints only; modern context creation and rendering are not certified.\n");
    display_inventory();
    if (module && GetModuleFileNameW(module, module_path, sizeof(module_path) / sizeof(module_path[0])))
        print_wide("OPENGL32_MODULE_PATH", module_path);
    else print_error("GetModuleFileNameW(opengl32.dll)");

    ZeroMemory(&window_class, sizeof(window_class));
    window_class.style = CS_OWNDC;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = instance;
    window_class.lpszClassName = class_name;
    SetLastError(0);
    registered = RegisterClassW(&window_class);
    if (!registered) { print_error("RegisterClassW"); goto cleanup; }
    window = CreateWindowW(class_name, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1,
                           NULL, NULL, instance, NULL);
    if (!window) { print_error("CreateWindowW"); goto cleanup; }
    dc = GetDC(window);
    if (!dc) { print_error("GetDC"); goto cleanup; }

    /* Same legacy request used by Rev's NativeWindow::createContext(). */
    ZeroMemory(&requested, sizeof(requested));
    requested.nSize = sizeof(requested);
    requested.nVersion = 1;
    requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    requested.iPixelType = PFD_TYPE_RGBA;
    requested.cColorBits = 32;
    requested.cDepthBits = 24;
    requested.cStencilBits = 8;
    requested.iLayerType = PFD_MAIN_PLANE;
    print_pfd("REQUESTED_PFD", 0, &requested);
    SetLastError(0);
    pixel_format = ChoosePixelFormat(dc, &requested);
    if (!pixel_format) { print_error("ChoosePixelFormat"); goto cleanup; }
    ZeroMemory(&selected, sizeof(selected));
    if (DescribePixelFormat(dc, pixel_format, sizeof(selected), &selected))
        print_pfd("SELECTED_PFD", pixel_format, &selected);
    else print_error("DescribePixelFormat");
    SetLastError(0);
    if (!SetPixelFormat(dc, pixel_format, &requested)) { print_error("SetPixelFormat"); goto cleanup; }
    SetLastError(0);
    context = wglCreateContext(dc);
    if (!context) { print_error("wglCreateContext"); goto cleanup; }
    SetLastError(0);
    if (!wglMakeCurrent(dc, context)) { print_error("wglMakeCurrent"); goto cleanup; }
    current = 1;
    printf("GL_VENDOR=%s\nGL_RENDERER=%s\nGL_VERSION=%s\n",
           gl_string(GL_VENDOR), gl_string(GL_RENDERER), gl_string(GL_VERSION));
    extensions = (const char*)glGetString(GL_EXTENSIONS);
    printf("GL_ARB_buffer_storage=%d\n", extensions ? extension_present(extensions, "GL_ARB_buffer_storage") : 0);
    create_context = wglGetProcAddress("wglCreateContextAttribsARB");
    choose_format = wglGetProcAddress("wglChoosePixelFormatARB");
    extensions_arb = wglGetProcAddress("wglGetExtensionsStringARB");
    extensions_ext = wglGetProcAddress("wglGetExtensionsStringEXT");
    if (available(extensions_arb)) {
        extensions = ((GetExtensionsARB)extensions_arb)(dc);
        printf("WGL_EXTENSIONS_ARB=%s\n", extensions ? extensions : "<unavailable>");
    } else printf("WGL_EXTENSIONS_ARB=<entrypoint unavailable>\n");
    if (available(extensions_ext)) {
        extensions = ((GetExtensionsEXT)extensions_ext)();
        printf("WGL_EXTENSIONS_EXT=%s\n", extensions ? extensions : "<unavailable>");
    } else printf("WGL_EXTENSIONS_EXT=<entrypoint unavailable>\n");
    result = available(create_context) && available(choose_format) ? 0 : 2;

cleanup:
    if (!current) printf("GL_VENDOR=<no current context>\nGL_RENDERER=<no current context>\nGL_VERSION=<no current context>\n");
    printf("wglCreateContextAttribsARB available=%d address=%p\n", available(create_context), (void*)create_context);
    printf("wglChoosePixelFormatARB available=%d address=%p\n", available(choose_format), (void*)choose_format);
    printf("WGL_BOOTSTRAP_RESULT=%s\n", result == 0 ? "required entrypoints present" : result == 2 ? "required ARB entrypoints missing" : "legacy context setup failed");
    if (current) wglMakeCurrent(NULL, NULL);
    if (context) wglDeleteContext(context);
    if (dc) ReleaseDC(window, dc);
    if (window) DestroyWindow(window);
    if (registered) UnregisterClassW(class_name, instance);
    return result;
}
'@
    [System.IO.File]::WriteAllText((Join-Path $work 'probe.c'), $source, [System.Text.UTF8Encoding]::new($false))
    $build = @"
@echo off
call "$vcvars" x64
if errorlevel 1 exit /b %errorlevel%
cl.exe /nologo /W4 /O2 /MT /TC probe.c /Feprobe.exe /Foprobe.obj /link opengl32.lib gdi32.lib user32.lib
exit /b %errorlevel%
"@
    $buildScript = Join-Path $work 'build.cmd'
    [System.IO.File]::WriteAllText($buildScript, $build, [System.Text.Encoding]::ASCII)
    Write-Host "Visual Studio: $vs"
    Write-Host 'Compiling standalone x64 WGL bootstrap diagnostic; no dependency builds or driver changes.'
    Invoke-BoundedProbeProcess -FilePath $env:ComSpec -Arguments ('/d /s /c ""{0}""' -f $buildScript) -Seconds 60 -Label 'compile'
    Invoke-BoundedProbeProcess -FilePath (Join-Path $work 'probe.exe') -Arguments '' -Seconds $TimeoutSeconds -Label 'wgl-probe'
}
finally {
    Remove-Item -LiteralPath $work -Recurse -Force
}

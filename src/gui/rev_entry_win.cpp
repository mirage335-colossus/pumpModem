#ifdef _WIN32
#include <windows.h>
#include <cstdlib>

int datapump_rev_main(int argc,char** argv);
// Rev's static library does not supply a Windows subsystem entry shim.
int WINAPI WinMain(HINSTANCE,HINSTANCE,LPSTR,int) {
    return datapump_rev_main(__argc,__argv);
}
#endif

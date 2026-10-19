// Stand-in for UndauntedInternalServer.dll in the proxy test: records that the proxy loaded it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) SetEnvironmentVariableW(L"VIBELESS_PROXY_TEST_RUNTIME", L"loaded");
    return TRUE;
}

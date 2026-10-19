// Vibeless proxy loader for the Dauntless 1.4.4 client and game servers. AGPL-3.0-only.
//
// Windows loads dxgi.dll from the game's folder before System32. This DLL:
//   1. loads the real dxgi.dll from the system directory, never from the game's folder;
//   2. forwards the DXGI entry points the game uses to it;
//   3. loads UndauntedInternalServer.dll from its own folder, so the runtime installs its hooks
//      before the game's own code starts.
// It replaces the upstream prebuilt proxy, whose source was never published (Phase 0 decision D4).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace {
HMODULE RealDxgi = nullptr;
INIT_ONCE RealDxgiOnce = INIT_ONCE_STATIC_INIT;

BOOL CALLBACK LoadRealDxgi(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t system[MAX_PATH] = {};
    const UINT length = GetSystemDirectoryW(system, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return TRUE;
    RealDxgi = LoadLibraryExW((std::wstring(system, length) + L"\\dxgi.dll").c_str(), nullptr, 0);
    return TRUE;
}

FARPROC Real(const char* name) {
    InitOnceExecuteOnce(&RealDxgiOnce, LoadRealDxgi, nullptr, nullptr);
    return RealDxgi ? GetProcAddress(RealDxgi, name) : nullptr;
}

template <class Fn> Fn Resolve(const char* name) {
    return reinterpret_cast<Fn>(Real(name));
}

void LoadRuntime(HMODULE self) {
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    std::wstring runtime(path, length);
    const auto slash = runtime.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    runtime.resize(slash + 1);
    runtime += L"UndauntedInternalServer.dll";
    // Like the upstream proxy, a missing runtime leaves the game unmodified rather than failing.
    LoadLibraryExW(runtime.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}
}

extern "C" {
HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** factory) {
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    const auto fn = Resolve<Fn>("CreateDXGIFactory");
    return fn ? fn(riid, factory) : E_NOTIMPL;
}
HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** factory) {
    using Fn = HRESULT(WINAPI*)(REFIID, void**);
    const auto fn = Resolve<Fn>("CreateDXGIFactory1");
    return fn ? fn(riid, factory) : E_NOTIMPL;
}
HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT flags, REFIID riid, void** factory) {
    using Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    const auto fn = Resolve<Fn>("CreateDXGIFactory2");
    return fn ? fn(flags, riid, factory) : E_NOTIMPL;
}
HRESULT WINAPI Proxy_DXGIGetDebugInterface1(UINT flags, REFIID riid, void** debug) {
    using Fn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    const auto fn = Resolve<Fn>("DXGIGetDebugInterface1");
    return fn ? fn(flags, riid, debug) : E_NOINTERFACE;
}
HRESULT WINAPI Proxy_DXGIDeclareAdapterRemovalSupport() {
    using Fn = HRESULT(WINAPI*)();
    const auto fn = Resolve<Fn>("DXGIDeclareAdapterRemovalSupport");
    return fn ? fn() : S_OK;
}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        LoadRuntime(module);
    }
    return TRUE;
}

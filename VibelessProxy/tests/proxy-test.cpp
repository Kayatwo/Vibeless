// Loads the proxy from a folder holding a stand-in runtime, then creates a real DXGI factory through it.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dxgi1_2.h>
#include <cstdio>
#include <string>
#pragma comment(lib, "dxguid.lib")

static int Fail(const char* what) { std::printf("FAIL %s (error %lu)\n", what, GetLastError()); return 1; }

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return Fail("usage: proxy-test <folder with dxgi.dll and UndauntedInternalServer.dll>");
    const std::wstring proxyPath = std::wstring(argv[1]) + L"\\dxgi.dll";
    HMODULE proxy = LoadLibraryExW(proxyPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!proxy) return Fail("load proxy");

    wchar_t flag[16] = {};
    if (!GetEnvironmentVariableW(L"VIBELESS_PROXY_TEST_RUNTIME", flag, 16) || std::wstring(flag) != L"loaded")
        return Fail("runtime DLL next to the proxy was not loaded");

    for (const char* name : {"CreateDXGIFactory", "CreateDXGIFactory1", "CreateDXGIFactory2", "DXGIGetDebugInterface1", "DXGIDeclareAdapterRemovalSupport"})
        if (!GetProcAddress(proxy, name)) { std::printf("missing export %s\n", name); return Fail("export"); }

    using Create1 = HRESULT(WINAPI*)(REFIID, void**);
    using Create2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
    IDXGIFactory1* factory1 = nullptr;
    if (FAILED(reinterpret_cast<Create1>(GetProcAddress(proxy, "CreateDXGIFactory1"))(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory1))) || !factory1)
        return Fail("CreateDXGIFactory1 through the proxy");
    IDXGIAdapter1* adapter = nullptr;
    const bool hasAdapter = SUCCEEDED(factory1->EnumAdapters1(0, &adapter));
    if (adapter) adapter->Release();
    factory1->Release();

    IDXGIFactory2* factory2 = nullptr;
    if (FAILED(reinterpret_cast<Create2>(GetProcAddress(proxy, "CreateDXGIFactory2"))(0, __uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2))) || !factory2)
        return Fail("CreateDXGIFactory2 through the proxy");
    factory2->Release();

    wchar_t system[MAX_PATH] = {};
    GetSystemDirectoryW(system, MAX_PATH);
    HMODULE real = GetModuleHandleW((std::wstring(system) + L"\\dxgi.dll").c_str());
    if (!real || real == proxy) return Fail("the system dxgi.dll was not loaded separately");

    std::printf("PASS proxy loaded runtime, forwarded factories (adapter enumerated: %s)\n", hasAdapter ? "yes" : "no");
    return 0;
}

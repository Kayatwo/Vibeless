# Vibeless proxy loader (`dxgi.dll`)

Replaces the upstream prebuilt `dxgi.dll`, whose source is unpublished (Phase 0 decision D4).
Placed next to `Dauntless-Win64-Shipping.exe`, it loads the real `dxgi.dll` from the system
directory, forwards `CreateDXGIFactory`, `CreateDXGIFactory1`, `CreateDXGIFactory2`,
`DXGIGetDebugInterface1` and `DXGIDeclareAdapterRemovalSupport`, and loads
`UndauntedInternalServer.dll` from its own folder. A missing runtime leaves the game unmodified.

Build and test from a Visual Studio x64 developer prompt: `build.cmd <output folder>`. The test
loads the proxy beside a stand-in runtime DLL and creates real DXGI factories through it. CI runs
the same script. Until this proxy has passed client and game-server start-up with the real game,
the upstream binary (SHA-256 `9a431d7b…0d1f`) remains the pinned input for releases.

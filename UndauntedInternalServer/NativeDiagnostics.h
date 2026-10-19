#pragma once
#include <Windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>

// Bounded crash diagnostics: never serialize commands, credentials or player data.
inline volatile LONG DR_TickStage = 0;
inline wchar_t DR_DiagnosticPath[MAX_PATH] = {};
inline volatile LONG DR_DiagnosticWrites = 0;
inline thread_local unsigned DR_EventDepth = 0;
inline thread_local char DR_EventFrames[24][768] = {};
struct DR_EventScope {
    bool Enabled = false;
    explicit DR_EventScope(const char* label) : Enabled(label != nullptr) {
        if (!Enabled) return;
        if (DR_EventDepth < 24) strncpy_s(DR_EventFrames[DR_EventDepth], label, _TRUNCATE);
        ++DR_EventDepth;
    }
    ~DR_EventScope() { if (Enabled && DR_EventDepth) --DR_EventDepth; }
};
inline void DR_WriteDiagnostic(const char* line, DWORD length) {
    if (!DR_DiagnosticPath[0]) return;
    HANDLE file = CreateFileW(DR_DiagnosticPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0; WriteFile(file, line, length, &written, nullptr); CloseHandle(file);
    }
}
inline LONG CALLBACK DR_RecordException(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000409 && code != 0xC0000374)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedIncrement(&DR_DiagnosticWrites) > 32) return EXCEPTION_CONTINUE_SEARCH;
    HMODULE module = nullptr; char name[MAX_PATH] = {}; char line[1024] = {};
    auto address = info->ExceptionRecord->ExceptionAddress;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(address), &module);
    if (module) GetModuleFileNameA(module, name, MAX_PATH);
    int size = sprintf_s(line, "first_chance code=%08lx address=%p module=%s offset=%llx tick_stage=%ld thread=%lu uptime_ms=%llu\n",
        code, address, name, static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(module)),
        DR_TickStage, GetCurrentThreadId(), GetTickCount64());
    if (size > 0) DR_WriteDiagnostic(line, static_cast<DWORD>(size));
    for (unsigned depth = 0; depth < DR_EventDepth && depth < 24; ++depth) {
        size = sprintf_s(line, "script_frame depth=%u %s\n", depth, DR_EventFrames[depth]);
        if (size > 0) DR_WriteDiagnostic(line, static_cast<DWORD>(size));
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
inline void DR_InitDiagnostics() {
    wchar_t folder[MAX_PATH] = {};
    DWORD size = GetEnvironmentVariableW(L"GAMESERVER_READY_DIR", folder, MAX_PATH);
    if (!size || size >= MAX_PATH - 48) return;
    swprintf_s(DR_DiagnosticPath, L"%s\\native-fault-%lu.log", folder, GetCurrentProcessId());
    AddVectoredExceptionHandler(1, DR_RecordException);
}

// Windows client side of the compatibility contract (Compatibility.h). AGPL-3.0-only.
//
// Shortly after start the client asks its metagame what the server needs (GET /vibeless/requirements),
// hashes only the .pak files named there (in its own Content/Paks folder), compares, and reports its
// state (POST /vibeless/compatibility, with the player's API key) so the metagame can refuse
// matchmaking for an incompatible client. On a mismatch the player sees the problems in a message
// box and the game closes, before it joins any world. A server that is not a Vibeless foundation
// (404) or does not answer is not judged here; the metagame still enforces its side.
// DR_COMPATIBILITY_CHECK=off skips the client-side check (troubleshooting only).
#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "Compatibility.h"
#include "FoundationSettings.h"
#include "VibelessFeatures.h"
#include "VibelessVersion.h"
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace VibelessCompatibility {
struct HttpResult { DWORD Status = 0; std::string Body; };

inline std::wstring Widen(const std::string& text) { return std::wstring(text.begin(), text.end()); }

// host:port of the metagame as the client received it; plain HTTP on the profile's loopback or LAN.
inline bool Request(const std::wstring& address, const wchar_t* verb, const std::wstring& path,
    const std::string& body, const std::wstring& headers, HttpResult& result) {
    const auto colon = address.rfind(L':');
    if (colon == std::wstring::npos) return false;
    const std::wstring host = address.substr(0, colon);
    const int port = _wtoi(address.substr(colon + 1).c_str());
    if (port <= 0 || port > 65535) return false;
    bool ok = false;
    HINTERNET session = WinHttpOpen(L"Vibeless/" VIBELESS_VERSION, WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    HINTERNET connection = session ? WinHttpConnect(session, host.c_str(), static_cast<INTERNET_PORT>(port), 0) : nullptr;
    HINTERNET request = connection ? WinHttpOpenRequest(connection, verb, path.c_str(), nullptr, nullptr, nullptr, 0) : nullptr;
    if (request) {
        WinHttpSetTimeouts(request, 3000, 3000, 5000, 10000);
        if (WinHttpSendRequest(request, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                headers.empty() ? 0 : static_cast<DWORD>(-1L), const_cast<char*>(body.data()),
                static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0)
            && WinHttpReceiveResponse(request, nullptr)) {
            DWORD size = sizeof(result.Status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &result.Status, &size, nullptr);
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                if (result.Body.size() + available > 256 * 1024) break;  // requirements are small
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
                result.Body.append(chunk.data(), read);
            }
            ok = true;
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    if (session) WinHttpCloseHandle(session);
    return ok;
}

inline std::string Sha256File(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::string hex;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0
        && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<char> buffer(1 << 20);
        bool failed = false;
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto count = static_cast<ULONG>(input.gcount());
            if (count && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), count, 0) != 0) { failed = true; break; }
        }
        unsigned char digest[32] = {};
        if (!failed && BCryptFinishHash(hash, digest, sizeof digest, 0) == 0) {
            static const char digits[] = "0123456789abcdef";
            for (const auto byte : digest) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
        }
    }
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}

inline std::filesystem::path PaksFolder() {
    wchar_t exe[32768] = {};
    GetModuleFileNameW(nullptr, exe, 32768);
    // <game>/Archon/Binaries/Win64/<exe> -> <game>/Archon/Content/Paks
    return std::filesystem::path(exe).parent_path().parent_path().parent_path() / L"Content" / L"Paks";
}

inline std::string ApiKeyFromCommandLine() {
    int count = 0;
    wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
    std::string key;
    for (int i = 0; args && i < count; ++i) {
        const std::wstring_view arg(args[i]);
        constexpr std::wstring_view prefix = L"-AUTH_PASSWORD=";
        if (arg.size() > prefix.size() && arg.substr(0, prefix.size()) == prefix)
            for (const auto c : arg.substr(prefix.size())) key += static_cast<char>(c < 128 ? c : '?');
    }
    if (args) LocalFree(args);
    return key;
}

inline void Run(std::wstring address) {
    const auto setting = FoundationSettings::Environment(L"DR_COMPATIBILITY_CHECK");
    const bool checkHere = FoundationSettings::ParseOnOff(setting, true);
    HttpResult response;
    bool answered = false;
    for (int attempt = 0; attempt < 10 && !answered; ++attempt) {
        if (attempt) Sleep(2000);
        response = {};
        answered = Request(address, L"GET", L"/vibeless/requirements", {}, {}, response);
    }
    if (!answered) { std::cout << "Vibeless compatibility: metagame did not answer; not checked here" << std::endl; return; }
    if (response.Status == 404) { std::cout << "Vibeless compatibility: server publishes no requirements" << std::endl; return; }
    Compatibility::Requirements required;
    if (response.Status != 200) { std::cout << "Vibeless compatibility: requirements status " << response.Status << std::endl; return; }
    if (const auto error = Compatibility::ParseRequirements(response.Body, required); !error.empty()) {
        std::cout << "Vibeless compatibility: unreadable requirements (" << error << ")" << std::endl;
        return;
    }

    Compatibility::ClientState client;
    client.Foundation = VIBELESS_VERSION;
    client.Features = Vibeless::Features().PackageFeatures();
    const auto paks = PaksFolder();
    for (const auto& file : required.Files) {
        std::error_code error;
        const auto path = paks / Widen(file.Name);
        client.Files[file.Name] = std::filesystem::is_regular_file(path, error) ? Sha256File(path) : std::string();
    }

    const auto key = ApiKeyFromCommandLine();
    if (!key.empty()) {
        HttpResult posted;
        const auto headers = L"Content-Type: text/plain\r\nx-undaunted-user-api-key: " + Widen(key) + L"\r\n";
        Request(address, L"POST", L"/vibeless/compatibility", Compatibility::Report(client), headers, posted);
        std::cout << "Vibeless compatibility: reported to the metagame (status " << posted.Status << ")" << std::endl;
    }

    const auto problems = Compatibility::Check(required, client);
    if (problems.empty()) { std::cout << "Vibeless compatibility: this game matches the server" << std::endl; return; }
    std::wstring message = L"This game cannot join this server because its content differs:\n\n";
    for (const auto& problem : problems) {
        std::cout << "Vibeless compatibility: " << problem << std::endl;
        message += L"\x2022 " + Widen(problem) + L"\n";
    }
    if (!checkHere) { std::cout << "Vibeless compatibility: DR_COMPATIBILITY_CHECK=off, continuing" << std::endl; return; }
    message += L"\nInstall the server's packages (or use the profile prepared for this server) and start again.\nThe game will now close.";
    MessageBoxW(nullptr, message.c_str(), L"Vibeless: incompatible content", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    // The game has not joined a world yet; end it without running engine teardown on this thread.
    TerminateProcess(GetCurrentProcess(), 2);
}

// Starts the check on its own thread so the game keeps loading meanwhile.
inline void Start(const std::wstring& metagameAddress) {
    auto* address = new std::wstring(metagameAddress);
    const HANDLE thread = CreateThread(nullptr, 0, [](LPVOID parameter) -> DWORD {
        std::unique_ptr<std::wstring> owned(static_cast<std::wstring*>(parameter));
        Run(*owned);
        return 0;
    }, address, 0, nullptr);
    if (thread) CloseHandle(thread);
    else delete address;
}
}

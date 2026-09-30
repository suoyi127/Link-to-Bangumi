#include "anime_vault/infrastructure/network/WindowsSystemHttp.hpp"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>

#include <array>
#include <memory>
#include <thread>

namespace anime_vault {
namespace {
struct Handle {
    HINTERNET value{};
    ~Handle() { if (value) WinHttpCloseHandle(value); }
};

WindowsHttpResult perform(const std::string& host, const std::string& path,
                          const std::string& method, const std::string& userAgent,
                          const std::string& body, std::size_t maxBytes) {
    if ((host != "api.bgm.tv" && host != "lain.bgm.tv" && host != "api.vndb.org" && host != "t.vndb.org") || path.empty() || path[0] != '/' ||
        path.size() > 2048 || (method != "GET" && method != "POST") || body.size() > 4096)
        return {};
    const auto wideHost = std::wstring(host.begin(), host.end());
    const auto widePath = std::wstring(path.begin(), path.end());
    const auto wideMethod = std::wstring(method.begin(), method.end());
    const auto wideAgent = std::wstring(userAgent.begin(), userAgent.end());
    Handle session{WinHttpOpen(wideAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) return {};
    WinHttpSetTimeouts(session.value, 5000, 5000, 5000, 8000);
    Handle connection{WinHttpConnect(session.value, wideHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)};
    if (!connection.value) return {};
    Handle request{WinHttpOpenRequest(connection.value, wideMethod.c_str(), widePath.c_str(),
                                     nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE)};
    if (!request.value) return {};
    DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_DISABLE_FEATURE,
                          &disabled, sizeof(disabled))) return {};
    const auto headers = method == "POST" ? L"Content-Type: application/json\r\n" : L"";
    if (!WinHttpSendRequest(request.value, headers, static_cast<DWORD>(-1),
                            body.empty() ? WINHTTP_NO_REQUEST_DATA :
                                const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) return {};
    DWORD status{};
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) return {};
    WindowsHttpResult result{static_cast<int>(status), std::string{}};
    std::array<char, 16384> buffer{};
    while (true) {
        DWORD received{};
        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                              &received)) return {};
        if (!received) break;
        if (result.body->size() + received > maxBytes) return {};
        result.body->append(buffer.data(), received);
    }
    return result;
}
}

void windowsSystemHttpsRequest(std::string host, std::string path, std::string method,
                               std::string userAgent, std::string body, std::size_t maxBytes,
                               std::function<void(WindowsHttpResult)> completion) {
    std::thread([host = std::move(host), path = std::move(path), method = std::move(method),
                 userAgent = std::move(userAgent), body = std::move(body), maxBytes,
                 completion = std::move(completion)]() mutable {
        WindowsHttpResult result;
        try { result = perform(host, path, method, userAgent, body, maxBytes); }
        catch (...) { result = {}; }
        completion(std::move(result));
    }).detach();
}
}
#endif

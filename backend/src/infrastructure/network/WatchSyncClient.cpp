#include "anime_vault/services/WatchSyncService.hpp"
#include "anime_vault/infrastructure/network/WindowsSystemHttp.hpp"
#include <algorithm>
#include <cwctype>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace anime_vault {
namespace {
std::string readCredential(const std::wstring& target) {
#ifdef _WIN32
    PCREDENTIALW raw = nullptr;
    if (CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
        std::unique_ptr<CREDENTIALW, decltype(&CredFree)> credential(raw, &CredFree);
        return {reinterpret_cast<const char*>(raw->CredentialBlob), raw->CredentialBlobSize};
    }
    if (GetLastError() == ERROR_NOT_FOUND)
        return {};
#endif
    throw WatchSyncError(500, "credential_storage_failed");
}
void writeCredential(const std::wstring& target, const std::string& token) {
#ifdef _WIN32
    if (token.empty()) {
        if (CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND)
            return;
    } else {
        CREDENTIALW credential{};
        credential.Type = CRED_TYPE_GENERIC;
        credential.TargetName = const_cast<LPWSTR>(target.c_str());
        credential.CredentialBlobSize = static_cast<DWORD>(token.size());
        credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(token.data()));
        credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
        if (CredWriteW(&credential, 0))
            return;
    }
#endif
    throw WatchSyncError(500, "credential_storage_failed");
}
} // namespace
std::shared_ptr<WatchSyncService> makeWatchSyncService(SqliteDatabase& db, const std::filesystem::path& data,
                                                       std::function<std::string()> userAgent) {
    auto path = std::filesystem::weakly_canonical(std::filesystem::absolute(data)).wstring();
    std::transform(path.begin(), path.end(), path.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    auto target = [prefix = L"AnimeVault/bgm-watch/" + path + L"/"](std::int64_t account) {
        return prefix + std::to_wstring(account);
    };
    auto service = std::make_shared<WatchSyncService>(
        db,
        [agent = std::move(userAgent)](const auto& method, const auto& route, const auto& body,
                                       const auto& token) -> WatchResponse {
#ifdef _WIN32
            auto userAgent = agent();
            if (userAgent.empty())
                userAgent = "suoyi127/Link-to-Bangumi/0.1 (Windows) (https://github.com/suoyi127/Link-to-Bangumi)";
            auto response = authenticatedBangumiRequest(route, method, userAgent, body, token);
            return {response.status, response.body.value_or("")};
#else
            throw WatchSyncError(503, "watch_sync_unavailable");
#endif
        },
        [target](auto account) { return readCredential(target(account)); },
        [target](auto account, const auto& token) { writeCredential(target(account), token); });
    service->start();
    return service;
}
} // namespace anime_vault

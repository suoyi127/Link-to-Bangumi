#include "anime_vault/infrastructure/network/QbConnectionManager.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cwctype>
#include <memory>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#include <wincred.h>
#endif

namespace anime_vault {
namespace {
bool printable(std::string_view value) {
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 0x20 && ch != 0x7f;
    });
}
}

QbConnectionManager::QbConnectionManager(std::filesystem::path dataPath,
                                         QbConnectionConfig environment)
    : dataPath_(std::filesystem::absolute(std::move(dataPath)).lexically_normal()),
      environment_(std::move(environment)), saved_(loadSaved()) {
    const auto& active = saved_ ? *saved_ : environment_;
    current_ = std::make_shared<QbWebClient>(active.url, active.username, active.password);
}

std::shared_ptr<QbWebClient> QbConnectionManager::current() const {
    std::lock_guard lock(mutex_);
    return current_;
}

QbConfigSummary QbConnectionManager::summary() const {
    std::lock_guard lock(mutex_);
    const auto& active = saved_ ? *saved_ : environment_;
    return {active.url, active.username,
        saved_ ? "saved" : current_->configured() ? "environment" : "none",
        current_->configured()};
}

QbConnectionConfig QbConnectionManager::validated(QbConnectionConfig submitted) const {
    try { QbWebClient::parseEndpoint(submitted.url); }
    catch (const std::exception&) { throw QbConfigError("invalid_qb_webui_url", "explicit HTTP loopback URL required"); }
    if (submitted.username.empty() || submitted.username.size() > 128 ||
        !printable(submitted.username) || submitted.password.size() > 512 ||
        !printable(submitted.password))
        throw QbConfigError("invalid_qb_credentials", "invalid qB credentials");
    if (submitted.password.empty()) {
        if (!saved_) throw QbConfigError("qb_password_required", "qB password is required");
        submitted.password = saved_->password;
    }
    return submitted;
}

QbConnectionConfig QbConnectionManager::draft(QbConnectionConfig submitted) const {
    std::lock_guard lock(mutex_);
    return validated(std::move(submitted));
}

void QbConnectionManager::save(QbConnectionConfig submitted) {
    std::lock_guard lock(mutex_);
    auto config = validated(std::move(submitted));
    auto client = std::make_shared<QbWebClient>(config.url, config.username, config.password);
    persist(config);
    saved_ = std::move(config);
    current_ = std::move(client);
}

void QbConnectionManager::clear() {
    std::lock_guard lock(mutex_);
    auto fallback = std::make_shared<QbWebClient>(environment_.url,
        environment_.username, environment_.password);
    erase();
    saved_.reset();
    current_ = std::move(fallback);
}

std::wstring QbConnectionManager::credentialTarget() const {
    auto path = dataPath_.wstring();
    std::transform(path.begin(), path.end(), path.begin(), std::towlower);
    return L"AnimeVault/qb-webui/" + path;
}

std::optional<QbConnectionConfig> QbConnectionManager::loadSaved() const {
#ifdef _WIN32
    const auto target = credentialTarget();
    PCREDENTIALW raw = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
        if (GetLastError() == ERROR_NOT_FOUND) return std::nullopt;
        throw QbConfigError("qb_config_storage_failed", "cannot read qB credential");
    }
    const std::unique_ptr<CREDENTIALW, decltype(&CredFree)> owned(raw, &CredFree);
    try {
        const auto bytes = std::string(reinterpret_cast<const char*>(raw->CredentialBlob),
            raw->CredentialBlobSize);
        const auto value = nlohmann::json::parse(bytes);
        QbConnectionConfig config{value.at("url").get<std::string>(),
            value.at("username").get<std::string>(), value.at("password").get<std::string>()};
        QbWebClient::parseEndpoint(config.url);
        if (config.username.empty() || config.password.empty() || !printable(config.username) ||
            !printable(config.password)) throw std::invalid_argument("invalid credential");
        return config;
    } catch (const std::exception&) {
        throw QbConfigError("qb_config_corrupt", "saved qB credential is invalid");
    }
#else
    return std::nullopt;
#endif
}

void QbConnectionManager::persist(const QbConnectionConfig& config) const {
#ifdef _WIN32
    const auto blob = nlohmann::json{{"url", config.url}, {"username", config.username},
        {"password", config.password}}.dump();
    if (blob.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE)
        throw QbConfigError("invalid_qb_credentials", "qB credential is too large");
    const auto target = credentialTarget();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target.c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(blob.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&credential, 0))
        throw QbConfigError("qb_config_storage_failed", "cannot save qB credential");
#else
    (void)config;
    throw QbConfigError("qb_config_storage_unavailable", "qB credential storage requires Windows");
#endif
}

void QbConnectionManager::erase() const {
#ifdef _WIN32
    const auto target = credentialTarget();
    if (!CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) && GetLastError() != ERROR_NOT_FOUND)
        throw QbConfigError("qb_config_storage_failed", "cannot clear qB credential");
#endif
}
}

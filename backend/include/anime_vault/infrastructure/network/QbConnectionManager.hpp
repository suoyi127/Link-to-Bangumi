#pragma once

#include "anime_vault/infrastructure/network/QbWebClient.hpp"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>

namespace anime_vault {
struct QbConnectionConfig { std::string url, username, password; };
struct QbConfigSummary { std::string url, username, source; bool configured{}; };
class QbConfigError final : public std::runtime_error {
public:
    QbConfigError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code(std::move(code)) {}
    std::string code;
};

// 管理设置页与环境变量的配置优先级，并热切换当前 qB 客户端。
class QbConnectionManager final {
public:
    QbConnectionManager(std::filesystem::path dataPath, QbConnectionConfig environment);
    std::shared_ptr<QbWebClient> current() const;
    QbConfigSummary summary() const;
    QbConnectionConfig draft(QbConnectionConfig submitted) const;
    void save(QbConnectionConfig submitted);
    void clear();
private:
    QbConnectionConfig validated(QbConnectionConfig submitted) const;
    std::wstring credentialTarget() const;
    std::optional<QbConnectionConfig> loadSaved() const;
    void persist(const QbConnectionConfig& config) const;
    void erase() const;
    std::filesystem::path dataPath_;
    QbConnectionConfig environment_;
    mutable std::mutex mutex_;
    std::optional<QbConnectionConfig> saved_;
    std::shared_ptr<QbWebClient> current_;
};
}

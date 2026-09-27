#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace anime_vault {

struct ExecuteFileRequest {
    std::filesystem::path source;
    std::filesystem::path target;
    std::uintmax_t expectedSize{};
    std::filesystem::file_time_type expectedModifiedAt{};
    std::string operation{"hardlink"};
};

struct ExecuteFileResult {
    std::filesystem::path target;
    std::uintmax_t bytes{};
};

class OrganizationError final : public std::runtime_error {
public:
    explicit OrganizationError(std::string code);
    [[nodiscard]] const std::string& code() const noexcept { return code_; }

private:
    std::string code_;
};

class OrganizationExecutor {
public:
    OrganizationExecutor(std::filesystem::path sourceRoot, std::filesystem::path libraryRoot);
    [[nodiscard]] ExecuteFileResult execute(const ExecuteFileRequest& request) const;

private:
    std::filesystem::path sourceRoot_;
    std::filesystem::path libraryRoot_;
};

}  // namespace anime_vault

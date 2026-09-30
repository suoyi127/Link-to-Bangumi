#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace anime_vault {

// 启动器接收独立参数列表，避免业务逻辑通过拼接 shell 命令启动外部程序。
class ProcessLauncher {
public:
    virtual ~ProcessLauncher() = default;
    virtual bool launch(const std::filesystem::path& executable,
                        const std::vector<std::string>& arguments) = 0;
    virtual bool openDefault(const std::filesystem::path&) { return false; }
    virtual bool launchInDirectory(const std::filesystem::path& executable, const std::vector<std::string>& arguments, const std::filesystem::path&) { return launch(executable, arguments); }
};

class NativeProcessLauncher final : public ProcessLauncher {
public:
    bool launch(const std::filesystem::path& executable,
                const std::vector<std::string>& arguments) override;
    bool openDefault(const std::filesystem::path& file) override;
    bool launchInDirectory(const std::filesystem::path& executable, const std::vector<std::string>& arguments, const std::filesystem::path& directory) override;
};

} // namespace anime_vault

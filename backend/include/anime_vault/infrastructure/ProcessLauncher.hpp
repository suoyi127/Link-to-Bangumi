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
};

class NativeProcessLauncher final : public ProcessLauncher {
public:
    bool launch(const std::filesystem::path& executable,
                const std::vector<std::string>& arguments) override;
};

} // namespace anime_vault

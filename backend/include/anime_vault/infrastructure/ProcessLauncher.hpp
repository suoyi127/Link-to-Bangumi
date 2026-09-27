#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace anime_vault {

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

#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace anime_vault {

struct MpvPlaybackSamples {
    explicit MpvPlaybackSamples(std::string expectedPath = {});
    void event(std::string_view line, double wallSeconds);
    std::vector<std::pair<double,double>> ranges;
    double position{-1}, duration{};
    bool finished{}, flushRequested{}, dirty{}, matchedPath{};
private:
    std::string expectedPath_;
    double lastTime_{}, speed_{1};
    bool continuous_{}, paused_{};
};

} // namespace anime_vault

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace anime_vault {

class BangumiTransport {
public:
    struct Response { int status{}; std::string body; };
    using Completion = std::function<void(std::optional<Response>, std::string)>;
    virtual ~BangumiTransport() = default;
    virtual void search(std::string keyword, Completion completion) = 0;
    virtual void subject(std::int64_t id, Completion completion) = 0;
};

} // namespace anime_vault

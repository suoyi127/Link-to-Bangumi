#pragma once
#include "anime_vault/ports/VndbTransport.hpp"
#include <drogon/HttpClient.h>
#include <deque>
#include <mutex>
#include <chrono>
namespace anime_vault {
class DrogonVndbTransport final : public VndbTransport {
public:
    DrogonVndbTransport();
    void search(std::string query, VndbTransport::Completion completion) override;
    void subject(std::string id, VndbTransport::Completion completion) override;
    void fetch(std::string url, CoverImageFetcher::Completion completion) override;
private:
    struct Limits { std::mutex mutex; std::deque<std::chrono::steady_clock::time_point> requests; int active{}; };
    std::shared_ptr<Limits> limits_ = std::make_shared<Limits>();
    drogon::HttpClientPtr api_, images_;
    void query(std::string filters, VndbTransport::Completion completion);
    void send(bool image, std::string path, std::string body, VndbTransport::Completion completion);
};
}

#include "anime_vault/api/HealthController.hpp"

#include <drogon/drogon.h>

#include <functional>

namespace anime_vault::api {

void registerHealthEndpoint() {
    drogon::app().registerHandler(
        "/health",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto response = drogon::HttpResponse::newHttpResponse(
                drogon::k200OK, drogon::CT_APPLICATION_JSON);
            response->setBody(serializeHealthPayload(makeHealthPayload()));
            callback(response);
        },
        {drogon::Get});
}

}  // namespace anime_vault::api

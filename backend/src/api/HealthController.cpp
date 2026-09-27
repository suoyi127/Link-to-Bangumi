#include "anime_vault/api/HealthController.hpp"

#include <drogon/drogon.h>

#include <functional>
#include <utility>

namespace anime_vault::api {

void registerHealthEndpoint(std::string instanceToken) {
    drogon::app().registerHandler(
        "/health",
        [instanceToken = std::move(instanceToken)](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto response = drogon::HttpResponse::newHttpResponse(
                drogon::k200OK, drogon::CT_APPLICATION_JSON);
            response->setBody(serializeHealthPayload(makeHealthPayload(instanceToken)));
            callback(response);
        },
        {drogon::Get});
}

}  // namespace anime_vault::api

#include "NativeHttpClient.h"

#include <cpr/cpr.h>

#include <chrono>

namespace effindom::v2::native {

NativeHttpResponse NativeHttpClient::Get(
    std::string_view url,
    const std::shared_ptr<std::atomic_bool>& cancelled,
    std::size_t maximum_bytes) {
    NativeHttpResponse result;
    cpr::Session session;
    session.SetUrl(cpr::Url{std::string(url)});
    session.SetConnectTimeout(cpr::ConnectTimeout{std::chrono::seconds(5)});
    session.SetTimeout(cpr::Timeout{std::chrono::seconds(20)});
    session.SetRedirect(cpr::Redirect{5L});
    session.SetCancellationParam(cancelled);

    bool too_large = false;
    const cpr::Response response = session.Download(cpr::WriteCallback{
        [&result, &too_large, maximum_bytes](std::string_view chunk, std::intptr_t) {
            if (result.bytes.size() > maximum_bytes ||
                chunk.size() > maximum_bytes - result.bytes.size()) {
                too_large = true;
                return false;
            }
            result.bytes.insert(result.bytes.end(), chunk.begin(), chunk.end());
            return true;
        }});
    result.status = response.status_code;
    result.effective_url = response.url.str();
    result.error = response.error.message;
    result.cancelled = cancelled != nullptr && cancelled->load();
    result.too_large = too_large;
    const auto content_type = response.header.find("content-type");
    if (content_type != response.header.end()) result.content_type = content_type->second;
    if (result.too_large) {
        result.bytes.clear();
        result.error = "Remote asset exceeds the 32 MiB response limit.";
    }
    return result;
}

} // namespace effindom::v2::native

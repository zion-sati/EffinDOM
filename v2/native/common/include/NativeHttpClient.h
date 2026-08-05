#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace effindom::v2::native {

struct NativeHttpResponse {
    long status = 0;
    std::string content_type;
    std::string effective_url;
    std::string error;
    std::vector<std::uint8_t> bytes;
    bool cancelled = false;
    bool too_large = false;
};

class NativeHttpClient final {
public:
    static constexpr std::size_t kMaximumAssetBytes = 32U * 1024U * 1024U;

    static NativeHttpResponse Get(
        std::string_view url,
        const std::shared_ptr<std::atomic_bool>& cancelled,
        std::size_t maximum_bytes = kMaximumAssetBytes);
};

} // namespace effindom::v2::native

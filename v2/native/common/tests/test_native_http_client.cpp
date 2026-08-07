#include "NativeHttpClient.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket kInvalidSocket = -1;
#endif

namespace effindom::v2::native::tests {
namespace {

void CloseSocket(Socket socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

class LocalHttpServer final {
public:
    LocalHttpServer() {
#ifdef _WIN32
        WSADATA data{};
        REQUIRE(WSAStartup(MAKEWORD(2, 2), &data) == 0);
#endif
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        REQUIRE(listener_ != kInvalidSocket);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        REQUIRE(bind(listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0);
        REQUIRE(listen(listener_, 8) == 0);
        SocketLength length = sizeof(address);
        REQUIRE(getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) == 0);
        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] { Run(); });
    }

    ~LocalHttpServer() {
        stopping_.store(true);
        const Socket wake = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (wake != kInvalidSocket) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port_);
            (void)connect(wake, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            CloseSocket(wake);
        }
        if (thread_.joinable()) thread_.join();
        CloseSocket(listener_);
#ifdef _WIN32
        WSACleanup();
#endif
    }

    std::string Url(std::string_view path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
    }

    bool SawEffinDomUserAgent() const { return saw_effindom_user_agent_.load(); }

private:
    static void Send(Socket client, std::string_view response) {
        std::size_t sent = 0U;
        while (sent < response.size()) {
            const int count = send(
                client,
                response.data() + sent,
                static_cast<int>(response.size() - sent),
                0);
            if (count <= 0) return;
            sent += static_cast<std::size_t>(count);
        }
    }

    void Run() {
        while (!stopping_.load()) {
            const Socket client = accept(listener_, nullptr, nullptr);
            if (client == kInvalidSocket) continue;
            std::array<char, 2048> request{};
            const int length = recv(client, request.data(), static_cast<int>(request.size()), 0);
            if (stopping_.load()) {
                CloseSocket(client);
                break;
            }
            const std::string_view text(request.data(), length > 0 ? static_cast<std::size_t>(length) : 0U);
            if (text.find("User-Agent: EffinDOM-NativeAssetLoader/1.0") != std::string_view::npos) {
                saw_effindom_user_agent_.store(true);
            }
            if (text.find("GET /redirect ") != std::string_view::npos) {
                Send(client, "HTTP/1.1 302 Found\r\nLocation: /image\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            } else if (text.find("GET /large ") != std::string_view::npos) {
                Send(client, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 9\r\nConnection: close\r\n\r\n123456789");
            } else {
                Send(client, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length: 3\r\nConnection: close\r\n\r\nPNG");
            }
            CloseSocket(client);
        }
    }

    Socket listener_ = kInvalidSocket;
    std::uint16_t port_ = 0U;
    std::atomic_bool stopping_{false};
    std::atomic_bool saw_effindom_user_agent_{false};
    std::thread thread_;
};

} // namespace

TEST_CASE("native HTTP transport follows redirects and returns bounded bytes", "[v2][native][http]") {
    LocalHttpServer server;
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    const NativeHttpResponse direct = NativeHttpClient::Get(server.Url("/image"), cancelled, 8U);
    CHECK(direct.status == 200);
    CHECK(direct.bytes == std::vector<std::uint8_t>{'P', 'N', 'G'});
    CHECK(direct.content_type.find("image/png") != std::string::npos);
    CHECK(server.SawEffinDomUserAgent());

    const NativeHttpResponse redirected = NativeHttpClient::Get(server.Url("/redirect"), cancelled, 8U);
    CHECK(redirected.status == 200);
    CHECK(redirected.bytes == std::vector<std::uint8_t>{'P', 'N', 'G'});
    CHECK(redirected.effective_url.find("/image") != std::string::npos);

    const NativeHttpResponse oversized = NativeHttpClient::Get(server.Url("/large"), cancelled, 8U);
    CHECK(oversized.too_large);
    CHECK(oversized.bytes.empty());
}

} // namespace effindom::v2::native::tests

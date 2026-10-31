#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
// macOS port: BSD sockets under the Winsock names this fixture uses.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
inline int closesocket(SOCKET socket) { return close(socket); }
#endif

#include "achievements_http.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace {

class LoopbackServer {
public:
    explicit LoopbackServer(bool stall) : stall_(stall) {
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == INVALID_SOCKET) throw std::runtime_error("loopback socket failed");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 || listen(listener_, 1) != 0) {
            closesocket(listener_);
            throw std::runtime_error("loopback bind/listen failed");
        }
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            closesocket(listener_);
            throw std::runtime_error("loopback port lookup failed");
        }
        url_ = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) + "/slow?test=1";
        worker_ = std::thread([this] { serve(); });
    }

    ~LoopbackServer() {
        {
            std::lock_guard lock(mutex_);
            released_ = true;
        }
        changed_.notify_all();
        closesocket(listener_);
        if (worker_.joinable()) worker_.join();
    }

    const std::string& url() const { return url_; }

    bool wait_for_request() {
        std::unique_lock lock(mutex_);
        return changed_.wait_for(lock, 5s, [this] { return received_; });
    }

private:
    void serve() {
        fd_set waiting;
        FD_ZERO(&waiting);
        FD_SET(listener_, &waiting);
        timeval timeout{3, 0};
        if (select(static_cast<int>(listener_) + 1, &waiting, nullptr, nullptr, &timeout) <= 0) return;
        SOCKET client = accept(listener_, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
#ifdef _WIN32
        DWORD receive_timeout = 1000;
#else
        timeval receive_timeout{1, 0};
#endif
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receive_timeout), sizeof(receive_timeout));
        char request[4096]{};
        const int read = recv(client, request, sizeof(request), 0);
        {
            std::unique_lock lock(mutex_);
            received_ = read > 0;
            changed_.notify_all();
            if (stall_) changed_.wait_for(lock, 5s, [this] { return released_; });
        }
        if (!stall_ && read > 0) {
            constexpr char reply[] = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
            send(client, reply, static_cast<int>(sizeof(reply) - 1), 0);
        }
        closesocket(client);
    }

    bool stall_;
    bool received_ = false;
    bool released_ = false;
    SOCKET listener_ = INVALID_SOCKET;
    std::string url_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable changed_;
};

int test_cancellation_and_restart() {
    gt4::achievements::HttpTransport transport;
    for (int attempt = 0; attempt < 3; ++attempt) {
        transport.reset();
        LoopbackServer server(true);
        gt4::achievements::HttpResult result;
        std::thread worker([&] { result = transport.request(server.url(), "", "", L"GT4Recomp fixture"); });
        const bool reached_server = server.wait_for_request();
        const auto start = std::chrono::steady_clock::now();
        // Production shutdown uses this exact cancel-before-join order.
        transport.cancel();
        worker.join();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (!reached_server || elapsed > 2s || result.status_code == 200) {
            std::fprintf(stderr, "FAIL: slow loopback cancellation (attempt %d, reached=%d, duration=%lld ms)\n",
                attempt, reached_server ? 1 : 0,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()));
            return 1;
        }
    }
    // A cancelled transport reports failure; reset enables a later successful session.
    if (transport.request("http://127.0.0.1:1/", "", "", L"GT4Recomp fixture").status_code == 200) return 1;
    transport.reset();
    LoopbackServer server(false);
    const auto result = transport.request(server.url(), "", "", L"GT4Recomp fixture");
    if (result.status_code != 200 || result.body != "OK") {
        std::fprintf(stderr, "FAIL: transport did not restart after cancellation\n");
        return 1;
    }
    return 0;
}

} // namespace

int main() {
#ifdef _WIN32
    WSADATA winsock{};
    if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) return 1;
#endif
    int result = 1;
    try {
        result = test_cancellation_and_restart();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
    }
#ifdef _WIN32
    WSACleanup();
#endif
    if (result == 0) std::puts("achievements_http_fixture: loopback cancellation and restart passed.");
    return result;
}

// macOS / POSIX port of achievements_http.cpp: the same HttpTransport on libcurl (the system library on macOS).
// Each request runs on its own multi handle; cancel() sets a flag and wakes curl_multi_poll, so a request blocked on a
// silent server returns at once, as closing the WinHTTP request handle does on Windows.
#include "achievements_http.hpp"
#include "rc_api_request.h"

#include <curl/curl.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

namespace gt4::achievements {
namespace {

// UTF-32 (wchar_t on POSIX) to UTF-8, for the user agent.
std::string narrow(const std::wstring& text) {
    std::string out;
    for (const wchar_t wide : text) {
        const auto code = static_cast<std::uint32_t>(wide);
        if (code < 0x80) {
            out.push_back(static_cast<char>(code));
        } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
    return out;
}

size_t append_body(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

// One request. The multi handle stays valid while any shared_ptr holds the request, so cancel() may wake it from
// another thread at any time.
struct Request {
    CURLM* multi = nullptr;
    CURL* easy = nullptr;
    curl_slist* headers = nullptr;
    std::atomic<bool> cancelled{false};

    ~Request() {
        if (multi && easy) curl_multi_remove_handle(multi, easy);
        if (easy) curl_easy_cleanup(easy);
        if (headers) curl_slist_free_all(headers);
        if (multi) curl_multi_cleanup(multi);
    }

    void cancel() {
        cancelled = true;
        curl_multi_wakeup(multi);
    }
};

void global_init() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

} // namespace

struct HttpTransport::Impl {
    std::mutex mutex;
    bool cancelled = false;
    std::shared_ptr<Request> active;
};

HttpTransport::HttpTransport() : impl_(std::make_unique<Impl>()) { global_init(); }
HttpTransport::~HttpTransport() { cancel(); }

void HttpTransport::cancel() {
    std::shared_ptr<Request> active;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->cancelled = true;
        active = impl_->active;
    }
    if (active) active->cancel();
}

void HttpTransport::reset() {
    std::lock_guard lock(impl_->mutex);
    impl_->cancelled = false;
}

HttpResult HttpTransport::request(const std::string& url, const std::string& post_data,
                                  const std::string& content_type, const std::wstring& user_agent) {
    HttpResult result;
    result.status_code = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->cancelled) return result;
    }
    auto active = std::make_shared<Request>();
    active->multi = curl_multi_init();
    active->easy = curl_easy_init();
    if (!active->multi || !active->easy) return result;
    CURL* easy = active->easy;
    const std::string agent = narrow(user_agent);
    std::string body;
    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_USERAGENT, agent.c_str());
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &body);
    // WinHTTP timeouts of the Windows transport: 8 s resolve and connect, 15 s without received data.
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(easy, CURLOPT_LOW_SPEED_TIME, 15L);
    if (!post_data.empty()) {
        curl_easy_setopt(easy, CURLOPT_POST, 1L);
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS, post_data.data());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(post_data.size()));
        if (!content_type.empty()) {
            active->headers = curl_slist_append(nullptr, ("Content-Type: " + content_type).c_str());
            curl_easy_setopt(easy, CURLOPT_HTTPHEADER, active->headers);
        }
    }
    if (curl_multi_add_handle(active->multi, easy) != CURLM_OK) return result;
    {
        std::lock_guard lock(impl_->mutex);
        impl_->active = active;
        if (impl_->cancelled) active->cancel();
    }
    CURLcode outcome = CURLE_ABORTED_BY_CALLBACK;
    bool finished = false;
    while (!active->cancelled) {
        int running = 0;
        if (curl_multi_perform(active->multi, &running) != CURLM_OK) break;
        int queued = 0;
        while (CURLMsg* message = curl_multi_info_read(active->multi, &queued)) {
            if (message->msg == CURLMSG_DONE) {
                outcome = message->data.result;
                finished = true;
            }
        }
        if (finished || !running) break;
        if (curl_multi_poll(active->multi, nullptr, 0, 1000, nullptr) != CURLM_OK) break;
    }
    if (finished && outcome == CURLE_OK && !active->cancelled) {
        long code = 0;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &code);
        result.status_code = static_cast<int>(code);
        result.body = std::move(body);
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->active.reset();
    }
    return result;
}

} // namespace gt4::achievements

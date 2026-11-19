#pragma once

#include "slc/common/error.h"

#include <chrono>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace slc::content {

struct HttpRequest {
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::chrono::milliseconds connectTimeout{3000};
    std::chrono::milliseconds totalTimeout{10000};
};

struct HttpResponse {
    int status = 0; // 0 = transport failure (timeout, DNS, TLS, ...)
    std::string body;
    std::unordered_map<std::string, std::string> headers; // lower-cased
};

/**
 * Pluggable HTTP backend so `slc_content` stays cpr-free (doc 6.3 core
 * isolation). Production uses CprTransport (slc_enrichment, built only with
 * SLC_WITH_CPR); tests inject a mock.
 */
class IHttpTransport {
  public:
    virtual ~IHttpTransport() = default;
    virtual Result<HttpResponse> get(const HttpRequest& request,
                                     std::stop_token stop) = 0;
    /// POST `body` with the request headers. Default: unsupported — only
    /// transports that implement it (cpr) can serve POSTing clients.
    virtual Result<HttpResponse> post(const HttpRequest& request,
                                      const std::string& body,
                                      std::stop_token stop)
    {
        (void)request;
        (void)body;
        (void)stop;
        return std::unexpected(
            Error::make(ErrorCode::Unsupported, "POST not supported"));
    }
};

} // namespace slc::content

// cpr-backed IHttpTransport — compiled only when SLC_WITH_CPR is set
// (vcpkg feature "enrichment": cpr + openssl).
#include "transport_factory.h"

#include <cpr/cpr.h>

#include <algorithm>
#include <cctype>
#include <chrono>

namespace slc::enrichment {
namespace {

// cpr header names keep their original case; our contract wants lower-case.
std::string lower(std::string s)
{
    std::ranges::transform(s, s.begin(),
                           [](unsigned char c) { return std::tolower(c); });
    return s;
}

Result<content::HttpResponse> convert(cpr::Response& r)
{
    content::HttpResponse out;
    out.status = static_cast<int>(r.status_code);
    if (r.error.code != cpr::ErrorCode::OK) {
        out.status = 0;
        if (r.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT) {
            return std::unexpected(
                Error::make(ErrorCode::Timeout, r.error.message));
        }
        return std::unexpected(
            Error::make(ErrorCode::IoError, r.error.message));
    }
    out.body = std::move(r.text);
    for (const auto& [k, v] : r.header) {
        out.headers[lower(k)] = v;
    }
    return out;
}

class CprTransport final : public content::IHttpTransport {
  public:
    Result<content::HttpResponse> get(const content::HttpRequest& req,
                                    std::stop_token stop) override
    {
        cpr::Header header;
        for (const auto& [k, v] : req.headers) {
            header[k] = v;
        }
        // cpr has no native cancellation; it blocks up to the total
        // timeout. The stop token is still checked eagerly for the common
        // "cancelled while queued" case.
        if (stop.stop_requested()) {
            return std::unexpected(
                Error::make(ErrorCode::Timeout, "cancelled"));
        }
        cpr::Response r = cpr::Get(
            cpr::Url{req.url}, header,
            cpr::ConnectTimeout{req.connectTimeout},
            cpr::Timeout{req.totalTimeout});
        return convert(r);
    }

    Result<content::HttpResponse> post(const content::HttpRequest& req,
                                       const std::string& body,
                                       std::stop_token stop) override
    {
        cpr::Header header;
        for (const auto& [k, v] : req.headers) {
            header[k] = v;
        }
        if (stop.stop_requested()) {
            return std::unexpected(
                Error::make(ErrorCode::Timeout, "cancelled"));
        }
        cpr::Response r = cpr::Post(
            cpr::Url{req.url}, header, cpr::Body{body},
            cpr::ConnectTimeout{req.connectTimeout},
            cpr::Timeout{req.totalTimeout});
        return convert(r);
    }
};

} // namespace

std::unique_ptr<content::IHttpTransport> makeCprTransport()
{
    return std::make_unique<CprTransport>();
}

} // namespace slc::enrichment

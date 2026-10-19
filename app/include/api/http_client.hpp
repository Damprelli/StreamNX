#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

// Minimal async HTTP GET: does the actual request on a background thread,
// then always calls back on the main thread (via brls::sync). `alive` is
// the same shared_ptr<bool> pattern LoadingGate uses -- own one per screen
// (flip it false in the screen's destructor) so a request that outlives
// its screen (user navigated away before it finished) silently drops its
// result instead of touching a freed view.
namespace http
{

using AliveFlag = std::shared_ptr<bool>;

void getAsync(const std::string& url, AliveFlag alive, std::function<void(const std::string&)> onSuccess,
    std::function<void(const std::string&)> onError);

// A full request -- any method, optional JSON body, optional bearer token --
// for talking to the profile backend (see api/cloud_sync.hpp). Unlike
// getAsync(), a 4xx/5xx is not an error here: the backend's error body
// carries the user-facing message, so status + body always come back and
// `error` is only set when no HTTP response arrived at all (offline, DNS,
// TLS, timeout).
struct Response
{
    long status = 0;
    std::string body;
    std::string error;
};

// Blocking: runs on the calling thread. Meant for a background worker that
// needs its requests to happen in order (cloud_sync's own queue). Setting
// *cancel aborts a request in flight -- so app shutdown never has to wait
// out a full timeout when the network is gone.
Response request(const std::string& method, const std::string& url, const std::string& jsonBody = "",
    const std::string& bearerToken = "", const std::atomic<bool>* cancel = nullptr);

// Must be called once before any getAsync() call (main() does this) and
// cleaned up once at shutdown.
void globalInit();
void globalCleanup();

} // namespace http

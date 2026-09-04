#pragma once

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

// Must be called once before any getAsync() call (main() does this) and
// cleaned up once at shutdown.
void globalInit();
void globalCleanup();

} // namespace http

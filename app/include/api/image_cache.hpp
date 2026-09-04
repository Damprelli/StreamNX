#pragma once

#include <borealis.hpp>

#include <functional>

#include "api/http_client.hpp"

// Loads a remote image into an `Image` view, the same way every screen in
// this app already did it (DetailScreen's poster/backdrop, now also
// MediaCard's poster) -- except this keeps a copy of every image it
// downloads on disk, keyed by URL, so switching screens or reopening the
// same title never re-fetches art that's already been seen once.
namespace imgcache
{

// `onLoaded` (optional) fires right after the image is actually set --
// whether that came from disk (synchronously, before `load()` even
// returns) or from the network a moment later -- so callers that need to
// swap some placeholder out only do it once art is genuinely showing.
void load(brls::Image* image, const std::string& url, http::AliveFlag alive, std::function<void()> onLoaded = nullptr);

} // namespace imgcache

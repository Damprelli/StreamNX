#include "api/image_cache.hpp"

#ifdef USE_WEBP
#include <webp/decode.h>
#endif

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace
{

constexpr const char* CACHE_DIR = "cache/images";

bool isWebP(const std::string& data)
{
    // RIFF????WEBP -- the standard container signature, checked on the
    // bytes themselves rather than the URL or a Content-Type header (this
    // app's http layer doesn't expose response headers to callers, and a
    // poster URL's extension is not reliably the real format anyway).
    return data.size() >= 12 && data.compare(0, 4, "RIFF") == 0 && data.compare(8, 4, "WEBP") == 0;
}

// borealis' own Image::setImageFromMem() decodes through stb_image, which
// has no WebP support at all -- it just logs "unknown image type" and
// leaves the view showing nothing. Some poster CDNs (RPDB in particular)
// serve WebP exclusively, which otherwise means those cards are stuck on
// their placeholder forever. Decode those ourselves with libwebp and hand
// the raw RGBA straight to nanovg the same way SVGImage does for rasterized
// SVGs; everything else still goes through the normal stb_image path.
void applyImage(brls::Image* image, const std::string& data)
{
#ifdef USE_WEBP
    if (isWebP(data))
    {
        int width = 0, height = 0;
        uint8_t* pixels = WebPDecodeRGBA(
            reinterpret_cast<const uint8_t*>(data.data()), data.size(), &width, &height);
        if (pixels)
        {
            NVGcontext* vg = brls::Application::getNVGContext();
            int tex        = nvgCreateImageRGBA(vg, width, height, 0, pixels);
            WebPFree(pixels);
            if (tex > 0)
            {
                image->innerSetImage(tex);
                return;
            }
            brls::Logger::error("imgcache: WebP decoded but texture creation failed");
        }
        else
        {
            brls::Logger::error("imgcache: WebP decode failed");
        }
        return;
    }
#endif
    image->setImageFromMem(reinterpret_cast<const unsigned char*>(data.data()), (int)data.size());
}

// A short, filesystem-safe stand-in for the URL -- collisions are of no
// consequence here (worst case, two URLs share a cached image and one of
// them just re-fetches next launch), so a fast non-cryptographic hash is
// enough.
std::string hashOf(const std::string& url)
{
    uint64_t hash = 1469598103934665603ull; // FNV-1a offset basis
    for (unsigned char c : url)
    {
        hash ^= c;
        hash *= 1099511628211ull;
    }

    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
    return std::string(buf);
}

bool readFile(const std::string& path, std::string& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;

    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void writeFile(const std::string& path, const std::string& data)
{
    try
    {
        std::filesystem::create_directories(CACHE_DIR);
        std::ofstream out(path, std::ios::binary);
        if (out.is_open())
            out.write(data.data(), (std::streamsize)data.size());
    }
    catch (const std::exception&)
    {
        // Not fatal -- the image already loaded from memory, it just won't
        // be cached for next time.
    }
}

} // namespace

namespace imgcache
{

void load(brls::Image* image, const std::string& url, http::AliveFlag alive, std::function<void()> onLoaded)
{
    if (url.empty() || !image)
        return;

    std::string path = std::string(CACHE_DIR) + "/" + hashOf(url);

    std::string cached;
    if (readFile(path, cached))
    {
        applyImage(image, cached);
        if (onLoaded)
            onLoaded();
        return;
    }

    // Posters sit behind the same network conditions that made a
    // reconnect/timeout policy necessary for video streams -- a transient
    // hiccup (or a request that just got queued behind a burst of others
    // in the shared http thread pool and timed out waiting) shouldn't
    // permanently leave a card on its placeholder. One retry catches most
    // of those; a second real failure logs so a *persistent* miss (a genu-
    // inely bad poster URL, an unsupported image format) is at least
    // visible instead of silently indistinguishable from "no poster".
    auto attempt = std::make_shared<int>(0);
    auto fetch   = std::make_shared<std::function<void()>>();
    *fetch       = [image, path, onLoaded, url, alive, attempt, fetch]() {
        http::getAsync(
            url, alive,
            [image, path, onLoaded](const std::string& body) {
                applyImage(image, body);
                writeFile(path, body);
                if (onLoaded)
                    onLoaded();
            },
            [url, attempt, fetch](const std::string& error) {
                if (++(*attempt) <= 1)
                {
                    (*fetch)();
                    return;
                }
                brls::Logger::warning("imgcache: giving up on {} after retry: {}", url, error);
            });
    };
    (*fetch)();
}

} // namespace imgcache

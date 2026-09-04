#pragma once

#include <borealis.hpp>

#include <memory>
#include <string>
#include <vector>

#include "api/stremio_api.hpp"

// Lists every stream an enabled addon reports for one specific playable
// video (a movie, or one series episode): name/title (whatever quality info
// the addon embedded there) plus which addon it came from. Informational
// only -- this app has no video player, so selecting a stream just shows
// its full title, it never attempts playback.
class StreamListScreen : public brls::Box
{
  public:
    // `videoId` is the movie's own id, or (for a series) the specific
    // episode's video id from MetaDetail::videos. `subtitle` is shown under
    // the header, e.g. "Temporada 1 - Episodio 2". `showId`/`posterUrl` are
    // the show's own (not the episode's) id and poster art -- recorded into
    // Continuar Assistindo when a stream is opened, so a series' episodes
    // all update the same entry instead of one each.
    StreamListScreen(const std::string& type, const std::string& videoId, const std::string& title,
        const std::string& subtitle, const std::string& showId, const std::string& posterUrl);
    ~StreamListScreen();

    // This screen parks focus on itself (setFocusable(true)) while the
    // stream list is still loading, so Box's default getDefaultFocus()
    // (which returns `this` whenever it's focusable, before ever looking at
    // children) would otherwise keep resolving to the screen itself forever
    // -- including from places outside this file, like
    // Application::popActivity()'s own fallback when restoring focus after
    // the player closes. Overriding it to defer to whatever the real
    // content ends up being (once `content` is set in finish()) fixes every
    // caller at once instead of special-casing each one.
    brls::View* getDefaultFocus() override;

  private:
    void queryAddon(size_t addonIndex);
    void finish();
    void play(const stremio::StreamEntry& entry);

    std::string type;
    std::string videoId;
    std::string title;
    std::string subtitle;
    std::string showId;
    std::string posterUrl;

    brls::View* loadingGate = nullptr;
    // The list (or the empty-state box), once finish() builds it -- see
    // getDefaultFocus() above.
    brls::Box* content = nullptr;
    std::vector<stremio::StreamEntry> collected;

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

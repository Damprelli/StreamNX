#pragma once

#include <string>
#include <vector>

#include "model/addon_list_store.hpp"

// Addons that answer the protocol's "subtitles" resource
// (GET {addonUrl}/subtitles/{type}/{id}.json -- OpenSubtitles and the like):
// never asked for streams, and stream addons never asked for subtitles.
// Plus the preferred language used to filter (and pick) what they return.
class SubtitleAddonStore : public AddonListStore
{
  public:
    struct Language
    {
        std::string code;  // what's persisted ("pob", "eng", ..., "all")
        std::string label; // shown on the Addons screen
        // Lowercased values an addon's "lang" field may use for it, best
        // match first -- addons disagree on ISO 639-2 vs 639-1 vs names.
        std::vector<std::string> aliases;
    };

    static SubtitleAddonStore& instance();
    static const std::vector<Language>& languages();

    const Language& language() const;
    void cycleLanguage();

    // Where a subtitle (its `lang`, or `langCode` when the addon sends one)
    // ranks for the preferred language: 0 is the best match, -1 means "not
    // this language" (never -1 when the preference is "all").
    int languageRank(const std::string& lang, const std::string& langCode = "") const;

  private:
    SubtitleAddonStore();
};

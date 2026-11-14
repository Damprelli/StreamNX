#include "model/subtitle_addon_store.hpp"

#include <algorithm>
#include <cctype>

namespace
{

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

} // namespace

SubtitleAddonStore::SubtitleAddonStore()
    : AddonListStore("subtitle_addons.json", "Esse addon de legendas ja foi adicionado.")
{
}

SubtitleAddonStore& SubtitleAddonStore::instance()
{
    static SubtitleAddonStore store;
    return store;
}

const std::vector<SubtitleAddonStore::Language>& SubtitleAddonStore::languages()
{
    // Brazilian Portuguese falls back to European Portuguese (ranked after
    // it) rather than showing nothing -- most catalogs only have one or the
    // other for a given title.
    static const std::vector<Language> list = {
        { "pob", "Portugues (BR)",
            { "pob", "pt-br", "pt_br", "ptbr", "bra", "portuguese (br)", "portuguese (brazil)", "portuguese-br",
                "brazilian portuguese", "brazilian", "por", "pt", "pt-pt", "portuguese", "portuguese (pt)" } },
        { "por", "Portugues (PT)",
            { "por", "pt", "pt-pt", "pt_pt", "portuguese", "portuguese (pt)", "portuguese (portugal)" } },
        { "eng", "Ingles", { "eng", "en", "en-us", "en-gb", "english" } },
        { "spa", "Espanhol",
            { "spa", "es", "spn", "es-es", "es-mx", "es-419", "ea", "spanish", "spanish (la)", "spanish-la",
                "spanish (latin america)", "spanish (spain)" } },
        { "all", "Todos os idiomas", {} },
    };
    return list;
}

const SubtitleAddonStore::Language& SubtitleAddonStore::language() const
{
    std::string code = this->setting("language", "pob");
    for (auto& l : languages())
        if (l.code == code)
            return l;
    return languages().front();
}

void SubtitleAddonStore::cycleLanguage()
{
    auto& list = languages();
    size_t idx = 0;
    for (size_t i = 0; i < list.size(); i++)
        if (list[i].code == this->language().code)
            idx = i;
    this->setSetting("language", list[(idx + 1) % list.size()].code);
}

int SubtitleAddonStore::languageRank(const std::string& lang, const std::string& langCode) const
{
    auto& aliases = this->language().aliases;
    if (aliases.empty())
        return 0;

    int best = -1;
    for (const std::string* value : { &lang, &langCode })
    {
        std::string l = lower(*value);
        for (size_t i = 0; i < aliases.size() && !l.empty(); i++)
            if (aliases[i] == l && (best < 0 || (int)i < best))
                best = (int)i;
    }
    return best;
}

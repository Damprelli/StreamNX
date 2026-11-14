#pragma once

#include "model/addon_list_store.hpp"

// Addons added for their catalogs/metadata (Kitsu, TMDB, MDBList...),
// alongside the always-on Cinemeta: their manifests' movie/series catalogs
// show up in Buscar, the ones that support it are searched, and Detail
// asks them for a title's meta when Cinemeta doesn't know it (e.g. a
// "kitsu:" id). Never asked for streams.
class CatalogAddonStore : public AddonListStore
{
  public:
    static CatalogAddonStore& instance();

  private:
    CatalogAddonStore();
};

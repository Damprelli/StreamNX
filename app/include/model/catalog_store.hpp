#pragma once

#include <string>
#include <vector>

#include "model/catalog.hpp"

// Persistent list of extra catalogs shown in Buscar's results (in addition
// to whatever each enabled addon's manifest itself declares) -- mirrors
// AddonStore's shape. Auto-seeded on first run with Cinemeta's known
// genre/year/rating catalog variants, all enabled.
class CatalogStore
{
  public:
    static CatalogStore& instance();

    void load();
    const std::vector<Catalog>& all() const { return items; }

    void toggle(size_t index);

  private:
    void save();

    bool loaded = false;
    std::vector<Catalog> items;
};

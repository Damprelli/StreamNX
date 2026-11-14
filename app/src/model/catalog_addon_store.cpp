#include "model/catalog_addon_store.hpp"

CatalogAddonStore::CatalogAddonStore()
    : AddonListStore("catalog_addons.json", "Esse addon de catalogo ja foi adicionado.")
{
}

CatalogAddonStore& CatalogAddonStore::instance()
{
    static CatalogAddonStore store;
    return store;
}

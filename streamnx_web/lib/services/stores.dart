// shared_preferences-backed equivalents of app/src/model/*_store.cpp --
// same shapes, same rules (dedup, MAX_ITEMS, CINEMETA_URL never stored as
// an addon). On Flutter Web, shared_preferences itself is a thin wrapper
// over the browser's localStorage, so this is the same persistence
// mechanism the plain-JS version used, just through Dart's idiomatic API.
//
// Each store's load() is called once in main() before runApp(); after
// that, all() is synchronous (reads the in-memory cache), matching the
// C++ stores' own "load once, mutate + save in memory after" shape.
import 'dart:convert';
import 'package:shared_preferences/shared_preferences.dart';

import '../models/models.dart';
import 'stremio_api.dart';

late SharedPreferences _prefs;

Future<void> initStores() async {
  _prefs = await SharedPreferences.getInstance();
  await AddonStore.load();
  await CatalogStore.load();
  await FavoritesStore.load();
  await ContinueWatchingStore.load();
}

List<Map<String, dynamic>> _loadList(String key) {
  final raw = _prefs.getString(key);
  if (raw == null) return [];
  try {
    final decoded = jsonDecode(raw);
    if (decoded is List) return decoded.cast<Map<String, dynamic>>();
  } catch (_) {}
  return [];
}

Future<void> _saveList(String key, List<Map<String, dynamic>> items) =>
    _prefs.setString(key, jsonEncode(items));

class AddonStore {
  static const _key = 'addons';
  static List<Addon> _items = [];

  static Future<void> load() async {
    _items = _loadList(_key).map(Addon.fromJson).where((a) => a.url != StremioApi.cinemetaUrl).toList();
  }

  static Future<void> _save() => _saveList(_key, _items.map((a) => a.toJson()).toList());

  static List<Addon> all() => _items;

  /// Returns "" on success, an error message otherwise -- same validation
  /// order as AddonStore::add() in the C++ app.
  static Future<String> add(String rawUrl) async {
    var url = rawUrl.trim().replaceAll(RegExp(r'^["\x27]+|["\x27]+$'), '');
    if (url.isEmpty) return 'Digite a URL do manifesto do addon.';
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
      return 'A URL precisa comecar com http:// ou https://';
    }
    if (url.endsWith('manifest.json')) url = url.substring(0, url.length - 'manifest.json'.length);
    while (url.endsWith('/')) {
      url = url.substring(0, url.length - 1);
    }
    if (url.isEmpty) return 'URL de addon invalida.';
    if (url == StremioApi.cinemetaUrl) return 'Cinemeta ja esta sempre ativo, nao precisa adicionar.';
    if (_items.any((a) => a.url == url)) return 'Esse addon ja foi adicionado.';

    _items.add(Addon(url: url));
    await _save();
    return '';
  }

  static Future<void> remove(int index) async {
    if (index < 0 || index >= _items.length) return;
    _items.removeAt(index);
    await _save();
  }

  static Future<void> toggle(int index) async {
    if (index < 0 || index >= _items.length) return;
    _items[index].enabled = !_items[index].enabled;
    await _save();
  }
}

class CatalogStore {
  static const _key = 'catalogs';
  static List<Catalog> _items = [];

  static List<Catalog> _defaults() {
    final cm = StremioApi.cinemetaUrl;
    final year = DateTime.now().year;
    final defs = [
      ('cinemeta:popular-movies', 'Filmes Populares', '$cm/catalog/movie/top.json'),
      ('cinemeta:popular-series', 'Series Populares', '$cm/catalog/series/top.json'),
      ('cinemeta:new-movies', 'Filmes Novos', '$cm/catalog/movie/year/genre=$year.json'),
      ('cinemeta:new-series', 'Series Novas', '$cm/catalog/series/year/genre=$year.json'),
      ('cinemeta:top-movies', 'Filmes Mais Bem Avaliados', '$cm/catalog/movie/imdbRating.json'),
      ('cinemeta:top-series', 'Series Mais Bem Avaliadas', '$cm/catalog/series/imdbRating.json'),
      ('cinemeta:animation-movies', 'Filmes de Animacao', '$cm/catalog/movie/top/genre=Animation.json'),
      ('cinemeta:animation-series', 'Series de Animacao', '$cm/catalog/series/top/genre=Animation.json'),
      ('cinemeta:documentary-movies', 'Documentarios', '$cm/catalog/movie/top/genre=Documentary.json'),
      ('cinemeta:documentary-series', 'Documentarios (Series)', '$cm/catalog/series/top/genre=Documentary.json'),
    ];
    return defs.map((d) => Catalog(key: d.$1, title: d.$2, url: d.$3)).toList();
  }

  static Future<void> load() async {
    final loaded = _loadList(_key);
    if (loaded.isEmpty) {
      _items = _defaults();
      await _save();
    } else {
      _items = loaded.map(Catalog.fromJson).toList();
    }
  }

  static Future<void> _save() => _saveList(_key, _items.map((c) => c.toJson()).toList());

  static List<Catalog> all() => _items;

  static Future<void> toggle(int index) async {
    if (index < 0 || index >= _items.length) return;
    _items[index].enabled = !_items[index].enabled;
    await _save();
  }
}

class FavoritesStore {
  static const _key = 'favorites';
  static List<MetaSummary> _items = [];

  static Future<void> load() async {
    _items = _loadList(_key).map(MetaSummary.fromJson).toList();
  }

  static Future<void> _save() => _saveList(_key, _items.map((f) => f.toJson()).toList());

  static List<MetaSummary> all() => _items;

  static bool contains(String id) => _items.any((f) => f.id == id);

  static Future<void> add(MetaSummary item) async {
    if (contains(item.id)) return;
    _items.add(item);
    await _save();
  }

  static Future<void> remove(String id) async {
    _items = _items.where((f) => f.id != id).toList();
    await _save();
  }
}

class ContinueWatchingStore {
  static const _key = 'continue_watching';
  static const maxItems = 20;
  static List<ContinueWatching> _items = [];

  static Future<void> load() async {
    _items = _loadList(_key).map(ContinueWatching.fromJson).toList();
  }

  static Future<void> _save() => _saveList(_key, _items.map((c) => c.toJson()).toList());

  static List<ContinueWatching> all() => _items;

  static Future<void> touch(ContinueWatching item) async {
    _items = _items.where((c) => c.id != item.id).toList();
    _items.insert(0, item);
    if (_items.length > maxItems) _items = _items.sublist(0, maxItems);
    await _save();
  }

  static Future<void> remove(String id) async {
    _items = _items.where((c) => c.id != id).toList();
    await _save();
  }
}

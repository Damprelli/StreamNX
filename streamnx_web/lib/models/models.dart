// Mirrors the shapes in app/include/api/stremio_api.hpp and
// app/include/model/*.hpp -- same fields, same JSON keys, so
// stremio_api.dart / stores.dart stay straight ports of the C++ originals.

class Addon {
  final String url;
  bool enabled;

  Addon({required this.url, this.enabled = true});

  factory Addon.fromJson(Map<String, dynamic> j) =>
      Addon(url: j['url'] as String? ?? '', enabled: j['enabled'] as bool? ?? true);

  Map<String, dynamic> toJson() => {'url': url, 'enabled': enabled};
}

class Catalog {
  final String key;
  final String title;
  final String url;
  bool enabled;

  Catalog({required this.key, required this.title, required this.url, this.enabled = true});

  factory Catalog.fromJson(Map<String, dynamic> j) => Catalog(
        key: j['key'] as String? ?? '',
        title: j['title'] as String? ?? '',
        url: j['url'] as String? ?? '',
        enabled: j['enabled'] as bool? ?? true,
      );

  Map<String, dynamic> toJson() => {'key': key, 'title': title, 'url': url, 'enabled': enabled};
}

class CatalogRef {
  final String type;
  final String id;
  final String name;
  CatalogRef({required this.type, required this.id, required this.name});
}

class ManifestInfo {
  final String name;
  final List<CatalogRef> catalogs;
  ManifestInfo({required this.name, required this.catalogs});
}

/// One item in a catalog listing, or a Favorite/ContinueWatching entry --
/// same shape covers all three (poster/year/rating optional depending on
/// where it came from), same as the C++ side sharing MetaSummary/Favorite/
/// ContinueWatching's near-identical fields.
class MetaSummary {
  final String id;
  final String type;
  final String name;
  final String poster;
  final String year;
  final String imdbRating;

  MetaSummary({
    required this.id,
    required this.type,
    required this.name,
    this.poster = '',
    this.year = '',
    this.imdbRating = '',
  });

  factory MetaSummary.fromJson(Map<String, dynamic> j) {
    String yearOf() {
      final ri = j['releaseInfo'];
      if (ri is String && ri.isNotEmpty) return ri;
      final y = j['year'];
      if (y is String) return y;
      if (y is num) return y.toInt().toString();
      return '';
    }

    return MetaSummary(
      id: (j['id'] ?? j['imdb_id'] ?? '') as String,
      type: j['type'] as String? ?? '',
      name: (j['name'] ?? j['title'] ?? '') as String,
      poster: j['poster'] as String? ?? '',
      year: yearOf(),
      imdbRating: j['imdbRating'] as String? ?? '',
    );
  }

  Map<String, dynamic> toJson() =>
      {'id': id, 'type': type, 'name': name, 'poster': poster, 'year': year, 'imdbRating': imdbRating};
}

class VideoEntry {
  final String id;
  final String name;
  final int season;
  final int episode;
  VideoEntry({required this.id, required this.name, required this.season, required this.episode});
}

class MetaDetail {
  final String id;
  final String type;
  final String name;
  final String poster;
  final String background;
  final String description;
  final String releaseInfo;
  final String imdbRating;
  final List<VideoEntry> videos;

  MetaDetail({
    required this.id,
    required this.type,
    required this.name,
    this.poster = '',
    this.background = '',
    this.description = '',
    this.releaseInfo = '',
    this.imdbRating = '',
    this.videos = const [],
  });
}

class StreamEntry {
  final String name;
  final String title;
  final String url;
  final String infoHash;
  final List<String> trackers;
  final String addonName;

  StreamEntry({
    required this.name,
    required this.title,
    required this.url,
    required this.infoHash,
    this.trackers = const [],
    this.addonName = '',
  });

  StreamEntry copyWithAddon(String addon) =>
      StreamEntry(name: name, title: title, url: url, infoHash: infoHash, trackers: trackers, addonName: addon);
}

/// "Continuar Assistindo" entry -- `subtitle` is "Temporada X - Episodio Y"
/// for a series episode, or the year for a movie (see continue_watching.hpp).
class ContinueWatching {
  final String id;
  final String type;
  final String name;
  final String poster;
  final String subtitle;

  ContinueWatching({
    required this.id,
    required this.type,
    required this.name,
    this.poster = '',
    this.subtitle = '',
  });

  factory ContinueWatching.fromJson(Map<String, dynamic> j) => ContinueWatching(
        id: j['id'] as String? ?? '',
        type: j['type'] as String? ?? '',
        name: j['name'] as String? ?? '',
        poster: j['poster'] as String? ?? '',
        subtitle: j['subtitle'] as String? ?? '',
      );

  Map<String, dynamic> toJson() => {'id': id, 'type': type, 'name': name, 'poster': poster, 'subtitle': subtitle};
}

// Straight port of app/src/api/stremio_api.cpp -- same endpoints, same
// field fallbacks. Not usable from the browser for every addon: many are
// built to be called by the official Stremio app (or this project's own
// native app), not fetch() from a page on another origin, and never set
// CORS headers for that -- those requests fail here with no useful detail
// (the browser blocks reading the response before this code even sees it).
import 'dart:convert';
import 'package:http/http.dart' as http;

import '../models/models.dart';

class StremioApi {
  static const cinemetaUrl = 'https://v3-cinemeta.strem.io';

  static Future<Map<String, dynamic>> _getJson(String url) async {
    final res = await http.get(Uri.parse(url));
    if (res.statusCode < 200 || res.statusCode >= 300) {
      throw Exception('HTTP ${res.statusCode}');
    }
    return jsonDecode(res.body) as Map<String, dynamic>;
  }

  static Future<ManifestInfo> fetchManifest(String addonUrl) async {
    final j = await _getJson('$addonUrl/manifest.json');
    final rawCatalogs = j['catalogs'];
    final catalogs = <CatalogRef>[];
    if (rawCatalogs is List) {
      for (final c in rawCatalogs) {
        if (c is Map<String, dynamic>) {
          final id = c['id'] as String? ?? '';
          catalogs.add(CatalogRef(
            type: c['type'] as String? ?? '',
            id: id,
            name: c['name'] as String? ?? id,
          ));
        }
      }
    }
    return ManifestInfo(name: j['name'] as String? ?? '', catalogs: catalogs);
  }

  static Future<List<MetaSummary>> _fetchMetasFromUrl(String url) async {
    final j = await _getJson(url);
    final metas = j['metas'];
    if (metas is! List) return [];
    return metas.whereType<Map<String, dynamic>>().map(MetaSummary.fromJson).toList();
  }

  /// `extra`, when given, is one more path segment before ".json" (e.g.
  /// "genre=Animation" or "search=batman") -- the protocol's way of asking
  /// a catalog for a filtered/searched slice.
  static Future<List<MetaSummary>> fetchCatalog(
    String addonUrl,
    String type,
    String catalogId, {
    String extra = '',
  }) {
    final url = '$addonUrl/catalog/$type/$catalogId${extra.isEmpty ? '' : '/$extra'}.json';
    return _fetchMetasFromUrl(url);
  }

  static Future<List<MetaSummary>> fetchCatalogUrl(String url) => _fetchMetasFromUrl(url);

  static Future<MetaDetail> fetchMeta(String addonUrl, String type, String id) async {
    final root = await _getJson('$addonUrl/meta/$type/$id.json');
    final m = (root['meta'] as Map<String, dynamic>?) ?? root;

    String yearOf() {
      final ri = m['releaseInfo'];
      if (ri is String && ri.isNotEmpty) return ri;
      final y = m['year'];
      if (y is String) return y;
      if (y is num) return y.toInt().toString();
      return '';
    }

    final rawVideos = m['videos'];
    final videos = <VideoEntry>[];
    if (rawVideos is List) {
      for (final v in rawVideos) {
        if (v is Map<String, dynamic>) {
          final id = v['id'] as String? ?? '';
          videos.add(VideoEntry(
            id: id,
            name: v['name'] as String? ?? id,
            season: (v['season'] as num?)?.toInt() ?? 0,
            episode: (v['episode'] as num?)?.toInt() ?? 0,
          ));
        }
      }
    }

    return MetaDetail(
      id: m['id'] as String? ?? '',
      type: m['type'] as String? ?? '',
      name: m['name'] as String? ?? '',
      poster: m['poster'] as String? ?? '',
      background: m['background'] as String? ?? '',
      description: (m['description'] ?? m['overview'] ?? '') as String,
      releaseInfo: yearOf(),
      imdbRating: m['imdbRating'] as String? ?? '',
      videos: videos,
    );
  }

  /// For a series, `id` is "{showId}:{season}:{episode}" (from a
  /// MetaDetail video entry), not the show's own id.
  static Future<List<StreamEntry>> fetchStream(String addonUrl, String type, String id) async {
    final j = await _getJson('$addonUrl/stream/$type/$id.json');
    final streams = j['streams'];
    if (streams is! List) return [];

    return streams.whereType<Map<String, dynamic>>().map((s) {
      final trackers = <String>[];
      final sources = s['sources'];
      if (sources is List) {
        for (final src in sources) {
          if (src is String && src.startsWith('tracker:')) {
            trackers.add(src.substring('tracker:'.length));
          }
        }
      }
      return StreamEntry(
        name: s['name'] as String? ?? '',
        title: (s['title'] ?? s['description'] ?? '') as String,
        url: s['url'] as String? ?? '',
        infoHash: s['infoHash'] as String? ?? '',
        trackers: trackers,
      );
    }).toList();
  }
}

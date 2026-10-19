import 'dart:async';

import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../models/models.dart';
import '../services/stores.dart';
import '../services/stremio_api.dart';
import '../theme.dart';
import '../widgets/app_scaffold.dart';
import '../widgets/media_card.dart';

class SearchScreen extends StatefulWidget {
  const SearchScreen({super.key});

  @override
  State<SearchScreen> createState() => _SearchScreenState();
}

class _SearchScreenState extends State<SearchScreen> {
  static const _pageSize = 100;

  final _controller = TextEditingController();
  final List<MetaSummary> _items = [];
  bool _loading = true;
  bool _isSearchMode = false;
  int _visibleCount = _pageSize;
  int _requestToken = 0;

  @override
  void initState() {
    super.initState();
    _runCatalog();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  List<MetaSummary> get _deduped {
    final seen = <String>{};
    final out = <MetaSummary>[];
    for (final it in _items) {
      if (it.id.isEmpty || seen.contains(it.id)) continue;
      seen.add(it.id);
      out.add(it);
    }
    return out;
  }

  Future<void> _runCatalog() async {
    final token = ++_requestToken;
    setState(() {
      _isSearchMode = false;
      _items.clear();
      _loading = true;
      _visibleCount = _pageSize;
    });

    final addons = AddonStore.all().where((a) => a.enabled).toList();
    final tasks = <Future<void>>[];

    for (final addon in addons) {
      tasks.add(() async {
        try {
          final manifest = await StremioApi.fetchManifest(addon.url);
          final catalogs = manifest.catalogs.where((c) => c.type == 'movie' || c.type == 'series');
          await Future.wait(catalogs.map((c) async {
            try {
              final metas = await StremioApi.fetchCatalog(addon.url, c.type, c.id);
              if (token != _requestToken) return;
              setState(() => _items.addAll(metas));
            } catch (_) {}
          }));
        } catch (_) {}
      }());
    }

    for (final cat in CatalogStore.all().where((c) => c.enabled)) {
      tasks.add(() async {
        try {
          final metas = await StremioApi.fetchCatalogUrl(cat.url);
          if (token != _requestToken) return;
          setState(() => _items.addAll(metas));
        } catch (_) {}
      }());
    }

    await Future.wait(tasks);
    if (token == _requestToken) setState(() => _loading = false);
  }

  Future<void> _runSearch(String query) async {
    if (query.trim().isEmpty) {
      _runCatalog();
      return;
    }

    final token = ++_requestToken;
    setState(() {
      _isSearchMode = true;
      _items.clear();
      _loading = true;
      _visibleCount = _pageSize;
    });

    final urls = [StremioApi.cinemetaUrl, ...AddonStore.all().where((a) => a.enabled).map((a) => a.url)];
    final extra = 'search=${Uri.encodeComponent(query.trim())}';

    final tasks = <Future<void>>[];
    for (final url in urls) {
      for (final type in ['movie', 'series']) {
        tasks.add(() async {
          try {
            final metas = await StremioApi.fetchCatalog(url, type, 'top', extra: extra);
            if (token != _requestToken) return;
            setState(() => _items.addAll(metas));
          } catch (_) {}
        }());
      }
    }

    await Future.wait(tasks);
    if (token == _requestToken) setState(() => _loading = false);
  }

  @override
  Widget build(BuildContext context) {
    final deduped = _deduped;
    final visible = deduped.take(_visibleCount).toList();
    final remaining = deduped.length - visible.length;

    return AppScaffold(
      currentIndex: 1,
      child: SingleChildScrollView(
        child: ContentBounds(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: _controller,
                      onSubmitted: _runSearch,
                      style: const TextStyle(color: AppColors.text),
                      decoration: const InputDecoration(
                        hintText: 'Buscar filmes e series...',
                        hintStyle: TextStyle(color: AppColors.textDim),
                        prefixIcon: Icon(Icons.search, color: AppColors.textDim),
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  FilledButton(onPressed: () => _runSearch(_controller.text), child: const Text('Buscar')),
                ],
              ),
              const SizedBox(height: 20),
              if (_loading && _items.isEmpty)
                const Padding(
                  padding: EdgeInsets.only(top: 40),
                  child: Center(child: CircularProgressIndicator()),
                )
              else if (deduped.isEmpty)
                Text(
                  _isSearchMode ? 'Nenhum resultado encontrado.' : 'Nenhum catalogo disponivel -- adicione addons.',
                  style: const TextStyle(color: AppColors.textDim),
                )
              else ...[
                MediaGrid(
                  children: visible
                      .map((m) => MediaCard(
                            id: m.id,
                            type: m.type,
                            name: m.name,
                            poster: m.poster,
                            metaLine: (m.type == 'series' ? 'Serie' : 'Filme') +
                                (m.year.isNotEmpty ? ' - ${m.year}' : ''),
                            onTap: () => context.push('/detail?type=${m.type}&id=${Uri.encodeComponent(m.id)}'),
                          ))
                      .toList(),
                ),
                if (remaining > 0)
                  Padding(
                    padding: const EdgeInsets.only(top: 20),
                    child: Center(
                      child: OutlinedButton(
                        onPressed: () => setState(() => _visibleCount += _pageSize),
                        child: Text('Carregar mais ($remaining restantes)'),
                      ),
                    ),
                  ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}

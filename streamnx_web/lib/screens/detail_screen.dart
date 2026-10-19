import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../models/models.dart';
import '../services/stores.dart';
import '../services/stremio_api.dart';
import '../theme.dart';

class DetailScreen extends StatefulWidget {
  final String type;
  final String id;
  final String? fallbackTitle;
  final String? fallbackPoster;

  const DetailScreen({
    super.key,
    required this.type,
    required this.id,
    this.fallbackTitle,
    this.fallbackPoster,
  });

  @override
  State<DetailScreen> createState() => _DetailScreenState();
}

class _DetailScreenState extends State<DetailScreen> {
  MetaDetail? _detail;
  bool _loading = true;
  bool _failed = false;
  int _selectedSeason = 0;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final sources = [StremioApi.cinemetaUrl, ...AddonStore.all().where((a) => a.enabled).map((a) => a.url)];

    for (final addonUrl in sources) {
      try {
        final d = await StremioApi.fetchMeta(addonUrl, widget.type, widget.id);
        if (d.name.isNotEmpty || d.id.isNotEmpty) {
          if (!mounted) return;
          setState(() {
            _detail = d;
            _loading = false;
            _selectedSeason = d.videos.isNotEmpty ? d.videos.map((v) => v.season).reduce((a, b) => a < b ? a : b) : 0;
          });
          return;
        }
      } catch (_) {}
    }

    if (!mounted) return;
    setState(() {
      _failed = true;
      _loading = false;
    });
  }

  Map<int, List<VideoEntry>> get _seasons {
    final map = <int, List<VideoEntry>>{};
    for (final v in _detail?.videos ?? const <VideoEntry>[]) {
      map.putIfAbsent(v.season, () => []).add(v);
    }
    return map;
  }

  void _toggleFavorite() {
    final d = _detail!;
    if (FavoritesStore.contains(d.id)) {
      FavoritesStore.remove(d.id);
    } else {
      FavoritesStore.add(MetaSummary(
        id: d.id,
        type: d.type.isEmpty ? widget.type : d.type,
        name: d.name.isEmpty ? (widget.fallbackTitle ?? '') : d.name,
        poster: d.poster.isEmpty ? (widget.fallbackPoster ?? '') : d.poster,
        year: d.releaseInfo,
        imdbRating: d.imdbRating,
      ));
    }
    setState(() {});
  }

  void _openStreams(String videoId, String title, String subtitle) {
    final d = _detail!;
    context.push(
      '/streams?type=${widget.type}&videoId=${Uri.encodeComponent(videoId)}'
      '&title=${Uri.encodeComponent(title)}&subtitle=${Uri.encodeComponent(subtitle)}'
      '&showId=${Uri.encodeComponent(d.id)}&poster=${Uri.encodeComponent(d.poster)}',
    );
  }

  @override
  Widget build(BuildContext context) {
    if (_loading) {
      return Scaffold(
        appBar: AppBar(leading: const BackButton()),
        body: const Center(child: CircularProgressIndicator()),
      );
    }

    if (_failed || _detail == null) {
      return Scaffold(
        appBar: AppBar(leading: const BackButton()),
        body: const Center(
          child: Text('Nenhum addon conseguiu carregar este titulo.', style: TextStyle(color: AppColors.textDim)),
        ),
      );
    }

    final d = _detail!;
    final name = d.name.isNotEmpty ? d.name : (widget.fallbackTitle ?? '');
    final poster = d.poster.isNotEmpty ? d.poster : (widget.fallbackPoster ?? '');
    final backdrop = d.background.isNotEmpty ? d.background : poster;
    final isFav = FavoritesStore.contains(d.id);
    final seasons = _seasons;
    final seasonNumbers = seasons.keys.toList()..sort();
    final isSeries = seasonNumbers.isNotEmpty;

    return Scaffold(
      body: CustomScrollView(
        slivers: [
          SliverAppBar(
            expandedHeight: 340,
            pinned: true,
            backgroundColor: AppColors.bg,
            flexibleSpace: FlexibleSpaceBar(
              background: Stack(
                fit: StackFit.expand,
                children: [
                  if (backdrop.isNotEmpty)
                    Image.network(backdrop, fit: BoxFit.cover, errorBuilder: (context, error, stack) => const SizedBox()),
                  DecoratedBox(
                    decoration: BoxDecoration(
                      gradient: LinearGradient(
                        begin: Alignment.topCenter,
                        end: Alignment.bottomCenter,
                        colors: [AppColors.bg.withValues(alpha: 0.15), AppColors.bg],
                      ),
                    ),
                  ),
                  Padding(
                    padding: const EdgeInsets.fromLTRB(24, 24, 24, 20),
                    child: Row(
                      crossAxisAlignment: CrossAxisAlignment.end,
                      children: [
                        ClipRRect(
                          borderRadius: BorderRadius.circular(10),
                          child: SizedBox(
                            width: 130,
                            height: 195,
                            child: poster.isNotEmpty
                                ? Image.network(poster, fit: BoxFit.cover,
                                    errorBuilder: (context, error, stack) => Container(color: AppColors.bgElevated))
                                : Container(color: AppColors.bgElevated),
                          ),
                        ),
                        const SizedBox(width: 20),
                        Expanded(
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            mainAxisSize: MainAxisSize.min,
                            children: [
                              Text(name,
                                  style: const TextStyle(fontSize: 26, fontWeight: FontWeight.bold),
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis),
                              const SizedBox(height: 8),
                              Wrap(spacing: 8, children: [
                                if (d.imdbRating.isNotEmpty) _Pill('★ ${d.imdbRating}', AppColors.bgElevated),
                                if (d.releaseInfo.isNotEmpty) _Pill(d.releaseInfo, AppColors.bgElevated),
                                _Pill(isSeries ? 'Serie' : 'Filme', AppColors.bgElevated),
                              ]),
                            ],
                          ),
                        ),
                      ],
                    ),
                  ),
                ],
              ),
            ),
          ),
          SliverPadding(
            padding: const EdgeInsets.fromLTRB(24, 12, 24, 40),
            sliver: SliverToBoxAdapter(
              child: ConstrainedBox(
                constraints: const BoxConstraints(maxWidth: 1400),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(children: [
                      FilledButton.icon(
                        onPressed: _toggleFavorite,
                        icon: Icon(isFav ? Icons.star : Icons.star_border),
                        label: Text(isFav ? 'Remover dos Favoritos' : 'Adicionar aos Favoritos'),
                      ),
                      const SizedBox(width: 12),
                      if (!isSeries)
                        OutlinedButton.icon(
                          onPressed: () => _openStreams(d.id.isNotEmpty ? d.id : widget.id, name, d.releaseInfo),
                          icon: const Icon(Icons.play_arrow),
                          label: const Text('Ver Streams'),
                        ),
                    ]),
                    if (d.description.isNotEmpty) ...[
                      const SizedBox(height: 20),
                      ConstrainedBox(
                        constraints: const BoxConstraints(maxWidth: 760),
                        child: Text(d.description, style: const TextStyle(height: 1.5, color: Color(0xFFD5D6DD))),
                      ),
                    ],
                    if (isSeries) ...[
                      const SizedBox(height: 24),
                      SizedBox(
                        height: 44,
                        child: ListView.separated(
                          scrollDirection: Axis.horizontal,
                          itemCount: seasonNumbers.length,
                          separatorBuilder: (context, i) => const SizedBox(width: 8),
                          itemBuilder: (context, i) {
                            final s = seasonNumbers[i];
                            final selected = s == _selectedSeason;
                            return ChoiceChip(
                              label: Text(s == 0 ? 'Especiais' : 'Temporada $s'),
                              selected: selected,
                              onSelected: (_) => setState(() => _selectedSeason = s),
                              selectedColor: AppColors.accent,
                              backgroundColor: AppColors.bgCard,
                              labelStyle: TextStyle(color: selected ? Colors.white : AppColors.text),
                              side: BorderSide.none,
                            );
                          },
                        ),
                      ),
                      const SizedBox(height: 14),
                      ...(seasons[_selectedSeason] ?? []).map((v) => Card(
                            margin: const EdgeInsets.only(bottom: 8),
                            child: ListTile(
                              leading: SizedBox(
                                width: 36,
                                child: Text('E${v.episode}', style: const TextStyle(color: AppColors.textDim)),
                              ),
                              title: Text(v.name),
                              trailing: const Icon(Icons.play_arrow, color: AppColors.textDim),
                              onTap: () => _openStreams(
                                  v.id, name, 'Temporada ${v.season} - Episodio ${v.episode}'),
                            ),
                          )),
                      if ((seasons[_selectedSeason] ?? []).isEmpty)
                        const Text('Nenhum episodio nesta temporada.', style: TextStyle(color: AppColors.textDim)),
                    ],
                  ],
                ),
              ),
            ),
          ),
        ],
      ),
    );
  }
}

class _Pill extends StatelessWidget {
  final String text;
  final Color color;
  const _Pill(this.text, this.color);

  @override
  Widget build(BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 4),
      decoration: BoxDecoration(color: color, borderRadius: BorderRadius.circular(20)),
      child: Text(text, style: const TextStyle(fontSize: 12)),
    );
  }
}

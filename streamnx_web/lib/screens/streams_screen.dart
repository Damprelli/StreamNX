import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../models/models.dart';
import '../services/stores.dart';
import '../services/stremio_api.dart';
import '../theme.dart';

class _Badges {
  final String quality;
  final String extra;
  final String size;
  _Badges(this.quality, this.extra, this.size);
}

/// Quality/HDR/size badges pulled out of an addon's free-text name/title,
/// e.g. "1080p WEB-DL x265 HDR10 | 2.3 GB" -- same patterns as StreamRow's
/// parseBadges() in stream_list_screen.cpp.
_Badges _parseBadges(String text) {
  String match(RegExp re) => re.firstMatch(text)?.group(0) ?? '';
  return _Badges(
    match(RegExp(r'4K|2160p|1080p|720p|480p', caseSensitive: false)),
    match(RegExp(r'HDR10\+|HDR10|HDR|DoVi|DV\b', caseSensitive: false)),
    match(RegExp(r'\d+(?:[.,]\d+)?\s?(?:GB|MB)', caseSensitive: false)),
  );
}

String _hostOf(String url) {
  try {
    return Uri.parse(url).host;
  } catch (_) {
    return url;
  }
}

class StreamsScreen extends StatefulWidget {
  final String type;
  final String videoId;
  final String title;
  final String subtitle;
  final String showId;
  final String poster;

  const StreamsScreen({
    super.key,
    required this.type,
    required this.videoId,
    required this.title,
    required this.subtitle,
    required this.showId,
    required this.poster,
  });

  @override
  State<StreamsScreen> createState() => _StreamsScreenState();
}

class _StreamsScreenState extends State<StreamsScreen> {
  final List<StreamEntry> _collected = [];
  bool _loading = true;

  @override
  void initState() {
    super.initState();
    _load();
  }

  Future<void> _load() async {
    final addons = AddonStore.all().where((a) => a.enabled).toList();
    await Future.wait(addons.map((addon) async {
      try {
        final streams = await StremioApi.fetchStream(addon.url, widget.type, widget.videoId);
        if (!mounted) return;
        setState(() => _collected.addAll(streams.map((s) => s.copyWithAddon(_hostOf(addon.url)))));
      } catch (_) {}
    }));
    if (mounted) setState(() => _loading = false);
  }

  void _play(StreamEntry s) {
    ContinueWatchingStore.touch(ContinueWatching(
      id: widget.showId.isNotEmpty ? widget.showId : widget.videoId,
      type: widget.type,
      name: widget.title,
      poster: widget.poster,
      subtitle: widget.subtitle,
    ));
    final displayName = s.name.isNotEmpty ? s.name : s.title;
    context.push('/player?url=${Uri.encodeComponent(s.url)}&title=${Uri.encodeComponent(displayName)}');
  }

  @override
  Widget build(BuildContext context) {
    final playable = _collected.where((s) => s.url.isNotEmpty || s.infoHash.isNotEmpty).toList();
    final direct = playable.where((s) => s.infoHash.isEmpty).toList();
    final torrents = playable.where((s) => s.infoHash.isNotEmpty).toList();

    return Scaffold(
      appBar: AppBar(
        leading: const BackButton(),
        title: Text(widget.title, overflow: TextOverflow.ellipsis),
      ),
      body: _loading && _collected.isEmpty
          ? const Center(child: CircularProgressIndicator())
          : SingleChildScrollView(
              padding: const EdgeInsets.all(20),
              child: Center(
                child: ConstrainedBox(
                  constraints: const BoxConstraints(maxWidth: 900),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      if (widget.subtitle.isNotEmpty)
                        Padding(
                          padding: const EdgeInsets.only(bottom: 16),
                          child: Text(widget.subtitle, style: const TextStyle(color: AppColors.textDim)),
                        ),
                      if (playable.isEmpty)
                        const Text('Nenhum stream encontrado nos addons ativos.',
                            style: TextStyle(color: AppColors.textDim))
                      else ...[
                        if (direct.isNotEmpty) ...[
                          const _SectionLabel('Links diretos'),
                          ...direct.map((s) => _StreamRow(entry: s, isTorrent: false, onTap: () => _play(s))),
                        ],
                        if (torrents.isNotEmpty) ...[
                          const SizedBox(height: 12),
                          const _SectionLabel('Torrents'),
                          Card(
                            margin: const EdgeInsets.only(bottom: 8),
                            child: Padding(
                              padding: const EdgeInsets.all(16),
                              child: Text(
                                '${torrents.length} stream(s) via torrent encontrados, mas essa versao web '
                                'nao consegue reproduzir torrents (o navegador nao tem acesso a sockets de '
                                'rede crus). Use o app Desktop ou Switch para esses.',
                                style: const TextStyle(color: AppColors.textDim),
                              ),
                            ),
                          ),
                          ...torrents.map((s) => _StreamRow(entry: s, isTorrent: true, onTap: null)),
                        ],
                      ],
                    ],
                  ),
                ),
              ),
            ),
    );
  }
}

class _SectionLabel extends StatelessWidget {
  final String text;
  const _SectionLabel(this.text);

  @override
  Widget build(BuildContext context) => Padding(
        padding: const EdgeInsets.only(bottom: 8),
        child: Text(text, style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600)),
      );
}

class _StreamRow extends StatelessWidget {
  final StreamEntry entry;
  final bool isTorrent;
  final VoidCallback? onTap;

  const _StreamRow({required this.entry, required this.isTorrent, required this.onTap});

  @override
  Widget build(BuildContext context) {
    final combined = '${entry.name} ${entry.title}';
    final badges = _parseBadges(combined);
    final detailParts = [badges.size, if (entry.title != entry.name) entry.title].where((s) => s.isNotEmpty);
    final detailLine = detailParts.join('  -  ');

    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(12),
        child: Padding(
          padding: const EdgeInsets.all(14),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Row(
                children: [
                  _pill(isTorrent ? 'Torrent' : 'Direto', isTorrent ? AppColors.torrentBadge : AppColors.directBadge),
                  if (badges.quality.isNotEmpty) ...[const SizedBox(width: 6), _pill(badges.quality, AppColors.qualityBadge)],
                  if (badges.extra.isNotEmpty) ...[const SizedBox(width: 6), _pill(badges.extra, AppColors.extraBadge)],
                  const Spacer(),
                  _pill(entry.addonName, const Color(0xFF28292F), textColor: const Color(0xFFB4B9C3)),
                ],
              ),
              const SizedBox(height: 8),
              Text(entry.name.isNotEmpty ? entry.name : 'Stream',
                  maxLines: 1, overflow: TextOverflow.ellipsis, style: const TextStyle(fontSize: 14)),
              if (detailLine.isNotEmpty)
                Padding(
                  padding: const EdgeInsets.only(top: 4),
                  child: Text(detailLine,
                      maxLines: 1, overflow: TextOverflow.ellipsis,
                      style: const TextStyle(fontSize: 11, color: AppColors.textDim)),
                ),
            ],
          ),
        ),
      ),
    );
  }

  Widget _pill(String text, Color bg, {Color textColor = Colors.white}) => Container(
        padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
        decoration: BoxDecoration(color: bg, borderRadius: BorderRadius.circular(20)),
        child: Text(text, style: TextStyle(fontSize: 11, color: textColor)),
      );
}

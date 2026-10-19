import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../services/stores.dart';
import '../theme.dart';
import '../widgets/app_scaffold.dart';
import '../widgets/media_card.dart';

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  @override
  Widget build(BuildContext context) {
    final continueItems = ContinueWatchingStore.all();
    final favItems = FavoritesStore.all();

    return AppScaffold(
      currentIndex: 0,
      child: SingleChildScrollView(
        child: ContentBounds(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              if (continueItems.isNotEmpty) ...[
                const Text('Continuar Assistindo', style: TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
                const SizedBox(height: 12),
                MediaShelf(
                  children: continueItems
                      .map((c) => MediaCard(
                            id: c.id,
                            type: c.type,
                            name: c.name,
                            poster: c.poster,
                            metaLine: c.subtitle,
                            onTap: () => context.push('/detail?type=${c.type}&id=${Uri.encodeComponent(c.id)}'),
                          ))
                      .toList(),
                ),
                const SizedBox(height: 28),
              ],
              const Text('Favoritos', style: TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
              const SizedBox(height: 12),
              if (favItems.isEmpty)
                const Text('Nenhum favorito ainda.', style: TextStyle(color: AppColors.textDim))
              else
                MediaGrid(
                  children: favItems
                      .map((f) => MediaCard(
                            id: f.id,
                            type: f.type,
                            name: f.name,
                            poster: f.poster,
                            metaLine: (f.type == 'series' ? 'Serie' : 'Filme') +
                                (f.year.isNotEmpty ? ' - ${f.year}' : ''),
                            onTap: () => context.push('/detail?type=${f.type}&id=${Uri.encodeComponent(f.id)}'),
                          ))
                      .toList(),
                ),
            ],
          ),
        ),
      ),
    );
  }
}

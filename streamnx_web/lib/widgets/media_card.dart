import 'package:flutter/material.dart';

import '../services/stores.dart';
import '../theme.dart';

class MediaCard extends StatelessWidget {
  final String id;
  final String type;
  final String name;
  final String poster;
  final String metaLine;
  final VoidCallback onTap;

  const MediaCard({
    super.key,
    required this.id,
    required this.type,
    required this.name,
    required this.poster,
    required this.metaLine,
    required this.onTap,
  });

  @override
  Widget build(BuildContext context) {
    final isFav = FavoritesStore.contains(id);

    return InkWell(
      onTap: onTap,
      borderRadius: BorderRadius.circular(12),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            child: Stack(
              fit: StackFit.expand,
              children: [
                ClipRRect(
                  borderRadius: BorderRadius.circular(12),
                  child: Container(
                    color: AppColors.bgCard,
                    child: poster.isNotEmpty
                        ? Image.network(
                            poster,
                            fit: BoxFit.cover,
                            errorBuilder: (context, error, stack) => const Icon(Icons.movie_outlined, color: AppColors.textDim),
                            loadingBuilder: (context, child, progress) =>
                                progress == null ? child : const Center(child: SizedBox()),
                          )
                        : const Icon(Icons.movie_outlined, color: AppColors.textDim),
                  ),
                ),
                if (isFav)
                  Positioned(
                    top: 6,
                    right: 6,
                    child: Container(
                      width: 26,
                      height: 26,
                      decoration: BoxDecoration(
                        color: Colors.black.withValues(alpha: 0.55),
                        shape: BoxShape.circle,
                      ),
                      child: const Icon(Icons.star, size: 14, color: Colors.amber),
                    ),
                  ),
              ],
            ),
          ),
          const SizedBox(height: 6),
          Text(
            name,
            maxLines: 2,
            overflow: TextOverflow.ellipsis,
            style: const TextStyle(fontSize: 13, height: 1.25),
          ),
          if (metaLine.isNotEmpty)
            Text(metaLine, style: const TextStyle(fontSize: 11, color: AppColors.textDim)),
        ],
      ),
    );
  }
}

/// Responsive grid of MediaCards -- column count adapts to available width
/// via [gridColumnsFor].
class MediaGrid extends StatelessWidget {
  final List<Widget> children;

  const MediaGrid({super.key, required this.children});

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (context, constraints) {
        final columns = gridColumnsFor(constraints.maxWidth);
        return GridView.builder(
          shrinkWrap: true,
          physics: const NeverScrollableScrollPhysics(),
          gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
            crossAxisCount: columns,
            childAspectRatio: 0.52,
            crossAxisSpacing: 16,
            mainAxisSpacing: 20,
          ),
          itemCount: children.length,
          itemBuilder: (context, i) => children[i],
        );
      },
    );
  }
}

/// Horizontally scrolling shelf (Continuar Assistindo) -- fixed card width,
/// scrolls instead of wrapping.
class MediaShelf extends StatelessWidget {
  final List<Widget> children;

  const MediaShelf({super.key, required this.children});

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 260,
      child: ListView.separated(
        scrollDirection: Axis.horizontal,
        itemCount: children.length,
        separatorBuilder: (context, i) => const SizedBox(width: 14),
        itemBuilder: (context, i) => SizedBox(width: 150, child: children[i]),
      ),
    );
  }
}

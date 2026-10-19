import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import '../theme.dart';

/// Wraps every top-level screen (Home/Search/Addons) with responsive
/// navigation: a side rail on wide windows (desktop-like), a bottom bar on
/// narrow ones (phone-like) -- same three destinations either way.
class AppScaffold extends StatelessWidget {
  final int currentIndex;
  final Widget child;

  const AppScaffold({super.key, required this.currentIndex, required this.child});

  static const _destinations = [
    (icon: Icons.home_outlined, selectedIcon: Icons.home, label: 'Inicio', path: '/home'),
    (icon: Icons.search_outlined, selectedIcon: Icons.search, label: 'Buscar', path: '/search'),
    (icon: Icons.extension_outlined, selectedIcon: Icons.extension, label: 'Addons', path: '/addons'),
  ];

  @override
  Widget build(BuildContext context) {
    final wide = MediaQuery.sizeOf(context).width >= 900;

    void onSelect(int i) => context.go(_destinations[i].path);

    if (wide) {
      return Scaffold(
        body: Row(
          children: [
            NavigationRail(
              backgroundColor: AppColors.bgElevated,
              selectedIndex: currentIndex,
              onDestinationSelected: onSelect,
              labelType: NavigationRailLabelType.all,
              leading: const Padding(
                padding: EdgeInsets.symmetric(vertical: 20),
                child: Text('StreamNX', style: TextStyle(fontWeight: FontWeight.bold, fontSize: 16)),
              ),
              destinations: _destinations
                  .map((d) => NavigationRailDestination(
                        icon: Icon(d.icon),
                        selectedIcon: Icon(d.selectedIcon),
                        label: Text(d.label),
                      ))
                  .toList(),
            ),
            const VerticalDivider(width: 1, color: Color(0xFF1E1F28)),
            Expanded(child: child),
          ],
        ),
      );
    }

    return Scaffold(
      appBar: AppBar(title: const Text('StreamNX', style: TextStyle(fontWeight: FontWeight.bold))),
      body: child,
      bottomNavigationBar: NavigationBar(
        backgroundColor: AppColors.bgElevated,
        selectedIndex: currentIndex,
        onDestinationSelected: onSelect,
        destinations: _destinations
            .map((d) => NavigationDestination(icon: Icon(d.icon), selectedIcon: Icon(d.selectedIcon), label: d.label))
            .toList(),
      ),
    );
  }
}

/// Content max-width wrapper -- keeps text/grids from stretching edge to
/// edge into unreadable lines on an ultrawide monitor.
class ContentBounds extends StatelessWidget {
  final Widget child;
  const ContentBounds({super.key, required this.child});

  @override
  Widget build(BuildContext context) {
    return Center(
      child: ConstrainedBox(
        constraints: const BoxConstraints(maxWidth: 1400),
        child: Padding(padding: const EdgeInsets.all(24), child: child),
      ),
    );
  }
}

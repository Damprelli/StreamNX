import 'package:flutter/material.dart';
import 'package:go_router/go_router.dart';

import 'screens/addons_screen.dart';
import 'screens/detail_screen.dart';
import 'screens/home_screen.dart';
import 'screens/player_screen.dart';
import 'screens/search_screen.dart';
import 'screens/streams_screen.dart';
import 'services/stores.dart';
import 'theme.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await initStores();
  runApp(const StreamNXApp());
}

final _router = GoRouter(
  initialLocation: '/home',
  routes: [
    GoRoute(path: '/home', builder: (context, state) => const HomeScreen()),
    GoRoute(path: '/search', builder: (context, state) => const SearchScreen()),
    GoRoute(path: '/addons', builder: (context, state) => const AddonsScreen()),
    GoRoute(
      path: '/detail',
      builder: (context, state) => DetailScreen(
        type: state.uri.queryParameters['type'] ?? '',
        id: state.uri.queryParameters['id'] ?? '',
        fallbackTitle: state.uri.queryParameters['title'],
        fallbackPoster: state.uri.queryParameters['poster'],
      ),
    ),
    GoRoute(
      path: '/streams',
      builder: (context, state) => StreamsScreen(
        type: state.uri.queryParameters['type'] ?? '',
        videoId: state.uri.queryParameters['videoId'] ?? '',
        title: state.uri.queryParameters['title'] ?? '',
        subtitle: state.uri.queryParameters['subtitle'] ?? '',
        showId: state.uri.queryParameters['showId'] ?? '',
        poster: state.uri.queryParameters['poster'] ?? '',
      ),
    ),
    GoRoute(
      path: '/player',
      builder: (context, state) => PlayerScreen(
        url: state.uri.queryParameters['url'] ?? '',
        title: state.uri.queryParameters['title'] ?? '',
      ),
    ),
  ],
);

class StreamNXApp extends StatelessWidget {
  const StreamNXApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp.router(
      title: 'StreamNX Web',
      debugShowCheckedModeBanner: false,
      theme: buildAppTheme(),
      routerConfig: _router,
    );
  }
}

import 'package:flutter/material.dart';

/// Same dark palette as the native app's Borealis screens (see the
/// nvgRGB(...) calls throughout app/src/screen/*.cpp) -- kept consistent so
/// this reads as the same app, not a reskin.
class AppColors {
  static const bg = Color(0xFF0C0D12);
  static const bgElevated = Color(0xFF16171E);
  static const bgCard = Color(0xFF1C1D26);
  static const text = Color(0xFFF0F0F3);
  static const textDim = Color(0xFF9A9AA5);
  static const accent = Color(0xFF3C5AC8);
  static const good = Color(0xFF6EDC82);
  static const bad = Color(0xFFDC5A5A);
  static const torrentBadge = Color(0xFF943A7A);
  static const directBadge = Color(0xFF2D7D55);
  static const qualityBadge = Color(0xFF345AA8);
  static const extraBadge = Color(0xFF7A4AD6);
}

ThemeData buildAppTheme() {
  final base = ThemeData(
    useMaterial3: true,
    brightness: Brightness.dark,
    colorScheme: ColorScheme.fromSeed(
      seedColor: AppColors.accent,
      brightness: Brightness.dark,
      surface: AppColors.bg,
    ),
    scaffoldBackgroundColor: AppColors.bg,
    fontFamily: 'Roboto',
  );

  return base.copyWith(
    appBarTheme: const AppBarTheme(
      backgroundColor: AppColors.bg,
      surfaceTintColor: Colors.transparent,
      elevation: 0,
    ),
    cardTheme: CardThemeData(
      color: AppColors.bgCard,
      elevation: 0,
      shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(12)),
    ),
    inputDecorationTheme: InputDecorationTheme(
      filled: true,
      fillColor: AppColors.bgCard,
      border: OutlineInputBorder(borderRadius: BorderRadius.circular(10), borderSide: BorderSide.none),
      contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 14),
    ),
    filledButtonTheme: FilledButtonThemeData(
      style: FilledButton.styleFrom(
        backgroundColor: AppColors.accent,
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
      ),
    ),
    outlinedButtonTheme: OutlinedButtonThemeData(
      style: OutlinedButton.styleFrom(
        foregroundColor: AppColors.text,
        side: const BorderSide(color: Color(0xFF2A2B36)),
        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(10)),
        padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 14),
      ),
    ),
  );
}

/// Simple breakpoint helper for a responsive grid -- more columns as the
/// window widens, never so few that a card gets absurdly wide on a big
/// monitor, never so many it gets crushed on a phone.
int gridColumnsFor(double width) {
  if (width >= 1400) return 7;
  if (width >= 1100) return 6;
  if (width >= 850) return 5;
  if (width >= 600) return 4;
  if (width >= 420) return 3;
  return 2;
}

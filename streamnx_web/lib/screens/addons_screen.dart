import 'package:flutter/material.dart';

import '../services/stores.dart';
import '../services/stremio_api.dart';
import '../theme.dart';
import '../widgets/app_scaffold.dart';

String _hostOf(String url) {
  try {
    return Uri.parse(url).host;
  } catch (_) {
    return url;
  }
}

class AddonsScreen extends StatefulWidget {
  const AddonsScreen({super.key});

  @override
  State<AddonsScreen> createState() => _AddonsScreenState();
}

class _AddonsScreenState extends State<AddonsScreen> {
  final _urlController = TextEditingController();

  @override
  void dispose() {
    _urlController.dispose();
    super.dispose();
  }

  Future<void> _addAddon() async {
    final err = await AddonStore.add(_urlController.text);
    if (!mounted) return;
    if (err.isNotEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(SnackBar(content: Text(err)));
    } else {
      _urlController.clear();
      setState(() {});
    }
  }

  @override
  Widget build(BuildContext context) {
    final addons = AddonStore.all();
    final catalogs = CatalogStore.all();

    return AppScaffold(
      currentIndex: 2,
      child: SingleChildScrollView(
        child: ContentBounds(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              const Text('Addons', style: TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
              const SizedBox(height: 14),
              Row(
                children: [
                  Expanded(
                    child: TextField(
                      controller: _urlController,
                      onSubmitted: (_) => _addAddon(),
                      style: const TextStyle(color: AppColors.text),
                      decoration: const InputDecoration(
                        hintText: 'URL do manifesto (ex: https://addon.exemplo.com)',
                        hintStyle: TextStyle(color: AppColors.textDim),
                      ),
                    ),
                  ),
                  const SizedBox(width: 12),
                  FilledButton(onPressed: _addAddon, child: const Text('Adicionar')),
                ],
              ),
              const SizedBox(height: 20),
              if (addons.isEmpty)
                const Text('Nenhum addon adicionado ainda.', style: TextStyle(color: AppColors.textDim))
              else
                ...addons.asMap().entries.map((e) => _AddonRow(
                      index: e.key,
                      addon: e.value,
                      onChanged: () => setState(() {}),
                    )),
              const SizedBox(height: 28),
              const Text('Catalogos', style: TextStyle(fontSize: 20, fontWeight: FontWeight.w600)),
              const SizedBox(height: 14),
              ...catalogs.asMap().entries.map((e) => Card(
                    margin: const EdgeInsets.only(bottom: 8),
                    child: ListTile(
                      title: Text(e.value.title),
                      trailing: Switch(
                        value: e.value.enabled,
                        activeThumbColor: AppColors.accent,
                        onChanged: (_) async {
                          await CatalogStore.toggle(e.key);
                          setState(() {});
                        },
                      ),
                    ),
                  )),
            ],
          ),
        ),
      ),
    );
  }
}

class _AddonRow extends StatelessWidget {
  final int index;
  final dynamic addon; // Addon
  final VoidCallback onChanged;

  const _AddonRow({required this.index, required this.addon, required this.onChanged});

  @override
  Widget build(BuildContext context) {
    return Card(
      margin: const EdgeInsets.only(bottom: 8),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 12),
        child: Row(
          children: [
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(_hostOf(addon.url), style: const TextStyle(fontSize: 14)),
                  Text(addon.url,
                      style: const TextStyle(fontSize: 11, color: AppColors.textDim), overflow: TextOverflow.ellipsis),
                  const SizedBox(height: 4),
                  FutureBuilder(
                    future: StremioApi.fetchManifest(addon.url),
                    builder: (context, snapshot) {
                      if (snapshot.connectionState != ConnectionState.done) {
                        return const Text('Verificando conexao...',
                            style: TextStyle(fontSize: 12, color: Color(0xFFC8A05A)));
                      }
                      final online = !snapshot.hasError;
                      return Text(
                        online ? 'Online' : 'Indisponivel',
                        style: TextStyle(fontSize: 12, color: online ? AppColors.good : AppColors.bad),
                      );
                    },
                  ),
                ],
              ),
            ),
            const SizedBox(width: 8),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 3),
              decoration: BoxDecoration(
                color: addon.enabled ? AppColors.directBadge : const Color(0xFF3A3B45),
                borderRadius: BorderRadius.circular(20),
              ),
              child: Text(addon.enabled ? 'Ativo' : 'Desativado', style: const TextStyle(fontSize: 11)),
            ),
            const SizedBox(width: 8),
            IconButton(
              icon: Icon(addon.enabled ? Icons.toggle_on : Icons.toggle_off, size: 28),
              onPressed: () async {
                await AddonStore.toggle(index);
                onChanged();
              },
            ),
            IconButton(
              icon: const Icon(Icons.delete_outline),
              onPressed: () async {
                await AddonStore.remove(index);
                onChanged();
              },
            ),
          ],
        ),
      ),
    );
  }
}

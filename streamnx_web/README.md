# StreamNX Web

Versão do StreamNX para navegador, em Flutter Web. Reescreve a lógica de
dados do app nativo (`app/src/api/stremio_api.cpp`,
`app/src/model/*_store.cpp`) em Dart (`lib/services/stremio_api.dart`,
`lib/services/stores.dart`), usando `shared_preferences` (que no navegador
é só `localStorage` por baixo dos panos) no lugar dos arquivos JSON locais
do app nativo, com uma interface nova em Material 3 no lugar do Borealis
(que não tem equivalente para navegador).

## Como rodar

```bash
cd streamnx_web
flutter pub get
flutter run -d chrome          # modo dev, hot reload
# ou, para gerar os arquivos estáticos de produção:
flutter build web --release
# o resultado fica em build/web/ -- sirva com qualquer servidor estático
```

## Limitações (em relação ao app Desktop/Switch)

- **Sem suporte a torrent.** Um navegador não tem acesso a sockets TCP/UDP
  crus, então o protocolo BitTorrent é inviável aqui. Streams que vêm como
  torrent (`infoHash`, ex: Torrentio) aparecem listados na tela de streams,
  mas com um aviso — não são clicáveis. Só os que vêm como link direto
  (`url`) tocam.
- **CORS é imprevisível, e isso não é uma limitação do Flutter.** Trocar a
  tecnologia de interface não muda essa restrição — o Flutter Web compila
  para JS/Wasm rodando dentro do mesmo navegador, sujeito à mesma política
  de mesma origem que qualquer página. Muitos addons Stremio foram feitos
  para ser chamados pelo app oficial (ou por este projeto nativo), não por
  uma requisição HTTP de outra origem, e não configuram headers CORS para
  isso — esses simplesmente falham aqui, sem um erro útil (o navegador
  bloqueia a leitura da resposta antes do código Dart sequer vê-la). A
  única forma real de contornar isso é um proxy server-side que repasse a
  requisição e devolva a resposta com CORS liberado — não implementado
  aqui por ora.
- **Decodificação de vídeo depende do navegador.** O player usa o
  `video_player` do Flutter (que por baixo usa a tag `<video>` nativa do
  navegador) — funciona bem para os formatos que o navegador já sabe tocar
  (H.264/AAC em MP4, principalmente), mas não tem o mesmo alcance de
  codecs que o mpv do app nativo, nem decodificação por hardware garantida.
- **Sem sincronização entre dispositivos.** Favoritos, addons e "Continuar
  Assistindo" ficam no `localStorage` do navegador — por dispositivo/
  perfil de navegador, sem conta nem servidor.

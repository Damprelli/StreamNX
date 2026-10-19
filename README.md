# StreamNX

Um catálogo de filmes e séries com a cara do sistema do Nintendo Switch, que
fala o protocolo de addons do [Stremio](https://www.stremio.com/) para
buscar catálogos, metadados e streams — com um player de vídeo (mpv)
embutido direto na própria janela do app, sem depender de nenhum player
externo. Roda tanto em Desktop (Windows) quanto homebrew no Switch

## Como funciona

O app não guarda nenhum conteúdo próprio: ele conversa com **addons
Stremio** (o mesmo protocolo HTTP simples que o Stremio oficial usa —
`manifest.json`, `catalog`, `meta`, `stream`) para montar catálogos, buscar
títulos, carregar detalhes/temporadas e listar de onde é possível assistir
cada filme ou episódio.

- **Início**: "Continuar Assistindo" e "Favoritos", guardados localmente no
  próprio Switch/PC (sem servidor, sem conta).
- **Addons**: você cadastra addons pela URL do manifesto. O
  [Cinemeta](https://github.com/Stremio/stremio-addon-catalogs) (catálogo
  oficial do Stremio) vem sempre ativo por padrão e não aparece nessa lista
  — é uma configuração fixa, não um addon que dá pra desativar por engano.
- **Buscar**: agrega os catálogos de todo addon habilitado, mais alguns
  recortes fixos do Cinemeta (populares, lançamentos, mais bem avaliados,
  animação, documentários), com busca por texto.
- **Detalhes**: tenta buscar metadados (sinopse, elenco, temporadas/
  episódios) em cada addon habilitado, na ordem, até um responder.
- **Streams**: lista o que cada addon encontrou para aquele filme/episódio,
  com selos de qualidade/HDR extraídos do texto que o addon devolveu — links
  diretos e torrents aparecem em seções separadas, já que um torrent precisa
  de um passo de download antes de poder tocar.
- **Torrents**: addons que respondem com um `infoHash` (como o
  [Torrentio](https://torrentio.strem.fun/)) em vez de uma URL tocam do mesmo
  jeito — o app baixa o maior arquivo do torrent (o vídeo, tipicamente) para
  um cache local e só abre o player quando o download termina. O cache é
  limpo automaticamente ao sair do player, concluído ou não.
- **Player**: o [mpv](https://mpv.io/) roda embutido, renderizando direto
  no framebuffer do próprio app (OpenGL no PC, deko3d no Switch) — o vídeo
  literalmente faz parte da mesma janela/tela, não é uma janela ou processo
  separado. Uma barra de controles (OSD) some sozinha depois de alguns
  segundos e volta com **Y**; **B** fecha a barra ou sai do player; **LB/RB**
  avança e retrocede; **A** pausa/retoma; **START** abre as opções de
  áudio/legenda/velocidade. No Switch também dá pra usar a tela de toque
  (toque simples mostra/esconde a barra, duplo toque pausa, arrastar avança
  ou mexe em volume/brilho).

No Switch, a decodificação de vídeo usa o acelerador de hardware do próprio
console (Tegra X1) sempre que possível, em vez de decodificar tudo pela CPU.

## Como compilar

### Desktop (Windows)

Pré-requisitos: [MSYS2](https://www.msys2.org/) com o toolchain mingw64, e
os pacotes `mingw-w64-x86_64-{cmake,ninja,curl,mpv,libwebp}`.

Suporte a torrent (opcional, mas habilitado automaticamente se encontrado)
usa o [libtorrent-rasterbar](https://www.libtorrent.org/), que não tem
pacote pronto no MSYS2 com a config certa — precisa ser compilado a partir
do código-fonte, com o suporte a WebRTC desligado (exige `libdatachannel`,
que não está disponível no MSYS2, e não é necessário: RTC é só um transporte
alternativo de dados que o BitTorrent normal não usa):

```bash
git clone https://github.com/arvidn/libtorrent.git --branch RC_2_0
cd libtorrent && git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -Dwebrtc=OFF \
    -DCMAKE_INSTALL_PREFIX=/mingw64
cmake --build build -j8
cmake --install build
```

Com isso instalado (ou não — o app compila normalmente sem ele, só sem
suporte a torrent), o app em si compila normal:

```bash
cmake -S . -B build_pc -DPLATFORM_DESKTOP=ON
cmake --build build_pc --target StreamNX -j8
```

O executável e os recursos ficam em `build_pc/`.

### Nintendo Switch (homebrew)

Pré-requisitos: [devkitPro](https://devkitpro.org/wiki/Getting_Started)
com o pacote `switch-dev`, e os portlibs `switch-{curl,mpv,libwebp,libass,
libfribidi,freetype,dav1d,libssh2,zlib,bzip2}`.

O `switch-mpv` dos portlibs do devkitPro não tem suporte a deko3d (o driver
de vídeo que o borealis usa no Switch), e o `switch-mbedtls`/`switch-ffmpeg`
padrão não decodificam vídeo por hardware nem sustentam uma conexão HTTPS
por muito tempo nesta combinação de bibliotecas — então essas três
precisam ser recompiladas do zero antes de compilar o app. Os patches e
scripts usados estão em `scripts/switch/`:

```bash
# dentro de um shell MSYS puro do devkitPro (não é o mesmo mingw64/MINGW64
# usado no Windows normal):
env -i MSYSTEM=MSYS HOME="$HOME" /c/devkitPro/msys2/usr/bin/bash.exe --login

bash scripts/switch/mbedtls/build.sh   # TLS com entropia de hardware
bash scripts/switch/curl/build.sh      # religado contra o mbedTLS novo
bash scripts/switch/ffmpeg/build.sh    # decodificação de vídeo por hardware (nvtegra)
```

Depois disso, ainda seguindo esse mesmo shell/PATH, é preciso compilar o
`libmpv` com o backend `deko3d` habilitado (o pacote `switch-libmpv` do
devkitPro só vem com o backend OpenGL). Baixe o mpv, e compile com meson
usando `-Ddeko3d=enabled -Dgl=disabled` (veja
`app/src/api/mpv_player.cpp` para o restante das flags relevantes).

Suporte a torrent no Switch usa a engine BitTorrent própria do projeto
pipensx (vendorizada em `library/pipensx-bt`, sem
alteração de conteúdo além de reposicionamento de arquivos) em vez do
libtorrent-rasterbar do desktop — libtorrent depende de Boost (boost::asio
como transporte de rede), e não existe portlib de Boost para o Switch, nem
relato de alguém tendo conseguido cross-compilá-lo para devkitA64/libnx
([devkitPro/pacman-packages#90](https://github.com/devkitPro/pacman-packages/issues/90)).
A engine do pipensx (peer wire, piece manager, tracker announce, DHT, MSE —
tudo próprio, sem libtorrent/libtransmission) é o que aquele homebrew já usa
de verdade em produção no Switch, então não depende de nenhum port de
terceiros nem de portlib externo: compila como parte do próprio CMake do
StreamNX (`library/pipensx-bt/CMakeLists.txt`, `add_subdirectory` em
`library/CMakeLists.txt`, guardado por `PLATFORM_SWITCH`).

Só duas dependências de portlib do devkitPro além disso: `miniupnpc`
(mapeamento de porta) e `mbedcrypto` (expoenciação modular do MSE — mesmo
mbedTLS já buildado por `scripts/switch/mbedtls`, só reaproveitado aqui).
`app/src/api/magnet_resolver_switch.cpp` (adaptado, não copiado 1:1, do
`magnet_resolver.cpp` do pipensx) resolve infoHash+trackers em um `.torrent`
completo via tracker announce + DHT + BEP-9 (`ut_metadata`), e
`app/src/api/download_manager_switch.cpp` (atrás de `USE_PIPENSX_BT`) drives
o download em si, na mesma interface pública que o `download_manager.cpp`
do desktop usa para libtorrent.

Com tudo isso, o app em si compila normal:

```bash
cmake -S . -B build_switch -DPLATFORM_SWITCH=ON -DUSE_DEKO3D=ON -DCMAKE_DEPENDS_USE_COMPILER=OFF
cmake --build build_switch --target StreamNX.nro -j8
```

O `StreamNX.nro` fica em `build_switch/`.

## Créditos

A interface e o player deste projeto foram inspirados no
**[StreamFin](https://github.com/scamNscoot/StreamFin)**, um cliente
Jellyfin com a mesma cara do sistema do Switch.

StreamNX é construído em cima de bibliotecas de código aberto:

- **[borealis](https://github.com/xfangfang/borealis)** — o framework de
  interface gráfica com a cara do Switch, incluindo o driver deko3d.
- **[mpv](https://mpv.io/)** e **[FFmpeg](https://ffmpeg.org/)** — motor de
  reprodução e decodificação de vídeo/áudio.
- **[lunasvg](https://github.com/sammycage/lunasvg)** — renderização dos
  ícones em SVG da tela do player.
- **[mbedTLS](https://www.trustedfirmware.org/projects/mbed-tls/)** e
  **[libcurl](https://curl.se/)** — TLS/HTTPS para addons e streams.
- **[libwebp](https://chromium.googlesource.com/webm/libwebp)** — decodifica
  capas que vêm em formato WebP.
- **[libtorrent-rasterbar](https://www.libtorrent.org/)** — protocolo
  BitTorrent no Desktop, para streams que vêm como torrent (Torrentio e
  afins) em vez de link direto.
- **[libtransmission](https://github.com/transmission/transmission)** e o
  port **[nxTransmission](https://github.com/t-flo/nxTransmission)** —
  o mesmo suporte a torrent no Switch, via um backend diferente do
  Desktop (veja "Como compilar" acima para o porquê).
- **[devkitPro](https://devkitpro.org/)** (devkitA64/libnx) — toolchain de
  desenvolvimento homebrew para Nintendo Switch.
- O protocolo de addons e o catálogo **[Stremio](https://www.stremio.com/)**
  / **Cinemeta**, que tornam possível buscar catálogos e streams de forma
  aberta e descentralizada.

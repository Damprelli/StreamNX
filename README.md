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
  com selos de qualidade/HDR extraídos do texto que o addon devolveu.
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

Com tudo isso instalado nos portlibs do devkitPro, o app em si compila
normal:

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
- **[devkitPro](https://devkitpro.org/)** (devkitA64/libnx) — toolchain de
  desenvolvimento homebrew para Nintendo Switch.
- O protocolo de addons e o catálogo **[Stremio](https://www.stremio.com/)**
  / **Cinemeta**, que tornam possível buscar catálogos e streams de forma
  aberta e descentralizada.

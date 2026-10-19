# Engine BitTorrent própria do projeto pipensx (https://C:\Projetos\pipensx),
# vendorizada aqui verbatim (sem alteração de conteúdo -- só reposicionada,
# mesma árvore relativa src/core + src/platform + vendor/dht + vendor/libutp
# que o próprio pipensx usa, pra manter os #include "../..." internos
# funcionando sem tocar em nenhum arquivo). Switch-only: substitui
# libtransmission (ver CMakeLists.txt raiz, bloco PLATFORM_SWITCH da seção
# de torrent) -- o desktop continua em libtorrent (app/src/api/download_manager.cpp).
#
# Layout espelhado do CMakeLists.txt do pipensx (mesmas fontes, mesmas
# flags), só como STATIC libraries em vez de linkado direto no executável.

# vendor/libutp (transporte µTP/BEP-29) -- mesma postura de vendoring que o
# pipensx usa: POSIX seleciona os caminhos de BSD sockets/inet_ntop (corretos
# tanto pra Linux quanto libnx/aarch64); NDEBUG desarma os assert() internos
# (um cliente de torrent homebrew não pode abortar em um deles).
add_library(pipensx_utp STATIC
    vendor/libutp/utp_internal.cpp
    vendor/libutp/utp_utils.cpp
    vendor/libutp/utp_hash.cpp
    vendor/libutp/utp_callbacks.cpp
    vendor/libutp/utp_api.cpp
    vendor/libutp/utp_packedsockaddr.cpp
)
target_include_directories(pipensx_utp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/libutp
)
target_compile_definitions(pipensx_utp PRIVATE POSIX NDEBUG)
target_compile_options(pipensx_utp PRIVATE
    -fpermissive -fno-exceptions -fno-rtti -Wno-sign-compare
)

# Core do protocolo (peer wire, piece manager, tracker announce, DHT via
# vendor/dht, MSE, bencode/metainfo) + storage (E/S de arquivo) +
# switch_crashlog (instala handlers de sinal fatal + é o que log_emergency()
# grava em -- usado por app/src/main.cpp para diagnosticar um crash/abort em
# hardware real, já que homebrew não mostra dialog nem stack trace nenhum).
add_library(pipensx_bt STATIC
    src/core/sha1.c
    src/core/sha256.c
    src/core/mse.c
    src/core/bencode.c
    src/core/metainfo.c
    src/core/util.c
    src/core/net.c
    src/core/peer.c
    src/core/piece.c
    src/core/tracker.c
    src/core/dht.c
    src/core/peer_handoff.c
    src/core/torrent.c
    src/platform/storage.c
    src/platform/switch_crashlog.c
    vendor/dht/dht.c
)
target_include_directories(pipensx_bt PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/platform
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/dht
    # switch_crashlog.c includes "core/util.h" (pipensx's own src/ is an
    # include root there too, not just src/core) -- added rather than
    # editing that #include, to keep every vendored file byte-identical.
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# __SWITCH__ não precisa ser definida aqui -- o próprio toolchain devkitA64
# já a pré-define (é assim que o resto do app já usa #ifdef __SWITCH__ sem
# nenhum -D explícito em lugar nenhum do CMakeLists raiz). Esses arquivos
# usam essa macro internamente pra escolher os poucos shims específicos
# (RNG via libnx, SHA-1 por hardware, UPnP via miniupnpc).
# USE_MBEDTLS seleciona o backend de expoenciação modular do MSE (mse.c)
# contra o mbedtls já vendorizado/buildado neste projeto (ver
# scripts/switch/mbedtls) em vez do fallback OpenSSL.
target_compile_definitions(pipensx_bt PUBLIC USE_MBEDTLS)
target_compile_options(pipensx_bt PRIVATE -Wno-unused-parameter)
set_target_properties(pipensx_bt PROPERTIES C_STANDARD 11)
target_link_libraries(pipensx_bt PUBLIC pipensx_utp)

cmake_minimum_required(VERSION 3.10)

set(BOREALIS_LIBRARY ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis/library)

# build options
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

# Dependencies
option(USE_SHARED_LIB "Whether to use shared libs provided by system" OFF)
cmake_dependent_option(USE_SYSTEM_FMT "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TINYXML2 "" OFF "NOT USE_SHARED_LIB" ON)
cmake_dependent_option(USE_SYSTEM_TWEENY "" OFF "NOT USE_SHARED_LIB" ON)

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# project info
project(StreamNX)
set(VERSION_MAJOR "2")
set(VERSION_MINOR "0")
set(VERSION_ALTER "2")
set(VERSION_BUILD "0")
set(PACKAGE_NAME "com.streamnx.app")
set(PROJECT_AUTHOR "StreamNX")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/streamnx_icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

if (USE_LIBROMFS)
    add_libromfs(${PROJECT_NAME} ${PROJECT_RESOURCES})
endif ()

# PC build is a mouse-first desktop app with its own layout (STREAMNX_PC_UI:
# top tab bar, hover, clickable everything) and no gamepad at all
# (BRLS_MOUSE_ONLY: no gamepad, no keyboard-as-gamepad navigation, no focus
# highlight -- see those blocks in borealis' glfw_input.cpp /
# application.cpp / edit_text_dialog.cpp). Set before add_subdirectory(library)
# so borealis itself is built with it too.
#
# -DSTREAMNX_TV_UI=ON builds the PC with the Switch interface instead (icon
# rail, gamepad/keyboard navigation) -- for developing/testing the Switch
# layout without a console.
option(STREAMNX_TV_UI "PC build with the Switch (TV/gamepad) interface, for testing" OFF)
if (PLATFORM_DESKTOP AND WIN32 AND NOT STREAMNX_TV_UI)
    add_compile_definitions(BRLS_MOUSE_ONLY STREAMNX_PC_UI)
elseif (STREAMNX_TV_UI)
    add_compile_definitions(STREAMNX_TV_PREVIEW)
endif ()

add_subdirectory(library)

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

if (PLATFORM_DESKTOP)
    # Real addon fetching (manifest/catalog/meta) needs an HTTP client.
    find_package(CURL REQUIRED)
    list(APPEND APP_PLATFORM_LIB CURL::libcurl)

    # Playback: libmpv, rendered directly into borealis' own GL context
    # (api/mpv_player.cpp) so a stream plays inside this app's window
    # instead of an external process/browser tab.
    if (WIN32)
        # MSYS2's mpv.pc hardcodes its own `/mingw64/...` (MSYS-native)
        # prefix, which only resolves under an actual MSYS2 shell -- from
        # any other shell (this project's own build docs use plain
        # cmd.exe/PowerShell) pkg-config hands back a bogus, non-existent
        # include path. The real files sit right next to the compiler, so
        # just look there directly instead of going through pkg-config.
        get_filename_component(_mingw_bin "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(_mingw_root "${_mingw_bin}" DIRECTORY)
        find_path(MPV_INCLUDE_DIR mpv/client.h HINTS "${_mingw_root}/include" REQUIRED)
        find_library(MPV_LIBRARY NAMES mpv HINTS "${_mingw_root}/lib" REQUIRED)
        list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIR})
        list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARY})
    else ()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
        list(APPEND APP_PLATFORM_LIB PkgConfig::MPV)
    endif ()
elseif (PLATFORM_SWITCH)
    # Same two dependencies as desktop, via the switch-portlibs versions --
    # devkitPro's own Switch.cmake toolchain file already points
    # find_package(PkgConfig) at the right cross pkg-config wrapper.
    # switch-libmpv needs to be the deko3d-enabled build specifically
    # (`-Ddeko3d=enabled` instead of the default plain-gl one), since
    # borealis itself renders via deko3d on this platform, not GL -- see
    # api/mpv_player.cpp's BOREALIS_USE_DEKO3D path.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
    list(APPEND APP_PLATFORM_LIB PkgConfig::CURL)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    list(APPEND APP_PLATFORM_LIB PkgConfig::MPV)
elseif (PLATFORM_ANDROID)
    # No portlib/pacman equivalent for Android -- curl, mpv (+ffmpeg), webp
    # and libtorrent-rasterbar all need to be cross-compiled for the NDK by
    # hand and installed under this prefix, one subfolder per ABI (the NDK
    # toolchain file sets ANDROID_ABI itself, e.g. "arm64-v8a"). Layout
    # mirrors a normal install prefix: android-libs/<ABI>/{include,lib}.
    set(ANDROID_LIBS_PREFIX "${CMAKE_SOURCE_DIR}/android-libs/${ANDROID_ABI}")

    find_path(CURL_INCLUDE_DIR curl/curl.h HINTS "${ANDROID_LIBS_PREFIX}/include" REQUIRED)
    find_library(CURL_LIBRARY NAMES curl HINTS "${ANDROID_LIBS_PREFIX}/lib" REQUIRED)
    list(APPEND APP_PLATFORM_INCLUDE ${CURL_INCLUDE_DIR})
    list(APPEND APP_PLATFORM_LIB ${CURL_LIBRARY})

    find_path(MPV_INCLUDE_DIR mpv/client.h HINTS "${ANDROID_LIBS_PREFIX}/include" REQUIRED)
    find_library(MPV_LIBRARY NAMES mpv HINTS "${ANDROID_LIBS_PREFIX}/lib" REQUIRED)
    list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIR})
    list(APPEND APP_PLATFORM_LIB ${MPV_LIBRARY})
endif ()

# borealis' own image loading (stb_image) can't decode WebP -- some poster
# CDNs (RPDB in particular) serve it, which otherwise just leaves those
# cards on their placeholder forever. Optional: falls back to stb_image-only
# (still handles the common JPEG/PNG case) if libwebp isn't available.
#
# Not REQUIRED: the NDK's own toolchain has no pkg-config wrapper set up by
# default (unlike devkitPro's Switch.cmake), so find_package(PkgConfig)
# would otherwise fail the whole configure step on Android before this
# optional dependency even gets a chance to be treated as optional.
find_package(PkgConfig)
if (PLATFORM_ANDROID)
    find_path(WEBP_INCLUDE_DIRS webp/decode.h HINTS "${ANDROID_LIBS_PREFIX}/include")
    find_library(WEBP_LIBRARIES NAMES webp HINTS "${ANDROID_LIBS_PREFIX}/lib")
    if (WEBP_INCLUDE_DIRS AND WEBP_LIBRARIES)
        set(WEBP_FOUND TRUE)
    endif ()
elseif (PkgConfig_FOUND)
    pkg_check_modules(WEBP QUIET libwebp)
endif ()
if (WEBP_FOUND)
    message(STATUS "Found libwebp: ${WEBP_INCLUDE_DIRS}")
    list(APPEND APP_PLATFORM_OPTION -DUSE_WEBP)
    list(APPEND APP_PLATFORM_INCLUDE ${WEBP_INCLUDE_DIRS})
    list(APPEND APP_PLATFORM_LIB ${WEBP_LIBRARIES})
    link_directories(${WEBP_LIBRARY_DIRS})
else ()
    message(STATUS "libwebp not found -- WebP poster images will stay on their placeholder")
endif ()

# Torrent download support via libtorrent-rasterbar (optional on desktop, may not be in Switch portlibs)
if (PLATFORM_DESKTOP)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBTORRENT QUIET libtorrent-rasterbar)
    if (LIBTORRENT_FOUND)
        message(STATUS "Found libtorrent-rasterbar: ${LIBTORRENT_INCLUDE_DIRS}")
        list(APPEND APP_PLATFORM_OPTION -DUSE_LIBTORRENT -DUSE_TORRENT -DTORRENT_USE_OPENSSL -DTORRENT_DISABLE_RTC)
        list(APPEND APP_PLATFORM_INCLUDE ${LIBTORRENT_INCLUDE_DIRS})
        list(APPEND APP_PLATFORM_LIB ${LIBTORRENT_LIBRARIES})
        link_directories(${LIBTORRENT_LIBRARY_DIRS})
        if (WIN32)
            list(APPEND APP_PLATFORM_LIB crypt32)
        endif ()
    else ()
        message(STATUS "libtorrent-rasterbar not found -- torrent downloads disabled")
    endif ()
elseif (PLATFORM_SWITCH)
    # libtorrent needs Boost (boost::asio as its network transport), and
    # there's no Boost portlib for the Switch -- cross-compiling it from
    # scratch for devkitA64/libnx isn't documented as ever having worked (see
    # devkitPro/pacman-packages#90). pipensx_bt (library/pipensx-bt, added
    # via add_subdirectory in library/CMakeLists.txt) is the BitTorrent
    # engine the pipensx homebrew ships and validates on real Switch
    # hardware -- its own peer wire/piece manager/tracker/DHT, needing only
    # libcurl (tracker HTTP announce, already linked above for mpv/ffmpeg),
    # miniupnpc (port mapping) and mbedtls' bignum module (MSE key exchange).
    find_library(MINIUPNPC_LIBRARY miniupnpc HINTS ${DEVKITPRO}/portlibs/switch/lib)
    find_library(MBEDCRYPTO_LIBRARY mbedcrypto HINTS ${DEVKITPRO}/portlibs/switch/lib)
    if (NOT MINIUPNPC_LIBRARY)
        message(FATAL_ERROR "miniupnpc not found in ${DEVKITPRO}/portlibs/switch/lib -- required by pipensx_bt")
    endif ()
    if (NOT MBEDCRYPTO_LIBRARY)
        message(FATAL_ERROR "mbedcrypto not found in ${DEVKITPRO}/portlibs/switch/lib -- required by pipensx_bt (see scripts/switch/mbedtls)")
    endif ()

    list(APPEND APP_PLATFORM_OPTION -DUSE_PIPENSX_BT -DUSE_TORRENT)
    # Link order matters here (GNU ld resolves left to right): pipensx_bt's
    # own symbols reference all of these.
    list(APPEND APP_PLATFORM_LIB pipensx_bt ${MINIUPNPC_LIBRARY} ${MBEDCRYPTO_LIBRARY})
endif ()

# setting src and include
list(APPEND APP_PLATFORM_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/app/include)
file(GLOB_RECURSE MAIN_SRC app/src/*.cpp)

set(PLATFORM_OPTION)
if (PLATFORM_DESKTOP)
    if (WIN32)
        configure_file("${CMAKE_SOURCE_DIR}/app/platform/win32/resource.rc.in" "${CMAKE_BINARY_DIR}/resource.rc")
        list(APPEND MAIN_SRC ${CMAKE_BINARY_DIR}/resource.rc)
        if (MINGW)
            set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ -static" CACHE STRING "" FORCE)
        endif ()
    endif ()
elseif (PLATFORM_SWITCH)
    # APPEND, not set() -- this used to overwrite APP_PLATFORM_LIB outright,
    # silently dropping the curl/mpv PkgConfig targets added to it further
    # up (Threads too), which linked fine right up until the final link
    # step, where curl_*/mpv_* symbols would all come back undefined.
    list(APPEND APP_PLATFORM_LIB
        # needed by borealis
        glfw3 EGL glapi drm_nouveau
        # base lib
        nx m
    )
    list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)
elseif (PLATFORM_ANDROID)
    # Nothing StreamNX-specific needed here -- borealis' own CMakeLists.txt
    # (library/borealis/library/CMakeLists.txt's own PLATFORM_ANDROID branch)
    # already links SDL2/SDL2main and the EGL/GLESv2 libs this platform needs.
endif ()


# building target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

if (PLATFORM_DESKTOP AND WIN32)
    # MSYS2's curl package only exports a CMake target for the *shared*
    # libcurl (CURL::libcurl links libcurl-4.dll even under -static), so the
    # exe needs that DLL and its own dependency chain sitting next to it --
    # otherwise it fails to even start on a PATH that doesn't happen to
    # include mingw64/bin (i.e. everywhere but this dev shell).
    get_filename_component(MINGW_BIN_DIR ${CMAKE_C_COMPILER} DIRECTORY)
    set(CURL_RUNTIME_DLLS
        libcurl-4.dll
        libssl-3-x64.dll libcrypto-3-x64.dll
        libssh2-1.dll libnghttp2-14.dll libnghttp3-9.dll
        libngtcp2-16.dll libngtcp2_crypto_ossl-0.dll
        libbrotlidec.dll libbrotlicommon.dll
        libidn2-0.dll libpsl-5.dll libunistring-5.dll libiconv-2.dll libintl-8.dll
        libzstd.dll zlib1.dll
    )
    foreach (dll ${CURL_RUNTIME_DLLS})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MINGW_BIN_DIR}/${dll}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endforeach ()

    # libmpv's own dependency tree is far too large to hand-maintain like
    # curl's (ffmpeg's codecs, libplacebo, vulkan, vapoursynth...) -- walk
    # and copy the whole closure instead, see cmake/copy_mingw_dlls.cmake.
    find_program(OBJDUMP_EXECUTABLE objdump HINTS ${MINGW_BIN_DIR} REQUIRED)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DTARGET_DLL=libmpv-2.dll
            -DMINGW_BIN_DIR=${MINGW_BIN_DIR}
            -DOUT_DIR=$<TARGET_FILE_DIR:${PROJECT_NAME}>
            -DOBJDUMP=${OBJDUMP_EXECUTABLE}
            -P "${CMAKE_SOURCE_DIR}/cmake/copy_mingw_dlls.cmake"
    )

    if (WEBP_FOUND)
        # Our own direct -lwebp link, not part of libmpv's dependency closure
        # walked above.
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${MINGW_BIN_DIR}/libwebp-7.dll" "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        )
    endif ()
endif ()


# building release file
if (PLATFORM_DESKTOP)
    add_custom_target(${PROJECT_NAME}.data
            COMMAND "${CMAKE_COMMAND}" -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_CURRENT_BINARY_DIR}/resources
    )
    if (NOT USE_LIBROMFS)
        add_dependencies(${PROJECT_NAME} ${PROJECT_NAME}.data)
    endif ()
elseif (PLATFORM_SWITCH)
    if (USE_DEKO3D)
        gen_dksh("${PROJECT_RESOURCES}/shaders")
    endif ()
    add_custom_target(${PROJECT_NAME}.nro DEPENDS ${PROJECT_NAME}
        COMMAND ${NX_NACPTOOL_EXE} --create "${PROJECT_NAME}" "${PROJECT_AUTHOR}" "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}" ${PROJECT_NAME}.nacp --titleid=${PROJECT_TITLEID}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
        COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/resources/font
        COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${PROJECT_ICON} --nacp=${PROJECT_NAME}.nacp --romfsdir=${CMAKE_BINARY_DIR}/resources
    )
endif ()

target_include_directories(${PROJECT_NAME} PRIVATE app ${APP_PLATFORM_INCLUDE})
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections ${APP_PLATFORM_OPTION})
# What the updater sends to /api/update -- has to match the release tag
# (v2.0.0 / v2.0.0_PC) for "is there something newer" to be right.
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_VERSION="${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_ALTER}")
if (PLATFORM_SWITCH)
    # borealis pulls in deko3d transitively; CMake's link-line generator
    # places a target's transitive deps immediately after the target itself.
    # libmpv.a (in APP_PLATFORM_LIB) also needs deko3d symbols, so list
    # APP_PLATFORM_LIB before borealis: that way borealis's (and hence
    # deko3d's) position lands after mpv, where mpv's undefined refs need it.
    target_link_libraries(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_LIB} borealis lunasvg)
else ()
    target_link_libraries(${PROJECT_NAME} PRIVATE borealis lunasvg ${APP_PLATFORM_LIB})
endif ()

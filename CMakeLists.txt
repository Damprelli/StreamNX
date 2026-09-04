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
set(VERSION_MAJOR "1")
set(VERSION_MINOR "0")
set(VERSION_ALTER "0")
set(VERSION_BUILD "0")
set(PACKAGE_NAME "com.streamnx.app")
set(PROJECT_AUTHOR "StreamNX")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/streamnx_icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)

if (USE_LIBROMFS)
    add_libromfs(${PROJECT_NAME} ${PROJECT_RESOURCES})
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
endif ()

# borealis' own image loading (stb_image) can't decode WebP -- some poster
# CDNs (RPDB in particular) serve it, which otherwise just leaves those
# cards on their placeholder forever. Optional: falls back to stb_image-only
# (still handles the common JPEG/PNG case) if libwebp isn't available.
find_package(PkgConfig REQUIRED)
pkg_check_modules(WEBP QUIET libwebp)
if (WEBP_FOUND)
    message(STATUS "Found libwebp: ${WEBP_INCLUDE_DIRS}")
    list(APPEND APP_PLATFORM_OPTION -DUSE_WEBP)
    list(APPEND APP_PLATFORM_INCLUDE ${WEBP_INCLUDE_DIRS})
    list(APPEND APP_PLATFORM_LIB ${WEBP_LIBRARIES})
    link_directories(${WEBP_LIBRARY_DIRS})
else ()
    message(STATUS "libwebp not found -- WebP poster images will stay on their placeholder")
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

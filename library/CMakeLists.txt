# add lunasvg
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
add_subdirectory(lunasvg EXCLUDE_FROM_ALL)

add_subdirectory(borealis/library)

# Engine BitTorrent do pipensx -- só faz sentido no Switch (desktop já usa
# libtorrent, ver CMakeLists.txt raiz).
if (PLATFORM_SWITCH)
    add_subdirectory(pipensx-bt EXCLUDE_FROM_ALL)
endif ()
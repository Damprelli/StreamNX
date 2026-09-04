# Recursively resolves TARGET_DLL's DLL dependency closure against
# MINGW_BIN_DIR and copies every DLL in it into OUT_DIR.
#
# libcurl's own dependency chain (17 DLLs) was small enough to enumerate by
# hand; libmpv links dozens of optional codec/renderer libraries (ffmpeg's
# own codec set, libplacebo, vulkan, vapoursynth...) that changes whenever
# the mpv package itself is rebuilt -- hand-maintaining that list would
# silently rot. This walks objdump's import table instead, so it always
# matches whatever the installed mpv package actually needs.
#
# Usage: cmake -DTARGET_DLL=<name> -DMINGW_BIN_DIR=<dir> -DOUT_DIR=<dir>
#              -DOBJDUMP=<path-to-objdump> -P copy_mingw_dlls.cmake

set(_visited "")
set(_queue "${TARGET_DLL}")

while (_queue)
    list(POP_FRONT _queue _dll)
    get_filename_component(_name "${_dll}" NAME)

    list(FIND _visited "${_name}" _idx)
    if (_idx GREATER -1)
        continue()
    endif ()
    list(APPEND _visited "${_name}")

    if (NOT EXISTS "${MINGW_BIN_DIR}/${_name}")
        continue() # a Windows system DLL, or something else not ours to ship
    endif ()

    file(COPY "${MINGW_BIN_DIR}/${_name}" DESTINATION "${OUT_DIR}")

    execute_process(
        COMMAND "${OBJDUMP}" -p "${MINGW_BIN_DIR}/${_name}"
        OUTPUT_VARIABLE _dump
        ERROR_QUIET
    )
    string(REGEX MATCHALL "DLL Name: [^\r\n]+" _lines "${_dump}")
    foreach (_line ${_lines})
        string(REPLACE "DLL Name: " "" _dep "${_line}")
        string(STRIP "${_dep}" _dep)
        list(APPEND _queue "${_dep}")
    endforeach ()
endwhile ()

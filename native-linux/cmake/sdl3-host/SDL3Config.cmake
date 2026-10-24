# Use the host's SDL shared library, so the user's display/audio/input drivers
# stay native. Installed development headers or a local SDL checkout work.
get_filename_component(TOPSPIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
if(NOT TARGET SDL3::SDL3)
    find_library(TOPSPIN_SDL3 NAMES SDL3 libSDL3.so.0 REQUIRED)
    find_path(TOPSPIN_SDL3_INCLUDE NAMES SDL3/SDL.h
        HINTS "${TOPSPIN_ROOT}/deps/SDL/include" REQUIRED)
    add_library(SDL3::SDL3 SHARED IMPORTED)
    set_target_properties(SDL3::SDL3 PROPERTIES IMPORTED_LOCATION "${TOPSPIN_SDL3}"
        INTERFACE_INCLUDE_DIRECTORIES "${TOPSPIN_SDL3_INCLUDE}")
endif()
set(SDL3_FOUND TRUE)

cmake_minimum_required(VERSION 3.28)
project(MyEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- library flavour ----
# Both flavours are first-class and both are tested:
#   OFF (default) - static: one library, simplest to embed;
#   ON            - shared: plugins link the same DLL, so the process-wide state
#                   (core_application::instance(), type ids) is shared instead of duplicated.
# SDL follows the same choice, so a shared build never embeds a private copy of SDL.
option(METAOBJECT_SHARED "Build metaobject and SDL as shared libraries" ON)

# ---- SDL3: vendored git submodule (hard dependency) ----
# First checkout: git submodule update --init --recursive
if(METAOBJECT_SHARED)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)
else()
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
endif()
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL_DOCS ON CACHE BOOL "" FORCE)
# Executables and any DLLs land in one directory, so the loader finds them without PATH tweaks.
# Must be set before add_subdirectory(SDL3) so that SDL's DLL follows too.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
add_subdirectory(external/SDL3 EXCLUDE_FROM_ALL)

if(METAOBJECT_SHARED)
    add_library(metaobject SHARED)
    target_link_libraries(metaobject PUBLIC SDL3::SDL3)
    # Only HWSHQTB_EXPORT-annotated entities leave the DLL; the marker is the public API contract,
    # so the annotated set is exactly the surface a consumer may name.
    target_compile_options(metaobject PRIVATE -fvisibility=hidden)
    target_compile_definitions(metaobject PRIVATE HWSHQTB_BUILDING)
    if(MINGW)
        # Without this the MinGW linker exports every symbol regardless of -fvisibility.
        target_link_options(metaobject PRIVATE -Wl,--exclude-all-symbols)
    endif()
else()
    add_library(metaobject STATIC)
    target_link_libraries(metaobject PUBLIC SDL3::SDL3-static)
endif()
target_include_directories(metaobject PUBLIC "${PROJECT_SOURCE_DIR}/include")


# metaobject owns the core layer: every source under src/metaobject/. The other layers (src/gui/,
# src/ui/, src/audio/) have their own targets, their own export macros and their own globs below, so a
# file belongs to exactly one library by where it sits. Globbing the layer directory directly — the same
# shape gui, ui and audio use — makes that structural: adding a layer needs no change here, and there is
# no exclusion list to keep in step with the directories.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/metaobject/*.cpp)

target_sources(metaobject
    PRIVATE
        ${SOURCES}
)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
	target_compile_options(metaobject PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-freflection>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(metaobject PUBLIC $<$<COMPILE_LANGUAGE:CXX>:-freflection>)
endif()

# ---- gui: the QtGui-equivalent layer (stage H2/H5) ----
# The same two flavours as metaobject, driven by the same METAOBJECT_SHARED switch: a shared gui
# links the shared metaobject and the shared SDL, so a process has exactly one copy of each.
# SDL3::SDL3 is the alias of whichever SDL flavour the switch configured, so it is correct in both.
#
# Public headers use HWSHQTB_GUI_EXPORT on classes only (see include/hwshqtb/gui/export.h); the
# constexpr value types are header-only and are never exported. No SDL header is included here: gui
# needs SDL only for its own translation units.
file(GLOB_RECURSE GUI_SOURCES CONFIGURE_DEPENDS src/gui/*.cpp)

if(METAOBJECT_SHARED)
	add_library(gui SHARED)
	target_compile_options(gui PRIVATE -fvisibility=hidden)
	target_compile_definitions(gui PRIVATE HWSHQTB_GUI_BUILDING)
	if(MINGW)
		# Without this the MinGW linker exports every symbol regardless of -fvisibility.
		target_link_options(gui PRIVATE -Wl,--exclude-all-symbols)
	endif()
else()
	add_library(gui STATIC)
endif()
target_link_libraries(gui PUBLIC metaobject SDL3::SDL3)
target_include_directories(gui PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_sources(gui
    PRIVATE
        ${GUI_SOURCES}
)

# ---- ui: the visual-control layer (stages W1/W2) ----
# Every visual element is an item_t: geometry, size hints, painting, hit testing, interaction and
# keyboard focus. widget_t is an item with item children, properties, signals and a layout (layout_t,
# box/grid arrange them); im_host_t is a leaf item whose content the frame handler enumerates by code.
# widget_window_t is the retained host that paints the tree and routes the window's input into it.
# The target follows the same two flavours as metaobject/gui and links gui (which links metaobject and
# SDL3), so a shared build has exactly one copy of each layer.
#
# Public headers use HWSHQTB_UI_EXPORT on classes only (include/hwshqtb/ui/export.h); the
# value types it uses live in gui and stay constexpr/header-only.
file(GLOB_RECURSE UI_SOURCES CONFIGURE_DEPENDS src/ui/*.cpp)

if(METAOBJECT_SHARED)
	add_library(ui SHARED)
	target_compile_options(ui PRIVATE -fvisibility=hidden)
	target_compile_definitions(ui PRIVATE HWSHQTB_UI_BUILDING)
	if(MINGW)
		# Without this the MinGW linker exports every symbol regardless of -fvisibility.
		target_link_options(ui PRIVATE -Wl,--exclude-all-symbols)
	endif()
else()
	add_library(ui STATIC)
endif()
target_link_libraries(ui PUBLIC gui)
target_include_directories(ui PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_sources(ui
    PRIVATE
        ${UI_SOURCES}
)

# ---- audio: playback and sound-file decoding (a sibling of gui, not part of it) --------------------
#
# Layering: audio depends on metaobject and SDL3 only. It does NOT depend on gui or ui — the audio
# layer knows nothing about pixels, windows or widgets, exactly as QtMultimedia is separate from
# QtWidgets. A demo that wants a button next to a sound links all of them; the layers themselves stay
# independent, so an audio-only program never pulls in the widget tree.
#
# The MP3 decoder is the vendored minimp3 submodule (external/minimp3): SDL3 loads WAV only, so MP3
# needs its own backend. Both sit behind audio_decoder_t, so neither is baked into the interface.
file(GLOB_RECURSE AUDIO_SOURCES CONFIGURE_DEPENDS src/audio/*.cpp)

if(METAOBJECT_SHARED)
	add_library(audio SHARED)
	target_compile_options(audio PRIVATE -fvisibility=hidden)
	target_compile_definitions(audio PRIVATE HWSHQTB_AUDIO_BUILDING)
	if(MINGW)
		# Without this the MinGW linker exports every symbol regardless of -fvisibility.
		target_link_options(audio PRIVATE -Wl,--exclude-all-symbols)
	endif()
else()
	add_library(audio STATIC)
endif()
target_link_libraries(audio PUBLIC metaobject SDL3::SDL3)
# minimp3 is a header-only library: MINIMP3_IMPLEMENTATION is compiled inside the audio translation unit
# that includes it, so the only thing the build needs is the include path.
target_include_directories(audio PUBLIC "${PROJECT_SOURCE_DIR}/include"
                                        PRIVATE "${PROJECT_SOURCE_DIR}/external/minimp3")
target_sources(audio
    PRIVATE
        ${AUDIO_SOURCES}
)

add_subdirectory(examples)

enable_testing()
add_subdirectory(tests)

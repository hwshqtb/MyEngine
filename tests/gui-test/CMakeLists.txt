# H1: geometry.h / color_t.h are header-only constexpr value types, so this test needs no library of
# its own (metaobject is linked only to inherit its PUBLIC include/ directory). The gui target now
# exists (H2), but a pure value-type test stays deliberately independent of it.
add_executable(gui_geometry_test gui_geometry_test.cpp)
target_link_libraries(gui_geometry_test PRIVATE metaobject)
add_test(NAME gui_geometry_test COMMAND gui_geometry_test)

# window_to_pixel_point(): the pointer conversion from WINDOW coordinates to the render/pixel space.
# Pure constexpr integer arithmetic, so the densities (1, 1.5, 2 and a shrinking 2/3) and the edges
# (degenerate extents, negative points, int saturation) are pinned without a window, without a
# display and without a display of the density under test. Like gui_geometry_test it needs no gui
# symbols: the function is header-only, and metaobject is linked only for the include/ root.
add_executable(gui_window_coordinate_test window_coordinate_test.cpp)
target_link_libraries(gui_window_coordinate_test PRIVATE metaobject)
add_test(NAME gui_window_coordinate_test COMMAND gui_window_coordinate_test)

# H2: the paint-engine seam plus image_t / painter_t. The test links gui (which links metaobject and
# SDL3), so its pixel assertions cover the same rendering path the window will use — offscreen, with
# no window and no display.
add_executable(gui_paint_test gui_paint_test.cpp)
target_link_libraries(gui_paint_test PRIVATE gui)
add_test(NAME gui_paint_test COMMAND gui_paint_test)

# H3: the bitmap font, painter_t::draw_text and the glyph-run seam. Offscreen pixel assertions plus a
# recording paint engine (a backend that rasterises nothing), so the test needs no window either.
add_executable(gui_text_test gui_text_test.cpp)
target_link_libraries(gui_text_test PRIVATE gui)
add_test(NAME gui_text_test COMMAND gui_text_test)

# region_t: the disjoint-rectangle set algebra the repaint machinery rests on — normalization,
# ordering, subtraction, and the degrade-to-bounds fallback. Pure value algebra, no window, no SDL;
# gui is linked for region_t's out-of-line members.
add_executable(region_test region_test.cpp)
target_link_libraries(region_test PRIVATE gui)
add_test(NAME region_test COMMAND region_test)

# platform_window_t plus window_t's frame policy with NO window system at all: a fake platform
# implementation and a recording paint engine drive show / repaint / set_pending_damage / scroll /
# capture, so the pass decision (per rectangle or one region), the empty-damage frame, the presented
# region and the scroll accumulation are pinned without an SDL window, a display or a driver.
add_executable(gui_platform_window_test platform_window_test.cpp)
target_link_libraries(gui_platform_window_test PRIVATE gui)
add_test(NAME gui_platform_window_test COMMAND gui_platform_window_test)

# offscreen_window_t: the SECOND platform_window_t backend. It has no native window at all — its
# "window target" is an ARGB8888 software surface — so this test drives the whole frame policy
# (passes, presentation, scroll, capture, input) with no core_application, no gui_application_t and
# no video subsystem. It is the evidence that the platform interface assumes no real window.
add_executable(gui_offscreen_window_test offscreen_window_test.cpp)
target_link_libraries(gui_offscreen_window_test PRIVATE gui)
add_test(NAME gui_offscreen_window_test COMMAND gui_offscreen_window_test)

# The font stack: description → database → engine → glyph indices. It pins the built-in face and the
# UTF-8 regression (one CODE POINT is one glyph and one advance, however many bytes it takes), with no
# window, no video subsystem and no display: the numbers are exact on every machine.
add_executable(gui_font_engine_test gui_font_engine_test.cpp)
target_link_libraries(gui_font_engine_test PRIVATE gui)
add_test(NAME gui_font_engine_test COMMAND gui_font_engine_test)

# The GDI font engine, constructed DIRECTLY (not through the database): semantic assertions only, since
# the system face is machine dependent. It includes the internal header, so src/gui is on its path, and
# it compiles the engine's own translation unit: the engine is internal (not exported from the gui DLL),
# which is exactly why a test that wants to construct it directly must build its source.
add_executable(gui_win32_font_engine_test win32_font_engine_test.cpp)
target_link_libraries(gui_win32_font_engine_test PRIVATE gui)
target_include_directories(gui_win32_font_engine_test PRIVATE "${PROJECT_SOURCE_DIR}/src/gui")
target_sources(gui_win32_font_engine_test PRIVATE "${PROJECT_SOURCE_DIR}/src/gui/win32_font_engine.cpp")
add_test(NAME gui_win32_font_engine_test COMMAND gui_win32_font_engine_test)


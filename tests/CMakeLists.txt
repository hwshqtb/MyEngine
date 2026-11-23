# One folder per test, mirroring examples/. Every milestone must ship with a test that covers its
# acceptance criteria.
add_subdirectory(implementation-test)

# Ported Qt 6.8.3 tests: one CTest per Qt test directory.
add_subdirectory(qt-port)

# H1/H2: the GUI layer — value types (geometry / colour) and the paint seam (image / painter).
add_subdirectory(gui-test)

# W1/W2: the ui layer — widget_t / widget_window_t, the painter prerequisites (translate, clip,
# save/restore) and the layouts. Offscreen only, like gui-test.
add_subdirectory(ui-test)

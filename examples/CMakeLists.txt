add_subdirectory(demo)
add_subdirectory(hello_gui)

# W1/W2: the retained host demo — widget_window_t + nested coloured panels arranged by layouts, with
# --screenshot.
add_subdirectory(ui_demo)

# A5: the audio demo — a window with two buttons driving two push-mode output streams (music loops, a
# one-shot click overlaps it). It links ui AND audio, the only place that needs both.
add_subdirectory(audio_demo)

# M16 Phase D: a plugin only makes sense in the shared-library configuration. A static metaobject is
# one private copy per module, so a "static plugin" could never share core_application::instance()
# or the type ids with its host. The static build stays exactly as it was.
if(METAOBJECT_SHARED)
    add_subdirectory(plugin)
endif()

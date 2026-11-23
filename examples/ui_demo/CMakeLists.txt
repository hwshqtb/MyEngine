# W1/W2/W3: the retained host demo — a widget_window_t owning a small widget tree (nested coloured
# panels with text labels, arranged by hbox/vbox layouts) whose INPUT ROUTER makes the left half
# interactive (clickable panels, a Tab focus ring), beside the immediate host of I1. Includes the
# scripted --duration, headless --screenshot and event-loop-driven --self-test modes. It links ui
# (which links gui, metaobject and SDL3), so the demo drives exactly the interact/render path the ui
# tests assert on.
add_executable(ui_demo main.cpp)
target_link_libraries(ui_demo PRIVATE ui)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(ui_demo PRIVATE -Wl,--allow-multiple-definition)
endif()

# H4: the vertical slice demo — a persistent window showing "hello world", plus the scripted
# --duration and headless --screenshot modes. It links gui (which links metaobject and SDL3), exactly
# like the gui tests, so the demo paints through the same painter/engine path the tests assert on.
add_executable(hello_gui main.cpp)
target_link_libraries(hello_gui PRIVATE gui)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(hello_gui PRIVATE -Wl,--allow-multiple-definition)
endif()

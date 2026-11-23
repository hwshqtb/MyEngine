# M16 Phase D: a real demo plugin. Only the shared configuration makes sense here: a plugin links
# the one libmetaobject.dll, so host and plugin share core_application::instance() and the type ids
# at runtime. examples/CMakeLists.txt therefore adds this directory only when METAOBJECT_SHARED is ON.
add_library(demo_plugin SHARED demo_plugin.cpp demo_interface.h)
target_link_libraries(demo_plugin PRIVATE metaobject)

# The interface header is the host/plugin contract: the end-to-end test includes it too, so both
# sides compile the exact same vtable layout.
target_include_directories(demo_plugin PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The two entry points are exported with __declspec(dllexport) in demo_plugin.cpp. The metaobject
# target's -fvisibility=hidden / -Wl,--exclude-all-symbols options are PRIVATE, so the plugin does not
# inherit them and the loader can resolve the names.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(demo_plugin PRIVATE -Wl,--allow-multiple-definition)
endif()

# M16 Phase D: end-to-end test against the real demo_plugin shared library.
#
# The DLL path is compiled in from the build system instead of being guessed at run time:
# $<TARGET_FILE:demo_plugin> expands to the exact output file (the flat CMAKE_BINARY_DIR, spaces in
# the build-directory name included). add_dependencies makes sure the plugin exists before the test
# binary links, independent of the generator-expression dependency rules.
add_executable(m16_plugin_test m16_plugin_test.cpp)
target_link_libraries(m16_plugin_test PRIVATE metaobject)
target_include_directories(m16_plugin_test PRIVATE "${PROJECT_SOURCE_DIR}/examples/plugin")
target_compile_definitions(m16_plugin_test PRIVATE
    M16_PLUGIN_PATH="$<TARGET_FILE:demo_plugin>")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m16_plugin_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_dependencies(m16_plugin_test demo_plugin)
add_test(NAME m16_plugin_test COMMAND m16_plugin_test)

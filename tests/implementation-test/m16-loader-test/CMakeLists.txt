# M16 Phase B/C: plugin_loader_t + factory_loader_t. The test source path is passed as a string literal
# so the "exists but is not a shared library" case can name this very file without guessing the
# working directory.
add_executable(m16_loader_test m16_loader_test.cpp)
target_link_libraries(m16_loader_test PRIVATE metaobject)
target_compile_definitions(m16_loader_test PRIVATE
    M16_LOADER_TEST_SOURCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/m16_loader_test.cpp")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m16_loader_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m16_loader_test COMMAND m16_loader_test)

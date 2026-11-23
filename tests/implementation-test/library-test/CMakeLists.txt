# M16: library_t, the generic dynamic-library wrapper. The test source path is passed as a string
# literal so the "exists but is not a shared library" case can name this very file without guessing
# the working directory.
add_executable(library_test library_test.cpp)
target_link_libraries(library_test PRIVATE metaobject)
target_compile_definitions(library_test PRIVATE
    LIBRARY_TEST_SOURCE_FILE="${CMAKE_CURRENT_SOURCE_DIR}/library_test.cpp")
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(library_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME library_test COMMAND library_test)

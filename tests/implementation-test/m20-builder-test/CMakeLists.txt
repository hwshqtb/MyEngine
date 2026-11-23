# P1: the run-time builder family — meta_object_builder_t and its three member handles.
#
# The object builder owns the arrays; add_*() returns a {owner*, index} handle that can be mutated
# until build() takes the one-shot snapshot. Covered: mutating through a handle, the per-parameter
# cvref column (check_args parity with a reflected class), index_of_* / remove_* shifting, snapshot
# semantics and a property with a notifier.
add_executable(m20_builder_test m20_builder_test.cpp)
target_link_libraries(m20_builder_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m20_builder_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m20_builder_test COMMAND m20_builder_test)

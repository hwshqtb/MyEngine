# Stage S: object_t slimming — layout (32 bytes) and lazy allocation of detail::object_data.
#
# Reports the two sizeof numbers and asserts the behaviours described below:
# a bare object allocates nothing, each state-using accessor allocates exactly one block,
# object_pointer still tracks a bare object, and a member-less derived class adds no bytes.
add_executable(object_size_test object_size_test.cpp)
target_link_libraries(object_size_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(object_size_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME object_size_test COMMAND object_size_test)

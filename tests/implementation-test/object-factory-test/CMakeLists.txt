# I0: the generic object factory on object_t (register_type / create).
#
# The registry itself (primary keys / short-name aliases / first-wins duplicates / type_names), the
# reflected-constructor path (0-argument and object_t*-argument constructors), explicit creators,
# create(..., parent) parenting and the registration-time rejection of an abstract type.
add_executable(object_factory_test object_factory_test.cpp)
target_link_libraries(object_factory_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(object_factory_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME object_factory_test COMMAND object_factory_test)

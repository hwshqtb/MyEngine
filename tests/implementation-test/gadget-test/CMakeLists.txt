# GADGET: the value-type meta-object (Q_GADGET equivalent) — static table, no vtable, no accessor.
add_executable(gadget_test gadget_test.cpp)
target_link_libraries(gadget_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(gadget_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME gadget_test COMMAND gadget_test)

# M18: constructor reflection (QMetaObject::constructor / newInstance equivalent).
add_executable(constructor_test constructor_test.cpp)
target_link_libraries(constructor_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(constructor_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME constructor_test COMMAND constructor_test)

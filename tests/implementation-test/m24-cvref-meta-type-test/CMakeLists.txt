# M24: cvref_meta_type_t — one argument's identity (a bare type handle plus the value's cvref) and
# the single string it spells.
#
# Nothing consumes the type yet, so this target pins the identity rule itself, which the parameter
# column and the overload-resolution layer will match on: cv placement (prefix for a non-pointer,
# trailing for a pointer), the reference suffix, the identity spelling taken from the namer rather
# than a literal, agreement between from<T>() and from(any_t), and structural equality.
add_executable(m24_cvref_meta_type_test m24_cvref_meta_type_test.cpp)
target_link_libraries(m24_cvref_meta_type_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m24_cvref_meta_type_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m24_cvref_meta_type_test COMMAND m24_cvref_meta_type_test)

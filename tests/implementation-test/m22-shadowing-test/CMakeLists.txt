# M22: own-first lookup vs a derived class that shadows a same-signature base member.
#
# The library now resolves a name against the most-derived table before walking super_class()
# (meta_object_t::global_index / enumeration_in). Covered here: method("f()") selects the DERIVED
# row (the make_tag payloads tell the two declarations apart), index_of_method()/method(index)
# round-trip, Direct and Queued invocation both reach the derived body, from_method(&derived::f)
# agrees with the by-name query, a signal declared in both classes fires when emitted on the derived
# object, and enumeration("Mode") reports the derived enum.
add_executable(m22_shadowing_test m22_shadowing_test.cpp)
target_link_libraries(m22_shadowing_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m22_shadowing_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m22_shadowing_test COMMAND m22_shadowing_test)

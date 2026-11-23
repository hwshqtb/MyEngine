# M27: the `call` family — resolve a member by NAME, then invoke it.
#
# `invoke` is signature-based; `call` is name-based. This target pins the name-based half and the
# DELEGATION rule behind it: one resolve_call() and then the handle's invoke(), so the
# two layers cannot disagree and every failure is diagnosed exactly once by the resolver.
#
# The fixture forces every branch: one value row, one void row, a three-way overload set
# (int / double / const std::string&) with distinguishable results, a signal with a connected
# receiver, lvalue arguments that only the accommodating pass accepts, and a class whose h()/h() const
# pair is ambiguous on a non-const object.
add_executable(m27_call_by_name_test m27_call_by_name_test.cpp)
target_link_libraries(m27_call_by_name_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m27_call_by_name_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m27_call_by_name_test COMMAND m27_call_by_name_test)

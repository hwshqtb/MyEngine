# M25: overload resolution against ONE table — row_matches() (the slot-addressed runtime matcher:
# arity, bare spelling, the exact / accommodating reference-kind rule, the member's own cvref against
# the object), check_args<Ts...>() (its compile-time twin) and meta_info_t::resolve_call() /
# resolve_call_static<Ts...>() (the two-pass scan and its verdict: slot, ambiguity, or index_npos).
# Section (11) compares the two worlds at run time; the design deliberately keeps the runtime half out
# of constant expressions.
#
# The fixture forces every branch: an int / double pair, an arity-only overload, and a const T& / T&&
# pair whose bare spelling is IDENTICAL, so only the reference kind separates them.
add_executable(m25_call_resolution_test m25_call_resolution_test.cpp)
target_link_libraries(m25_call_resolution_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m25_call_resolution_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m25_call_resolution_test COMMAND m25_call_resolution_test)

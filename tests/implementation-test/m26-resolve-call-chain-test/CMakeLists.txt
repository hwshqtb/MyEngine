# M26: the CHAIN-level resolver — meta_object_t::resolve_call(name) / resolve_call(name, args).
#
# meta_info_t::resolve_call() answers from ONE table and is silent; this test pins the layer above it:
# own-first walking (this, then super_class()), the first table that answers winning, an ambiguity
# being terminal, a miss naming the arguments, and the DECLARING table travelling in the answer.
#
# The fixture forces every branch: a base with f(int) + g(), a derived that HIDES f(int) with its own
# f(int), adds f(double) and h()/h() const (the ambiguity + const pair), and a second derived with no f
# at all (the fall-through case).
add_executable(m26_resolve_call_chain_test m26_resolve_call_chain_test.cpp)
target_link_libraries(m26_resolve_call_chain_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m26_resolve_call_chain_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m26_resolve_call_chain_test COMMAND m26_resolve_call_chain_test)

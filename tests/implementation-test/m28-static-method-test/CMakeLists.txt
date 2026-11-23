# M28: STATIC member functions are rows of the method table.
#
# Phase N3 gave the method table its third kind of row. Before it, detail::is_aimed_function
# excluded every static member function, so the table could not describe one and no invoker could
# reach it. After it:
#   * the METHOD table collects statics (build_method_infos passes want_static=true) while the
#     PROPERTY accessor scan keeps excluding them (the default get_functions<T>() call);
#   * build_method_info classifies a static as method_kind_enum::StaticMethod, keys it with the bare
#     "f(int)" spelling (no cvref suffix) and gives it the static invoker;
#   * the two matchers skip the object-cvref block for a receiver-less row;
#   * meta_method_t grew invoke_static() (eight forms, mirroring invoke()) and meta_object_t grew
#     call_static<"name"> / call_static(name) / invoke_static(signature) with their twins;
#   * BOTH directions of the misuse rule are refused with exactly ONE Critical:
#     invoke(obj, ...) on a static row and invoke_static(...) on an instance row.
#
# This file pins all of that: the collection split, the classification and the key, the call with NO
# object through both the handle and the object layer, the overload pair f(int) instance /
# f(double) static, the property regression guard (a static accessor-shaped function must never bind
# as a property accessor), the misuse rule, the method_count() growth and the write/discard contract.
add_executable(m28_static_method_test m28_static_method_test.cpp)
target_link_libraries(m28_static_method_test PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(m28_static_method_test PRIVATE -Wl,--allow-multiple-definition)
endif()
add_test(NAME m28_static_method_test COMMAND m28_static_method_test)

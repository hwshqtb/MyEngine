# Negative tests: each file below violates exactly one compile-time check and MUST NOT compile.
# Every case is registered with WILL_FAIL, so CTest reports success when the compiler rejects it.
# (There is deliberately no "undeclared metatype" case any more: a type's interface is produced on
# demand from its reflection, so there is no declaration to forget.)
set(negative_cases
    m10_negative_required_hidden
    m10_negative_two_user
    m10_negative_ambiguous_accessor
    m22_negative_object_through_plain
    m22_negative_gadget_over_object
    # N4 rollback: an explicit object parameter ("deducing-this") is NOT modelled by the meta-object,
    # and an unannotated one is refused by name from build_meta<T>(). (Its [[= make_hidden()]] twin is
    # the opt-out and is exercised by the POSITIVE suite instead — m28-static-method-test.)
    m29_negative_deducing_this
)

foreach(case IN LISTS negative_cases)
    add_test(NAME ${case}
             COMMAND ${CMAKE_CXX_COMPILER} -std=c++26 -freflection -fsyntax-only
                     -I${PROJECT_SOURCE_DIR}/include
                     ${CMAKE_CURRENT_SOURCE_DIR}/${case}.cpp)
    set_tests_properties(${case} PROPERTIES WILL_FAIL TRUE)
endforeach()

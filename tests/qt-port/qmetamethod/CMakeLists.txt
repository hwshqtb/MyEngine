add_executable(qt_port_qmetamethod qmetamethod_test.cpp)
target_link_libraries(qt_port_qmetamethod PRIVATE metaobject)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(qt_port_qmetamethod PRIVATE -Wl,--allow-multiple-definition)
    # This is the largest meta-object in the tree: three fixture classes with 52 reflected entries
    # (44 methods + 8 constructors), Qt's 42-row method_data() table and ~114k assertions make
    # build_meta's consteval pass exceed GCC's default -fconstexpr-ops-limit (33,554,432). The budget
    # is raised for THIS target only; the library and every other target keep the default.
    target_compile_options(qt_port_qmetamethod PRIVATE -fconstexpr-ops-limit=50000000)
endif()
add_test(NAME qt_port_qmetamethod COMMAND qt_port_qmetamethod)

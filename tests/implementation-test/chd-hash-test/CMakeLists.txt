add_executable(chd_hash_test chd_hash_test.cpp)
target_include_directories(chd_hash_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_test(NAME chd_hash_test COMMAND chd_hash_test)

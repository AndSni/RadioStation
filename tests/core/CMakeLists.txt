add_executable(rs_core_tests
    LoggerTest.cpp
)

target_link_libraries(rs_core_tests PRIVATE
    rs_core
    Qt6::Test
)

add_test(NAME rs_core_tests COMMAND rs_core_tests)

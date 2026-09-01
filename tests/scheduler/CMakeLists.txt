add_executable(rs_rotation_picker_tests
    RotationWeightedPickerTest.cpp
)

target_link_libraries(rs_rotation_picker_tests PRIVATE
    rs_scheduler
    rs_db
    rs_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME rs_rotation_picker_tests COMMAND rs_rotation_picker_tests)

add_executable(rs_block_time_resolver_tests
    BlockTimeResolverTest.cpp
)

target_link_libraries(rs_block_time_resolver_tests PRIVATE
    rs_scheduler
    rs_db
    rs_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME rs_block_time_resolver_tests COMMAND rs_block_time_resolver_tests)

add_executable(rs_clock_wheel_tests
    ClockWheelTest.cpp
)

target_link_libraries(rs_clock_wheel_tests PRIVATE
    rs_scheduler
    rs_db
    rs_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME rs_clock_wheel_tests COMMAND rs_clock_wheel_tests)

add_executable(rs_autodj_tests
    AutoDjEngineTest.cpp
)

target_link_libraries(rs_autodj_tests PRIVATE
    rs_scheduler
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)

add_test(NAME rs_autodj_tests COMMAND rs_autodj_tests)

add_executable(rs_clock_engine_tests
    ClockEngineTest.cpp
)

target_link_libraries(rs_clock_engine_tests PRIVATE
    rs_scheduler
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)

add_test(NAME rs_clock_engine_tests COMMAND rs_clock_engine_tests)

add_executable(rs_schedule_block_validity_tests
    ScheduleBlockValidityTest.cpp
)

target_link_libraries(rs_schedule_block_validity_tests PRIVATE
    rs_scheduler
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)

add_test(NAME rs_schedule_block_validity_tests COMMAND rs_schedule_block_validity_tests)

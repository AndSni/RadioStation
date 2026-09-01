add_library(rs_scheduler STATIC
    RotationWeightedPicker.cpp
    RotationWeightedPicker.h
    BlockTimeResolver.cpp
    BlockTimeResolver.h
    AutoDjEngine.cpp
    AutoDjEngine.h
    ScheduleBlockValidity.cpp
    ScheduleBlockValidity.h
    ClockWheel.cpp
    ClockWheel.h
    ClockEngine.cpp
    ClockEngine.h
)

target_include_directories(rs_scheduler PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(rs_scheduler PUBLIC Qt6::Core rs_core rs_db)

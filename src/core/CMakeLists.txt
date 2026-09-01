add_library(rs_core STATIC
    Logging.cpp
    Logging.h
)

target_include_directories(rs_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(rs_core PUBLIC Qt6::Core)

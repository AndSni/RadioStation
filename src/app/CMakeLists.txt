add_executable(rs_app
    main.cpp
)

target_compile_definitions(rs_app PRIVATE RS_APP_VERSION="${PROJECT_VERSION}")

target_link_libraries(rs_app PRIVATE
    rs_ui
    rs_core
    rs_db
    Qt6::Widgets
    Qt6::Svg
    PkgConfig::GST
)

add_executable(rs_migrations_tests
    MigrationsTest.cpp
)
target_link_libraries(rs_migrations_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_migrations_tests COMMAND rs_migrations_tests)

add_executable(rs_playlist_repository_tests
    PlaylistRepositoryTest.cpp
)
target_link_libraries(rs_playlist_repository_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_playlist_repository_tests COMMAND rs_playlist_repository_tests)

add_executable(rs_playlist_folder_repository_tests
    PlaylistFolderRepositoryTest.cpp
)
target_link_libraries(rs_playlist_folder_repository_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_playlist_folder_repository_tests COMMAND rs_playlist_folder_repository_tests)

add_executable(rs_schedule_block_repository_tests
    ScheduleBlockRepositoryTest.cpp
)
target_link_libraries(rs_schedule_block_repository_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_schedule_block_repository_tests COMMAND rs_schedule_block_repository_tests)

add_executable(rs_clock_repository_tests
    ClockRepositoryTest.cpp
)
target_link_libraries(rs_clock_repository_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_clock_repository_tests COMMAND rs_clock_repository_tests)

add_executable(rs_track_repository_tests
    TrackRepositoryTest.cpp
)
target_link_libraries(rs_track_repository_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_track_repository_tests COMMAND rs_track_repository_tests)

add_executable(rs_smart_playlist_repository_tests
    SmartPlaylistRepositoryTest.cpp
)
target_link_libraries(rs_smart_playlist_repository_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_smart_playlist_repository_tests COMMAND rs_smart_playlist_repository_tests)

add_executable(rs_smart_playlist_filter_tests
    SmartPlaylistFilterTest.cpp
)
target_link_libraries(rs_smart_playlist_filter_tests PRIVATE
    rs_db
    rs_core
    Qt6::Core
    Qt6::Sql
    Qt6::Test
)
add_test(NAME rs_smart_playlist_filter_tests COMMAND rs_smart_playlist_filter_tests)

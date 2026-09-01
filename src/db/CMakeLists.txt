add_library(rs_db STATIC
    TrackRecord.h
    Database.cpp
    Database.h
    Migrations.cpp
    Migrations.h
    TrackRepository.cpp
    TrackRepository.h
    RotationCategoryRepository.cpp
    RotationCategoryRepository.h
    PlaylistRepository.cpp
    PlaylistRepository.h
    PlaylistFolderRepository.cpp
    PlaylistFolderRepository.h
    ScheduleBlockRepository.cpp
    ScheduleBlockRepository.h
    ClockRepository.cpp
    ClockRepository.h
    PlayHistoryRepository.cpp
    PlayHistoryRepository.h
    LoudnessHistoryRepository.cpp
    LoudnessHistoryRepository.h
    CartRepository.cpp
    CartRepository.h
    SmartPlaylistRepository.cpp
    SmartPlaylistRepository.h
    SmartPlaylistFilter.cpp
    SmartPlaylistFilter.h
)

target_include_directories(rs_db PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(rs_db PUBLIC Qt6::Core Qt6::Sql rs_core)

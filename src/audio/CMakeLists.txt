add_library(rs_audio STATIC
    GstEngineThread.cpp
    GstEngineThread.h
    RingBuffer.h
    GstSourcePipeline.cpp
    GstSourcePipeline.h
    MixEngine.cpp
    MixEngine.h
    DeckEngine.cpp
    DeckEngine.h
    AudioEngine.cpp
    AudioEngine.h
    CrossfadeController.cpp
    CrossfadeController.h
    MetadataScanner.cpp
    MetadataScanner.h
    CartSlotPlayer.cpp
    CartSlotPlayer.h
    CartWallEngine.cpp
    CartWallEngine.h
    CartPicker.cpp
    CartPicker.h
    CartAutomationEngine.cpp
    CartAutomationEngine.h
    StreamEncoderBin.cpp
    StreamEncoderBin.h
    ReplayGainAnalyzer.cpp
    ReplayGainAnalyzer.h
    DuckingController.cpp
    DuckingController.h
)

target_include_directories(rs_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(rs_audio PUBLIC Qt6::Core PkgConfig::GST PkgConfig::TAGLIB rs_core rs_db rs_scheduler miniaudio)

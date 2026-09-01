add_executable(rs_audio_tests
    AudioEngineTest.cpp
)

target_link_libraries(rs_audio_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_audio_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_audio_tests COMMAND rs_audio_tests)
set_tests_properties(rs_audio_tests PROPERTIES ENVIRONMENT "RS_AUDIO_SINK=fakesink")

add_executable(rs_crossfade_tests
    CrossfadeControllerTest.cpp
)

target_link_libraries(rs_crossfade_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_crossfade_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_crossfade_tests COMMAND rs_crossfade_tests)
set_tests_properties(rs_crossfade_tests PROPERTIES ENVIRONMENT "RS_AUDIO_SINK=fakesink")

add_executable(rs_cartwall_tests
    CartWallEngineTest.cpp
)

target_link_libraries(rs_cartwall_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_cartwall_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_cartwall_tests COMMAND rs_cartwall_tests)
set_tests_properties(rs_cartwall_tests PROPERTIES ENVIRONMENT "RS_AUDIO_SINK=fakesink")

add_executable(rs_cart_picker_tests
    CartPickerTest.cpp
)

target_link_libraries(rs_cart_picker_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME rs_cart_picker_tests COMMAND rs_cart_picker_tests)

add_executable(rs_cart_automation_tests
    CartAutomationEngineTest.cpp
)

target_link_libraries(rs_cart_automation_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_cart_automation_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_cart_automation_tests COMMAND rs_cart_automation_tests)
set_tests_properties(rs_cart_automation_tests PROPERTIES ENVIRONMENT "RS_AUDIO_SINK=fakesink")

add_executable(rs_streamencoder_tests
    StreamEncoderTest.cpp
)

target_link_libraries(rs_streamencoder_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_streamencoder_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_streamencoder_tests COMMAND rs_streamencoder_tests)
set_tests_properties(rs_streamencoder_tests PROPERTIES ENVIRONMENT "RS_AUDIO_SINK=fakesink")

add_executable(rs_ringbuffer_tests
    RingBufferTest.cpp
)

target_link_libraries(rs_ringbuffer_tests PRIVATE
    rs_audio
    Qt6::Core
    Qt6::Test
)

add_test(NAME rs_ringbuffer_tests COMMAND rs_ringbuffer_tests)

add_executable(rs_mixengine_tests
    MixEngineTest.cpp
)

target_link_libraries(rs_mixengine_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_mixengine_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_mixengine_tests COMMAND rs_mixengine_tests)

add_executable(rs_deckengine_stress_tests
    DeckEngineStressTest.cpp
)

target_link_libraries(rs_deckengine_stress_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_deckengine_stress_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_deckengine_stress_tests COMMAND rs_deckengine_stress_tests)
set_tests_properties(rs_deckengine_stress_tests PROPERTIES
    ENVIRONMENT "RS_AUDIO_SINK=fakesink"
    TIMEOUT 60
)

add_executable(rs_gstsourcepipeline_tests
    GstSourcePipelineTest.cpp
)

target_link_libraries(rs_gstsourcepipeline_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_gstsourcepipeline_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_gstsourcepipeline_tests COMMAND rs_gstsourcepipeline_tests)
set_tests_properties(rs_gstsourcepipeline_tests PROPERTIES ENVIRONMENT "RS_AUDIO_SINK=fakesink")

add_executable(rs_metadatascanner_tests
    MetadataScannerTest.cpp
)

target_link_libraries(rs_metadatascanner_tests PRIVATE
    rs_audio
    rs_core
    rs_db
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_metadatascanner_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_metadatascanner_tests COMMAND rs_metadatascanner_tests)
set_tests_properties(rs_metadatascanner_tests PROPERTIES ENVIRONMENT "RS_AUDIO_SINK=fakesink" TIMEOUT 60)

add_executable(rs_replaygain_tests
    ReplayGainAnalyzerTest.cpp
)

target_link_libraries(rs_replaygain_tests PRIVATE
    rs_audio
    rs_core
    rs_db
    Qt6::Core
    Qt6::Test
)

target_compile_definitions(rs_replaygain_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

add_test(NAME rs_replaygain_tests COMMAND rs_replaygain_tests)
set_tests_properties(rs_replaygain_tests PROPERTIES TIMEOUT 60)

add_executable(rs_ducking_tests
    DuckingControllerTest.cpp
)

target_link_libraries(rs_ducking_tests PRIVATE
    rs_audio
    rs_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME rs_ducking_tests COMMAND rs_ducking_tests)


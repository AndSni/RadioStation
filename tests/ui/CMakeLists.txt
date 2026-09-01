add_executable(rs_ui_tests
    MainWindowTest.cpp
)

target_link_libraries(rs_ui_tests PRIVATE
    rs_ui
    rs_core
    rs_db
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_ui_tests COMMAND rs_ui_tests)
set_tests_properties(rs_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink")

add_executable(rs_deckwidget_tests
    DeckWidgetTest.cpp
)

target_link_libraries(rs_deckwidget_tests PRIVATE
    rs_ui
    rs_audio
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

target_compile_definitions(rs_deckwidget_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../audio/fixtures"
)

add_test(NAME rs_deckwidget_tests COMMAND rs_deckwidget_tests)
set_tests_properties(rs_deckwidget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink")

add_executable(rs_cartbutton_tests
    CartButtonTest.cpp
)

target_link_libraries(rs_cartbutton_tests PRIVATE
    rs_ui
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_cartbutton_tests COMMAND rs_cartbutton_tests)
set_tests_properties(rs_cartbutton_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_autodj_integration_tests
    AutoDjIntegrationTest.cpp
)

target_link_libraries(rs_autodj_integration_tests PRIVATE
    rs_ui
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

target_compile_definitions(rs_autodj_integration_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../audio/fixtures"
)

add_test(NAME rs_autodj_integration_tests COMMAND rs_autodj_integration_tests)
set_tests_properties(rs_autodj_integration_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink"
)

add_executable(rs_audiopillpainter_tests
    AudioPillPainterTest.cpp
)

target_link_libraries(rs_audiopillpainter_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_audiopillpainter_tests COMMAND rs_audiopillpainter_tests)
set_tests_properties(rs_audiopillpainter_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_audioobjectmime_tests
    AudioObjectMimeTest.cpp
)

target_link_libraries(rs_audioobjectmime_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_audioobjectmime_tests COMMAND rs_audioobjectmime_tests)
set_tests_properties(rs_audioobjectmime_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_queuelistwidget_tests
    QueueListWidgetTest.cpp
)

target_link_libraries(rs_queuelistwidget_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_queuelistwidget_tests COMMAND rs_queuelistwidget_tests)
set_tests_properties(rs_queuelistwidget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_playlisttracklistwidget_tests
    PlaylistTrackListWidgetTest.cpp
)

target_link_libraries(rs_playlisttracklistwidget_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_playlisttracklistwidget_tests COMMAND rs_playlisttracklistwidget_tests)
set_tests_properties(rs_playlisttracklistwidget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_crossfaderwidget_tests
    CrossfaderWidgetTest.cpp
)

target_link_libraries(rs_crossfaderwidget_tests PRIVATE
    rs_ui
    rs_audio
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

target_compile_definitions(rs_crossfaderwidget_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../audio/fixtures"
)

add_test(NAME rs_crossfaderwidget_tests COMMAND rs_crossfaderwidget_tests)
set_tests_properties(rs_crossfaderwidget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink")

add_executable(rs_autodjpanelwidget_tests
    AutoDjPanelWidgetTest.cpp
)

target_link_libraries(rs_autodjpanelwidget_tests PRIVATE
    rs_ui
    rs_audio
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_autodjpanelwidget_tests COMMAND rs_autodjpanelwidget_tests)
set_tests_properties(rs_autodjpanelwidget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink")

add_executable(rs_tracktablemodel_tests
    TrackTableModelTest.cpp
)

target_link_libraries(rs_tracktablemodel_tests PRIVATE
    rs_ui
    rs_db
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_tracktablemodel_tests COMMAND rs_tracktablemodel_tests)
set_tests_properties(rs_tracktablemodel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_smartplaylistfilterproxymodel_tests
    SmartPlaylistFilterProxyModelTest.cpp
)

target_link_libraries(rs_smartplaylistfilterproxymodel_tests PRIVATE
    rs_ui
    rs_db
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_smartplaylistfilterproxymodel_tests COMMAND rs_smartplaylistfilterproxymodel_tests)
set_tests_properties(rs_smartplaylistfilterproxymodel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_smartplaylistpanelwidget_tests
    SmartPlaylistPanelWidgetTest.cpp
)

target_link_libraries(rs_smartplaylistpanelwidget_tests PRIVATE
    rs_ui
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Sql
    Qt6::Test
)

add_test(NAME rs_smartplaylistpanelwidget_tests COMMAND rs_smartplaylistpanelwidget_tests)
set_tests_properties(rs_smartplaylistpanelwidget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_mixerpanelwidget_tests
    MixerPanelWidgetTest.cpp
)

target_link_libraries(rs_mixerpanelwidget_tests PRIVATE
    rs_ui
    rs_audio
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_mixerpanelwidget_tests COMMAND rs_mixerpanelwidget_tests)
set_tests_properties(rs_mixerpanelwidget_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink")

add_executable(rs_blocktransitioncontroller_tests
    BlockTransitionControllerTest.cpp
)

target_link_libraries(rs_blocktransitioncontroller_tests PRIVATE
    rs_ui
    rs_audio
    rs_scheduler
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

target_compile_definitions(rs_blocktransitioncontroller_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../audio/fixtures"
)

add_test(NAME rs_blocktransitioncontroller_tests COMMAND rs_blocktransitioncontroller_tests)
set_tests_properties(rs_blocktransitioncontroller_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink"
)

add_executable(rs_onairlabel_tests
    OnAirLabelTest.cpp
)

target_link_libraries(rs_onairlabel_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_onairlabel_tests COMMAND rs_onairlabel_tests)
set_tests_properties(rs_onairlabel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_indicatorbutton_tests
    IndicatorButtonTest.cpp
)

target_link_libraries(rs_indicatorbutton_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_indicatorbutton_tests COMMAND rs_indicatorbutton_tests)
set_tests_properties(rs_indicatorbutton_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_lcdreadout_tests
    LcdReadoutTest.cpp
)

target_link_libraries(rs_lcdreadout_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_lcdreadout_tests COMMAND rs_lcdreadout_tests)
set_tests_properties(rs_lcdreadout_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_rotaryknob_tests
    RotaryKnobTest.cpp
)

target_link_libraries(rs_rotaryknob_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_rotaryknob_tests COMMAND rs_rotaryknob_tests)
set_tests_properties(rs_rotaryknob_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_consolefader_tests
    ConsoleFaderTest.cpp
)

target_link_libraries(rs_consolefader_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_consolefader_tests COMMAND rs_consolefader_tests)
set_tests_properties(rs_consolefader_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_consolebutton_tests
    ConsoleButtonTest.cpp
)

target_link_libraries(rs_consolebutton_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_consolebutton_tests COMMAND rs_consolebutton_tests)
set_tests_properties(rs_consolebutton_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_dotmatrixdisplay_tests
    DotMatrixDisplayTest.cpp
)

target_link_libraries(rs_dotmatrixdisplay_tests PRIVATE
    rs_ui
    Qt6::Widgets
    Qt6::Test
)

add_test(NAME rs_dotmatrixdisplay_tests COMMAND rs_dotmatrixdisplay_tests)
set_tests_properties(rs_dotmatrixdisplay_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(rs_radiostatisticspanel_tests
    RadioStatisticsPanelTest.cpp
)

target_link_libraries(rs_radiostatisticspanel_tests PRIVATE
    rs_ui
    rs_audio
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

target_compile_definitions(rs_radiostatisticspanel_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../audio/fixtures"
)

add_test(NAME rs_radiostatisticspanel_tests COMMAND rs_radiostatisticspanel_tests)
set_tests_properties(rs_radiostatisticspanel_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink")

add_executable(rs_cartgridwidget_tests
    CartGridWidgetTest.cpp
)

target_link_libraries(rs_cartgridwidget_tests PRIVATE
    rs_ui
    rs_audio
    rs_db
    rs_core
    Qt6::Widgets
    Qt6::Test
)

target_compile_definitions(rs_cartgridwidget_tests PRIVATE
    RS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../audio/fixtures"
)

add_test(NAME rs_cartgridwidget_tests COMMAND rs_cartgridwidget_tests)
set_tests_properties(rs_cartgridwidget_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;RS_AUDIO_SINK=fakesink"
)

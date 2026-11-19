add_executable(KyumistEditor
    main.cpp
    panels/EditorPanels.cpp
    panels/ScriptEditorPanel.cpp
)

target_include_directories(KyumistEditor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(KyumistEditor PRIVATE
    kyumist_engine
)


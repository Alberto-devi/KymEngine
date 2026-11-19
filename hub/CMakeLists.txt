add_executable(KymEngineHub
    main.cpp
    VersionManager.cpp
    VersionManager.hpp
)

target_link_libraries(KymEngineHub PRIVATE
    kyumist_engine
)

if(WIN32)
    target_link_libraries(KymEngineHub PRIVATE shell32 ole32 uuid)
    set_target_properties(KymEngineHub PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()


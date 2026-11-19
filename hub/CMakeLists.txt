add_executable(KymEngineHub
    main.cpp
    VersionManager.cpp
    VersionManager.hpp
)

target_link_libraries(KymEngineHub PRIVATE
    kyumist_engine
)

if(WIN32)
    set_target_properties(KymEngineHub PROPERTIES
        WIN32_EXECUTABLE FALSE
    )
endif()


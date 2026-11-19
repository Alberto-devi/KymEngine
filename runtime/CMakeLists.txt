add_executable(KyumistRuntime
    main.cpp
)

target_link_libraries(KyumistRuntime PRIVATE
    kyumist_engine
)


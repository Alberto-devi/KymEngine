add_executable(KyumistTests
    main.cpp
)

target_link_libraries(KyumistTests PRIVATE
    kyumist_engine
)

add_test(NAME KyumistTests COMMAND KyumistTests)


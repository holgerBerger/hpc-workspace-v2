add_executable(utils_test
    utils_test.cpp
)
target_link_libraries(utils_test
    PRIVATE
        ws_common
        mail
        Catch2::Catch2WithMain
)
catch_discover_tests(utils_test)

add_executable(user_test
    user_test.cpp
)
target_link_libraries(user_test
    PRIVATE
        ws_common
        Catch2::Catch2WithMain
)
catch_discover_tests(user_test)

add_executable(config_test
    config_test.cpp
)
target_link_libraries(config_test
    PRIVATE
        ws_common
        Catch2::Catch2WithMain
)
catch_discover_tests(config_test)


add_executable(caps_test
    caps_test.cpp
)
target_link_libraries(caps_test
    PRIVATE
        ws_common
        Catch2::Catch2WithMain
)
catch_discover_tests(caps_test)

add_executable(UserConfig_test
    UserConfig_test.cpp
)
target_link_libraries(UserConfig_test
    PRIVATE
        ws_common
        Catch2::Catch2WithMain
)
catch_discover_tests(UserConfig_test)

add_executable(dbv1_test
    dbv1_test.cpp
)
target_link_libraries(dbv1_test
    PRIVATE
        ws_common
        Catch2::Catch2WithMain
)
catch_discover_tests(dbv1_test)

add_executable(db_test
    db_test.cpp
)
target_link_libraries(db_test
    PRIVATE
        ws_common
        Catch2::Catch2WithMain
)
catch_discover_tests(db_test)

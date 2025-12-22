# Attempt to find system GTest; otherwise fetch it.
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        gtest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    FetchContent_MakeAvailable(gtest)
endif()

add_executable(broker_tests
    account_manager_test.cpp
    fee_config_test.cpp
    rate_limiter_test.cpp
    matching_engine_test.cpp
    session_manager_test.cpp
    performance_test.cpp
    integration_test.cpp
    stress_test.cpp
)

target_link_libraries(broker_tests PRIVATE
    GTest::gtest
    GTest::gtest_main
    broker_core
    pthread
)

add_test(NAME broker_tests COMMAND broker_tests)

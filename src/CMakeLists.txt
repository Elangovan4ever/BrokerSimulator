set(CORE_FILES
    core/time_engine.cpp
    core/matching_engine.cpp
    core/session_manager.cpp
    core/account_manager.cpp
    core/performance.cpp
    control/control_server.cpp
    control/alpaca_controller.cpp
    control/polygon_controller.cpp
    control/finnhub_controller.cpp
    ws/ws_controller.cpp
)
set(APP_FILES
    main.cpp
)

if(clickhouse-cpp_FOUND)
    list(APPEND CORE_FILES core/data_source_clickhouse.cpp)
    add_compile_definitions(USE_CLICKHOUSE)
endif()

# PostgreSQL support for Alpaca account persistence
find_package(PostgreSQL QUIET)
if(PostgreSQL_FOUND)
    list(APPEND CORE_FILES core/postgres_store.cpp)
    add_compile_definitions(USE_POSTGRES)
    message(STATUS "PostgreSQL found: ${PostgreSQL_INCLUDE_DIRS}")
else()
    # Try pkg-config fallback
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(LIBPQ QUIET libpq)
        if(LIBPQ_FOUND)
            list(APPEND CORE_FILES core/postgres_store.cpp)
            add_compile_definitions(USE_POSTGRES)
            set(PostgreSQL_INCLUDE_DIRS ${LIBPQ_INCLUDE_DIRS})
            set(PostgreSQL_LIBRARIES ${LIBPQ_LIBRARIES})
            message(STATUS "PostgreSQL found via pkg-config: ${LIBPQ_LIBRARIES}")
        endif()
    endif()
endif()

add_library(broker_core STATIC ${CORE_FILES})
target_link_libraries(broker_core PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    Threads::Threads
)

if(clickhouse-cpp_FOUND)
    target_include_directories(broker_core PUBLIC ${CLICKHOUSE_CPP_INCLUDE})
    if(DEFINED CLICKHOUSE_CPP_LIB_PATH)
        target_link_libraries(broker_core PUBLIC ${CLICKHOUSE_CPP_LIB_PATH} z lz4 zstd)
        target_link_options(broker_core PUBLIC -Wl,-rpath,"${CLICKHOUSE_ROOT}/lib")
    else()
        target_link_libraries(broker_core PUBLIC clickhouse-cpp-lib)
    endif()
endif()

if(USE_DROGON)
    target_link_libraries(broker_core PUBLIC Drogon::Drogon)
endif()

# PostgreSQL linking
if(PostgreSQL_FOUND OR LIBPQ_FOUND)
    target_include_directories(broker_core PUBLIC ${PostgreSQL_INCLUDE_DIRS})
    target_link_libraries(broker_core PUBLIC ${PostgreSQL_LIBRARIES} pq)
endif()

target_include_directories(broker_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    /usr/include/jsoncpp
    /usr/include/postgresql
)

add_executable(broker_simulator ${APP_FILES})
target_link_libraries(broker_simulator PRIVATE broker_core)
set_target_properties(broker_simulator PROPERTIES OUTPUT_NAME "broker_simulator")

if(BUILD_PERF)
    add_executable(event_queue_bench perf/event_queue_bench.cpp)
    target_link_libraries(event_queue_bench PRIVATE broker_core)
    set_target_properties(event_queue_bench PROPERTIES OUTPUT_NAME "event_queue_bench")
endif()

if(clickhouse-cpp_FOUND)
    if(DEFINED CLICKHOUSE_CPP_LIB_PATH)
        target_link_libraries(broker_simulator PRIVATE ${CLICKHOUSE_CPP_LIB_PATH})
        target_link_options(broker_simulator PRIVATE -Wl,-rpath,"${CLICKHOUSE_ROOT}/lib")
    else()
        target_link_libraries(broker_simulator PRIVATE clickhouse-cpp-lib)
    endif()
endif()

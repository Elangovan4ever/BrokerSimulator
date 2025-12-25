cmake_minimum_required(VERSION 3.20)
project(broker_simulator VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BUILD_TESTS "Build unit tests" ON)
option(BUILD_PERF "Build perf benchmarks" ON)
option(USE_DROGON "Use Drogon for HTTP/WS (requires libdrogon-dev)" ON)
option(USE_CLICKHOUSE "Use ClickHouse client if available" ON)
option(USE_WEBSOCKETPP "Enable websocketpp server" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)

if(ENABLE_ASAN)
  message(STATUS "AddressSanitizer enabled")
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address)
endif()

include(FetchContent)

# cpp-httplib (header-only)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# Optional Drogon fetch if not available system-wide
if(USE_DROGON)
  find_package(Drogon REQUIRED)
endif()

# nlohmann_json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

find_package(Threads REQUIRED)
if(USE_CLICKHOUSE)
  find_package(clickhouse-cpp QUIET)
  if(NOT clickhouse-cpp_FOUND)
    set(CLICKHOUSE_ROOT "$ENV{HOME}/.local")
    find_path(CLICKHOUSE_CPP_INCLUDE_DIR clickhouse/client.h
      PATHS "${CLICKHOUSE_ROOT}/include")
    find_library(CLICKHOUSE_CPP_LIB clickhouse-cpp-lib
      PATHS "${CLICKHOUSE_ROOT}/lib")
    if(CLICKHOUSE_CPP_INCLUDE_DIR AND CLICKHOUSE_CPP_LIB)
      set(clickhouse-cpp_FOUND TRUE)
      set(CLICKHOUSE_CPP_LIB_PATH ${CLICKHOUSE_CPP_LIB})
      set(CLICKHOUSE_CPP_INCLUDE ${CLICKHOUSE_CPP_INCLUDE_DIR})
      message(STATUS "Using clickhouse-cpp from ${CLICKHOUSE_ROOT}")
    endif()
  endif()
endif()
find_package(GTest QUIET)

if(USE_WEBSOCKETPP)
  find_package(Boost COMPONENTS system REQUIRED)
  FetchContent_Declare(
    websocketpp
    GIT_REPOSITORY https://github.com/zaphoyd/websocketpp.git
    GIT_TAG 0.8.2
  )
  set(WEBSOCKETPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(WEBSOCKETPP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(websocketpp)
endif()

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

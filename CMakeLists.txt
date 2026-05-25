cmake_minimum_required(VERSION 3.16)

# Policy for older CMake compatibility in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(polymarket_cpp_client VERSION 1.2.2 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(POLYMARKET_CLIENT_BUILD_EXAMPLES "Build example/test executables" ON)
option(POLYMARKET_CLIENT_BUILD_TESTS "Build test executables" ON)
option(POLYMARKET_CLIENT_BUILD_BENCHMARKS "Build local benchmark executables" OFF)

include(CMakePackageConfigHelpers)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags for performance
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -DDEBUG")

# Find required packages
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Fetch dependencies
include(FetchContent)

# nlohmann/json for JSON parsing
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# ixwebsocket - modern C++ WebSocket library (easier to build than libwebsockets)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG v11.4.5
)

# secp256k1 for ECDSA signing
FetchContent_Declare(
    secp256k1
    GIT_REPOSITORY https://github.com/bitcoin-core/secp256k1.git
    GIT_TAG v0.4.1
)

# ethash for keccak256 hashing
FetchContent_Declare(
    ethash
    GIT_REPOSITORY https://github.com/chfast/ethash.git
    GIT_TAG v1.0.1
)

set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY ON CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF CACHE BOOL "" FORCE)
set(SECP256K1_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(json ixwebsocket secp256k1 ethash)

# Core client library
set(POLYMARKET_CLIENT_SOURCES
    src/decimal_math.cpp
    src/evm_event_indexer.cpp
    src/evm_utils.cpp
    src/http_client.cpp
    src/json_rpc_client.cpp
    src/websocket_client.cpp
    src/websocket_client_resilience.cpp
    src/websocket_resilience.cpp
    src/market_fetcher.cpp
    src/orderbook.cpp
    src/clob_order_execution.cpp
    src/order_execution.cpp
    src/polymarket_events.cpp
    src/order_signer.cpp
    src/order_signer_v2.cpp
    src/sdk_error.cpp
    src/clob_client.cpp
)

add_library(polymarket_client ${POLYMARKET_CLIENT_SOURCES})
add_library(polymarket::client ALIAS polymarket_client)

target_include_directories(polymarket_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(polymarket_client
    PUBLIC
        ${CURL_LIBRARIES}
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        nlohmann_json::nlohmann_json
        ixwebsocket
        secp256k1
        keccak
)

if(POLYMARKET_CLIENT_BUILD_EXAMPLES)
    add_executable(polymarket_arb src/main.cpp)
    target_link_libraries(polymarket_arb PRIVATE polymarket::client)

    add_executable(order_test src/order_test.cpp)
    target_link_libraries(order_test PRIVATE polymarket::client)

    add_executable(arb_test src/arb_test.cpp)
    target_link_libraries(arb_test PRIVATE polymarket::client)

    add_executable(clob_client_test src/clob_client_test.cpp)
    target_link_libraries(clob_client_test PRIVATE polymarket::client)

    add_executable(rest_example examples/rest_example.cpp)
    target_link_libraries(rest_example PRIVATE polymarket::client)

    add_executable(test_positions examples/test_positions.cpp)
    target_link_libraries(test_positions PRIVATE polymarket::client)

    add_executable(sign_example examples/sign_example.cpp)
    target_link_libraries(sign_example PRIVATE polymarket::client)

    add_executable(ws_example examples/ws_example.cpp)
    target_link_libraries(ws_example PRIVATE polymarket::client)

    add_executable(uma_oracle_watch examples/uma_oracle_watch.cpp)
    target_link_libraries(uma_oracle_watch PRIVATE polymarket::client)

    add_executable(condition_resolution_watch examples/condition_resolution_watch.cpp)
    target_link_libraries(condition_resolution_watch PRIVATE polymarket::client)

    add_executable(feed_latency_benchmark examples/feed_latency_benchmark.cpp)
    target_link_libraries(feed_latency_benchmark PRIVATE polymarket::client)

    add_executable(evm_event_indexer_example examples/evm_event_indexer_example.cpp)
    target_link_libraries(evm_event_indexer_example PRIVATE polymarket::client)
endif()

if(POLYMARKET_CLIENT_BUILD_TESTS)
    enable_testing()
    add_executable(test_utils tests/test_utils.cpp)
    target_link_libraries(test_utils PRIVATE polymarket::client)
    add_test(NAME test_utils COMMAND test_utils)

    add_executable(test_evm_events tests/test_evm_events.cpp)
    target_link_libraries(test_evm_events PRIVATE polymarket::client)
    add_test(NAME test_evm_events COMMAND test_evm_events)

    add_executable(test_evm_event_indexer tests/test_evm_event_indexer.cpp)
    target_link_libraries(test_evm_event_indexer PRIVATE polymarket::client)
    add_test(NAME test_evm_event_indexer COMMAND test_evm_event_indexer)

    add_executable(test_order_signer_v2 tests/test_order_signer_v2.cpp)
    target_link_libraries(test_order_signer_v2 PRIVATE polymarket::client)
    add_test(NAME test_order_signer_v2 COMMAND test_order_signer_v2)

    add_executable(test_http_client_transport tests/test_http_client_transport.cpp)
    target_link_libraries(test_http_client_transport PRIVATE polymarket::client)
    add_test(NAME test_http_client_transport COMMAND test_http_client_transport)

    add_executable(test_order_execution tests/test_order_execution.cpp)
    target_include_directories(test_order_execution PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_order_execution PRIVATE polymarket::client)
    add_test(NAME test_order_execution COMMAND test_order_execution)

    add_executable(test_sdk_error tests/test_sdk_error.cpp)
    target_link_libraries(test_sdk_error PRIVATE polymarket::client)
    add_test(NAME test_sdk_error COMMAND test_sdk_error)

    add_executable(test_websocket_resilience tests/test_websocket_resilience.cpp)
    target_include_directories(test_websocket_resilience PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_websocket_resilience PRIVATE polymarket::client)
    add_test(NAME test_websocket_resilience COMMAND test_websocket_resilience)
endif()

if(POLYMARKET_CLIENT_BUILD_BENCHMARKS)
    add_executable(benchmark_v2_signing benchmarks/benchmark_v2_signing.cpp)
    target_link_libraries(benchmark_v2_signing PRIVATE polymarket::client)

    add_executable(benchmark_order_payload benchmarks/benchmark_order_payload.cpp)
    target_link_libraries(benchmark_order_payload PRIVATE polymarket::client)

    add_executable(benchmark_http_fixture benchmarks/benchmark_http_fixture.cpp)
    target_link_libraries(benchmark_http_fixture PRIVATE polymarket::client)

    add_executable(benchmark_ws_parse benchmarks/benchmark_ws_parse.cpp)
    target_link_libraries(benchmark_ws_parse PRIVATE polymarket::client)
endif()

# Install library, headers, and dependency targets into a single export set
install(TARGETS
    polymarket_client
    nlohmann_json
    ixwebsocket
    secp256k1
    keccak
    EXPORT polymarket_clientTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

install(DIRECTORY include/ DESTINATION include)

install(EXPORT polymarket_clientTargets
    FILE polymarket_clientTargets.cmake
    NAMESPACE polymarket::
    DESTINATION lib/cmake/polymarket_client
)

set(POLYMARKET_CLIENT_CONFIG ${CMAKE_CURRENT_BINARY_DIR}/polymarket_clientConfig.cmake)
set(POLYMARKET_CLIENT_CONFIG_VERSION ${CMAKE_CURRENT_BINARY_DIR}/polymarket_clientConfigVersion.cmake)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/polymarket_clientConfig.cmake.in
    ${POLYMARKET_CLIENT_CONFIG}
    INSTALL_DESTINATION lib/cmake/polymarket_client
)

write_basic_package_version_file(
    ${POLYMARKET_CLIENT_CONFIG_VERSION}
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${POLYMARKET_CLIENT_CONFIG}
    ${POLYMARKET_CLIENT_CONFIG_VERSION}
    DESTINATION lib/cmake/polymarket_client
)

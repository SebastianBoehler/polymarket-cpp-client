cmake_minimum_required(VERSION 3.22)

# Policy for older CMake compatibility in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

project(polymarket_cpp_client VERSION 2.0.0 LANGUAGES CXX C)

if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

set(POLYMARKET_CLIENT_TOP_LEVEL_DEFAULT OFF)
if("${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_SOURCE_DIR}")
    set(POLYMARKET_CLIENT_TOP_LEVEL_DEFAULT ON)
endif()

option(POLYMARKET_CLIENT_BUILD_EXAMPLES "Build example/test executables" ${POLYMARKET_CLIENT_TOP_LEVEL_DEFAULT})
option(POLYMARKET_CLIENT_BUILD_TESTS "Build test executables" ${POLYMARKET_CLIENT_TOP_LEVEL_DEFAULT})
option(POLYMARKET_CLIENT_BUILD_BENCHMARKS "Build local benchmark executables" OFF)
unset(POLYMARKET_CLIENT_TOP_LEVEL_DEFAULT)

include(CMakePackageConfigHelpers)

# Fetch dependencies
include(FetchContent)

# nlohmann/json for JSON parsing
FetchContent_Declare(
    json
    URL https://codeload.github.com/nlohmann/json/tar.gz/55f93686c01528224f448c19128836e7df245f72
    URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
)

# ixwebsocket - modern C++ WebSocket library (easier to build than libwebsockets)
FetchContent_Declare(
    ixwebsocket
    URL https://codeload.github.com/machinezone/IXWebSocket/tar.gz/64fae7676bd8fe31f7cb4bcde7a6841892dad65e
    URL_HASH SHA256=af641277dd79c8f0970a31b84beec223d8c683eb8a671b0cce20bd8a8f750878
)

# secp256k1 for ECDSA signing
FetchContent_Declare(
    secp256k1
    URL https://codeload.github.com/bitcoin-core/secp256k1/tar.gz/1a53f4961f337b4d166c25fce72ef0dc88806618
    URL_HASH SHA256=b51329bdd3b6c955380f4c4ca236d07dd8e109d4e869a4758be4602d8ed5fe31
)

# ethash for keccak256 hashing
FetchContent_Declare(
    ethash
    URL https://codeload.github.com/chfast/ethash/tar.gz/83bd5ad51acf7d7fdba215347cc805d51a194d2b
    URL_HASH SHA256=9c2b342411d6c68983a6b1cae752cf4b65d105b582b9e3f277f5112c9b76c90f
)

function(_polymarket_client_load_dependencies)
    find_package(CURL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)

    # Dependency options are normal variables in this function scope. CMP0077
    # makes option() honor them without overwriting a parent's cache entries.
    set(USE_TLS ON)
    set(USE_OPEN_SSL ON)
    set(IXWEBSOCKET_INSTALL OFF)
    set(SECP256K1_ENABLE_MODULE_RECOVERY ON)
    set(SECP256K1_BUILD_TESTS OFF)
    set(SECP256K1_BUILD_EXHAUSTIVE_TESTS OFF)
    set(SECP256K1_BUILD_BENCHMARK OFF)

    # IXWebSocket consumes this legacy variable. Imported targets keep its
    # exported link interface free of build-host library paths.
    set(OPENSSL_LIBRARIES OpenSSL::SSL OpenSSL::Crypto)
    FetchContent_MakeAvailable(json ixwebsocket secp256k1 ethash)

    foreach(dependency IN ITEMS json ixwebsocket secp256k1 ethash)
        set(${dependency}_SOURCE_DIR "${${dependency}_SOURCE_DIR}" PARENT_SCOPE)
    endforeach()
endfunction()

_polymarket_client_load_dependencies()

# We install IXWebSocket through our own export set; its upstream pkg-config
# file captures the configure-time prefix and is not relocatable.
set_target_properties(ixwebsocket secp256k1 PROPERTIES PUBLIC_HEADER "")

# Core client library
set(POLYMARKET_CLIENT_SOURCES
    src/arb_runtime.cpp
    src/arb_sizing.cpp
    src/bounded_workers.cpp
    src/decimal_math.cpp
    src/evm_cursor_io.cpp
    src/evm_cursor_store.cpp
    src/evm_event_dispatch.cpp
    src/evm_event_indexer.cpp
    src/evm_indexer_callbacks.cpp
    src/evm_indexer_lifecycle.cpp
    src/evm_indexer_transport.cpp
    src/evm_live_handoff.cpp
    src/evm_reorg_reconciliation.cpp
    src/evm_utils.cpp
    src/gamma_market_fetcher.cpp
    src/http_client.cpp
    src/http_client_connection.cpp
    src/http_global.cpp
    src/http_client_request.cpp
    src/json_rpc_client.cpp
    src/json_rpc_ws_dispatch.cpp
    src/json_rpc_ws_runtime.cpp
    src/websocket_client_callbacks.cpp
    src/websocket_client.cpp
    src/websocket_client_resilience.cpp
    src/websocket_client_teardown.cpp
    src/websocket_client_transport.cpp
    src/websocket_resilience.cpp
    src/websocket_market_data.cpp
    src/market_fetcher.cpp
    src/market_discovery.cpp
    src/market_time.cpp
    src/orderbook.cpp
    src/orderbook_runtime.cpp
    src/orderbook_runtime_lifecycle.cpp
    src/orderbook_runtime_stream.cpp
    src/orderbook_subscription.cpp
    src/orderbook_stream.cpp
    src/order_amounts.cpp
    src/clob_order_execution.cpp
    src/clob_order_submission.cpp
    src/order_execution.cpp
    src/polymarket_events.cpp
    src/order_signer.cpp
    src/order_signer_auth.cpp
    src/order_signer_domain.cpp
    src/order_signer_v2.cpp
    src/sdk_error.cpp
    src/clob_account.cpp
    src/clob_activity.cpp
    src/clob_activity_pagination.cpp
    src/clob_books.cpp
    src/clob_client.cpp
    src/clob_market_parsing.cpp
    src/clob_market_metadata.cpp
    src/clob_mutation_parsing.cpp
    src/clob_parsing.cpp
    src/clob_position_pages.cpp
    src/clob_positions.cpp
    src/clob_rewards_parsing.cpp
    src/clob_trade_parsing.cpp
    src/oracle_watcher.cpp
    src/oracle_event_history.cpp
)

add_library(polymarket_client ${POLYMARKET_CLIENT_SOURCES})
add_library(polymarket::client ALIAS polymarket_client)
set_target_properties(polymarket_client ixwebsocket secp256k1 keccak PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
set_target_properties(polymarket_client PROPERTIES EXPORT_NAME client)
target_compile_features(polymarket_client PUBLIC cxx_std_20)

target_include_directories(polymarket_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(polymarket_client
    PUBLIC
        CURL::libcurl
        OpenSSL::SSL
        OpenSSL::Crypto
        Threads::Threads
        nlohmann_json::nlohmann_json
        ixwebsocket
        secp256k1
        keccak
)

include(cmake/PolymarketExamples.cmake)
include(cmake/PolymarketTests.cmake)
include(cmake/PolymarketBenchmarks.cmake)

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
install(DIRECTORY ${json_SOURCE_DIR}/include/nlohmann DESTINATION include)
install(DIRECTORY ${ixwebsocket_SOURCE_DIR}/ixwebsocket/
    DESTINATION include/ixwebsocket
    FILES_MATCHING PATTERN "*.h"
)
set(POLYMARKET_CLIENT_LICENSE_DIR share/licenses/polymarket_client)
install(FILES LICENSE
    DESTINATION ${POLYMARKET_CLIENT_LICENSE_DIR}
    RENAME polymarket-client.txt)
install(FILES ${json_SOURCE_DIR}/LICENSE.MIT
    DESTINATION ${POLYMARKET_CLIENT_LICENSE_DIR}
    RENAME nlohmann-json.txt)
install(FILES ${ixwebsocket_SOURCE_DIR}/LICENSE.txt
    DESTINATION ${POLYMARKET_CLIENT_LICENSE_DIR}
    RENAME ixwebsocket.txt)
install(FILES ${secp256k1_SOURCE_DIR}/COPYING
    DESTINATION ${POLYMARKET_CLIENT_LICENSE_DIR}
    RENAME secp256k1.txt)
install(FILES ${ethash_SOURCE_DIR}/LICENSE
    DESTINATION ${POLYMARKET_CLIENT_LICENSE_DIR}
    RENAME ethash.txt)
unset(POLYMARKET_CLIENT_LICENSE_DIR)

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

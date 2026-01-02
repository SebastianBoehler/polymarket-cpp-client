cmake_minimum_required(VERSION 3.16)

# Policy for older CMake compatibility in dependencies
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(polymarket_cpp_client VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(POLYMARKET_CLIENT_BUILD_EXAMPLES "Build example/test executables" ON)
option(POLYMARKET_CLIENT_BUILD_TESTS "Build test executables" ON)

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
    src/http_client.cpp
    src/websocket_client.cpp
    src/market_fetcher.cpp
    src/orderbook.cpp
    src/order_signer.cpp
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

    add_executable(sign_example examples/sign_example.cpp)
    target_link_libraries(sign_example PRIVATE polymarket::client)

    add_executable(ws_example examples/ws_example.cpp)
    target_link_libraries(ws_example PRIVATE polymarket::client)
endif()

if(POLYMARKET_CLIENT_BUILD_TESTS)
    enable_testing()
    add_executable(test_utils tests/test_utils.cpp)
    target_link_libraries(test_utils PRIVATE polymarket::client)
    add_test(NAME test_utils COMMAND test_utils)
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

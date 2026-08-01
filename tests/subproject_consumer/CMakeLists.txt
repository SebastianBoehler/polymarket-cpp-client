cmake_minimum_required(VERSION 3.16)
project(polymarket_subproject_consumer LANGUAGES CXX C)

if(NOT POLYMARKET_CLIENT_SOURCE_DIR)
    message(FATAL_ERROR "POLYMARKET_CLIENT_SOURCE_DIR is required")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE OFF)
set(OPENSSL_LIBRARIES parent-openssl-sentinel)

set(USE_TLS OFF CACHE BOOL "parent sentinel" FORCE)
set(USE_OPEN_SSL OFF CACHE BOOL "parent sentinel" FORCE)
set(IXWEBSOCKET_INSTALL ON CACHE BOOL "parent sentinel" FORCE)
set(SECP256K1_ENABLE_MODULE_RECOVERY OFF CACHE BOOL "parent sentinel" FORCE)
set(SECP256K1_BUILD_TESTS ON CACHE BOOL "parent sentinel" FORCE)
set(SECP256K1_BUILD_EXHAUSTIVE_TESTS ON CACHE BOOL "parent sentinel" FORCE)
set(SECP256K1_BUILD_BENCHMARK ON CACHE BOOL "parent sentinel" FORCE)

add_subdirectory("${POLYMARKET_CLIENT_SOURCE_DIR}" polymarket-client)

if(NOT TARGET polymarket_client)
    message(FATAL_ERROR "subproject did not define polymarket_client")
endif()

function(assert_parent_value variable expected)
    if(NOT "${${variable}}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "subproject changed parent ${variable}: ${${variable}}")
    endif()
endfunction()

function(assert_parent_cache_value variable expected)
    get_property(actual CACHE "${variable}" PROPERTY VALUE)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR
            "subproject changed parent cache ${variable}: ${actual}")
    endif()
endfunction()

assert_parent_value(CMAKE_CXX_STANDARD 17)
assert_parent_value(CMAKE_CXX_STANDARD_REQUIRED OFF)
assert_parent_value(CMAKE_BUILD_TYPE Debug)
assert_parent_value(CMAKE_EXPORT_COMPILE_COMMANDS OFF)
assert_parent_value(CMAKE_POSITION_INDEPENDENT_CODE OFF)
assert_parent_value(OPENSSL_LIBRARIES parent-openssl-sentinel)
assert_parent_cache_value(USE_TLS OFF)
assert_parent_cache_value(USE_OPEN_SSL OFF)
assert_parent_cache_value(IXWEBSOCKET_INSTALL ON)
assert_parent_cache_value(SECP256K1_ENABLE_MODULE_RECOVERY OFF)
assert_parent_cache_value(SECP256K1_BUILD_TESTS ON)
assert_parent_cache_value(SECP256K1_BUILD_EXHAUSTIVE_TESTS ON)
assert_parent_cache_value(SECP256K1_BUILD_BENCHMARK ON)

get_target_property(ixwebsocket_definitions ixwebsocket
    INTERFACE_COMPILE_DEFINITIONS)
if(NOT IXWEBSOCKET_USE_TLS IN_LIST ixwebsocket_definitions OR
   NOT IXWEBSOCKET_USE_OPEN_SSL IN_LIST ixwebsocket_definitions)
    message(FATAL_ERROR "subproject did not configure IXWebSocket TLS locally")
endif()

foreach(unexpected_target IN ITEMS order_test polymarket_arb test_utils benchmark_v2_signing)
    if(TARGET "${unexpected_target}")
        message(FATAL_ERROR
            "subproject default unexpectedly defines ${unexpected_target}")
    endif()
endforeach()

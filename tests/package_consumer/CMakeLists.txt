cmake_minimum_required(VERSION 3.16)
project(polymarket_package_consumer LANGUAGES CXX)

find_package(polymarket_client CONFIG REQUIRED)

add_executable(polymarket_package_consumer main.cpp)
target_link_libraries(polymarket_package_consumer PRIVATE polymarket::client)

add_library(polymarket_package_plugin SHARED plugin.cpp)
target_link_libraries(polymarket_package_plugin PRIVATE polymarket::client)

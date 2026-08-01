if(POLYMARKET_CLIENT_BUILD_BENCHMARKS)
    add_executable(benchmark_v2_signing benchmarks/benchmark_v2_signing.cpp)
    target_link_libraries(benchmark_v2_signing PRIVATE polymarket::client)

    add_executable(benchmark_order_payload benchmarks/benchmark_order_payload.cpp)
    target_link_libraries(benchmark_order_payload PRIVATE polymarket::client)

    add_executable(benchmark_http_fixture benchmarks/benchmark_http_fixture.cpp)
    target_link_libraries(benchmark_http_fixture PRIVATE polymarket::client)

    add_executable(benchmark_ws_parse benchmarks/benchmark_ws_parse.cpp)
    target_include_directories(benchmark_ws_parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(benchmark_ws_parse PRIVATE polymarket::client)
endif()

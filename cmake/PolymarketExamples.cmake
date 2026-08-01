if(POLYMARKET_CLIENT_BUILD_EXAMPLES)
    add_executable(polymarket_arb src/main.cpp)
    target_link_libraries(polymarket_arb PRIVATE polymarket::client)

    add_executable(order_test
        src/order_test.cpp
        src/order_test_live.cpp
    )
    target_link_libraries(order_test PRIVATE polymarket::client)

    # Preserve the historical binary name without maintaining a second trading engine.
    add_executable(arb_test src/main.cpp)
    target_link_libraries(arb_test PRIVATE polymarket::client)

    add_executable(clob_client_test
        src/clob_activity_contract_tests.cpp
        src/clob_auth_contract_tests.cpp
        src/clob_client_test.cpp
        src/clob_credentials_contract_tests.cpp
        src/clob_mutation_contract_tests.cpp
        src/clob_order_contract_tests.cpp
        src/clob_order_numeric_contract_tests.cpp
        src/clob_pagination_contract_tests.cpp
    )
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

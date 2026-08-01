if(POLYMARKET_CLIENT_BUILD_TESTS)
    enable_testing()
    if(NOT TARGET polymarket_arb)
        add_executable(polymarket_arb src/main.cpp)
        target_link_libraries(polymarket_arb PRIVATE polymarket::client)
    endif()
    add_test(
        NAME test_arb_live_disabled
        COMMAND ${CMAKE_COMMAND}
            -DARB_EXECUTABLE=$<TARGET_FILE:polymarket_arb>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_arb_live_disabled.cmake
    )

    add_executable(test_utils tests/test_utils.cpp)
    target_link_libraries(test_utils PRIVATE polymarket::client)
    add_test(NAME test_utils COMMAND test_utils)

    add_executable(test_evm_events tests/test_evm_events.cpp)
    target_link_libraries(test_evm_events PRIVATE polymarket::client)
    add_test(NAME test_evm_events COMMAND test_evm_events)

    add_executable(test_evm_event_indexer tests/test_evm_event_indexer.cpp)
    target_link_libraries(test_evm_event_indexer PRIVATE polymarket::client)
    add_test(NAME test_evm_event_indexer COMMAND test_evm_event_indexer)

    add_executable(test_json_rpc_http_concurrency tests/test_json_rpc_http_concurrency.cpp)
    target_link_libraries(test_json_rpc_http_concurrency PRIVATE polymarket::client)
    add_test(NAME test_json_rpc_http_concurrency COMMAND test_json_rpc_http_concurrency)

    add_executable(test_json_rpc_http_integrity tests/test_json_rpc_http_integrity.cpp)
    target_link_libraries(test_json_rpc_http_integrity PRIVATE polymarket::client)
    add_test(NAME test_json_rpc_http_integrity COMMAND test_json_rpc_http_integrity)

    add_executable(test_json_rpc_ws_owner_reset tests/test_json_rpc_ws_owner_reset.cpp)
    target_include_directories(test_json_rpc_ws_owner_reset PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_json_rpc_ws_owner_reset PRIVATE polymarket::client)
    foreach(scenario log pending head error reset-throw setter-dispatch)
        add_test(
            NAME test_json_rpc_ws_owner_reset_${scenario}
            COMMAND test_json_rpc_ws_owner_reset ${scenario}
        )
    endforeach()

    add_executable(test_order_signer_v2 tests/test_order_signer_v2.cpp)
    target_link_libraries(test_order_signer_v2 PRIVATE polymarket::client)
    add_test(NAME test_order_signer_v2 COMMAND test_order_signer_v2)

    add_executable(test_http_client_transport tests/test_http_client_transport.cpp)
    target_link_libraries(test_http_client_transport PRIVATE polymarket::client)
    add_test(NAME test_http_client_transport COMMAND test_http_client_transport)

    add_executable(test_http_global_lifetime tests/test_http_global_lifetime.cpp)
    target_include_directories(test_http_global_lifetime PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_http_global_lifetime PRIVATE polymarket::client)
    add_test(NAME test_http_global_lifetime COMMAND test_http_global_lifetime)

    add_executable(test_clob_client_contracts
        src/clob_activity_contract_tests.cpp
        src/clob_auth_contract_tests.cpp
        src/clob_client_test.cpp
        src/clob_credentials_contract_tests.cpp
        src/clob_mutation_contract_tests.cpp
        src/clob_order_contract_tests.cpp
        src/clob_order_numeric_contract_tests.cpp
        src/clob_pagination_contract_tests.cpp
    )
    target_link_libraries(test_clob_client_contracts PRIVATE polymarket::client)
    add_test(NAME test_clob_client_contracts COMMAND test_clob_client_contracts)

    add_executable(test_clob_trade_contracts src/clob_trade_contract_tests.cpp)
    target_link_libraries(test_clob_trade_contracts PRIVATE polymarket::client)
    add_test(NAME test_clob_trade_contracts COMMAND test_clob_trade_contracts)

    add_executable(test_order_execution tests/test_order_execution.cpp)
    target_include_directories(test_order_execution PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_order_execution PRIVATE polymarket::client)
    add_test(NAME test_order_execution COMMAND test_order_execution)

    add_executable(test_order_type_serialization tests/test_order_type_serialization.cpp)
    target_link_libraries(test_order_type_serialization PRIVATE polymarket::client)
    add_test(NAME test_order_type_serialization COMMAND test_order_type_serialization)

    add_executable(test_prepared_order tests/test_prepared_order.cpp)
    target_link_libraries(test_prepared_order PRIVATE polymarket::client)
    add_test(NAME test_prepared_order COMMAND test_prepared_order)

    add_executable(test_sdk_error tests/test_sdk_error.cpp)
    target_link_libraries(test_sdk_error PRIVATE polymarket::client)
    add_test(NAME test_sdk_error COMMAND test_sdk_error)

    add_executable(test_websocket_resilience tests/test_websocket_resilience.cpp)
    target_include_directories(test_websocket_resilience PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_websocket_resilience PRIVATE polymarket::client)
    add_test(NAME test_websocket_resilience COMMAND test_websocket_resilience)

    add_executable(test_oracle_watcher tests/test_oracle_watcher.cpp)
    target_link_libraries(test_oracle_watcher PRIVATE polymarket::client)
    add_test(NAME test_oracle_watcher COMMAND test_oracle_watcher)

    add_executable(test_oracle_watcher_historical tests/test_oracle_watcher_historical.cpp)
    target_link_libraries(test_oracle_watcher_historical PRIVATE polymarket::client)
    add_test(NAME test_oracle_watcher_historical COMMAND test_oracle_watcher_historical)
    set_tests_properties(test_oracle_watcher_historical PROPERTIES LABELS live)

    add_executable(test_arb_sizing tests/test_arb_sizing.cpp)
    target_include_directories(test_arb_sizing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_arb_sizing PRIVATE polymarket::client)
    add_test(NAME test_arb_sizing COMMAND test_arb_sizing)

    add_executable(test_orderbook_stream tests/test_orderbook_stream.cpp)
    target_include_directories(test_orderbook_stream PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_orderbook_stream PRIVATE polymarket::client)
    add_test(NAME test_orderbook_stream COMMAND test_orderbook_stream)

    add_executable(test_orderbook_owner_reset tests/test_orderbook_owner_reset.cpp)
    target_include_directories(test_orderbook_owner_reset PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_orderbook_owner_reset PRIVATE polymarket::client)
    foreach(scenario update arb-legacy arb-snapshot arb-permit stop-update setter-dispatch)
        add_test(
            NAME test_orderbook_owner_reset_${scenario}
            COMMAND test_orderbook_owner_reset ${scenario}
        )
    endforeach()

    add_executable(test_arb_stream_admission tests/test_arb_stream_admission.cpp)
    target_include_directories(test_arb_stream_admission PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(test_arb_stream_admission PRIVATE polymarket::client)
    add_test(NAME test_arb_stream_admission COMMAND test_arb_stream_admission)

    add_executable(test_market_discovery tests/test_market_discovery.cpp)
    target_include_directories(test_market_discovery PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_market_discovery PRIVATE polymarket::client)
    add_test(NAME test_market_discovery COMMAND test_market_discovery)

    add_executable(test_market_data_numeric_contracts tests/test_market_data_numeric_contracts.cpp)
    target_include_directories(test_market_data_numeric_contracts PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_market_data_numeric_contracts PRIVATE polymarket::client)
    add_test(NAME test_market_data_numeric_contracts COMMAND test_market_data_numeric_contracts)

    add_executable(test_clob_positions tests/test_clob_positions.cpp)
    target_include_directories(test_clob_positions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_clob_positions PRIVATE polymarket::client)
    add_test(NAME test_clob_positions COMMAND test_clob_positions)

    add_test(
        NAME test_package_consumer
        COMMAND ${CMAKE_COMMAND}
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DSUBPROJECT_JSON_SOURCE_DIR=${json_SOURCE_DIR}
            -DSUBPROJECT_IXWEBSOCKET_SOURCE_DIR=${ixwebsocket_SOURCE_DIR}
            -DSUBPROJECT_SECP256K1_SOURCE_DIR=${secp256k1_SOURCE_DIR}
            -DSUBPROJECT_ETHASH_SOURCE_DIR=${ethash_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_package_consumer.cmake
    )
endif()

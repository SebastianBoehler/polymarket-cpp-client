if(POLYMARKET_CLIENT_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "POLYMARKET_CLIENT_BUILD_FUZZERS requires Clang with libFuzzer")
    endif()

    set(POLYMARKET_FUZZ_SANITIZERS
        -fsanitize=address,undefined
        -fno-sanitize-recover=undefined
        -fno-omit-frame-pointer)
    target_compile_options(polymarket_client PRIVATE
        -fsanitize=fuzzer-no-link ${POLYMARKET_FUZZ_SANITIZERS})
    target_link_options(polymarket_client INTERFACE
        $<BUILD_INTERFACE:-fsanitize=address,undefined>)
    enable_testing()
elseif(NOT POLYMARKET_CLIENT_BUILD_TESTS)
    return()
endif()

foreach(fuzz_target IN ITEMS websocket_messages rest_responses)
    if(POLYMARKET_CLIENT_BUILD_FUZZERS)
        add_executable(fuzz_${fuzz_target} tests/fuzz/fuzz_${fuzz_target}.cpp)
        target_compile_options(fuzz_${fuzz_target} PRIVATE
            -fsanitize=fuzzer ${POLYMARKET_FUZZ_SANITIZERS})
        target_link_options(fuzz_${fuzz_target} PRIVATE -fsanitize=fuzzer)
        set(fuzz_replay_args -runs=0)
    else()
        add_executable(fuzz_${fuzz_target}
            tests/fuzz/fuzz_${fuzz_target}.cpp
            tests/fuzz/fuzz_replay_main.cpp)
        set(fuzz_replay_args)
    endif()
    target_include_directories(fuzz_${fuzz_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(fuzz_${fuzz_target} PRIVATE polymarket::client)
    add_test(NAME fuzz_${fuzz_target}_corpus
        COMMAND fuzz_${fuzz_target} ${fuzz_replay_args}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/${fuzz_target})
    set_tests_properties(fuzz_${fuzz_target}_corpus PROPERTIES LABELS fuzz)
endforeach()

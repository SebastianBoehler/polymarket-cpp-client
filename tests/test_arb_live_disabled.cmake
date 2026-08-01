if(NOT DEFINED ARB_EXECUTABLE)
    message(FATAL_ERROR "ARB_EXECUTABLE is required")
endif()

execute_process(
    COMMAND "${ARB_EXECUTABLE}" --live
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
    TIMEOUT 3
)

if(result EQUAL 0)
    message(FATAL_ERROR "polymarket_arb --live unexpectedly succeeded")
endif()
if(NOT "${result}" MATCHES "^-?[0-9]+$")
    message(FATAL_ERROR "polymarket_arb --live did not reject promptly: ${result}")
endif()
set(combined "${output}${error}")
if(NOT combined MATCHES "--live is disabled because paired CLOB orders are not atomic")
    message(FATAL_ERROR "unexpected --live diagnostic: ${combined}")
endif()

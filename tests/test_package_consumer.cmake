set(install_dir "${BINARY_DIR}/package-consumer-install")
set(consumer_build_dir "${BINARY_DIR}/package-consumer-build")
set(subproject_build_dir "${BINARY_DIR}/subproject-consumer-build")

file(REMOVE_RECURSE "${install_dir}" "${consumer_build_dir}" "${subproject_build_dir}")

execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BINARY_DIR}" --prefix "${install_dir}"
    RESULT_VARIABLE install_result
)
if(NOT install_result EQUAL 0)
    message(FATAL_ERROR "installing the package failed")
endif()

file(GLOB_RECURSE installed_cmake_files
    "${install_dir}/lib/cmake/*.cmake")
foreach(installed_cmake_file IN LISTS installed_cmake_files)
    file(READ "${installed_cmake_file}" installed_cmake)
    string(FIND "${installed_cmake}" "${SOURCE_DIR}" source_path_index)
    string(FIND "${installed_cmake}" "${BINARY_DIR}" build_path_index)
    if(NOT source_path_index EQUAL -1 OR NOT build_path_index EQUAL -1 OR
       installed_cmake MATCHES "/opt/homebrew/Cellar/|MacOSX\\.sdk|/opt/hostedtoolcache/")
        message(FATAL_ERROR
            "installed package metadata contains a build-host path: ${installed_cmake_file}")
    endif()
endforeach()
if(EXISTS "${install_dir}/lib/pkgconfig/ixwebsocket.pc")
    message(FATAL_ERROR "non-relocatable IXWebSocket pkg-config metadata was installed")
endif()
foreach(required_license IN ITEMS
        polymarket-client.txt nlohmann-json.txt ixwebsocket.txt secp256k1.txt ethash.txt)
    if(NOT EXISTS "${install_dir}/share/licenses/polymarket_client/${required_license}")
        message(FATAL_ERROR "installed package is missing license: ${required_license}")
    endif()
endforeach()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}/tests/package_consumer"
        -B "${consumer_build_dir}"
        -DCMAKE_PREFIX_PATH=${install_dir}
    RESULT_VARIABLE configure_result
)
if(NOT configure_result EQUAL 0)
    message(FATAL_ERROR "configuring the installed-package consumer failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${consumer_build_dir}" --parallel
    RESULT_VARIABLE build_result
)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "building the installed-package consumer failed")
endif()

execute_process(
    COMMAND "${consumer_build_dir}/polymarket_package_consumer"
    RESULT_VARIABLE run_result
)
if(NOT run_result EQUAL 0)
    message(FATAL_ERROR "running the installed-package consumer failed")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}"
        -S "${SOURCE_DIR}/tests/subproject_consumer"
        -B "${subproject_build_dir}"
        -DPOLYMARKET_CLIENT_SOURCE_DIR=${SOURCE_DIR}
        -DFETCHCONTENT_SOURCE_DIR_JSON=${SUBPROJECT_JSON_SOURCE_DIR}
        -DFETCHCONTENT_SOURCE_DIR_IXWEBSOCKET=${SUBPROJECT_IXWEBSOCKET_SOURCE_DIR}
        -DFETCHCONTENT_SOURCE_DIR_SECP256K1=${SUBPROJECT_SECP256K1_SOURCE_DIR}
        -DFETCHCONTENT_SOURCE_DIR_ETHASH=${SUBPROJECT_ETHASH_SOURCE_DIR}
    RESULT_VARIABLE subproject_configure_result
)
if(NOT subproject_configure_result EQUAL 0)
    message(FATAL_ERROR "configuring as a quiet subproject failed")
endif()

cmake_minimum_required(VERSION 3.20)
project(teez-core VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TEEZ_CORE_IS_TOPLEVEL OFF)
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(TEEZ_CORE_IS_TOPLEVEL ON)
endif()

include(cmake/Dependencies.cmake)
include(cmake/CoditaryResolveUtils.cmake)
include(cmake/CoditaryResolveFs.cmake)
include(cmake/CoditaryResolveData.cmake)
include(cmake/CoditaryResolveLua.cmake)
include(cmake/Coverage.cmake)

coditary_resolve_utils()
coditary_resolve_fs()
coditary_resolve_data()
coditary_resolve_lua()

add_library(teez_core
    src/compression.cpp
    src/config.cpp
    src/context.cpp
    src/coverage.cpp
    src/coverage_cobertura.cpp
    src/coverage_lcov.cpp
    src/coverage_lua.cpp
    src/coverage_msgpack.cpp
    src/coverage_program.cpp
    src/coverage_reporter.cpp
    src/discovery.cpp
    src/exit_code.cpp
    src/lua_helpers.cpp
    src/lua_json.cpp
    src/plugin.cpp
    src/plugin_config.cpp
    src/process.cpp
    src/runner.cpp
    src/runner_config.cpp
    src/signals.cpp
    src/teez_config.cpp
    src/test_filter.cpp
    src/test_report.cpp
    src/test_codec.cpp
    src/test_codec_json.cpp
    src/test_codec_junit.cpp
    src/test_codec_msgpack.cpp
    src/test_reporter.cpp
)

target_include_directories(teez_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(teez_core
    PUBLIC
        coditary_utils
        coditary_fs
        coditary_data
        coditary_lua
        nlohmann_json::nlohmann_json
        sol2::sol2
        ${LUA_LIBRARIES}
    PRIVATE
        libzstd_static
)

find_library(TEEZ_UTIL_LIB util)
if(TEEZ_UTIL_LIB)
    target_link_libraries(teez_core PUBLIC ${TEEZ_UTIL_LIB})
endif()

target_compile_definitions(teez_core
    PUBLIC
        TEEZ_PLUGIN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/plugins"
)

if(TEEZ_CORE_IS_TOPLEVEL)
    enable_testing()
    include(cmake/TestDependencies.cmake)

    set(TEEZ_CORE_TEST_SOURCES
        tests/test_ctest_integration.cpp
        tests/test_ctest_plugin.cpp
        tests/test_coverage.cpp
        tests/test_coverage_reporter.cpp
        tests/test_coverage_reporter_extended.cpp
        tests/test_coverage_msgpack.cpp
        tests/test_coverage_program.cpp
        tests/test_coverage_program_extended.cpp
        tests/test_coverage_lua_extended.cpp
        tests/test_compression.cpp
        tests/test_discovery.cpp
        tests/test_exit_code.cpp
        tests/test_filesystem.cpp
        tests/test_lua_helpers.cpp
        tests/test_lua_json.cpp
        tests/test_list_context.cpp
        tests/test_plugin.cpp
        tests/test_plugin_config.cpp
        tests/test_process.cpp
        tests/test_process_extended.cpp
        tests/test_pytest_integration.cpp
        tests/test_runner.cpp
        tests/test_runner_extended.cpp
        tests/test_runner_coverage.cpp
        tests/test_runner_config.cpp
        tests/test_signals.cpp
        tests/test_teez_config.cpp
        tests/test_filter.cpp
        tests/test_filter_extended.cpp
        tests/test_test_report.cpp
        tests/test_test_report_extended.cpp
        tests/test_test_report_msgpack_extended.cpp
        tests/test_test_codec.cpp
        tests/test_codec_extended.cpp
        tests/test_test_reporter.cpp
        tests/test_harness_discovery.cpp
    )

    add_executable(teez_core_tests ${TEEZ_CORE_TEST_SOURCES})

    target_link_libraries(teez_core_tests
        PRIVATE
            teez_core
            Catch2::Catch2WithMain
    )

    include(cmake/TeezEmbedWorker.cmake)
    teez_embed_worker(teez_core_tests)

    if(TEEZ_HAS_EMBEDDED_WORKER)
        target_sources(teez_core_tests PRIVATE
            tests/test_worker_integration.cpp
            tests/test_runner_integration.cpp
        )
    endif()

    catch_discover_tests(teez_core_tests)
    teez_apply_gcov_prebuild_cleanup()
    teez_add_coverage_target(coverage "teez-core")

    include(cmake/Fuzz.cmake)
    teez_add_fuzz_target(fuzz_core
        CORPUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus"
        SOURCES
            fuzz/fuzz_core.cpp
        LINK_LIBRARIES
            teez_core
    )
    if(TARGET fuzz_core)
        target_include_directories(fuzz_core
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/fuzz
        )
    endif()
endif()

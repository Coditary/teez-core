option(TEEZ_ENABLE_FUZZ "Build libFuzzer targets (requires Clang)" OFF)

if(TEEZ_ENABLE_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "${PROJECT_NAME}: TEEZ_ENABLE_FUZZ requires Clang; fuzz targets disabled")
        set(TEEZ_ENABLE_FUZZ OFF)
    endif()
endif()

function(teez_add_fuzz_target target_name)
    if(NOT TEEZ_ENABLE_FUZZ)
        return()
    endif()

    set(options "")
    set(oneValueArgs CORPUS_DIR)
    set(multiValueArgs SOURCES LINK_LIBRARIES COMPILE_DEFINITIONS)
    cmake_parse_arguments(FUZZ "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(FUZZ_UNPARSED_ARGUMENTS)
        list(APPEND FUZZ_SOURCES ${FUZZ_UNPARSED_ARGUMENTS})
    endif()

    add_executable(${target_name} ${FUZZ_SOURCES})
    target_compile_options(${target_name} PRIVATE -fsanitize=fuzzer,address -g -O1)
    target_link_options(${target_name} PRIVATE -fsanitize=fuzzer,address)

    if(FUZZ_LINK_LIBRARIES)
        target_link_libraries(${target_name} PRIVATE ${FUZZ_LINK_LIBRARIES})
    endif()

    if(FUZZ_COMPILE_DEFINITIONS)
        target_compile_definitions(${target_name} PRIVATE ${FUZZ_COMPILE_DEFINITIONS})
    endif()

    if(FUZZ_CORPUS_DIR AND EXISTS "${FUZZ_CORPUS_DIR}")
        target_compile_definitions(${target_name}
            PRIVATE
                TEEZ_FUZZ_CORPUS_DIR="${FUZZ_CORPUS_DIR}"
        )
    endif()
endfunction()

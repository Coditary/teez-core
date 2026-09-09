# Resolves the coditary_utils library (shared Coditary C++ utilities).
#
# Usage:
#   include(CoditaryResolveUtils)
#   coditary_resolve_utils()
#   target_link_libraries(my_target PRIVATE coditary_utils)
#
# Override search path:
#   cmake -DCODITARY_UTILS_DIR=/path/to/Shared-Cpp/coditary_utils ...

function(coditary_resolve_utils)
    if(TARGET coditary_utils)
        return()
    endif()

    if(NOT CODITARY_UTILS_DIR)
        set(_utils_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../Coditary/shared/Shared-Cpp/coditary_utils"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Shared-Cpp/coditary_utils"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Shared-Cpp/coditary_utils"
        )
        foreach(candidate IN LISTS _utils_candidates)
            if(EXISTS "${candidate}/CMakeLists.txt")
                set(CODITARY_UTILS_DIR "${candidate}" CACHE PATH "Path to coditary_utils" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT CODITARY_UTILS_DIR OR NOT EXISTS "${CODITARY_UTILS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "coditary_utils not found. Set -DCODITARY_UTILS_DIR=... to Shared-Cpp/coditary_utils")
    endif()

    add_subdirectory("${CODITARY_UTILS_DIR}" "${CMAKE_BINARY_DIR}/_deps/coditary_utils")
endfunction()

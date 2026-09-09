function(coditary_resolve_data)
    if(TARGET coditary_data)
        return()
    endif()

    if(NOT CODITARY_DATA_DIR)
        set(_data_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/Shared-Cpp/coditary_data"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../Coditary/shared/Shared-Cpp/coditary_data"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Shared-Cpp/coditary_data"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Shared-Cpp/coditary_data"
        )
        foreach(candidate IN LISTS _data_candidates)
            if(EXISTS "${candidate}/CMakeLists.txt")
                set(CODITARY_DATA_DIR "${candidate}" CACHE PATH "Path to coditary_data" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT CODITARY_DATA_DIR OR NOT EXISTS "${CODITARY_DATA_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "coditary_data not found. Set -DCODITARY_DATA_DIR=... to Shared-Cpp/coditary_data")
    endif()

    add_subdirectory("${CODITARY_DATA_DIR}" "${CMAKE_BINARY_DIR}/_deps/coditary_data")
endfunction()

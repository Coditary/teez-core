function(coditary_resolve_fs)
    if(TARGET coditary_fs)
        return()
    endif()

    if(NOT CODITARY_FS_DIR)
        set(_fs_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/Shared-Cpp/coditary_fs"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../Coditary/shared/Shared-Cpp/coditary_fs"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Shared-Cpp/coditary_fs"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Shared-Cpp/coditary_fs"
        )
        foreach(candidate IN LISTS _fs_candidates)
            if(EXISTS "${candidate}/CMakeLists.txt")
                set(CODITARY_FS_DIR "${candidate}" CACHE PATH "Path to coditary_fs" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT CODITARY_FS_DIR OR NOT EXISTS "${CODITARY_FS_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "coditary_fs not found. Set -DCODITARY_FS_DIR=... to Shared-Cpp/coditary_fs")
    endif()

    add_subdirectory("${CODITARY_FS_DIR}" "${CMAKE_BINARY_DIR}/_deps/coditary_fs")
endfunction()

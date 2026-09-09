function(coditary_resolve_lua)
    if(TARGET coditary_lua)
        return()
    endif()

    if(NOT CODITARY_LUA_DIR)
        set(_lua_candidates
            "${CMAKE_CURRENT_SOURCE_DIR}/Shared-Cpp/coditary_lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../Coditary/shared/Shared-Cpp/coditary_lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/Shared-Cpp/coditary_lua"
            "${CMAKE_CURRENT_SOURCE_DIR}/../Shared-Cpp/coditary_lua"
        )
        foreach(candidate IN LISTS _lua_candidates)
            if(EXISTS "${candidate}/CMakeLists.txt")
                set(CODITARY_LUA_DIR "${candidate}" CACHE PATH "Path to coditary_lua" FORCE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT CODITARY_LUA_DIR OR NOT EXISTS "${CODITARY_LUA_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "coditary_lua not found. Set -DCODITARY_LUA_DIR=... to Shared-Cpp/coditary_lua")
    endif()

    add_subdirectory("${CODITARY_LUA_DIR}" "${CMAKE_BINARY_DIR}/_deps/coditary_lua")
endfunction()

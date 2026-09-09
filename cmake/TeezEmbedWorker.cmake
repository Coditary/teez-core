# Links teez_worker_lib into a target when teez-worker is available as a sibling project.
get_filename_component(_TEEZ_EMBED_WORKER_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
get_filename_component(_TEEZ_EMBED_WORKER_PLUGIN_DIR "${_TEEZ_EMBED_WORKER_MODULE_DIR}/../plugins" ABSOLUTE)

function(teez_embed_worker target)
    if(TARGET teez_worker_lib)
    target_link_libraries(${target} PRIVATE teez_worker_lib)
    target_compile_definitions(${target} PRIVATE
        TEEZ_EMBEDDED_WORKER=1
        TEEZ_PLUGIN_DIR="${_TEEZ_EMBED_WORKER_PLUGIN_DIR}"
    )
    set(TEEZ_HAS_EMBEDDED_WORKER TRUE PARENT_SCOPE)
    return()
    endif()

    set(_teez_worker_dir "${TEEZ_EMBED_WORKER_DIR}")
    if(NOT _teez_worker_dir)
        set(_teez_worker_dir "${_TEEZ_EMBED_WORKER_MODULE_DIR}/../../teez-worker")
    endif()

    if(NOT EXISTS "${_teez_worker_dir}/CMakeLists.txt")
        set(TEEZ_HAS_EMBEDDED_WORKER FALSE PARENT_SCOPE)
        return()
    endif()

    if(NOT TARGET teez_worker_lib)
        add_subdirectory("${_teez_worker_dir}" teez-worker-embed EXCLUDE_FROM_ALL)
    endif()

    if(NOT TARGET teez_worker_lib)
        set(TEEZ_HAS_EMBEDDED_WORKER FALSE PARENT_SCOPE)
        return()
    endif()

    target_link_libraries(${target} PRIVATE teez_worker_lib)
    target_compile_definitions(${target} PRIVATE
        TEEZ_EMBEDDED_WORKER=1
        TEEZ_PLUGIN_DIR="${_TEEZ_EMBED_WORKER_PLUGIN_DIR}"
    )
    set(TEEZ_HAS_EMBEDDED_WORKER TRUE PARENT_SCOPE)
endfunction()

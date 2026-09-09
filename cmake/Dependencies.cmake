include(FetchContent)

# --- Lua 5.4 (no upstream CMake; build static lib manually) ---
FetchContent_Declare(
    lua_src
    URL https://www.lua.org/ftp/lua-5.4.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lua_src)

file(GLOB LUA_SOURCES ${lua_src_SOURCE_DIR}/src/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac)\\.c$")

add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_src_SOURCE_DIR}/src)
if(UNIX AND NOT APPLE)
    target_link_libraries(lua_static PUBLIC m dl)
endif()
set(LUA_LIBRARIES lua_static)

# --- sol2 ---
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG        v3.5.0
    GIT_SHALLOW    TRUE
)
set(SOL2_LUA_VERSION "5.4" CACHE STRING "" FORCE)
set(SOL2_BUILD_LUA FALSE CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(sol2)

# --- nlohmann/json ---
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(json)

# --- zstd ---
FetchContent_Declare(
    zstd
    GIT_REPOSITORY https://github.com/facebook/zstd.git
    GIT_TAG        v1.5.6
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  build/cmake
)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zstd)

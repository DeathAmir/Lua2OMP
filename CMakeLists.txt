cmake_minimum_required(VERSION 3.20)
project(Lua2OMP VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

FetchContent_Declare(
    omp_sdk
    GIT_REPOSITORY https://github.com/openmultiplayer/open.mp-sdk.git
    GIT_TAG 08cc69a32d96c0eb8fe063d26c78259e54ca853f
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(omp_sdk)

FetchContent_Declare(
    lua51
    URL https://www.lua.org/ftp/lua-5.1.5.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(lua51)
if(NOT lua51_POPULATED)
    FetchContent_Populate(lua51)
endif()

set(LUA51_SRC ${lua51_SOURCE_DIR}/src)
set(LUA51_CORE
    lapi.c lcode.c ldebug.c ldo.c ldump.c lfunc.c lgc.c llex.c lmem.c
    lobject.c lopcodes.c lparser.c lstate.c lstring.c ltable.c ltm.c
    lundump.c lvm.c lzio.c lauxlib.c lbaselib.c ldblib.c liolib.c
    lmathlib.c loslib.c ltablib.c lstrlib.c loadlib.c linit.c
)
list(TRANSFORM LUA51_CORE PREPEND "${LUA51_SRC}/")
add_library(lua51 STATIC ${LUA51_CORE})
target_include_directories(lua51 PUBLIC ${LUA51_SRC})
if(WIN32)
    target_compile_definitions(lua51 PRIVATE LUA_USE_WINDOWS)
endif()

add_executable(luac ${LUA51_SRC}/luac.c ${LUA51_SRC}/print.c)
target_link_libraries(luac PRIVATE lua51)
target_include_directories(luac PRIVATE ${LUA51_SRC})

add_library(Lua2OMP SHARED src/component.cpp)
target_link_libraries(Lua2OMP PRIVATE OMP-SDK lua51)
target_include_directories(Lua2OMP PRIVATE ${LUA51_SRC})
target_compile_definitions(Lua2OMP PRIVATE OMP_VERSION_MAJOR=1 OMP_VERSION_MINOR=0 OMP_VERSION_PATCH=0)
set_target_properties(Lua2OMP PROPERTIES PREFIX "" OUTPUT_NAME "Lua2OMP")

if(MSVC)
    target_compile_options(Lua2OMP PRIVATE /W4 /permissive-)
else()
    target_compile_options(Lua2OMP PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS Lua2OMP luac
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
    ARCHIVE DESTINATION .
)

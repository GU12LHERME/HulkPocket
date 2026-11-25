cmake_minimum_required(VERSION 3.24)
project(HulkPocketXbeRecompilerHost LANGUAGES CXX)

# Build the REAL upstream XboxXBERecomp static recompiler without
# linking any Windows-only graphics, audio or Xbox host runtime.
# It performs source generation only and contains no game files.
if(NOT DEFINED XBE_SDK_DIR)
    message(FATAL_ERROR "Set -DXBE_SDK_DIR=/path/to/XboxXBERecomp")
endif()
if(NOT EXISTS "${XBE_SDK_DIR}/tools/recomp/main.cpp")
    message(FATAL_ERROR "XBE_SDK_DIR must point to the upstream SDK")
endif()

include(FetchContent)
set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG v4.1.1 GIT_SHALLOW TRUE)
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(zydis tomlplusplus)

file(GLOB XBE_RECOMP_SOURCES CONFIGURE_DEPENDS
    "${XBE_SDK_DIR}/tools/recomp/*.cpp")
add_executable(xbe_recomp_host ${XBE_RECOMP_SOURCES})
set_target_properties(xbe_recomp_host PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(xbe_recomp_host PRIVATE
    "${XBE_SDK_DIR}/runtime"
    "${XBE_SDK_DIR}/tools/recomp")
target_compile_definitions(xbe_recomp_host PRIVATE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(xbe_recomp_host PRIVATE Zydis tomlplusplus::tomlplusplus)

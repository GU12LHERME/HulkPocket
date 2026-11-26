cmake_minimum_required(VERSION 3.22.1)
project(hulkpocket_bridge LANGUAGES CXX)
add_library(hulkpocket_bridge SHARED bridge.cpp)
target_compile_features(hulkpocket_bridge PRIVATE cxx_std_17)
target_compile_options(hulkpocket_bridge PRIVATE -Wall -Wextra -Werror)

find_library(android_log_lib log REQUIRED)
target_link_libraries(hulkpocket_bridge PRIVATE ${android_log_lib})

get_filename_component(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
add_library(hulkpocket_preview SHARED preview.cpp
    "${ROOT}/native/hulk_xbe_preflight.cpp"
    "${ROOT}/native/xbox_guest_address_space.cpp"
    "${ROOT}/native/xbe_image_memory_loader.cpp"
    "${ROOT}/native/xbe_kernel_imports.cpp"
    "${ROOT}/native/portable_xbe_image.cpp"
    "${ROOT}/native/portable_xbe_dispatch.cpp"
    "${ROOT}/native/xbe_gles_backend.cpp"
    "${ROOT}/third_party/xboxxberecomp/kernel/crypto.cpp"
    "${ROOT}/third_party/xboxxberecomp/gpu/texture.cpp"
    "${ROOT}/third_party/xboxxberecomp/gpu/vertex.cpp"
)
target_compile_features(hulkpocket_preview PRIVATE cxx_std_20)
target_compile_definitions(hulkpocket_preview PRIVATE HULK_ENABLE_ENTRY_DIAGNOSTIC HULK_ENABLE_GLES_DEVICE_PROBE HULK_ANDROID_PREVIEW)
target_include_directories(hulkpocket_preview PRIVATE "${ROOT}/native" "${ROOT}/third_party/xboxxberecomp" "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
target_link_libraries(hulkpocket_preview PRIVATE android log dl EGL GLESv3)

# Experimental renderer is compiled only when explicitly enabled by Gradle.
option(HULK_ENABLE_VULKAN_PREVIEW "Build Vulkan preview alongside GLES" OFF)
if(HULK_ENABLE_VULKAN_PREVIEW)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
    set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    FetchContent_Declare(glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git GIT_TAG 16.0.0 GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(glslang)
    target_sources(hulkpocket_preview PRIVATE "${ROOT}/third_party/xboxxberecomp/gpu/shaders.cpp")
    target_compile_definitions(hulkpocket_preview PRIVATE HULK_ENABLE_VULKAN_PREVIEW ENABLE_HLSL)
    target_include_directories(hulkpocket_preview PRIVATE "${ROOT}/third_party/scda-vulkan/src")
    target_link_libraries(hulkpocket_preview PRIVATE vulkan glslang glslang-default-resource-limits)
endif()

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

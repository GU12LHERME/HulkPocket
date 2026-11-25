cmake_minimum_required(VERSION 3.22.1)
project(hulkpocket_bridge LANGUAGES CXX)
add_library(hulkpocket_bridge SHARED bridge.cpp)
target_compile_features(hulkpocket_bridge PRIVATE cxx_std_17)
target_compile_options(hulkpocket_bridge PRIVATE -Wall -Wextra -Werror)

find_library(android_log_lib log REQUIRED)
target_link_libraries(hulkpocket_bridge PRIVATE ${android_log_lib})

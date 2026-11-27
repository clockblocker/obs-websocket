cmake_minimum_required(VERSION 3.23)

project(obs-websocket-protocol LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(nlohmann_json 3.11 REQUIRED)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/reflect-cpp.cmake")

enable_testing()

add_executable(obs-ws-protocol-smoke smoke_test.cpp)
target_include_directories(obs-ws-protocol-smoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(obs-ws-protocol-smoke PRIVATE nlohmann_json::nlohmann_json reflectcpp::reflectcpp)
target_compile_options(obs-ws-protocol-smoke PRIVATE -Wall -Wextra -Werror)

add_test(NAME obs-ws-protocol-smoke COMMAND obs-ws-protocol-smoke)

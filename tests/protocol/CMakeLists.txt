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

add_executable(obs-ws-schema-gen schema_gen.cpp)
target_include_directories(obs-ws-schema-gen PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_link_libraries(obs-ws-schema-gen PRIVATE nlohmann_json::nlohmann_json reflectcpp::reflectcpp)
target_compile_options(obs-ws-schema-gen PRIVATE -Wall -Wextra -Werror)
add_test(NAME obs-ws-schema-gen-summary COMMAND obs-ws-schema-gen --summary)

add_executable(obs-ws-protocol-test protocol_test.cpp)
target_include_directories(obs-ws-protocol-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../src")
target_compile_definitions(
  obs-ws-protocol-test
  PRIVATE OBS_WEBSOCKET_PROTOCOL_JSON="${CMAKE_CURRENT_SOURCE_DIR}/../../docs/generated/protocol.json")
target_link_libraries(obs-ws-protocol-test PRIVATE nlohmann_json::nlohmann_json reflectcpp::reflectcpp)
target_compile_options(obs-ws-protocol-test PRIVATE -Wall -Wextra -Werror)
add_test(NAME obs-ws-protocol-test COMMAND obs-ws-protocol-test)

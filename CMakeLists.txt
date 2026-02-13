cmake_minimum_required(VERSION 3.20)
project(Phosphor
    VERSION 0.2.0
    LANGUAGES CXX
    DESCRIPTION "Multi-system emulator in C++23"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

file(GLOB_RECURSE COMMON_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/common/src/*.cpp"
)
file(GLOB_RECURSE COMMON_HDR CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/common/include/*.hpp"
)

file(GLOB_RECURSE GAMEBOY_SRC CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/cores/gameboy/src/*.cpp"
)
file(GLOB_RECURSE GAMEBOY_HDR CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/cores/gameboy/include/*.hpp"
)

add_executable(${PROJECT_NAME}
    ${COMMON_SRC} ${COMMON_HDR}
    ${GAMEBOY_SRC} ${GAMEBOY_HDR}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/common/include
    ${CMAKE_SOURCE_DIR}/cores/gameboy/include
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:DEBUG_BUILD>
    $<$<CONFIG:Release>:NDEBUG>
)

find_package(SDL2 CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>
)

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")

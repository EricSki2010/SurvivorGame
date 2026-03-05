cmake_minimum_required(VERSION 3.20)
project(2D_survivalGame LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

file(GLOB_RECURSE SOURCES src/*.cpp)

add_executable(game ${SOURCES})

target_include_directories(game PRIVATE
    src/core
    src/entities
    src/world
    src/world/terrain
    src/world/rivers
    src/third_party
)

find_package(raylib CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
target_link_libraries(game PRIVATE raylib glfw opengl32 gdi32 winmm)

# Static link MSVC runtime so no DLLs needed
set_property(TARGET game PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

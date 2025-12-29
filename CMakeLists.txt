cmake_minimum_required(VERSION 3.20)
project(dmwss VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Optimization flags
if(MSVC)
    add_compile_options(/W4 /O2 /Oi /Ot /GL /arch:AVX2)
    add_link_options(/LTCG)
else()
    add_compile_options(-Wall -Wextra -pedantic -O3 -march=native -flto)
    add_link_options(-flto)
endif()

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets)

# Enable Qt MOC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/glfw)

# GLAD - We'll use Qt's OpenGL functions instead for now
# Since we're using Qt OpenGLWidgets, we don't need GLAD immediately
# TODO: Add proper GLAD integration later if needed

# fmt library
add_subdirectory(modules/fmt)

# spdlog - configure to use external fmt
set(SPDLOG_FMT_EXTERNAL ON CACHE BOOL "" FORCE)
add_subdirectory(modules/spdlog)

# nlohmann JSON
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
add_subdirectory(modules/nlohmann-json)

# Source files
file(GLOB_RECURSE SOURCES
    "src/*.cpp"
    "src/*.hpp"
)

add_executable(dmwss ${SOURCES})

target_include_directories(dmwss PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/modules/miniaudio
    ${CMAKE_SOURCE_DIR}/modules/opengl
)

target_link_libraries(dmwss PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGLWidgets
    glfw
    fmt::fmt
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Linux-specific Qt6 dependencies
if(UNIX AND NOT APPLE)
    target_link_libraries(dmwss PRIVATE
        pcre2-16
        icuuc
        icui18n
    )
endif()

# Copy shaders to build directory
add_custom_command(TARGET dmwss POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:dmwss>/shaders
)

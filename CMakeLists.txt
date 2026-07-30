cmake_minimum_required(VERSION 3.20)
project(dmwss VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Host-CPU tuning is great for local builds but produces binaries that
# crash on older machines; release packaging turns it off
option(DMWSS_NATIVE "Optimize for the host CPU (disable for distributable builds)" ON)

# Optimization flags
if(MSVC)
    add_compile_options(/W4 /O2 /Oi /Ot /GL)
    if(DMWSS_NATIVE)
        add_compile_options(/arch:AVX2)
    endif()
    add_link_options(/LTCG)
else()
    add_compile_options(-Wall -Wextra -pedantic -O3 -flto)
    if(DMWSS_NATIVE)
        add_compile_options(-march=native)
    endif()
    add_link_options(-flto)
endif()

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Widgets OpenGLWidgets Network)

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

# Core emulation library (platform-agnostic, no Qt dependency)
file(GLOB_RECURSE CORE_SOURCES
    "src/core/*.cpp"
    "src/core/*.hpp"
    "src/machine/*.cpp"
    "src/machine/*.hpp"
)

add_library(dmwss_core STATIC ${CORE_SOURCES})

target_include_directories(dmwss_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dmwss_core PUBLIC
    fmt::fmt
    spdlog::spdlog
)

# GUI application
file(GLOB_RECURSE UI_SOURCES
    "src/ui/*.cpp"
    "src/ui/*.hpp"
    "src/platform/*.cpp"
    "src/platform/*.hpp"
)

add_executable(dmwss src/main.cpp ${UI_SOURCES})

# Package as a proper .app bundle on macOS so macdeployqt can produce a
# self-contained, double-clickable application
if(APPLE)
    set_target_properties(dmwss PROPERTIES MACOSX_BUNDLE TRUE)
endif()

target_include_directories(dmwss PRIVATE
    ${CMAKE_SOURCE_DIR}/modules/miniaudio
    ${CMAKE_SOURCE_DIR}/modules/opengl
)

target_link_libraries(dmwss PRIVATE
    dmwss_core
    Qt6::Core
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Network
    glfw
    nlohmann_json::nlohmann_json
    ${CMAKE_DL_LIBS}  # miniaudio loads backends at runtime
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

# Tests - headless blargg test ROM runner
enable_testing()

add_executable(blargg_runner tests/blargg_runner.cpp)
target_link_libraries(blargg_runner PRIVATE dmwss_core)

# Register test ROMs with CTest if the (untracked) test-roms directory exists.
# The oam_bug single ROM 7-timing_effect is excluded: its text output
# (~19 OAM dumps) overflows the test framework's 8KB output buffer at
# 0xA004 and destroys its own code in WRAM, so the single ROM cannot pass
# (binjgb fails it the same way). The combined oam_bug.gb runs the same
# test with a working layout and passes, so it covers test 7.
set(TEST_ROM_DIR ${CMAKE_SOURCE_DIR}/test-roms/gb-test-roms)
if(EXISTS ${TEST_ROM_DIR})
    file(GLOB BLARGG_ROMS
        "${TEST_ROM_DIR}/cpu_instrs/individual/*.gb"
        "${TEST_ROM_DIR}/instr_timing/instr_timing.gb"
        "${TEST_ROM_DIR}/mem_timing/individual/*.gb"
        "${TEST_ROM_DIR}/mem_timing-2/rom_singles/*.gb"
        "${TEST_ROM_DIR}/halt_bug.gb"
        "${TEST_ROM_DIR}/interrupt_time/interrupt_time.gb"
        "${TEST_ROM_DIR}/oam_bug/rom_singles/1-lcd_sync.gb"
        "${TEST_ROM_DIR}/oam_bug/rom_singles/2-causes.gb"
        "${TEST_ROM_DIR}/oam_bug/rom_singles/3-non_causes.gb"
        "${TEST_ROM_DIR}/oam_bug/rom_singles/4-scanline_timing.gb"
        "${TEST_ROM_DIR}/oam_bug/rom_singles/5-timing_bug.gb"
        "${TEST_ROM_DIR}/oam_bug/rom_singles/6-timing_no_bug.gb"
        "${TEST_ROM_DIR}/oam_bug/rom_singles/8-instr_effect.gb"
        "${TEST_ROM_DIR}/oam_bug/oam_bug.gb"
        "${TEST_ROM_DIR}/dmg_sound/rom_singles/*.gb"
        "${TEST_ROM_DIR}/dmg_sound/dmg_sound.gb"
    )
    foreach(ROM ${BLARGG_ROMS})
        file(RELATIVE_PATH TEST_NAME ${TEST_ROM_DIR} ${ROM})
        add_test(NAME "blargg/${TEST_NAME}" COMMAND blargg_runner ${ROM} 7200)
        set_tests_properties("blargg/${TEST_NAME}" PROPERTIES TIMEOUT 120)
    endforeach()
endif()

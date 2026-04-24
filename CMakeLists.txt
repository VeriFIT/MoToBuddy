cmake_minimum_required(VERSION 3.12)
project(buddy
  VERSION 2.4.0
  DESCRIPTION "BuDDy: A BDD Package"
  HOMEPAGE_URL "https://ssoelvsten.github.io/buddy/"
  LANGUAGES C CXX
)
cmake_policy(SET CMP0077 NEW)

# ============================================================================ #
# Settings
# ============================================================================ #

message(STATUS "")

message(STATUS "CMake build configuration for BuDDy ${PROJECT_VERSION}")

message(STATUS "  OS:                        ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Compiler:                  ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(VERBOSE " |  C Flags:                ${CMAKE_C_FLAGS}")
message(VERBOSE " |  CXX Flags:              ${CMAKE_CXX_FLAGS}")
message(VERBOSE " |  EXE Linker Flags:       ${CMAKE_EXE_LINKER_FLAGS}")

message(STATUS "  Library Options:")

option(BUDDY_STATS "Enable statistics for ${PROJECT_NAME}" OFF)
message(STATUS "  | Statistics:              ${BUDDY_STATS}")

option(BUDDY_DISABLE_WARNINGS "Disable warnings from ${PROJECT_NAME}" ON)
message(STATUS "  | Disable Warnings:        ${BUDDY_DISABLE_WARNINGS}")

option(BUDDY_EXAMPLES "Build BuDDy Examples" ON)
message(STATUS "  | Build Examples:          ${BUDDY_EXAMPLES}")

message(STATUS "")

# ============================================================================ #
# External dependencies
# ============================================================================ #

include(FetchContent)

FetchContent_Declare(
    nlohmann_json_dl
    URL          https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
    DOWNLOAD_NO_EXTRACT TRUE
    DOWNLOAD_DIR ${CMAKE_BINARY_DIR}/include/nlohmann
)
FetchContent_MakeAvailable(nlohmann_json_dl)

add_library(nlohmann_json_iface INTERFACE)
add_library(nlohmann_json::nlohmann_json ALIAS nlohmann_json_iface)
target_include_directories(nlohmann_json_iface INTERFACE
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/include>
)

# ============================================================================ #
# Library Source
# ============================================================================ #
add_subdirectory(src)

# ============================================================================ #
# Examples
# ============================================================================ #
if(BUDDY_EXAMPLES)
  add_subdirectory(examples)
endif()
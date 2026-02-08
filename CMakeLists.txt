cmake_minimum_required(VERSION 3.10)
project(numakit VERSION 1.0.0 LANGUAGES C)

# ------------------------------------------------------------------------------
# Output Directory Configuration
# ------------------------------------------------------------------------------
# Force all binaries to go to ./build/bin and ./build/lib
# This makes them easy to find, regardless of Release/Debug mode.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ------------------------------------------------------------------------------
# Build Type Configuration
# ------------------------------------------------------------------------------
# Default to "Debug" (Development Mode) if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type selected, defaulting to Debug (Development Mode)")
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build." FORCE)
endif()

# ------------------------------------------------------------------------------
# Option 1: Development Mode (Export Compile Commands)
# ------------------------------------------------------------------------------
# If we are in Debug or RelWithDebInfo, generate the JSON for tooling (clangd, vscode)
if(CMAKE_BUILD_TYPE MATCHES "^(Debug|RelWithDebInfo)$")
    message(STATUS "Development Mode detected: Enabling compile_commands.json generation")
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile commands" FORCE)
endif()

# ------------------------------------------------------------------------------
# Standard Options
# ------------------------------------------------------------------------------
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_BENCHMARKS "Build benchmarks" ON)
option(BUILD_SHARED_LIBS "Build shared library" ON)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
pkg_check_modules(NUMA REQUIRED IMPORTED_TARGET numa)

# ------------------------------------------------------------------------------
# Library Target
# ------------------------------------------------------------------------------
file(GLOB_RECURSE SOURCES "src/*.c")

add_library(numakit ${SOURCES})

target_include_directories(numakit 
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        src
)

target_link_libraries(numakit 
    PRIVATE 
        Threads::Threads
        PkgConfig::HWLOC
        PkgConfig::NUMA
)

# ------------------------------------------------------------------------------
# Option 2: Compiler Flags & Release Optimizations
# ------------------------------------------------------------------------------
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    # Base flags for all modes (Warnings are good for everyone)
    target_compile_options(numakit PRIVATE -Wall -Wextra)

    # --- RELEASE OPTIMIZATIONS ---
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        message(STATUS "Release Mode detected: Applying aggressive optimizations")

        # 1. Architecture Tuning (Instructions specific to THIS CPU)
        target_compile_options(numakit PRIVATE -O3 -march=native)

        # 2. Link Time Optimization (LTO)
        # This allows the compiler to inline functions across different .c files
        include(CheckIPOSupported)
        check_ipo_supported(RESULT lto_supported OUTPUT error)

        if(lto_supported)
            message(STATUS "  -> LTO/IPO is supported and enabled.")
            set_property(TARGET numakit PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(WARNING "  -> LTO/IPO is NOT supported: ${error}")
        endif()

    # --- DEBUG/DEV FLAGS ---
    else()
        # Ensure we have debug symbols and no heavy optimization confusing the debugger
        target_compile_options(numakit PRIVATE -O0 -g)
    endif()
endif()

# ------------------------------------------------------------------------------
# Installation Rules
# ------------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS numakit
    EXPORT numakitTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/numakit DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT numakitTargets
    FILE numakit-config.cmake
    NAMESPACE numakit::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/numakit
)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------
if(BUILD_TESTS OR BUILD_BENCHMARKS)
    enable_testing()
    add_subdirectory(tests)
endif()

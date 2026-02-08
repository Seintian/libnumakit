cmake_minimum_required(VERSION 3.15)

# 1. Project Definitions (Must be at the root)
project(libnumakit VERSION 0.1.0 LANGUAGES C)

# 2. Build Options
option(NKIT_BUILD_TESTS "Build unit and integration tests" ON)
option(NKIT_BUILD_EXAMPLES "Build example programs" OFF)
option(NKIT_ENABLE_ASAN "Enable Address Sanitizer (Debug only)" OFF)

# 3. Compiler Configuration (C17 Standard)
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 4. Strict Warning Flags (Development Mode)
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# 5. Global Dependencies (Find hwloc here so we fail early if missing)
find_package(PkgConfig REQUIRED)
pkg_check_modules(HWLOC REQUIRED IMPORTED_TARGET hwloc)
if(NOT HWLOC_FOUND)
    message(FATAL_ERROR "hwloc library not found! Please install libhwloc-dev.")
endif()

# 6. Output Directories (Keep binaries tidy)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 7. Add Subdirectories
add_subdirectory(src)

if(NKIT_BUILD_TESTS)
    enable_testing()
    # Check if tests directory exists before adding
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
        add_subdirectory(tests)
    else()
        message(WARNING "NKIT_BUILD_TESTS is ON, but tests/CMakeLists.txt is missing.")
    endif()
endif()

# 8. Documentation (Doxygen)
find_package(Doxygen)
if(DOXYGEN_FOUND)
    # Check if input file exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in")
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile.in 
                       ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile @ONLY)
        add_custom_target(docs
            COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            COMMENT "Generating API documentation with Doxygen"
            VERBATIM)
    endif()
endif()

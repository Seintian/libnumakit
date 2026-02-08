# 1. Unit Tests (Logic & Data Structures)
# ------------------------------------------------------------------------------
file(GLOB UNIT_SOURCES "unit/*.c")

if(UNIT_SOURCES)
    add_executable(nkit_unit_tests ${UNIT_SOURCES})

    # Link against the main libraries
    target_link_libraries(nkit_unit_tests PRIVATE numakit PkgConfig::HWLOC ${NUMA_LIBRARIES})

    # Register with CTest
    add_test(NAME unit_tests COMMAND nkit_unit_tests)

    # Verify we can find the headers
    target_include_directories(nkit_unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

# 2. Integration Tests (Hardware & Topology)
# ------------------------------------------------------------------------------
file(GLOB INTEGRATION_SOURCES "integration/*.c")

if(INTEGRATION_SOURCES)
    add_executable(nkit_integration_tests ${INTEGRATION_SOURCES})

    # Link against the main libraries
    target_link_libraries(nkit_integration_tests PRIVATE numakit PkgConfig::HWLOC ${NUMA_LIBRARIES})

    # Register with CTest
    add_test(NAME integration_tests COMMAND nkit_integration_tests)

    # Note: On the QEMU CI, this binary is copied into the initramfs.
endif()

# 3. HWLOC Sanity Tests
# ------------------------------------------------------------------------------
file(GLOB HWLOC_SOURCES "hwloc/*.c")

if(HWLOC_SOURCES)
    add_executable(nkit_hwloc_tests ${HWLOC_SOURCES})

    # Link against the main libraries
    target_link_libraries(nkit_hwloc_tests PRIVATE numakit PkgConfig::HWLOC ${NUMA_LIBRARIES})

    # Register with CTest
    add_test(NAME hwloc_tests COMMAND nkit_hwloc_tests)

    # Verify we can find the headers
    target_include_directories(nkit_hwloc_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

# 4. Benchmarks
# ------------------------------------------------------------------------------
file(GLOB BENCHMARK_SOURCES "benchmarks/*.c")

if(BENCHMARK_SOURCES)
    add_executable(nkit_benchmarks ${BENCHMARK_SOURCES})

    # Link against the main libraries
    target_link_libraries(nkit_benchmarks PRIVATE numakit PkgConfig::HWLOC ${NUMA_LIBRARIES})

    # Register with CTest
    add_test(NAME benchmarks COMMAND nkit_benchmarks all)

    # Verify we can find the headers
    target_include_directories(nkit_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

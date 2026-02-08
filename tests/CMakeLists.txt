# 1. Unit Tests (Logic & Data Structures)
# ------------------------------------------------------------------------------
file(GLOB UNIT_SOURCES "unit/*.c")

if(UNIT_SOURCES)
    add_executable(nkit_unit_tests ${UNIT_SOURCES})

    # Link against the main library
    target_link_libraries(nkit_unit_tests PRIVATE numakit)

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

    # Link against the main library
    target_link_libraries(nkit_integration_tests PRIVATE numakit)

    # Register with CTest
    add_test(NAME integration_tests COMMAND nkit_integration_tests)

    # Note: On the QEMU CI, this binary is copied into the initramfs.
    # On a local dev machine, it runs directly on the host.
endif()

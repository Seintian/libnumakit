# 1. Gather Sources
# We explicitly check for sources. Ideally, list them manually, but GLOB is fine for starting.
file(GLOB_RECURSE SOURCES "*.c")
file(GLOB_RECURSE INTERNAL_HEADERS "*.h")

# 2. Define the Library Target
add_library(numakit ${SOURCES} ${INTERNAL_HEADERS})

# 3. Setup Include Directories
target_include_directories(numakit
    PUBLIC
        # For consumers of the library (and us)
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        # Internal headers (src/internal.h)
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 4. Link Dependencies
# Link hwloc privately (users of libnumakit don't need to know we use hwloc internally)
# Link numa (for mbind)
target_link_libraries(numakit PRIVATE PkgConfig::HWLOC numa)
target_include_directories(numakit PRIVATE ${HWLOC_INCLUDE_DIRS})

# 5. Compilation Features
target_compile_features(numakit PUBLIC c_std_17)

# 6. Versioning
set_target_properties(numakit PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# 7. Installation Rules
include(GNUInstallDirs)

# Install the library binary (.so / .a)
install(TARGETS numakit
    EXPORT numakitTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install the Public Headers (include/numakit/*.h)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/numakit
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

add_executable(01_hello_numa 01_hello_numa.c)
target_link_libraries(01_hello_numa PRIVATE numakit)

add_executable(02_arena_alloc 02_arena_alloc.c)
target_link_libraries(02_arena_alloc PRIVATE numakit)

add_executable(03_topology_query 03_topology_query.c)
target_link_libraries(03_topology_query PRIVATE numakit)

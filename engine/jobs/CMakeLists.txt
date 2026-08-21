# L1 -- the CPU work pool (architecture.md §2).
#
# Beside `platform` rather than above it: the pool knows nothing about files,
# windows or events, and `platform`'s async IO knows nothing about jobs. They
# are two independent L1 services and the asset system above them is what pairs
# a read with a decode.

luaug_add_module(jobs
    LAYER 1
    SOURCES
        src/jobs.cpp
    DEPS
        core
)

find_package(Threads REQUIRED)
target_link_libraries(luaug_jobs PUBLIC Threads::Threads)

luaug_add_module_tests(jobs
    SOURCES
        tests/jobs_tests.cpp
        tests/commit_tests.cpp
)

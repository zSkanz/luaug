# Standalone native samples.
#
# NOT `examples/`, which holds numbered Luau projects the host mounts and runs.
# A sample is a C++ executable of its own: it links a subset of the engine's
# modules directly and answers a question about the native layer that a scripted
# project could not isolate.
#
# Deliberately outside `engine/`, so nothing here can become a dependency of the
# engine by accident -- samples consume modules, and no module may consume a
# sample.

add_subdirectory(triangle)

# tools/surfacewrap -- the build-time half of ADR 0091's surface shaders.
#
# Writes the HLSL the engine compiles around a surface shader it ships -- one
# file per pass variant and stage -- from the same `asset::surfaceWrapper` the
# editor and `assetc` use at run time. `luaug_add_shaders(... SURFACES ...)`
# runs it and compiles what it writes, so the contract compiles on every
# backend or the build stops.
add_executable(surfacewrap src/main.cpp)
target_link_libraries(surfacewrap PRIVATE luaug::warnings luaug::asset)

# shaders/ — HLSL Sources

All engine shaders are authored in HLSL (ADR 0006): entry files in `src/`
(one per pipeline), shared code in `include/*.hlsli`. The build invokes
SDL_shadercross (`cmake/luaug_shaders.cmake`) to produce SPIR-V, DXIL, and
MSL into the out-of-tree build directory plus a shader manifest; `luaug dev`
re-runs it for shader hot reload. Compiled output is never committed.

Authoring convention enforced by `luaug_add_shaders()`: one `src/*.hlsl` file is
one graphics pipeline, with entry points `VertexMain` and `FragmentMain`, and
its stem is its name in the shader manifest (so stems must be unique). Register
spaces are dictated by SDL_GPU per stage — see `SDL_CreateGPUShader` in
`third_party/sdl3/include/SDL3/SDL_gpu.h`, and `src/debug_line.hlsl` for a
worked example. Vertex inputs use `TEXCOORDn` semantics because that is what
SDL_shadercross maps to SPIR-V locations.

Alongside each blob the build emits `content/shaders/reflect/<name>.<stage>.json`
— shadercross's reflection of the resource counts and IO locations SDL_GPU
needs at shader- and pipeline-creation time — and `content/shaders/manifest.json`
indexes all of it.

On a host with no DirectXShaderCompiler there is no HLSL front end and therefore
no compiled shaders at all; that is macOS, and it is ADR 0032's stated cost.

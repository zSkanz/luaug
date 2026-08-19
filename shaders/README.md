# shaders/ — HLSL Sources

All engine shaders are authored in HLSL (ADR 0006): entry files in `src/`
(one per pipeline), shared code in `include/*.hlsli`. The build invokes
SDL_shadercross (`cmake/luaug_shaders.cmake`) to produce SPIR-V, DXIL, and
MSL into the out-of-tree build directory plus a shader manifest; `luaug dev`
re-runs it for shader hot reload. Compiled output is never committed.

Populated starting at M1 per `docs/roadmap.md`.

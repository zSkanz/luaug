# LuauG Native Core — Architecture

This is the authoritative design for the native core. The Luau-facing surface
it must serve is defined in [`api-design.md`](api-design.md); the build
sequence and gates live in [`roadmap.md`](roadmap.md); settled decisions are
recorded in [`decisions/`](decisions/). Divergence discovered during
implementation goes through an ADR + an edit to this file in the same commit.

Working conventions used throughout: C++ namespace root `luaug::` (one nested
namespace per module, e.g. `luaug::rhi`), CMake targets `luaug_<module>` with
alias `luaug::<module>`, file names `snake_case`, public headers under
`engine/<module>/include/luaug/<module>/`. The Luau-facing API is
PascalCase Roblox-style. Identifiers below are intended as the real names.

---

## 1. Monorepo layout

```
LuauG/
├─ CMakeLists.txt                     # superbuild root: options, module subdirs
├─ CMakePresets.json                  # all platform×config presets (§8)
├─ LICENSE  NOTICE                    # Apache-2.0 (ADR 0001)
├─ README.md  CONTRIBUTING.md  THIRD_PARTY_NOTICES.md    # notices generated from manifest
├─ .clang-format  .clang-tidy  .editorconfig  .gitignore  .gitattributes
├─ .luaurc                            # repo-root aliases: @std, @luaug, @tools (analyze/lsp)
├─ cmake/
│  ├─ luaug_options.cmake             # LUAUG_* option definitions, backend toggles
│  ├─ luaug_warnings.cmake            # warnings-as-errors profiles per compiler
│  ├─ luaug_sanitizers.cmake
│  ├─ luaug_luau.cmake                # vendored-Luau targets + bytecode precompile function
│  ├─ luaug_shaders.cmake             # luaug_add_shaders() → SDL_shadercross invocation
│  ├─ luaug_module.cmake              # luaug_add_module() helper enforcing layer deps
│  └─ toolchains/                     # android.cmake (nightly compile job), ios.cmake (later), console stubs
├─ third_party/                       # vendored source (ADR 0021)
│  ├─ manifest.json                   # {name, version, upstream URL, commit SHA, license, patches[]}
│  ├─ patches/luau/*.patch            # local patches, applied by vendor tool, ideally empty
│  ├─ luau/  sdl3/  sdl_shadercross/  jolt/  box2d/  miniaudio/  fastgltf/
│  ├─ meshoptimizer/  basis_universal/ ktx/  stb/  imgui/  recastnavigation/
│  ├─ gamenetworkingsockets/  enet/   assimp/  doctest/  blake3/  xxhash/
├─ engine/                            # one dir per C++ module (§2); each contains
│  ├─ core/      #   include/luaug/core/  src/  tests/  CMakeLists.txt
│  ├─ jobs/  platform/  rhi/  render/  physics/  audio/  asset/
│  ├─ scene/  script/  input/  ui/  nav/  net/  app/
├─ runtime/                           # Luau code shipped inside the engine
│  ├─ std/                            # @std implementation (Lute-compatible surface), --!strict
│  ├─ luaug/                          # @luaug pure-Luau layers (e.g. camera rigs, test lib)
│  └─ types/                          # GENERATED .d.luau (checked in; CI verifies freshness)
├─ api/
│  ├─ schema.luau                     # typed IDL schema (validated by the type checker itself)
│  ├─ defs/*.api.luau                 # single source of truth for every class/service (§4)
│  └─ generator/                      # Lute-run: gen_cpp.luau, gen_dts.luau, gen_docs.luau,
│                                     #   gen_i18n.luau, check.luau (naming lints)
├─ shaders/
│  ├─ include/*.hlsli                 # shared HLSL includes (lighting, packing)
│  └─ src/*.hlsl                      # one entry file per pipeline
├─ tools/
│  ├─ cli/                            # `luaug` CLI (Lute app): cmd/{new,dev,run,build,asset,
│  │                                  #   test,check,fmt,setup,add,doctor}.luau  (user CLI per api-design)
│  ├─ repo/                           # repo-internal tools (Lute): vendor.luau, checklayers.luau,
│  │                                  #   apigen wrappers, docs-lint
│  ├─ importer/                       # C++ offline importer exe (assimp lives ONLY here)
│  ├─ imgcmp/                         # screenshot tolerance comparator
│  └─ bootstrap/                      # toolchain setup helpers used by scripts/bootstrap.*
├─ samples/                           # native C++ samples: triangle (the M4 Android checkpoint's artifact)
├─ examples/                          # 00-clear … 10-open-world (roadmap numbering) -- Luau projects run by the host
├─ tests/
│  ├─ integration/                    # C++ multi-module tests (headless host)
│  ├─ conformance/                    # .spec.luau behavior specs run by the headless engine
│  ├─ determinism/                    # recorded-input scenarios + golden world hashes
│  └─ rendercapture/                  # golden command-stream captures for rhi_capture
├─ i18n/
│  ├─ en.json                         # the only catalog at launch
│  └─ keys.gen.json                   # generated key inventory for CI validation
├─ templates/                         # `luaug new` project templates (starter, obby, openworld-demo)
├─ docs/                              # this documentation set
├─ scripts/                           # bootstrap.ps1 / bootstrap.sh
└─ .github/workflows/                 # ci.yml, nightly.yml, release.yml
```

**Vendored dependency policy** (ADR 0021): full source in `third_party/`,
managed by `tools/repo/vendor.luau`; no submodules, no FetchContent. Hermetic
offline builds (console CI is network-restricted), reviewable patch sets
(Luau), trivial license audits (`manifest.json` → generated
`THIRD_PARTY_NOTICES.md`), stable in-repo grounding paths for the agent. Lute
binaries are NOT vendored — `scripts/bootstrap` installs the pinned Lute via
rokit into the user's toolchain.

**Build location:** the repo lives at `D:\Projects\LuauG` (outside any synced
folder). Builds are still always out-of-tree: `CMakePresets.json` sets
`binaryDir` to `$env{LUAUG_BUILD_ROOT}/${presetName}` with the documented
default `%LOCALAPPDATA%/LuauG/build` on Windows and `~/.cache/luaug/build`
elsewhere (rule R14). Bytecode/shader caches also live under the build root.

---

## 2. C++ module decomposition and dependency rules

**Layering (a module may include only modules at strictly lower layers;
seam/API targets are header-only and count as their layer):**

```
L0  core
L1  jobs, platform
L2  rhi_api (+rhi_sdlgpu, rhi_capture, rhi_null, rhi_bgfx later),
    physics_api (+physics_jolt, +physics_box2d later),
    net_api (+net_gns, +net_enet), asset
L3  scene            (may include physics_api only -- interface headers, no impl)
L4  render (renderer_default behind IRenderer), audio, input, nav (seam only, ADR 0022)
L5  ui, script
L6  app
```

Hard rules:
1. Impl backends (`*_jolt`, `*_sdlgpu`, …) are linked only by `app`; every
   other module sees only `*_api` headers (ADR 0023).
2. `script` is the ONLY module that may include everything L0–L5: all Luau
   bindings live there. No module below `script` may include Luau headers —
   `core` only mirrors the `vector` memory layout, with no `#include <lua.h>`.
3. `scene` never includes `render`, `ui`, `input`, or `script`. Higher modules
   register their classes/components INTO scene's registries at startup
   (`render::registerSceneTypes(ClassRegistry&)` etc.).
4. Shared vocabulary types live in `core`: `ContentHash` (BLAKE3-128),
   `InstanceId`, `NameAtom`, `TextKey`, `MemTag`, `Phase`.
5. Enforced mechanically: `luaug_add_module()` declares allowed deps; a CI job
   runs `tools/repo/checklayers.luau`, parsing `#include "luaug/…"` lines
   against the layer matrix.

### Per module — responsibility, key interface, dependencies

**core** (deps: none)
Memory (arenas, pools, tagged tracking), logging, i18n keys, profiling, math
(layout-identical to Luau `vector`: `Vec3` is 3×f32), containers, hashing,
time, RNG.
```cpp
namespace luaug::core {
  struct Arena;                 // linear; FrameArena resets per frame, per thread
  struct PoolAllocator;         // fixed-size blocks (signals, connections, waiters)
  enum class MemTag : u8;       // mirrors the Luau memcat plan (§6)
  void* alloc(MemTag, size_t); struct MemStats snapshotMemStats();
  struct TextKey { u32 hash; }; // constexpr from LUAUG_TR("scene.err.parent_cycle")
  std::string i18nFormat(TextKey, std::span<const I18nArg>);   // catalog from i18n/en.json
  void log(LogLevel, TextKey, std::span<const I18nArg>);       // no raw user-facing strings (R3)
  LUAUG_ZONE(name);             // profiler scope macro; counters API; frame timeline ring
  struct Vec2; struct Vec3;     // f32, Vec3 bit-identical to the Luau vector
  struct Quat; struct Mat4; struct DVec3;                      // f64 world positions
  struct CFrameD { DVec3 pos; Mat3 rot; };                     // canonical world transform
  struct AABB; struct DAABB; struct Frustum; struct Color3;
  struct NameAtom { u32 id; };  // interned strings, engine-wide
  struct InstanceId { u32 index; u32 gen; };                   // stable slotmap handle
  struct ContentHash;           // BLAKE3-128
  struct Pcg32;                 // deterministic RNG streams
  enum class Phase : u8;        // frame pipeline vocabulary (§3), incl. parallel windows A/B
  template<class T> class SlotMap; class SmallVec; class FlatMap; // containers
}
```

**jobs** (deps: core) — work-stealing CPU pool; NO blocking IO here.
```cpp
namespace luaug::jobs {
  enum class Domain : u8 { SimVisible, Render, AssetIo, Tooling };
  void init(u32 workerCount = 0); void shutdown(); bool initialized(); u32 workerCount();
  template<class Callable>
  JobHandle schedule(const char* name, Domain, Callable&&, std::span<const JobHandle> deps = {});
  void wait(JobHandle); void waitAll(std::span<const JobHandle>);
  void parallelFor(const char* name, Domain, usize begin, usize end, usize grain, RangeFn, void* user);
  constexpr u32 rangeCount(usize begin, usize end, usize grain);   // what a StableCommit is sized by
  template<class T> class StableCommit;                            // jobs/commit.h
}
```
**Shipped at M7, and three things about it are contracts rather than sketch.**
`Domain` is a *parameter* rather than a comment, because the classification has
to survive being read by somebody who did not write the job. **An uninitialized
pool is a serial pool** -- `schedule` runs the callable immediately and
`parallelFor` walks its ranges in order -- which is the mode every headless
determinism run wants and means no caller needs an `if (poolExists)` branch.
And **the Jolt bridge is NOT here**: `jobs` is L1 and Jolt is a backend at L2,
so a `JPH::JobSystem` implemented on this pool lives in `physics_jolt` where the
Jolt headers already are. The planning sketch put a `joltAdapter()` in this
namespace; that would have been the one include that made L1 know what a solver
is.

**Deterministic commit rule (R10):** parallel work that is visible to the
simulation writes into per-job result/command buffers, hits a barrier, and is
merged in a stable, index-ordered pass before any world mutation. Job domains
are classified — *sim-visible* (stable commit required), *render*, *asset/IO*,
*tooling* (no ordering requirement). Never let "which worker finished first"
become observable simulation state.

`StableCommit<T>` is that rule as a type, and `parallelFor`'s range partition is
what makes it work: a range index is a function of the DATA -- `begin`, `end`,
`grain` -- and never of how many workers the machine has, so bucket 3 holds the
same elements on every machine and the merged order is ascending by element
rather than by whoever finished first.

**platform** (deps: core) — SDL3 wrapper; the only module touching SDL
directly (besides `rhi_sdlgpu` and `app` glue).
```cpp
namespace luaug::platform {
  struct Window;  Window* createWindow(const WindowDesc&);
  std::span<const Event> pumpEvents();          // raw input, window, drop, quit
  bool initIo(u32 maxInFlight); void shutdownIo();             // SDL_AsyncIO, one completion queue
  IoRequest readFileAsync(const path&, IoPriority, IoCallback = {});
  void pumpIo();                                               // completions land HERE and nowhere else
  bool takeIoResult(IoRequest, std::vector<std::byte>&); void cancelIo(IoRequest);
  void setIoPriority(IoRequest, IoPriority);                   // a chunk the focus turned towards
  WatchHandle watchDirectory(PathView, WatchCallback);         // hot-reload feed
  Paths paths();                // projectDir, userDataDir, cacheDir
  u64 nowNs(); void setThreadName(const char*);
}
```

**Two things about the IO service are M7 corrections to this sketch.**
**Priority is ours**: SDL_AsyncIO has a completion queue and no notion of
priority at all, so requests wait in a priority order this module maintains and
only `maxInFlight` reach SDL at a time -- which also bounds how much memory
in-flight reads can hold. And **completions land during `pumpIo()`**: the
callback firing from the IO thread would put an arbitrary thread and an
arbitrary moment between a read and the world, which R10 forbids for anything
the simulation can see. `cancelIo` on a read SDL already started marks it
abandoned rather than pretending to stop it, because SDL_AsyncIO has no cancel
and a pretend one means a buffer nobody frees.

**rhi_api** (headers-only; deps: core, platform for native window handles) —
~40 calls with **LuauG semantics over the concepts common to
Vulkan/Metal/D3D12**: Device, Buffer, Texture, Sampler, Shader, Pipeline,
CmdList, RenderPass, Compute, Swapchain, Capabilities. The shape stays
deliberately close to SDL_GPU so the default adapter is thin — but **no SDL
type appears in these headers**, and neutrality is validated by the
`rhi_capture` (CI) and future bgfx backends (ADR 0005).
```cpp
namespace luaug::rhi {
  struct DeviceDesc { BackendId backend; bool debug; };
  class IDevice {  // created via the app factory (§7)
    Capabilities caps();        // optional-feature queries from day one
    BufferHandle   createBuffer(const BufferDesc&);
    TextureHandle  createTexture(const TextureDesc&);
    SamplerHandle  createSampler(const SamplerDesc&);
    ShaderHandle   createShader(const ShaderDesc&);      // SPIR-V/DXIL/MSL blob per backend
    PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc&);
    PipelineHandle createComputePipeline(const ComputePipelineDesc&);
    void destroy(AnyHandle);
    CmdList* beginFrame();  TextureHandle acquireSwapchain(Window*);
    void submitAndPresent(); void waitIdle();
  };
  struct CmdList {
    void beginRenderPass(const RenderPassDesc&); void endRenderPass();
    void setPipeline(PipelineHandle); void setViewportScissor(...);
    void bindVertexBuffers(...); void bindIndexBuffer(...);
    void bindUniforms(u32 slot, const void*, usize);     // push-style
    void bindTextures(u32 slot, std::span<TextureBinding>);
    void draw(...); void drawIndexed(...); void drawIndexedIndirect(...);
    void dispatch(...);
    void upload(BufferHandle, std::span<const std::byte>, usize off); // staging managed inside
    void copyBufferToTexture(...); void readback(...);
    void pushDebugGroup(const char*); void popDebugGroup();
  };
}
```
Backends: `rhi_sdlgpu` (v1 default), `rhi_capture` (records a canonical
command stream — the blocking render-regression gate, §9), `rhi_null`,
`rhi_bgfx` (mobile phase).

**render** (deps: core, jobs, platform, rhi_api, asset, scene) — the
**`IRenderer` contract** (ADR 0027): a renderer consumes a `RenderWorld` (the
POD snapshot extracted from scene each frame — the stable data contract, and
the seam for a future render thread) plus an `rhi::IDevice`.
```cpp
namespace luaug::render {
  void registerSceneTypes(scene::ClassRegistry&);   // Camera, MeshPart visuals, Lights, Sky
  struct RenderWorld;           // POD snapshot: visible meshes, lights, camera, sky, ui2d lists
  RenderWorld* extract(const scene::World&, float alpha, FrameArena&); // interpolated transforms
  class IRenderer { public: virtual void render(rhi::IDevice&, const RenderWorld&) = 0; };
  // renderer_default (v1): CSM shadow atlas → depth prepass → SSAO → sky +
  //   clustered forward → auto-exposure → bloom → tonemap → FXAA → ui2d → imgui
  DebugDraw& debugDraw();       // lines/boxes/spheres/text, dev builds; available from M1
}
```
**Anti-aliasing is FXAA, and there are no motion vectors** (M7.5, and the
roadmap's design constraint asked for this either way). FXAA is spatial: it needs
the final image and nothing else, so `RenderWorld` gained no previous-frame
transform, no draw writes a velocity target, and `RenderCamera::jitter` exists as
a field that is zero everywhere. **What that costs is named rather than left to
be discovered**: a temporal upscaler (FSR2, XeSS) and frame generation are
exactly as far away as they were before M7.5, because both need the per-pixel
motion vector that does not exist, and producing one is a second render target
and a previous transform on every object -- renderer-wide work, not a pass. It
did not become closer because the engine now has a depth buffer it can sample;
depth is the cheap half.

Specular aliasing, which FXAA cannot reach because it is a point flickering
inside a surface rather than a step along an edge, is answered in the shading
instead: `antiAliasedAlpha` widens the GGX lobe by the normal variation a pixel
covers (Kaplanyan/Tokuyoshi; `shaders/include/luaug_brdf.hlsli`).

Alternative renderers (deferred, GPU-driven) are build-time selections like
any backend and may not reach into scene or rhi internals. A 2D sprite pass
slot is reserved before ui2d for the post-v1 2D layer. Meshlet data ships in
assets from day one; the GPU-driven path gates on `Capabilities`.

**physics_api** (headers-only; deps: core) + `physics_jolt`, later
`physics_box2d` (a separate `IPhysics2D`). Shipped at M5; the shape below is
what it is, not a sketch.
```cpp
namespace luaug::physics {
  class IPhysics3D {
    WorldHandle createWorld(const WorldDesc&);          // gravity, body budget
    BodyHandle  createBody(WorldHandle, const BodyDesc&);      // shape, motion type, group
    void updateBody(WorldHandle, BodyHandle, const BodyDesc&); // a rebuild that keeps the handle
    void setBodyTransform(...); BodyState bodyState(...) const;
    void collectActiveBodies(WorldHandle, std::vector<ActiveBody>&) const;  // STABLE order
    void step(WorldHandle, f32 fixedDt);                // deterministic per ADR 0025
    std::span<const ContactEvent> drainContacts(WorldHandle);  // → deferred Touched signals
    bool raycast(WorldHandle, const RayD&, const QueryFilter&, RayHit&) const;  // + spherecast, overlapBox
    CharacterHandle createCharacter(WorldHandle, const CharacterDesc&);  // Jolt CharacterVirtual
    void moveCharacter(WorldHandle, CharacterHandle, Vec3 velocity, f32 dt);
    CharacterState characterState(WorldHandle, CharacterHandle) const;
    bool saveState(WorldHandle, std::vector<u8>&) const;       // rollback-oriented seam:
    bool restoreState(WorldHandle, std::span<const u8>);       // declared, refuses in v1
    CollisionGroup registerCollisionGroup(WorldHandle, std::string_view);
    void setGroupsCollidable(WorldHandle, CollisionGroup, CollisionGroup, bool);
    void debugDraw(WorldHandle, IDebugDrawSink&);       // the backend's own wireframe
  };
}
```
A body carries an opaque `u64 userData` the caller chooses and this module never
interprets — that is how `scene` owns the tree and `physics` owns the simulation
without either learning the other's vocabulary, and it is why a contact event
reports two user-data values rather than two things this module would have to
name.

**Three of these methods promise a STABLE ORDER, and that is R10 rather than
tidiness.** Upstream documents contact callbacks, the active-body list and a
query's hit order as non-deterministic under a multi-threaded job system
(`third_party/jolt/Docs/Architecture.md:804-807`). None of it is true today
under the single-threaded one, which is exactly why the sorts had to be written
at M5: M7 wires the pool, and the milestone that discovers a thousand recorded
traces are worthless is not one anybody wants.

**audio** (deps: core, jobs, platform, scene) — miniaudio. **At L4 from M6, not
L2**: it owns the `Sound` and `AudioGroup` classes, and a module that registers a
class into scene's registry has to be able to see scene. ADR 0009's seam is
unchanged by the move -- the seam is the module boundary, not its layer -- and
`render` sits at L4 for the same reason. miniaudio. **The module itself is the
swappable seam** (ADR 0009): one implementation in v1, selected at build time;
the public Luau API never leaks miniaudio concepts; FMOD/Wwise arrive post-v1
as alternative module implementations, not as a "mixer backend" layer.
```cpp
namespace luaug::audio {
  void init(const AudioDesc&); void shutdown();
  VoiceHandle play(const SoundAssetRef&, const PlayParams&);   // 2D or positional
  void setVoice(VoiceHandle, VoiceParam, float); void stop(VoiceHandle);
  BusHandle createBus(NameAtom parent);                        // AudioGroup backing
  void setListener(const CFrameD&);
  void update();                // main-thread pump; the mixer runs on its own thread (lock-free queue)
}
```

**asset** (deps: core, jobs, platform) — runtime loading, streaming, content
addressing. Produces CPU-side resources; render/audio own uploads. Knows
nothing about rhi.
```cpp
namespace luaug::asset {
  void mount(PathView dirOrPack);                    // dev: content dir; ship: .lpack files
  template<class T> AssetHandle<T> load(ContentHash, Priority);   // refcounted; Unloaded/Loading/Ready/Failed
  AssetHandle<> loadByUrn(std::string_view);         // "asset://models/tree.glb" via the content manifest
  // Asset types: MeshAsset (LOD chain + meshlets), TextureAsset (KTX2, transcoded on jobs),
  //   MaterialDef, SoundAsset, AnimationClip, ModuleSource (Luau), PrefabDef, ChunkManifest, ShaderPack
  struct StreamingManager {                          // the policy engine for §10
    void setFoci(std::span<const StreamingFocus>);   // position + Min/TargetRadius
    void setBudgetBytes(u64); void tick(FrameBudget);// issues loads/evictions by priority score
    Signal<ChunkId, ChunkState> onChunkStateChanged; // consumed by scene streaming glue
  };
  Signal<ContentHash> onAssetInvalidated;            // hot-reload feed
}
```
`PrefabDef` is the compiled form of `.prefab.luau` declarative instance trees
(api-design §prefabs): the importer bakes them (standalone or into chunk
payloads); the runtime materializes them through the same reflection path as
scripts, and `AssetService:LoadModelAsync` + `Instance:Clone()` give the
Unity-prefab-style instantiate-many workflow.

**scene** (deps: core, jobs, physics_api, asset) — ECS storage, Instance facade,
reflection, attributes/tags, signals, serialization, streaming glue, built-in
systems (transform hierarchy, physics sync via an injected `IPhysics3D*`).
Details in §4.

**script** (deps: core, jobs, platform, scene, asset, input, net_api, ui) —
VM pool, scheduler integration, sandbox, bindings, `@std`, Luau.Require
wiring, hot reload. Details in §5.

**input** (deps: core, platform, scene) — the Input Action System (ADR 0029).
`InputAction`/`InputBinding`/`InputContext` are Instance classes (registered
into scene); this module owns their components and resolution.
```cpp
namespace luaug::input {
  void registerSceneTypes(scene::ClassRegistry&);
  void pumpFrame(std::span<const platform::Event>);   // device snapshots + timestamped queue
  void dispatchRenderRate();                          // fires UI/camera-priority contexts (§3)
  void dispatchSimTick(u64 tick);                     // drains queue ≤ tick time → action signals (deterministic)
  // Resolution: contexts sorted by Priority (STABLE, so a tie reproduces -- R10);
  // Sink=true consumes the inputs a context names, per input rather than per context;
  // per-binding Scale; GetPreferredBinding(device) for glyph prompts.
  //
  // Which CLOCK a context is dispatched on is the context's own `Rate` property and
  // is NOT derived from Priority (ADR 0039): priority orders fallthrough, and one
  // number cannot answer two questions.
  //
  // No saveBindings/loadBindings. An earlier draft of this sketch had them; v1 has
  // no caller, rebinding is a property write, and persistence is the game
  // serializing an ordinary Instance (ADR 0039).
}
```

**ui** (deps: core, scene, input, render, asset) — Roblox-style GUI instances
(`ScreenGui`, `Frame`, `TextLabel`, `TextButton`, `TextInput`, `ImageLabel`,
`ImageButton`, `ScrollFrame`, layout modifiers — the api-design v1 set) with
familiar `UDim2`/anchor properties; **the layout is computed directly** -- two
passes over each dirty `ScreenGui`, bottom-up for `AutomaticSize` and top-down
for every absolute rectangle. It was to have compiled to Clay; ADR 0040 records
why it does not, and the short version is that a `UDim2` placement is arithmetic
rather than a constraint, and that Clay cannot express an unclamped scale or a
fractional anchor point. **Clay was un-vendored at M6** by the same reasoning: a
dependency nobody calls still enters every build, every notices file and every
future reader's half hour, and "not used yet" and "does not fit the model" are
different states. The ADR stays if a genuinely flow-shaped feature ever wants it
back. Text: stb_truetype atlas, kerning only in v1 (complex-script shaping
is a flagged i18n gap; HarfBuzz seam post-v1). Produces a 2D draw list
consumed by render's ui2d pass; hit-testing routes through an engine-owned
high-priority `InputContext` with Sink.

**nav** (deps: core, jobs, scene) — **seam only in v1** (ADR 0022): Recast
builds on jobs (async, per streamed region) and Detour queries on the sim
thread are the designed shape; vendored, interface sketched, not integrated,
no Luau service.

**net_api** (deps: core) + `net_gns`, `net_enet` — v1 ships primitives only
(ADR 0012); `ITransport` is the future replication seam.
```cpp
namespace luaug::net {
  // Primitives backing @std/net (buffer-based payloads):
  TcpListener listen(u16 port); TcpStream connect(Host);
  UdpSocket udp(u16 port); WebSocketServer wsListen(u16); HttpServer httpListen(u16);
  HttpResponse httpRequest(const HttpRequestDesc&);
  class ITransport {  // future replication channel abstraction (GNS default, ENet alt)
    ConnId connect(...); void send(ConnId, Channel, std::span<const std::byte>, Reliability);
    std::span<const NetEvent> poll();
  };
}
```

**app** (deps: everything) — the host executable `luaug-host`: subsystem
bring-up order, the **explicit backend factory** (ADR 0023: one hand-written
`switch` over `#if LUAUG_RHI_*`-compiled backends; runtime choice among
compiled-in options via `--rhi=capture`), the `FrameScheduler` (§3), DataModel
service wiring, the ImGui `DebugShell` (explorer, properties, profiler with
the memcat table, log/REPL, streaming map, physics wireframe), **headless
mode** (`--headless`: no window/rhi unless capture; sim + script + asset only
— the CI harness and the future dedicated-server shape), crash handler
(minidump + log), `BindToClose` with a capped grace period.

---

## 3. Frame pipeline and scheduler

Owner: `luaug::app::FrameScheduler`. Two clocks: **SimClock** (fixed tick,
default 1/60, configurable 30–240 via `PhysicsService.FixedTimestep`, `u64
tick` counter) and **RenderClock** (variable dt). Deterministic simulation
lives entirely inside sim ticks; rendering interpolates.

```
Frame:
 1. FrameStart safe point:
      apply hot-reload batch (§5), apply streaming materialization (budgeted, §10),
      apply origin rebase if pending (§10), process asset-ready callbacks
 2. platform::pumpEvents → input::pumpFrame          (timestamped raw queue)
 3. input::dispatchRenderRate                        (UI/camera contexts; render-rate signals)
 4. RP PreRender (RenderClock dt + interpolation alpha)      [parallel window B seam]
 5. accumulator += renderDt;  while accumulator >= fixedDt (max 4, then clamp+warn):
      SimTick(tick++):
        a. input::dispatchSimTick(tick)              (gameplay action signals, deterministic)
        b. RP PreAnimation                           (animation clip sampling after drain)
           tween step                                (property tweens, on the SimClock -- after the
                                                      drain, so a tween started by a handler in this
                                                      phase begins on the next tick)
        c. RP PreSimulation                          [parallel window A seam]
        d. physics.step(fixedDt)  → drainContacts → enqueue Touched signals
        e. scene systems: transform hierarchy sync, character update, weld
           resolution (topological, cycles refused at write time), day/night tick
        f. RP PostSimulation
        g. task-resume: SimClock timer wheel (task.wait/delay resume here, fixed dt args)
        h. RP Heartbeat
 6. script GC step within the frame budget (§5)
 7. render::extract(world, alpha = accumulator/fixedDt)      (interpolated transforms)
 8. renderer->render(...); ui + DebugShell; rhi.submitAndPresent
 9. audio::update;  profiler frame close
 On quit: fire BindToClose handlers, wait ≤ 30 s (configurable), shutdown in reverse init order.
```

**Resumption points (RP):** each RP = run the engine phase systems, then
**drain the deferred queue**: every queued fire invokes each eligible handler
on its own coroutine; handlers that defer further work re-enqueue into the same
drain, which runs to fixpoint. There is one queue, shared by engine-raised
fires, script `Signal:Fire`, and `task.defer`, and re-entrancy is capped at a
per-fire generation depth of 10 (overflow logs `script.err.reentrancy_limit`
and drops). There is no Immediate mode and no legacy `wait`/`spawn`/`delay`
anywhere (ADR 0015). **The full ordering contract — what a fire captures,
connection order, `:Once`, `:Wait`, `Destroy` against queued fires — is
[`api-design.md`](api-design.md) §3.1**, which is what the conformance specs
are written against; this section only places the drains in the frame.

**Rate mapping (documented loudly — the one deliberate divergence from a naive
Roblox mental model):** `PreRender` fires per render frame with variable dt;
`PreAnimation`, `PreSimulation`, `PostSimulation`, and `Heartbeat` fire per
sim tick with fixed dt. `task.wait()` resumes on the SimClock (deterministic);
`RunService.PreRender:Wait()` gives render rate. This is the rollback-ready
shape Roblox retrofitted; LuauG starts there.

**Parallel (actor) windows:** window A (after the PreSimulation drain) and
window B (after the PreRender drain) exist in the `Phase` enum and the
scheduler from day one. v1 executes `ConnectParallel` handlers serially inside
those windows on the game VM — but the thread-safety checker (§4) already
enforces Unsafe/ReadParallel/LocalSafe/Safe as if they were parallel, so code
written today survives real actor VMs later.

**GC step budgeting:** after task-resume phases,
`ScriptRuntime::gcStep(budget)` runs `LUA_GCSTEP` with a step size derived
from `lua_allocationrate` and the remaining frame headroom; target ≤ 1 ms at
60 fps, escalating (assist) when allocation outpaces collection; full
collection only at loading screens/level transitions.

**Hot reload interaction:** the watcher thread only enqueues; mutations happen
exclusively at the FrameStart safe point (never mid-tick), preserving
within-run determinism.

**Headless mode:** the identical scheduler minus steps 2–4 and 7–9; sim ticks
driven by `--ticks N` (as fast as possible, for tests) or real time. This is
the determinism-test and conformance-test harness.

---

## 4. Instance ⇄ ECS bridge

**ECS storage — hand-rolled sparse-set ECS (`luaug::scene::World`), not EnTT**
(ADR 0028): (a) guaranteed deterministic iteration order (dense pools in
insertion order, compaction only at FrameStart safe points); (b) trivially
snapshottable POD SoA pools for the rollback-oriented foundations
(`World::snapshot()/restore()` = per-pool memcpy + entity maps); (c)
chunk-scoped lifetime (entities tagged with `ChunkId`; unload = bulk removal
from pools + arena free); (d) no template-metaprogramming compile costs for an
autonomous implementer. EnTT is inspiration only.

- Entity = `InstanceId {u32 index, u32 gen}` in a `SlotMap`; the generation
  bumps only on true `Destroy`.
- Hierarchy component: `{ parent, firstChild, nextSibling, prevSibling,
  childCount }` intrusive links.
- **Child-name index — duplicate names supported** (ADR 0026): per parent, a
  `FlatMap<NameAtom → first child with that name>` plus an intrusive
  `nextSameName` chain on each instance. `FindFirstChild` is O(1) and returns
  the first match in child (document) order — Roblox semantics; rename/reparent
  is an O(chain) unlink/relink; ≈ 8 bytes per instance. (A plain
  `FlatMap<Name, Id>` would clobber the extremely common
  "three siblings named Tree" case.)
- Universal components: `NameComp{NameAtom}`, `ClassComp{ClassId}`,
  `AttributesComp` (typed variant map, lazily allocated), `TagsComp` (backed
  by global tag→entity sets for TagService), `TransformComp{ CFrameD world;
  CFrame local; }`.
- Class-specific state lives in module-owned components (`RigidBodyComp`,
  `MeshVisualComp`, `UiLayoutComp`, `InputActionComp`, `AnimationComp`, …)
  registered by those modules.

**Reflection:** `scene::ClassRegistry` holds per-class `ClassDescriptor {
ClassId, superId, flags(Service|Abstract|NotCreatable|DevOnly), properties[],
methods[], events[], defaultName }`; `PropertyDesc { NameAtom atom; TypeTag;
ThreadSafety; getter/setter fn-ptrs (World&, InstanceId); flags(ReadOnly|
Scriptable|Serialized); TextKey errKeyOnInvalidSet; }`. All descriptors are
**generated** from the API definition IDL (`api/defs/*.api.luau` — see
api-design §5); hand-written C++ implements only the referenced native
functions.

**Property writes** go through the generated setter, which (1) validates
(i18n-keyed error on failure), (2) writes the component field, (3) enqueues
`{instance, propertyAtom}` into the deferred change queue → fans out to
`GetPropertyChangedSignal(prop)` / `AttributeChanged` at the next RP drain.
Bulk engine-side mutation (physics results, streaming, tweens) uses a **quiet
write + batched changed-set** path with per-instance subscription bitmasks, so
10k moving parts enqueue nothing unless someone actually subscribed. The
10k-parts/1k-listeners benchmark guards this with a CI threshold from M2.

**Instance semantics checklist (all v1):** `Parent` (cycle check →
`scene.err.parent_cycle`), `Clone` (deep copy with internal-reference fixup —
the prefab workhorse), `Destroy` (locks Parent, disconnects, fires
`Destroying`, generation bump deferred to end of drain),
`FindFirstChild/FindFirstChildOfClass/FindFirstAncestor`,
`WaitForChild(name, timeout?)` (parks the coroutine on a waiter record keyed
by `(parent, NameAtom)`, woken at RP drains as soon as a matching child
*exists* under that parent — so the wake hangs off the child-name index above,
which a rename updates exactly as a reparent does, not off `ChildAdded` alone;
the full contract, timeout and warning included, is
[`api-design.md`](api-design.md) §2.2), `GetDescendants`, `IsA` (ClassId
ancestor table),
`ChildAdded/ChildRemoved/DescendantAdded/DescendantRemoving/AncestryChanged/
Destroying`, Attributes (`GetAttribute/SetAttribute/GetAttributeChangedSignal`)
and Tags (`AddTag/HasTag/GetTags` + `TagService:GetTagged/
GetInstanceAddedSignal`) from day one.

**Streaming semantics** (service surface in api-design): foci with
Min/Target radii, `StreamingMode = Default|Atomic|Persistent` per Model,
`PauseOutsideLoadedArea` integrity. Stream-out **reparents to nil, never
destroys**: instances still referenced from Luau become nil-parented husks
(identity and generation preserved, class-specific components stripped, state
serialized into the chunk's resident blob); unreferenced instances are
dematerialized entirely and rebuilt as new objects on stream-in
(Roblox-faithful). `Persistent` subtrees never dematerialize.
`PauseOutsideLoadedArea`: entities in non-resident chunks get physics bodies
removed and their scripts' sim-clock tasks suspended.

---

## 5. Script host design

**VM topology v1:** exactly **one game VM** (`lua_State`) on the main thread
for all gameplay, created through a `VmPool` that is architecturally ready for
N actor VMs (`VmKind::Game | Actor | Repl`). Each future actor = its own VM on
a jobs worker during parallel windows, with a `script/marshal` layer (allowed
values: primitives, vector, `buffer` by copy or transfer, `InstanceId` refs,
frozen plain tables) — "many light VMs per core" is the Roblox-validated
recipe; nothing in v1 may assume cross-VM table sharing. The dev-only REPL
runs as a sandboxed thread of the game VM, not a separate VM.

**Boot sequence per VM:** open the Luau stdlib → register engine libraries
(`@luaug/*` modules via `luarequire_registermodule`, `task`, the `Vector3`
library mapped onto the native vector) → install `game`/`workspace` globals →
`luaL_sandbox` (mandatory: security AND safeenv fastpaths, R4). Every Script
instance executes on its own `lua_newthread` + `luaL_sandboxthread` (isolated
globals). `lua_setthreaddata` carries `ScriptThreadCtx { scriptInstance,
memcat, budgets, actorId }`; the `userthread` callback maintains it.

**Compilation:** `Luau::CompileOptions { optimizationLevel = 2, debugLevel = 2
(dev) / 1 (ship), typeInfoLevel = 1, vectorLib = "Vector3", vectorCtor =
"new", vectorType = "Vector3" }` (no mutable globals — `_G` is frozen per
api-design). Bytecode cache keyed by `blake3(source) + luauVersion + options`
under the build root. Shipping clients load precompiled bytecode only and link
Luau.VM + Luau.CodeGen (`LUAUG_LUAU_COMPILER=OFF`). NCG is enabled on
x64/A64 desktop and (later) Android; **never on iOS** — hence R16:
interpreter-first hot paths.

**Require:** Luau.Require (the `luarequire_*` vtable) over the real
filesystem — project `.luaurc` aliases, `@self`, cyclic-require semantics,
resolution identical across engine runtime / luau-analyze / luau-lsp / Lute
tooling. Engine APIs appear as `require("@luaug/…")`; `@std` is the
Lute-compatible surface implemented in `runtime/std/`, capability-gated per
api-design §7 (`luaug.toml [permissions]`). The runtime tracks the module
dependency graph (who required whom) — used for bytecode-cache invalidation.

**Bindings:** tag-based userdata throughout, no `__gc`.
- One userdata tag `TAG_INSTANCE` for all instances; payload = `InstanceId`
  (8 bytes); per-class metatables pre-registered so
  `lua_newuserdatataggedwithmetatable` (~3× faster) applies; a per-VM
  weak-valued cache table `InstanceId → userdata` guarantees `a == b`
  identity. A dead handle (generation mismatch) errors with
  `script.err.instance_dead`.
- Value types (`CFrame`, `Color3`, `UDim2`, `Rect`, `Random`, `Connection`,
  `AssetRef`) each get their own tag + `lua_setuserdatadtor` where a
  destructor is needed. `Vector3` IS the native vector primitive — never
  userdata; constructors fold at compile time via vectorCtor.
- Property/method dispatch: the `useratom` callback interns names once;
  `__index`/`__newindex`/`__namecall` switch on atom → direct `PropertyDesc`
  slot (no string compares, no per-call allocation). Never persist
  `lua_Type`/tag values (ABI is not stable across configs).
- **Bulk data policy (R16):** any API moving more than a handful of numbers
  uses `buffer` (zero-copy views into engine memory where lifetime allows:
  mesh scratch, particle emitters, net payloads) or vectors — the API schema
  rejects tables-of-numbers.

**Timeouts/interrupts:** `lua_callbacks->interrupt` armed every resumption;
per-resumption watchdog — dev: warn at 10 ms (`script.warn.long_frame`),
hard-kill a runaway script at 1 s with a full traceback; ship: no hard kill by
default, hitch telemetry only. `BindToClose` handlers get their own capped
budget.

**Memory categories (256):** 0 engine-misc/stdlib, 1 require/module registry,
2 bindings/userdata, 3 signals + task scheduler, 4 UI, 5 net buffers,
6 asset/module sources, 7 REPL/debug; 32–255 assigned per Script instance
from a recycling pool (coalesce oldest when exhausted). `lua_setmemcat` wraps
every script entry; `lua_totalbytes(cat)` feeds the DebugShell profiler table
and per-script leak triage. The `lua_Alloc` we install is a thin counting
wrapper over the OS allocator — Luau's internal paged allocator stays (it
beats jemalloc/tcmalloc for this workload; do not replace without
benchmarks). The wrapper also enforces the **optional hard cap on script heap**
(`luaug.toml`, generous default) with a graceful keyed error — Luau imposes no
memory limit of its own. The game VM has no environment-variable or process
access (no `@std/process` — tooling only).

**GC pacing:** per §3 — explicit `LUA_GCSTEP` per frame with a budget derived
from `lua_allocationrate` + frame headroom; goal/stepmul tuned once and
recorded via ADR; full GC only at transitions.

**Error pipeline:** every entry into Luau is a protected resume at a
resumption point. On error: capture `lua_debugtrace`, message, script
instance, memcat → structured `ScriptError { TextKey key or raw message,
args, traceback, source }` → `core::log` (i18n-formatted), a `ScriptContext`
error signal (in-game console/DebugShell), and the CI reporter in headless
mode. Engine-raised errors into Luau always originate from `TextKey`s;
script-authored `error("…")` strings pass through verbatim.

**Hot reload — canonical model: fast world restart** (ADR 0024 and its
2026-08-20 addendum). Watcher events batch → FrameStart safe point →
`PreReload`, drained → capture the explicit state bag + `PreserveOnReload`-tagged
instances → **build the fresh world alongside the old one** → re-run scripts →
restore preserved state → swap → destroy the old VM → `PostReload`. A world that
fails to build leaves the previous one running. Window, GPU resources, imported assets, and engine-materialized
streamed chunks survive; target **< 500 ms** (perf-gated from M3). Shader file
changes re-run shadercross and invalidate pipelines; asset source changes
re-import → content-hash swap through `asset::onAssetInvalidated` — both
without a VM restart. Module-level in-place reload (`__hotreload`) is
post-v1, only if the restart budget fails.

---

## 6. Memory strategy

- **Engine general allocation:** the OS allocator behind
  `core::alloc(MemTag, size)`; every call site passes a tag. No global
  jemalloc/mimalloc in v1 (measure first; the hot paths below don't hit
  malloc).
- **Frame arenas:** per-thread linear `FrameArena` reset at FrameStart
  (extract snapshots, draw lists, temp strings, signal argument staging).
- **Per-chunk arenas (the open-world discipline):** every streamed chunk's
  CPU-side data (instance blobs, decoded meshes pending upload, future nav
  tiles) allocates from that chunk's arena; unload = wholesale arena free.
  Zero fragmentation, O(1) eviction, exact per-chunk accounting.
- **Pools:** signals/connections/waiters (PoolAllocator), ECS component pages
  (16–64 KB pages per pool), the name-atom table (append-only arena).
- **GPU:** upload ring buffers (per-frame fences) + explicit budgets for
  texture/mesh residency managed by the StreamingManager.
- **Luau:** the counting wrapper + memcats per §5; Luau's internal allocator
  untouched; optional hard cap on script heap.
- **Budgets & tracking:** `MemBudget { tag → softLimitBytes }` from
  `luaug.toml` with engine defaults (desktop: streamed content 2 GB, textures
  1 GB, script heap 256 MB soft); counters unified in one DebugShell table:
  engine tags + `lua_totalbytes` per memcat + GPU pools. A soft-limit breach
  logs a keyed warning and raises StreamingManager eviction pressure; **never
  a hard OOM from streaming** — drop to HLOD/lower LODs instead.

---

## 7. Swappable-backend contract pattern

Per ADR 0023: **compile-time inclusion via CMake options + one explicit
hand-written runtime factory; no dynamic libraries, no static-initializer
self-registration.**

- Each seam has an interface target (`luaug_rhi_api`) and impl targets
  (`luaug_rhi_sdlgpu`, `luaug_rhi_capture`, `luaug_rhi_null`,
  `luaug_rhi_bgfx`).
- CMake: `LUAUG_RHI_SDLGPU=ON/OFF`, `LUAUG_RHI_BGFX=…`,
  `LUAUG_PHYSICS_JOLT=…`, `LUAUG_AUDIO_MINIAUDIO=…`, `LUAUG_NET_GNS=…`, etc.
  Only enabled impls compile and link into `app`.
- `app/src/backends.cpp` contains the whole mechanism: `rhi::IDevice*
  createDevice(BackendId)` — a plain `switch` over `#if LUAUG_RHI_*` blocks;
  same pattern for `render::IRenderer*`, `physics::IPhysics3D*`, audio, and
  transports. Runtime selection exists among compiled-in backends
  (`--rhi=capture`) for A/B and test injection.
- Rationale: consoles and iOS want fully static, LTO-dead-strippable
  binaries; self-registering statics break under aggressive dead-stripping
  and make binary-size audits opaque. A ~20-line explicit factory is the
  entire cost, and shipping builds compile exactly one backend per seam.
- **Capabilities policy:** interfaces are coarse; `rhi::Capabilities` exists
  from day one (advanced render paths gate on it). Physics/audio interfaces
  gain capability queries when their second backend actually arrives —
  no speculative fields before then. No caller may assume an optional feature
  without checking.

---

## 8. Build system

- **CMake ≥ 3.28**, presets-first. Configure presets: `win-msvc`, `win-clang`
  (secondary), `linux-clang` (primary), `linux-gcc` (CI-only), `macos-clang`
  (arm64), later `android-clang` (nightly compile job first), `ios-clang`.
  Build presets cross with configs: `debug`, `dev` (optimized + asserts +
  profiler + ImGui + Luau compiler + hot reload), `profile` (dev minus
  asserts), `player` (Release, Luau compiler, **no ImGui and no REPL**),
  `shipping` (LTO, no ImGui/REPL/compiler, bytecode-only, single
  RHI backend). `binaryDir = $env{LUAUG_BUILD_ROOT}/${presetName}` (R14).
  **`player` is what `luaug build` packages, and it exists because the two
  options only ever shared a profile string by accident** (D057): a game ships
  its Luau as SOURCE (ADR 0045), so the binary it ships with must be able to
  compile source, which `shipping` cannot — and the packager was therefore
  handing out `dev` binaries carrying the debug overlay, the inspector and a
  REPL against the game's own VM. The gate builds and links it beside
  `shipping` on every run, because a profile nothing builds is a profile nobody
  knows is broken.
- **Baselines:** C++20 engine code; MSVC 19.40+ (VS2022 17.10), Clang 17+,
  GCC 13+. Vendored deps build with their own standards (Luau: C++11 VM /
  C++17 compiler — its own CMake, wrapped by `cmake/luaug_luau.cmake`).
- **Warnings-as-errors** on `engine/` and `tools/importer/` only (`/W4` +
  curated, `-Wall -Wextra -Wconversion`-curated); `third_party/` is included
  as SYSTEM.
- **Sanitizers:** presets `linux-clang-asan` (ASan+UBSan), `linux-clang-tsan`
  (jobs/audio tests), `win-msvc-asan`. LTO
  (`INTERPROCEDURAL_OPTIMIZATION`) in shipping only.
- **Luau configuration:** vendored pin (0.734; upgraded only via
  `tools/repo/vendor.luau` + ADR), `LUA_VECTOR_SIZE=3`, `LUA_VECTOR_DOUBLE=0`
  (ADR 0013); targets VM+CodeGen always; Compiler+Ast gated by
  `LUAUG_LUAU_COMPILER` (ON in debug/dev/profile/player, OFF shipping); **Analysis is
  never built** — type checking is `luau-analyze` at the pinned toolchain
  version (ADR 0018), a tool rather than a runtime dependency. This line said
  "Compiler+Analysis gated" until M4; Analysis was in fact linked under no
  profile at all, and upstream built it on every build anyway because its
  CMakeLists creates all twelve libraries unconditionally. `cmake/luaug_luau.cmake`
  now adds the directory `EXCLUDE_FROM_ALL`, which took a cold build from 53.9 s
  to 34.8 s — Analysis alone was 35% of the compile time.
  `luaug_add_luau_bytecode()` precompiles `runtime/` + project scripts at
  pack time with the §5 options.
- **Shaders in the build:** `luaug_add_shaders(target GLOB shaders/src/*.hlsl)`
  → custom commands invoking SDL_shadercross →
  `content/shaders/{spirv,dxil,msl}/…` **beside the target's executable** +
  a shader manifest (and a `reflect/` sidecar per stage, carrying the resource
  counts SDL_GPU needs at shader-creation time and the input locations it needs
  at pipeline creation — without it the runtime would hardcode numbers only the
  shader source knows). Beside the executable rather than at the binary-dir
  root because `platform::paths()` derives the content directory from the
  running binary, which is the shape a packaged build has; the message catalog
  is staged the same way. The runtime loads per active backend; `luaug dev`
  re-runs the same command for shader hot reload. shadercross builds once as a
  host tool (also used when cross-compiling) — and **DirectXShaderCompiler is
  fetched and hash-pinned rather than vendored** (ADR 0032), which is also why
  a macOS host has no shader toolchain: Microsoft publishes no macOS build.
- **Packaging:** `luaug build` (CLI) produces: the platform `luaug-host`
  (shipping preset) + `game.lpack` (content-addressed assets, chunk
  manifests, bytecode, shader packs, `en.json` catalog) + launcher config.
  The client runtime contains no Luau compiler, no assimp, no analysis, no
  ImGui.

---

## 9. Testing & CI

- **C++ unit tests: doctest** (header-only, fast compiles, subcases fit
  table-driven engine tests; no mocking framework needed). One test exe per
  module (`luaug_core_tests`, …) + `tests/integration/` against the headless
  host.
- **Luau conformance/behavior tests:** `tests/conformance/**.spec.luau`
  written against `require("@luaug/testing")` (describe/it/expect, in
  `runtime/luaug/testing/`), executed by `luaug test` → headless
  `luaug-host --headless --run-tests`, so specs exercise the REAL scheduler,
  signals (deferred order, re-entrancy limit 10), Instance semantics
  (including duplicate-name lookup per ADR 0026), task lib, streaming
  reparent behavior, and IAS. Pure-logic runtime modules also run under Lute
  for fast iteration; the engine run is the gate. The `@std` conformance
  suite runs against both Lute and the engine (ADR 0030).
- **Determinism tests** (ADR 0025): `tests/determinism/` scenarios = world
  seed + recorded tick-stamped input stream; the harness runs each scenario
  twice in-process and once from a fresh process, comparing `WorldHash`
  (xxh3 over canonical serialization of sim-relevant components + physics
  state) every N ticks. **Guarantee under test: same build + same platform +
  same seed/inputs/tick-config ⇒ same WorldHash.** Same-binary determinism is
  a merge gate from M5; cross-platform (win↔linux) comparison runs as a
  tracked non-blocking job.
  **A committed trace is a cross-BUILD check and the guarantee is same-build**,
  which is free for integer and tree state and is not for floating point: a
  scenario whose hash depends on the compiler's code generation sets
  `sameBuildOnly` in its manifest, carries no trace, and is verified by three
  runs of one build plus tolerance-based assertions inside the scene. M5's
  character replay is the first of those, and CI is what found the distinction.
- **Render tests — capture first:** the `rhi_capture` backend records a
  canonical JSON command stream (pipelines, bind sets, draw params, resource
  descs, debug groups; floats quantized) → hash vs golden per scenario. This
  is the deterministic, GPU-less **blocking** gate. A small real-image golden
  suite (lavapipe on Linux, WARP/D3D12 on Windows) runs nightly,
  non-blocking, with per-pixel tolerance (`tools/imgcmp`). Screenshots remain
  the agent's own verification tool from M1 (roadmap).
- **Static gates:** clang-format check, curated clang-tidy, StyLua for all
  Luau, `luau-analyze --mode=strict` (new solver) over `runtime/`, `api/`,
  `tools/`, `templates/`, examples; the layer checker; API-gen freshness
  diff; the i18n key inventory vs `en.json`.
- **The gates run locally first.** `scripts/localgate.ps1` runs everything the
  dev machine can run: the documentation gate, the Luau gates, the Windows
  build and tests, and the Linux tier inside the container
  `scripts/docker/tier2.Dockerfile` builds. Both callers execute the same
  `scripts/gates/*.sh` rather than a transcription, because a transcription
  drifts until "it passes locally" means nothing. Only macOS cannot run here.
  This is the "scripted local gate" the roadmap already allows, generalised:
  the repository is private, so hosted minutes carry platform multipliers, and
  a portability break found in seconds on the machine that caused it is worth
  more than the same break found later on a meter.
- **CI (GitHub Actions):** `ci.yml` on PR/main — jobs: `changes` (decides
  whether a push can affect a build at all; a documentation-only push skips the
  build jobs, and anything it cannot classify builds); `docs-lint`;
  `luau-check`; `build-test` matrix {windows MSVC, ubuntu Clang}. `build-macos`
  is compile-only and **blocking on every code push from M4**, which is that
  milestone's own gate item. It ran on `workflow_dispatch` or a `milestone/*`
  tag until then — a deliberate cost decision, since macOS minutes are charged
  at 10× and Tier-3 runtime verification is post-v1 — and the cost of that was
  giving up finding a macOS-only break at the commit that caused it. M4 added a
  shader toolchain, a new module and the first code with no Windows equivalent,
  which is where that trade stopped paying. The `changes` filter still skips it
  for a documentation-only push. Planned and not yet present:
  `sanitize` (ubuntu ASan/UBSan), `determinism` (win+linux), conformance
  bundled into build-test. `nightly.yml`: the Android NDK cross-compile job
  (non-blocking, from M1–M2); planned there too are the GCC build, TSan, image
  goldens, the openworld perf smoke and packaging artifacts.
  **Cache:** sccache backed by `actions/cache`, plus the hash-pinned DXC archive
  (ADR 0032) keyed on `hash(manifest.json)`. Two caches with opposite rules, on
  purpose: the DXC key is exact with no restore-keys because it holds a real
  artifact, and it is re-verified against its SHA256 on every configure; the
  sccache key is a loose heuristic because sccache addresses entries by a digest
  of preprocessed source plus command line, so a stale entry can only fail to be
  found, never be served wrongly.
  **`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded` is load-bearing on Windows:**
  CMake gives every object in a target the same `/Fd<target>.pdb`, and sccache
  declines any compilation whose PDB already exists — silently, recording no
  reason. With the default `/Zi` the measured hit rate is exactly zero.

---

## 10. Open-world tech plan

- **Coordinates** (ADR 0014): ECS `TransformComp` stores `CFrameD` (f64
  position, f32 rotation). Render and physics operate in f32 **rebased local
  space**: a per-World `OriginRebase` (current f64 origin) shifts when the
  primary focus moves > 4 km from it; the rebase rewrites the resident f32
  mirrors (physics bodies via velocity-preserving teleport, render caches) at
  a FrameStart safe point in one budgeted pass. Scripts always see true world
  coordinates: `CFrame` carries the f64 position; `Vector3` (f32) remains the
  vector math type, so `part.Position` is exact to ~131 km and documented
  lossy beyond — accumulated error never grows because the source of truth is
  f64. Rebasing is invisible to Luau. **Origin/rebase is World-scoped state,
  never a global** — `IPhysics3D` is already multi-world, so a future server
  can run N simulation regions with independent origins without rework.
- **Chunking:** uniform world grid, default cell 256 m
  (project-configurable), addressed by `ChunkId{i32 x, z, layer}`. The import
  pipeline (`luaug asset` → `tools/importer`) slices authored content (glTF
  worlds + `.prefab.luau` declarative instance trees) into per-chunk
  payloads: serialized instance blobs, mesh/texture references by
  `ContentHash`, future nav inputs, plus a generated **HLOD** merged-imposter
  mesh per chunk. Everything content-addressed (BLAKE3) — dedupe across
  chunks for free, and hot reload is a hash swap.
- **Streaming pipeline:** the `StreamingManager` scores chunks by focus
  distance vs the Min radius (must-have ring, integrity-guaranteed) and
  Target radius (best-effort ring), with hysteresis between load/evict
  thresholds. Flow per chunk: `SDL_AsyncIO` read (IO priority = score) → jobs
  decode (meshoptimizer index/vertex decode, basis→GPU-format transcode) into
  the chunk arena → FrameStart budgeted **materialization** (instance
  construction through the same reflection path as scripts, ≤ 2 ms/frame) →
  GPU residency upload via rings. Eviction reverses it honoring §4 semantics
  (reparent-to-nil husks for Luau-referenced instances, `Persistent` immune)
  and frees the chunk arena wholesale. Memory pressure (§6 budgets) first
  drops far-ring chunks to HLOD-only, then shrinks the target ring.
- **LOD strategy:** per-mesh LOD chains generated at import via meshoptimizer
  simplification (fixed error targets), runtime selection by projected screen
  error; meshlet data emitted at import from day one (feeds the future
  GPU-driven path behind an RHI capability flag) while v1 renders classic
  indexed LODs. Chunk-level HLOD swaps in beyond the target radius so the
  horizon stays populated at fixed cost.
- **Integrity:** the sim never consults non-resident state; queries clamp to
  resident chunks; the min-radius ring is guaranteed resident before the
  character may advance into it (a familiar "streaming pause" rather than
  falling through the world).

---

## 11. Build-out order and top risks

The canonical sequence, gates, and sizes live in [`roadmap.md`](roadmap.md)
(M0–M8). Within milestones, module build order follows the layering: core →
platform/jobs → rhi → scene → script → render → physics → input/ui/tween/
audio/animation → asset/streaming → flagship.

**Top architecture risks and their mitigations:**

1. **Instance-facade overhead negating ECS wins** (property churn through
   reflection, signal storms). → Atom dispatch + generated accessors (no
   string lookups), subscription bitmasks + quiet-write batching,
   buffer/vector-only bulk APIs enforced by the schema, and the
   10k-parts/1k-listeners benchmark with CI thresholds from M2.
2. **Determinism erosion** (Jolt config, float variance, Luau pointer-keyed
   iteration, wall-clock leaks). → Determinism as a merge gate from M5
   (ADR 0025); sim-authoritative state in components; seeded `Random`; R10
   discipline; cross-platform tracked separately from the same-binary
   guarantee.
3. **SDL_GPU feature ceiling** for GPU-driven open-world rendering. → v1
   renderer is classic forward+ with CPU culling and indexed LODs (well
   within SDL_GPU); meshlet data already in assets; bgfx as the planned
   second backend; RHI capability flags gate advanced paths.
4. **Hot-reload complexity creep.** → Scope frozen at ADR 0024 (fast world
   restart + state bag); no live upgrade of instances/closures; anything
   fancier is post-v1.
5. **Agent drift from spec.** → ADRs + the IDL single source of truth +
   conformance/determinism/layer/apigen gates make silent divergence fail CI.
6. **Luau upgrade churn** (weekly releases, ABI/type-tag instability). →
   Pinned vendor + patch dir; upgrades only via `tools/repo/vendor.luau` PRs
   run through the full matrix; never persist `lua_Type` tags; bytecode cache
   keyed by Luau version.
7. **SDL3 GPU Android maturity.** → Nightly NDK compile job from M1–M2, human
   device checkpoint before the RHI freeze (end of M4), bgfx hedge
   (ADR 0005).

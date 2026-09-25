# 0091 — A material may name a surface shader the user writes, and the editor ships the compiler

- Status: accepted
- Date: 2026-09-24
- Extends: [0090](0090-a-material-is-an-asset-a-part-wears-one-and-a-script-clones-one.md)
  (a material is an asset). Answers what 0090 left under *Not decided here*: "a
  material that names its own shader".
- Amends: [0032](0032-binary-toolchain-artifacts-fetched-not-vendored.md)
  (DXC is fetched and never redistributed) -- the editor now ships a DXC this
  project built from source. [0006](0006-hlsl-via-sdl-shadercross.md) (HLSL via
  SDL_shadercross) is unchanged; it gains a second caller.
- Closes: the roadmap's M4 carried item *"a material must be able to name a
  shader that is not the default PBR one"*, whose named caller was
  vertex-displaced water.
- Decided by: the owner, 2026-09-24, over a design the agent proposed:
  *"temos que deixar isso de maneira profissional como outras game engine"*, then
  *"muito bem concordo"*. The owner approved the new dependency this record
  introduces (a from-source DXC build, redistributed with the editor) in that
  same message.

## Context

The owner asked for an ocean rendered on the GPU, then corrected the ask: *"eu
queria que fosse algo que o usuario poderia desenvolver saca então temos que
pensar em uma classe base para essas coisas que ai a implementação pode ficar
por conta do usuario"*. An engine-owned `Ocean` class answers the first request
and not the second.

`examples/11-ocean` shows why the second matters. It is 676 parts that Luau
moves every tick. Its own README measures the ceiling: about 4.7 µs per property
write, so a thousand moving parts cost five milliseconds before anything is
drawn, and its colour ramp is quantised to twelve materials because a distinct
colour per tile is 3,629 draws. **No amount of engine work on that example
removes the ceiling**, because the ceiling is the design. The water has to be a
static grid displaced where it is drawn.

All three engines this design is measured against give their users the same
thing, and none of them gives an ocean class as the primitive:

| | Language | Visual layer | Compiler |
|---|---|---|---|
| The two most-used commercial engines | HLSL | a node graph over the same code | shipped with the editor |
| The most-used open-source engine | its own GLSL-like language | a node graph over the same code | shipped with the editor, permissively licensed |

Every one of them has the same three properties: **two tiers, where the graph
generates the code**; **the compiler travels with the editor on every desktop
platform**; and **a shipped game never compiles a shader** -- it carries
bytecode prepared at build time.

### What stood in the way, and what no longer does

This engine compiles HLSL only at its own build, through DXC and
SDL_shadercross (ADR 0006). ADR 0032 decided DXC is **fetched and never
redistributed**, because Microsoft's prebuilt archives carry a proprietary
`LICENSE-MS.txt` beside the NCSA licence their own release notes name. So a
user's editor could not compile a user's shader.

Two facts, verified for this record, remove that:

- **DXC's source is NCSA, with no Microsoft terms.** The upstream
  `LICENSE.TXT` is the LLVM release licence (University of Illinois/NCSA), with
  permissively licensed third-party parts (MIT, BSD-3, public domain). The
  proprietary EULA is in the **prebuilt archives** and nowhere in the source. A
  binary this project builds from that source is redistributable under NCSA,
  with attribution.
- **A source-built DXC now produces DXIL that D3D12 accepts.** It used to need
  `dxil.dll`, which was partly closed source, to sign its output. D3D12 rejects
  unsigned DXIL outside Developer Mode, and `cmake/luaug_dxc.cmake` stages
  `dxil.dll` for exactly that reason. DXC **v1.8.2502** open-sourced the
  validator hash and stopped requiring the validator binaries for signed
  shaders. **v1.8.2505** made the compiler always use its internal validator.
  The pin is **v1.9.2602**. This is recorded as `U-64` in
  `docs/research/UNCONFIRMED.md` and verified by Stage 0 of the brief on a
  retail Windows machine with Developer Mode off, before anything depends on it.

A from-source DXC also answers ADR 0032's other gap: **there is no macOS DXC
binary** from Microsoft, and SDL_shadercross's own CI builds DXC from source on
macOS for that reason. Building it ourselves gives a macOS editor a compiler.

## Decision

### Two tiers, and the code tier comes first

A material asset may name a **surface shader** with `"shader": "<content URN>"`.
A node graph is the second tier and is not built here: it will generate the
same surface shader, so building the code tier first means the contract is
designed once.

### A surface shader is two functions, not a pipeline

The user writes HLSL against `luaug/surface.hlsli`, which is a versioned public
API from its first release:

```hlsl
#include "luaug/surface.hlsli"

LUAUG_PARAM(float,  WaveHeight, 1.0,  "range(0, 10)")
LUAUG_PARAM(float3, DeepColor,  float3(0.02, 0.10, 0.20), "colour")
LUAUG_TEXTURE(FoamMap)

void luaugVertex(inout SurfaceVertex v, SurfaceInputs i)
{
    v.positionWorld.y += gerstner(v.positionWorld.xz, i.simTime) * WaveHeight;
    v.normalWorld = gerstnerNormal(v.positionWorld.xz, i.simTime);
}

void luaugSurface(inout SurfaceOutput o, SurfaceInputs i)
{
    o.baseColor = DeepColor;
    o.roughness = 0.05;
    o.opacity   = 0.85;
}
```

**The engine builds every pipeline the surface needs from those two functions**:
the forward pass, the instanced forward pass, the shadow and depth passes, and
the blended variant. So lights, cascaded shadows, IBL, fog and the post chain
apply to a user surface without the user writing any of them, and a shadow
follows the displaced vertex rather than the flat one. The user never sees a
binding layout, a vertex format or a backend, so R17 holds and the RHI stays
frozen (ADR 0037).

- `luaugVertex` is optional. With no vertex function, the surface is not
  displaced.
- `luaugSurface` writes a `SurfaceOutput` whose fields are the PBR inputs the
  built-in shader already reads: base colour, opacity, normal (tangent or
  world), metalness, roughness, occlusion and emissive. The built-in material is
  expressible as a surface shader, and the brief's Stage 2 proves this by
  writing it as one and requiring the goldens to hold.
- `SurfaceInputs` carries **the simulation time, interpolated to the frame**
  (the same clock `Workspace` ticks on, so a GPU wave and a Luau wave agree on
  screen), the camera, the object's transform, world position, normal, tangent,
  UV0 and UV1, vertex colour, and screen position.
- **Scene depth** is readable from a blended surface. That is what intersection
  foam needs, and the roadmap has named it since M4.5.
- **Scene colour** is readable when the material asks for it
  (`"readsSceneColor": true`), for refraction. It costs a copy of the HDR target
  after the opaque pass, made by a full-screen draw the way ADR 0072 closes the
  pass for soft particles, so the RHI does not change. The copy is made only in
  a frame where some visible material asks for it, and once per frame however
  many do.
- `#include` of a `.hlsli` in the project's own `content/` works, so a project
  can share its own functions between shaders.

### Parameters are material fields

`LUAUG_PARAM(type, Name, default, "annotations")` and `LUAUG_TEXTURE(Name)`
declare the shader's inputs. The compiler reflects them. They appear in the
material panel as typed fields: a number with a range, a colour, a vector, a
texture picker, a toggle. They are written in the material asset's `properties`
like any other field. **They are also eligible for `instanceParameters`**, so
ADR 0090's closed list gains whatever parameters the material's shader
declares, and a part may override `WaveHeight` exactly as it overrides `Color`.
From Luau, a shader parameter reads and writes through the same members as a
built-in one, with a clone writable and a loaded asset read-only (ADR 0090).

### Compiled in the editor, never in a game

- `assetc` compiles a surface shader to SPIR-V, DXIL and MSL for every pass
  variant, through the same SDL_shadercross path the engine's own shaders take.
  The result is cached by content hash and stored in the pack beside the
  material that uses it.
- **The editor compiles asynchronously.** While a shader compiles, a surface
  draws with the engine default material, and the frame does not wait. A save
  recompiles and reloads it, as ADR 0062 says a changed asset does.
- **A shader that fails to compile draws with an error material**, one flat,
  unmistakable colour and not the default look, so nobody mistakes it for a
  working surface. The editor shows each error with its file and line.
- **An exported game carries bytecode only.** It never loads the compiler and
  never compiles.

### The editor ships a DXC this project builds

DXC is built from source at the pinned version, **by CI, once per version and
per desktop platform** (Windows x64, Linux x64, macOS arm64 and x64), and the
result is cached so that no ordinary build ever compiles LLVM. The editor
package carries `dxcompiler` and SDL_shadercross beside the editor binary.
`THIRD_PARTY_NOTICES.md` carries DXC's NCSA notice and the notices of the
third-party parts its licence lists, generated from the manifest as for any
other row.

The developer build keeps ADR 0032's fetch of Microsoft's prebuilt archive for
compiling the engine's own shaders, because it is small and fast. **Only the
source-built binary is ever redistributed.** The manifest records both rows and
which one is which.

### The ocean is user code, and the example proves the contract

`examples/11-ocean` is rewritten using public pieces only: a material asset
naming a surface shader, a subdivided grid mesh, a few `MeshPart`s that follow
the camera, and the same wave written in Luau for buoyancy. **If it needs
anything the public contract does not give it, the contract is incomplete**,
and the fix goes into the contract, not into the example.

The engine provides the grid as a built-in mesh (a subdivided plane at a few
fixed resolutions), because every displaced surface needs one and none should
need a modelling tool.

The CPU copy of the wave is the user's to write. The alternative is a way for
Luau to read the GPU's result, and that is a GPU readback on the simulation's
critical path, which R10 cannot allow: the simulation would then depend on a
GPU. The example ships the two copies side by side, and the manual says
plainly that they must agree.

## Consequences

- **HLSL becomes a public API**, and so does `surface.hlsli`. Its contract is
  versioned under semantic versioning. A change that breaks a user's shader is a
  major-version change, as a change that breaks a user's script is.
- **Pipelines multiply.** Every surface shader is several pipelines (forward,
  instanced, shadow, depth, blended) on every backend. The brief measures
  pipeline creation and the pack size per shader. User-defined keywords and
  variants are left out of this record on purpose, because they are where the
  variant count explodes in the engines this follows.
- **The editor package grows** by DXC and shadercross (tens of MB per platform),
  and the release pipeline gains a cached LLVM build per platform.
- **A GPU hang is possible from user code.** An unbounded loop in a surface
  shader can reset the device, as it can in every engine that runs user
  shaders. The sandbox (R4) governs scripts, not shaders. The manual says so,
  and the editor recovers from a device loss rather than crashing.
- **The roadmap's M4 carried item closes**, and so does the part of ADR 0090
  that deferred it.

### Not decided here

- **The node graph.** It is the second tier, and it generates this contract.
  It gets its own record.
- **Compute shaders.** The RHI has no compute (ADR 0037). A Gerstner ocean does
  not need it. An FFT ocean, GPU particles and GPU culling do, and adding compute
  is an RHI change with its own record.
- **User keywords and variants**, **tessellation** and **geometry shaders**.
- **Post-process shaders written by users.** They are a different contract (a
  full-screen pass with the scene as input). The 2026-09-24 mandate's
  post-processing API is the place to decide whether users get one.
- **Mobile.** Phase 5 is closed (R15). The contract does not preclude it, and
  R16 does not bear on shaders.

### Rejected

- **An engine-owned `Ocean` class.** It answers the first request and not the
  owner's correction. Every other displaced or custom surface would need its own
  class.
- **Full user shaders instead of two functions.** A user shader that owned its
  whole pipeline would have to reimplement lighting, shadows and fog, would see
  binding layouts (R17), and would break whenever the forward pass changes.
- **Asking each user to fetch DXC**, as ADR 0032 has each engine developer do.
  It is what makes an engine feel like a toolchain, it leaves macOS with no
  compiler at all, and the engines this is measured against do not ask it.
- **glslang with an HLSL front end.** It is permissive, but its HLSL coverage
  trails DXC's, and D3D12 needs DXIL, which only DXC emits. It would be a second
  compiler with a second set of bugs for the same language.
- **Our own shading language.** It would mean designing, documenting and
  maintaining a language, when HLSL is what the largest share of users already
  write.

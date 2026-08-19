# 0005 — Custom RHI; SDL3 GPU default backend; bgfx in the mobile phase

- Status: accepted
- Date: 2026-08-19

## Context
The renderer backend must be swappable (core user requirement). Candidates:
SDL3 GPU (modern, lean, Vulkan/Metal/D3D12 + compute, zero marginal dependency
since SDL3 is already in the stack, but Android support officially "limited");
bgfx (battle-proven iOS/Android incl. GLES2 for low-end devices — the real
audience of a Roblox-like — plus NVN/GNM console paths, but a large dependency
with an older API model); wgpu-native (unstable C ABI, Rust toolchain, no
console path); Diligent (Metal backend licensing unconfirmed); NVRHI (no
Metal/mobile).

## Decision
Build a **custom RHI (~40 calls)** with LuauG semantics over the concepts
common to Vulkan/Metal/D3D12 (Device, Buffer, Texture, Sampler, Shader,
Pipeline, CmdList, RenderPass, Compute, Swapchain, Capabilities). The shape
stays deliberately close to SDL_GPU so the default adapter is thin, but **no
SDL type appears in `rhi_api` headers**; neutrality is validated by the
`rhi_capture` (CI) and future bgfx backends. **SDL3 GPU is the default backend
for the desktop MVP**; **bgfx is the second backend, implemented in the mobile
phase** (post-v1) as the Android low-end hedge and console path. `rhi_null`
and `rhi_capture` exist from M1 for headless/testing. User decision #13.

## Consequences
Desktop v1 ships on a lean, modern backend; the two-backend plan keeps the
abstraction honest. Risk (register): SDL3 GPU Android maturity — mitigated by
the nightly Android cross-compile job from M1–M2, a human device checkpoint
before the RHI freeze at end of M4, and bgfx as the fallback. v1 renderer
stays within SDL_GPU's feature envelope (no bindless/mesh shaders); advanced
paths gate on `Capabilities`.

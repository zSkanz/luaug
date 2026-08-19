# 0027 — IRenderer contract with RenderWorld as the stable boundary

- Status: accepted
- Date: 2026-08-19

## Context
The RHI makes the *graphics API* replaceable, but the *renderer architecture*
(forward+, deferred, GPU-driven) is a separate axis of replaceability the user
requirement implies. A heavyweight renderer plugin system would be premature;
no seam at all would be a dead end.

## Decision
Formalize the boundary that already exists: **`IRenderer` consumes a
`RenderWorld`** (the POD snapshot extracted from the scene each frame, with
interpolated transforms — the stable data contract) **plus an `rhi::IDevice`**.
`renderer_default` (v1) is the classic forward+ pipeline (CSM → depth →
clustered forward → sky/fog → tonemap → ui2d → imgui, with a reserved 2D pass
slot). Alternative renderers are **build-time selections like any backend**
(ADR 0023) and may not touch `scene` or `rhi` internals. Meshlet data emitted
at import (ADR 0010) keeps the GPU-driven path open behind RHI capability
flags.

## Consequences
Renderer evolution without engine surgery, at the cost of one interface and
one snapshot struct — both already required by the extract/render split (and
by the future render-thread seam).

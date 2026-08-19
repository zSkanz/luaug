# tools/ — Tooling (Lute + Offline C++)

- `cli/` — the user-facing `luaug` CLI, written in `--!strict` Luau and run
  under the pinned, unmodified Lute (ADR 0003); shipped via `lute compile`.
  Command set per `docs/api-design.md` §4:
  `new · dev · run · build · asset · test · check · fmt · setup · add · doctor`.
- `repo/` — repo-internal tools (also Lute): `vendor.luau` (third_party
  import per manifest, ADR 0021), `checklayers.luau` (layer-rule CI gate),
  apigen wrappers, docs-lint.
- `importer/` — the offline C++ asset importer; the ONLY place assimp exists
  (ADR 0010). Normalizes exotic formats to glTF 2.0, then runs the standard
  pipeline (meshoptimizer LODs/meshlets, KTX2/basis textures,
  content-addressed output).
- `imgcmp/` — screenshot tolerance comparator used by the golden-image
  nightly and the agent's self-verification.
- `bootstrap/` — helpers used by `scripts/bootstrap.*`.

All CLI/tool user-facing strings go through `tools/cli/i18n/` catalogs
(rule R3). Populated starting at M0 (`repo/`) and M3 (`cli/`).

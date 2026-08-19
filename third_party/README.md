# third_party — Vendored Dependencies

Policy (ADR 0021): full upstream source trees are vendored here, governed by
[`manifest.json`](manifest.json), imported/updated exclusively by
`tools/repo/vendor.luau` (run under the pinned Lute). No git submodules, no
FetchContent.

Rules:
- **Never edit vendored files in place** (rule R13). Local changes are
  `.patch` files under [`patches/`](patches/), listed in the manifest, and
  re-applied by the vendor tool on every import. Keep the patch set as close
  to empty as possible.
- Every manifest row records: exact version, upstream commit SHA, license,
  vendored path, patch list. `THIRD_PARTY_NOTICES.md` (repo root) is
  regenerated from the manifest in the same commit as any change here.
- Adding or upgrading a dependency = human-approved ADR first (rule R5),
  then a vendor PR run through the full CI matrix.
- Permissive licenses only (rule R6).
- Lute itself is NOT vendored — it is a rokit-installed binary (ADR 0003).

Current state: manifest rows carry target versions; source trees and real
SHAs land at milestone M0 (`docs/roadmap.md`).

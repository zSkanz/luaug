# api/ — The Typed IDL (Single Source of Truth)

Every public class, service, datatype, and enum is declared once here and
generated everywhere else (`docs/api-design.md` §5, `docs/architecture.md` §4):

- `schema.luau` — the typed schema the definition files are validated against
  (by the Luau type checker itself).
- `defs/*.api.luau` — one file per class/service: properties (type, default,
  read-only, **threadSafety**, docKey), methods (overloads; yields ⇒ `Async`
  suffix enforced), events, enums. Doc prose is authored inline (English) with
  auto-derived i18n keys.
- `generator/` — Lute-run codegen: C++ ClassRegistry descriptors + binding
  glue, `runtime/types/*.d.luau` (`declare extern type`), luau-lsp docs JSON,
  the versioned `api-dump.json` (CI-diffed to catch accidental breaks),
  reference pages, the i18n key inventory, and the naming-convention lints
  (CI gate).

The schema **rejects tables-of-numbers parameters** (rule R16 — bulk data goes
through `buffer`/vectors). Never edit generated outputs by hand.

Populated starting at M2 per `docs/roadmap.md`.

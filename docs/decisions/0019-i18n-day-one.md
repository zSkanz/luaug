# 0019 — i18n from day one: key + catalog, English-only launch

- Status: accepted
- Date: 2026-08-19

## Context
User decision #10: multiple languages must be supported eventually, launching
English-only, and adding a language must be trivial. Retrofitting i18n over
hardcoded strings is notoriously expensive.

## Decision
**No hardcoded user-facing strings anywhere** — engine errors, CLI/tooling
output, and the future editor all route through a central key+catalog system:
flat JSON per locale, dotted area-first keys (`engine.physics.err.invalid_shape`),
`{param}` placeholders, CLDR plural categories. C++ raises `(key, params)`
records; a formatter resolves against loaded catalogs (fallback: locale → `en`
→ raw key echo + one-time dev warning). Errors reaching Luau are prefixed with
their key so tests match on keys, not prose. **Adding a locale = adding a
catalog file, zero code changes.** Games get a familiar `LocalizationService`
(`Locale`, `Translate(key, params?)`, `LoadCatalog`, `LocaleChanged`). An i18n
lint (CI) rejects user-facing string literals in C++ and CLI code paths.

## Consequences
English-only at launch with structurally guaranteed translatability.
Complex-script text shaping is a documented gap (HarfBuzz seam, post-v1).

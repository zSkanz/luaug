# templates/ — `luaug new` Project Templates

Three templates, per `docs/api-design.md` §4 and §8:

- `starter/` — the minimal tree: `luaug.toml`, strict `.luaurc`, pinned
  `rokit.toml`, zero-config `.vscode/`, one entry script, one shared module,
  one test.
- `obby/` — the idiom teacher: tags + `TagService` signals, `CharacterBody`
  respawn, IAS jump action, tweened platforms, `Signal.new`, a HUD, localized
  strings, a `.prefab.luau`.
- `openworld-demo/` — the flagship project (roadmap M8), streaming + camera
  rig + day/night + hot-reload workflow; also serves as `examples/10-open-world`.

Templates are analyzed in CI under the pinned luau-lsp (all `--!strict`).
Populated starting at M3 (`starter`), M6 (`obby`), M8 (`openworld-demo`).

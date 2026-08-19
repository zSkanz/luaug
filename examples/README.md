# examples/ — Always-Runnable Milestone Artifacts

Numbering matches `docs/roadmap.md`; every example is a real project plus an
automated headless gate script (screenshot/capture + asserted behavior):

| Example | Born in | Proves |
|---|---|---|
| `00-clear` | M1 | window, RHI clear, debug draw, Luau-driven visuals, screenshot harness |
| `01-instances` | M2 | Instance tree over ECS, deferred signals, task, 500 scripted cubes |
| `02-meshes` | M4 | glTF loading, PBR, shadows, camera, day/night slider |
| `03-physics-playground` | M5 | Jolt bodies, contacts→Touched, CharacterBody, third-person camera |
| `04-obby` | M6 | IAS input, UI, tweens, audio, minimal animation — playable end-to-end |
| `05-streaming` | M7 | chunk streaming, floating origin, LOD/HLOD, memory ceilings |
| `10-open-world` | M8 | the v1 flagship: streamed open world + character + day/night + hot reload |

Assets used by examples must be permissively licensed and recorded in
`THIRD_PARTY_NOTICES.md`. Keep binary assets tiny until the git-LFS ADR (M4);
the streaming example generates its world procedurally for this reason.

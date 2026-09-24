# Orbit shell refresh

Date: 2026-09-23. The user requested a modern, clean editor and launcher that
match the new Orbit icons and Orbit G identity, with icons added where useful.
This supersedes ADR 0056's square-corner visual requirement; its centralized
theme data, persistent theme IDs, scaling and contrast rules remain in force.

## Design prompt

> Refresh the LuauG development interface to match Orbit and Orbit G. Use calm
> graphite surfaces with mint accents in dark mode, off-white surfaces with
> deep teal accents in light mode. Keep the viewport and authored content the
> focus. Use compact rounded controls, slightly larger container radii, quiet
> dividers, generous but practical spacing and the existing Inter typography.
> Avoid borders around every button, excessive accent fills and decorative
> graphics inside work panels. Reserve strong color for selection, interaction
> and the primary action. Use familiar Orbit icons beside labels in navigation
> menus and project actions; preserve text, shortcuts, tooltips, disabled states
> and keyboard behavior. Keep docked panels flush and existing layouts intact.
> Show the Orbit G symbol in the editor menu bar and launcher header. Retain
> the 4.5:1 foreground contrast floor and the existing per-display UI scaling.

## Implementation

- `ui_theme.h` defines shared metrics: 5-pixel controls, 8-pixel containers,
  14 by 12 window padding, 10 by 6 frame padding and 4-pixel docking gutters.
- `ui_theme.cpp` maps those metrics and Orbit colors into ImGui. Theme IDs
  remain `dark` and `light`; display names are Orbit Dark and Orbit Light.
  Filled controls lose their redundant outline; popup/container boundaries
  retain a fine border. No new theme dependency or font is required.
- The File, Edit and Window menus pair icons with readable labels. Existing
  transport, content and dock-tab icons continue using the same atlas.
- Labeled icon controls paint without submitting another ImGui item, retaining
  native button/menu focus, shortcuts, tooltips and disabled behavior. When an
  atlas is unavailable they remain ordinary text controls.
- The launcher owns and releases its icon atlas, uploads after swapchain
  acquisition, and uses it for section headings, project folders and actions.
  Recent-project rows retain the existing open/remove behavior, with more space
  and clipping that keeps long paths out of the Remove control.
- The in-app Orbit G is a vector drawing using the geometry of
  `branding/luaug-mark.svg`. Its ink comes from the active theme accent.

## Maintenance

Change the shared tokens rather than introducing panel-specific colors. Keep
the theme contrast tests and inspect launcher/editor at normal and increased
UI scale in both themes. The native application must be rebuilt to see these
changes; modifying the source does not patch an installed release.

The editor viewport, scene lighting and game UI are separate from the shell
palette. This refresh changes the development interface.

## Usability and QA pass

The follow-up request explicitly includes every editor window, spacing,
closing/reopening panels and permission to reorganize panels where useful.
Keep task-specific panels (Properties, Terrain, Blocks, Console, Debug and
Stats); they can be docked or floated independently. Avoid fragmenting a
single property editing task into more windows.

Additional reusable design prompt:

> Audit the editor from the perspective of a first-time user. Make closing,
> reopening and cancelling discoverable. Give every closable tab one visible
> X and every modal an X plus Escape with cancellation semantics. Keep panel
> identity, icons and labels consistent. Adapt fields, toolbars and palettes
> to available width; wrap explanatory text. Preserve keyboard shortcuts,
> docking layouts, undo and unsaved-work safeguards. Use readable empty states
> and theme-aware status colors. Prefer an existing task panel over a new
> window unless separating it makes the user's workflow clearer.

| Area | Changes / review |
| --- | --- |
| Docked windows | Icons on all editor panel tabs; always-visible per-tab close buttons; removed the redundant group-wide close button. Window menu reopens panels and retains Reset Layout. Stable window IDs preserve docking. |
| Explorer and Content | Shared search styling, existing hierarchy/file actions retained; inspected native layout. |
| Properties | Labels wrap; narrow panels stack editors below labels instead of squeezing both columns. Numeric composite rows follow the same layout. Clearer empty state. |
| Viewport | Tool controls wrap to another row. New/Save stay together; camera instructions move into the mode tooltip. |
| Terrain | Sculpt actions wrap; creation/paint descriptions wrap; palette column count adapts to available width. |
| Blocks | Operation buttons wrap and palette columns adapt instead of fixing six swatches per row. |
| Console | Theme palette replaces hardcoded pastel log colors that were unreadable in light mode. Search icon and horizontal scrolling for long log lines. |
| Debug | Transport buttons wrap when the panel is narrow. Kept alongside Console, with its own tab/icon. |
| Script editor and Stats | Inspected tab identity, closing, code area and statistics layout in the native application. |
| Dialogs | Shared sizing respects available work area and scales with UI size. Taller dialogs scroll; button rows wrap. Preferences and Project Settings use labels above full-width fields. |
| Launcher | Theme switch in header; project/create sections stack below 900 scaled pixels, with body scrolling. |

The shared modal behavior covers Preferences, Project Settings, Save Scene As,
Rename, New Folder, New Stamp From Class, New Stamp, Delete, Unsaved Changes
and About. Closing clears pending dialog state where necessary; closing the
unsaved-work prompt cancels the pending navigation/exit, never discards work.
Preferences continue applying immediately; closing them does not undo a theme
or scale choice.

### Verification, 2026-09-23

- Built the native host and app tests with MSVC and Clang/Linux.
- Windows and Linux targeted CTest suites `app`, `editor_shell` and `platform`
  passed. Theme contrast/geometry tests are part of the app suite.
- Native inspection at 1280 x 720 in both themes; Preferences additionally
  checked at 1.51x scale, including scrolling and restoring display scale.
- Native launcher additionally checked at 800 x 600: stacked sections and
  scrolling keep Create/Open actions reachable.
- Verified Preferences and Rename X closure, Save Scene As / Project Settings
  Escape cancellation, and Terrain tab close/reopen through Window.
- Inspected Explorer, Properties, Viewport, Content, Console, Stats, Terrain,
  Blocks, Debug and the script editor. No scene or source edits saved during QA.
- Full Linux gate: 57/58 CTest suites passed; `streaming_soak` failed its
  population/return-count assertions. That is a remaining failure in the
  streaming subsystem, not a green full gate. No claim of a pre-existing cause.

Manual checks above are specific coverage, not an exhaustive validation of
all combinations of docking, DPI, data and dialog actions. Delete/stamp
confirmation paths received code review of cancellation, not destructive
end-to-end actions against user projects.

## Second workflow review

The professional-editor follow-up led to concrete information-architecture changes:

- **Streaming** now has its own dockable/closable window, independent of Stats.
  Resident/loading counters and cell maps no longer compete with frame and
  memory statistics. Its controls expose the chunk grid and generated objects
  in Explorer, with an explanatory empty state for non-streaming projects.
- **Viewport Settings** is a dockable tool panel for visualization overlays,
  fly camera speed and move/rotate/scale snapping. Open it from Window or the
  gear button in the viewport toolbar. The panel uses the same editor state
  and setters as existing controls; it does not create a second configuration.
- **Window** now lists windows and Reset Layout. Visualization switches moved
  to the relevant tool panels instead of mixing panel visibility and scene
  debugging in a single menu.
- Explorer and Content context menus gained semantic Orbit icons for opening,
  importing, renaming, duplication, grouping, deleting, stamping and refreshing.
  Labels, disabled states, confirmation paths and shortcuts remain intact.
- Tab icons also render in floating dock groups and ordinary floating windows.
- Initial docking now finds the existing right-hand dock even when Properties
  is an inactive tab. Remembered floating positions are still respected.

Native QA checked the Viewport Settings controls, its toolbar entry and floating
window icon, the Window menu, and Streaming's inactive state. MSVC and Clang
builds passed; the targeted app/platform/editor-shell suites passed again on
Windows and Linux. The earlier full-gate streaming-soak failure remains recorded
above; it was not fixed or hidden by this interface work.

This is an editor workflow improvement, not a claim of feature parity with
Unreal, Unity or Godot. Active streaming maps, all possible docking layouts and
all game-production workflows are not covered by this manual UI pass.

## Focused viewport and terrain tools

The user approved removing duplicated specialized actions from the viewport.
Its scene toolbar now contains transport, selection/transforms, local/world,
pivot/centre, snapping, a single Tools entry and viewport settings. Tools opens
Terrain or Blocks without changing the current operation. New Scene and scene
saving remain in File; Save Scene now opens Save As for an untitled scene.
The stamp-editing session keeps its contextual save/close/discard controls.

Sculpt/Paint/Blocks status is shown beside the toolbar with the Q escape route.
Q always returns to selection, including an empty world. Transform buttons and
W/E/R also leave specialized brush modes. T/Y/B retain their existing terrain/
voxel availability checks and open the corresponding panel.

Terrain has four collapsible sections: Create, Sculpt, Brush and Paint. Headers
and actions carry Orbit icons; sculpt actions pair icons with labels and wrap.
Creation fields fill the panel width with labels above. Brush shape is an
explicit Sphere/Box combo, with radius and strength/spacing controls shared
between sculpting and painting. The responsive material palette and selected
material name remain, with a labeled Paint Material action.

Visual QA at 1280 x 720 verified the one-row editing toolbar, opening Terrain
through Tools, section icons and collapse behavior, the brush/palette layout,
activating Paint from the panel and Q returning to selection without terrain.
No terrain was generated, cleared or saved during this UI review.

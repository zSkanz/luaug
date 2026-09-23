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

# Magpie UI source

The Qt Widgets theme is taken from the user's JX3DPS repository:

- Repository: https://github.com/Nowaterisenough/JX3DPS
- Branch: `develop`
- Commit: `2686421e38144c4a3740e43722f2fcbce7825857`
- Source directory: `apps/desktop/controls/theme`
- License: MIT, Copyright (c) 2021 NoWats; full notice in `licenses/JX3DPS-MIT.txt`.

`accent_color.h` and `dark_style.h` are imported unchanged. The base stylesheet
and proxy style retain JX3DPS's structure, with the typography corrections
documented below. `magpie_motion.h` and `segmented_tabs.h` retain their motion and
input behavior, with widget-scoped surface colors for the tray screenshot.
`animated_navigation.h` adds a plain secondary strip and an explicit empty
selection for the settings page. `magpie_shell.h` adapts the shell to the
user's original Magpie screenshots: text-only centered navigation, panel
control on the left, refresh/settings on the right, no breadcrumb/status
header, plain settings tabs and search. `export.h` removes the reference
project's shared-library boundary.

Main content uses a responsive 18 logical pixel minimum margin and a centered
1320 logical pixel maximum width. At a 1412 logical pixel viewport this gives
46 pixel margins (58 physical pixels at 125% scale). Scrollbars stay at the
window edge, and their gutter is compensated inside the page. The main
navigation is 51 logical pixels high. Colors were also checked against
the supplied PNGs: canvas `#1a1a1e`, surface `#232327`, selected main tab
`#3c3c40`, and selected settings tab `#2d2d33`. Mouse focus does not paint a
second navigation frame; explicit keyboard navigation retains its focus cue.
The tray screenshot uses a separate palette: canvas `#1c1d21`, surface
`#25262c`, raised controls `#2e2f36`, border `#323338`. Its selected navigation
tab uses the surface fill and border, unlike the main window's brighter thumb.
Primary actions and charts use `#a5b4fc`, sampled from the supplied Magpie
button and chart pixels, through JX3DPS's existing configurable accent API.

The reference's palette, row/card properties, 280 ms selection movement,
180 ms text transitions, 140 ms press
feedback and easing curve are shared by the main window and tray popup.
Pages and input controls live for the lifetime of their window. Navigation
only selects an existing QStackedWidget page; it does not recreate controls.
The password-list manager uses the same settings tabs, card rows, list delegate,
typography and segmented choices as the existing pages. Its page stays alive alongside
the other five pages in both windows. The native preview runner seeds an isolated temporary library.
The extraction page's nested-extraction and media-organization choices reuse
the existing settings-row typography and segmented control, and share their state between
both windows. Media organization selects independent output directories and
recursive extraction while it is enabled.

## Typography verification

Typography is calibrated from the CSS embedded in the locally installed
`magpie-windows-amd64.exe`, rather than assuming the JX3DPS base font is the
same. Only the presentation keys in Magpie's preferences were inspected;
the reference uses `textSize: 110` with no custom UI font override.

- Native Qt font chain: Segoe UI, Segoe UI Variable Text, Microsoft YaHei UI, Microsoft YaHei.
- Body: 13 CSS px at 110%, regular. Qt settings names use weight 600: copying
  the web stylesheet's numerical weight 500 produced regular Han glyphs and
  was visibly lighter than the supplied PNG.
- Main navigation: 12.5 CSS px, regular.
- Settings tabs: 11 CSS px, weight 600, .05em letter spacing.
- Tray navigation: 12 CSS px, active weight 550.
- Settings descriptions: 11.5 CSS px, regular, `#b9b9c1`.
- Muted text: `#8b8b94` on the main window and `#9c9ca6` in the tray.

`typography.h` preserves fractional sizes with point-size fonts at Qt's logical
DPI. Qt tests inspect the actual polished label fonts/colors and shaped Latin
and Han glyph runs, not only the application's requested font.

## Visual verification

Offscreen Qt tests are functional regression checks, not the Windows visual
reference: the font rasterization differs between the offscreen and Windows
platform plugins. `RenderUiReferences` renders real production widgets with
the Windows plugin into `build/native-previews` without showing or activating
a window. `render-info.json` records actual DPI, resolved font and image sizes.
These native captures are the visual review artifacts.

`VisualReferenceProbe` and `CompareReferenceText.py` compare rendered copies of
the original screenshot's Chinese title, description and secondary tab text.
They read the supplied PNG in place. The native-font comparison revealed the
regular-versus-semibold title mismatch that declaration-only tests missed.
The SVG header icons, search-field geometry and popup
header spacing are also calibrated to the supplied screenshots.

## Current control and interaction alignment

The locally installed Magpie 0.1.2738 settings pages were inspected on 2026-11-03.
Boolean settings now use its explicit off/on segmented choices. They share the
existing selection animation and keyboard behavior with format and compression
choices. Minimum widths follow the painted segments, avoiding Qt's extra tab
scroll-button reservation. Synchronization updates the selection indicator in
both visible windows without writing preferences again.

Simple settings use one-line rows; descriptions are reserved for consequences
such as deleting non-media extraction output and actual update status. Import
formats and optional syntax are available on the relevant controls. Idle
instructions, feature summaries and repeated empty-state descriptions have been
removed. Queue availability, selection, drag highlights, disabled actions and
search empty states provide feedback at the point of interaction.

The macOS review compiles production UI sources against isolated substitutes for
Windows archive, storage and update services. It verifies UI behavior and captures
main and tray layouts, including the 440-pixel tray width. This does not replace
the Windows integration tests or native font/DPI verification described above.

`cmake/DeployWindowsRuntime.cmake` is also imported from this commit to deploy
the non-Qt dependencies of MSYS2 builds alongside the Qt plugins.

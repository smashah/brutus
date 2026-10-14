# Terminal preview provenance

`brutus.svg` renders an actual 80-column terminal capture of Brutus 0.1.1 running in `--dry-run` mode on 2026-10-14. The traffic-light state, system metrics, candidate resource values, per-process swap-in rates, and controls come from the running program.

The local account path and project name were redacted for publication. The SVG adds the project title and a terminal frame, and maps ANSI styles to readable colors on a dark background. It does not show a fabricated recovery or a process close.

The header rounds the shipped universal executable's size to KiB, including Apple silicon and Intel slices. The dagger marker, its spacing, and the per-process swap rates come from the current CLI.

The artwork uses [Paper Mono 1.000](https://github.com/paper-design/paper-mono/releases/tag/v1.000), Regular and Bold. The webfonts are included under `fonts/` with their [SIL Open Font License](fonts/OFL.txt), and embedded in the SVG so the typography doesn't depend on installed fonts. The terminal keeps its original 12-unit character grid.

The light teal-and-sand backdrop is an actual static render of Paper's [Grain Gradient shader](https://shaders.paper.design/grain-gradient), using `@paper-design/shaders` 0.0.81. It is saved as `paper-grain-background.webp` and embedded in the SVG. No shader runs on the README page or in Brutus.

Shader settings: `shape: wave`, `colors: [#eadfce, #c7ece3, #c5dfe7, #e4bb95]`, `colorBack: #f3ecdf`, `softness: 0.8`, `intensity: 0.13`, `noise: 0.075`, `scale: 0.65`, `rotation: 25`, `offsetX: 0.2`, `offsetY: 0.05`, `fit: cover`, `speed: 0`, `frame: 18000`. The frame is 1200 × 1015 pixels.

The SVG is the editable source and contains no scripts, remote fonts, or external image references. `brutus.png` is its static raster rendering for the repository homepage.

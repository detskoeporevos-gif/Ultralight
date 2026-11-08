# EscFR — Wired browser on Ultralight

`EscFR` is a desktop browser built on the **Ultralight** renderer. The window chrome is
plain HTML/CSS/JS drawn by an overlay View; tabs, navigation, settings and the
DOM inspector are wired to C++ through Ultralight's JS bridge.

* **Shell geometry** — Firefox: tab strip, rounded address bar with shield, app menu,
  `about:preferences`-style settings with a sidebar and switch rows.
* **Skin** — *Wired* (`Serial Experiments Lain`): deep navy, wire grid, scanlines,
  cyan/red glitch accents, EB Garamond quotes, `present day, present time`.
* **Internal pages** — `esc://newtab`, `esc://settings`, `esc://about`.
* **Persistence** — settings live in `escfr-settings.json` next to `assets/`.

## Build

```bash
cmake -S samples -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DESCFR_ONLY=ON
cmake --build build --target EscFR
```

The first configure downloads the Ultralight SDK binaries (AppCore, WebCore,
UltralightCore) from Ultralight's CDN.

Run the result from the build output directory (so `./assets/` resolves):

```bash
cd "build/Sample 10 - EscFR Browser"
./EscFR
```

Set `ESCFR_FORCE_CPU=1` to force the CPU renderer (useful for remote desktops,
VMs and CI runs under Xvfb).

## Screenshots

The GitHub Actions run renders the real window under Xvfb and commits the captures:

| File | What it shows |
| --- | --- |
| `screenshots/escfr-newtab.png` | startup, `esc://newtab` |
| `screenshots/escfr-settings.png` | `esc://settings` |
| `screenshots/escfr-about.png` | `esc://about` |
| `screenshots/escfr-tabs-1.png` | a second tab opened with `Ctrl+T` |
| `screenshots/escfr-tabs.png` | after typing `esc://settings` in the address bar |

## Keyboard

| Shortcut | Action |
| --- | --- |
| `Ctrl+T` | new tab |
| `Ctrl+W` | close tab |
| `Ctrl+L` | focus address bar |
| `Ctrl+R` / `F5` | reload |
| `Alt+←` / `Alt+→` | back / forward |
| `Ctrl+,` | settings (`esc://settings`) |
| `Ctrl+Shift+I` / `F12` | DOM inspector |
| `Esc` | stop loading |
| `Ctrl+Q` | quit |

## Artwork & fonts

Fonts are bundled under `assets/fonts` (Inter, JetBrains Mono, EB Garamond, SIL OFL).
The EscFR mark is an original inline SVG. No external requests are made by the chrome.

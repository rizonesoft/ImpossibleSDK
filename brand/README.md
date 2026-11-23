# The Impossible SDK identity

Three sheets stacked front to back, each with the slanted corner of the Impossible OS logo's ribbons: programs climb tiers, from a console to the whole desktop. Chosen by the owner on 2026-11-23.

| File | Use |
| --- | --- |
| `icon/icon.svg` | 48 pixels and larger |
| `icon/icon-16.svg`, `-20`, `-24`, `-32` | Their own drawing, on whole pixels |
| `icon/glyph.svg` | One colour, in the text colour around it |
| `icon.ico`, `png/` | Windows and everything that takes a picture |
| `wordmark-dark.png`, `wordmark-light.png` | The name, on a dark and on a light ground |
| `banner-dark.png`, `banner-light.png` | The head of the README |
| `social-preview.png` | GitHub, Settings, Social preview |
| `badge-dark.png`, `badge-light.png` | Programs built with the SDK |

The rules are those of Impossible OS: a 48 grid, seen from the front, lit from above, no outlines, the logo's blues. The wordmark is set in Plus Jakarta Sans, the banner in Selawik and Cascadia Code (all OFL). `tools/brand.py` renders everything but the sources; the banners follow `abi/tiers.json`.

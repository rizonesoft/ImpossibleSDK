#!/usr/bin/env python3
"""Render the SDK's images from their sources: python3 tools/brand.py --os <Impossible OS checkout>

brand/icon/ holds the sources: icon.svg for 48 pixels and up, and a drawing of
its own for 16, 20, 24 and 32. This writes the PNGs and icon.ico, the wordmarks,
the README banners, the social preview and the badge, and the picture of our buttons
beside real WinUI's. The window in the banners is a preview of Impossible UI.

Needs rsvg-convert, ImageMagick and a Chromium (CHROME, or Playwright's). The
interface and code fonts and the wallpaper are read from the checkout's resources, the
wordmark's font (Plus Jakarta Sans, OFL) from Google Fonts while rendering.
"""
import argparse
import json
import os
import subprocess
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / "brand"
SMALL = (16, 20, 24, 32)
LARGE = (48, 64, 128, 256)
TAG = "One small C kit for Windows programs that Impossible&nbsp;OS will run&nbsp;too."

def icon(size):
    text = (BRAND / "icon" / (f"icon-{size}.svg" if size in SMALL else "icon.svg")).read_text(encoding="utf-8")
    box = size if size in SMALL else 48
    return text.replace(f'width="{box}" height="{box}"', f'width="{size}" height="{size}" style="display:block"', 1)


def page(resources, body, light, width=None, height=None):
    fonts = resources / "fonts"
    size = f"width:{width}px;height:{height}px;overflow:hidden" if width else "display:inline-flex;align-items:center;gap:26px;padding:20px 26px;background:transparent"
    veil = "rgba(243,246,252,.9),rgba(243,246,252,.35) 60%,rgba(243,246,252,.25)" if light else "rgba(4,10,30,.82),rgba(4,10,30,.4) 60%,rgba(4,10,30,.3)"
    ground = f"background:linear-gradient(90deg,{veil}),url({resources.as_uri()}/backgrounds/silk-{'light' if light else 'dark'}.jpg) 100% 45%/cover" if width else ""
    return f"""<!doctype html><meta charset="utf-8"><link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@700;800&display=swap" rel="stylesheet"><style>
@font-face{{font-family:Selawik;src:url({fonts.as_uri()}/selawk.ttf)}}@font-face{{font-family:Selawik;font-weight:600;src:url({fonts.as_uri()}/selawksb.ttf)}}
@font-face{{font-family:Cascadia;src:url({fonts.as_uri()}/CascadiaCode-Regular.ttf)}}
*{{box-sizing:border-box}}body{{margin:0;position:relative;font-family:Selawik;{size};{ground}}}i{{display:block}}</style><body>{body}"""


def word(size, light):
    """The name, with SDK on an accent button of the theme."""
    ink, fill, text = ("#1B1B1B", "linear-gradient(#0067C0,#005FB8)", "#FFFFFF") if light else ("#F5F8FF", "linear-gradient(#66D0FF,#60CDFF)", "#000000")
    return (f"<div style=\"font-family:'Plus Jakarta Sans';display:flex;align-items:center;gap:{size * .24:.0f}px;font-weight:700;font-size:{size:.0f}px;line-height:1;letter-spacing:-.012em;color:{ink};white-space:nowrap\">Impossible"
            f"<span style='font-size:{size * .72:.0f}px;font-weight:800;letter-spacing:.01em;background:{fill};color:{text};padding:{size * .15:.0f}px {size * .27:.0f}px;border-radius:{size * .13:.0f}px;"
            f"box-shadow:inset 0 -2px 0 rgba(0,0,0,.22),inset 0 1px 0 rgba(255,255,255,.22)'>SDK</span></div>")


def window(width, height, light):
    """A preview of Impossible UI beside the code that draws its button."""
    z = min(height / 360, 1.12)
    ink, quiet, back, edge, control = ("#1B1B1B", "#5D5D5D", "rgba(243,243,243,.94)", "rgba(0,0,0,.09)", "#FFFFFF") if light else ("#FFFFFF", "#C5C5C5", "rgba(32,32,36,.93)", "rgba(255,255,255,.11)", "rgba(255,255,255,.07)")
    accent, on, kind, name, note = ("#005FB8", "#FFFFFF", "#0F62B5", "#8A3FA0", "#5D5D5D") if light else ("#60CDFF", "#000000", "#7CC7FF", "#D7A6FF", "#9A9A9A")
    row, code = ("rgba(0,0,0,.05)", "rgba(255,255,255,.55)") if light else ("rgba(255,255,255,.07)", "rgba(0,0,0,.28)")

    def px(value):
        return f"{value * z:.1f}px"

    glyph = "".join(f"<svg width='{px(10)}' height='{px(10)}' viewBox='0 0 10 10'>{shape}</svg>" for shape in (
        "<path d='M0 5h10' stroke='currentColor'/>", "<rect x='.5' y='.5' width='9' height='9' rx='1.4' fill='none' stroke='currentColor'/>", "<path d='M0 0l10 10M10 0L0 10' stroke='currentColor'/>"))
    title = icon(16).replace('width="16" height="16"', f'width="{16 * z:.1f}" height="{16 * z:.1f}"', 1)
    return f"""<div style="position:absolute;left:{width - 596 * z:.0f}px;top:{(height - 300 * z) / 2:.0f}px;width:{px(560)};height:{px(300)};border-radius:{px(8)};background:{back};border:1px solid {edge};box-shadow:0 {px(18)} {px(46)} rgba(0,8,36,{.22 if light else .55});color:{ink};font-size:{px(13)};overflow:hidden">
<div style="height:{px(34)};display:flex;align-items:center;gap:{px(8)};padding-left:{px(12)};font-size:{px(12)}">{title}Impossible UI preview<span style="margin-left:auto;display:flex;gap:{px(26)};padding-right:{px(18)};color:{quiet}">{glyph}</span></div>
<div style="display:grid;grid-template-columns:1fr 1.12fr;height:{px(266)}"><div style="padding:{px(16)} {px(20)};display:flex;flex-direction:column;gap:{px(15)}">
<div style="display:flex;gap:{px(8)}"><span style="background:{accent};color:{on};border-radius:{px(4)};padding:{px(7)} {px(16)};box-shadow:inset 0 -1px 0 rgba(0,0,0,.25)">Build</span><span style="background:{control};border:1px solid {edge};border-radius:{px(4)};padding:{px(6)} {px(15)}">Cancel</span></div>
<div style="display:flex;align-items:center;gap:{px(10)}"><span style="width:{px(40)};height:{px(20)};border-radius:99px;background:{accent};position:relative"><i style="position:absolute;right:{px(4)};top:{px(4)};width:{px(12)};height:{px(12)};border-radius:50%;background:{on}"></i></span>On</div>
<div style="background:{control};border:1px solid {edge};border-bottom:{px(2)} solid {accent};border-radius:{px(4)};padding:{px(7)} {px(11)}">Impossible</div>
<div style="display:flex;flex-direction:column;gap:{px(3)}"><div style="background:{row};border-radius:{px(4)};padding:{px(7)} {px(12)};position:relative"><i style="position:absolute;left:0;top:{px(8)};width:{px(3)};height:{px(16)};border-radius:2px;background:{accent}"></i>Button</div><div style="padding:{px(7)} {px(12)}">Toggle switch</div><div style="padding:{px(7)} {px(12)}">Text box</div></div></div>
<pre style="margin:0;padding:{px(16)} {px(18)};font:{px(11.2)}/1.72 Cascadia;background:{code};border-left:1px solid {edge}"><span style="color:{name}">#include</span> <span style="color:{kind}">"button.h"</span>

im_draw_button(
    &amp;surface,
    (<span style="color:{kind}">im_rect</span>){{0, 0, w, h}},
    <span style="color:{name}">IM_BUTTON_ACCENT</span>,
    <span style="color:{name}">IM_REST</span>);

<span style="color:{note}">/* one C source:
   Windows today,
   Impossible OS next */</span></pre></div></div>"""


def banner(width, height, light):
    social = height > 500
    mark, letters, small = (136, 66, 27) if social else (int(height * .24), height * .145, height * .062)
    large = icon(256).replace('width="256" height="256"', f'width="{mark}" height="{mark}"', 1)
    head = "flex-direction:column;align-items:flex-start;gap:28px" if social else f"align-items:center;gap:{height * .055:.0f}px"
    return window(width, height, light) + (
        f"<div style='position:absolute;left:{width * .05:.0f}px;top:0;bottom:0;display:flex;flex-direction:column;justify-content:center;gap:{height * .052:.0f}px'>"
        f"<div style='display:flex;{head}'>{large}{word(letters, light)}</div>"
        f"<div style='font-size:{small:.0f}px;line-height:1.35;color:{'#3f4754' if light else '#C9D6EA'};max-width:{width * .42:.0f}px'>{TAG}</div></div>")


def badge(light):
    ground, ink, edge = ("#FFFFFF", "#1B1B1B", "#D5D9DF") if light else ("#2B2B2B", "#FFFFFF", "#474747")
    return (f"<span style='display:inline-flex;align-items:center;gap:8px;height:28px;padding:0 11px 0 9px;border:1px solid {edge};border-radius:4px;background:{ground};color:{ink};font-weight:600;font-size:12.5px;white-space:nowrap'>"
            f"{icon(16)}Built with Impossible SDK</span>")


def comparison(out):
    """Our buttons beside real WinUI's, from the frames the tests compare: rest, hover, pressed and disabled, dark and light."""
    manifest = json.loads((ROOT / "tests/reference/cases.json").read_text(encoding="utf-8"))
    cases = [c for c in manifest["cases"] if c.get("control") in ("button", "accent")]
    accent_keys = ("Accent", "AccentLight1", "AccentLight2", "AccentLight3", "AccentDark1", "AccentDark2", "AccentDark3")
    accent_args = [manifest["accents"][k] for k in accent_keys]
    ours = subprocess.run([ROOT / "build/linux/control-shots.exe", *accent_args], capture_output=True, check=True, timeout=60).stdout
    size = 160 * 64 * 4
    width, height, zoom = 4 * 160, 4 * 64, 2
    sheets = []
    for which in (0, 1):
        pixels = bytearray(width * height * 4)
        for index, case in enumerate(cases):
            frame = ours[index * size:(index + 1) * size] if which == 0 else zlib.decompress((ROOT / f"tests/reference/{case['file']}.z").read_bytes())
            left, top = index % 4 * 160, index // 4 * 64
            for y in range(64):
                at = ((top + y) * width + left) * 4
                pixels[at:at + 640] = frame[y * 640:(y + 1) * 640]
        sheets.append(bytes(pixels))
    with tempfile.TemporaryDirectory() as folder:
        for n, pixels in enumerate(sheets):
            Path(folder, f"{n}.bgra").write_bytes(pixels)
            subprocess.run(["convert", "-size", f"{width}x{height}", "-depth", "8", f"bgra:{folder}/{n}.bgra", "-alpha", "off", "-filter", "point", "-resize", f"{zoom * 100}%", f"{folder}/{n}.png"], check=True, timeout=60)
        subprocess.run(["convert", f"{folder}/0.png", f"{folder}/1.png", "-background", "#808890", "-splice", "8x0+0+0", "+append", "-chop", "8x0+0+0", "-strip", out], check=True, timeout=60)


def chromium():
    found = sorted((Path.home() / ".cache/ms-playwright").glob("chromium-*/chrome-linux*/chrome"), key=lambda p: int(p.parts[-3].split("-")[1]))
    given = os.environ.get("CHROME") or (found and str(found[-1]))
    if not given:
        raise SystemExit("brand: no Chromium found; set CHROME")
    return given


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--os", type=Path, required=True, help="a checkout of Impossible OS, for its fonts and wallpaper")
    resources = (parser.parse_args().os / "resources").resolve()
    if not (resources / "fonts/selawk.ttf").is_file():
        parser.error(f"{resources} does not have the fonts")
    chrome, png = chromium(), BRAND / "png"
    png.mkdir(exist_ok=True)
    for size in SMALL + LARGE:
        source = BRAND / "icon" / (f"icon-{size}.svg" if size in SMALL else "icon.svg")
        subprocess.run(["rsvg-convert", "-w", str(size), source, "-o", png / f"icon-{size}.png"], check=True, timeout=60)
    subprocess.run(["convert", *(png / f"icon-{s}.png" for s in (*SMALL, 48, 64, 256)), BRAND / "icon.ico"], check=True, timeout=60)
    with tempfile.TemporaryDirectory() as folder:
        def shot(name, html, width, height, scale, trim):
            source, out = Path(folder, "page.html"), BRAND / f"{name}.png"
            source.write_text(html, encoding="utf-8")
            subprocess.run([chrome, "--headless=new", "--no-sandbox", "--hide-scrollbars", "--allow-file-access-from-files", "--virtual-time-budget=6000", "--default-background-color=00000000",
                            f"--force-device-scale-factor={scale}", f"--window-size={width},{height}", f"--screenshot={out}", source.as_uri()], check=True, capture_output=True, timeout=120)
            subprocess.run(["convert", out, *(["-trim", "+repage", "-bordercolor", "none", "-border", "8"] if trim else []), "-strip", out], check=True, timeout=60)
        for light in (False, True):
            on = "light" if light else "dark"
            shot(f"banner-{on}", page(resources, banner(1280, 360, light), light, 1280, 360), 1280, 360, 2, False)
            shot(f"wordmark-{on}", page(resources, icon(128) + word(76, light), light), 1000, 240, 2, True)
            shot(f"badge-{on}", page(resources, badge(light), light), 400, 120, 2, True)
        shot("social-preview", page(resources, banner(1280, 640, False), False, 1280, 640), 1280, 640, 1, False)
    (ROOT / "docs/images").mkdir(exist_ok=True)
    comparison(ROOT / "docs/images/buttons.png")


if __name__ == "__main__":
    main()

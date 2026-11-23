#!/usr/bin/env python3
"""Render the SDK's images from their sources: python3 tools/brand.py --os <Impossible OS checkout>

brand/icon/ holds the sources: icon.svg for 48 pixels and up, and a drawing of
its own for 16, 20, 24 and 32. This writes the PNGs and icon.ico, the wordmarks,
the README banners, the social preview and the badge. The banners show the tiers
of abi/tiers.json, so run it again when a tier is added or renamed.

Needs rsvg-convert, ImageMagick and a Chromium (CHROME, or Playwright's). The
interface and code fonts are read from the checkout's resources/fonts, the
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
RAMP = ("#8DF0FB", "#5CE6F7", "#35C1F3", "#35A6F2", "#2185E6", "#1558D6")


def tiers():
    return [[t["tier"], t["name"]] for t in json.loads((ROOT / "abi/tiers.json").read_text(encoding="utf-8"))["tiers"]]


def icon(size):
    text = (BRAND / "icon" / (f"icon-{size}.svg" if size in SMALL else "icon.svg")).read_text(encoding="utf-8")
    box = size if size in SMALL else 48
    return text.replace(f'width="{box}" height="{box}"', f'width="{size}" height="{size}" style="display:block"', 1)


def page(fonts, body, light, width=None, height=None):
    size = f"width:{width}px;height:{height}px;overflow:hidden" if width else "display:inline-flex;align-items:center;gap:26px;padding:20px 26px;background:transparent"
    ground = "" if not width else ("background:radial-gradient(90% 130% at 85% 10%,#dcebff,transparent 60%),radial-gradient(70% 110% at 100% 100%,#d5f5fb,transparent 60%),#F3F3F3" if light
                                   else "background:radial-gradient(90% 130% at 85% 10%,#17366f,transparent 60%),radial-gradient(70% 110% at 100% 100%,#0c4a63,transparent 60%),#0d1526")
    return f"""<!doctype html><meta charset="utf-8"><link href="https://fonts.googleapis.com/css2?family=Plus+Jakarta+Sans:wght@600;800&display=swap" rel="stylesheet"><style>
@font-face{{font-family:Selawik;src:url({fonts.as_uri()}/selawk.ttf)}}@font-face{{font-family:Selawik;font-weight:600;src:url({fonts.as_uri()}/selawksb.ttf)}}
@font-face{{font-family:Cascadia;src:url({fonts.as_uri()}/CascadiaCode-Regular.ttf)}}
*{{box-sizing:border-box}}body{{margin:0;position:relative;font-family:Selawik;{size};{ground}}}</style><body>{body}"""


def word(size, light):
    ink = "#1B1B1B" if light else "#F5F8FF"
    top, middle, low = ("#0E8FC6", "#1B6FE0", "#1450C8") if light else ("#5CE6F7", "#35A6F2", "#2F78EA")
    cut = f"linear-gradient({top} 0 37%,transparent 37% 39.5%,{middle} 39.5% 57%,transparent 57% 59.5%,{low} 59.5%)"
    return (f"<div style=\"font-family:'Plus Jakarta Sans';line-height:1;white-space:nowrap\"><div style='font-weight:600;font-size:{size * .36:.0f}px;margin-left:.12em;color:{ink}'>Impossible</div>"
            f"<div style='font-weight:800;font-size:{size * 1.32:.0f}px;letter-spacing:-.02em;line-height:1.02;background:{cut};-webkit-background-clip:text;color:transparent'>SDK</div></div>")


def panels(width, height, light):
    ink, quiet, card, edge = ("#1B1B1B", "#5D5D5D", "rgba(255,255,255,.62)", "rgba(0,0,0,.08)") if light else ("#fff", "#C5C5C5", "rgba(44,44,52,.55)", "rgba(255,255,255,.10)")
    rows = tiers()[::-1]
    scale = 6 / max(6, len(rows))
    tall, gap = height * .118 * scale, height * .022 * scale
    start, out = (height - len(rows) * tall - (len(rows) - 1) * gap) / 2, ""
    for i, (number, name) in enumerate(rows):
        step = i / max(1, len(rows) - 1)
        wide = width * (.25 + .14 * step)
        slant = tall * .42
        out += (f"<div style='position:absolute;left:{width * .955 - wide:.0f}px;top:{start + i * (tall + gap):.0f}px;width:{wide:.0f}px;height:{tall:.0f}px;display:flex;align-items:center;"
                f"gap:{height * .03:.0f}px;padding-left:{height * .035:.0f}px;color:{ink};font-size:{tall * .4:.0f}px'>"
                f"<svg style='position:absolute;inset:0;filter:drop-shadow(0 6px 12px rgba(0,10,40,{.1 if light else .3}))' width='{wide:.0f}' height='{tall:.0f}'>"
                f"<path d='M8.5 .5H{wide - slant - .5:.1f}L{wide - .5:.1f} {slant:.1f}V{tall - 8.5:.1f}q0 8-8 8H8.5q-8 0-8-8V8.5q0-8 8-8z' fill='{card}' stroke='{edge}'/>"
                f"<path d='M8.5 1H{wide - slant - .5:.1f}' stroke='#fff' stroke-opacity='{.8 if light else .22}'/></svg>"
                f"<i style='position:relative;width:{tall * .11:.0f}px;height:{tall * .46:.0f}px;border-radius:3px;background:{RAMP[round(step * 5)]}'></i>"
                f"<span style='position:relative;color:{quiet};font-family:Cascadia;font-size:{tall * .34:.0f}px'>{number}</span><b style='position:relative;font-weight:600'>{name}</b></div>")
    return out


def banner(width, height, light):
    social = height > 500
    mark, letters, small = (150, 150, 27) if social else (int(height * .33), height * .235, height * .062)
    large = icon(256).replace('width="256" height="256"', f'width="{mark}" height="{mark}"', 1)
    return panels(width, height, light) + (
        f"<div style='position:absolute;left:{width * .05:.0f}px;top:0;bottom:0;display:flex;flex-direction:column;justify-content:center;gap:{height * .052:.0f}px'>"
        f"<div style='display:flex;align-items:center;gap:{height * .055:.0f}px'>{large}{word(letters, light)}</div>"
        f"<div style='font-size:{small:.0f}px;line-height:1.35;color:{'#3f4754' if light else '#C9D6EA'};max-width:{width * .42:.0f}px'>{TAG}</div></div>")


def badge(light):
    ground, ink, edge = ("#FFFFFF", "#1B1B1B", "#D5D9DF") if light else ("#2B2B2B", "#FFFFFF", "#474747")
    return (f"<span style='display:inline-flex;align-items:center;gap:8px;height:28px;padding:0 11px 0 9px;border:1px solid {edge};border-radius:4px;background:{ground};color:{ink};font-weight:600;font-size:12.5px;white-space:nowrap'>"
            f"{icon(16)}Built with Impossible SDK</span>")


def comparison(out):
    """Our buttons beside real WinUI's, from the frames the tests compare: rest, hover, pressed and disabled, dark and light."""
    cases = json.loads((ROOT / "tests/reference/cases.json").read_text(encoding="utf-8"))["cases"]
    ours = subprocess.run([ROOT / "build/linux/control-shots.exe"], capture_output=True, check=True, timeout=60).stdout
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
    parser.add_argument("--os", type=Path, required=True, help="a checkout of Impossible OS, for its fonts")
    fonts = (parser.parse_args().os / "resources/fonts").resolve()
    if not (fonts / "selawk.ttf").is_file():
        parser.error(f"{fonts} does not have the fonts")
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
            shot(f"banner-{on}", page(fonts, banner(1280, 360, light), light, 1280, 360), 1280, 360, 2, False)
            shot(f"wordmark-{on}", page(fonts, icon(128) + word(92, light), light), 1000, 240, 2, True)
            shot(f"badge-{on}", page(fonts, badge(light), light), 400, 120, 2, True)
        shot("social-preview", page(fonts, banner(1280, 640, False), False, 1280, 640), 1280, 640, 1, False)
    (ROOT / "docs/images").mkdir(exist_ok=True)
    comparison(ROOT / "docs/images/buttons.png")
    (BRAND / "rendered.json").write_text(json.dumps({"tiers": tiers()}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()

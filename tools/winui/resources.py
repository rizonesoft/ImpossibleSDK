#!/usr/bin/env python3
"""Generate ui/theme_data.{h,c} from WinUI's theme resources (MIT) at the pinned commit.

    python tools/winui/resources.py <checkout>           writes the files
    python tools/winui/resources.py <checkout> --check   fails when they differ

Every brush, colour, number and thickness a control template can name is
resolved for the dark, light and contrast themes. The accent colours and the
contrast colours are chosen by the user, so they stay references ("slots").
"""
import argparse
import fnmatch
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PIN = json.loads((Path(__file__).parent / "pin.json").read_text(encoding="utf-8"))
KEY = "{http://schemas.microsoft.com/winfx/2006/xaml}Key"
THEMES = {"Default": "dark", "Light": "light", "HighContrast": "contrast"}
SLOTS = ["SystemAccentColor", "SystemAccentColorLight1", "SystemAccentColorLight2", "SystemAccentColorLight3",
         "SystemAccentColorDark1", "SystemAccentColorDark2", "SystemAccentColorDark3",
         "SystemColorWindowTextColor", "SystemColorWindowColor", "SystemColorButtonFaceColor",
         "SystemColorButtonTextColor", "SystemColorHighlightColor", "SystemColorHighlightTextColor",
         "SystemColorHotlightColor", "SystemColorGrayTextColor", "SystemColorDisabledTextColor",
         "SystemColorControlAccentColor"]
NAMED = {"Transparent": 0x00FFFFFF, "White": 0xFFFFFFFF, "Black": 0xFF000000, "LightGray": 0xFFD3D3D3}
NUMBERS = {"Double": 1, "Thickness": 4, "CornerRadius": 4, "Int32": 1}


class Unresolved(Exception):
    pass


def local(tag):
    return tag.rsplit("}", 1)[-1]


def fixed(text):
    """16.16 fixed point, exact where the decimal allows and rounded half away from zero otherwise."""
    from fractions import Fraction
    value = Fraction(text.strip()) * 65536
    whole = int(abs(value) + Fraction(1, 2))
    return -whole if value < 0 else whole


def argb(text):
    text = text.strip()
    if text in NAMED:
        return NAMED[text]
    match = re.fullmatch(r"#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})", text)
    if not match:
        raise Unresolved(f"colour {text!r}")
    return int(match[1], 16) | (0xFF000000 if len(match[1]) == 6 else 0)


def reference(text):
    match = re.fullmatch(r"\{(?:Static|Theme)Resource\s+(?:ResourceKey=)?([\w.]+)\}", (text or "").strip())
    return match[1] if match else None


class Dictionary:
    """Resources by theme. A later file replaces an earlier one, as WinUI's own merge does."""

    def __init__(self):
        self.theme = {name: {} for name in THEMES}
        self.any = {}

    def add(self, path):
        root = ET.parse(path).getroot()
        themed = [d for d in root if local(d.tag) == "ResourceDictionary.ThemeDictionaries"]
        for group in themed:
            for dictionary in group:
                name = dictionary.get(KEY)
                if name == "Dark":
                    name = "Default"
                if name not in self.theme:
                    raise ValueError(f"{path}: theme {name!r}")
                for item in dictionary:
                    if item.get(KEY):
                        self.theme[name][item.get(KEY)] = item
        for item in root:
            if item.get(KEY):
                self.any[item.get(KEY)] = item

    def keys(self):
        return sorted(set(self.any).union(*self.theme.values()))

    def find(self, theme, key):
        """The resource itself, following aliases to their end."""
        seen = []
        while True:
            if key in seen:
                raise Unresolved(f"a loop: {' > '.join([*seen, key])}")
            seen.append(key)
            item = self.theme[theme].get(key, self.any.get(key))
            if item is None and theme == "HighContrast":  # WinUI falls back to the default theme
                item = self.theme["Default"].get(key)
            if item is None:
                raise Unresolved(f"nothing is called {key}")
            if local(item.tag) != "StaticResource":
                return item
            key = item.get("ResourceKey")

    def colour(self, theme, text):
        """(slot or -1, argb) of a colour written in a resource."""
        key = reference(text)
        if key is None:
            return -1, argb(text)
        if key in SLOTS:
            return SLOTS.index(key), 0xFF000000
        item = self.find(theme, key)
        if local(item.tag) == "Color":
            return self.colour(theme, item.text)
        if local(item.tag) == "SolidColorBrush" and item.get("Opacity") is None:
            return self.colour(theme, item.get("Color"))
        raise Unresolved(f"{key} is a {local(item.tag)}, not a colour")

    def brush(self, theme, item):
        """(kind, stops, end): kind is solid, absolute or relative; a stop is (offset, slot, argb, opacity)."""
        kind = local(item.tag)
        opacity = fixed(item.get("Opacity", "1"))
        if kind == "Color":
            return "solid", [(0, *self.colour(theme, item.text), opacity)], 0
        if kind == "SolidColorBrush":
            return "solid", [(0, *self.colour(theme, item.get("Color")), opacity)], 0
        if kind != "LinearGradientBrush":
            raise Unresolved(f"a {kind}")
        start, end = item.get("StartPoint", "0,0"), item.get("EndPoint", "1,1")
        if start != "0,0" or not re.fullmatch(r"0,[\d.]+", end):
            raise Unresolved(f"a gradient from {start} to {end}")
        stops, flipped = [], False
        for node in item.iter():
            name = local(node.tag)
            if name == "GradientStop":
                stops.append((fixed(node.get("Offset")), *self.colour(theme, node.get("Color")), opacity))
            elif name == "ScaleTransform":
                if (node.get("ScaleY"), node.get("CenterY"), node.get("ScaleX")) != ("-1", "0.5", None):
                    raise Unresolved("a gradient with another transform")
                flipped = True
            elif name not in ("LinearGradientBrush", "LinearGradientBrush.GradientStops", "LinearGradientBrush.RelativeTransform"):
                raise Unresolved(f"a gradient with a {name}")
        stops.sort(key=lambda stop: stop[0])  # stable, as WinUI orders equal offsets by position
        where = {"Absolute": "absolute", "RelativeToBoundingBox": "relative"}[item.get("MappingMode", "RelativeToBoundingBox")]
        return where + ("_flipped" if flipped else ""), stops, fixed(end.split(",")[1])


def numbers(item):
    parts = re.split(r"[,\s]+", (item.text or "").strip())
    values = [fixed(p) for p in parts]
    if local(item.tag) == "Thickness":
        values = {1: values * 4, 2: values * 2, 4: values}.get(len(values))
    elif local(item.tag) == "CornerRadius":
        values = {1: values * 4, 4: values}.get(len(values))
    if not values or len(values) != NUMBERS[local(item.tag)]:
        raise Unresolved(f"{local(item.tag)} {item.text!r}")
    return values


def load(checkout):
    resources = Dictionary()
    resources.add(checkout / PIN["base"])
    files = [f for f in sorted(checkout.glob(PIN["controls"])) if not any(fnmatch.fnmatch(f.name, f"*{s}*") for s in PIN["skip"])]
    if not files:
        raise ValueError("the checkout has no control resources")
    # The shared dictionaries come first, so a control's own file can replace them.
    for path in sorted(files, key=lambda f: not f.name.startswith(("Common_", "CornerRadius_"))):
        resources.add(path)
    return resources


def collect(resources):
    """{key: {theme: brush}}, {key: {theme: numbers}} and {key: why} for what cannot be expressed."""
    brushes, values, left = {}, {}, {}
    for key in resources.keys():
        if key in SLOTS:
            continue
        try:
            found = {theme: resources.find(theme, key) for theme in THEMES}
        except Unresolved as why:
            left[key] = str(why)
            continue
        kinds = {local(item.tag) for item in found.values()}
        try:
            if kinds <= {"Color", "SolidColorBrush", "LinearGradientBrush"}:
                brushes[key] = {theme: resources.brush(theme, item) for theme, item in found.items()}
            elif len(kinds) == 1 and kinds <= set(NUMBERS):
                values[key] = {theme: numbers(item) for theme, item in found.items()}
            else:
                left[key] = "a " + " or ".join(sorted(kinds))
        except Unresolved as why:
            left[key] = str(why)
    return brushes, values, left


def emit(brushes, values):
    head = (f"/* Generated by tools/winui/resources.py from microsoft-ui-xaml {PIN['commit']}.\n"
            "   Copyright (c) Microsoft Corporation. Licensed under the MIT License (see licenses/WinUI.txt).\n"
            "   Do not edit. */\n")
    h = [head, "#pragma once\n", "enum im_color_slot {\n"]
    h += [f"    IM_SLOT_{re.sub(r'(?<!^)(?=[A-Z0-9])', '_', s.removeprefix('System').removesuffix('Color') or s).upper()},\n" for s in SLOTS]
    h += ["    IM_SLOT_COUNT\n};\n\nenum im_brush_id {\n"]
    h += [f"    IM_BRUSH_{key},\n" for key in brushes] + ["    IM_BRUSH_COUNT\n};\n\nenum im_value_id {\n"]
    h += [f"    IM_VALUE_{key},\n" for key in values] + ["    IM_VALUE_COUNT\n};\n"]

    pool, where = [], {}
    def stops_at(stops):
        key = tuple(stops)
        if key not in where:
            where[key] = len(pool)
            pool.extend(stops)
        return where[key]

    kinds = ["solid", "absolute", "absolute_flipped", "relative", "relative_flipped"]
    rows = {theme: [] for theme in THEMES}
    for key, by_theme in brushes.items():
        for theme, (kind, stops, end) in by_theme.items():
            rows[theme].append(f"    {{{kinds.index(kind)}, {len(stops)}, {stops_at(stops)}, {end}}}, /* {key} */\n")
    c = [head, '#include "theme.h"\n\n', "const im_brush im_theme_brushes[3][IM_BRUSH_COUNT] = {\n"]
    for theme in THEMES:
        c += [f"{{ /* {THEMES[theme]} */\n", *rows[theme], "},\n"]
    c += ["};\n\nconst im_stop im_theme_stops[] = {\n"]
    c += [f"    {{{offset}, {slot}, 0x{value:08X}, {opacity}}},\n" for offset, slot, value, opacity in pool]
    c += ["};\n\nconst int32_t im_theme_values[3][IM_VALUE_COUNT][4] = {\n"]
    for theme in THEMES:
        c += [f"{{ /* {THEMES[theme]} */\n"]
        c += ["    {" + ", ".join(map(str, by_theme[theme])) + f"}}, /* {key} */\n" for key, by_theme in values.items()]
        c += ["},\n"]
    c += ["};\n"]
    return {"theme_data.h": "".join(h), "theme_data.c": "".join(c)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--left", action="store_true", help="list what is not expressed, and why")
    args = parser.parse_args()
    head = subprocess.run(["git", "-C", str(args.checkout), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    if head != PIN["commit"]:
        raise SystemExit(f"error: the checkout is at {head}, the pin is {PIN['commit']}")
    brushes, values, left = collect(load(args.checkout))
    print(f"{len(brushes)} brushes, {len(values)} numbers, {len(left)} other resources")
    if args.left:
        for key, why in left.items():
            print(f"  {key}: {why}")
    stale = []
    for name, text in emit(brushes, values).items():
        path = ROOT / "ui" / name
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != text:
                stale.append(name)
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
    if stale:
        raise SystemExit(f"error: not what the pinned WinUI gives: {', '.join(stale)}")


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    main()

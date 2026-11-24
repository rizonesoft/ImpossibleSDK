"""Impossible UI draws what it should, and stays something the OS can take."""
import json
import os
import platform
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("IMPOSSIBLE_BUILD", ROOT / "build" / ("windows" if os.name == "nt" else "linux")))
sys.path.insert(0, str(ROOT / "tools"))
import bootstrap

LLVM = next(bootstrap.home(p) for p in bootstrap.components() if p["name"] == "llvm-mingw") / "bin"
EXE = ".exe" if os.name == "nt" else ""
WINUI = Path(os.environ.get("IMPOSSIBLE_WINUI", Path.home() / ".cache/impossible-sdk/winui"))


@unittest.skipUnless((LLVM / f"clang{EXE}").is_file(), "the pinned compiler is not installed here: the build in WSL runs this")
class Portable(unittest.TestCase):
    def test_it_builds_as_the_os_builds_and_calls_nothing(self):
        allowed = {"memcpy", "memmove", "memset", "memcmp"}
        with tempfile.TemporaryDirectory() as temp:
            objects = []
            for source in sorted((ROOT / "ui").glob("*.c")):
                objects.append(Path(temp, source.stem + ".o"))
                subprocess.run([str(LLVM / f"clang{EXE}"), "--target=x86_64-elf", "-std=gnu23", "-ffreestanding", "-O2",
                                "-Wall", "-Wextra", "-Werror", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2", "-mno-80387",
                                "-c", str(source), "-o", str(objects[-1])], check=True, timeout=120)
            defined, wanted = set(), set()
            for line in subprocess.run([str(LLVM / f"llvm-nm{EXE}"), *map(str, objects)], capture_output=True, text=True,
                                       check=True, timeout=60).stdout.splitlines():
                words = line.split()
                if len(words) == 2 and words[0] == "U":
                    wanted.add(words[1])
                elif len(words) == 3 and words[1].isupper():  # a small letter is local to its own object
                    defined.add(words[2])
            self.assertEqual(wanted - defined - allowed, set())


@unittest.skipUnless((WINUI / "LICENSE").is_file(), f"no WinUI checkout at {WINUI}")
class Generated(unittest.TestCase):
    def test_the_theme_data_is_what_the_pinned_winui_gives(self):
        result = subprocess.run([sys.executable, str(ROOT / "tools/winui/resources.py"), str(WINUI), "--check"],
                                capture_output=True, text=True, timeout=120)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_the_compiled_templates_match_the_pinned_winui(self):
        script = str(ROOT / "tools/winui/templates.py")
        res = subprocess.run([sys.executable, script, str(WINUI), "--check"], capture_output=True, text=True, timeout=120)
        self.assertEqual(res.returncode, 0, res.stderr)

        # Round-trip listing must name every compiled style, key element, and visual state.
        listed = subprocess.run([sys.executable, script, str(WINUI), "--list"], capture_output=True, text=True, timeout=120, check=True).stdout
        for needle in (
            "DefaultButtonStyle (Button)", "AccentButtonStyle (Button)", "SubtleButtonStyle (Button)",
            "DefaultToggleSwitchStyle (ToggleSwitch)", "SwitchThumb.Template (Thumb)",
            "DefaultTextBoxStyle (TextBox)", "DeleteButtonStyle (Button)",
            "ListView (ListView)", "ListView.ItemsPanel (ListView)",
            "DefaultListViewItemStyle (ListViewItem)", "ListViewItemExpanded (ListViewItem)",
            "IM_ELEM_CONTENT_PRESENTER name=ContentPresenter",
            "IM_ELEM_ITEMS_STACK_PANEL", "IM_ELEM_SCROLL_VIEWER name=ScrollViewer",
            "IM_ELEM_ITEMS_PRESENTER", "IM_ELEM_FONT_ICON name=MultiSelectCheck",
            "STATE PointerOver", "STATE Pressed", "STATE Dragging", "STATE Focused", "STATE Selected",
        ):
            self.assertIn(needle, listed)

        # Independently corrupt template_data.h and template_data.c in scratch: --check must fail without repairing.
        with tempfile.TemporaryDirectory() as tmp:
            tdir = Path(tmp)
            h_orig = (ROOT / "ui/template_data.h").read_text(encoding="utf-8")
            c_orig = (ROOT / "ui/template_data.c").read_text(encoding="utf-8")
            (tdir / "template_data.h").write_text(h_orig, encoding="utf-8")
            (tdir / "template_data.c").write_text(c_orig, encoding="utf-8")
            ok = subprocess.run([sys.executable, script, str(WINUI), "--check", "--out-dir", str(tdir)], capture_output=True, text=True, timeout=120)
            self.assertEqual(ok.returncode, 0, ok.stderr)

            mut_h = h_orig + "\n/* drift */\n"
            (tdir / "template_data.h").write_text(mut_h, encoding="utf-8")
            bad_h = subprocess.run([sys.executable, script, str(WINUI), "--check", "--out-dir", str(tdir)], capture_output=True, text=True, timeout=120)
            self.assertNotEqual(bad_h.returncode, 0)
            self.assertEqual((tdir / "template_data.h").read_text(encoding="utf-8"), mut_h)

            (tdir / "template_data.h").write_text(h_orig, encoding="utf-8")
            mut_c = c_orig.replace("IM_BRUSH_ButtonBorderBrushPointerOver", "IM_BRUSH_ButtonBorderBrushPressed", 1)
            (tdir / "template_data.c").write_text(mut_c, encoding="utf-8")
            bad_c = subprocess.run([sys.executable, script, str(WINUI), "--check", "--out-dir", str(tdir)], capture_output=True, text=True, timeout=120)
            self.assertNotEqual(bad_c.returncode, 0)
            self.assertEqual((tdir / "template_data.c").read_text(encoding="utf-8"), mut_c)


class TemplateCompiler(unittest.TestCase):
    def _run_fixture(self, xaml, name="fixture.xaml"):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / name
            path.write_text(xaml, encoding="utf-8")
            res = subprocess.run([sys.executable, str(ROOT / "tools/winui/templates.py"), "--parse-file", str(path)],
                                 capture_output=True, text=True, timeout=30)
            return res, path

    def test_refuses_unknown_element_with_file_and_line(self):
        xaml = '<ControlTemplate TargetType="Button">\n  <Grid>\n    <BogusWidget />\n  </Grid>\n</ControlTemplate>\n'
        res, path = self._run_fixture(xaml)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:3:", res.stderr)
        self.assertIn("unknown element", res.stderr)

    def test_refuses_unknown_property_on_multiline_element_with_exact_line(self):
        xaml = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Border\n'
            '    Width="10"\n'
            '    BogusProp="42"\n'
            '    Height="20" />\n'
            '</ControlTemplate>\n'
        )
        res, path = self._run_fixture(xaml)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:4:", res.stderr)
        self.assertIn("BogusProp", res.stderr)

    def test_refuses_unknown_converter_with_file_and_line(self):
        xaml = (
            '<ControlTemplate TargetType="TextBox">\n'
            '  <TextBlock\n'
            '    Text="{Binding Value, Converter={StaticResource MyConv}}" />\n'
            '</ControlTemplate>\n'
        )
        res, path = self._run_fixture(xaml)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:3:", res.stderr)
        self.assertIn("converter", res.stderr.lower())

    def test_refuses_unknown_animation_with_file_and_line(self):
        xaml = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid>\n'
            '    <VisualStateManager.VisualStateGroups>\n'
            '      <VisualStateGroup Name="CommonStates">\n'
            '        <VisualState Name="Pressed">\n'
            '          <Storyboard>\n'
            '            <BogusAnimation TargetName="Root" />\n'
            '          </Storyboard>\n'
            '        </VisualState>\n'
            '      </VisualStateGroup>\n'
            '    </VisualStateManager.VisualStateGroups>\n'
            '  </Grid>\n'
            '</ControlTemplate>\n'
        )
        res, path = self._run_fixture(xaml)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:7:", res.stderr)
        self.assertIn("unknown animation", res.stderr)

    def test_refuses_unknown_binding_mode_with_file_and_line(self):
        xaml = (
            '<ControlTemplate TargetType="TextBox">\n'
            '  <TextBlock\n'
            '    Text="{Binding Value, Mode=Sideways}" />\n'
            '</ControlTemplate>\n'
        )
        res, path = self._run_fixture(xaml)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:3:", res.stderr)
        self.assertIn("binding mode", res.stderr.lower())


EDGE_LIMIT = 1
STANDARD_DARK_CORNER_LIMIT = 3
STANDARD_LIGHT_CORNER_LIMIT = 6
ACCENT_CORNER_LIMIT = 14
PROBE_LIMIT = 40

ACCENT_KEYS = (
    "Accent", "AccentLight1", "AccentLight2", "AccentLight3",
    "AccentDark1", "AccentDark2", "AccentDark3",
)

EXPECTED_CASES = [
    *[
        ("button_case", control, state, theme, 1.0, 160, 64, None, f"{control}-{state}-{theme}.bgra")
        for theme in ("Dark", "Light")
        for control in ("button", "accent")
        for state in ("Normal", "PointerOver", "Pressed", "Disabled")
    ],
    ("probe", "probe-r4-s1", None, None, 1.0, 64, 64, [4, 4, 4, 4], "probe-r4-s1.bgra"),
    ("probe", "probe-r4-s15", None, None, 1.5, 64, 64, [4, 4, 4, 4], "probe-r4-s15.bgra"),
    ("probe", "probe-r4-s2", None, None, 2.0, 64, 64, [4, 4, 4, 4], "probe-r4-s2.bgra"),
    ("probe", "probe-r8-s1", None, None, 1.0, 64, 64, [8, 8, 8, 8], "probe-r8-s1.bgra"),
    ("probe", "probe-r8-s15", None, None, 1.5, 64, 64, [8, 8, 8, 8], "probe-r8-s15.bgra"),
    ("probe", "probe-r8-s2", None, None, 2.0, 64, 64, [8, 8, 8, 8], "probe-r8-s2.bgra"),
    ("probe", "probe-r16-s1", None, None, 1.0, 64, 64, [16, 16, 16, 16], "probe-r16-s1.bgra"),
    ("probe", "probe-r16-s15", None, None, 1.5, 64, 64, [16, 16, 16, 16], "probe-r16-s15.bgra"),
    ("probe", "probe-r16-s2", None, None, 2.0, 64, 64, [16, 16, 16, 16], "probe-r16-s2.bgra"),
    ("probe", "probe-asym-s1", None, None, 1.0, 64, 64, [16, 0, 0, 0], "probe-asym-s1.bgra"),
]


def validate_manifest(manifest):
    """Verify the reference manifest independently of its own count."""
    accents = manifest.get("accents", {})
    accent_args = []
    for key in ACCENT_KEYS:
        val = accents.get(key)
        if not isinstance(val, str) or len(val) != 8 or any(ch not in "0123456789abcdefABCDEF" for ch in val):
            raise ValueError(f"missing or malformed accent shade {key}: {val!r}")
        accent_args.append(val)
    cases = manifest.get("cases", [])
    seen = []
    files = []
    for case in cases:
        scale = case.get("scale")
        lw, lh = case.get("logical_width"), case.get("logical_height")
        w, h = case.get("width"), case.get("height")
        fn = case.get("file")
        if scale is None or lw is None or lh is None or w is None or h is None or not fn:
            raise ValueError(f"case missing scale, file or dimensions: {case}")
        if w != round(lw * scale) or h != round(lh * scale):
            raise ValueError(
                f"dimension/scale mismatch for {fn}: "
                f"{w}x{h} != round({lw}x{lh} * {scale})"
            )
        if case.get("control") == "probe":
            ident = ("probe", case.get("name"), None, None, float(scale), lw, lh, case.get("radii"), fn)
        else:
            ident = ("button_case", case.get("control"), case.get("state"), case.get("theme"), float(scale), lw, lh, None, fn)
        seen.append(ident)
        files.append(fn)
    if len(files) != len(set(files)):
        raise ValueError("duplicate reference filename in manifest")
    if len(seen) != len(set(tuple(x if not isinstance(x, list) else tuple(x) for x in s) for s in seen)):
        raise ValueError("duplicate case identity in manifest")
    if seen != EXPECTED_CASES:
        raise ValueError(f"manifest cases differ from expected 26 identities (got {len(seen)})")
    return accent_args


def check_probe_crispness(case, raw):
    """Catch bilinearly upscaled captures by requiring crisp 0->255 transitions on straight edges.

    Nearest-neighbour enlargement is caught separately by the full-frame pixel comparison against the SDK.
    """
    w, h = case["width"], case["height"]
    scale = case["scale"]
    bw = round(case["box_width"] * scale)
    bh = round(case["box_height"] * scale)
    bx0 = (w - bw) // 2
    by0 = (h - bh) // 2
    bx1 = bx0 + bw
    by1 = by0 + bh
    mid_x = w // 2
    mid_y = h // 2
    # Top and bottom midpoints must transition cleanly 0 -> 255 without bilinear blur.
    checks = [
        ("top-out", mid_x, by0 - 1, 0),
        ("top-in", mid_x, by0, 255),
        ("bot-in", mid_x, by1 - 1, 255),
        ("bot-out", mid_x, by1, 0),
        ("right-in", bx1 - 1, mid_y, 255),
        ("right-out", bx1, mid_y, 0),
    ]
    if case["radii"][0] == case["radii"][3]:
        checks.extend([("left-out", bx0 - 1, mid_y, 0), ("left-in", bx0, mid_y, 255)])
    for label, x, y, expected in checks:
        got = raw[(y * w + x) * 4]
        if got != expected:
            raise ValueError(f"{case['name']} {label} at ({x},{y}): expected {expected}, got {got}")


def bilinear_upscale(raw, sw, sh, dw, dh):
    """Bilinearly upscale a BGRA buffer from sw x sh to dw x dh (simulating the cheaper substitute)."""
    out = bytearray(dw * dh * 4)
    for dy in range(dh):
        sy = (dy + 0.5) * sh / dh - 0.5
        y0 = max(0, min(sh - 1, int(sy)))
        y1 = min(sh - 1, y0 + 1)
        fy = max(0.0, min(1.0, sy - y0))
        for dx in range(dw):
            sx = (dx + 0.5) * sw / dw - 0.5
            x0 = max(0, min(sw - 1, int(sx)))
            x1 = min(sw - 1, x0 + 1)
            fx = max(0.0, min(1.0, sx - x0))
            dst = (dy * dw + dx) * 4
            for k in range(4):
                v00 = raw[(y0 * sw + x0) * 4 + k]
                v10 = raw[(y0 * sw + x1) * 4 + k]
                v01 = raw[(y1 * sw + x0) * 4 + k]
                v11 = raw[(y1 * sw + x1) * 4 + k]
                top = v00 * (1.0 - fx) + v10 * fx
                bot = v01 * (1.0 - fx) + v11 * fx
                out[dst + k] = round(top * (1.0 - fy) + bot * fy)
    return bytes(out)


@unittest.skipUnless(os.name == "nt" or "microsoft" in platform.release().lower(), "needs Windows, directly or through WSL")
class Drawing(unittest.TestCase):
    def test_drawing(self):
        self.assertEqual(subprocess.run([str(BUILD / "ui-tests.exe")], stdin=subprocess.DEVNULL, capture_output=True, timeout=60).returncode, 0)

    def test_manifest_validation_catches_faults(self):
        folder = ROOT / "tests/reference"
        manifest = json.loads((folder / "cases.json").read_text(encoding="utf-8"))
        validate_manifest(manifest)

        # Removing one probe must fail.
        dropped = {**manifest, "cases": [c for c in manifest["cases"] if c.get("name") != "probe-r8-s15"]}
        with self.assertRaises(ValueError):
            validate_manifest(dropped)

        # Relabelling a 100% capture (64x64) as 150% (scale=1.5) must fail.
        relabelled_cases = [dict(c) for c in manifest["cases"]]
        for c in relabelled_cases:
            if c.get("name") == "probe-r4-s15":
                c["width"] = 64
                c["height"] = 64
        with self.assertRaises(ValueError):
            validate_manifest({**manifest, "cases": relabelled_cases})

        # Duplicating a case or pointing two cases to the same reference file must fail.
        duped = {**manifest, "cases": [*manifest["cases"], manifest["cases"][-1]]}
        with self.assertRaises(ValueError):
            validate_manifest(duped)
        shared_file = [dict(c) for c in manifest["cases"]]
        for c in shared_file:
            if c.get("control") == "button" and c.get("state") == "Disabled" and c.get("theme") == "Light":
                c["file"] = "button-Pressed-Light.bgra"
        with self.assertRaises(ValueError):
            validate_manifest({**manifest, "cases": shared_file})

        # Missing or malformed accent shade must fail.
        missing_accent = {**manifest, "accents": {k: v for k, v in manifest["accents"].items() if k != "AccentDark1"}}
        with self.assertRaises(ValueError):
            validate_manifest(missing_accent)
        bad_accent = {**manifest, "accents": {**manifest["accents"], "Accent": "ZZZZZZZZ"}}
        with self.assertRaises(ValueError):
            validate_manifest(bad_accent)

        # Upscaling a 100% probe to 96x96 and labelling it as 150% must fail crispness check.
        r4_s1 = zlib.decompress((folder / "probe-r4-s1.bgra.z").read_bytes())
        r4_s15_case = next(c for c in manifest["cases"] if c.get("name") == "probe-r4-s15")
        upscaled = bilinear_upscale(r4_s1, 64, 64, 96, 96)
        with self.assertRaises(ValueError):
            check_probe_crispness(r4_s15_case, upscaled)

    def test_control_shots_honours_accent_palette_and_rejects_malformed(self):
        custom = ["FF11AA22", "FF22BB33", "FF33CC44", "FF44DD55", "FF556677", "FF667788", "FF778899"]
        ours = subprocess.run([str(BUILD / "control-shots.exe"), *custom], capture_output=True, timeout=60, check=True).stdout
        # Case 4 is accent Normal Dark (uses AccentLight2 = 0xFF33CC44); center pixel (80, 32).
        dark_offset = 4 * 160 * 64 * 4 + (32 * 160 + 80) * 4
        self.assertEqual((ours[dark_offset + 2], ours[dark_offset + 1], ours[dark_offset]), (0x33, 0xCC, 0x44))
        # Case 12 is accent Normal Light (uses AccentDark1 = 0xFF556677); center pixel (80, 32).
        light_offset = 12 * 160 * 64 * 4 + (32 * 160 + 80) * 4
        self.assertEqual((ours[light_offset + 2], ours[light_offset + 1], ours[light_offset]), (0x55, 0x66, 0x77))
        # Malformed accent argument must be refused with non-zero exit code.
        bad = subprocess.run([str(BUILD / "control-shots.exe"), "NOTHEX"], capture_output=True, timeout=60)
        self.assertNotEqual(bad.returncode, 0)

    def test_guide_documents_verified_limits(self):
        guide = (ROOT / "docs/guide/button.md").read_text(encoding="utf-8")
        self.assertIn(f"{EDGE_LIMIT} of 255", guide)
        self.assertIn(f"{STANDARD_DARK_CORNER_LIMIT} of 255", guide)
        self.assertIn(f"{STANDARD_LIGHT_CORNER_LIMIT} of 255", guide)
        self.assertIn(f"{ACCENT_CORNER_LIMIT} of 255", guide)
        self.assertIn(f"{PROBE_LIMIT} of 255", guide)

    def test_buttons_match_real_winui(self):
        """Straight edges and fills may differ by rounding only; corners and probes have verified limits."""
        folder = ROOT / "tests/reference"
        manifest = json.loads((folder / "cases.json").read_text(encoding="utf-8"))
        accent_args = validate_manifest(manifest)
        cases = manifest["cases"]
        ours = subprocess.run([str(BUILD / "control-shots.exe"), *accent_args], capture_output=True, timeout=60, check=True).stdout
        expected_bytes = sum(c["width"] * c["height"] * 4 for c in cases)
        self.assertEqual(len(ours), expected_bytes)
        offset = 0
        for case in cases:
            w, h = case["width"], case["height"]
            size = w * h * 4
            mine = ours[offset:offset + size]
            offset += size
            real = zlib.decompress((folder / f"{case['file']}.z").read_bytes())
            self.assertEqual(len(real), size, case["file"])
            if case["control"] == "probe":
                check_probe_crispness(case, real)
                worst = 0
                for p in range(w * h):
                    off = max(abs(mine[p * 4 + k] - real[p * 4 + k]) for k in range(3))
                    if off > worst:
                        worst = off
                self.assertLessEqual(worst, PROBE_LIMIT, case["name"])
            else:
                edge = corner = 0
                for p in range(w * h):
                    x, y = p % w, p // w
                    off = max(abs(mine[p * 4 + k] - real[p * 4 + k]) for k in range(3))
                    if (x < 25 or x >= 135) and (y < 21 or y >= 43):
                        corner = max(corner, off)
                    else:
                        edge = max(edge, off)
                name = f"{case['control']} {case['state']} {case['theme']}"
                self.assertLessEqual(edge, EDGE_LIMIT, name)
                if case["control"] == "accent":
                    limit = ACCENT_CORNER_LIMIT
                else:
                    limit = STANDARD_DARK_CORNER_LIMIT if case["theme"] == "Dark" else STANDARD_LIGHT_CORNER_LIMIT
                self.assertLessEqual(corner, limit, name)


if __name__ == "__main__":
    unittest.main()

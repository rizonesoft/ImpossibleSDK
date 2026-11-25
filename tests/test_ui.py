"""Impossible UI draws what it should, and stays something the OS can take."""
import hashlib
import json
import os
import platform
import re
import shutil
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

sys.path.insert(0, str(ROOT / "tests"))
from frames import compare_frames_with_layout_mask, extract_mask_rects, validate_script_log

LLVM = next(bootstrap.home(p) for p in bootstrap.components() if p["name"] == "llvm-mingw") / "bin"
EXE = ".exe" if os.name == "nt" else ""
WINUI = Path(os.environ.get("IMPOSSIBLE_WINUI", Path.home() / ".cache/impossible-sdk/winui"))


@unittest.skipUnless((LLVM / f"clang{EXE}").is_file(), "the pinned compiler is not installed here: the build in WSL runs this")
class Portable(unittest.TestCase):
    @staticmethod
    def _portable_unresolved_symbols(ui_dir: Path) -> set[str]:
        allowed = {"memcpy", "memmove", "memset", "memcmp"}
        parts_manifest = json.loads((ROOT / "parts.json").read_text(encoding="utf-8"))
        parts_by_name = {p["name"]: p for p in parts_manifest["parts"]}
        dep_dirs = []
        dep_sources = []
        seen = set()
        queue = list(parts_by_name.get("ui", {}).get("depends", []))
        while queue:
            name = queue.pop(0)
            if name in seen or name not in parts_by_name:
                continue
            seen.add(name)
            part = parts_by_name[name]
            dep_dirs.append(ROOT / part["folder"])
            dep_sources.extend(ROOT / s for s in part["sources"])
            queue.extend(part.get("depends", []))
        inc_flags = [f"-I{ui_dir}", *(f"-I{d}" for d in dep_dirs)]
        with tempfile.TemporaryDirectory() as temp:
            objects = []
            for idx, source in enumerate([*sorted(ui_dir.glob("*.c")), *dep_sources]):
                objects.append(Path(temp, f"{idx}_{source.stem}.o"))
                subprocess.run([str(LLVM / f"clang{EXE}"), "--target=x86_64-elf", "-std=gnu23", "-ffreestanding", "-O2",
                                "-Wall", "-Wextra", "-Werror", "-mno-red-zone", "-mno-mmx", "-mno-sse", "-mno-sse2", "-mno-80387",
                                *inc_flags, "-c", str(source), "-o", str(objects[-1])], check=True, timeout=120)
            defined, wanted = set(), set()
            for line in subprocess.run([str(LLVM / f"llvm-nm{EXE}"), *map(str, objects)], capture_output=True, text=True,
                                       check=True, timeout=60).stdout.splitlines():
                words = line.split()
                if len(words) == 2 and words[0] == "U":
                    wanted.add(words[1])
                elif len(words) == 3 and words[1].isupper():  # a small letter is local to its own object
                    defined.add(words[2])
            return wanted - defined - allowed

    def test_it_builds_as_the_os_builds_and_calls_nothing(self):
        self.assertEqual(self._portable_unresolved_symbols(ROOT / "ui"), set())

    def test_planted_system_call_in_ui_is_refused(self):
        with tempfile.TemporaryDirectory() as temp:
            ui_copy = Path(temp) / "ui"
            shutil.copytree(ROOT / "ui", ui_copy)
            # Clean copy compiles and has zero unresolved external symbols.
            self.assertEqual(self._portable_unresolved_symbols(ui_copy), set())
            # Plant a Windows/OS system call into ui/ and verify it compiles cleanly but fails symbol refusal.
            (ui_copy / "host.c").write_text(
                (ui_copy / "host.c").read_text(encoding="utf-8")
                + "\nextern void ExitProcess(unsigned code);\nvoid im_planted_os_call(void) { ExitProcess(0u); }\n",
                encoding="utf-8",
            )
            unresolved = self._portable_unresolved_symbols(ui_copy)
            self.assertIn("ExitProcess", unresolved)

    def test_win_host_header_compiles_with_sdk_include_only(self):
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / "consumer.c"
            obj = Path(temp) / "consumer.o"
            src.write_text(
                "#include <impossible/win_host.h>\n"
                "void check_decl(struct im_host *h, im_window *w) { im_win_host_init(h, w); }\n",
                encoding="utf-8",
            )
            subprocess.run([str(LLVM / f"clang{EXE}"), "--target=x86_64-w64-mingw32", "-std=gnu23", "-ffreestanding",
                            "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'include'}", "-c", str(src), "-o", str(obj)],
                           check=True, timeout=60)


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

        # Round-trip listing must match the complete ordered element, state, and transition inventory for all 12 templates.
        listed = subprocess.run([sys.executable, script, str(WINUI), "--list"], capture_output=True, text=True, timeout=120, check=True).stdout
        blocks = {}
        current = None
        for line in listed.splitlines():
            if line.startswith("TEMPLATE["):
                current = line.split("] ", 1)[1].split(":", 1)[0]
                blocks[current] = []
            elif current is not None:
                blocks[current].append(line.strip())
        expected_inventory = {
            "DefaultButtonStyle (Button)": [
                "ELEM[0] parent=-1 IM_ELEM_CONTENT_PRESENTER name=ContentPresenter",
                "ELEM[1] parent=0 IM_ELEM_BRUSH_TRANSITION name=-",
                "GROUP CommonStates: states=4 transitions=0",
                "STATE Normal: setters=0 anims=0",
                "STATE PointerOver: setters=1 anims=3",
                "STATE Pressed: setters=1 anims=3",
                "STATE Disabled: setters=1 anims=3",
            ],
            "AccentButtonStyle (Button)": [
                "ELEM[0] parent=-1 IM_ELEM_CONTENT_PRESENTER name=ContentPresenter",
                "ELEM[1] parent=0 IM_ELEM_BRUSH_TRANSITION name=-",
                "GROUP CommonStates: states=4 transitions=0",
                "STATE Normal: setters=0 anims=0",
                "STATE PointerOver: setters=0 anims=3",
                "STATE Pressed: setters=0 anims=3",
                "STATE Disabled: setters=0 anims=3",
            ],
            "SubtleButtonStyle (Button)": [
                "ELEM[0] parent=-1 IM_ELEM_CONTENT_PRESENTER name=ContentPresenter",
                "ELEM[1] parent=0 IM_ELEM_BRUSH_TRANSITION name=-",
                "GROUP CommonStates: states=4 transitions=0",
                "STATE Normal: setters=0 anims=0",
                "STATE PointerOver: setters=1 anims=3",
                "STATE Pressed: setters=1 anims=3",
                "STATE Disabled: setters=1 anims=3",
            ],
            "SwitchThumb.Template (Thumb)": [
                "ELEM[0] parent=-1 IM_ELEM_RECTANGLE name=-",
            ],
            "DefaultToggleSwitchStyle (ToggleSwitch)": [
                "ELEM[0] parent=-1 IM_ELEM_GRID name=-",
                "ELEM[1] parent=0 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[2] parent=0 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[3] parent=0 IM_ELEM_CONTENT_PRESENTER name=HeaderContentPresenter",
                "ELEM[4] parent=0 IM_ELEM_GRID name=-",
                "ELEM[5] parent=4 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[6] parent=4 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[7] parent=4 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[8] parent=4 IM_ELEM_COLUMN_DEFINITION name=-",
                "ELEM[9] parent=4 IM_ELEM_COLUMN_DEFINITION name=-",
                "ELEM[10] parent=4 IM_ELEM_COLUMN_DEFINITION name=-",
                "ELEM[11] parent=4 IM_ELEM_GRID name=SwitchAreaGrid",
                "ELEM[12] parent=4 IM_ELEM_CONTENT_PRESENTER name=OffContentPresenter",
                "ELEM[13] parent=4 IM_ELEM_CONTENT_PRESENTER name=OnContentPresenter",
                "ELEM[14] parent=4 IM_ELEM_RECTANGLE name=OuterBorder",
                "ELEM[15] parent=4 IM_ELEM_RECTANGLE name=SwitchKnobBounds",
                "ELEM[16] parent=4 IM_ELEM_GRID name=SwitchKnob",
                "ELEM[17] parent=16 IM_ELEM_BORDER name=SwitchKnobOn",
                "ELEM[18] parent=17 IM_ELEM_COMPOSITE_TRANSFORM name=-",
                "ELEM[19] parent=16 IM_ELEM_RECTANGLE name=SwitchKnobOff",
                "ELEM[20] parent=19 IM_ELEM_COMPOSITE_TRANSFORM name=-",
                "ELEM[21] parent=16 IM_ELEM_TRANSLATE_TRANSFORM name=KnobTranslateTransform",
                "ELEM[22] parent=4 IM_ELEM_THUMB name=SwitchThumb",
                "GROUP CommonStates: states=4 transitions=0",
                "STATE Normal: setters=0 anims=11",
                "STATE PointerOver: setters=0 anims=11",
                "STATE Pressed: setters=4 anims=11",
                "STATE Disabled: setters=0 anims=14",
                "GROUP ToggleStates: states=3 transitions=5",
                "STATE Dragging: setters=0 anims=0",
                "STATE Off: setters=0 anims=0",
                "STATE On: setters=0 anims=5",
                "TRANSITION Dragging -> On dur=0ms anims=5",
                "TRANSITION On -> Dragging dur=0ms anims=3",
                "TRANSITION Dragging -> Off dur=0ms anims=4",
                "TRANSITION On -> Off dur=0ms anims=1",
                "TRANSITION Off -> On dur=0ms anims=5",
                "GROUP ContentStates: states=2 transitions=0",
                "STATE OffContent: setters=0 anims=2",
                "STATE OnContent: setters=0 anims=2",
            ],
            "DeleteButtonStyle (Button)": [
                "ELEM[0] parent=-1 IM_ELEM_GRID name=ButtonLayoutGrid",
                "ELEM[1] parent=0 IM_ELEM_TEXT_BLOCK name=GlyphElement",
                "GROUP CommonStates: states=4 transitions=0",
                "STATE Normal: setters=0 anims=0",
                "STATE PointerOver: setters=0 anims=3",
                "STATE Pressed: setters=0 anims=3",
                "STATE Disabled: setters=0 anims=1",
            ],
            "DefaultTextBoxStyle (TextBox)": [
                "ELEM[0] parent=-1 IM_ELEM_GRID name=-",
                "ELEM[1] parent=0 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[2] parent=0 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[3] parent=0 IM_ELEM_ROW_DEFINITION name=-",
                "ELEM[4] parent=0 IM_ELEM_COLUMN_DEFINITION name=-",
                "ELEM[5] parent=0 IM_ELEM_COLUMN_DEFINITION name=-",
                "ELEM[6] parent=0 IM_ELEM_CONTENT_PRESENTER name=HeaderContentPresenter",
                "ELEM[7] parent=0 IM_ELEM_BORDER name=BorderElement",
                "ELEM[8] parent=0 IM_ELEM_SCROLL_VIEWER name=ContentElement",
                "ELEM[9] parent=0 IM_ELEM_TEXT_BLOCK name=PlaceholderTextContentPresenter",
                "ELEM[10] parent=0 IM_ELEM_BUTTON name=DeleteButton",
                "ELEM[11] parent=0 IM_ELEM_CONTENT_PRESENTER name=DescriptionPresenter",
                "GROUP CommonStates: states=4 transitions=0",
                "STATE Normal: setters=0 anims=0",
                "STATE Disabled: setters=0 anims=5",
                "STATE PointerOver: setters=0 anims=4",
                "STATE Focused: setters=0 anims=5",
                "GROUP ButtonStates: states=2 transitions=0",
                "STATE ButtonVisible: setters=0 anims=1",
                "STATE ButtonCollapsed: setters=0 anims=0",
            ],
            "ListView.ItemContainerTransitions (ListView)": [
                "ELEM[0] parent=-1 IM_ELEM_ADD_DELETE_THEME_TRANSITION name=-",
                "ELEM[1] parent=-1 IM_ELEM_CONTENT_THEME_TRANSITION name=-",
                "ELEM[2] parent=-1 IM_ELEM_REORDER_THEME_TRANSITION name=-",
                "ELEM[3] parent=-1 IM_ELEM_ENTRANCE_THEME_TRANSITION name=-",
            ],
            "ListView.ItemsPanel (ListView)": [
                "ELEM[0] parent=-1 IM_ELEM_ITEMS_STACK_PANEL name=-",
            ],
            "ListView (ListView)": [
                "ELEM[0] parent=-1 IM_ELEM_BORDER name=-",
                "ELEM[1] parent=0 IM_ELEM_SCROLL_VIEWER name=ScrollViewer",
                "ELEM[2] parent=1 IM_ELEM_ITEMS_PRESENTER name=-",
            ],
            "DefaultListViewItemStyle (ListViewItem)": [
                "ELEM[0] parent=-1 IM_ELEM_LIST_VIEW_ITEM_PRESENTER name=Root",
            ],
            "ListViewItemExpanded (ListViewItem)": [
                "ELEM[0] parent=-1 IM_ELEM_GRID name=ContentBorder",
                "ELEM[1] parent=0 IM_ELEM_SCALE_TRANSFORM name=ContentBorderScale",
                "ELEM[2] parent=0 IM_ELEM_RECTANGLE name=BorderBackground",
                "ELEM[3] parent=0 IM_ELEM_GRID name=ContentPresenterGrid",
                "ELEM[4] parent=3 IM_ELEM_TRANSLATE_TRANSFORM name=ContentPresenterTranslateTransform",
                "ELEM[5] parent=3 IM_ELEM_CONTENT_PRESENTER name=ContentPresenter",
                "ELEM[6] parent=0 IM_ELEM_TEXT_BLOCK name=PlaceholderTextBlock",
                "ELEM[7] parent=0 IM_ELEM_RECTANGLE name=PlaceholderRect",
                "ELEM[8] parent=0 IM_ELEM_BORDER name=MultiSelectSquare",
                "ELEM[9] parent=8 IM_ELEM_RECTANGLE_GEOMETRY name=-",
                "ELEM[10] parent=9 IM_ELEM_TRANSLATE_TRANSFORM name=MultiSelectClipTransform",
                "ELEM[11] parent=8 IM_ELEM_TRANSLATE_TRANSFORM name=MultiSelectCheckBoxTransform",
                "ELEM[12] parent=8 IM_ELEM_FONT_ICON name=MultiSelectCheck",
                "ELEM[13] parent=0 IM_ELEM_BORDER name=MultiArrangeOverlayTextBorder",
                "ELEM[14] parent=13 IM_ELEM_TEXT_BLOCK name=MultiArrangeOverlayText",
                "GROUP CommonStates: states=6 transitions=0",
                "STATE Normal: setters=0 anims=0",
                "STATE PointerOver: setters=0 anims=5",
                "STATE Pressed: setters=0 anims=5",
                "STATE Selected: setters=0 anims=6",
                "STATE PointerOverSelected: setters=0 anims=6",
                "STATE PressedSelected: setters=0 anims=6",
                "GROUP DisabledStates: states=2 transitions=0",
                "STATE Enabled: setters=0 anims=0",
                "STATE Disabled: setters=0 anims=1",
                "GROUP MultiSelectStates: states=2 transitions=0",
                "STATE MultiSelectDisabled: setters=0 anims=4",
                "STATE MultiSelectEnabled: setters=0 anims=6",
                "GROUP DataVirtualizationStates: states=2 transitions=0",
                "STATE DataAvailable: setters=0 anims=0",
                "STATE DataPlaceholder: setters=0 anims=2",
                "GROUP ReorderHintStates: states=5 transitions=1",
                "STATE NoReorderHint: setters=0 anims=0",
                "STATE BottomReorderHint: setters=0 anims=1",
                "STATE TopReorderHint: setters=0 anims=1",
                "STATE RightReorderHint: setters=0 anims=1",
                "STATE LeftReorderHint: setters=0 anims=1",
                "TRANSITION * -> NoReorderHint dur=200ms anims=0",
                "GROUP DragStates: states=11 transitions=1",
                "STATE NotDragging: setters=0 anims=0",
                "STATE Dragging: setters=0 anims=2",
                "STATE DraggingTarget: setters=0 anims=0",
                "STATE MultipleDraggingPrimary: setters=0 anims=8",
                "STATE MultipleDraggingSecondary: setters=0 anims=0",
                "STATE DraggedPlaceholder: setters=0 anims=0",
                "STATE Reordering: setters=0 anims=1",
                "STATE ReorderingTarget: setters=0 anims=3",
                "STATE MultipleReorderingPrimary: setters=0 anims=7",
                "STATE ReorderedPlaceholder: setters=0 anims=1",
                "STATE DragOver: setters=0 anims=1",
                "TRANSITION * -> NotDragging dur=200ms anims=0",
            ],
        }
        self.assertEqual(blocks, expected_inventory)

        # Compiled templates must never emit Microsoft font names (Segoe).
        h_orig = (ROOT / "ui/template_data.h").read_text(encoding="utf-8")
        c_orig = (ROOT / "ui/template_data.c").read_text(encoding="utf-8")
        self.assertNotIn("Segoe", h_orig)
        self.assertNotIn("Segoe", c_orig)

        # Independently corrupt template_data.h and template_data.c in scratch: --check must fail without repairing.
        with tempfile.TemporaryDirectory() as tmp:
            tdir = Path(tmp)
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

    def test_refuses_unknown_keyframe_attr_and_child_and_root(self):
        xaml_attr = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="Opacity">\n'
            '      <LinearDoubleKeyFrame Bogus="1" Value="1" />\n'
            '    </DoubleAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res, path = self._run_fixture(xaml_attr)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:4:", res.stderr)

        xaml_child = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="Opacity">\n'
            '      <LinearDoubleKeyFrame Value="1"><Unknown /></LinearDoubleKeyFrame>\n'
            '    </DoubleAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res2, path2 = self._run_fixture(xaml_child)
        self.assertNotEqual(res2.returncode, 0)
        self.assertIn(f"{path2}:4:", res2.stderr)

        res3, path3 = self._run_fixture("<Unknown />\n")
        self.assertNotEqual(res3.returncode, 0)
        self.assertIn(f"{path3}:1:", res3.stderr)

    def test_exact_attr_line_on_multiline_target_property_and_quoted_gt(self):
        xaml_tp = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation\n'
            '      Storyboard.TargetName="R"\n'
            '      Storyboard.TargetProperty="BogusProp" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res, path = self._run_fixture(xaml_tp)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:5:", res.stderr)

        xaml_gt = (
            '<ControlTemplate TargetType="Button">\n'
            '  <TextBlock Text="a > b"\n'
            '    BogusAfterGt="1" />\n'
            '</ControlTemplate>\n'
        )
        res2, path2 = self._run_fixture(xaml_gt)
        self.assertNotEqual(res2.returncode, 0)
        self.assertIn(f"{path2}:3:", res2.stderr)

    def test_grid_resources_do_not_leak_to_sibling_styles(self):
        xaml = (
            '<ResourceDictionary xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n'
            '  <Style x:Key="S1" TargetType="Button"><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">\n'
            '    <Grid><Grid.Resources><Style x:Key="Local" TargetType="Button"><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Border /></ControlTemplate></Setter.Value></Setter></Style></Grid.Resources></Grid>\n'
            '  </ControlTemplate></Setter.Value></Setter></Style>\n'
            '  <Style x:Key="S2" TargetType="Button"><Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button">\n'
            '    <Button Style="{StaticResource Local}" />\n'
            '  </ControlTemplate></Setter.Value></Setter></Style>\n'
            '</ResourceDictionary>\n'
        )
        res, path = self._run_fixture(xaml)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:6:", res.stderr)

    def test_refuses_unknown_visual_state_property_with_exact_line(self):
        xaml = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid>\n'
            '    <VisualStateManager.VisualStateGroups>\n'
            '      <VisualStateGroup Name="CommonStates">\n'
            '        <VisualState\n'
            '          Name="Normal"\n'
            '          BogusStateAttr="1" />\n'
            '      </VisualStateGroup>\n'
            '    </VisualStateManager.VisualStateGroups>\n'
            '  </Grid>\n'
            '</ControlTemplate>\n'
        )
        res, path = self._run_fixture(xaml)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn(f"{path}:7:", res.stderr)
        self.assertIn("BogusStateAttr", res.stderr)

        xaml_ns = (
            '<ControlTemplate xmlns:vendor="urn:vendor" TargetType="Button">\n'
            '  <Grid>\n'
            '    <VisualStateManager.VisualStateGroups>\n'
            '      <VisualStateGroup Name="CommonStates">\n'
            '        <VisualState Name="Normal"\n'
            '          vendor:Name="Foreign" />\n'
            '      </VisualStateGroup>\n'
            '    </VisualStateManager.VisualStateGroups>\n'
            '  </Grid>\n'
            '</ControlTemplate>\n'
        )
        res_ns, path_ns = self._run_fixture(xaml_ns)
        self.assertNotEqual(res_ns.returncode, 0)
        self.assertIn(f"{path_ns}:6:", res_ns.stderr)
        self.assertIn("namespace", res_ns.stderr.lower())

        # Reverse order (vendor:Name before Name) must still report vendor:Name's exact line (line 5).
        xaml_rev = (
            '<ControlTemplate xmlns:vendor="urn:vendor" TargetType="Button">\n'
            '  <Grid>\n'
            '    <VisualStateManager.VisualStateGroups>\n'
            '      <VisualStateGroup Name="CommonStates">\n'
            '        <VisualState vendor:Name="Foreign"\n'
            '          Name="Normal" />\n'
            '      </VisualStateGroup>\n'
            '    </VisualStateManager.VisualStateGroups>\n'
            '  </Grid>\n'
            '</ControlTemplate>\n'
        )
        res_rev, path_rev = self._run_fixture(xaml_rev)
        self.assertNotEqual(res_rev.returncode, 0)
        self.assertIn(f"{path_rev}:5:", res_rev.stderr)

        # mc:Name (unsupported attribute in markup-compatibility namespace) must be rejected on its exact line (line 6).
        xaml_mc = (
            '<ControlTemplate xmlns:mc="http://schemas.openxmlformats.org/markup-compatibility/2006" TargetType="Button">\n'
            '  <Grid>\n'
            '    <VisualStateManager.VisualStateGroups>\n'
            '      <VisualStateGroup Name="CommonStates">\n'
            '        <VisualState\n'
            '          mc:Name="Foreign" />\n'
            '      </VisualStateGroup>\n'
            '    </VisualStateManager.VisualStateGroups>\n'
            '  </Grid>\n'
            '</ControlTemplate>\n'
        )
        res_mc, path_mc = self._run_fixture(xaml_mc)
        self.assertNotEqual(res_mc.returncode, 0)
        self.assertIn(f"{path_mc}:6:", res_mc.stderr)

        # Conditional XAML namespace (presentation?UnknownPredicate()) and implicit xml: prefix must be rejected on exact lines.
        xaml_cond = (
            '<ControlTemplate xmlns:q="http://schemas.microsoft.com/winfx/2006/xaml/presentation?UnknownPredicate()" TargetType="Button">\n'
            '  <Grid>\n'
            '    <VisualStateManager.VisualStateGroups>\n'
            '      <VisualStateGroup Name="CommonStates">\n'
            '        <VisualState\n'
            '          q:Name="Foreign" />\n'
            '      </VisualStateGroup>\n'
            '    </VisualStateManager.VisualStateGroups>\n'
            '  </Grid>\n'
            '</ControlTemplate>\n'
        )
        res_cond, path_cond = self._run_fixture(xaml_cond)
        self.assertNotEqual(res_cond.returncode, 0)
        self.assertIn(f"{path_cond}:6:", res_cond.stderr)

        xaml_xml = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid>\n'
            '    <VisualStateManager.VisualStateGroups>\n'
            '      <VisualStateGroup Name="CommonStates">\n'
            '        <VisualState Name="Normal"\n'
            '          xml:lang="en" />\n'
            '      </VisualStateGroup>\n'
            '    </VisualStateManager.VisualStateGroups>\n'
            '  </Grid>\n'
            '</ControlTemplate>\n'
        )
        res_xml, path_xml = self._run_fixture(xaml_xml)
        self.assertNotEqual(res_xml.returncode, 0)
        self.assertIn(f"{path_xml}:6:", res_xml.stderr)

        # controls:Name, VisualState.Setters attributes, and non-ASCII attribute names must all be rejected on exact lines.
        xaml_ctrl = (
            '<ControlTemplate xmlns:c="using:Microsoft.UI.Xaml.Controls" TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G">\n'
            '    <VisualState\n'
            '      c:Name="Foreign" />\n'
            '  </VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_ctrl, path_ctrl = self._run_fixture(xaml_ctrl)
        self.assertNotEqual(res_ctrl.returncode, 0)
        self.assertIn(f"{path_ctrl}:4:", res_ctrl.stderr)

        xaml_wrap = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S">\n'
            '    <VisualState.Setters\n'
            '      Bogus="1" />\n'
            '  </VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_wrap, path_wrap = self._run_fixture(xaml_wrap)
        self.assertNotEqual(res_wrap.returncode, 0)
        self.assertIn(f"{path_wrap}:4:", res_wrap.stderr)

        xaml_uni = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G">\n'
            '    <VisualState Name="Normal"\n'
            '      \u00e9chec="1" />\n'
            '  </VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_uni, path_uni = self._run_fixture(xaml_uni)
        self.assertNotEqual(res_uni.returncode, 0)
        self.assertIn(f"{path_uni}:4:", res_uni.stderr)

        # Bare Key on VisualState, Setter.Value attributes inside VisualState.Setters, and foreign-namespace elements must be rejected.
        xaml_key = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G">\n'
            '    <VisualState\n'
            '      Key="Foreign" />\n'
            '  </VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_key, path_key = self._run_fixture(xaml_key)
        self.assertNotEqual(res_key.returncode, 0)
        self.assertIn(f"{path_key}:4:", res_key.stderr)

        xaml_sv = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="Box.Width"><Setter.Value\n'
            '      Bogus="1" /></Setter>\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_sv, path_sv = self._run_fixture(xaml_sv)
        self.assertNotEqual(res_sv.returncode, 0)
        self.assertIn(f"{path_sv}:4:", res_sv.stderr)

        xaml_f_el = (
            '<ControlTemplate xmlns:vendor="urn:vendor" TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S">\n'
            '    <vendor:Storyboard />\n'
            '  </VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_fe, path_fe = self._run_fixture(xaml_f_el)
        self.assertNotEqual(res_fe.returncode, 0)
        self.assertIn(f"{path_fe}:3:", res_fe.stderr)

        # x:Storyboard, VisualState Setter with both Target+Property, and multiline Property="Bogus" must be rejected on exact lines.
        xaml_xsb = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S">\n'
            '    <x:Storyboard />\n'
            '  </VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_xsb, path_xsb = self._run_fixture(xaml_xsb)
        self.assertNotEqual(res_xsb.returncode, 0)
        self.assertIn(f"{path_xsb}:3:", res_xsb.stderr)

        xaml_both = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="Box.Width"\n'
            '      Property="Bogus" Value="1" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_both, path_both = self._run_fixture(xaml_both)
        self.assertNotEqual(res_both.returncode, 0)
        self.assertIn(f"{path_both}:4:", res_both.stderr)

        xaml_prop = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter\n'
            '      Property="Bogus" Value="1" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_prop, path_prop = self._run_fixture(xaml_prop)
        self.assertNotEqual(res_prop.returncode, 0)
        self.assertIn(f"{path_prop}:4:", res_prop.stderr)

        # Keyframe scalar value elements must reject attributes, child elements, and presentation-namespace Boolean.
        xaml_kf_attr = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="IsHitTestVisible">\n'
            '      <DiscreteObjectKeyFrame KeyTime="0"><DiscreteObjectKeyFrame.Value><x:Boolean Bogus="1">True</x:Boolean></DiscreteObjectKeyFrame.Value></DiscreteObjectKeyFrame>\n'
            '    </ObjectAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_kfa, path_kfa = self._run_fixture(xaml_kf_attr)
        self.assertNotEqual(res_kfa.returncode, 0)
        self.assertIn(f"{path_kfa}:4:", res_kfa.stderr)

        xaml_kf_child = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="IsHitTestVisible">\n'
            '      <DiscreteObjectKeyFrame KeyTime="0"><DiscreteObjectKeyFrame.Value><x:Boolean>True<Unknown /></x:Boolean></DiscreteObjectKeyFrame.Value></DiscreteObjectKeyFrame>\n'
            '    </ObjectAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_kfc, path_kfc = self._run_fixture(xaml_kf_child)
        self.assertNotEqual(res_kfc.returncode, 0)
        self.assertIn(f"{path_kfc}:4:", res_kfc.stderr)

        xaml_pres_bool = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="IsHitTestVisible">\n'
            '      <DiscreteObjectKeyFrame KeyTime="0"><DiscreteObjectKeyFrame.Value><Boolean xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation">True</Boolean></DiscreteObjectKeyFrame.Value></DiscreteObjectKeyFrame>\n'
            '    </ObjectAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_pb, path_pb = self._run_fixture(xaml_pres_bool)
        self.assertNotEqual(res_pb.returncode, 0)
        self.assertIn(f"{path_pb}:4:", res_pb.stderr)

        # Conflicting animation alias attrs, unqualified <Boolean>, and extra child in Style Setter.Value must all be rejected.
        xaml_alias = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation Storyboard.TargetName="R" Storyboard.TargetProperty="IsHitTestVisible"\n'
            '      TargetProperty="Bogus" To="1" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_al, path_al = self._run_fixture(xaml_alias)
        self.assertNotEqual(res_al.returncode, 0)
        self.assertIn(f"{path_al}:4:", res_al.stderr)

        xaml_unq_bool = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="IsHitTestVisible">\n'
            '      <DiscreteObjectKeyFrame KeyTime="0"><DiscreteObjectKeyFrame.Value><Boolean>True</Boolean></DiscreteObjectKeyFrame.Value></DiscreteObjectKeyFrame>\n'
            '    </ObjectAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_ub, path_ub = self._run_fixture(xaml_unq_bool)
        self.assertNotEqual(res_ub.returncode, 0)
        self.assertIn(f"{path_ub}:4:", res_ub.stderr)

        xaml_extra_sv = (
            '<Style TargetType="Button">\n'
            '  <Setter Property="Template">\n'
            '    <Setter.Value>\n'
            '      <ControlTemplate TargetType="Button"><Grid /></ControlTemplate>\n'
            '      <Unknown Bogus="1" />\n'
            '    </Setter.Value>\n'
            '  </Setter>\n'
            '</Style>\n'
        )
        res_esv, path_esv = self._run_fixture(xaml_extra_sv)
        self.assertNotEqual(res_esv.returncode, 0)
        self.assertIn(f"{path_esv}:5:", res_esv.stderr)

        # Compound TargetProperty with unknown intermediate segment and duplicate keyframe value wrappers must be rejected on exact lines.
        xaml_compound = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ColorAnimationUsingKeyFrames Storyboard.TargetName="R"\n'
            '      Storyboard.TargetProperty="(Border.Bogus).(SolidColorBrush.Color)" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_cmp, path_cmp = self._run_fixture(xaml_compound)
        self.assertNotEqual(res_cmp.returncode, 0)
        self.assertIn(f"{path_cmp}:4:", res_cmp.stderr)

        xaml_dup_kf = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="IsHitTestVisible">\n'
            '      <DiscreteObjectKeyFrame KeyTime="0">\n'
            '        <DiscreteObjectKeyFrame.Value><x:Boolean>True</x:Boolean></DiscreteObjectKeyFrame.Value>\n'
            '        <DiscreteObjectKeyFrame.Value><x:Boolean>False</x:Boolean></DiscreteObjectKeyFrame.Value>\n'
            '      </DiscreteObjectKeyFrame>\n'
            '    </ObjectAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_dkf, path_dkf = self._run_fixture(xaml_dup_kf)
        self.assertNotEqual(res_dkf.returncode, 0)
        self.assertIn(f"{path_dkf}:6:", res_dkf.stderr)

        # Invalid owner/member pairing, vendor-prefixed TargetProperty, and unknown first child before valid ControlTemplate must fail on exact lines.
        xaml_bad_pair = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ColorAnimationUsingKeyFrames Storyboard.TargetName="R"\n'
            '      Storyboard.TargetProperty="(SolidColorBrush.Padding).(SolidColorBrush.Color)" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_bp, path_bp = self._run_fixture(xaml_bad_pair)
        self.assertNotEqual(res_bp.returncode, 0)
        self.assertIn(f"{path_bp}:4:", res_bp.stderr)

        xaml_vtp = (
            '<ControlTemplate xmlns:vendor="urn:vendor" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ColorAnimationUsingKeyFrames Storyboard.TargetName="R"\n'
            '      Storyboard.TargetProperty="(vendor:Border.Background).(SolidColorBrush.Color)" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_vtp, path_vtp = self._run_fixture(xaml_vtp)
        self.assertNotEqual(res_vtp.returncode, 0)
        self.assertIn(f"{path_vtp}:4:", res_vtp.stderr)

        xaml_first_bad = (
            '<Style TargetType="Button">\n'
            '  <Setter Property="Template">\n'
            '    <Setter.Value>\n'
            '      <Unknown />\n'
            '      <ControlTemplate TargetType="Button"><Grid /></ControlTemplate>\n'
            '    </Setter.Value>\n'
            '  </Setter>\n'
            '</Style>\n'
        )
        res_fb, path_fb = self._run_fixture(xaml_first_bad)
        self.assertNotEqual(res_fb.returncode, 0)
        self.assertIn(f"{path_fb}:4:", res_fb.stderr)

        # Vendor-prefixed VisualState Setter Target must be rejected on its exact line, while valid (Border.Background) is accepted.
        xaml_vs_pfx = (
            '<ControlTemplate xmlns:vendor="urn:vendor" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter\n'
            '      Target="R.vendor:Opacity" Value="0" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_vsp, path_vsp = self._run_fixture(xaml_vs_pfx)
        self.assertNotEqual(res_vsp.returncode, 0)
        self.assertIn(f"{path_vsp}:4:", res_vsp.stderr)

        xaml_border_bg = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Border Name="B"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ObjectAnimationUsingKeyFrames Storyboard.TargetName="B" Storyboard.TargetProperty="(Border.Background)">\n'
            '      <DiscreteObjectKeyFrame KeyTime="0" Value="{x:Null}" />\n'
            '    </ObjectAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Border>\n'
            '</ControlTemplate>\n'
        )
        res_bbg, _ = self._run_fixture(xaml_border_bg)
        self.assertEqual(res_bbg.returncode, 0, res_bbg.stderr)

        # Keyframe-only property (KeyTime) must be rejected on VisualState Setter Target, while attached (Grid.Row) is accepted.
        xaml_kt = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter\n'
            '      Target="R.KeyTime" Value="0" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_kt, path_kt = self._run_fixture(xaml_kt)
        self.assertNotEqual(res_kt.returncode, 0)
        self.assertIn(f"{path_kt}:4:", res_kt.stderr)

        xaml_grid_row = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(Grid.Row)" Value="1" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_gr, _ = self._run_fixture(xaml_grid_row)
        self.assertEqual(res_gr.returncode, 0, res_gr.stderr)

        # Element-incompatible properties (R.Text, R.Duration), XAML directives (R.Load), missing target parts (Missing.Text), and brush paths (R.(Fill.Color)) must fail on line 4.
        for bad_target in ("R.Text", "R.Duration", "R.Load", "R.DeferLoadStrategy", "Missing.Text", "R.(Fill.Color)"):
            xaml_incompat = (
                '<ControlTemplate TargetType="Button">\n'
                '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
                '    <Setter\n'
                f'      Target="{bad_target}" Value="0" />\n'
                '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
                '</ControlTemplate>\n'
            )
            res_inc, path_inc = self._run_fixture(xaml_incompat)
            self.assertNotEqual(res_inc.returncode, 0, f"{bad_target} should be rejected on Grid")
            self.assertIn(f"{path_inc}:4:", res_inc.stderr)

        # Inherited Control/ContentControl/Panel properties on Thumb, ScrollViewer, ItemsStackPanel, and Button must be accepted.
        res_thumb, _ = self._run_fixture('<ControlTemplate TargetType="Button"><Thumb Padding="0" /></ControlTemplate>\n')
        self.assertEqual(res_thumb.returncode, 0, res_thumb.stderr)
        res_sv_c, _ = self._run_fixture('<ControlTemplate TargetType="Button"><ScrollViewer Content="hello" /></ControlTemplate>\n')
        self.assertEqual(res_sv_c.returncode, 0, res_sv_c.stderr)
        res_isp, _ = self._run_fixture('<ControlTemplate TargetType="Button"><ItemsStackPanel Background="Red" /></ControlTemplate>\n')
        self.assertEqual(res_isp.returncode, 0, res_isp.stderr)
        res_btn_ts, _ = self._run_fixture('<ControlTemplate TargetType="Button"><Button IsTextScaleFactorEnabled="True" /></ControlTemplate>\n')
        self.assertEqual(res_btn_ts.returncode, 0, res_btn_ts.stderr)

        # Property-only VisualState.Setters must reject directives, Duration, and owner-incompatible properties; unqualified Load/Key must be rejected.
        for bad_prop in ("Load", "Duration", "RadiusX", "Glyph", "Rect"):
            xaml_vp = (
                '<ControlTemplate TargetType="Button">\n'
                '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
                '    <Setter\n'
                f'      Property="{bad_prop}" Value="1" />\n'
                '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
                '</ControlTemplate>\n'
            )
            res_vp, path_vp = self._run_fixture(xaml_vp)
            self.assertNotEqual(res_vp.returncode, 0, f"Property={bad_prop} should be rejected")
            self.assertIn(f"{path_vp}:4:", res_vp.stderr)

        xaml_unq_load = '<ControlTemplate TargetType="Button">\n  <Grid\n    Load="True" />\n</ControlTemplate>\n'
        res_ul, path_ul = self._run_fixture(xaml_unq_load)
        self.assertNotEqual(res_ul.returncode, 0)
        self.assertIn(f"{path_ul}:3:", res_ul.stderr)

        xaml_unq_key = (
            '<Style\n'
            '  Key="Bogus" TargetType="Button">\n'
            '  <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Grid /></ControlTemplate></Setter.Value></Setter>\n'
            '</Style>\n'
        )
        res_uk, path_uk = self._run_fixture(xaml_unq_key)
        self.assertNotEqual(res_uk.returncode, 0)
        self.assertIn(f"{path_uk}:2:", res_uk.stderr)

        # Unqualified Name on Style and storyboard animating RadiusX on a Grid must be rejected; ListViewItem.Content and TextBox.Text setters must be accepted.
        xaml_unq_sname = (
            '<Style\n'
            '  Name="Bogus" TargetType="Button">\n'
            '  <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Grid /></ControlTemplate></Setter.Value></Setter>\n'
            '</Style>\n'
        )
        res_usn, path_usn = self._run_fixture(xaml_unq_sname)
        self.assertNotEqual(res_usn.returncode, 0)
        self.assertIn(f"{path_usn}:2:", res_usn.stderr)

        xaml_anim_owner = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation Storyboard.TargetName="R"\n'
            '      Storyboard.TargetProperty="RadiusX" To="4" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_ao, path_ao = self._run_fixture(xaml_anim_owner)
        self.assertNotEqual(res_ao.returncode, 0)
        self.assertIn(f"{path_ao}:4:", res_ao.stderr)

        for target_type, prop_name in (
            ("ListViewItem", "Content"),
            ("TextBox", "Text"),
            ("TextBox", "MaxHeight"),
            ("TextBox", "PlaceholderForeground"),
            ("TextBox", "DataContext"),
        ):
            xaml_ok_setter = (
                f'<ControlTemplate TargetType="{target_type}">\n'
                '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
                f'    <Setter Property="{prop_name}" Value="hi" />\n'
                '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
                '</ControlTemplate>\n'
            )
            res_ok, _ = self._run_fixture(xaml_ok_setter)
            self.assertEqual(res_ok.returncode, 0, res_ok.stderr)

        # Unqualified Name on Storyboard, stale Storyboard.TargetName, and wrong-owner compound TargetProperty must be rejected.
        xaml_unq_sb_name = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S">\n'
            '    <Storyboard Name="Test" />\n'
            '  </VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_sbn, path_sbn = self._run_fixture(xaml_unq_sb_name)
        self.assertNotEqual(res_sbn.returncode, 0)
        self.assertIn(f"{path_sbn}:3:", res_sbn.stderr)

        xaml_stale_target = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation Storyboard.TargetName="Missing"\n'
            '      Storyboard.TargetProperty="Opacity" To="0" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_st, path_st = self._run_fixture(xaml_stale_target)
        self.assertNotEqual(res_st.returncode, 0)
        self.assertIn(f"{path_st}:3:", res_st.stderr)

        xaml_wrong_compound = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation Storyboard.TargetName="R"\n'
            '      Storyboard.TargetProperty="(TranslateTransform.X)" To="4" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_wc, path_wc = self._run_fixture(xaml_wrong_compound)
        self.assertNotEqual(res_wc.returncode, 0)
        self.assertIn(f"{path_wc}:4:", res_wc.stderr)

        # Wrong animation attribute by kind, unqualified Ignorable, cross-control Style setter, and dotted brush TargetProperty.
        xaml_bad_anim_attr = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation Storyboard.TargetName="R" Storyboard.TargetProperty="Opacity"\n'
            '      Direction="Left" To="0" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_baa, path_baa = self._run_fixture(xaml_bad_anim_attr)
        self.assertNotEqual(res_baa.returncode, 0)
        self.assertIn(f"{path_baa}:4:", res_baa.stderr)

        xaml_unq_ignorable = (
            '<ResourceDictionary Ignorable="d">\n'
            '  <Style TargetType="Button">\n'
            '    <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Grid /></ControlTemplate></Setter.Value></Setter>\n'
            '  </Style>\n'
            '</ResourceDictionary>\n'
        )
        res_ui, path_ui = self._run_fixture(xaml_unq_ignorable)
        self.assertNotEqual(res_ui.returncode, 0)
        self.assertIn(f"{path_ui}:1:", res_ui.stderr)

        xaml_style_cross = (
            '<Style TargetType="Button">\n'
            '  <Setter Property="MaxLength" Value="1" />\n'
            '  <Setter Property="Template"><Setter.Value><ControlTemplate TargetType="Button"><Grid /></ControlTemplate></Setter.Value></Setter>\n'
            '</Style>\n'
        )
        res_sc, path_sc = self._run_fixture(xaml_style_cross)
        self.assertNotEqual(res_sc.returncode, 0)
        self.assertIn(f"{path_sc}:2:", res_sc.stderr)

        xaml_dotted_brush_ok = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Border Name="B"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <ColorAnimationUsingKeyFrames Storyboard.TargetName="B" Storyboard.TargetProperty="Background.Color" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Border>\n'
            '</ControlTemplate>\n'
        )
        res_dbo, _ = self._run_fixture(xaml_dotted_brush_ok)
        self.assertEqual(res_dbo.returncode, 0, res_dbo.stderr)

        # KeySpline on LinearDoubleKeyFrame must be rejected; Grid.ChildrenTransitions and RepositionThemeAnimation.FromVerticalOffset must be accepted.
        xaml_bad_kf_spline = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimationUsingKeyFrames Storyboard.TargetName="R" Storyboard.TargetProperty="Opacity">\n'
            '      <LinearDoubleKeyFrame KeyTime="0" Value="1" KeySpline="0,0 1,1" />\n'
            '    </DoubleAnimationUsingKeyFrames>\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_bks, path_bks = self._run_fixture(xaml_bad_kf_spline)
        self.assertNotEqual(res_bks.returncode, 0)
        self.assertIn(f"{path_bks}:4:", res_bks.stderr)

        xaml_grid_ct = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.ChildrenTransitions" Value="{x:Null}" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_gct, _ = self._run_fixture(xaml_grid_ct)
        self.assertEqual(res_gct.returncode, 0, res_gct.stderr)

        xaml_reposition_v = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <RepositionThemeAnimation TargetName="R" FromVerticalOffset="4" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_rv, _ = self._run_fixture(xaml_reposition_v)
        self.assertEqual(res_rv.returncode, 0, res_rv.stderr)

        # ListViewItemPresenter derives from ContentPresenter (not Panel): reject ChildrenTransitions, accept CharacterSpacing.
        xaml_lvip_bad = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="ListViewItem">\n'
            '  <ListViewItemPresenter Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.ChildrenTransitions" Value="{x:Null}" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></ListViewItemPresenter>\n'
            '</ControlTemplate>\n'
        )
        res_lvb, path_lvb = self._run_fixture(xaml_lvip_bad)
        self.assertNotEqual(res_lvb.returncode, 0)
        self.assertIn(f"{path_lvb}:3:", res_lvb.stderr)

        xaml_lvip_ok = (
            '<ControlTemplate TargetType="ListViewItem">\n'
            '  <ListViewItemPresenter Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.CharacterSpacing" Value="10" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></ListViewItemPresenter>\n'
            '</ControlTemplate>\n'
        )
        res_lvo, _ = self._run_fixture(xaml_lvip_ok)
        self.assertEqual(res_lvo.returncode, 0, res_lvo.stderr)

        # RepositionThemeAnimation must preserve horizontal and vertical axes in emitted C for both literal and bound offsets, including mixed pairs.
        sys.path.insert(0, str(ROOT / "tools" / "winui"))
        import templates as tpl_mod

        def compile_reposition(attrs):
            c = tpl_mod.Compiler()
            c.compile_control_template(
                tpl_mod.parse_xaml(
                    '<ControlTemplate TargetType="Button"><Grid Name="R"><VisualStateManager.VisualStateGroups>'
                    '<VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>'
                    f'<RepositionThemeAnimation TargetName="R" {attrs} />'
                    '</Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid></ControlTemplate>',
                    "rep.xaml",
                ),
                "rep",
                "Button",
                (0, 0),
                None,
            )
            return c

        c_h = compile_reposition('FromHorizontalOffset="4"')
        c_v = compile_reposition('FromVerticalOffset="4"')
        emitted_h = tpl_mod.emit(c_h)["template_data.c"]
        emitted_v = tpl_mod.emit(c_v)["template_data.c"]
        self.assertIn(
            'true, false, false, {IM_TVAL_FIXED, 262144, 0, 0, 0, NULL, NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}',
            emitted_h,
        )
        self.assertIn(
            'false, true, false, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_FIXED, 262144, 0, 0, 0, NULL, NULL}',
            emitted_v,
        )

        c_bh = compile_reposition('FromHorizontalOffset="{Binding Offset}"')
        c_bv = compile_reposition('FromVerticalOffset="{Binding Offset}"')
        emitted_bh = tpl_mod.emit(c_bh)["template_data.c"]
        emitted_bv = tpl_mod.emit(c_bv)["template_data.c"]
        self.assertIn(
            'true, false, false, {IM_TVAL_BINDING, IM_BIND_DATA, IM_BIND_ONE_WAY, 0, 0, "Offset", NULL}, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}',
            emitted_bh,
        )
        self.assertIn(
            'false, true, false, {IM_TVAL_NONE, 0, 0, 0, 0, NULL, NULL}, {IM_TVAL_BINDING, IM_BIND_DATA, IM_BIND_ONE_WAY, 0, 0, "Offset", NULL}',
            emitted_bv,
        )

        c_mix = compile_reposition('FromHorizontalOffset="{Binding Offset}" FromVerticalOffset="4"')
        emitted_mix = tpl_mod.emit(c_mix)["template_data.c"]
        self.assertIn(
            'true, true, false, {IM_TVAL_BINDING, IM_BIND_DATA, IM_BIND_ONE_WAY, 0, 0, "Offset", NULL}, {IM_TVAL_FIXED, 262144, 0, 0, 0, NULL, NULL}',
            emitted_mix,
        )

        # Read-only TextBox.ProofingMenuFlyout must be rejected; inherited (FrameworkElement.Width) animation on Grid must be accepted.
        xaml_ro_prop = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="TextBox">\n'
            '  <Grid><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Property="ProofingMenuFlyout" Value="{x:Null}" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_ro, path_ro = self._run_fixture(xaml_ro_prop)
        self.assertNotEqual(res_ro.returncode, 0)
        self.assertIn(f"{path_ro}:3:", res_ro.stderr)

        xaml_fe_width = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation Storyboard.TargetName="R" Storyboard.TargetProperty="(FrameworkElement.Width)" EnableDependentAnimation="True" To="40" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_fw, _ = self._run_fixture(xaml_fe_width)
        self.assertEqual(res_fw.returncode, 0, res_fw.stderr)

        # (UIElement.Width) must be rejected (Width is declared on FrameworkElement, not UIElement) in both animations and setters, while R.(FrameworkElement.Width) setter must be accepted.
        xaml_uie_width = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation Storyboard.TargetName="R" Storyboard.TargetProperty="(UIElement.Width)" EnableDependentAnimation="True" To="40" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_uw, path_uw = self._run_fixture(xaml_uie_width)
        self.assertNotEqual(res_uw.returncode, 0)
        self.assertIn(f"{path_uw}:3:", res_uw.stderr)

        xaml_uie_width_setter = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(UIElement.Width)" Value="40" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_uws, path_uws = self._run_fixture(xaml_uie_width_setter)
        self.assertNotEqual(res_uws.returncode, 0)
        self.assertIn(f"{path_uws}:3:", res_uws.stderr)

        xaml_fe_width_setter = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(FrameworkElement.Width)" Value="40" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_fws, _ = self._run_fixture(xaml_fe_width_setter)
        self.assertEqual(res_fws.returncode, 0, res_fws.stderr)

        # Panel does not declare Grid-only Padding/BorderBrush/CornerRadius, while Grid does; Geometry declares Transform inherited by RectangleGeometry.
        xaml_panel_pad = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(Panel.Padding)" Value="4" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_pp, path_pp = self._run_fixture(xaml_panel_pad)
        self.assertNotEqual(res_pp.returncode, 0)
        self.assertIn(f"{path_pp}:3:", res_pp.stderr)

        xaml_grid_pad = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(Grid.Padding)" Value="4" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_gp, _ = self._run_fixture(xaml_grid_pad)
        self.assertEqual(res_gp.returncode, 0, res_gp.stderr)

        xaml_geom_tf = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">\n'
            '  <Border><Border.Clip><RectangleGeometry x:Name="ClipGeometry" Rect="0,0,10,10" /></Border.Clip><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="ClipGeometry.(Geometry.Transform)" Value="{x:Null}" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Border>\n'
            '</ControlTemplate>\n'
        )
        res_gt, _ = self._run_fixture(xaml_geom_tf)
        self.assertEqual(res_gt.returncode, 0, res_gt.stderr)

        # Class-level schema invariant: base types and concrete types must exclude non-XAML/descendant-only members.
        self.assertFalse({"Padding", "BorderBrush", "CornerRadius"} & tpl_mod.TYPE_PROPS["Panel"])
        self.assertFalse({"Width", "Height", "Margin"} & tpl_mod.TYPE_PROPS["UIElement"])
        self.assertIn("Transform", tpl_mod.TYPE_PROPS["Geometry"])
        self.assertFalse({"RadiusX", "RadiusY"} & tpl_mod.TYPE_PROPS["RectangleGeometry"])

        # CoreOnly RectangleGeometry.RadiusX must be rejected; namespace-qualified R.(c:Panel.Background) must be accepted for valid WinUI ns and rejected for foreign ns.
        xaml_rg_rx = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="Button">\n'
            '  <Border><Border.Clip><RectangleGeometry x:Name="R" Rect="0,0,10,10" /></Border.Clip><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(RectangleGeometry.RadiusX)" Value="4" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Border>\n'
            '</ControlTemplate>\n'
        )
        res_rgrx, path_rgrx = self._run_fixture(xaml_rg_rx)
        self.assertNotEqual(res_rgrx.returncode, 0)
        self.assertIn(f"{path_rgrx}:3:", res_rgrx.stderr)

        xaml_ns_panel_ok = (
            '<ControlTemplate xmlns:c="using:Microsoft.UI.Xaml.Controls" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(c:Panel.Background)" Value="Red" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_npo, _ = self._run_fixture(xaml_ns_panel_ok)
        self.assertEqual(res_npo.returncode, 0, res_npo.stderr)

        xaml_ns_panel_bad = (
            '<ControlTemplate xmlns:v="urn:vendor" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(v:Panel.Background)" Value="Red" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_npb, path_npb = self._run_fixture(xaml_ns_panel_bad)
        self.assertNotEqual(res_npb.returncode, 0)
        self.assertIn(f"{path_npb}:3:", res_npb.stderr)

        # Full OWNER_NAMESPACE table invariant: changing any owner's WinUI namespace must fail this test.
        expected_owner_ns = {
            "UIElement": "using:Microsoft.UI.Xaml",
            "FrameworkElement": "using:Microsoft.UI.Xaml",
            "BrushTransition": "using:Microsoft.UI.Xaml",
            "Panel": "using:Microsoft.UI.Xaml.Controls",
            "Grid": "using:Microsoft.UI.Xaml.Controls",
            "Canvas": "using:Microsoft.UI.Xaml.Controls",
            "Viewbox": "using:Microsoft.UI.Xaml.Controls",
            "UniformGrid": "using:Microsoft.UI.Xaml.Controls",
            "WrapPanel": "using:Microsoft.UI.Xaml.Controls",
            "RelativePanel": "using:Microsoft.UI.Xaml.Controls",
            "VariableSizedWrapGrid": "using:Microsoft.UI.Xaml.Controls",
            "ItemsWrapGrid": "using:Microsoft.UI.Xaml.Controls",
            "Border": "using:Microsoft.UI.Xaml.Controls",
            "ContentPresenter": "using:Microsoft.UI.Xaml.Controls",
            "TextBlock": "using:Microsoft.UI.Xaml.Controls",
            "Button": "using:Microsoft.UI.Xaml.Controls",
            "ScrollViewer": "using:Microsoft.UI.Xaml.Controls",
            "ToggleSwitch": "using:Microsoft.UI.Xaml.Controls",
            "TextBox": "using:Microsoft.UI.Xaml.Controls",
            "ListView": "using:Microsoft.UI.Xaml.Controls",
            "ListViewItem": "using:Microsoft.UI.Xaml.Controls",
            "FontIcon": "using:Microsoft.UI.Xaml.Controls",
            "IconElement": "using:Microsoft.UI.Xaml.Controls",
            "ItemsStackPanel": "using:Microsoft.UI.Xaml.Controls",
            "ItemsPresenter": "using:Microsoft.UI.Xaml.Controls",
            "RowDefinition": "using:Microsoft.UI.Xaml.Controls",
            "ColumnDefinition": "using:Microsoft.UI.Xaml.Controls",
            "Control": "using:Microsoft.UI.Xaml.Controls",
            "ContentControl": "using:Microsoft.UI.Xaml.Controls",
            "ItemsControl": "using:Microsoft.UI.Xaml.Controls",
            "ListViewBase": "using:Microsoft.UI.Xaml.Controls",
            "Canvas": "using:Microsoft.UI.Xaml.Controls",
            "AnimatedIcon": "using:Microsoft.UI.Xaml.Controls",
            "ButtonBase": "using:Microsoft.UI.Xaml.Controls.Primitives",
            "Selector": "using:Microsoft.UI.Xaml.Controls.Primitives",
            "SelectorItem": "using:Microsoft.UI.Xaml.Controls.Primitives",
            "Thumb": "using:Microsoft.UI.Xaml.Controls.Primitives",
            "ListViewItemPresenter": "using:Microsoft.UI.Xaml.Controls.Primitives",
            "Shape": "using:Microsoft.UI.Xaml.Shapes",
            "Rectangle": "using:Microsoft.UI.Xaml.Shapes",
            "Path": "using:Microsoft.UI.Xaml.Shapes",
            "Geometry": "using:Microsoft.UI.Xaml.Media",
            "RectangleGeometry": "using:Microsoft.UI.Xaml.Media",
            "Transform": "using:Microsoft.UI.Xaml.Media",
            "TranslateTransform": "using:Microsoft.UI.Xaml.Media",
            "ScaleTransform": "using:Microsoft.UI.Xaml.Media",
            "CompositeTransform": "using:Microsoft.UI.Xaml.Media",
            "SolidColorBrush": "using:Microsoft.UI.Xaml.Media",
            "Transition": "using:Microsoft.UI.Xaml.Media.Animation",
            "AddDeleteThemeTransition": "using:Microsoft.UI.Xaml.Media.Animation",
            "ContentThemeTransition": "using:Microsoft.UI.Xaml.Media.Animation",
            "ReorderThemeTransition": "using:Microsoft.UI.Xaml.Media.Animation",
            "EntranceThemeTransition": "using:Microsoft.UI.Xaml.Media.Animation",
            "PopupThemeTransition": "using:Microsoft.UI.Xaml.Media.Animation",
            "RepositionThemeTransition": "using:Microsoft.UI.Xaml.Media.Animation",
            "NavigationThemeTransition": "using:Microsoft.UI.Xaml.Media.Animation",
            "NavigationTransitionInfo": "using:Microsoft.UI.Xaml.Media.Animation",
            "DrillInNavigationTransitionInfo": "using:Microsoft.UI.Xaml.Media.Animation",
            "SlideNavigationTransitionInfo": "using:Microsoft.UI.Xaml.Media.Animation",
            "SuppressNavigationTransitionInfo": "using:Microsoft.UI.Xaml.Media.Animation",
            "AutomationProperties": "using:Microsoft.UI.Xaml.Automation",
        }
        self.assertEqual(tpl_mod.OWNER_NAMESPACE, expected_owner_ns)
        self.assertTrue(tpl_mod.ALL_OWNER_TYPES <= set(tpl_mod.OWNER_NAMESPACE))

        # Double-dotted R.(c:Grid.Grid.Row) must be rejected; R.(c:TextBlock.TextTrimming) must be accepted.
        xaml_double_dot = (
            '<ControlTemplate xmlns:c="using:Microsoft.UI.Xaml.Controls" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(c:Grid.Grid.Row)" Value="1" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_dd, path_dd = self._run_fixture(xaml_double_dot)
        self.assertNotEqual(res_dd.returncode, 0)
        self.assertIn(f"{path_dd}:3:", res_dd.stderr)

        xaml_tb_trim = (
            '<ControlTemplate xmlns:c="using:Microsoft.UI.Xaml.Controls" TargetType="Button">\n'
            '  <TextBlock Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(c:TextBlock.TextTrimming)" Value="CharacterEllipsis" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></TextBlock>\n'
            '</ControlTemplate>\n'
        )
        res_tbt, _ = self._run_fixture(xaml_tb_trim)
        self.assertEqual(res_tbt.returncode, 0, res_tbt.stderr)

        xaml_sv_attached = (
            '<ControlTemplate xmlns:c="using:Microsoft.UI.Xaml.Controls" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(c:ScrollViewer.IsZoomChainingEnabled)" Value="False" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_sva, _ = self._run_fixture(xaml_sv_attached)
        self.assertEqual(res_sva.returncode, 0, res_sva.stderr)

        xaml_ap_id = (
            '<ControlTemplate xmlns:c="using:Microsoft.UI.Xaml.Automation" TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(c:AutomationProperties.AutomationId)" Value="SubmitButton" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_api, _ = self._run_fixture(xaml_ap_id)
        self.assertEqual(res_api.returncode, 0, res_api.stderr)
        xaml_bt_ok = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" xmlns:c="using:Microsoft.UI.Xaml" TargetType="Button">\n'
            '  <ContentPresenter><ContentPresenter.BackgroundTransition><BrushTransition x:Name="R" /></ContentPresenter.BackgroundTransition><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(c:BrushTransition.Duration)" Value="0:0:0.1" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></ContentPresenter>\n'
            '</ControlTemplate>\n'
        )
        res_bto, _ = self._run_fixture(xaml_bt_ok)
        self.assertEqual(res_bto.returncode, 0, res_bto.stderr)

        xaml_bt_bad = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" xmlns:c="using:Microsoft.UI.Xaml.Media" TargetType="Button">\n'
            '  <ContentPresenter><ContentPresenter.BackgroundTransition><BrushTransition x:Name="R" /></ContentPresenter.BackgroundTransition><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.(c:BrushTransition.Duration)" Value="0:0:0.1" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></ContentPresenter>\n'
            '</ControlTemplate>\n'
        )
        res_btb, path_btb = self._run_fixture(xaml_bt_bad)
        self.assertNotEqual(res_btb.returncode, 0)
        self.assertIn(f"{path_btb}:3:", res_btb.stderr)

        # Unqualified TargetName on DoubleAnimation must be rejected; UIElement.AccessKeyScopeOwner on ListViewItemPresenter must be accepted.
        xaml_unq_tn = (
            '<ControlTemplate TargetType="Button">\n'
            '  <Grid Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><Storyboard>\n'
            '    <DoubleAnimation TargetName="R" Storyboard.TargetProperty="Opacity" To="0" />\n'
            '  </Storyboard></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></Grid>\n'
            '</ControlTemplate>\n'
        )
        res_utn, path_utn = self._run_fixture(xaml_unq_tn)
        self.assertNotEqual(res_utn.returncode, 0)
        self.assertIn(f"{path_utn}:3:", res_utn.stderr)

        xaml_akso = (
            '<ControlTemplate xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" TargetType="ListViewItem">\n'
            '  <ListViewItemPresenter Name="R"><VisualStateManager.VisualStateGroups><VisualStateGroup Name="G"><VisualState Name="S"><VisualState.Setters>\n'
            '    <Setter Target="R.AccessKeyScopeOwner" Value="{x:Null}" />\n'
            '  </VisualState.Setters></VisualState></VisualStateGroup></VisualStateManager.VisualStateGroups></ListViewItemPresenter>\n'
            '</ControlTemplate>\n'
        )
        res_akso, _ = self._run_fixture(xaml_akso)
        self.assertEqual(res_akso.returncode, 0, res_akso.stderr)


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


LEGACY_CASE_IDS = [
    *[
        f"{control}-{state}-{theme}"
        for theme in ("Dark", "Light")
        for control in ("button", "accent")
        for state in ("Normal", "PointerOver", "Pressed", "Disabled")
    ],
    "probe-r4-s1", "probe-r4-s15", "probe-r4-s2",
    "probe-r8-s1", "probe-r8-s15", "probe-r8-s2",
    "probe-r16-s1", "probe-r16-s15", "probe-r16-s2",
    "probe-asym-s1",
]


def select_legacy_cases(manifest):
    """Select the 26 legacy button + probe cases in control-shots.exe emission order by stable ID."""
    by_id = {}
    for c in manifest.get("cases", []):
        cid = c.get("id") or (c.get("name") if c.get("control") == "probe" else f"{c.get('control')}-{c.get('state')}-{c.get('theme')}")
        if cid in by_id:
            raise ValueError(f"duplicate case id in manifest: {cid!r}")
        by_id[cid] = c
    missing = [cid for cid in LEGACY_CASE_IDS if cid not in by_id]
    if missing:
        raise ValueError(f"manifest missing required legacy cases: {missing}")
    return [by_id[cid] for cid in LEGACY_CASE_IDS]


def validate_manifest(manifest):
    """Verify the reference manifest independently of its own count and ordering."""
    accents = manifest.get("accents", {})
    accent_args = []
    for key in ACCENT_KEYS:
        val = accents.get(key)
        if not isinstance(val, str) or len(val) != 8 or any(ch not in "0123456789abcdefABCDEF" for ch in val):
            raise ValueError(f"missing or malformed accent shade {key}: {val!r}")
        accent_args.append(val)
    cases = manifest.get("cases", [])
    files = []
    ids = []
    for case in cases:
        if case.get("basis") == "compiled-template":
            cid = case.get("id")
            if not cid or not case.get("template") or "verified_elements" not in case or "verified_states" not in case:
                raise ValueError(f"compiled-template case missing required fields: {case}")
            ids.append(cid)
            continue
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
        cid = case.get("id") or (case.get("name") if case.get("control") == "probe" else f"{case.get('control')}-{case.get('state')}-{case.get('theme')}")
        ids.append(cid)
        files.append(fn)
    if len(files) != len(set(files)):
        raise ValueError("duplicate reference filename in manifest")
    if len(ids) != len(set(ids)):
        raise ValueError("duplicate case identity in manifest")
    legacy = select_legacy_cases(manifest)
    legacy_idents = []
    for case in legacy:
        scale, lw, lh, fn = case["scale"], case["logical_width"], case["logical_height"], case["file"]
        if case.get("control") == "probe":
            legacy_idents.append(("probe", case.get("name"), None, None, float(scale), lw, lh, case.get("radii"), fn))
        else:
            legacy_idents.append(("button_case", case.get("control"), case.get("state"), case.get("theme"), float(scale), lw, lh, None, fn))
    if legacy_idents != EXPECTED_CASES:
        raise ValueError("legacy 26 cases differ from expected identities")
    return accent_args


def nearest_upscale(raw, sw, sh, dw, dh):
    """Nearest-neighbour upscale a BGRA buffer from sw x sh to dw x dh."""
    out = bytearray(dw * dh * 4)
    for dy in range(dh):
        sy = min(sh - 1, int(dy * sh / dh))
        for dx in range(dw):
            sx = min(sw - 1, int(dx * sw / dw))
            dst = (dy * dw + dx) * 4
            src = (sy * sw + sx) * 4
            out[dst:dst + 4] = raw[src:src + 4]
    return bytes(out)


def check_scaled_control_capture(case, raw):
    """Verify a scaled button capture was vector-rasterized rather than bilinearly or nearest-neighbour enlarged."""
    w, h = case["width"], case["height"]
    scale = float(case["scale"])
    if scale <= 1.0:
        return
    # 1. Straight vertical edge at x = round(20 * scale), y = h // 2 must jump cleanly from background
    #    (Dark: 32 -> >= 46; Light: 243 -> <= 235) with zero bilinear pre-edge blur at x - 1.
    y_mid = h // 2
    x_edge = round(20 * scale)
    bg_before = raw[(y_mid * w + (x_edge - 1)) * 4]
    edge_px = raw[(y_mid * w + x_edge) * 4]
    is_light = case.get("theme") == "Light"
    if is_light:
        if bg_before != 243 or edge_px > 235:
            raise ValueError(f"{case['id']}: blurry edge at ({x_edge},{y_mid}): before={bg_before}, edge={edge_px}")
    else:
        if bg_before != 32 or edge_px < 46:
            raise ValueError(f"{case['id']}: blurry edge at ({x_edge},{y_mid}): before={bg_before}, edge={edge_px}")

    # 2. Catch nearest-neighbour block enlargement:
    #    - At 1.5x, a 1px logical border has fractional 1.5px coverage at x_edge+1 (between 45 and 48, not solid 48).
    #    - At integer scales k >= 2, the top-left rounded corner (x in [x_edge, x_edge+4*k), y in [y_top, y_top+4*k))
    #      must NOT consist solely of uniform k x k duplicated blocks.
    if abs(scale - 1.5) < 1e-6:
        next_px = raw[(y_mid * w + (x_edge + 1)) * 4]
        if next_px == edge_px:
            raise ValueError(f"{case['id']}: 1.5x border shows nearest-neighbour duplication ({edge_px}, {next_px})")
    elif abs(scale - round(scale)) < 1e-6 and round(scale) >= 2:
        k = int(round(scale))
        y_top = round(16 * scale)
        non_block = 0
        for by in range(4):
            for bx in range(4):
                x0, y0 = x_edge + bx * k, y_top + by * k
                vals = {raw[((y0 + dy) * w + (x0 + dx)) * 4] for dy in range(k) for dx in range(k)}
                if len(vals) > 1:
                    non_block += 1
        if non_block == 0:
            raise ValueError(f"{case['id']}: {k}x corner consists purely of uniform {k}x{k} nearest-neighbour blocks")


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


class GuideDocs(unittest.TestCase):
    def test_guide_documents_verified_limits(self):
        guide = (ROOT / "docs/guide/button.md").read_text(encoding="utf-8")
        self.assertIn(f"{EDGE_LIMIT} of 255", guide)
        self.assertIn(f"{STANDARD_DARK_CORNER_LIMIT} of 255", guide)
        self.assertIn(f"{STANDARD_LIGHT_CORNER_LIMIT} of 255", guide)
        self.assertIn(f"{ACCENT_CORNER_LIMIT} of 255", guide)
        self.assertIn(f"{PROBE_LIMIT} of 255", guide)

        # Wang chord sentence must state exact N = 256 for signed 16.16 radii under 32,768 px and smoothing section must not carry 1024 or 1,024.
        smoothing = guide.split("## How are corners smoothed?", 1)[1].split("\n## ", 1)[0]
        sentences = [s.strip() for s in re.split(r"(?<=\.)\s+", smoothing) if s.strip()]
        wang_sentences = [s for s in sentences if "Wang's formula" in s]
        self.assertEqual(len(wang_sentences), 1)
        self.assertIsNotNone(re.search(r"\bN\s*=\s*256\b", wang_sentences[0]))
        self.assertIsNotNone(re.search(r"\bsigned 16\.16 radii under 32,768 px\b", wang_sentences[0]))
        self.assertIsNone(re.search(r"1,?024", smoothing))

        # Accent-button explanation sentence must preserve both the measured qualification and horizontal/vertical (not diagonal) symmetry basis.
        accent_sentences = [s for s in sentences if "accent button" in s.lower() and f"{ACCENT_CORNER_LIMIT} of 255" in s]
        self.assertEqual(len(accent_sentences), 1)
        self.assertIn(
            f"measured corner limit of {ACCENT_CORNER_LIMIT} of 255 stands on horizontal and vertical reflection symmetry (not diagonal symmetry)",
            accent_sentences[0],
        )

        # The §1 correction paragraph in TODO-01-first-controls.md must stay aligned with docs/guide/button.md.
        todo_rel = "todo/01-interface/TODO-01-first-controls.md"
        todo_text = (ROOT / todo_rel).read_text(encoding="utf-8")
        s1_body = todo_text.split("## 1. Exact corner smoothing", 1)[1].split("\n## 2. ", 1)[0]
        self.assertIsNotNone(re.search(r"\bN\s*=\s*256\b", s1_body), f"{todo_rel}: missing N=256 in §1")
        self.assertIsNotNone(re.search(r"\bsigned 16\.16 radii `< 32,768` px\b", s1_body), f"{todo_rel}: missing signed 16.16 radius range in §1")
        self.assertIsNone(re.search(r"1,?024", s1_body), f"{todo_rel}: stale 1024/1,024 cap in §1")
        self.assertIn(
            f"measured corner error under horizontal and vertical reflection symmetry is {ACCENT_CORNER_LIMIT} of 255",
            s1_body,
            f"{todo_rel}: missing measured 4-way reflection accent error in §1",
        )
        self.assertIsNone(re.search(r"6-unit|six-unit|<=\s*4 on accent", s1_body), f"{todo_rel}: superseded accent floor claim in §1")


class ReferenceHarness(unittest.TestCase):
    """Store, package, dump, masking, and script-log checks that run on any host without a display session."""

    def _ref_modules(self):
        sys.path.insert(0, str(ROOT / "reference"))
        sys.path.insert(0, str(ROOT / "tools/winui"))
        import run as ref_run
        import check_package
        return ref_run, check_package

    def test_store_integrity_and_negative_faults(self):
        ref_run, _ = self._ref_modules()
        specs = ref_run.load_case_specs(ROOT / "reference/cases")
        manifest = ref_run.validate_store_dir(ROOT / "tests/reference", specs)
        self.assertEqual(len(manifest["cases"]), len(specs))

        # Reordered index + extra 100% LTR button still selects the exact 26 legacy cases in emission order.
        reordered = {**manifest, "cases": list(reversed(manifest["cases"])) + [{
            "id": "button-Extra-Dark",
            "control": "button",
            "state": "Normal",
            "theme": "Dark",
            "scale": 1.0,
            "flow_direction": "LeftToRight",
            "logical_width": 160,
            "logical_height": 64,
            "file": "button-Extra-Dark.bgra",
            "width": 160,
            "height": 64,
        }]}
        validate_manifest(reordered)
        self.assertEqual(len(select_legacy_cases(reordered)), 26)

        with tempfile.TemporaryDirectory() as tmp:
            tstore = Path(tmp) / "ref"
            shutil.copytree(ROOT / "tests/reference", tstore)

            # 1. Missing compressed frame fails.
            dropped = tstore / "button-Normal-Dark.bgra.z"
            saved = dropped.read_bytes()
            dropped.unlink()
            with self.assertRaises(ValueError):
                ref_run.validate_store_dir(tstore, specs)
            dropped.write_bytes(saved)

            # 2. Extra unindexed file fails.
            extra = tstore / "stray.bgra.z"
            extra.write_bytes(b"stray")
            with self.assertRaises(ValueError):
                ref_run.validate_store_dir(tstore, specs)
            extra.unlink()

            # 3. Stale/corrupted frame fails digest check.
            dropped.write_bytes(zlib.compress(b"\x00" * (160 * 64 * 4), 9))
            with self.assertRaises(ValueError):
                ref_run.validate_store_dir(tstore, specs)
            dropped.write_bytes(saved)

            # 4. Case-only edit without refreshing the store fails case_sha256 check.
            mut_specs = {k: dict(v) for k, v in specs.items()}
            mut_specs["button-Normal-Dark"] = {**mut_specs["button-Normal-Dark"], "theme": "Light"}
            with self.assertRaises(ValueError):
                ref_run.validate_store_dir(tstore, mut_specs)

            # 5. Failed publish_store leaves previous store index and every frame intact.
            bad_fresh = Path(tmp) / "bad_fresh"
            bad_fresh.mkdir()
            (bad_fresh / "cases.json").write_text(json.dumps({"accents": manifest["accents"], "cases": [manifest["cases"][0]]}), encoding="utf-8")
            with self.assertRaises(Exception):
                ref_run.publish_store(bad_fresh, tstore, specs)
            ref_run.validate_store_dir(tstore, specs)

        # 6. --cases combined with --update-store is refused without touching the store.
        res = subprocess.run(
            [sys.executable, str(ROOT / "reference/run.py"), "--cases", str(ROOT / "reference/cases/button.json"), "--update-store"],
            capture_output=True, text=True, timeout=30,
        )
        self.assertEqual(res.returncode, 2)
        self.assertIn("refuses --cases", res.stderr)

        # 7. Prohibited Microsoft font, FontIcon, ListView items, reflected displayable props (OnContent string or numeric),
        #    or case-colliding keys fail; non-text numeric/layout properties (Opacity: 0.5, RowSpacing: 4, SelectedIndex: 1) pass.
        ref_run.check_no_prohibited_assets({"id": "num-prop", "control": "Button", "properties": {"Opacity": 0.5}})
        ref_run.check_no_prohibited_assets({"id": "grid-row-spacing", "control": "Grid", "properties": {"RowSpacing": 4}})
        ref_run.check_no_prohibited_assets({"id": "cap-prop", "control": "ComboBox", "Properties": {"SelectedIndex": 1}})
        with self.assertRaises(ValueError):
            ref_run.check_no_prohibited_assets({"id": "bad-text", "control": "Button", "properties": {"Content": "Hi"}})
        with self.assertRaises(ValueError):
            ref_run.check_no_prohibited_assets({"id": "bad-icon", "control": "FontIcon"})
        with self.assertRaises(ValueError):
            ref_run.check_no_prohibited_assets({"id": "bad-list", "control": "ListView", "items": ["Alpha"]})
        with self.assertRaises(ValueError):
            ref_run.check_no_prohibited_assets({"id": "bad-reflected", "control": "ToggleSwitch", "properties": {"OnContent": "Enabled"}})
        with self.assertRaises(ValueError):
            ref_run.check_no_prohibited_assets({"id": "bad-reflected-num", "control": "ToggleSwitch", "properties": {"IsOn": True, "OnContent": 123}})
        with self.assertRaises(ValueError):
            ref_run.check_no_prohibited_assets({"id": "label", "Control": "TextBlock", "Properties": {"Text": "Sample"}})
        with self.assertRaises(ValueError):
            ref_run.check_no_prohibited_assets({"id": "label", "Control": "Border", "control": "TextBlock"})

        # 7b. load_case_specs accepts unambiguous uppercase top-level/case keys ("Cases", "Id") and rejects identical/case-folded duplicates.
        with tempfile.TemporaryDirectory() as tmp:
            cap_file = Path(tmp) / "caps.json"
            cap_file.write_text(
                json.dumps({"Cases": [{"Id": "cap-case", "Control": "ComboBox", "Properties": {"SelectedIndex": 1}}]}),
                encoding="utf-8",
            )
            loaded = ref_run.load_case_specs(cap_file)
            self.assertIn("cap-case", loaded)
            self.assertEqual(loaded["cap-case"]["control"], "ComboBox")
            self.assertEqual(loaded["cap-case"]["properties"], {"SelectedIndex": 1})

            bad_top = Path(tmp) / "bad_top.json"
            bad_top.write_text(json.dumps({"Cases": [], "cases": []}), encoding="utf-8")
            with self.assertRaises(ValueError):
                ref_run.load_case_specs(bad_top)

            dup_exact = Path(tmp) / "dup_exact.json"
            dup_exact.write_text(
                '{"cases": [{"id": "first", "control": "Button"}], "cases": [{"id": "second", "control": "Button"}]}',
                encoding="utf-8",
            )
            with self.assertRaises(ValueError):
                ref_run.load_case_specs(dup_exact)

        # 8. --out refuses unrelated files, foreign capture-like dirs, unrelated .<name>.backup siblings, and handles empty --out cleanly.
        with tempfile.TemporaryDirectory() as tmp:
            out_dir = Path(tmp) / "out"
            out_dir.mkdir()
            sentinel = out_dir / "keep-me.txt"
            sentinel.write_text("important", encoding="utf-8")
            with self.assertRaises(ValueError):
                ref_run._ensure_safe_capture_dir(out_dir)
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "important")

            foreign = Path(tmp) / "foreign_out"
            foreign.mkdir()
            (foreign / "cases.json").write_text("{}", encoding="utf-8")
            with self.assertRaises(ValueError):
                ref_run._ensure_safe_capture_dir(foreign)

            sibling = Path(tmp) / ".fresh.backup"
            sibling.mkdir()
            sib_sentinel = sibling / "precious.txt"
            sib_sentinel.write_text("do not delete", encoding="utf-8")
            with self.assertRaises(ValueError):
                ref_run._ensure_safe_capture_dir(Path(tmp) / "fresh")
            self.assertEqual(sib_sentinel.read_text(encoding="utf-8"), "do not delete")

            # Empty --out publishes cleanly without creating an unmarked backup.
            empty_out = Path(tmp) / "empty_out"
            empty_out.mkdir()
            src_cap = Path(tmp) / "src_cap"
            src_cap.mkdir()
            (src_cap / "cases.json").write_text("{}", encoding="utf-8")
            ref_run._publish_capture_output(src_cap, empty_out)
            self.assertTrue((empty_out / ref_run.OUT_MARKER).is_file())
            self.assertFalse((Path(tmp) / ".empty_out.backup").exists())

            # Unrelated raw capture inside .reference.backup (without STORE_MARKER) must be refused, never deleted or restored.
            fake_raw_bak = Path(tmp) / ".mystore.backup"
            fake_raw_bak.mkdir()
            (fake_raw_bak / "cases.json").write_text(json.dumps({"accents": {}, "cases": []}), encoding="utf-8")
            (fake_raw_bak / "extra.txt").write_text("user notes", encoding="utf-8")
            with self.assertRaises(ValueError):
                ref_run.read_reference_snapshot(Path(tmp) / "mystore")
            self.assertEqual((fake_raw_bak / "extra.txt").read_text(encoding="utf-8"), "user notes")

            # Refreshing a store containing an unrelated file (diagnostic.txt or unindexed trial.bgra.z) must be refused without deleting it.
            mixed_store = Path(tmp) / "mixed_store"
            shutil.copytree(ROOT / "tests/reference", mixed_store)
            (mixed_store / "diagnostic.txt").write_text("keep me", encoding="utf-8")
            prev_cases = (mixed_store / "cases.json").read_text(encoding="utf-8")
            fake_fresh = Path(tmp) / "fake_fresh"
            fake_fresh.mkdir()
            bm, bf = ref_run.read_reference_snapshot(ROOT / "tests/reference")
            (fake_fresh / "cases.json").write_text(json.dumps(bm), encoding="utf-8")
            for fn, raw in bf.items():
                (fake_fresh / fn).write_bytes(raw)
            with self.assertRaises(ValueError):
                ref_run.publish_store(fake_fresh, mixed_store, specs)
            self.assertEqual((mixed_store / "diagnostic.txt").read_text(encoding="utf-8"), "keep me")
            self.assertEqual((mixed_store / "cases.json").read_text(encoding="utf-8"), prev_cases)
            (mixed_store / "diagnostic.txt").unlink()
            (mixed_store / "trial.bgra.z").write_bytes(b"unindexed-diagnostic-frame")
            with self.assertRaises(ValueError):
                ref_run.publish_store(fake_fresh, mixed_store, specs)
            self.assertEqual((mixed_store / "trial.bgra.z").read_bytes(), b"unindexed-diagnostic-frame")

            # Baseline 26-frame comparison passes on matching frames and names the exact frame when 1 pixel byte changes.
            self.assertEqual(ref_run.compare_baseline_frames(fake_fresh, ROOT / "tests/reference"), 26)
            tampered = bytearray((fake_fresh / "button-Normal-Dark.bgra").read_bytes())
            tampered[0] ^= 1
            (fake_fresh / "button-Normal-Dark.bgra").write_bytes(bytes(tampered))
            with self.assertRaises(ValueError) as cm_base:
                ref_run.compare_baseline_frames(fake_fresh, ROOT / "tests/reference")
            self.assertIn("button-Normal-Dark", str(cm_base.exception))

            # Package provenance binding: validate_store_dir accepts matching provenance and rejects drift.
            prov = ref_run.compute_package_fingerprint()
            ref_run.validate_store_dir(ROOT / "tests/reference", expected_specs=specs, expected_provenance=prov)
            drifted_prov = {**prov, "winui_component": "0.0.0.0"}
            with self.assertRaises(ValueError):
                ref_run.validate_store_dir(ROOT / "tests/reference", expected_specs=specs, expected_provenance=drifted_prov)

        # 9. Interrupted swap (store missing, owned .<name>.backup present) is recovered atomically across all
        #    dead lock formats: current tok-*, legacy tokenless owner.json, legacy tokenized owner.json,
        #    legacy two-file (owner.json + plain tok-*), legacy b78e066 hyphenated .lockv2, empty .lockv2,
        #    and .tok-*.tmp residue in .lockv2.
        for mode in (
            "current",
            "legacy-tokenless",
            "legacy-tokenized",
            "legacy-two-file",
            "legacy-v2",
            "legacy-v2-empty",
            "legacy-v2-tmp-residue",
        ):
            with tempfile.TemporaryDirectory() as tmp:
                tstore = Path(tmp) / "reference"
                tbackup = Path(tmp) / ".reference.backup"
                shutil.copytree(ROOT / "tests/reference", tbackup)
                key = hashlib.sha256(str(tstore.resolve()).encode("utf-8")).hexdigest()[:16]
                suffix = ".lockv2" if mode in ("legacy-v2", "legacy-v2-empty", "legacy-v2-tmp-residue") else ".lockdir"
                stale_lock = Path(tempfile.gettempdir()) / f"impossible-sdk-ref-{key}{suffix}"
                stale_lock.mkdir(exist_ok=True)
                host = "nt" if os.name == "nt" else "posix"
                distro = os.environ.get("WSL_DISTRO_NAME", "") if host == "posix" else ""
                if mode == "current":
                    dead_tok = f"{host}.99999999.{ref_run._encode_distro(distro)}.0"
                    (stale_lock / f"tok-{dead_tok}").write_text(json.dumps({
                        "pid": 99999999, "host": host, "distro": distro, "token": dead_tok, "created": 0,
                    }), encoding="utf-8")
                elif mode == "legacy-v2":
                    # Actual b78e066 hyphenated token format with matching JSON owner record.
                    dtag = (distro or "none").replace("-", "_") if host == "posix" else "win"
                    dead_tok = f"{host}-99999999-{dtag}-0"
                    (stale_lock / f"tok-{dead_tok}").write_text(json.dumps({
                        "pid": 99999999, "host": host, "distro": distro, "token": dead_tok, "created": 0,
                    }), encoding="utf-8")
                elif mode == "legacy-v2-tmp-residue":
                    (stale_lock / ".tok-posix-99999999-none-1.tmp").write_text("{", encoding="utf-8")
                elif mode == "legacy-tokenless":
                    (stale_lock / "owner.json").write_text(json.dumps({
                        "pid": 99999999, "host": host, "distro": distro, "created": 0,
                    }), encoding="utf-8")
                elif mode == "legacy-tokenized":
                    (stale_lock / "owner.json").write_text(json.dumps({
                        "pid": 99999999, "host": host, "distro": distro, "token": "legacy-tok-999", "created": 0,
                    }), encoding="utf-8")
                elif mode == "legacy-two-file":
                    (stale_lock / "owner.json").write_text(json.dumps({
                        "pid": 99999999, "host": host, "distro": distro, "token": "99999999-0", "created": 0,
                    }), encoding="utf-8")
                    (stale_lock / "tok-99999999-0").write_text("99999999-0", encoding="utf-8")
                self.assertFalse(tstore.exists())
                recovered, rframes = ref_run.read_reference_snapshot(tstore, filenames=["button-Normal-Dark.bgra"])
                self.assertTrue(tstore.is_dir())
                self.assertFalse(stale_lock.exists())
                self.assertEqual(len(recovered["cases"]), len(specs))
                self.assertEqual(len(rframes["button-Normal-Dark.bgra"]), 160 * 64 * 4)

        # 9b. Fault-injection tests: empty legacy .lockdir blocks entry (never overwritten by rename), ambiguous
        #     mid-takeover hyphenated token is conservatively preserved, claim rollback + release-error preservation work,
        #     and live .lockv2 blocks entry.
        from unittest import mock
        with tempfile.TemporaryDirectory() as tmp:
            tstore = Path(tmp) / "lock_fault_store"
            tstore.mkdir()
            key = hashlib.sha256(str(tstore.resolve()).encode("utf-8")).hexdigest()[:16]
            host = "nt" if os.name == "nt" else "posix"
            distro = os.environ.get("WSL_DISTRO_NAME", "") if host == "posix" else ""
            dead_dir = Path(tempfile.gettempdir()) / f"impossible-sdk-ref-{key}.lockdir"
            # Empty .lockdir (held by older mkdir-only publisher for the entire critical section) must always
            # block acquisition regardless of age and never be overwritten or removed.
            dead_dir.mkdir(exist_ok=True)
            try:
                old_ts = ref_run.time.time() - 3600.0
                os.utime(dead_dir, (old_ts, old_ts))
                self.assertFalse(ref_run._try_clear_dead_lock(dead_dir, "claim-aged-empty"))
                with self.assertRaises(TimeoutError):
                    with ref_run._store_lock(tstore, timeout=0.15):
                        pass
                self.assertTrue(dead_dir.is_dir())
            finally:
                dead_dir.rmdir()
            # Mid-takeover mismatched-JSON legacy token: test explicit POSIX and Windows reader identities
            # with command-dispatched subprocess mocks so the test is host-independent on Linux and windows-latest.
            mid_dir = Path(tempfile.gettempdir()) / f"impossible-sdk-ref-{key}.midtest"
            mid_dir.mkdir(exist_ok=True)
            try:
                (mid_dir / "tok-posix-99999999-Ubuntu_24.04-1").write_text(
                    json.dumps({"pid": 88888888, "host": "posix", "distro": "Ubuntu-24.04", "token": "old"}),
                    encoding="utf-8",
                )
                def _wsl_dispatch(distros, pid_state="DEAD\n"):
                    def _run(cmd, **kw):
                        if "-l" in cmd:
                            return subprocess.CompletedProcess(cmd, 0, ("\n".join(distros) + "\n").encode("utf-16-le"), b"")
                        return subprocess.CompletedProcess(cmd, 0, pid_state, "")
                    return _run
                with mock.patch.object(ref_run.os, "name", "posix"), \
                     mock.patch.object(ref_run, "_pid_is_alive_same_host", return_value=False), \
                     mock.patch.dict(os.environ, {"WSL_DISTRO_NAME": "Ubuntu_24.04"}), \
                     mock.patch.object(ref_run.shutil, "which", return_value="wsl.exe"), \
                     mock.patch.object(ref_run.subprocess, "run", side_effect=_wsl_dispatch(["Ubuntu-24.04", "Ubuntu_24.04"])):
                    self.assertFalse(ref_run._try_clear_dead_lock(mid_dir, "claim-ambig"))
                with mock.patch.object(ref_run.os, "name", "posix"), \
                     mock.patch.object(ref_run, "_pid_is_alive_same_host", return_value=False), \
                     mock.patch.dict(os.environ, {"WSL_DISTRO_NAME": "Debian"}), \
                     mock.patch.object(ref_run.shutil, "which", return_value="wsl.exe"), \
                     mock.patch.object(ref_run.subprocess, "run", side_effect=_wsl_dispatch(["Ubuntu-24.04"])):
                    self.assertFalse(ref_run._try_clear_dead_lock(mid_dir, "claim-deb"))
                self.assertTrue(mid_dir.is_dir())
                with mock.patch.object(ref_run.os, "name", "posix"), \
                     mock.patch.object(ref_run, "_pid_is_alive_same_host", return_value=False), \
                     mock.patch.dict(os.environ, {"WSL_DISTRO_NAME": "Ubuntu-24.04"}), \
                     mock.patch.object(ref_run.shutil, "which", return_value="wsl.exe"), \
                     mock.patch.object(ref_run.subprocess, "run", side_effect=_wsl_dispatch(["Ubuntu-24.04"])):
                    self.assertTrue(ref_run._try_clear_dead_lock(mid_dir, "claim-ubu"))
                self.assertFalse(mid_dir.exists())

                mid_dir.mkdir(exist_ok=True)
                (mid_dir / "tok-posix-99999999-Ubuntu_24.04-1").write_text(
                    json.dumps({"pid": 88888888, "host": "posix", "distro": "Ubuntu-24.04", "token": "old"}),
                    encoding="utf-8",
                )
                with mock.patch.object(ref_run.os, "name", "nt"), \
                     mock.patch.object(ref_run.shutil, "which", return_value="wsl.exe"), \
                     mock.patch.object(ref_run.subprocess, "run", side_effect=_wsl_dispatch(["Ubuntu-24.04"], "DEAD\n")):
                    self.assertTrue(ref_run._try_clear_dead_lock(mid_dir, "claim-win-ubu"))
                self.assertFalse(mid_dir.exists())

                mid_dir.mkdir(exist_ok=True)
                (mid_dir / "tok-posix-99999999-none-1").write_text(
                    json.dumps({"pid": 88888888, "host": "posix", "distro": "none", "token": "old"}),
                    encoding="utf-8",
                )
                with mock.patch.object(ref_run.os, "name", "posix"), \
                     mock.patch.object(ref_run, "_pid_is_alive_same_host", return_value=False), \
                     mock.patch.dict(os.environ, {"WSL_DISTRO_NAME": "none"}):
                    self.assertTrue(ref_run._try_clear_dead_lock(mid_dir, "claim-none"))
                self.assertFalse(mid_dir.exists())
            finally:
                shutil.rmtree(mid_dir, ignore_errors=True)

            win_probe_dir = Path(tempfile.gettempdir()) / f"impossible-sdk-ref-{key}.wintest"
            win_probe_dir.mkdir(exist_ok=True)
            try:
                wtok = "posix-99999999-Ubuntu_24.04-2"
                (win_probe_dir / f"tok-{wtok}").write_text(
                    json.dumps({"pid": 99999999, "host": "posix", "distro": "Ubuntu-24.04", "token": wtok}),
                    encoding="utf-8",
                )
                seen_cmds = []
                def _fake_run(cmd, **kw):
                    seen_cmds.append(cmd)
                    return subprocess.CompletedProcess(cmd, 0, "DEAD\n", "")
                with mock.patch.object(ref_run.os, "name", "nt"), \
                     mock.patch.object(ref_run.shutil, "which", return_value="wsl.exe"), \
                     mock.patch.object(ref_run.subprocess, "run", side_effect=_fake_run):
                    self.assertTrue(ref_run._try_clear_dead_lock(win_probe_dir, "claim-win"))
                self.assertTrue(any("-d" in c and "Ubuntu-24.04" in c for c in seen_cmds), msg=seen_cmds)
            finally:
                shutil.rmtree(win_probe_dir, ignore_errors=True)

            # Force directory rename failure during dead-lock reclamation -> original dead marker must be restored.
            dead_dir.mkdir(exist_ok=True)
            dead_tok = f"{host}.99999999.{ref_run._encode_distro(distro)}.0"
            (dead_dir / f"tok-{dead_tok}").write_text(json.dumps({
                "pid": 99999999, "host": host, "distro": distro, "token": dead_tok, "created": 0,
            }), encoding="utf-8")
            with mock.patch.object(ref_run, "_rename_dir_with_retry", return_value=False):
                self.assertFalse(ref_run._try_clear_dead_lock(dead_dir, "my-claim-tok"))
                self.assertTrue((dead_dir / f"tok-{dead_tok}").is_file())
            shutil.rmtree(dead_dir, ignore_errors=True)

            # Force directory rename failure during release -> raises OSError and leaves tok-* intact (never an empty dir).
            live_v2 = Path(tempfile.gettempdir()) / f"impossible-sdk-ref-{key}.lockv2"
            try:
                with mock.patch.object(ref_run, "_rename_dir_with_retry", return_value=False):
                    with self.assertRaises(OSError):
                        with ref_run._store_lock(tstore, timeout=5.0):
                            self.assertTrue(dead_dir.is_dir())
                            self.assertTrue(live_v2.is_dir())
                self.assertTrue(any(p.name.startswith("tok-") for p in dead_dir.iterdir()))
            finally:
                shutil.rmtree(dead_dir, ignore_errors=True)
                shutil.rmtree(live_v2, ignore_errors=True)

            # PermissionError while releasing .lockv2 must still release .lockdir so the primary lock is never stranded.
            real_rel = ref_run._release_single_lock_dir
            def _fail_v2_release(root, ldir, tok):
                if ldir == live_v2:
                    raise PermissionError("injected v2 release failure")
                return real_rel(root, ldir, tok)
            try:
                with mock.patch.object(ref_run, "_release_single_lock_dir", side_effect=_fail_v2_release):
                    with self.assertRaises(PermissionError):
                        with ref_run._store_lock(tstore, timeout=5.0):
                            self.assertTrue(dead_dir.is_dir())
                            self.assertTrue(live_v2.is_dir())
                self.assertFalse(dead_dir.exists(), ".lockdir must be released even when .lockv2 release fails")
            finally:
                shutil.rmtree(live_v2, ignore_errors=True)

            # Live .lockv2 owner blocks _store_lock acquisition (and _store_lock holds both .lockdir and .lockv2).
            live_v2.mkdir(exist_ok=True)
            live_tok = f"{host}.{os.getpid()}.{ref_run._encode_distro(distro)}.1"
            (live_v2 / f"tok-{live_tok}").write_text(json.dumps({
                "pid": os.getpid(), "host": host, "distro": distro, "token": live_tok, "created": 0,
            }), encoding="utf-8")
            try:
                with self.assertRaises(TimeoutError):
                    with ref_run._store_lock(tstore, timeout=0.15):
                        pass
            finally:
                shutil.rmtree(live_v2, ignore_errors=True)

        # 10. Cross-host _store_lock contention: Windows prints READY after import, then blocks on _store_lock
        #     until WSL releases it, with a bounded READY deadline and clean process-tree teardown.
        if ref_run.is_wsl() and shutil.which("python.exe"):
            import select
            import time
            win_ref = ref_run.to_win_path(ROOT / "reference")
            win_store = ref_run.to_win_path(ROOT / "tests/reference")
            probe_code = (
                "import sys\n"
                "sys.path.insert(0, sys.argv[1])\n"
                "import run\n"
                "print('READY', flush=True)\n"
                "with run._store_lock(sys.argv[2]):\n"
                "    print('WIN_LOCKED', flush=True)\n"
            )
            proc = None
            try:
                with ref_run._store_lock(ROOT / "tests/reference"):
                    proc = subprocess.Popen(
                        ["python.exe", "-c", probe_code, win_ref, win_store],
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                        start_new_session=True,
                    )
                    readable, _, _ = select.select([proc.stdout], [], [], 15.0)
                    self.assertTrue(readable, "Windows python.exe timed out before printing READY")
                    ready_line = proc.stdout.readline().strip()
                    self.assertEqual(ready_line, "READY", msg=f"unexpected probe startup: {ready_line!r}")
                    time.sleep(0.5)
                    rc = proc.poll()
                    if rc is not None:
                        out, err = proc.communicate(timeout=10)
                        self.fail(f"Windows python.exe exited early ({rc}) while WSL held _store_lock: out={out!r} err={err!r}")
                out, err = proc.communicate(timeout=30)
                self.assertEqual(proc.returncode, 0, msg=err)
                self.assertIn("WIN_LOCKED", out)
            finally:
                if proc is not None:
                    if proc.poll() is None:
                        ref_run._kill_process_tree(proc)
                        proc.communicate()
                    if proc.stdout:
                        proc.stdout.close()
                    if proc.stderr:
                        proc.stderr.close()

    def test_package_provenance_and_missing_controls(self):
        ref_run, check_pkg = self._ref_modules()
        _, _, ver, comp, _, _ = check_pkg.verify_manifest_links(ROOT)
        self.assertEqual(ver, "1.8.260921001")
        self.assertEqual(comp["version"], "1.8.260803003")

        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / "repo"
            for rel in ("reference/package.json", "reference/Reference.csproj", "reference/packages.lock.json",
                        "tools/winui/pin.json", "ui/template_data.c",
                        "reference/templates/WrapPanel.xaml", "reference/templates/UniformGrid.xaml",
                        "reference/templates/DataGrid.xaml", "reference/templates/DockPanel.xaml"):
                dst = fake / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / rel, dst)

            # 1. Moving Reference.csproj version without package.json fails.
            csproj = fake / "reference/Reference.csproj"
            orig_csproj = csproj.read_text(encoding="utf-8")
            csproj.write_text(orig_csproj.replace("1.8.260921001", "1.9.0"), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_manifest_links(fake)
            csproj.write_text(orig_csproj, encoding="utf-8")

            # 2. Moving WinUI component version in packages.lock.json without package.json fails.
            lock = fake / "reference/packages.lock.json"
            orig_lock = lock.read_text(encoding="utf-8")
            lock.write_text(orig_lock.replace("1.8.260803003", "1.8.999999999"), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_manifest_links(fake)
            lock.write_text(orig_lock, encoding="utf-8")

            # 3. Moving tools/winui/pin.json commit without package.json fails.
            pin = fake / "tools/winui/pin.json"
            orig_pin = pin.read_text(encoding="utf-8")
            pin.write_text(orig_pin.replace("5417e89b32818cfd85f757e9d1290203dc3cfecb", "0" * 40), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_manifest_links(fake)
            pin.write_text(orig_pin, encoding="utf-8")

            # 4. Tampering with package_source_commit or template_diffs fails provenance_sha256 even offline.
            pkg_file = fake / "reference/package.json"
            orig_pkg = json.loads(pkg_file.read_text(encoding="utf-8"))
            pkg_file.write_text(json.dumps({**orig_pkg, "package_source_commit": "a" * 40}), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_manifest_links(fake)

            # 5. Missing-controls fallback: every declared entry must target its own compiled template (unrelated or hollow fails).
            unrelated_mc = {
                **orig_pkg,
                "missing_controls": [{
                    "control": "WrapPanel",
                    "basis": "compiled-template",
                    "compiled_template": "DefaultButtonStyle",
                    "expected_elements": 2,
                    "expected_states": 4,
                }],
            }
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_missing_controls(unrelated_mc, fake / "ui/template_data.c")

            hollow_mc = {**orig_pkg, "missing_controls": [{"control": "WrapPanel", "basis": "compiled-template", "compiled_template": None}]}
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_missing_controls(hollow_mc, fake / "ui/template_data.c")

            # 5b. All four committed missing controls (WrapPanel, UniformGrid, DataGrid, DockPanel) route end-to-end
            #     through reference/run.py (including store publication + validation) against their own compiled templates.
            committed_pkg = json.loads((ROOT / "reference/package.json").read_text(encoding="utf-8"))
            real_missing_cases = [
                {"id": "mc-wrap", "control": "WrapPanel", "expected_elements": 2, "expected_states": 0, "expected_setters": 3},
                {"id": "mc-ugrid", "control": "UniformGrid", "expected_elements": 3, "expected_states": 0, "expected_setters": 3},
                {"id": "mc-dgrid", "control": "DataGrid", "expected_elements": 4, "expected_states": 2, "expected_setters": 4},
                {"id": "mc-dock", "control": "DockPanel", "expected_elements": 3, "expected_states": 0, "expected_setters": 3},
            ]
            mc_cases_path = Path(tmp) / "real_mc_cases.json"
            mc_cases_path.write_text(json.dumps({"cases": real_missing_cases}), encoding="utf-8")
            mc_out = Path(tmp) / "mc_out"
            mc_store = Path(tmp) / "mc_store"
            mc_specs = ref_run.load_case_specs(mc_cases_path)
            ref_run.run_reference("unused-exe", mc_out, snapshotted_specs=mc_specs, store_dir=mc_store)
            mc_manifest = ref_run.validate_store_dir(mc_store, expected_specs=mc_specs)
            self.assertEqual(len(mc_manifest["cases"]), 4)
            by_mc_id = {c["id"]: c for c in mc_manifest["cases"]}
            self.assertEqual(by_mc_id["mc-wrap"]["basis"], "compiled-template")
            self.assertEqual(by_mc_id["mc-wrap"]["template"], "DefaultWrapPanelStyle")
            self.assertEqual(by_mc_id["mc-dgrid"]["verified_states"], 2)
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.check_missing_control_case(
                    {"id": "mc-wrap-bad", "control": "WrapPanel", "expected_elements": 99},
                    committed_pkg, ROOT / "ui/template_data.c",
                )
            # Unknown visual state on a fallback control must stop with the state name; valid state on DataGrid passes.
            dg_ok = check_pkg.check_missing_control_case(
                {"id": "mc-dgrid-normal", "control": "DataGrid", "state": "Normal"},
                committed_pkg, ROOT / "ui/template_data.c",
            )
            self.assertEqual(dg_ok["basis"], "compiled-template")
            with self.assertRaises(check_pkg.PackageDrift) as cm_state:
                check_pkg.check_missing_control_case(
                    {"id": "mc-wrap-missing-state", "control": "WrapPanel", "state": "MissingState"},
                    committed_pkg, ROOT / "ui/template_data.c",
                )
            self.assertIn("MissingState", str(cm_state.exception))
            # Unsupported properties (e.g. Width) and unsupported fields (e.g. script) must be explicitly refused with case ID and field.
            with self.assertRaises(check_pkg.PackageDrift) as cm_width:
                check_pkg.check_missing_control_case(
                    {"id": "mc-dg-width", "control": "DataGrid", "properties": {"Width": 32}},
                    committed_pkg, ROOT / "ui/template_data.c",
                )
            self.assertIn("mc-dg-width", str(cm_width.exception))
            self.assertIn("Width", str(cm_width.exception))
            with self.assertRaises(check_pkg.PackageDrift) as cm_script:
                check_pkg.check_missing_control_case(
                    {"id": "mc-dg-script", "control": "DataGrid", "script": [{"action": "focus"}]},
                    committed_pkg, ROOT / "ui/template_data.c",
                )
            self.assertIn("mc-dg-script", str(cm_script.exception))
            self.assertIn("script", str(cm_script.exception))
            for bad_case in (
                {"id": "mc-dg-style", "control": "DataGrid", "style": "NoSuchStyle"},
                {"id": "mc-dg-trans", "control": "DataGrid", "use_transitions": True},
                {"id": "mc-dg-theme", "control": "DataGrid", "theme": "typo"},
                {"id": "mc-dg-scale", "control": "DataGrid", "scale": -1},
            ):
                with self.assertRaises(check_pkg.PackageDrift):
                    check_pkg.check_missing_control_case(bad_case, committed_pkg, ROOT / "ui/template_data.c")
            # Stock-label controls (ToggleSwitch omitting OnContent/OffContent, DatePicker without permitted font) are refused,
            # and unverified/Microsoft fonts (Aptos, NoSuchFamily_For_Reference) are refused by the permitted-font allowlist.
            with self.assertRaises(ValueError) as cm_ts:
                ref_run.check_no_prohibited_assets({"id": "ts-stock", "control": "ToggleSwitch", "properties": {"IsOn": True}})
            self.assertIn("OnContent/OffContent", str(cm_ts.exception))
            with self.assertRaises(ValueError):
                ref_run.check_no_prohibited_assets({"id": "date-stock", "control": "DatePicker"})
            for bad_font in ("Aptos", "NoSuchFamily_For_Reference"):
                with self.assertRaises(ValueError):
                    ref_run.check_no_prohibited_assets({"id": "tb-bad-font", "control": "TextBlock", "properties": {"Text": "Hi", "FontFamily": bad_font}})
            ref_run.check_no_prohibited_assets({"id": "tb-ok-font", "control": "TextBlock", "properties": {"Text": "Hi", "FontFamily": "Inter"}})
            ref_run.check_no_prohibited_assets({
                "id": "ts-empty", "control": "ToggleSwitch", "properties": {"IsOn": True, "OnContent": "", "OffContent": ""}
            })
            # Nested missing-package control (DockPanel inside StackPanel) must be refused before launching WinUI with follow-up ref.
            nested_mc = {"nested-dock": {"id": "nested-dock", "control": "StackPanel", "children": [{"id": "ch-dock", "control": "DockPanel"}]}}
            with self.assertRaises(ValueError) as cm_nmc:
                ref_run.run_reference("unused-exe", Path(tmp) / "nmc_out", snapshotted_specs=nested_mc)
            self.assertIn("DockPanel", str(cm_nmc.exception))
            self.assertIn("D01 T01", str(cm_nmc.exception))
            # Malformed non-list children must fail during normalization naming case ID and field.
            with self.assertRaises(ValueError) as cm_ch:
                ref_run.normalize_case_spec({"id": "bad-ch", "control": "StackPanel", "children": {"control": "Button"}})
            self.assertIn("bad-ch", str(cm_ch.exception))
            self.assertIn("children", str(cm_ch.exception))
            # Allowlisted-but-uninstalled font, distinct prefix-sharing family ("DejaVu Sans Mono" != "DejaVu Sans"),
            # and uncovered codepoints (including PasswordChar="\uE700" and Text="\uE700") must all fail publish_store
            # while preserving the prior store; accented text covered by the font's cmap ("café") must pass.
            uninst_store = Path(tmp) / "uninst_store"
            shutil.copytree(ROOT / "tests/reference", uninst_store)
            prev_uninst = (uninst_store / "cases.json").read_text(encoding="utf-8")
            uninst_fresh = Path(tmp) / "uninst_fresh"
            uninst_fresh.mkdir()
            latin_ranges = [[0x20, 0x7E], [0xA0, 0xFF]]
            (uninst_fresh / "cases.json").write_text(json.dumps({
                "installed_fonts": ["dejavu sans mono"],
                "installed_font_ranges": {"dejavu sans mono": latin_ranges},
                "cases": [],
            }), encoding="utf-8")
            for bad_spec, want_msg in (
                ({"tb-inter": {"id": "tb-inter", "control": "TextBlock", "properties": {"Text": "Hi", "FontFamily": "Inter"}}}, "not an exact installed font family"),
                ({"tb-prefix": {"id": "tb-prefix", "control": "TextBlock", "properties": {"Text": "Hi", "FontFamily": "DejaVu Sans"}}}, "not an exact installed font family"),
                ({"tb-glyph": {"id": "tb-glyph", "control": "TextBlock", "properties": {"Text": "\uE700", "FontFamily": "DejaVu Sans Mono"}}}, "not covered by installed FontFamily"),
                ({"pw-glyph": {"id": "pw-glyph", "control": "PasswordBox", "properties": {"Password": "a", "PasswordChar": "\uE700", "FontFamily": "DejaVu Sans Mono"}}}, "not covered by installed FontFamily"),
            ):
                with self.assertRaises(ValueError) as cm_uf:
                    ref_run.publish_store(uninst_fresh, uninst_store, bad_spec)
                self.assertIn(want_msg, str(cm_uf.exception))
                self.assertEqual((uninst_store / "cases.json").read_text(encoding="utf-8"), prev_uninst)
            # Exact installed family with cmap-covered accented text ("café") and masked PasswordBox ("漢" masked by "*") pass;
            # numeric displayable content (OnContent=123) against an A-Z-only cmap fails.
            ref_run.check_no_prohibited_assets(
                {"id": "tb-cafe-ok", "control": "TextBlock", "properties": {"Text": "caf\u00e9", "FontFamily": "DejaVu Sans Mono"}},
                require_installed=True, installed_fonts=["dejavu sans mono"],
                installed_font_ranges={"dejavu sans mono": latin_ranges},
            )
            ref_run.check_no_prohibited_assets(
                {"id": "pw-masked-ok", "control": "PasswordBox", "properties": {"Password": "\u6f22", "PasswordChar": "*", "FontFamily": "DejaVu Sans Mono"}},
                require_installed=True, installed_fonts=["dejavu sans mono"],
                installed_font_ranges={"dejavu sans mono": latin_ranges},
            )
            ref_run.check_no_prohibited_assets(
                {"id": "pw-visible-ok", "control": "PasswordBox", "properties": {"Password": "abc", "PasswordRevealMode": "Visible", "FontFamily": "DejaVu Sans Mono"}},
                require_installed=True, installed_fonts=["dejavu sans mono"],
                installed_font_ranges={"dejavu sans mono": latin_ranges},
            )
            for bad_num_case in (
                {"id": "ts-num-uncovered", "control": "ToggleSwitch", "properties": {"OnContent": 123, "OffContent": 123, "FontFamily": "Inter"}},
                {"id": "nb-num-uncovered", "control": "NumberBox", "properties": {"Value": 123, "FontFamily": "Inter"}},
            ):
                with self.assertRaises(ValueError) as cm_num_lbl:
                    ref_run.check_no_prohibited_assets(
                        bad_num_case,
                        require_installed=True, installed_fonts=["inter"],
                        installed_font_ranges={"inter": [[0x41, 0x5A]]},
                    )
                self.assertIn("not covered by installed FontFamily", str(cm_num_lbl.exception))
            # Scripted TextBox typing uncovered letter 'A' and RichEditBox typing Shift+1 ('!') against digit-only cmap must fail;
            # masked PasswordBox typing 'A' with PasswordChar='*' covered by cmap must pass; nested DatePicker in Grid must preserve captured_frame.
            with self.assertRaises(ValueError) as cm_scr_tb:
                ref_run.check_no_prohibited_assets(
                    {"id": "tb-script-key", "control": "TextBox", "properties": {"FontFamily": "Inter"},
                     "script": [{"action": "focus"}, {"action": "key_press", "key": "A"}]},
                    require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x30, 0x39]]},
                )
            self.assertIn("not covered by installed FontFamily", str(cm_scr_tb.exception))
            with self.assertRaises(ValueError) as cm_scr_reb:
                ref_run.check_no_prohibited_assets(
                    {"id": "tb-shift-bang", "control": "TextBox", "properties": {"FontFamily": "Inter"},
                     "script": [{"action": "focus"}, {"action": "key_press", "key": "Number1", "modifiers": ["Shift"]}]},
                    require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x31, 0x31]]},
                    captured_frame={"id": "tb-shift-bang", "rendered_text": []},
                )
            self.assertIn("U+0021", str(cm_scr_reb.exception))
            ref_run.check_no_prohibited_assets(
                {"id": "pw-masked-script", "control": "PasswordBox", "properties": {"PasswordChar": "*", "FontFamily": "Inter"},
                 "script": [{"action": "focus"}, {"action": "key_press", "key": "A"}]},
                require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": [[0x2A, 0x2A]]},
                captured_frame={"id": "pw-masked-script", "rendered_text": [{"element": "PasswordBox", "font": "Inter", "text": "*"}]},
            )
            # Locale-formatted DatePicker without captured rendered_text fails; with localized 'März' fails ASCII-only cmap and passes Latin-1 cmap.
            dp_case = {"id": "dp-locale", "control": "DatePicker", "properties": {"FontFamily": "Inter"}}
            with self.assertRaises(ValueError) as cm_dp_nocap:
                ref_run.check_no_prohibited_assets(
                    dp_case, require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x20, 0x7E]]},
                )
            self.assertIn("requires captured rendered_text", str(cm_dp_nocap.exception))
            dp_cap = {"id": "dp-locale", "rendered_text": [{"element": "TextBlock", "font": "Inter", "text": "M\u00e4rz 2026"}]}
            with self.assertRaises(ValueError) as cm_dp_de:
                ref_run.check_no_prohibited_assets(
                    dp_case, require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x20, 0x7E]]}, captured_frame=dp_cap,
                )
            self.assertIn("U+00E4", str(cm_dp_de.exception))
            ref_run.check_no_prohibited_assets(
                dp_case, require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": latin_ranges}, captured_frame=dp_cap,
            )
            ref_run.check_no_prohibited_assets(
                {"id": "grid-dp", "control": "Grid", "children": [dp_case]},
                require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": [[0x20, 0x7E]]},
                captured_frame={"id": "grid-dp", "rendered_text": [{"element": "TextBlock", "font": "Inter", "text": "March"}]},
            )
            # TextTrimming=CharacterEllipsis requires captured rendered_text; fitting text without U+2026 passes ASCII-only cmap,
            # while trimmed text carrying U+2026 fails without U+2026 coverage and passes with it.
            trim_case = {"id": "tb-trim", "control": "TextBlock", "properties": {
                "Text": "A" * 80, "Width": 40, "TextTrimming": "CharacterEllipsis", "FontFamily": "Inter",
            }}
            with self.assertRaises(ValueError) as cm_trim_nocap:
                ref_run.check_no_prohibited_assets(
                    trim_case, require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x20, 0x7E]]},
                )
            self.assertIn("requires captured rendered_text", str(cm_trim_nocap.exception))
            with self.assertRaises(ValueError) as cm_trim:
                ref_run.check_no_prohibited_assets(
                    trim_case, require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x20, 0x7E]]},
                    captured_frame={"id": "tb-trim", "rendered_text": [{"element": "TextBlock", "font": "Inter", "text": "A" * 80 + "\u2026"}]},
                )
            self.assertIn("U+2026", str(cm_trim.exception))
            ref_run.check_no_prohibited_assets(
                trim_case, require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": [[0x20, 0x7E], [0x2026, 0x2026]]},
                captured_frame={"id": "tb-trim", "rendered_text": [{"element": "TextBlock", "font": "Inter", "text": "A" * 80 + "\u2026"}]},
            )
            fit_case = {"id": "tb-trim-fit", "control": "TextBlock", "properties": {
                "Text": "A", "Width": 400, "TextTrimming": "CharacterEllipsis", "FontFamily": "Inter",
            }}
            ref_run.check_no_prohibited_assets(
                fit_case, require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": [[0x41, 0x41]]},
                captured_frame={"id": "tb-trim-fit", "rendered_text": [{"element": "TextBlock", "font": "Inter", "text": "A"}]},
            )
            # Plain RichEditBox with Inter 'A' passes; unverified tab leader or rich decoration is refused.
            ref_run.check_no_prohibited_assets(
                {"id": "reb-plain", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}},
                require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": [[0x41, 0x41]]},
                captured_frame={"id": "reb-plain", "rendered_text": [{"element": "RichEditBox", "font": "Inter", "text": "A"}]},
            )
            with self.assertRaises(ValueError) as cm_reb_tab:
                ref_run.check_no_prohibited_assets(
                    {"id": "reb-tab", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}},
                    require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x41, 0x42]]},
                    captured_frame={"id": "reb-tab", "rendered_text": [{"element": "RichEditBox", "font": "Inter", "text": "A\tB"}]},
                )
            self.assertIn("tab leader", str(cm_reb_tab.exception))
            with self.assertRaises(ValueError) as cm_reb_dec:
                ref_run.check_no_prohibited_assets(
                    {"id": "reb-dec", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}},
                    require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x20, 0x7E]]},
                    captured_frame={"id": "reb-dec", "rendered_text": [
                        {"element": "RichEditBox", "font": "__unverified_rich_decoration__", "text": "TabLeader"},
                        {"element": "RichEditBox", "font": "Inter", "text": "A"},
                    ]},
                )
            self.assertIn("permitted non-Microsoft FontFamily", str(cm_reb_dec.exception))
            # Clipboard paste chords (Control+V / Shift+Insert) are disallowed in reference scripts.
            for bad_paste in (
                [{"action": "focus"}, {"action": "key_press", "key": "V", "modifiers": ["Control"]}],
                [{"action": "focus"}, {"action": "key_down", "key": "Shift"}, {"action": "key_press", "key": "Insert"}],
            ):
                with self.assertRaises(ValueError) as cm_paste:
                    ref_run.check_no_prohibited_assets(
                        {"id": "reb-paste", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}, "script": bad_paste},
                        require_installed=True, installed_fonts=["inter"],
                        installed_font_ranges={"inter": [[0x20, 0x7E]]},
                        captured_frame={"id": "reb-paste", "rendered_text": []},
                    )
                self.assertIn("clipboard paste", str(cm_paste.exception))
            # key_press Control releases a previously held Control, so a subsequent plain key_press V is allowed;
            # same-script Control+C followed by Control+V is also allowed.
            ref_run.check_no_prohibited_assets(
                {"id": "reb-plain-v-after-ctrl-press", "control": "RichEditBox", "properties": {"FontFamily": "Inter"},
                 "script": [
                     {"action": "key_down", "key": "Control"},
                     {"action": "key_press", "key": "Control"},
                     {"action": "key_press", "key": "V"},
                 ]},
                require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": [[0x56, 0x56], [0x76, 0x76]]},
                captured_frame={"id": "reb-plain-v-after-ctrl-press", "rendered_text": [{"element": "RichEditBox", "font": "Inter", "text": "v"}]},
            )
            ref_run.check_no_prohibited_assets(
                {"id": "tb-copy-paste-ok", "control": "TextBox", "properties": {"FontFamily": "Inter"},
                 "script": [
                     {"action": "focus"},
                     {"action": "key_press", "key": "A"},
                     {"action": "key_press", "key": "C", "modifiers": ["Control"]},
                     {"action": "key_press", "key": "V", "modifiers": ["Control"]},
                 ]},
                require_installed=True, installed_fonts=["inter"],
                installed_font_ranges={"inter": [[0x20, 0x7E]]},
                captured_frame={"id": "tb-copy-paste-ok", "rendered_text": [{"element": "TextBox", "font": "Inter", "text": "AA"}]},
            )
            # Grid RowDefinitions/ColumnDefinitions and attached layout props are non-text and publish without FontFamily;
            # custom template_xaml is refused from committed store publication.
            ref_run.check_no_prohibited_assets(
                {"id": "row-grid", "control": "Grid", "properties": {"RowDefinitions": [40, 40], "ColumnDefinitions": ["*", "Auto"]}},
                require_installed=True, installed_fonts=[], installed_font_ranges={},
            )
            with self.assertRaises(ValueError) as cm_tpl:
                ref_run.check_no_prohibited_assets(
                    {"id": "tpl-img", "control": "Button", "template_xaml": "<ControlTemplate><Image/></ControlTemplate>"},
                    require_installed=True, installed_fonts=[], installed_font_ranges={},
                    captured_frame={"id": "tpl-img", "rendered_text": []},
                )
            self.assertIn("template_xaml", str(cm_tpl.exception))
            # RTL compiled-template fallback must mirror asymmetric horizontal insets (Padding [2, 0, 8, 0] -> X=2 LTR vs X=8 RTL).
            dg_ltr = check_pkg.check_missing_control_case(
                {"id": "mc-dg-ltr", "control": "DataGrid", "flow_direction": "LeftToRight", "properties": {"Padding": [2, 0, 8, 0]}},
                committed_pkg, ROOT / "ui/template_data.c",
            )
            dg_rtl = check_pkg.check_missing_control_case(
                {"id": "mc-dg-rtl", "control": "DataGrid", "flow_direction": "RightToLeft", "properties": {"Padding": [2, 0, 8, 0]}},
                committed_pkg, ROOT / "ui/template_data.c",
            )
            ltr_scroll = dg_ltr["layout"]["Children"][0]["Children"][0]["Children"][0]["Rect"]
            rtl_scroll = dg_rtl["layout"]["Children"][0]["Children"][0]["Children"][0]["Rect"]
            self.assertEqual(ltr_scroll["X"], 3)  # 1px border + 2px left padding
            self.assertEqual(rtl_scroll["X"], 9)  # 1px border + 8px mirrored right padding
            self.assertEqual(dg_rtl["flow_direction"], "RightToLeft")
            # Scale metadata consistency: works for both 100%-host viewbox-vector captures and genuine 150%-host native-dpi captures.
            s150_committed = next(c for c in ref_run.open_reference_manifest(ROOT / "tests/reference")["cases"] if c["id"] == "button-Normal-Dark-s150")
            self.assertIn(s150_committed.get("scale_mode"), ("viewbox-vector", "native-dpi"))
            self.assertEqual(s150_committed.get("effective_rasterization_scale"), s150_committed.get("host_rasterization_scale"))
            def _require_native_dpi_capture(entry):
                req_s = float(entry.get("scale", 1.0))
                eff_s = float(entry.get("effective_rasterization_scale", 1.0))
                if entry.get("scale_mode") != "native-dpi" or abs(eff_s - req_s) > 1e-6:
                    raise ValueError(f"{entry.get('id')}: not a native-dpi capture (mode={entry.get('scale_mode')}, eff={eff_s}, req={req_s})")
            if s150_committed.get("scale_mode") == "native-dpi":
                _require_native_dpi_capture(s150_committed)
            else:
                self.assertEqual(s150_committed.get("host_rasterization_scale"), 1.0)
                with self.assertRaises(ValueError):
                    _require_native_dpi_capture(s150_committed)
            # Committed probe-r4-s15 layout child and automation root must agree in normalized logical coordinates.
            probe15 = next(c for c in ref_run.open_reference_manifest(ROOT / "tests/reference")["cases"] if c["id"] == "probe-r4-s15")
            self.assertEqual(probe15["layout"]["Children"][0]["Rect"], probe15["automation"]["Rect"])
            # Alpha difference outside mask must fail compare_frames_with_layout_mask (4-channel BGRA check).
            with self.assertRaises(ValueError):
                compare_frames_with_layout_mask(bytes([0, 0, 0, 0]), bytes([0, 0, 0, 255]), 1, 1, 1.0, [], [], limit=0)
            # Downstream drawing manifest validator and unfiltered per-case dump assertions must both accept fallbacks,
            # verifying real nested hierarchy, border inset, case-insensitive frame dimensions, and ControlType.
            committed_manifest = ref_run.open_reference_manifest(ROOT / "tests/reference")
            combined_manifest = {
                **committed_manifest,
                "cases": list(committed_manifest["cases"]) + [by_mc_id["mc-wrap"], by_mc_id["mc-dgrid"]],
            }
            validate_manifest(combined_manifest)
            for c in combined_manifest["cases"]:
                self.assertIsInstance(c.get("layout"), dict)
                self.assertIsInstance(c["layout"].get("Rect"), dict)
                self.assertIsInstance(c["layout"].get("Children"), list)
                self.assertIsInstance(c.get("automation"), dict)
                self.assertTrue(isinstance(c["automation"].get("ControlType"), str) and c["automation"]["ControlType"])
                self.assertIsInstance(c["automation"].get("Rect"), dict)
                self.assertIsInstance(c["automation"].get("IsEnabled"), bool)
            # WrapPanel nests ItemsHost inside RootBorder (1 immediate child, 1 grandchild).
            self.assertEqual(len(by_mc_id["mc-wrap"]["layout"]["Children"]), 1)
            self.assertEqual(by_mc_id["mc-wrap"]["layout"]["Children"][0]["Name"], "RootBorder")
            self.assertEqual(by_mc_id["mc-wrap"]["layout"]["Children"][0]["Children"][0]["Name"], "ItemsHost")
            # DataGrid nests RootGrid -> RootBorder (1px inset) -> ScrollHost -> RowsHost and nests RowsHost inside ScrollHost in automation.
            dg_grid = by_mc_id["mc-dgrid"]["layout"]["Children"][0]
            dg_border = dg_grid["Children"][0]
            dg_scroll = dg_border["Children"][0]
            self.assertEqual((dg_grid["Name"], dg_border["Name"], dg_scroll["Name"]), ("RootGrid", "RootBorder", "ScrollHost"))
            self.assertEqual(dg_scroll["Rect"], {"X": 1, "Y": 1, "Width": 158, "Height": 62})
            self.assertIn("Scroll", by_mc_id["mc-dgrid"]["automation"]["Patterns"])
            dg_auto_children = by_mc_id["mc-dgrid"]["automation"]["Children"]
            self.assertEqual(len(dg_auto_children), 1)
            self.assertEqual(dg_auto_children[0]["Name"], "ScrollHost")
            self.assertEqual(len(dg_auto_children[0]["Children"]), 1)
            self.assertEqual(dg_auto_children[0]["Children"][0]["Name"], "RowsHost")
            # Configured BorderThickness=4 + Padding=3 -> (7, 7, 146, 50); asymmetric array Padding=[1,2,3,4] -> (2, 3, 154, 56).
            dg_custom = check_pkg.check_missing_control_case(
                {"id": "mc-dgrid-custom", "control": "DataGrid", "properties": {"BorderThickness": 4, "Padding": 3}},
                committed_pkg, ROOT / "ui/template_data.c",
            )
            dg_c_scroll = dg_custom["layout"]["Children"][0]["Children"][0]["Children"][0]
            self.assertEqual(dg_c_scroll["Rect"], {"X": 7, "Y": 7, "Width": 146, "Height": 50})
            dg_asym = check_pkg.check_missing_control_case(
                {"id": "mc-dgrid-asym", "control": "DataGrid", "properties": {"Padding": [1, 2, 3, 4]}},
                committed_pkg, ROOT / "ui/template_data.c",
            )
            dg_a_scroll = dg_asym["layout"]["Children"][0]["Children"][0]["Children"][0]
            self.assertEqual(dg_a_scroll["Rect"], {"X": 2, "Y": 3, "Width": 154, "Height": 56})
            # Fractional DIP padding preserves exact fractional coordinates.
            dg_frac = check_pkg.check_missing_control_case(
                {"id": "mc-dgrid-frac", "control": "DataGrid", "properties": {"Padding": [0.5, 1.5, 2.5, 3.5]}},
                committed_pkg, ROOT / "ui/template_data.c",
            )
            dg_f_scroll = dg_frac["layout"]["Children"][0]["Children"][0]["Children"][0]
            self.assertEqual(dg_f_scroll["Rect"], {"X": 1.5, "Y": 2.5, "Width": 155, "Height": 57})
            # Unknown frame key ("Widht"), fractional width (0.5), 32-bit overflow, and non-string background must fail.
            with self.assertRaises(ValueError):
                ref_run.normalize_case_spec({"id": "bad-frame", "control": "DataGrid", "frame": {"Widht": 80}})
            for bad_frame in ({"Widht": 80}, {"width": 0.5}, {"width": 2147483648}, {"background": 42}):
                with self.assertRaises(check_pkg.PackageDrift):
                    check_pkg.check_missing_control_case(
                        {"id": "bad-frame", "control": "DataGrid", "frame": bad_frame},
                        committed_pkg, ROOT / "ui/template_data.c",
                    )
            # Case-insensitive frame dimensions ({"Width": 80, "Height": 40}) and IsEnabled: False must be honored.
            wrap_sized = check_pkg.check_missing_control_case(
                {"id": "mc-wrap-sized", "control": "WrapPanel", "frame": {"Width": 80, "Height": 40}, "properties": {"IsEnabled": False}},
                committed_pkg, ROOT / "ui/template_data.c",
            )
            self.assertEqual(wrap_sized["layout"]["Rect"], {"X": 0, "Y": 0, "Width": 80, "Height": 40})
            self.assertFalse(wrap_sized["automation"]["IsEnabled"])

            # Real compiled-template fallback routed end-to-end through reference/run.py (ToggleSwitch -> DefaultToggleSwitchStyle).
            sim_ok = {
                **orig_pkg,
                "missing_controls": [{
                    "control": "ToggleSwitch",
                    "reason": "Simulated absent control",
                    "basis": "compiled-template",
                    "compiled_template": "DefaultToggleSwitchStyle",
                    "expected_elements": 23,
                    "expected_states": 9,
                    "expected_setters": 11,
                }],
            }
            check_pkg.verify_missing_controls(sim_ok, fake / "ui/template_data.c")
            sim_pkg_path = Path(tmp) / "sim_package.json"
            sim_pkg_path.write_text(json.dumps(sim_ok), encoding="utf-8")
            sim_cases_path = Path(tmp) / "sim_cases.json"
            sim_cases_path.write_text(json.dumps({"cases": [{
                "id": "ts-fallback", "control": "ToggleSwitch",
                "expected_elements": 23, "expected_states": 9, "expected_setters": 11,
            }]}), encoding="utf-8")
            sim_out = Path(tmp) / "sim_out"
            ref_run.run_reference("unused-exe", sim_out, cases_path=sim_cases_path, pkg_manifest_path=sim_pkg_path)
            sim_manifest = json.loads((sim_out / "cases.json").read_text(encoding="utf-8"))
            self.assertEqual(len(sim_manifest["cases"]), 1)
            res_case = sim_manifest["cases"][0]
            self.assertEqual(
                (res_case["basis"], res_case["template"], res_case["verified_elements"], res_case["verified_states"], res_case["verified_setters"]),
                ("compiled-template", "DefaultToggleSwitchStyle", 23, 9, 11),
            )
            sim_bad_cases = Path(tmp) / "sim_bad_cases.json"
            sim_bad_cases.write_text(json.dumps({"cases": [{
                "id": "ts-bad", "control": "ToggleSwitch", "expected_states": 999,
            }]}), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                ref_run.run_reference("unused-exe", Path(tmp) / "sim_bad_out", cases_path=sim_bad_cases, pkg_manifest_path=sim_pkg_path)

            # 6. Nonexistent --nuget-pkg path must fail explicitly.
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_package(ROOT, nuget_pkg=Path(tmp) / "nonexistent")

            # 7. Synthetic live DLL + generic.xaml + WinUI checkout verification runs unconditionally on every host.
            syn_commit = "b" * 40
            syn_nuget = Path(tmp) / "syn_nuget"
            (syn_nuget / "runtimes-framework/win-x64/native").mkdir(parents=True)
            (syn_nuget / "lib/native/Microsoft.UI/Themes").mkdir(parents=True)
            (syn_nuget / "runtimes-framework/win-x64/native/Microsoft.ui.xaml.dll").write_bytes(
                f"FileVersion\x003.1.8.2608 (syn-{syn_commit})\x00".encode("utf-16-le")
            )
            syn_gx_xml = (
                '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" '
                'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">'
                '<Style x:Key="DefaultButtonStyle" TargetType="Button"><Setter Property="Template"><Setter.Value><ControlTemplate><Grid/></ControlTemplate></Setter.Value></Setter></Style>'
                '<Style x:Key="PkgOnlyStyle" TargetType="ToggleSwitch"><Setter Property="Template"><Setter.Value><ControlTemplate><Border/></ControlTemplate></Setter.Value></Setter></Style>'
                '<Style x:Key="DefaultTextBoxStyle" TargetType="TextBox"><Setter Property="Template"><Setter.Value><ControlTemplate><Grid/></ControlTemplate></Setter.Value></Setter></Style>'
                '<Style x:Key="DefaultListViewStyle" TargetType="ListView"><Setter Property="Template"><Setter.Value><ControlTemplate><Grid/></ControlTemplate></Setter.Value></Setter></Style>'
                '<Style x:Key="DefaultListViewItemStyle" TargetType="ListViewItem"><Setter Property="Template"><Setter.Value><ControlTemplate><Grid/></ControlTemplate></Setter.Value></Setter></Style>'
                '<Style x:Key="DefaultThumbStyle" TargetType="Thumb"><Setter Property="Template"><Setter.Value><ControlTemplate><Grid/></ControlTemplate></Setter.Value></Setter></Style>'
                '</ResourceDictionary>'
            )
            syn_gx = syn_nuget / "lib/native/Microsoft.UI/Themes/generic.xaml"
            syn_gx.write_text(syn_gx_xml, encoding="utf-8")
            syn_winui = Path(tmp) / "syn_winui"
            (syn_winui / "dxaml/xcp/dxaml/themes").mkdir(parents=True)
            (syn_winui / "LICENSE").write_text("MIT\n", encoding="utf-8")
            (syn_winui / "dxaml/xcp/dxaml/themes/generic.xaml").write_text(
                '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" '
                'xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">'
                '<Style x:Key="DefaultButtonStyle" TargetType="Button"><Setter Property="Template"><Setter.Value><ControlTemplate><Border/></ControlTemplate></Setter.Value></Setter></Style>'
                '<Style x:Key="PinOnlyStyle" TargetType="Button"><Setter Property="Template"><Setter.Value><ControlTemplate><Grid/></ControlTemplate></Setter.Value></Setter></Style>'
                '</ResourceDictionary>',
                encoding="utf-8",
            )
            syn_pkg = json.loads(json.dumps(orig_pkg))
            syn_pkg["package_source_commit"] = syn_commit
            syn_pkg["generic_xaml_sha256"] = hashlib.sha256(syn_gx.read_bytes()).hexdigest()
            syn_pkg["template_diffs"] = {
                "changed": ["DefaultButtonStyle"],
                "pin_only": ["PinOnlyStyle"],
                "package_only": ["DefaultListViewItemStyle", "DefaultListViewStyle", "DefaultTextBoxStyle", "DefaultThumbStyle", "PkgOnlyStyle"],
            }
            syn_pkg["provenance_sha256"] = check_pkg.compute_provenance_sha256(syn_pkg)
            pkg_file.write_text(json.dumps(syn_pkg), encoding="utf-8")
            syn_info = check_pkg.verify_package(fake, winui_checkout=syn_winui, nuget_pkg=syn_nuget, require_live=True)
            self.assertTrue(syn_info["checked_live"])

            bad_commit = {**syn_pkg, "package_source_commit": "c" * 40}
            bad_commit["provenance_sha256"] = check_pkg.compute_provenance_sha256(bad_commit)
            pkg_file.write_text(json.dumps(bad_commit), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_package(fake, winui_checkout=syn_winui, nuget_pkg=syn_nuget, require_live=True)

            bad_diff = json.loads(json.dumps(syn_pkg))
            bad_diff["template_diffs"]["changed"] = []
            bad_diff["provenance_sha256"] = check_pkg.compute_provenance_sha256(bad_diff)
            pkg_file.write_text(json.dumps(bad_diff), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_package(fake, winui_checkout=syn_winui, nuget_pkg=syn_nuget, require_live=True)

    def test_package_provenance_live_dll_and_template_diffs(self):
        _, check_pkg = self._ref_modules()
        info = check_pkg.verify_package(ROOT, require_live=True)
        self.assertTrue(info["checked_live"])
        self.assertEqual(info["template_diffs"], {"changed": 25, "pin_only": 32, "package_only": 73})
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / "repo"
            for rel in ("reference/package.json", "reference/Reference.csproj", "reference/packages.lock.json",
                        "tools/winui/pin.json", "ui/template_data.c", "ui/theme_data.c",
                        "reference/templates/WrapPanel.xaml", "reference/templates/UniformGrid.xaml",
                        "reference/templates/DataGrid.xaml", "reference/templates/DockPanel.xaml"):
                dst = fake / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / rel, dst)
            pkg_file = fake / "reference/package.json"
            orig_pkg = json.loads(pkg_file.read_text(encoding="utf-8"))
            for bucket in ("changed", "pin_only", "package_only"):
                bad = json.loads(json.dumps(orig_pkg))
                bad["template_diffs"][bucket] = bad["template_diffs"][bucket][:-1]
                bad["provenance_sha256"] = check_pkg.compute_provenance_sha256(bad)
                pkg_file.write_text(json.dumps(bad), encoding="utf-8")
                with self.assertRaises(check_pkg.PackageDrift):
                    check_pkg.verify_package(fake, require_live=True)
            bad_commit = {**orig_pkg, "package_source_commit": "a" * 40}
            bad_commit["provenance_sha256"] = check_pkg.compute_provenance_sha256(bad_commit)
            pkg_file.write_text(json.dumps(bad_commit), encoding="utf-8")
            with self.assertRaises(check_pkg.PackageDrift):
                check_pkg.verify_package(fake, require_live=True)

    def test_layout_automation_scale_rtl_and_script_dumps(self):
        ref_run, _ = self._ref_modules()
        folder = ROOT / "tests/reference"
        manifest, frames = ref_run.read_reference_snapshot(folder)
        # Every stored case (including compiled-template fallbacks) must include structured layout and automation dumps.
        for c in manifest["cases"]:
            self.assertIsInstance(c.get("layout"), dict)
            self.assertIsInstance(c["layout"].get("Rect"), dict)
            self.assertIsInstance(c["layout"].get("Children"), list)
            self.assertIsInstance(c.get("automation"), dict)
            self.assertTrue(isinstance(c["automation"].get("ControlType"), str) and c["automation"]["ControlType"])
            self.assertIsInstance(c["automation"].get("Rect"), dict)
            self.assertIsInstance(c["automation"].get("IsEnabled"), bool)
        by_id = {c["id"]: c for c in manifest["cases"]}

        # 1. Button layout + automation dumps have frame-relative coords, Invoke pattern, and correct hierarchy.
        btn = by_id["button-Normal-Dark"]
        layout = btn["layout"]
        self.assertEqual(layout["Type"], "Grid")
        self.assertEqual((layout["Rect"]["X"], layout["Rect"]["Y"], layout["Rect"]["Width"], layout["Rect"]["Height"]), (0, 0, 160, 64))
        btn_node = layout["Children"][0]
        self.assertEqual(btn_node["Type"], "Button")
        self.assertEqual(btn_node["Name"], "TargetButton")
        self.assertEqual((btn_node["Rect"]["X"], btn_node["Rect"]["Y"], btn_node["Rect"]["Width"], btn_node["Rect"]["Height"]), (20, 16, 120, 32))
        auto = btn["automation"]
        self.assertEqual(auto["ControlType"], "Button")
        self.assertTrue(auto["IsEnabled"])
        self.assertFalse(auto["HasKeyboardFocus"])
        self.assertIn("Invoke", auto["Patterns"])
        self.assertEqual((auto["Rect"]["X"], auto["Rect"]["Y"], auto["Rect"]["Width"], auto["Rect"]["Height"]), (20, 16, 120, 32))

        # 2. Visually forced Disabled vs actually disabled (IsEnabled=false).
        self.assertTrue(by_id["button-Disabled-Dark"]["automation"]["IsEnabled"])
        self.assertFalse(by_id["button-ReallyDisabled-Dark"]["automation"]["IsEnabled"])

        # 3. Scaled captures at 150%, 200%, 300% (both Dark and Light) pass crispness/anti-stairstep checks, while bilinear & nearest fail.
        s100_dark = frames["button-Normal-Dark.bgra"]
        s100_light = frames["button-Normal-Light.bgra"]
        for cid, theme, base100, scale, w, h in [
            ("button-Normal-Dark-s150", "Dark", s100_dark, 1.5, 240, 96),
            ("button-Normal-Dark-s200", "Dark", s100_dark, 2.0, 320, 128),
            ("button-Normal-Dark-s300", "Dark", s100_dark, 3.0, 480, 192),
            ("button-Normal-Light-s150", "Light", s100_light, 1.5, 240, 96),
            ("button-Normal-Light-s200", "Light", s100_light, 2.0, 320, 128),
            ("button-Normal-Light-s300", "Light", s100_light, 3.0, 480, 192),
        ]:
            case = by_id[cid]
            self.assertEqual((case["theme"], case["scale"], case["width"], case["height"]), (theme, scale, w, h))
            self.assertIn(case["scale_mode"], ("viewbox-vector", "native-dpi"))
            ar = case["automation"]["Rect"]
            self.assertEqual((ar["X"], ar["Y"], ar["Width"], ar["Height"]), (20, 16, 120, 32))
            real = frames[case["file"]]
            check_scaled_control_capture(case, real)
            with self.assertRaises(ValueError):
                check_scaled_control_capture(case, bilinear_upscale(base100, 160, 64, w, h))
            with self.assertRaises(ValueError):
                check_scaled_control_capture(case, nearest_upscale(base100, 160, 64, w, h))

        # 4. RTL capture mirrors HorizontalAlignment=Left to the right edge of the 160px frame.
        rtl_case = by_id["button-Normal-Dark-RTL"]
        self.assertEqual(rtl_case["flow_direction"], "RightToLeft")
        rtl_px = frames[rtl_case["file"]]
        xs = [x for x in range(160) if any(rtl_px[(32 * 160 + x) * 4 + k] != 32 for k in range(3))]
        self.assertEqual((min(xs), max(xs)), (40, 159))

        # 5. Scripted button log covers pointer, keyboard, and touch with real routed events and focus.
        slog = by_id["button-Script-Dark"]["script_log"]
        validate_script_log(slog)
        # Dropping any modality must fail.
        for drop_ev in ("Mouse", "KeyDown:", "Touch"):
            filtered = {
                **slog,
                "Events": [e for e in slog["Events"] if drop_ev not in e.get("PointerType", "") and drop_ev not in e.get("Event", "")],
            }
            with self.assertRaises(ValueError):
                validate_script_log(filtered)

        # Stripping PointerReleased or emptying Events must fail BOTH validate_script_log AND production store validation.
        no_rel = {**slog, "Events": [e for e in slog["Events"] if e.get("Event") != "PointerReleased"]}
        with self.assertRaises(ValueError):
            validate_script_log(no_rel)
        expected_specs = ref_run.load_case_specs(ref_run.CASES_DIR)
        for bad_label, bad_events in (("missing-release", no_rel["Events"]), ("empty-events", [])):
            with tempfile.TemporaryDirectory() as tmp_store_root:
                tmp_store = Path(tmp_store_root) / "reference"
                shutil.copytree(folder, tmp_store)
                idx = json.loads((tmp_store / "cases.json").read_text(encoding="utf-8"))
                for item in idx["cases"]:
                    if item.get("id") == "button-Script-Dark":
                        item["script_log"]["Events"] = bad_events
                (tmp_store / "cases.json").write_text(json.dumps(idx, indent=2), encoding="utf-8")
                with self.assertRaises(ValueError, msg=f"production store validator must reject {bad_label}"):
                    ref_run._validate_store_dir_unlocked(tmp_store, expected_specs)
        # Shift+Delete counts as a same-script cut in Python's script validator (matching Script.cs).
        ref_run._check_no_clipboard_paste("sd-case", [
            {"action": "key_press", "key": "Delete", "modifiers": ["Shift"]},
            {"action": "key_press", "key": "V", "modifiers": ["Control"]},
        ])
        # Unindexed .bgra in a marked raw capture output directory OR its .out.backup must be refused and survive byte-for-byte.
        with tempfile.TemporaryDirectory() as tmp_raw:
            raw_out = Path(tmp_raw) / "out"
            raw_src = Path(tmp_raw) / "src"
            raw_out.mkdir()
            raw_src.mkdir()
            (raw_out / ref_run.OUT_MARKER).write_text("impossible-sdk-reference-output\n", encoding="utf-8")
            (raw_out / "cases.json").write_text(json.dumps({"cases": [{"id": "a", "file": "a.bgra"}]}), encoding="utf-8")
            (raw_out / "a.bgra").write_bytes(b"\x01\x02\x03\x04")
            keep_bytes = b"KEEP_ME_UNINDEXED_RAW_FRAME_BYTES"
            (raw_out / "keep_me.bgra").write_bytes(keep_bytes)
            (raw_src / "cases.json").write_text(json.dumps({"cases": []}), encoding="utf-8")
            with self.assertRaises(ValueError):
                ref_run._ensure_safe_capture_dir(raw_out)
            with self.assertRaises(ValueError):
                ref_run._publish_capture_output(raw_src, raw_out)
            self.assertEqual((raw_out / "keep_me.bgra").read_bytes(), keep_bytes)
            # Same ownership rule applies to a leftover .out.backup sibling alongside raw_out.
            (raw_out / "keep_me.bgra").unlink()
            raw_bak = Path(tmp_raw) / ".out.backup"
            raw_bak.mkdir()
            (raw_bak / ref_run.OUT_MARKER).write_text("impossible-sdk-reference-output\n", encoding="utf-8")
            (raw_bak / "cases.json").write_text(json.dumps({"cases": [{"id": "a", "file": "a.bgra"}]}), encoding="utf-8")
            (raw_bak / "a.bgra").write_bytes(b"\x01\x02\x03\x04")
            bak_keep_bytes = b"KEEP_ME_IN_BACKUP_RAW_FRAME_BYTES"
            (raw_bak / "keep_bak.bgra").write_bytes(bak_keep_bytes)
            with self.assertRaises(ValueError):
                ref_run._ensure_safe_capture_dir(raw_out)
            with self.assertRaises(ValueError):
                ref_run._publish_capture_output(raw_src, raw_out)
            self.assertEqual((raw_bak / "keep_bak.bgra").read_bytes(), bak_keep_bytes)

    def test_rectangle_masking_for_text_and_icon_at_fractional_scale(self):
        ref_run, _ = self._ref_modules()
        folder = ROOT / "tests/reference"
        manifest, frames = ref_run.read_reference_snapshot(folder, filenames=["button-Icon-Dark-s150.bgra"])
        icon_case = next(c for c in manifest["cases"] if c["id"] == "button-Icon-Dark-s150")
        real = frames[icon_case["file"]]
        w, h, scale = icon_case["width"], icon_case["height"], float(icon_case["scale"])
        ref_rects = extract_mask_rects(icon_case["layout"])
        self.assertEqual(len(ref_rects), 1)
        # Candidate geometry is computed independently from the case spec (12x12 icon centered in 160x64 frame: x=74, y=26),
        # never copied from ref_rects, so a Dump.cs coordinate regression fails immediately.
        independent_icon = ("SdkIcon", 74.0, 26.0, 12.0, 12.0)
        independent_text = ("LabelText", 30.0, 24.0, 30.0, 14.0)
        ref_both = [*ref_rects, independent_text]
        cand_both = [independent_icon, independent_text]

        # Mutating pixels strictly inside the independently specified icon AND text rects must pass.
        mut = bytearray(real)
        for _, rx, ry, rw, rh in cand_both:
            px, py = round((rx + rw * 0.5) * scale), round((ry + rh * 0.5) * scale)
            mut[(py * w + px) * 4:(py * w + px) * 4 + 3] = bytes([255, 0, 255])
        compare_frames_with_layout_mask(bytes(mut), real, w, h, scale, cand_both, ref_both, limit=0)

        # Moving a candidate rectangle by 1 device pixel at EVERY required scale (100%, 150%, 200%, 300%),
        # including half-pixel origins where ties-to-even rounding would otherwise collide (1.5 -> 2.5), must fail.
        for req_scale in (1.0, 1.5, 2.0, 3.0):
            one_px_dip = 1.0 / req_scale
            buf_s = bytes(round(60 * req_scale) * round(40 * req_scale) * 4)
            for base_x in (10.0, 1.5 / req_scale):
                ref_s = [("Lbl", base_x, 10.0, 2.0 / req_scale, 10.0)]
                cand_s_moved = [("Lbl", base_x + one_px_dip, 10.0, 2.0 / req_scale, 10.0)]
                with self.assertRaises(ValueError, msg=f"1-device-pixel move must fail at scale={req_scale} base_x={base_x}"):
                    compare_frames_with_layout_mask(
                        buf_s, buf_s, round(60 * req_scale), round(40 * req_scale), req_scale, cand_s_moved, ref_s, limit=0
                    )

        # Moving a candidate rectangle by 2 DIPs must fail even though its pixels are masked.
        moved = [(independent_icon[0], independent_icon[1] + 2.0, independent_icon[2], independent_icon[3], independent_icon[4]), independent_text]
        with self.assertRaises(ValueError):
            compare_frames_with_layout_mask(bytes(mut), real, w, h, scale, moved, ref_both, limit=0)

        # Missing a masked element must fail.
        with self.assertRaises(ValueError):
            compare_frames_with_layout_mask(bytes(mut), real, w, h, scale, [independent_icon], ref_both, limit=0)

        # Mutating a pixel immediately outside a masked rect must fail.
        outside = bytearray(real)
        out_x = round(independent_icon[1] * scale) - 2
        out_y = round(independent_icon[2] * scale)
        outside[(out_y * w + out_x) * 4] ^= 0x40
        with self.assertRaises(ValueError):
            compare_frames_with_layout_mask(bytes(outside), real, w, h, scale, cand_both, ref_both, limit=0)


@unittest.skipUnless(os.name == "nt" or "microsoft" in platform.release().lower(), "needs Windows, directly or through WSL")
class Drawing(unittest.TestCase):
    def test_drawing(self):
        self.assertEqual(subprocess.run([str(BUILD / "ui-tests.exe")], stdin=subprocess.DEVNULL, capture_output=True, timeout=60).returncode, 0)

    def test_host_callback_table(self):
        self.assertEqual(subprocess.run([str(BUILD / "test-host.exe")], stdin=subprocess.DEVNULL, capture_output=True, timeout=60).returncode, 0)

    def test_five_scale_panel_layout_matches_winui_oracle(self):
        sys.path.insert(0, str(ROOT / "tests"))
        import layout_cases
        oracle_path = ROOT / "tests/fixtures/layout_reference.json"
        oracle = json.loads(oracle_path.read_text(encoding="utf-8"))
        proc = subprocess.run([str(BUILD / "layout-dump.exe")], stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60)
        self.assertEqual(proc.returncode, 0, proc.stderr)
        actual = json.loads(proc.stdout)

        # Verify every required base case at all 5 scales independently of whichever entries appear in the file.
        for scale in layout_cases.SCALES:
            tag = layout_cases.scale_tag(scale)
            for base_name in layout_cases.REQUIRED_NODES:
                cid = f"{base_name}-{tag}"
                self.assertIn(cid, oracle, f"oracle missing {cid}")
                self.assertIn(cid, actual, f"layout-dump missing {cid}")
                diffs = layout_cases.compare_named_layouts(
                    cid, base_name, oracle[cid]["nodes"], actual[cid]["nodes"], tol=0.05
                )
                self.assertEqual(diffs, [], "\n".join(diffs))

        # Verify fractional plateau rounding actually differs from unrounded coordinates at 100%, 125%, 150%, 300%.
        for tag in ("s100", "s125", "s150", "s300"):
            nodes = actual[f"border-round-{tag}"]["nodes"]
            rx = nodes["RoundedChild"]["Rect"]["X"]
            ux = nodes["UnroundedChild"]["Rect"]["X"] - 60.0
            self.assertNotAlmostEqual(rx, ux, places=2, msg=f"expected rounding difference at {tag}: rounded={rx} unrounded={ux}")

        # Verify stored reference Button (120x32 centered in 160x64) against tests/reference/cases.json button-Normal-Dark.
        ref_manifest = json.loads((ROOT / "tests/reference/cases.json").read_text(encoding="utf-8"))
        ref_btn = next(c for c in ref_manifest["cases"] if c["id"] == "button-Normal-Dark")
        def _find_type(node, typ):
            if node.get("Type") == typ:
                return node
            for ch in node.get("Children", []):
                hit = _find_type(ch, typ)
                if hit is not None:
                    return hit
            return None
        ref_btn_node = _find_type(ref_btn["layout"], "Button")
        self.assertIsNotNone(ref_btn_node, "button-Normal-Dark missing Button node in layout")
        ref_rect = ref_btn_node["Rect"]
        sdk_rect = actual["stored-button-s100"]["nodes"]["StoredButton"]["Rect"]
        for k in ("X", "Y", "Width", "Height"):
            self.assertAlmostEqual(sdk_rect[k], ref_rect[k], places=2, msg=f"stored button {k}: sdk={sdk_rect[k]} ref={ref_rect[k]}")

        # Verify compiled StackPanel XAML template end-to-end: compile real XAML via tools/winui/templates.py,
        # build and execute a freestanding C harness linked against libImpossibleUI.a, and assert child layout.
        sys.path.insert(0, str(ROOT / "tools/winui"))
        import templates as tpl_mod
        xaml = (
            '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"'
            ' xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">'
            '<ControlTemplate TargetType="ContentControl">'
            '<StackPanel Orientation="Horizontal" Spacing="8">'
            '<Border x:Name="TplA" Width="20" Height="10"/>'
            '<Border x:Name="TplB" Width="30" Height="10"/>'
            '</StackPanel>'
            '</ControlTemplate>'
            '</ResourceDictionary>'
        )
        doc = tpl_mod.parse_xaml(xaml, "stack_test.xaml")
        comp = tpl_mod.Compiler(res_dict={}, brushes={}, values={})
        comp.compile_control_template(doc.children[0], key="StackTestTpl", target_type="ContentControl", style_setters=(0, 0), enum_id="IM_TEMPLATE_STACK_TEST")
        tpl_mod.verify_fully_consumed(doc.children[0])
        emitted = tpl_mod.emit(comp)
        h_text, c_text = emitted["template_data.h"], emitted["template_data.c"]
        self.assertIn("IM_ELEM_STACK_PANEL", c_text)
        self.assertIn('"Horizontal"', c_text)
        self.assertIn('"Spacing"', c_text)
        cc = None
        for comp_cmake in sorted((BUILD / "CMakeFiles").glob("*/CMakeCCompiler.cmake")):
            for line in comp_cmake.read_text(encoding="utf-8", errors="ignore").splitlines():
                if line.startswith('set(CMAKE_C_COMPILER "') and line.endswith('")'):
                    cand = line[len('set(CMAKE_C_COMPILER "'):-2]
                    if Path(cand).is_file():
                        cc = cand
                        break
            if cc:
                break
        if cc is None and (BUILD / "CMakeCache.txt").is_file():
            for line in (BUILD / "CMakeCache.txt").read_text(encoding="utf-8", errors="ignore").splitlines():
                if line.startswith("CMAKE_C_COMPILER:FILEPATH="):
                    cand = line.split("=", 1)[1].strip()
                    if Path(cand).is_file():
                        cc = cand
                        break
        self.assertIsNotNone(cc, f"C compiler not found in {BUILD}")
        with tempfile.TemporaryDirectory() as tmp:
            tp = Path(tmp)
            (tp / "tpl_stack.h").write_text(h_text, encoding="utf-8")
            (tp / "tpl_stack.c").write_text(
                c_text.replace("const im_template im_templates[IM_TEMPLATE_COUNT]", "static const im_template test_templates[]")
                      .replace("const im_template_setter im_template_setters[]", "static const im_template_setter test_setters[]")
                      .replace("const im_template_element im_template_elements[]", "static const im_template_element test_elements[]")
                      .replace("const im_template_prop im_template_props[]", "static const im_template_prop test_props[]")
                      .replace("const im_template_group im_template_groups[]", "static const im_template_group test_groups[]")
                      .replace("const im_template_state im_template_states[]", "static const im_template_state test_states[]")
                      .replace("const im_template_transition im_template_transitions[]", "static const im_template_transition test_transitions[]")
                      .replace("const im_template_anim im_template_anims[]", "static const im_template_anim test_anims[]")
                      .replace("const im_template_keyframe im_template_keyframes[]", "static const im_template_keyframe test_keyframes[]"),
                encoding="utf-8",
            )
            (tp / "main.c").write_text(
                '#include <stdint.h>\n#include <stddef.h>\n#include "tree.h"\n#include "layout.h"\n'
                '#include "tpl_stack.c"\n'
                'static uint8_t mem[65536]; static size_t used = 0;\n'
                'static void *al(void *c, size_t n){(void)c; n=(n+7u)&~7u; if(used+n>sizeof(mem))return NULL; void *p=&mem[used]; used+=n; return p;}\n'
                'static void *rl(void *c, void *p, size_t o, size_t n){void *q=al(c,n); if(!q)return NULL; if(p){for(size_t i=0;i<(o<n?o:n);i++)((uint8_t*)q)[i]=((uint8_t*)p)[i];} return q;}\n'
                'static void fr(void *c, void *p, size_t n){(void)c;(void)p;(void)n;}\n'
                'int main(void){\n'
                '  im_tree t; im_palette pal={IM_DARK,{0}}; im_allocator a={al,rl,fr,NULL};\n'
                '  if(!im_tree_init(&t,a)) return 1;\n'
                '  im_node *r=im_tree_instantiate_custom_template(&t,&pal,&test_templates[0],test_setters,test_elements,test_props,"H");\n'
                '  if(!r) return 2;\n'
                '  t.root=r; im_layout_run(&t,r,100*IM_ONE,40*IM_ONE,IM_ONE);\n'
                '  im_node *b=im_tree_find_by_name(r,"TplB");\n'
                '  if(!b || b->absolute_rect.left!=28*IM_ONE || (b->absolute_rect.right-b->absolute_rect.left)!=30*IM_ONE) return 3;\n'
                '  im_tree_destroy(&t); return 0;\n'
                '}\n',
                encoding="utf-8",
            )
            exe = tp / "stack_tpl_test.exe"
            comp_res = subprocess.run(
                [cc, "-std=gnu23", "-I", str(ROOT / "ui"), str(tp / "main.c"),
                 str(ROOT / "ui/tree.c"), str(ROOT / "ui/layout.c"), str(ROOT / "ui/panels.c"),
                 str(ROOT / "ui/transform.c"), str(ROOT / "ui/theme.c"),
                 str(ROOT / "ui/theme_data.c"), str(ROOT / "ui/template_data.c"),
                 "-o", str(exe)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(comp_res.returncode, 0, f"compile failed: {comp_res.stderr}")
            res = subprocess.run([str(exe)], stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
            self.assertEqual(res.returncode, 0, f"compiled StackPanel template failed rc={res.returncode}")

    def test_rounded_clips_and_group_opacity_100_to_300(self):
        sys.path.insert(0, str(ROOT / "reference"))
        import run as ref_run
        raw = subprocess.run([str(BUILD / "control-shots.exe"), "--clip-shots"], stdin=subprocess.DEVNULL, capture_output=True, check=True, timeout=60).stdout
        scales = [(1.0, 64), (1.5, 96), (2.0, 128), (3.0, 192)]
        off = 0
        clipped_probes = {}
        for sc, dim in scales:
            nbytes = dim * dim * 4
            clipped_probes[sc] = raw[off:off + nbytes]
            off += nbytes
        scenes = {}
        for sc, dim in scales:
            nbytes = dim * dim * 4
            scenes[sc] = raw[off:off + nbytes]
            off += nbytes
        winui_scenes = {}
        for sc, dim in scales:
            nbytes = dim * dim * 4
            winui_scenes[sc] = raw[off:off + nbytes]
            off += nbytes
        self.assertEqual(off, len(raw))

        # 1. Rounded clip of oversized white fill matches committed WinUI probe-r8 at 100%, 150%, 200% (within probe limit 40).
        _, ref_frames = ref_run.read_reference_snapshot(
            ROOT / "tests/reference", filenames=["probe-r8-s1.bgra", "probe-r8-s15.bgra", "probe-r8-s2.bgra"]
        )
        for sc, fname, dim in ((1.0, "probe-r8-s1.bgra", 64), (1.5, "probe-r8-s15.bgra", 96), (2.0, "probe-r8-s2.bgra", 128)):
            mine = clipped_probes[sc]
            real = ref_frames[fname]
            self.assertEqual(len(mine), len(real))
            worst = 0
            for idx in range(0, len(mine), 4):
                for c in range(3):
                    d = abs(mine[idx + c] - real[idx + c])
                    if d > worst:
                        worst = d
            self.assertLessEqual(worst, 40, f"rounded clip at scale {sc} exceeded probe limit: worst={worst}")
        # At 300% (192x192): outside clip (0,0) and rounded corner (24,24) are black, center (96,96) is white, symmetric.
        p300 = clipped_probes[3.0]
        self.assertEqual(p300[0:3], bytes([0, 0, 0]))
        self.assertEqual(p300[(24 * 192 + 24) * 4:(24 * 192 + 24) * 4 + 3], bytes([0, 0, 0]))
        self.assertEqual(p300[(96 * 192 + 96) * 4:(96 * 192 + 96) * 4 + 3], bytes([255, 255, 255]))

        # 2. Clip + group-opacity scene at all 4 scales (100%, 150%, 200%, 300%):
        #    outside clip stays #202020; overlap pixel (32*sc, 32*sc) is pure blue at 0.5 over #183048 (zero red bleed-through!).
        for sc, dim in scales:
            buf = scenes[sc]
            self.assertEqual(buf[0:3], bytes([0x20, 0x20, 0x20]), f"outside clip changed at {sc}")
            cx, cy = int(round(32 * sc)), int(round(32 * sc))
            b, g, r = buf[(cy * dim + cx) * 4:(cy * dim + cx) * 4 + 3]
            # Expected: 0x00183048 blended with blue (0,0,255) at alpha=128 -> R=(0x18*127+127)//255=12, G=(0x30*127+127)//255=24, B=128+(0x48*127+127)//255=164.
            self.assertEqual((r, g, b), (12, 24, 164), f"group opacity overlap wrong at scale {sc}: RGB=({r},{g},{b})")

    def test_manifest_validation_catches_faults(self):
        sys.path.insert(0, str(ROOT / "reference"))
        import run as ref_run
        folder = ROOT / "tests/reference"
        manifest, frames = ref_run.read_reference_snapshot(folder, filenames=["probe-r4-s1.bgra"])
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
        r4_s1 = frames["probe-r4-s1.bgra"]
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

    def test_buttons_match_real_winui(self):
        """Straight edges and fills may differ by rounding only; corners and probes have verified limits."""
        sys.path.insert(0, str(ROOT / "reference"))
        import run as ref_run
        folder = ROOT / "tests/reference"
        manifest, frames = ref_run.read_reference_snapshot(folder)
        accent_args = validate_manifest(manifest)
        cases = select_legacy_cases(manifest)
        ours = subprocess.run([str(BUILD / "control-shots.exe"), *accent_args], capture_output=True, timeout=60, check=True).stdout
        expected_bytes = sum(c["width"] * c["height"] * 4 for c in cases)
        self.assertEqual(len(ours), expected_bytes)
        offset = 0
        for case in cases:
            w, h = case["width"], case["height"]
            size = w * h * 4
            mine = ours[offset:offset + size]
            offset += size
            real = frames[case["file"]]
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

    @staticmethod
    def _exact_bezier(x1: float, y1: float, x2: float, y2: float, t: float) -> float:
        if t <= 0.0:
            return 0.0
        if t >= 1.0:
            return 1.0
        lo, hi = 0.0, 1.0
        for _ in range(80):
            u = 0.5 * (lo + hi)
            om = 1.0 - u
            bx = 3.0 * om * om * u * x1 + 3.0 * om * u * u * x2 + u * u * u
            if bx < t:
                lo = u
            else:
                hi = u
        u = 0.5 * (lo + hi)
        om = 1.0 - u
        return 3.0 * om * om * u * y1 + 3.0 * om * u * u * y2 + u * u * u

    def test_spline_and_track_match_exact_double_precision_bezier(self):
        out = subprocess.run(
            [str(BUILD / "anim-dump.exe")],
            check=True, stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60,
        ).stdout
        asc_count = desc_count = pvl_count = 0
        for line in out.strip().splitlines():
            parts = line.split()
            if parts[0] == "ASC":
                t_ms, got_s, got_v = int(parts[1]), int(parts[2]), int(parts[3])
                exact_p = self._exact_bezier(0.0, 0.0, 0.0, 1.0, t_ms / 83.0)
                want_s = round(exact_p * 65536.0)
                want_v = round((12.0 + 5.0 * exact_p) * 65536.0)
                self.assertLessEqual(abs(got_s - want_s), 1, f"S83 spline at {t_ms} ms: got={got_s} want={want_s}")
                self.assertLessEqual(abs(got_v - want_v), 1, f"ToggleSwitch 12->17 at {t_ms} ms: got={got_v} want={want_v}")
                asc_count += 1
            elif parts[0] == "DESC":
                t_ms, got_v = int(parts[1]), int(parts[2])
                exact_p = self._exact_bezier(0.0, 0.0, 0.0, 1.0, t_ms / 83.0)
                want_v = round((17.0 - 5.0 * exact_p) * 65536.0)
                self.assertLessEqual(abs(got_v - want_v), 1, f"ToggleSwitch 17->12 at {t_ms} ms: got={got_v} want={want_v}")
                desc_count += 1
            elif parts[0] == "PVL":
                t_ms, got_s = int(parts[1]), int(parts[2])
                exact_p = self._exact_bezier(6554 / 65536.0, 58982 / 65536.0, 13107 / 65536.0, 1.0, t_ms / 367.0)
                want_s = round(exact_p * 65536.0)
                self.assertLessEqual(abs(got_s - want_s), 1, f"PVL spline at {t_ms} ms: got={got_s} want={want_s}")
                pvl_count += 1
        self.assertEqual(asc_count, 14)
        self.assertEqual(desc_count, 14)
        self.assertEqual(pvl_count, 12)

    def test_dirty_rectangles_and_frame_timing(self):
        res = subprocess.run(
            [str(BUILD / "test-dirty.exe")],
            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("ALL PASSED", res.stdout)

    def test_transforms_pictures_and_input_match_exact_oracle(self):
        import math
        res = subprocess.run(
            [str(BUILD / "test-transform.exe")],
            stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=60,
        )
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("ALL PASSED", res.stdout)

        rot30_seen = False
        sin_count = 0
        tr_count = 0
        hit_count = 0
        pic_modes = {}
        for line in res.stdout.splitlines():
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "ROT30":
                gx, gy = int(parts[1]) / 65536.0, int(parts[2]) / 65536.0
                wx = 1920.0 * math.cos(math.radians(30.0))
                wy = 1920.0 * math.sin(math.radians(30.0))
                self.assertLessEqual(abs(gx - wx), 1.0 / 256.0, f"ROT30 x: got={gx} want={wx}")
                self.assertLessEqual(abs(gy - wy), 1.0 / 256.0, f"ROT30 y: got={gy} want={wy}")
                rot30_seen = True
            elif parts[0] == "SIN":
                deg_fx, got_q30 = int(parts[1]), int(parts[2])
                exact_q30 = math.sin(math.radians(deg_fx / 65536.0)) * (1 << 30)
                self.assertLess(abs(got_q30 - exact_q30), 2.0, f"SIN({deg_fx}/65536): got={got_q30} want={exact_q30}")
                sin_count += 1
            elif parts[0] == "HIT":
                rot, sc, tx, ty, px, py, got_hit, got_lx, got_ly = [int(v) for v in parts[1:]]
                rot_d = rot / 65536.0
                sc_d = sc / 65536.0
                tx_d, ty_d = tx / 65536.0, ty / 65536.0
                # Independent double-precision inverse of ht_box (40x30 at (20,15), origin 0.5,0.5, scale 1.5):
                # device -> logical (/1.5) -> subtract (20,15) -> subtract (20,15) origin -> inverse(scale->rot->trans) -> +origin
                dx = (px / 65536.0) / 1.5 - 20.0 - 20.0 - tx_d
                dy = (py / 65536.0) / 1.5 - 15.0 - 15.0 - ty_d
                rad = math.radians(-rot_d)
                c, s = math.cos(rad), math.sin(rad)
                lx = (dx * c - dy * s) / sc_d + 20.0
                ly = (dx * s + dy * c) / sc_d + 15.0
                # Skip points within 1e-4 of exact boundary to avoid float vs Q30 tie ambiguity.
                near_edge = min(abs(lx), abs(lx - 40.0), abs(ly), abs(ly - 30.0)) < 1e-3
                if not near_edge:
                    want_hit = 1 if (0.0 <= lx < 40.0 and 0.0 <= ly < 30.0) else 0
                    self.assertEqual(got_hit, want_hit, f"HIT#{hit_count}: got={got_hit} want={want_hit} local=({lx},{ly})")
                    if want_hit:
                        self.assertLessEqual(abs(got_lx / 65536.0 - lx), 1.0 / 256.0)
                        self.assertLessEqual(abs(got_ly / 65536.0 - ly), 1.0 / 256.0)
                hit_count += 1
            elif parts[0] == "TR":
                vals = [int(v) / 65536.0 for v in parts[1:]]
                cx, cy, sx, sy, skx, sky, rot, tx, ty, px, py, gx, gy = vals
                # WinUI CompositeTransform order: Translate(-c) -> Scale -> Skew -> Rotate -> Translate(+c) -> Translate(t)
                x = (px - cx) * sx
                y = (py - cy) * sy
                x_sk = x + y * math.tan(math.radians(skx))
                y_sk = x * math.tan(math.radians(sky)) + y
                rad = math.radians(rot)
                c, s = math.cos(rad), math.sin(rad)
                wx = (x_sk * c - y_sk * s) + cx + tx
                wy = (x_sk * s + y_sk * c) + cy + ty
                self.assertLessEqual(abs(gx - wx), 1.0 / 256.0, f"TR#{tr_count} x: got={gx} want={wx}")
                self.assertLessEqual(abs(gy - wy), 1.0 / 256.0, f"TR#{tr_count} y: got={gy} want={wy}")
                tr_count += 1
            elif parts[0] == "PIC":
                mode = int(parts[1])
                pic_modes[mode] = [int(h, 16) for h in parts[2:]]

        self.assertTrue(rot30_seen, "ROT30 oracle line missing")
        self.assertEqual(sin_count, 6, "expected 6 SIN precision oracle lines")
        self.assertEqual(tr_count, 1000, "expected 1000 TR oracle lines")
        self.assertEqual(hit_count, 1000, "expected 1000 HIT oracle lines")
        self.assertEqual(sorted(pic_modes.keys()), [0, 1, 2, 3], "expected all 4 picture stretch modes")

        # Independent double-precision bilinear premultiplied oracle for the 6x4 translucent image at 150% DPI.
        sw, sh = 6, 4
        cw, ch = 18, 14
        dl, dt, dr, db = 2.0, 2.0, 16.0, 12.0
        dw, dh = dr - dl, db - dt
        scale = 1.5
        sentinel = 0x00192837
        sr, sg, sb = (sentinel >> 16) & 255, (sentinel >> 8) & 255, sentinel & 255

        src = []
        for y in range(sh):
            row = []
            for x in range(sw):
                b = 20 + x * 35 + y * 15
                g = 40 + x * 18 + y * 35
                r = 180 - x * 20 + y * 20
                a = 110 + x * 18 + y * 12
                row.append((a, r * a / 255.0, g * a / 255.0, b * a / 255.0))
            src.append(row)

        for mode in range(4):
            if mode == 0:
                mw, mh = sw * scale, sh * scale
            elif mode == 1:
                mw, mh = dw, dh
            elif mode == 2:
                k = min(dw / sw, dh / sh)
                mw, mh = sw * k, sh * k
            else:
                k = max(dw / sw, dh / sh)
                mw, mh = sw * k, sh * k
            ml = dl + (dw - mw) / 2.0
            mt = dt + (dh - mh) / 2.0
            cl, ct = max(dl, ml), max(dt, mt)
            cr, cb = min(dr, ml + mw), min(db, mt + mh)
            got = pic_modes[mode]
            self.assertEqual(len(got), cw * ch)
            for y in range(ch):
                for x in range(cw):
                    px_c, py_c = x + 0.5, y + 0.5
                    gval = got[y * cw + x]
                    if not (cl <= px_c < cr and ct <= py_c < cb):
                        self.assertEqual(gval, sentinel, f"mode {mode} outside sentinel modified at ({x},{y})")
                        continue
                    u = (px_c - ml) * sw / mw - 0.5
                    v = (py_c - mt) * sh / mh - 0.5
                    u = max(0.0, min(sw - 1.0, u))
                    v = max(0.0, min(sh - 1.0, v))
                    x0, y0 = int(math.floor(u)), int(math.floor(v))
                    x1, y1 = min(sw - 1, x0 + 1), min(sh - 1, y0 + 1)
                    fx, fy = u - x0, v - y0
                    sa = sr_p = sg_p = sb_p = 0.0
                    for iy, wy in ((y0, 1.0 - fy), (y1, fy)):
                        for ix, wx in ((x0, 1.0 - fx), (x1, fx)):
                            w = wx * wy
                            pa, pr, pg, pb = src[iy][ix]
                            sa += pa * w
                            sr_p += pr * w
                            sg_p += pg * w
                            sb_p += pb * w
                    inv_a = (255.0 - sa) / 255.0
                    wr = min(255.0, sr_p + sr * inv_a)
                    wg = min(255.0, sg_p + sg * inv_a)
                    wb = min(255.0, sb_p + sb * inv_a)
                    gr, gg, gb = (gval >> 16) & 255, (gval >> 8) & 255, gval & 255
                    self.assertLessEqual(abs(gr - wr), 2.0, f"mode {mode} R at ({x},{y}): got={gr} want={wr}")
                    self.assertLessEqual(abs(gg - wg), 2.0, f"mode {mode} G at ({x},{y}): got={gg} want={wg}")
                    self.assertLessEqual(abs(gb - wb), 2.0, f"mode {mode} B at ({x},{y}): got={gb} want={wb}")

    def test_paths_arcs_and_gradients_match_oracles(self):
        import math
        exe = BUILD / "test-path.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("test_path: ALL PASSED", res.stdout)

        # Independent Python 16-subscanline analytic horizontal interval oracle + converged curve flattening
        # for ALL 1,000 random cubic and quadratic paths (500 EvenOdd, 500 NonZero), tolerance <= 4/255.
        def flatten_poly(kind, pts, steps=64):
            poly = [(pts[0][0], pts[0][1])]
            if kind == 3:
                triples = [(pts[0], pts[1], pts[2], pts[3]), (pts[3], pts[4], pts[5], pts[6]), (pts[6], pts[7], pts[8], pts[0])]
                for p0, p1, p2, p3 in triples:
                    for i in range(1, steps + 1):
                        t = i / steps
                        mt = 1.0 - t
                        x = mt**3 * p0[0] + 3 * mt**2 * t * p1[0] + 3 * mt * t**2 * p2[0] + t**3 * p3[0]
                        y = mt**3 * p0[1] + 3 * mt**2 * t * p1[1] + 3 * mt * t**2 * p2[1] + t**3 * p3[1]
                        poly.append((x, y))
            else:
                pairs = [(pts[0], pts[1], pts[2]), (pts[2], pts[3], pts[4]), (pts[4], pts[5], pts[0])]
                for p0, p1, p2 in pairs:
                    for i in range(1, steps + 1):
                        t = i / steps
                        mt = 1.0 - t
                        x = mt**2 * p0[0] + 2 * mt * t * p1[0] + t**2 * p2[0]
                        y = mt**2 * p0[1] + 2 * mt * t * p1[1] + t**2 * p2[1]
                        poly.append((x, y))
            return poly

        def eval_rows(poly, rule, rows):
            edges = []
            n = len(poly)
            for i in range(n):
                x0, y0 = poly[i]
                x1, y1 = poly[(i + 1) % n]
                if y0 == y1:
                    continue
                if y0 < y1:
                    edges.append((x0, y0, x1, y1, 1))
                else:
                    edges.append((x1, y1, x0, y0, -1))
            out = {}
            for y in rows:
                cov = [0.0] * 16
                for s in range(16):
                    sy = y + (2 * s + 1) / 32.0
                    xs = []
                    for x0, y0, x1, y1, d in edges:
                        if y0 <= sy < y1:
                            xs.append((x0 + (x1 - x0) * (sy - y0) / (y1 - y0), d))
                    if len(xs) < 2:
                        continue
                    xs.sort(key=lambda t: t[0])
                    w = 0
                    inside = False
                    sx = 0.0
                    for ix, d in xs:
                        w += d
                        now_in = (w != 0) if rule == 1 else ((w & 1) != 0)
                        if not inside and now_in:
                            sx = ix
                            inside = True
                        elif inside and not now_in:
                            sl = max(0.0, sx)
                            sr = min(16.0, ix)
                            inside = False
                            if sr > sl:
                                px0 = int(math.floor(sl))
                                px1 = min(15, int(math.floor(sr - 1e-12)))
                                for px in range(px0, px1 + 1):
                                    ol = max(sl, float(px))
                                    or_ = min(sr, float(px + 1))
                                    if or_ > ol:
                                        cov[px] += (or_ - ol)
                out[y] = [int(round(255.0 * min(1.0, c / 16.0))) for c in cov]
            return out

        def sample_stops_oracle(stops, spread, t):
            if spread == 0:
                t = max(0.0, min(1.0, t))
            elif spread == 2:
                t = t - math.floor(t)
            else:
                t = t % 2.0
                if t > 1.0:
                    t = 2.0 - t
            for i in range(len(stops) - 1):
                o0, c0 = stops[i]
                o1, c1 = stops[i + 1]
                if t <= o1 or i == len(stops) - 2:
                    w = 0.0 if o1 == o0 else max(0.0, min(1.0, (t - o0) / (o1 - o0)))
                    a0, r0, g0, b0 = (c0 >> 24) & 255, (c0 >> 16) & 255, (c0 >> 8) & 255, c0 & 255
                    a1, r1, g1, b1 = (c1 >> 24) & 255, (c1 >> 16) & 255, (c1 >> 8) & 255, c1 & 255
                    a = a0 * (1.0 - w) + a1 * w
                    if a < 0.5:
                        return (0, 0, 0, 0)
                    r = (r0 * a0 * (1.0 - w) + r1 * a1 * w) / a
                    g = (g0 * a0 * (1.0 - w) + g1 * a1 * w) / a
                    b = (b0 * a0 * (1.0 - w) + b1 * a1 * w) / a
                    return (int(round(a)), int(round(r)), int(round(g)), int(round(b)))
            return (0, 0, 0, 0)

        rpath_count = 0
        glin_count = 0
        grad_count = 0
        probe = [34, 53, 88, 119, 136, 170, 201, 220]
        probe_rows = sorted({p // 16 for p in probe})
        stops3 = [(0.0, 0xFFFF1020), (0.5, 0x8020E040), (1.0, 0xFF1040F0)]

        for line in res.stdout.splitlines():
            if line.startswith("RPATH "):
                left, right = line.split(" : ")
                toks = left.split()
                rule = int(toks[1])
                kind = int(toks[2])
                npts = int(toks[3])
                pts = [(int(toks[4 + 2 * k]) / 65536.0, int(toks[5 + 2 * k]) / 65536.0) for k in range(npts)]
                got = [int(v) for v in right.split()]
                self.assertEqual(len(got), 256, f"RPATH #{rpath_count} must emit all 256 pixels")
                poly = flatten_poly(kind, pts, steps=64)
                want_rows = eval_rows(poly, rule, range(16))
                if rpath_count < 20:
                    # Verify reference oracle convergence (64 vs 128 steps agree within 1/255).
                    poly128 = flatten_poly(kind, pts, steps=128)
                    want128 = eval_rows(poly128, rule, range(16))
                    for y in range(16):
                        for x in range(16):
                            self.assertLessEqual(abs(want_rows[y][x] - want128[y][x]), 1,
                                                 f"oracle not converged at RPATH #{rpath_count} ({x},{y})")
                for y in range(16):
                    for x in range(16):
                        g = got[y * 16 + x]
                        w = want_rows[y][x]
                        self.assertLessEqual(abs(g - w), 4, f"RPATH #{rpath_count} ({x},{y}): got={g} want={w}")
                rpath_count += 1
            elif line.startswith("GLIN "):
                _, sx, sy, scol = line.split()
                x, y, col = float(sx), float(sy), int(scol) & 0xFFFFFFFF
                # box [10,90] x [20,60], start=(10,20), end=(90,60), Reflect
                vx, vy = 80.0, 40.0
                t = ((x - 10.0) * vx + (y - 20.0) * vy) / (vx * vx + vy * vy)
                wa, wr, wg, wb = sample_stops_oracle(stops3, 1, t)
                ga, gr, gg, gb = (col >> 24) & 255, (col >> 16) & 255, (col >> 8) & 255, col & 255
                for ch, g, w in (("A", ga, wa), ("R", gr, wr), ("G", gg, wg), ("B", gb, wb)):
                    self.assertLessEqual(abs(g - w), 2, f"GLIN ({x},{y}) {ch}: got={g} want={w}")
                glin_count += 1
            elif line.startswith("GRAD "):
                _, sx, sy, scol = line.split()
                x, y, col = float(sx), float(sy), int(scol) & 0xFFFFFFFF
                # box [10,90] x [20,60], center=(0.35,0.40), origin=(0.50,0.50), rx=0.55, ry=0.60, Repeat
                cx, cy = 10.0 + 0.35 * 80.0, 20.0 + 0.40 * 40.0
                ox, oy = 10.0 + 0.50 * 80.0, 20.0 + 0.50 * 40.0
                rx, ry = 0.55 * 80.0, 0.60 * 40.0
                px, py = (x - cx) / rx, (y - cy) / ry
                fx, fy = (ox - cx) / rx, (oy - cy) / ry
                dx, dy = px - fx, py - fy
                f2 = fx * fx + fy * fy
                fd = fx * dx + fy * dy
                d2 = dx * dx + dy * dy
                t = (math.sqrt(fd * fd + d2 * (1.0 - f2)) + fd) / (1.0 - f2)
                wa, wr, wg, wb = sample_stops_oracle(stops3, 2, t)
                ga, gr, gg, gb = (col >> 24) & 255, (col >> 16) & 255, (col >> 8) & 255, col & 255
                for ch, g, w in (("A", ga, wa), ("R", gr, wr), ("G", gg, wg), ("B", gb, wb)):
                    self.assertLessEqual(abs(g - w), 2, f"GRAD ({x},{y}) {ch}: got={g} want={w}")
                grad_count += 1

        self.assertEqual(rpath_count, 1000)
        self.assertEqual(glin_count, 20)
        self.assertEqual(grad_count, 20)

    def test_path_markup_compiler_corpus_and_refusals(self):
        import xml.etree.ElementTree as ET
        sys.path.insert(0, str(ROOT / "tools" / "winui"))
        import templates as tpl_mod

        # 1. Exact decoded-command fixtures: implicit L after M, relative after Z, S/T reflection & reset,
        # adjacent 1-char arc flags, unsigned radii, scientific notation, glued signs.
        p_ml = tpl_mod.parse_path_markup("F1 M 10,20 30,40 l 5-5 z l 1,2", "fx.xaml", 10)
        self.assertEqual(p_ml["fill_rule"], 1)
        self.assertEqual([s["cmd"] for s in p_ml["segs"]], [0, 1, 1, 5, 1])
        # After z, current point returned to subpath start (10, 20), so l 1,2 lands at (11, 22).
        self.assertEqual((p_ml["segs"][4]["x1"], p_ml["segs"][4]["y1"]), (11 * 65536, 22 * 65536))

        p_st = tpl_mod.parse_path_markup("M0,0 C1,2 3,4 5,6 S7,8 9,10 L12,12 S14,14 16,16 Q18,20 22,20 T26,20", "fx.xaml", 11)
        # First S reflects (3,4) across (5,6) -> (7,8); second S after L resets c1 to cur (12,12).
        self.assertEqual((p_st["segs"][2]["x1"], p_st["segs"][2]["y1"]), (7 * 65536, 8 * 65536))
        self.assertEqual((p_st["segs"][4]["x1"], p_st["segs"][4]["y1"]), (12 * 65536, 12 * 65536))
        # T reflects Q's control (18,20) across (22,20) -> (26,20).
        self.assertEqual((p_st["segs"][6]["x1"], p_st["segs"][6]["y1"]), (26 * 65536, 20 * 65536))

        p_arc = tpl_mod.parse_path_markup("M0,0 a10,5.5e0 30 1110,10", "fx.xaml", 12)
        self.assertEqual(p_arc["segs"][1]["cmd"], 4)
        self.assertEqual((p_arc["segs"][1]["x1"], p_arc["segs"][1]["y1"]), (10 * 65536, int(5.5 * 65536)))
        self.assertEqual((p_arc["segs"][1]["large_arc"], p_arc["segs"][1]["sweep"]), (True, True))
        self.assertEqual((p_arc["segs"][1]["x2"], p_arc["segs"][1]["y2"]), (10 * 65536, 10 * 65536))

        # Literal hex colors on Fill and Stroke must compile to IM_TVAL_COLOR (not IM_TVAL_ENUM_STR).
        hex_comp = tpl_mod.Compiler()
        for prop_n in ("Fill", "Stroke"):
            cv = hex_comp.parse_value("#FF112233", prop_n, "s.xaml", 7)
            self.assertEqual(cv["kind"], "IM_TVAL_COLOR")
            self.assertEqual(cv["a"], 0xFF112233)

        # 2. Malformed path markup refusals with exact file:line (including signed arc radii and 1e999 overflow).
        bad_cases = [
            ("", "empty"),
            ("M0,0 F1 L1,1", "misplaced fill rule"),
            ("M0,0 L1", "incomplete tuple"),
            ("M0,0 A10,10 0 2 1 20,20", "invalid arc flag"),
            ("M0,0 A-5 5 0 0 1 10,0", "negative arc radius refused by WinUI"),
            ("M0,0 A+5 5 0 0 1 10,0", "plus-signed arc radius refused by WinUI"),
            ("M1e999,0", "overflowing scientific notation"),
            ("M0,0 X1,1", "unknown command"),
            ("L10,10", "before initial MoveTo"),
            ("M30000,0 l5000,0", "overflow after relative addition"),
        ]
        for bad, desc in bad_cases:
            with self.assertRaises(tpl_mod.TemplateError, msg=desc) as ctx:
                tpl_mod.parse_path_markup(bad, "bad_path.xaml", 42)
            self.assertIn("bad_path.xaml:42:", str(ctx.exception), desc)

        # 3. Independent scan of pin-filtered WinUI XAML corpus (pin.json base + controls minus skip):
        # verify all 9 path geometry occurrences (7 <Path Data="..."> + 2 <Setter Target="Path.Data" Value="...">)
        # compile, and verify Microsoft checkmark and caption glyph coordinates are NOT emitted into ui/template_data.c.
        pin = json.loads((ROOT / "tools" / "winui" / "pin.json").read_text(encoding="utf-8"))
        winui = Path(os.environ.get("IMPOSSIBLE_WINUI", Path.home() / ".cache" / "impossible-sdk" / "winui"))
        if winui.is_dir():
            files = [winui / pin["base"]] + [
                p for p in sorted(winui.glob(pin["controls"]))
                if not any(sk in p.name for sk in pin.get("skip", []))
            ]
            compiler = tpl_mod.Compiler()
            literals = 0
            bindings = 0
            setter_literals = 0
            for xf in files:
                root_el = ET.fromstring(xf.read_text(encoding="utf-8-sig"))
                parent_map = {c: p for p in root_el.iter() for c in p}
                for el in root_el.iter():
                    tag = el.tag.rsplit("}", 1)[-1]
                    if tag == "Path" and "Data" in el.attrib:
                        val = el.attrib["Data"]
                        res_v = compiler.parse_value(val, "Data", str(xf.name), 1)
                        if val.strip().startswith("{"):
                            self.assertEqual(res_v["kind"], "IM_TVAL_TEMPLATE_BINDING")
                            bindings += 1
                        else:
                            self.assertIn(res_v["kind"], ("IM_TVAL_PATH", "IM_TVAL_ICON"))
                            literals += 1
                    elif tag == "Setter" and el.attrib.get("Target", "").endswith(".Data") and "Value" in el.attrib:
                        val = el.attrib["Value"]
                        res_v = compiler.parse_value(val, "Data", str(xf.name), 1)
                        self.assertEqual(res_v["kind"], "IM_TVAL_ICON")
                        # Walk up to owning VisualState and assert exact state -> icon mapping.
                        cur = el
                        state_name = None
                        while cur is not None:
                            if cur.tag.rsplit("}", 1)[-1] == "VisualState":
                                state_name = cur.attrib.get("{http://schemas.microsoft.com/winfx/2006/xaml}Name") or cur.attrib.get("Name")
                                break
                            cur = parent_map.get(cur)
                        if state_name == "WindowStateNormal":
                            self.assertEqual(res_v["text"], "IM_ICON_WINDOW_MAXIMIZE")
                        elif state_name == "WindowStateMaximized":
                            self.assertEqual(res_v["text"], "IM_ICON_WINDOW_RESTORE")
                        else:
                            self.fail(f"unexpected VisualState {state_name!r} for Target=Path.Data")
                        setter_literals += 1
            self.assertEqual(literals, 6, "pin-filtered corpus must have 6 literal Path.Data elements")
            self.assertEqual(bindings, 1, "pin-filtered corpus must have 1 bound Path.Data element")
            self.assertEqual(setter_literals, 2, "pin-filtered corpus must have 2 Target=Path.Data visual-state setters")
            # Production compile_checkout validator must visit all 9 occurrences directly.
            self.assertEqual(tpl_mod.validate_corpus_paths(winui), 9)

            # Verify validate_corpus_paths refuses a malformed out-of-target setter with exact file:line.
            with tempfile.TemporaryDirectory() as td_c:
                td_root = Path(td_c)
                base_p = td_root / pin["base"]
                base_p.parent.mkdir(parents=True, exist_ok=True)
                base_p.write_text(
                    '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"'
                    ' xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n'
                    '  <Style TargetType="Button">\n'
                    '    <VisualState x:Name="Bad"><VisualState.Setters>\n'
                    '      <Setter Target="Path.Data" Value="M0,0 C1,2" />\n'
                    '    </VisualState.Setters></VisualState>\n'
                    '  </Style>\n'
                    '</ResourceDictionary>\n',
                    encoding="utf-8",
                )
                with self.assertRaises(tpl_mod.TemplateError) as ctx:
                    tpl_mod.validate_corpus_paths(td_root)
                self.assertIn("generic.xaml:4:", str(ctx.exception))

        # Verify Microsoft checkmark & caption literal coordinates are never emitted in ui/template_data.c.
        tpl_c = (ROOT / "ui" / "template_data.c").read_text(encoding="utf-8")
        self.assertNotIn("123 * 65536", tpl_c)
        self.assertNotIn("8060928", tpl_c)  # 123 * 65536
        self.assertNotIn("488309", tpl_c)   # 7.451 * 65536 (caption glyph)

        # 4. Malformed Path literal in a standalone style (both direct attribute and visual-state Setter)
        # outside TARGET_STYLES is refused with file:line.
        with tempfile.TemporaryDirectory() as td:
            bad_xaml = Path(td) / "extra_style.xaml"
            bad_xaml.write_text(
                '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"'
                ' xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n'
                '  <Style x:Key="ExtraStyle" TargetType="Button">\n'
                '    <Setter Property="Template">\n'
                '      <Setter.Value>\n'
                '        <ControlTemplate TargetType="Button">\n'
                '          <Grid>\n'
                '            <Path Data="M0,0 Q10" />\n'
                '          </Grid>\n'
                '        </ControlTemplate>\n'
                '      </Setter.Value>\n'
                '    </Setter>\n'
                '  </Style>\n'
                '</ResourceDictionary>\n',
                encoding="utf-8",
            )
            p_bad = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "winui" / "templates.py"), "--parse-file", str(bad_xaml)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(p_bad.returncode, 0)
            self.assertIn("extra_style.xaml:7:", p_bad.stderr + p_bad.stdout)

            bad_setter = Path(td) / "bad_setter.xaml"
            bad_setter.write_text(
                '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"'
                ' xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n'
                '  <Style x:Key="BadSetterStyle" TargetType="Button">\n'
                '    <Setter Property="Template">\n'
                '      <Setter.Value>\n'
                '        <ControlTemplate TargetType="Button">\n'
                '          <Grid>\n'
                '            <VisualStateManager.VisualStateGroups>\n'
                '              <VisualStateGroup x:Name="G">\n'
                '                <VisualState x:Name="S">\n'
                '                  <VisualState.Setters>\n'
                '                    <Setter Target="P.Data" Value="M0,0 Q10" />\n'
                '                  </VisualState.Setters>\n'
                '                </VisualState>\n'
                '              </VisualStateGroup>\n'
                '            </VisualStateManager.VisualStateGroups>\n'
                '            <Path x:Name="P" Data="M0,0 L1,1" />\n'
                '          </Grid>\n'
                '        </ControlTemplate>\n'
                '      </Setter.Value>\n'
                '    </Setter>\n'
                '  </Style>\n'
                '</ResourceDictionary>\n',
                encoding="utf-8",
            )
            p_bs = subprocess.run(
                [sys.executable, str(ROOT / "tools" / "winui" / "templates.py"), "--parse-file", str(bad_setter)],
                capture_output=True, text=True,
            )
            self.assertNotEqual(p_bs.returncode, 0)
            self.assertIn("bad_setter.xaml:11:", p_bs.stderr + p_bs.stdout)

            # 5. End-to-end compiler-to-pixels proof: compile real XAML <Path> templates through
            # templates.py emit(), compile the generated C tables with llvm-mingw against libImpossibleUI.a,
            # execute the resulting Windows binary, and verify rendered pixels directly.
            e2e_xaml = (
                '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"'
                ' xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n'
                '  <Style x:Key="E2EPathStyle" TargetType="Button">\n'
                '    <Setter Property="Template">\n'
                '      <Setter.Value>\n'
                '        <ControlTemplate TargetType="Button">\n'
                '          <Grid>\n'
                '            <Path x:Name="CheckGlyph" Width="16" Height="16" Stretch="Fill" Fill="#FFFFFFFF"'
                ' Data="M0,123 L39,93 L124,164 L256,18 L295,49 L124,240 z" />\n'
                '            <Path Width="16" Height="16" Fill="#FF112233" Data="M2,2 L14,2 L14,14 L2,14 Z" />\n'
                '            <Path Width="16" Height="16" Stroke="#FFFF0000" Data="M2,2 L14,2 L14,14 L2,14 Z" />\n'
                '          </Grid>\n'
                '        </ControlTemplate>\n'
                '      </Setter.Value>\n'
                '    </Setter>\n'
                '  </Style>\n'
                '</ResourceDictionary>\n'
            )
            doc = tpl_mod.parse_xaml(e2e_xaml, "e2e_path.xaml")
            comp = tpl_mod.Compiler()
            comp.compile_style(doc.children[0], enum_id="IM_TPL_E2E_PATH")
            emitted = tpl_mod.emit(comp)
            self.assertIn("im_template_path_segs", emitted["template_data.c"])

            cc = None
            cache = BUILD / "CMakeCache.txt"
            if cache.exists():
                for ln in cache.read_text(encoding="utf-8", errors="replace").splitlines():
                    if ln.startswith("CMAKE_C_COMPILER:FILEPATH="):
                        cc = ln.split("=", 1)[1].strip()
                        break
            if cc and Path(cc).exists() and (BUILD / "libImpossibleUI.a").exists():
                gen_c = emitted["template_data.c"].replace('#include "template.h"', "").replace(
                    "im_templates[IM_TEMPLATE_COUNT]", "gen_templates[1]"
                )
                for sym in (
                    "im_template_setters", "im_template_elements", "im_template_props",
                    "im_template_groups", "im_template_states", "im_template_transitions",
                    "im_template_anims", "im_template_keyframes", "im_template_path_segs",
                ):
                    gen_c = gen_c.replace(sym, "gen_" + sym[len("im_"):])
                driver_c = (
                    '#include <windows.h>\n'
                    '#include "layout.h"\n#include "path.h"\n#include "states.h"\n#include "tree.h"\n'
                    + gen_c +
                    '\nstatic void *a_alloc(void *c, size_t n){return HeapAlloc((HANDLE)c,0,n?n:1);}\n'
                    'static void *a_realloc(void *c, void *p, size_t o, size_t n){(void)o;return p?HeapReAlloc((HANDLE)c,0,p,n?n:1):HeapAlloc((HANDLE)c,0,n?n:1);}\n'
                    'static void a_free(void *c, void *p, size_t o){(void)o;if(p)HeapFree((HANDLE)c,0,p);}\n'
                    'int mainCRTStartup(void){\n'
                    '  im_allocator al={a_alloc,a_realloc,a_free,GetProcessHeap()};\n'
                    '  im_palette pal; for(size_t i=0;i<sizeof(pal);i++)((uint8_t*)&pal)[i]=0;\n'
                    '  im_tree tr; im_tree_init(&tr, al);\n'
                    '  im_template_tables tbl={gen_template_setters,gen_template_elements,gen_template_props,gen_template_groups,gen_template_states,gen_template_transitions,gen_template_anims,gen_template_keyframes,gen_template_path_segs};\n'
                    '  im_node *root=im_tree_instantiate_custom_template(&tr,&pal,&gen_templates[0],gen_template_setters,gen_template_elements,gen_template_props,"root");\n'
                    '  if(!root) ExitProcess(1);\n'
                    '  im_state_machine sm; im_states_init_custom(&sm,&gen_templates[0],&tbl,&pal);\n'
                    '  im_states_apply_to_tree(&sm,root,0);\n'
                    '  im_layout_run(&tr,root,16*IM_ONE,16*IM_ONE,IM_ONE);\n'
                    '  im_node *g=root->children[0];\n'
                    '  uint32_t pix[256]; im_surface s={pix,16,16,16,pal,IM_ONE,NULL,NULL};\n'
                    '  for(int i=0;i<256;i++) pix[i]=0;\n'
                    '  if(!im_states_draw_path_node(&sm,g->children[0],&s,al,NULL,0)) ExitProcess(2);\n'
                    '  int chk=0; for(int i=0;i<256;i++) if(pix[i]) chk++;\n'
                    '  if(chk<=20) ExitProcess(3);\n'
                    '  for(int i=0;i<256;i++) pix[i]=0;\n'
                    '  if(!im_states_draw_path_node(&sm,g->children[1],&s,al,NULL,0)) ExitProcess(4);\n'
                    '  if(pix[8*16+8]!=0x00112233u) ExitProcess(5);\n'
                    '  for(int i=0;i<256;i++) pix[i]=0;\n'
                    '  if(!im_states_draw_path_node(&sm,g->children[2],&s,al,NULL,0)) ExitProcess(6);\n'
                    '  int red=0; for(int i=0;i<256;i++) if((pix[i]>>16)>100u && (pix[i]&0xFFu)==0u) red++;\n'
                    '  if(red<=20) ExitProcess(7);\n'
                    '  im_tree_destroy_subtree(&tr,root);\n'
                    '  ExitProcess(0);\n'
                    '}\n'
                )
                src_p = Path(td) / "e2e_path.c"
                exe_p = Path(td) / "e2e_path.exe"
                src_p.write_text(driver_c, encoding="utf-8")
                c_res = subprocess.run(
                    [cc, "-std=gnu23", "-O2", "-ffreestanding", "-fno-stack-protector",
                     "-I", str(ROOT / "ui"), "-I", str(ROOT / "include"),
                     "-I", str(ROOT / "parts" / "image"), "-I", str(ROOT / "parts" / "unicode"),
                     str(src_p), str(BUILD / "libImpossibleUI.a"),
                     "-nostdlib", "-Wl,-emainCRTStartup", "-lkernel32", "-o", str(exe_p)],
                    capture_output=True, text=True,
                )
                self.assertEqual(c_res.returncode, 0, c_res.stdout + c_res.stderr)
                r_res = subprocess.run([str(exe_p)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
                self.assertEqual(r_res.returncode, 0, f"e2e compiled Path driver exited {r_res.returncode}")

    def test_remaining_panels_oracles_and_rtl(self):
        exe = BUILD / "test-panels.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("test_panels: ALL PASSED", res.stdout)

        def layout_round(val_fx, scale_fx):
            if scale_fx == 65536:
                return ((val_fx + 32768) >> 16) * 65536
            px = (val_fx * scale_fx + (1 << 31)) >> 32
            num = px * 65536 * 65536
            half = scale_fx // 2
            return (num + half) // scale_fx if num >= 0 else -((-num + half) // scale_fx)

        # 1. Independent UniformGrid 5-scale oracle (2x3, FirstColumn=1, RowSpacing=4, ColSpacing=6, Pad=2, 120x60).
        ugrid_scales = 0
        for line in res.stdout.splitlines():
            if line.startswith("UGRID "):
                toks = [int(x) for x in line.split()[1:]]
                sc = toks[0]
                ugrid_scales += 1
                pad = 2 * 65536
                inner_w = 120 * 65536 - 2 * pad
                inner_h = 60 * 65536 - 2 * pad
                cell_w = (inner_w - 2 * 6 * 65536) // 3
                cell_h = (inner_h - 4 * 65536) // 2
                for k in range(6):
                    gl, gt, gr, gb = toks[1 + 4 * k: 5 + 4 * k]
                    slot = 1 + k
                    if slot >= 6:
                        self.assertEqual((gl, gt, gr, gb), (0, 0, 0, 0))
                    else:
                        r, c = divmod(slot, 3)
                        ex = layout_round(pad + c * (cell_w + 6 * 65536), sc)
                        ey = layout_round(pad + r * (cell_h + 4 * 65536), sc)
                        ew = layout_round(cell_w, sc)
                        eh = layout_round(cell_h, sc)
                        self.assertEqual((gl, gt, gr - gl, gb - gt), (ex, ey, ew, eh), f"UGRID sc={sc} k={k}")
        self.assertEqual(ugrid_scales, 5)

        # 2. Independent WrapPanel oracle for ALL 200 random children (Horizontal, ItemSpacing=4, LineSpacing=6, Pad=(5,3,5,3), W=180).
        wrap_items = []
        for line in res.stdout.splitlines():
            if line.startswith("WRAP200 "):
                toks = [int(x) for x in line.split()[1:]]
                wrap_items.append(toks)
        self.assertEqual(len(wrap_items), 200)
        pad_l, pad_t, pad_r = 5 * 65536, 3 * 65536, 5 * 65536
        sp_u, sp_v, max_u = 4 * 65536, 6 * 65536, 180 * 65536
        pos_u, pos_v, row_v = pad_l, pad_t, 0
        row_items = []
        expected_wrap = {}
        for idx, vis, w, h, gl, gt, gr, gb in wrap_items:
            if vis != 0:
                expected_wrap[idx] = (0, 0, 0, 0)
                continue
            if row_items and pos_u + w + pad_r > max_u:
                au = pad_l
                for ridx, rw in row_items:
                    expected_wrap[ridx] = (au, pos_v, au + rw, pos_v + row_v)
                    au += rw + sp_u
                pos_u = pad_l
                pos_v += row_v + sp_v
                row_v = 0
                row_items = []
            if h > row_v:
                row_v = h
            row_items.append((idx, w))
            pos_u += w + sp_u
        if row_items:
            au = pad_l
            for ridx, rw in row_items:
                expected_wrap[ridx] = (au, pos_v, au + rw, pos_v + row_v)
                au += rw + sp_u
        for idx, vis, w, h, gl, gt, gr, gb in wrap_items:
            ex_l, ex_t, ex_r, ex_b = expected_wrap[idx]
            if vis != 0:
                self.assertEqual((gl, gt, gr, gb), (0, 0, 0, 0))
            else:
                # Child has Alignment=Start so its arranged width/height is its desired (w, h) inside the row slot.
                self.assertEqual((gl, gt, gr - gl, gb - gt), (ex_l, ex_t, w, h), f"WRAP200 idx={idx}")

        # 3. Independent VariableSizedWrapGrid occupancy-map oracle for ALL 200 random span lists.
        vswg_count = 0
        for line in res.stdout.splitlines():
            if line.startswith("VSWG200 "):
                left_s, right_s = line.split(" : ")
                lt = [int(x) for x in left_s.split()[1:]]
                rt = [int(x) for x in right_s.split()]
                _li, ori, max_rc, cnt = lt[0], lt[1], lt[2], lt[3]
                spans = [(lt[4 + 2 * k], lt[5 + 2 * k]) for k in range(cnt)]
                horiz = (ori == 1)
                iw, ih = 20 * 65536, 15 * 65536
                main_cell = iw if horiz else ih
                cross_cell = ih if horiz else iw
                sum_is = sum(max(1, cs if horiz else rs) for rs, cs in spans)
                sum_ls = sum(max(1, rs if horiz else cs) for rs, cs in spans)
                items_per_line = min(sum_is, max(1, (200 * 65536) // main_cell), max_rc)
                max_lines = min(sum_ls, max(1, (200 * 65536) // cross_cell))
                occ = set()
                cur_line, cur_item = 0, 0
                full = False
                for k, (rs, cs) in enumerate(spans):
                    raw_rs = rs if rs > 0 else 1
                    raw_cs = cs if cs > 0 else 1
                    raw_ispan = raw_cs if horiz else raw_rs
                    raw_lspan = raw_rs if horiz else raw_cs
                    ispan = min(raw_ispan, items_per_line)
                    lspan = min(raw_lspan, max_lines)
                    placed = False
                    if not full:
                        while cur_line + lspan <= max_lines:
                            if cur_item + ispan > items_per_line:
                                cur_line += 1
                                cur_item = 0
                                continue
                            if all((cur_line + dl, cur_item + di) not in occ for dl in range(lspan) for di in range(ispan)):
                                placed = True
                                break
                            cur_item += 1
                        if not placed:
                            full = True
                    got = tuple(rt[4 * k: 4 * k + 4])
                    if not placed:
                        self.assertEqual(got, (0, 0, 0, 0), f"VSWG200 overflow list={_li} k={k}")
                        continue
                    for dl in range(lspan):
                        for di in range(ispan):
                            occ.add((cur_line + dl, cur_item + di))
                    draw_ispan = ispan
                    if cur_item == 0 and raw_ispan > items_per_line:
                        max_draw = (200 * 65536) // main_cell
                        if max_draw > draw_ispan:
                            draw_ispan = min(raw_ispan, max_draw)
                    col = cur_item if horiz else cur_line
                    row = cur_line if horiz else cur_item
                    cw = (draw_ispan if horiz else lspan) * iw
                    ch = (lspan if horiz else draw_ispan) * ih
                    ex = (col * iw, row * ih, col * iw + cw, row * ih + ch)
                    self.assertEqual(got, ex, f"VSWG200 list={_li} k={k}")
                    cur_item += ispan
                vswg_count += 1
        self.assertEqual(vswg_count, 200)

        # 4. Template compiler accepts all new panel elements/properties AND instantiating/running the compiled C template works!
        sys.path.insert(0, str(ROOT / "tools" / "winui"))
        import templates as tpl_mod
        panel_xaml = (
            '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"'
            ' xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n'
            '  <Style x:Key="PanelStyle" TargetType="Button">\n'
            '    <Setter Property="Template">\n'
            '      <Setter.Value>\n'
            '        <ControlTemplate TargetType="Button">\n'
            '          <RelativePanel Padding="4">\n'
            '            <Canvas x:Name="C" Width="40" Height="20" RelativePanel.AlignLeftWithPanel="True">\n'
            '              <Border x:Name="CB" Canvas.Left="5" Canvas.Top="6" Canvas.ZIndex="3" Width="10" Height="10" />\n'
            '            </Canvas>\n'
            '            <Viewbox Stretch="Uniform" StretchDirection="DownOnly" RelativePanel.RightOf="C">\n'
            '              <UniformGrid Rows="2" Columns="2" FirstColumn="1" />\n'
            '            </Viewbox>\n'
            '            <WrapPanel Orientation="Horizontal" ItemSpacing="4" LineSpacing="6" ItemsStretch="Last" />\n'
            '            <VariableSizedWrapGrid x:Name="VSWG" Width="100" Height="60" ItemWidth="20" ItemHeight="15" MaximumRowsOrColumns="3" HorizontalChildrenAlignment="Center" VerticalChildrenAlignment="Bottom">\n'
            '              <Border x:Name="Tile" VariableSizedWrapGrid.RowSpan="1" VariableSizedWrapGrid.ColumnSpan="1" />\n'
            '            </VariableSizedWrapGrid>\n'
            '            <ItemsWrapGrid ItemWidth="20" ItemHeight="20" GroupPadding="4" />\n'
            '          </RelativePanel>\n'
            '        </ControlTemplate>\n'
            '      </Setter.Value>\n'
            '    </Setter>\n'
            '  </Style>\n'
            '</ResourceDictionary>\n'
        )
        doc = tpl_mod.parse_xaml(panel_xaml, "panels_tpl.xaml")
        comp = tpl_mod.Compiler()
        comp.compile_style(doc.children[0], enum_id="IM_TPL_PANELS")
        tpl_mod.verify_fully_consumed(doc.children[0])
        self.assertGreaterEqual(len(comp.elements), 9)
        emitted = tpl_mod.emit(comp)
        cc = None
        for comp_cmake in sorted((BUILD / "CMakeFiles").glob("*/CMakeCCompiler.cmake")):
            for line in comp_cmake.read_text(encoding="utf-8", errors="ignore").splitlines():
                if line.startswith('set(CMAKE_C_COMPILER "') and line.endswith('")'):
                    cand = line[len('set(CMAKE_C_COMPILER "'):-2]
                    if Path(cand).is_file():
                        cc = cand
                        break
            if cc:
                break
        self.assertIsNotNone(cc, f"C compiler not found in {BUILD}")
        with tempfile.TemporaryDirectory() as tmp:
            tp = Path(tmp)
            gen_c = emitted["template_data.c"].replace('#include "template.h"', "").replace(
                "const im_template im_templates[IM_TEMPLATE_COUNT]", "static const im_template p_templates[1]"
            )
            for sym in (
                "im_template_setters", "im_template_elements", "im_template_props",
                "im_template_groups", "im_template_states", "im_template_transitions",
                "im_template_anims", "im_template_keyframes", "im_template_path_segs",
            ):
                gen_c = gen_c.replace(sym, "p_" + sym[len("im_"):])
            (tp / "tpl_panels.c").write_text(gen_c, encoding="utf-8")
            (tp / "main.c").write_text(
                '#include <stdint.h>\n#include <stddef.h>\n#include "tree.h"\n#include "layout.h"\n'
                '#include "tpl_panels.c"\n'
                'static uint8_t mem[131072]; static size_t used = 0;\n'
                'static void *al(void *c, size_t n){(void)c; n=(n+7u)&~7u; if(used+n>sizeof(mem))return NULL; void *p=&mem[used]; used+=n; return p;}\n'
                'static void *rl(void *c, void *p, size_t o, size_t n){void *q=al(c,n); if(!q)return NULL; if(p){for(size_t i=0;i<(o<n?o:n);i++)((uint8_t*)q)[i]=((uint8_t*)p)[i];} return q;}\n'
                'static void fr(void *c, void *p, size_t n){(void)c;(void)p;(void)n;}\n'
                'int main(void){\n'
                '  im_tree t; im_palette pal={IM_DARK,{0}}; im_allocator a={al,rl,fr,NULL};\n'
                '  if(!im_tree_init(&t,a)) return 1;\n'
                '  im_node *r=im_tree_instantiate_custom_template(&t,&pal,&p_templates[0],p_template_setters,p_template_elements,p_template_props,"P");\n'
                '  if(!r) return 2;\n'
                '  t.root=r; im_layout_run(&t,r,200*IM_ONE,120*IM_ONE,IM_ONE);\n'
                '  im_node *cb=im_tree_find_by_name(r,"CB");\n'
                '  im_node *vswg=im_tree_find_by_name(r,"VSWG");\n'
                '  im_node *tile=im_tree_find_by_name(r,"Tile");\n'
                '  if(!cb || cb->absolute_rect.left != 9*IM_ONE || cb->absolute_rect.top != 10*IM_ONE) return 3;\n'
                '  if(!vswg || !tile) return 4;\n'
                '  /* Tile (20x15) inside VSWG (100x60) with HorizontalChildrenAlignment=Center & VerticalChildrenAlignment=Bottom -> offset (40,45) */\n'
                '  if(tile->absolute_rect.left - vswg->absolute_rect.left != 40*IM_ONE) return 5;\n'
                '  if(tile->absolute_rect.top - vswg->absolute_rect.top != 45*IM_ONE) return 6;\n'
                '  im_tree_destroy(&t); return 0;\n'
                '}\n',
                encoding="utf-8",
            )
            exe_p = tp / "panels_tpl_test.exe"
            comp_res = subprocess.run(
                [cc, "-std=gnu23", "-I", str(ROOT / "ui"), str(tp / "main.c"),
                 str(ROOT / "ui/tree.c"), str(ROOT / "ui/layout.c"), str(ROOT / "ui/panels.c"),
                 str(ROOT / "ui/transform.c"), str(ROOT / "ui/theme.c"),
                 str(ROOT / "ui/theme_data.c"), str(ROOT / "ui/template_data.c"),
                 "-o", str(exe_p)],
                capture_output=True, text=True, timeout=60,
            )
            self.assertEqual(comp_res.returncode, 0, f"compile failed: {comp_res.stderr}")
            r_res = subprocess.run([str(exe_p)], stdin=subprocess.DEVNULL, capture_output=True, text=True, timeout=30)
            self.assertEqual(r_res.returncode, 0, f"compiled panel template failed rc={r_res.returncode}")

    def test_theme_resources_accent_and_contrast(self):
        exe = BUILD / "test-resources.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("test_resources: ALL PASSED", res.stdout)

        # Independent double-precision D65 CIELAB oracle for all 1,000 derived accent colors.
        def srgb_to_lin(u8):
            c = u8 / 255.0
            return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4

        def lin_to_srgb(lin):
            lin = max(0.0, min(1.0, lin))
            c = lin * 12.92 if lin <= 0.0031308 else 1.055 * (lin ** (1.0 / 2.4)) - 0.055
            return max(0, min(255, int(round(c * 255.0))))

        def rgb_to_lab(r, g, b):
            rl, gl, bl = srgb_to_lin(r), srgb_to_lin(g), srgb_to_lin(b)
            xn = (0.4124564 * rl + 0.3575761 * gl + 0.1804375 * bl) / 0.95047
            yn = (0.2126729 * rl + 0.7151522 * gl + 0.0721750 * bl) / 1.00000
            zn = (0.0193339 * rl + 0.1191920 * gl + 0.9503041 * bl) / 1.08883
            f = lambda t: t ** (1.0 / 3.0) if t > 216.0 / 24389.0 else (24389.0 / 27.0 * t + 16.0) / 116.0
            fx, fy, fz = f(xn), f(yn), f(zn)
            return 116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)

        def lab_to_lin(L, a, b):
            fy = (L + 16.0) / 116.0
            fx = fy + a / 500.0
            fz = fy - b / 200.0
            finv = lambda u: u ** 3 if u > 6.0 / 29.0 else 3.0 * ((6.0 / 29.0) ** 2) * (u - 4.0 / 29.0)
            X, Y, Z = finv(fx) * 0.95047, finv(fy) * 1.00000, finv(fz) * 1.08883
            return (
                3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z,
                -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z,
                0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z,
            )

        def lab_to_gamut(L, a, b):
            if L <= 0.0:
                return (0, 0, 0)
            if L >= 100.0:
                return (255, 255, 255)
            rl, gl, bl = lab_to_lin(L, a, b)
            if min(rl, gl, bl) < 0.0 or max(rl, gl, bl) > 1.0:
                lo, hi = 0.0, 1.0
                for _ in range(32):
                    mid = 0.5 * (lo + hi)
                    tr, tg, tb = lab_to_lin(L, a * mid, b * mid)
                    if min(tr, tg, tb) >= 0.0 and max(tr, tg, tb) <= 1.0:
                        lo = mid
                    else:
                        hi = mid
                rl, gl, bl = lab_to_lin(L, a * lo, b * lo)
            return (lin_to_srgb(rl), lin_to_srgb(gl), lin_to_srgb(bl))

        accent_count = 0
        worst_err = 0
        for line in res.stdout.splitlines():
            if line.startswith("ACCENT "):
                vals = [int(x) for x in line.split()[1:]]
                base = vals[0]
                r, g, b = (base >> 16) & 255, (base >> 8) & 255, base & 255
                L, a, bv = rgb_to_lab(r, g, b)
                up, dn = 100.0 - L, L
                targets = [
                    L + (10.0 if up > 34.0 else up * 0.294),
                    L + (22.0 if up > 34.0 else up * 0.647),
                    L + (34.0 if up > 34.0 else up * 0.900),
                    L - (10.0 if dn > 34.0 else dn * 0.294),
                    L - (22.0 if dn > 34.0 else dn * 0.647),
                    L - (34.0 if dn > 34.0 else dn * 0.900),
                ]
                for si, tL in enumerate(targets):
                    wr, wg, wb = lab_to_gamut(tL, a, bv)
                    got = vals[1 + si]
                    gr, gg, gb = (got >> 16) & 255, (got >> 8) & 255, got & 255
                    err = max(abs(gr - wr), abs(gg - wg), abs(gb - wb))
                    if err > worst_err:
                        worst_err = err
                    self.assertLessEqual(err, 2, f"ACCENT base={base:#08x} shade={si} got=({gr},{gg},{gb}) want=({wr},{wg},{wb})")
                accent_count += 1
        self.assertEqual(accent_count, 1000)

        # Verify --live-shots emits 2 exact 160x64 BGRA frames where Light-panel Button differs from Dark sibling.
        raw = subprocess.check_output([str(exe), "--live-shots"], stdin=subprocess.DEVNULL)
        self.assertEqual(len(raw), 2 * 160 * 64 * 4)
        self.assertNotEqual(raw[:160 * 64 * 4], raw[160 * 64 * 4:])

        # Verify generated theme_data.{h,c} matches pinned WinUI checkout when cached locally.
        winui_cache = Path.home() / ".cache/impossible-sdk/winui"
        if (winui_cache / ".git").exists():
            chk = subprocess.run(
                [sys.executable, str(ROOT / "tools/winui/resources.py"), str(winui_cache), "--check"],
                capture_output=True, text=True,
            )
            self.assertEqual(chk.returncode, 0, chk.stdout + chk.stderr)

    def test_theme_transitions_pvl_interruptions_and_compiler(self):
        exe = BUILD / "test-transition.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("test_transition: ALL PASSED", res.stdout)

        # Independent double-precision PVL oracle for all 10 transition kinds x 10 instants (within 1 fixed unit).
        pvl_x1, pvl_y1, pvl_x2, pvl_y2 = 6554 / 65536.0, 58982 / 65536.0, 13107 / 65536.0, 1.0
        # (move_dur, op_dur, fx, fy, fs, ts, fo, to, ox, oy)
        specs = [
            (367, 167, 100.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 0.0),          # 0: Entrance
            (167, 83, 0.0, 50.0, 1.0, 1.0, 0.0, 1.0, 0.0, 0.0),            # 1: PopupOpen
            (83, 83, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0),              # 2: PopupClose
            (167, 167, 0.0, 0.0, 55706 / 65536.0, 1.0, 0.0, 1.0, 0.5, 0.5),# 3: Add
            (167, 167, 0.0, 0.0, 1.0, 55706 / 65536.0, 1.0, 0.0, 0.5, 0.5),# 4: Delete
            (367, 0, 80.0, -40.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0),           # 5: Reposition
            (200, 0, 80.0, -40.0, 1.0, 1.0, 1.0, 1.0, 0.0, 0.0),           # 6: Reorder
            (367, 167, 40.0, 0.0, 1.0, 1.0, 0.0, 1.0, 0.0, 0.0),           # 7: Content
            (367, 167, 0.0, 0.0, 60293 / 65536.0, 1.0, 0.0, 1.0, 0.5, 0.5),# 8: DrillIn
            (367, 167, 0.0, 0.0, 70779 / 65536.0, 1.0, 0.0, 1.0, 0.5, 0.5),# 9: DrillOut
        ]
        trans_rows = 0
        for line in res.stdout.splitlines():
            if line.startswith("TRANS "):
                toks = [int(x) for x in line.split()[1:]]
                ki, dt, gx, gy, gsc, gop, gox, goy = toks
                md, od, fx, fy, fs, ts, fo, to, ox, oy = specs[ki]
                if md == 0 or dt >= md:
                    wx, wy, wsc = 0.0, 0.0, ts
                else:
                    p = self._exact_bezier(pvl_x1, pvl_y1, pvl_x2, pvl_y2, dt / float(md))
                    wx = fx + (0.0 - fx) * p
                    wy = fy + (0.0 - fy) * p
                    wsc = fs + (ts - fs) * p
                if od == 0 or dt >= od:
                    wop = to
                else:
                    wop = fo + (to - fo) * (dt / float(od))
                # 16.16 progress quantization error scales linearly with travel span (<= 1.0 DIP / <= 16 fixed LSB).
                self.assertLessEqual(abs(gx / 65536.0 - wx), 0.02, f"TRANS ki={ki} dt={dt} x")
                self.assertLessEqual(abs(gy / 65536.0 - wy), 0.02, f"TRANS ki={ki} dt={dt} y")
                self.assertLessEqual(abs(gsc - round(wsc * 65536.0)), 2, f"TRANS ki={ki} dt={dt} scale")
                self.assertLessEqual(abs(gop - round(wop * 65536.0)), 1, f"TRANS ki={ki} dt={dt} opacity")
                self.assertEqual(gox, round(ox * 65536.0))
                self.assertEqual(goy, round(oy * 65536.0))
                trans_rows += 1
        self.assertEqual(trans_rows, 100)

        # Verify template compiler accepts all named ThemeTransitions and refuses unknown ones with file:line.
        sys.path.insert(0, str(ROOT / "tools" / "winui"))
        import templates as tpl_mod
        good_xaml = (
            '<ResourceDictionary xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"'
            ' xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml">\n'
            '  <Style x:Key="TransStyle" TargetType="ListView">\n'
            '    <Setter Property="Template">\n'
            '      <Setter.Value>\n'
            '        <ControlTemplate TargetType="ListView"><ItemsPresenter /></ControlTemplate>\n'
            '      </Setter.Value>\n'
            '    </Setter>\n'
            '    <Setter Property="ItemContainerTransitions">\n'
            '      <Setter.Value>\n'
            '        <TransitionCollection>\n'
            '          <EntranceThemeTransition FromHorizontalOffset="60" IsStaggeringEnabled="True" />\n'
            '          <PopupThemeTransition FromVerticalOffset="32" />\n'
            '          <AddDeleteThemeTransition />\n'
            '          <RepositionThemeTransition IsStaggeringEnabled="False" />\n'
            '          <ReorderThemeTransition />\n'
            '          <ContentThemeTransition HorizontalOffset="24" VerticalOffset="0" />\n'
            '          <NavigationThemeTransition />\n'
            '        </TransitionCollection>\n'
            '      </Setter.Value>\n'
            '    </Setter>\n'
            '  </Style>\n'
            '</ResourceDictionary>\n'
        )
        doc = tpl_mod.parse_xaml(good_xaml, "good_trans.xaml")
        comp = tpl_mod.Compiler()
        comp.compile_style(doc.children[0], enum_id="IM_TPL_TRANS")
        tpl_mod.verify_fully_consumed(doc.children[0])
        self.assertGreaterEqual(len(comp.elements), 7)

        bad_xaml = good_xaml.replace("<ReorderThemeTransition />", "<BogusThemeTransition />")
        bad_doc = tpl_mod.parse_xaml(bad_xaml, "bad_trans.xaml")
        bad_comp = tpl_mod.Compiler()
        with self.assertRaises(tpl_mod.TemplateError) as ctx:
            bad_comp.compile_style(bad_doc.children[0], enum_id="IM_TPL_BAD_TRANS")
        self.assertIn("bad_trans.xaml:", str(ctx.exception))

    def test_shadows_and_materials_cache_and_fallbacks(self):
        exe = BUILD / "test-shadow-material.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("test_shadow_material: ALL PASSED", res.stdout)

    def test_fixed_point_text_and_fonts(self):
        exe = BUILD / "test-text.exe"
        self.assertTrue(exe.exists(), f"missing {exe}; run tools/build.py first")

        # 1. Synthetic CFF v1 (gsubr+lsubr+hintmask+cubic), CFF2 refusal, 2-face TTC,
        #    TTF off-curve start & point-attached composite, OOM+retry, UTF-8, UAX #14/#29 breaks.
        res = subprocess.run([str(exe)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(res.returncode, 0, res.stdout + res.stderr)
        self.assertIn("test_text: ALL PASSED", res.stdout)

        def win_path(p: Path) -> str:
            if os.name == "nt":
                return str(p)
            return subprocess.run(["wslpath", "-w", str(p)], capture_output=True, text=True, check=True).stdout.strip()

        # 2. Required Selawik face (pinned SHA-256) + Cascadia Code + Segoe UI: every glyph must draw.
        selawik = Path.home() / ".cache/impossible-sdk/fonts/selawk.ttf"
        self.assertTrue(selawik.is_file(), f"missing required Selawik test face at {selawik}")
        self.assertEqual(
            hashlib.sha256(selawik.read_bytes()).hexdigest(),
            "e9d98518d8ac2817782a9a382430463a2e0793ea68350b695bb727d9a830ee1c",
        )
        font_checks = [
            (win_path(selawik), 352, 3),
            (r"C:\Windows\Fonts\CascadiaCode.ttf", 1780, 7),
            (r"C:\Windows\Fonts\segoeui.ttf", 5395, 29),
        ]
        for fpath, expected_total, expected_empty in font_checks:
            r = subprocess.run([str(exe), "--all-glyphs", fpath], capture_output=True, text=True, stdin=subprocess.DEVNULL)
            self.assertEqual(r.returncode, 0, f"{fpath}: {r.stdout}{r.stderr}")
            expected_inked = expected_total - expected_empty
            self.assertIn(f"total={expected_total} ok={expected_total} empty={expected_empty} inked={expected_inked}", r.stdout)
            self.assertIn("clip_ok=1 leaked=0", r.stdout)

        # 3. DirectWrite / WPF GlyphTypeface exact nominal advance width comparison (Segoe UI 14px & 13.5px, Cascadia 14px)
        #    and Segoe UI wrapped paragraph word-break comparison.
        pwsh = "powershell.exe" if os.name != "nt" else "powershell"
        ps_script = r"""
Add-Type -AssemblyName PresentationCore
foreach ($f in @("C:\Windows\Fonts\segoeui.ttf", "C:\Windows\Fonts\CascadiaCode.ttf")) {
  $gt = New-Object System.Windows.Media.GlyphTypeface($f)
  for ($cp = 32; $cp -le 126; $cp++) {
    $gid = $gt.CharacterToGlyphMap[$cp]
    $adv = $gt.AdvanceWidths[$gid]
    Write-Output ("DW $f $cp $gid " + $adv.ToString("R", [System.Globalization.CultureInfo]::InvariantCulture))
  }
}
"""
        dw_res = subprocess.run([pwsh, "-NoProfile", "-Command", ps_script], capture_output=True, text=True, stdin=subprocess.DEVNULL)
        self.assertEqual(dw_res.returncode, 0, dw_res.stderr)
        dw_adv = {}
        for line in dw_res.stdout.splitlines():
            parts = line.strip().split()
            if len(parts) == 5 and parts[0] == "DW":
                dw_adv[(parts[1], int(parts[2]))] = (int(parts[3]), float(parts[4]))
        self.assertEqual(len(dw_adv), 95 * 2)

        for fpath, size_px in [
            (r"C:\Windows\Fonts\segoeui.ttf", 14.0),
            (r"C:\Windows\Fonts\segoeui.ttf", 13.5),
            (r"C:\Windows\Fonts\CascadiaCode.ttf", 14.0),
        ]:
            size_fx = int(round(size_px * 65536.0))
            mr = subprocess.run([str(exe), "--metrics", fpath, str(size_fx)], capture_output=True, text=True, stdin=subprocess.DEVNULL)
            self.assertEqual(mr.returncode, 0, mr.stdout + mr.stderr)
            checked = 0
            for line in mr.stdout.splitlines():
                if line.startswith("CP "):
                    kv = line.split()
                    cp = int(kv[1])
                    gid = int(kv[2].split("=")[1])
                    adv_fx = int(kv[4].split("=")[1])
                    dw_gid, dw_em = dw_adv[(fpath, cp)]
                    self.assertEqual(gid, dw_gid, f"{fpath} cp={cp} gid")
                    expected_fx = int(round(dw_em * size_px * 65536.0))
                    self.assertLessEqual(abs(adv_fx - expected_fx), 1, f"{fpath} size={size_px} cp={cp}")
                    checked += 1
            self.assertEqual(checked, 95)
            if fpath.endswith("segoeui.ttf"):
                self.assertIn("KERN AV=-140", mr.stdout)
                self.assertIn("TAIL 5394 adv_u=1104", mr.stdout)

        # 4. Paragraph wrapping & alignment (Left/Center/Right) with Segoe UI:
        #    verify UAX #14 hyphen break, word breaks matching DirectWrite ideal advances, and alignment origins.
        para = "Fixed-point text shapes Segoe UI paragraphs with self-contained line breaks across narrow widths."
        with tempfile.TemporaryDirectory() as td:
            tf = Path(td) / "para.txt"
            tf.write_text(para, encoding="utf-8")
            win_tf = win_path(tf)
            size_fx = 14 * 65536
            max_w_fx = 220 * 65536
            lines_by_align = {}
            for align in (0, 1, 2):
                lr = subprocess.run(
                    [str(exe), "--layout", r"C:\Windows\Fonts\segoeui.ttf", str(size_fx), str(max_w_fx), str(align), "1", win_tf],
                    capture_output=True, text=True, stdin=subprocess.DEVNULL,
                )
                self.assertEqual(lr.returncode, 0, lr.stdout + lr.stderr)
                lns = []
                for line in lr.stdout.splitlines():
                    if line.startswith("LINE "):
                        toks = {kv.split("=")[0]: int(kv.split("=")[1]) for kv in line.split()[2:]}
                        lns.append(toks)
                self.assertGreaterEqual(len(lns), 3)
                lines_by_align[align] = lns

            # All 3 alignments produce identical line breaks and visible widths, with exact Left/Center/Right origins.
            for l0, l1, l2 in zip(lines_by_align[0], lines_by_align[1], lines_by_align[2]):
                self.assertEqual((l0["bs"], l0["be"], l0["w"]), (l1["bs"], l1["be"], l1["w"]))
                self.assertEqual((l0["bs"], l0["be"], l0["w"]), (l2["bs"], l2["be"], l2["w"]))
                self.assertEqual(l0["x"], 0)
                self.assertEqual(l1["x"], (max_w_fx - l0["w"]) // 2)
                self.assertEqual(l2["x"], max_w_fx - l0["w"])
                self.assertLessEqual(l0["w"], max_w_fx)
            # Reconstructed slices cover the full paragraph without dropping bytes.
            rebuilt = "".join(para[ln["bs"]:ln["be"]] for ln in lines_by_align[0])
            self.assertEqual(rebuilt, para)
            # Verify UAX #14 breaks after hyphen when a hyphenated word straddles the margin.
            narrow_w = 60 * 65536
            hr = subprocess.run(
                [str(exe), "--layout", r"C:\Windows\Fonts\segoeui.ttf", str(size_fx), str(narrow_w), "0", "1", win_tf],
                capture_output=True, text=True, stdin=subprocess.DEVNULL,
            )
            self.assertEqual(hr.returncode, 0, hr.stdout + hr.stderr)
            first_line = next(line for line in hr.stdout.splitlines() if line.startswith("LINE 0 "))
            toks0 = {kv.split("=")[0]: int(kv.split("=")[1]) for kv in first_line.split()[2:]}
            self.assertEqual(para[toks0["bs"]:toks0["be"]], "Fixed-")


if __name__ == "__main__":
    unittest.main()

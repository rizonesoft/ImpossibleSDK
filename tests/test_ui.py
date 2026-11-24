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
    # 1. Straight vertical edge at x = round(20 * scale), y = h // 2 must jump cleanly from background 32 to border >= 45
    #    with zero bilinear pre-edge blur at x - 1.
    y_mid = h // 2
    x_edge = round(20 * scale)
    bg_before = raw[(y_mid * w + (x_edge - 1)) * 4]
    edge_px = raw[(y_mid * w + x_edge) * 4]
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


def extract_mask_rects(layout_node):
    """Collect frame-relative logical rectangles for all text and icon nodes in a Dump.cs layout tree."""
    rects = []
    if layout_node.get("IsText") or layout_node.get("IsIcon"):
        r = layout_node["Rect"]
        rects.append((layout_node.get("Name") or layout_node.get("Type"), r["X"], r["Y"], r["Width"], r["Height"]))
    for ch in layout_node.get("Children", []):
        rects.extend(extract_mask_rects(ch))
    return rects


def compare_frames_with_layout_mask(mine, real, width, height, scale, cand_rects, ref_rects, limit=0, rect_tol_dip=1.0):
    """Compare candidate and reference text/icon rectangles FIRST, then compare unmasked pixels."""
    if len(cand_rects) != len(ref_rects):
        raise ValueError(f"masked element count mismatch: candidate={len(cand_rects)} vs reference={len(ref_rects)}")
    pixel_masks = []
    for (cname, cx, cy, cw, ch), (rname, rx, ry, rw, rh) in zip(cand_rects, ref_rects):
        if cname != rname:
            raise ValueError(f"masked element name mismatch: {cname!r} vs {rname!r}")
        if max(abs(cx - rx), abs(cy - ry), abs(cw - rw), abs(ch - rh)) > rect_tol_dip:
            raise ValueError(f"masked element {rname!r} moved: cand=({cx},{cy},{cw},{ch}) vs ref=({rx},{ry},{rw},{rh})")
        # Convert logical DIPs to target pixels and clip to frame bounds.
        x0 = max(0, min(width, round(rx * scale)))
        y0 = max(0, min(height, round(ry * scale)))
        x1 = max(0, min(width, round((rx + rw) * scale)))
        y1 = max(0, min(height, round((ry + rh) * scale)))
        pixel_masks.append((x0, y0, x1, y1))

    worst = 0
    for y in range(height):
        for x in range(width):
            if any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in pixel_masks):
                continue
            off = max(abs(mine[(y * width + x) * 4 + k] - real[(y * width + x) * 4 + k]) for k in range(4))
            if off > worst:
                worst = off
    if worst > limit:
        raise ValueError(f"unmasked pixel diff {worst} exceeds limit {limit}")
    return worst


def validate_script_log(script_log, require_modalities=("pointer", "keyboard", "touch")):
    """Verify a Script.cs log exercised every required input modality with real routed events and focus."""
    events = script_log.get("Events", [])
    final = script_log.get("FinalState", {})
    if not events:
        raise ValueError("empty script event log")
    seen_mods = set()
    clicks = 0
    for ev in events:
        name = ev.get("Event", "")
        ptype = ev.get("PointerType", "")
        if name == "PointerPressed" and ptype == "Mouse":
            seen_mods.add("pointer")
        elif name.startswith("KeyDown:"):
            seen_mods.add("keyboard")
        elif name == "PointerPressed" and ptype == "Touch":
            seen_mods.add("touch")
        if name == "Click":
            clicks += 1
    missing = [m for m in require_modalities if m not in seen_mods]
    if missing:
        raise ValueError(f"script log missing required input modalities: {missing}")
    if clicks < len(require_modalities):
        raise ValueError(f"expected at least {len(require_modalities)} Click events, got {clicks}")
    if final.get("clicks") != clicks:
        raise ValueError(f"FinalState.clicks ({final.get('clicks')}) != logged Click events ({clicks})")
    if not final.get("focused") or final.get("focused") == "none":
        raise ValueError(f"FinalState.focused not set: {final}")


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
            # Nested missing-package control (DockPanel inside StackPanel) must be refused before launching WinUI.
            nested_mc = {"nested-dock": {"id": "nested-dock", "control": "StackPanel", "children": [{"id": "ch-dock", "control": "DockPanel"}]}}
            with self.assertRaises(ValueError) as cm_nmc:
                ref_run.run_reference("unused-exe", Path(tmp) / "nmc_out", snapshotted_specs=nested_mc)
            self.assertIn("DockPanel", str(cm_nmc.exception))
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

        # 3. Scaled captures at 150%, 200%, 300% pass crispness/anti-stairstep checks, while bilinear & nearest fail.
        s100 = frames["button-Normal-Dark.bgra"]
        for cid, scale, w, h in [
            ("button-Normal-Dark-s150", 1.5, 240, 96),
            ("button-Normal-Dark-s200", 2.0, 320, 128),
            ("button-Normal-Dark-s300", 3.0, 480, 192),
        ]:
            case = by_id[cid]
            self.assertEqual((case["scale"], case["width"], case["height"]), (scale, w, h))
            ar = case["automation"]["Rect"]
            self.assertEqual((ar["X"], ar["Y"], ar["Width"], ar["Height"]), (20, 16, 120, 32))
            real = frames[case["file"]]
            check_scaled_control_capture(case, real)
            with self.assertRaises(ValueError):
                check_scaled_control_capture(case, bilinear_upscale(s100, 160, 64, w, h))
            with self.assertRaises(ValueError):
                check_scaled_control_capture(case, nearest_upscale(s100, 160, 64, w, h))

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


@unittest.skipUnless(
    os.environ.get("IMPOSSIBLE_LIVE_REFERENCE") == "1" and (os.name == "nt" or "microsoft" in platform.release().lower()),
    "set IMPOSSIBLE_LIVE_REFERENCE=1 on a Windows display session to run live WinUI Reference.exe tests",
)
class LiveReference(unittest.TestCase):
    def test_live_non_button_storyboard_errors_and_script_repeatability(self):
        sys.path.insert(0, str(ROOT / "reference"))
        import run as ref_run

        with tempfile.TemporaryDirectory(dir=ref_run.host_temp_root(), prefix="impossible-live-ref-") as build_tmp:
            exe = ref_run.build_reference(build_tmp)

            # 1. Nonexistent visual state stops the run naming the state.
            with tempfile.TemporaryDirectory() as tmp:
                bad_state = Path(tmp) / "bad_state.json"
                bad_state.write_text(json.dumps({"cases": [{
                    "id": "bad-state-case",
                    "control": "Button",
                    "theme": "Dark",
                    "scale": 1.0,
                    "state": "NoSuchVisualState",
                    "frame": {"width": 160, "height": 64},
                    "properties": {"Width": 120, "Height": 32},
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as ctx:
                    ref_run.run_reference(exe, Path(tmp) / "out_bad", cases_path=bad_state, work_dir=build_tmp)
                self.assertIn("NoSuchVisualState", ctx.exception.stderr)

                # 2. Unknown control type and unknown property fail naming case ID and field.
                bad_ctrl = Path(tmp) / "bad_ctrl.json"
                bad_ctrl.write_text(json.dumps({"cases": [{
                    "id": "bad-type-case",
                    "control": "BogusWidget",
                    "theme": "Dark",
                    "scale": 1.0,
                    "frame": {"width": 160, "height": 64},
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as ctx2:
                    ref_run.run_reference(exe, Path(tmp) / "out_bad2", cases_path=bad_ctrl, work_dir=build_tmp)
                self.assertIn("bad-type-case.control", ctx2.exception.stderr)

                # 2b. Invalid scale (0) and path-traversal case ID must fail explicitly.
                bad_scale = Path(tmp) / "bad_scale.json"
                bad_scale.write_text(json.dumps({"cases": [{
                    "id": "zero-scale-case",
                    "control": "Button",
                    "theme": "Dark",
                    "scale": 0,
                    "frame": {"width": 160, "height": 64},
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as ctx3:
                    ref_run.run_reference(exe, Path(tmp) / "out_bad3", cases_path=bad_scale, work_dir=build_tmp)
                self.assertIn("zero-scale-case.scale", ctx3.exception.stderr)

                bad_id = Path(tmp) / "bad_id.json"
                bad_id.write_text(json.dumps({"cases": [{
                    "id": "../escape",
                    "control": "Button",
                    "theme": "Dark",
                    "scale": 1.0,
                    "frame": {"width": 160, "height": 64},
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as ctx4:
                    ref_run.run_reference(exe, Path(tmp) / "out_bad4", cases_path=bad_id, work_dir=build_tmp)
                self.assertIn("invalid case id", ctx4.exception.stderr)

                # Case-colliding keys ("Control" + "control") and unknown fields must be rejected by both Python and C#.
                no_missing = Path(tmp) / "no_missing.json"
                no_missing.write_text('{"missing_controls": []}', encoding="utf-8")
                bad_collide = Path(tmp) / "bad_collide.json"
                bad_collide.write_text('{"cases":[{"id":"collide","Control":"Border","control":"TextBlock","theme":"Dark","scale":1.0,"frame":{"width":160,"height":64}}]}', encoding="utf-8")
                with self.assertRaises(ValueError):
                    ref_run.run_reference(exe, Path(tmp) / "out_bad5_py", cases_path=bad_collide, work_dir=build_tmp)
                with self.assertRaises(subprocess.CalledProcessError) as ctx5:
                    ref_run.run_reference(exe, Path(tmp) / "out_bad5", cases_path=bad_collide, work_dir=build_tmp, pkg_manifest_path=no_missing)
                self.assertIn("case-colliding", ctx5.exception.stderr)

                bad_unknown = Path(tmp) / "bad_unknown.json"
                bad_unknown.write_text(json.dumps({"cases": [{
                    "id": "unknown-field-case",
                    "control": "Button",
                    "theme": "Dark",
                    "scale": 1.0,
                    "frame": {"width": 160, "height": 64},
                    "BogusField": 123,
                }]}), encoding="utf-8")
                with self.assertRaises(ValueError):
                    ref_run.run_reference(exe, Path(tmp) / "out_bad6_py", cases_path=bad_unknown, work_dir=build_tmp)
                with self.assertRaises(subprocess.CalledProcessError) as ctx6:
                    ref_run.run_reference(exe, Path(tmp) / "out_bad6", cases_path=bad_unknown, work_dir=build_tmp, pkg_manifest_path=no_missing)
                self.assertIn("BogusField", ctx6.exception.stderr)

                # 3. Non-button controls: ToggleSwitch (ts-off vs animated ts-on vs independently settled ts-on-settled),
                #    ComboBox with SelectedIndex=1 via reflection (zero C# edits), ListView with items + RTL,
                #    transient Button with real TextBlock at 150% for real text-mask discovery, and scripted Button run twice.
                combo = Path(tmp) / "combo.json"
                combo.write_text(json.dumps({"cases": [
                    {
                        "id": "ts-off",
                        "control": "ToggleSwitch",
                        "theme": "Dark",
                        "scale": 1.0,
                        "state": "Off",
                        "frame": {"width": 160, "height": 64},
                        "properties": {"IsOn": False, "IsTabStop": False, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    },
                    {
                        "id": "ts-on",
                        "control": "ToggleSwitch",
                        "theme": "Dark",
                        "scale": 1.0,
                        "state": "On",
                        "use_transitions": True,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"IsOn": False, "IsTabStop": False, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    },
                    {
                        "id": "ts-on-settled",
                        "control": "ToggleSwitch",
                        "theme": "Dark",
                        "scale": 1.0,
                        "state": "On",
                        "use_transitions": False,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"IsOn": False, "IsTabStop": False, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    },
                    {
                        "id": "combo-dyn",
                        "control": "ComboBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "Properties": {"Width": 120, "SelectedIndex": 1, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "items": ["First", "Second"],
                    },
                    {
                        "id": "lv-rtl",
                        "control": "ListView",
                        "theme": "Dark",
                        "scale": 1.0,
                        "flow_direction": "RightToLeft",
                        "frame": {"width": 160, "height": 96},
                        "properties": {"Width": 140, "Height": 80, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "items": ["Alpha", "Beta"],
                    },
                    {
                        "id": "btn-text-transient-s150",
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.5,
                        "state": "Normal",
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 120, "Height": 32, "IsTabStop": False, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "children": [
                            {"id": "lbl", "control": "TextBlock", "properties": {"Name": "RealLabel", "Text": "Sample", "Width": 48, "Height": 18}}
                        ],
                    },
                    {
                        "id": "btn-script-live",
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Name": "ScriptedButton", "Width": 120, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [
                            {"action": "pointer_click"},
                            {"action": "focus"},
                            {"action": "key_press", "key": "Space"},
                            {"action": "touch_tap"},
                        ],
                    },
                    {
                        "id": "panel-root-auto",
                        "control": "StackPanel",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 140, "Height": 40, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "children": [
                            {"id": "child-btn", "control": "Button", "properties": {"Name": "InnerBtn", "Content": "Go", "Width": 100, "Height": 32}}
                        ],
                    },
                    {
                        "id": "btn-cancel-click",
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Name": "CancelBtn", "Width": 120, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [
                            {"action": "pointer_down"},
                            {"action": "pointer_move", "x": -10, "y": -10},
                            {"action": "pointer_up", "x": -10, "y": -10},
                        ],
                    },
                    {
                        "id": "panel-tab-traversal",
                        "control": "StackPanel",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 200, "height": 80},
                        "properties": {"Width": 180, "Height": 70, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "children": [
                            {"id": "tb-first", "control": "Button", "properties": {"Name": "FirstBtn", "Width": 80, "Height": 28, "IsTabStop": True}},
                            {"id": "tb-second", "control": "Button", "properties": {"Name": "SecondBtn", "Width": 80, "Height": 28, "IsTabStop": True}},
                        ],
                        "script": [
                            {"action": "pointer_click", "x": 40, "y": 14},
                            {"action": "key_press", "key": "Tab", "use_focused": True},
                        ],
                    },
                    {
                        "id": "textbox-select-undo",
                        "control": "TextBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 64},
                        "properties": {"Name": "EditBox", "Text": "Hello", "Width": 140, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [
                            {"action": "focus"},
                            {"action": "key_press", "key": "A", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "Back"},
                            {"action": "key_press", "key": "Z", "modifiers": ["Control"]},
                        ],
                    },
                    {
                        "id": "ts-drag-live",
                        "control": "ToggleSwitch",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Name": "DragSwitch", "IsOn": False, "OnContent": "", "OffContent": "", "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [
                            {"action": "pointer_down", "x": 12, "y": 16},
                            {"action": "pointer_move", "x": 38, "y": 16},
                            {"action": "pointer_up", "x": 38, "y": 16},
                        ],
                    },
                    {
                        "id": "child-state-normal",
                        "control": "StackPanel",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 140, "Height": 40, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "children": [
                            {"id": "cs-btn-n", "control": "Button", "state": "Normal", "properties": {"Width": 100, "Height": 32}}
                        ],
                    },
                    {
                        "id": "child-state-pressed",
                        "control": "StackPanel",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 140, "Height": 40, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "children": [
                            {"id": "cs-btn-p", "control": "Button", "state": "Pressed", "properties": {"Width": 100, "Height": 32}}
                        ],
                    },
                    {
                        "id": "tb-overlap-shift",
                        "control": "TextBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 64},
                        "properties": {"Name": "ShiftBox", "Text": "", "Width": 140, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [
                            {"action": "focus"},
                            {"action": "key_down", "key": "Shift"},
                            {"action": "key_press", "key": "A", "modifiers": ["Shift"]},
                            {"action": "key_press", "key": "B"},
                            {"action": "key_up", "key": "Shift"},
                            {"action": "key_press", "key": "C"},
                        ],
                    },
                    {
                        "id": "prop-togglebtn",
                        "control": "ToggleButton",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"IsChecked": True, "Width": 100, "Height": 32, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    },
                    {
                        "id": "prop-progressbar",
                        "control": "ProgressBar",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Value": 50, "Width": 120, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    },
                    {
                        "id": "prop-slider-vert",
                        "control": "Slider",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 64, "height": 160},
                        "properties": {"Orientation": "Vertical", "Value": 40, "Height": 120, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    },
                ]}), encoding="utf-8")

                out1 = ref_run.run_reference(exe, Path(tmp) / "out1", cases_path=combo, work_dir=build_tmp)
                out2 = ref_run.run_reference(exe, Path(tmp) / "out2", cases_path=combo, work_dir=build_tmp)

                m1 = json.loads((out1 / "cases.json").read_text(encoding="utf-8"))
                m2 = json.loads((out2 / "cases.json").read_text(encoding="utf-8"))
                by1 = {c["id"]: c for c in m1["cases"]}
                by2 = {c["id"]: c for c in m2["cases"]}

                # Animated ToggleSwitch On transition must await storyboard, differ from Off, and reach settled On terminal frame.
                self.assertTrue(by1["ts-on"].get("storyboard_awaited"))
                off_px = (out1 / "ts-off.bgra").read_bytes()
                on_px = (out1 / "ts-on.bgra").read_bytes()
                settled_px = (out1 / "ts-on-settled.bgra").read_bytes()
                self.assertNotEqual(off_px, on_px)
                self.assertLessEqual(max(abs(a - b) for a, b in zip(on_px, settled_px)), 2)

                # ComboBox SelectedIndex=1 works via reflection without C# edits and reports selected_item="Second".
                self.assertEqual(by1["combo-dyn"]["automation"]["ControlType"], "ComboBox")
                self.assertEqual(by1["combo-dyn"].get("selected_index"), 1)
                self.assertEqual(by1["combo-dyn"].get("selected_item"), "Second")

                self.assertEqual(by1["lv-rtl"]["flow_direction"], "RightToLeft")
                self.assertEqual(by1["lv-rtl"]["automation"]["ControlType"], "List")

                # Panel-root without an automation peer must still expose descendant Button peer matching its layout child rect.
                panel_auto = by1["panel-root-auto"]["automation"]
                self.assertEqual(panel_auto["ControlType"], "StackPanel")
                self.assertGreaterEqual(len(panel_auto["Children"]), 1)
                inner_btn = panel_auto["Children"][0]
                self.assertEqual(inner_btn["ControlType"], "Button")
                self.assertEqual(inner_btn["Name"], "Go")
                self.assertIn("Invoke", inner_btn["Patterns"])
                def _find_layout_name(node, want):
                    if node.get("Name") == want:
                        return node
                    for ch in node.get("Children", []):
                        f = _find_layout_name(ch, want)
                        if f is not None:
                            return f
                    return None
                inner_layout = _find_layout_name(by1["panel-root-auto"]["layout"], "InnerBtn")
                self.assertIsNotNone(inner_layout)
                self.assertGreaterEqual(inner_btn["Rect"]["Width"], 99.0)
                self.assertGreaterEqual(inner_btn["Rect"]["Height"], 31.0)
                self.assertGreaterEqual(inner_btn["Rect"]["X"], 0.0)
                self.assertLessEqual(inner_btn["Rect"]["X"] + inner_btn["Rect"]["Width"], 160.5)
                self.assertGreaterEqual(inner_btn["Rect"]["Y"], 0.0)
                self.assertLessEqual(inner_btn["Rect"]["Y"] + inner_btn["Rect"]["Height"], 64.5)
                for axis in ("X", "Y", "Width", "Height"):
                    self.assertAlmostEqual(inner_btn["Rect"][axis], inner_layout["Rect"][axis], delta=1.0)

                # Cancelled click must have received PointerPressed and produced 0 clicks.
                cancel_events = by1["btn-cancel-click"]["script_log"]["Events"]
                self.assertTrue(any(ev["Event"] == "PointerPressed" for ev in cancel_events))
                self.assertEqual(by1["btn-cancel-click"]["script_log"]["FinalState"]["clicks"], 0)

                # Switch drag must toggle IsOn from False to True and emit Toggled.
                drag_log = by1["ts-drag-live"]["script_log"]
                self.assertTrue(any(ev["Event"] == "Toggled" for ev in drag_log["Events"]))
                self.assertTrue(drag_log["FinalState"]["is_on"])

                # Tab traversal with use_focused must move focus from FirstBtn to SecondBtn.
                self.assertIn("SecondBtn", by1["panel-tab-traversal"]["script_log"]["FinalState"]["focused"])

                # TextBox Ctrl+A -> Backspace -> Ctrl+Z must prove selection (Sel=0:5), deletion (Text=;Sel=0:0), and undo restoration ("Hello").
                tb_events = by1["textbox-select-undo"]["script_log"]["Events"]
                self.assertTrue(any("Sel=0:5" in ev.get("State", "") for ev in tb_events), msg=tb_events)
                self.assertTrue(any("Text=;Sel=0:0" in ev.get("State", "") for ev in tb_events), msg=tb_events)
                self.assertEqual(by1["textbox-select-undo"]["script_log"]["FinalState"]["text"], "Hello")

                # Cross-step desktop shortcut refusal in both orders (Control->Escape and Escape->Control).
                for order_id, steps, exp_rel in (
                    ("seq-ctrl-esc", [{"action": "key_down", "key": "Control"}, {"action": "key_press", "key": "Escape"}], "key_up:Control"),
                    ("seq-esc-ctrl", [{"action": "key_down", "key": "Escape"}, {"action": "key_press", "key": "Control"}], "key_up:Escape"),
                ):
                    seq_shortcut = Path(tmp) / f"{order_id}.json"
                    seq_shortcut.write_text(json.dumps({"cases": [{
                        "id": order_id,
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 120, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": steps,
                    }]}), encoding="utf-8")
                    with self.assertRaises(subprocess.CalledProcessError) as cm_seq:
                        ref_run.run_reference(exe, Path(tmp) / f"out_{order_id}", cases_path=seq_shortcut, work_dir=build_tmp)
                    self.assertIn("Control+Escape is disallowed", cm_seq.exception.stderr)
                    self.assertIn(f"cleanup_releases=[{exp_rel}]", cm_seq.exception.stderr)
                    self.assertNotIn("cleanup_errors=", cm_seq.exception.stderr)

                # Numeric ("65563") and comma-combined ("A, A") VirtualKey values must be refused before SendInput.
                for bad_id, bad_key in (("num-vk-bypass", "65563"), ("comma-vk-bypass", "A, A")):
                    num_vk = Path(tmp) / f"{bad_id}.json"
                    num_vk.write_text(json.dumps({"cases": [{
                        "id": bad_id,
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 120, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [{"action": "key_down", "key": "Control"}, {"action": "key_press", "key": bad_key}],
                    }]}), encoding="utf-8")
                    with self.assertRaises(subprocess.CalledProcessError) as cm_num:
                        ref_run.run_reference(exe, Path(tmp) / f"out_{bad_id}", cases_path=num_vk, work_dir=build_tmp)
                    self.assertIn("must be a single named VirtualKey", cm_num.exception.stderr)
                    self.assertIn("cleanup_releases=[key_up:Control]", cm_num.exception.stderr)
                    self.assertNotIn("cleanup_errors=", cm_num.exception.stderr)

                # Releasing Control while Escape is held (key_up) must NOT trip the Control+Escape press guard.
                esc_rel_ctrl = Path(tmp) / "esc_rel_ctrl.json"
                esc_rel_ctrl.write_text(json.dumps({"cases": [{
                    "id": "esc-rel-ctrl",
                    "control": "Button",
                    "theme": "Dark",
                    "scale": 1.0,
                    "frame": {"width": 160, "height": 64},
                    "properties": {"Width": 120, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    "script": [{"action": "key_down", "key": "Escape"}, {"action": "key_up", "key": "Control"}, {"action": "key_up", "key": "Escape"}],
                }]}), encoding="utf-8")
                ref_run.run_reference(exe, Path(tmp) / "out_esc_rel_ctrl", cases_path=esc_rel_ctrl, work_dir=build_tmp)

                # Negative storyboard timeout with use_transitions=False must fail naming the case and state.
                timeout_case = Path(tmp) / "sb_timeout.json"
                timeout_case.write_text(json.dumps({"cases": [{
                    "id": "ts-timeout-notrans",
                    "control": "ToggleSwitch",
                    "theme": "Dark",
                    "scale": 1.0,
                    "state": "On",
                    "use_transitions": False,
                    "storyboard_timeout_ms": 0,
                    "frame": {"width": 160, "height": 64},
                    "properties": {"IsOn": False, "OnContent": "", "OffContent": "", "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as cm_to:
                    ref_run.run_reference(exe, Path(tmp) / "out_sb_timeout", cases_path=timeout_case, work_dir=build_tmp)
                self.assertIn("ts-timeout-notrans", cm_to.exception.stderr)
                self.assertIn("use_transitions=False", cm_to.exception.stderr)

                # Forced failure after pointer_down / key_down / touch_down must run balanced input cleanup with no cleanup errors.
                for down_action, expected_rel in (
                    ({"action": "pointer_down"}, "mouse_left_up"),
                    ({"action": "key_down", "key": "Shift"}, "key_up:Shift"),
                    ({"action": "touch_down"}, "touch_up"),
                ):
                    abort_case = Path(tmp) / f"abort_{expected_rel.replace(':', '_')}.json"
                    abort_case.write_text(json.dumps({"cases": [{
                        "id": "abort-test",
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 120, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [down_action, {"action": "force_abort_unknown"}],
                    }]}), encoding="utf-8")
                    with self.assertRaises(subprocess.CalledProcessError) as cm_abort:
                        ref_run.run_reference(exe, Path(tmp) / f"out_{expected_rel.replace(':', '_')}", cases_path=abort_case, work_dir=build_tmp)
                    self.assertIn(f"cleanup_releases=[{expected_rel}]", cm_abort.exception.stderr)
                    self.assertNotIn("cleanup_errors=", cm_abort.exception.stderr)

                # Real transient TextBlock at 150% is discovered by Dump.cs and masked cleanly by compare_frames_with_layout_mask.
                txt_case = by1["btn-text-transient-s150"]
                self.assertIn("scale_mode", txt_case)
                self.assertEqual(txt_case["effective_rasterization_scale"], txt_case["host_rasterization_scale"])
                txt_rects = extract_mask_rects(txt_case["layout"])
                self.assertEqual(len(txt_rects), 1)
                self.assertEqual(txt_rects[0][0], "RealLabel")
                # Independent expected centered TextBlock inside 160x64 frame: left=56, top=23, ~46x19 DIPs.
                ind_txt = [("RealLabel", 56.0, 23.0, 46.0, 19.0)]
                txt_px = (out1 / "btn-text-transient-s150.bgra").read_bytes()
                mut_txt = bytearray(txt_px)
                px = round((56.0 + 23.0) * 1.5)
                py = round((23.0 + 9.0) * 1.5)
                mut_txt[(py * 240 + px) * 4:(py * 240 + px) * 4 + 3] = bytes([255, 0, 255])
                compare_frames_with_layout_mask(bytes(mut_txt), txt_px, 240, 96, 1.5, ind_txt, txt_rects, limit=0)
                with self.assertRaises(ValueError):
                    compare_frames_with_layout_mask(bytes(mut_txt), txt_px, 240, 96, 1.5, [("RealLabel", 58.0, 23.0, 46.0, 19.0)], txt_rects, limit=0)

                # Nested child visual state must take effect (Pressed child pixels differ from Normal child pixels).
                self.assertNotEqual((out1 / "child-state-normal.bgra").read_bytes(), (out1 / "child-state-pressed.bgra").read_bytes())

                # Overlapping Shift modifier: held Shift must remain held after chorded A so B is uppercase and C is lowercase.
                self.assertEqual(by1["tb-overlap-shift"]["script_log"]["FinalState"]["text"], "ABc")

                # Generic property dispatch works on ToggleButton (IsChecked), ProgressBar (Value), and Slider (Orientation).
                self.assertEqual(by1["prop-togglebtn"]["automation"]["ControlType"], "Button")
                self.assertEqual(by1["prop-progressbar"]["automation"]["ControlType"], "ProgressBar")
                self.assertEqual(by1["prop-slider-vert"]["automation"]["ControlType"], "Slider")

                # focus on Button(IsTabStop=False) must fail without silently mutating IsTabStop to True.
                no_tab_case = Path(tmp) / "no_tab.json"
                no_tab_case.write_text(json.dumps({"cases": [{
                    "id": "btn-no-tabstop",
                    "control": "Button",
                    "theme": "Dark",
                    "scale": 1.0,
                    "frame": {"width": 160, "height": 64},
                    "properties": {"Width": 120, "Height": 32, "IsTabStop": False, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    "script": [{"action": "focus"}],
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as cm_nt:
                    ref_run.run_reference(exe, Path(tmp) / "out_no_tab", cases_path=no_tab_case, work_dir=build_tmp)
                self.assertIn("IsTabStop=False", cm_nt.exception.stderr)

                # Scripted button gives identical event + focus log across two independent runs.
                validate_script_log(by1["btn-script-live"]["script_log"])
                self.assertEqual(by1["btn-script-live"]["script_log"], by2["btn-script-live"]["script_log"])


if __name__ == "__main__":
    unittest.main()

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

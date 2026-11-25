"""The live WinUI reference tests: they bring a window to the front and send real mouse, keyboard and touch input, so they
run in the test machine (python3 tools/winvm/live.py), and on a desktop only when its owner asks."""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from frames import compare_frames_with_layout_mask, extract_mask_rects, validate_script_log


@unittest.skipUnless(
    os.environ.get("IMPOSSIBLE_DISPLAY") in ("vm", "desktop"),
    "live tests take the mouse and keyboard: run python3 tools/winvm/live.py, which uses the test machine",
)
class LiveReference(unittest.TestCase):
    def test_the_program_opens_no_window_unasked(self):
        sys.path.insert(0, str(ROOT / "reference"))
        import run as ref_run
        with tempfile.TemporaryDirectory(dir=ref_run.host_temp_root(), prefix="impossible-live-ref-") as folder:
            exe = ref_run.build_reference(folder)
            env = {k: v for k, v in os.environ.items() if k != "IMPOSSIBLE_DISPLAY"}
            out = Path(folder) / "unasked"
            done = subprocess.run([str(exe), ref_run.to_win_path(out)], env=env, capture_output=True, text=True, timeout=60)
            self.assertEqual(done.returncode, 77)
            self.assertFalse(out.exists())

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
                        "properties": {"Width": 120, "Height": 32, "Padding": 0, "IsTabStop": False, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
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
                        "properties": {"Name": "ShiftBox", "Text": "", "FontFamily": "Inter", "Width": 140, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
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
                        "id": "btn-touch-nofocus",
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Name": "NoFocusBtn", "Width": 120, "Height": 32, "IsTabStop": True, "AllowFocusOnInteraction": False, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [{"action": "touch_tap"}],
                    },
                    {
                        "id": "tb-trim-overflow-live",
                        "control": "TextBlock",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 48},
                        "properties": {"Name": "TrimOverflow", "Text": "A" * 80, "Width": 40, "TextTrimming": "CharacterEllipsis", "FontFamily": "Inter"},
                    },
                    {
                        "id": "tb-trim-fit-live",
                        "control": "TextBlock",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 420, "height": 48},
                        "properties": {"Name": "TrimFit", "Text": "A", "Width": 400, "TextTrimming": "CharacterEllipsis", "FontFamily": "Inter"},
                    },
                    {
                        "id": "reb-plain-live",
                        "control": "RichEditBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 64},
                        "properties": {"Name": "PlainRich", "FontFamily": "Inter", "Width": 140, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [{"action": "focus"}, {"action": "key_press", "key": "A"}],
                    },
                    {
                        "id": "grid-rows-live",
                        "control": "Grid",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 100},
                        "properties": {"Name": "RowGrid", "Width": 120, "Height": 80, "RowDefinitions": ["40", "40"]},
                        "children": [
                            {"id": "gr-r0", "control": "Border", "properties": {"Name": "Row0", "Grid.Row": 0, "Width": 80, "Height": 20}},
                            {"id": "gr-r1", "control": "Border", "properties": {"Name": "Row1", "Grid.Row": 1, "Width": 80, "Height": 20}},
                        ],
                    },
                    {
                        "id": "canvas-left-live",
                        "control": "Canvas",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 80},
                        "properties": {"Name": "PosCanvas", "Width": 160, "Height": 80},
                        "children": [
                            {"id": "cv-a", "control": "Border", "properties": {"Name": "Left10", "Canvas.Left": 10, "Canvas.Top": 12, "Width": 20, "Height": 20}},
                            {"id": "cv-b", "control": "Border", "properties": {"Name": "Left50", "Canvas.Left": 50, "Canvas.Top": 12, "Width": 20, "Height": 20}},
                        ],
                    },
                    {
                        "id": "vswg-span-live",
                        "control": "VariableSizedWrapGrid",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 200, "height": 80},
                        "properties": {"Name": "SpanGrid", "Width": 200, "Height": 80, "ItemWidth": 40, "ItemHeight": 30, "Orientation": "Horizontal"},
                        "children": [
                            {"id": "vs-1", "control": "Border", "properties": {"Name": "Span1", "VariableSizedWrapGrid.ColumnSpan": 1}},
                            {"id": "vs-2", "control": "Border", "properties": {"Name": "Span2", "VariableSizedWrapGrid.ColumnSpan": 2}},
                        ],
                    },
                    {
                        "id": "tb-copy-paste-live",
                        "control": "TextBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 64},
                        "properties": {"Name": "CopyBox", "Text": "", "FontFamily": "Inter", "Width": 140, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [
                            {"action": "focus"},
                            {"action": "key_press", "key": "A"},
                            {"action": "key_press", "key": "A", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "C", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "Right"},
                            {"action": "key_press", "key": "V", "modifiers": ["Control"]},
                        ],
                    },
                    {
                        "id": "tb-copy-paste-undo-live",
                        "control": "TextBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 64},
                        "properties": {"Name": "UndoBox", "Text": "", "FontFamily": "Inter", "Width": 140, "Height": 32, "IsTabStop": True, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "script": [
                            {"action": "focus"},
                            {"action": "key_press", "key": "A"},
                            {"action": "key_press", "key": "A", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "C", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "Right"},
                            {"action": "key_press", "key": "V", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "Z", "modifiers": ["Control"]},
                        ],
                    },
                    {
                        "id": "reb-allcaps-live",
                        "control": "RichEditBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 64},
                        "properties": {"Name": "CapsRich", "FontFamily": "Inter", "Width": 140, "Height": 32},
                        "rich_text_rtf": r"{\rtf1\ansi{\fonttbl{\f0 Inter;}}\f0\caps a}",
                    },
                    {
                        "id": "reb-used-leader-live",
                        "control": "RichEditBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 220, "height": 64},
                        "properties": {"Name": "UsedLeaderRich", "FontFamily": "Inter", "Width": 180, "Height": 32},
                        "rich_text_rtf": r"{\rtf1\ansi{\fonttbl{\f0 Inter;}}\pard\tldot\tx1440\f0 A\tab B\par}",
                    },
                    {
                        "id": "reb-unused-leader-live",
                        "control": "RichEditBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 220, "height": 64},
                        "properties": {"Name": "UnusedLeaderRich", "FontFamily": "Inter", "Width": 180, "Height": 32},
                        "rich_text_rtf": r"{\rtf1\ansi{\fonttbl{\f0 Inter;}}\pard\tldot\tx1440\f0 Heading\par}",
                    },
                    {
                        "id": "btn-trans-only-live",
                        "control": "Button",
                        "theme": "Dark",
                        "scale": 1.0,
                        "state": "Pressed",
                        "use_transitions": True,
                        "storyboard_timeout_ms": 2000,
                        "frame": {"width": 120, "height": 60},
                        "properties": {"Width": 80, "Height": 40, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "template_xaml": (
                            "<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
                            "xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
                            "<Border x:Name='RootBorder' Background='#FFFF0000'>"
                            "<VisualStateManager.VisualStateGroups>"
                            "<VisualStateGroup x:Name='CommonStates'>"
                            "<VisualStateGroup.Transitions>"
                            "<VisualTransition To='Pressed'>"
                            "<Storyboard>"
                            "<ObjectAnimationUsingKeyFrames Storyboard.TargetName='RootBorder' Storyboard.TargetProperty='Background'>"
                            "<DiscreteObjectKeyFrame KeyTime='0:0:0.35' Value='#FF0000FF'/>"
                            "</ObjectAnimationUsingKeyFrames>"
                            "</Storyboard>"
                            "</VisualTransition>"
                            "</VisualStateGroup.Transitions>"
                            "<VisualState x:Name='Normal'/>"
                            "<VisualState x:Name='Pressed'>"
                            "<VisualState.Setters>"
                            "<Setter Target='RootBorder.Background' Value='#FF0000FF'/>"
                            "</VisualState.Setters>"
                            "</VisualState>"
                            "</VisualStateGroup>"
                            "</VisualStateManager.VisualStateGroups>"
                            "</Border></ControlTemplate>"
                        ),
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
                        "id": "child-theme-light",
                        "control": "StackPanel",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 160, "height": 64},
                        "properties": {"Width": 140, "Height": 40, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                        "children": [
                            {"id": "cs-btn-light", "control": "Button", "theme": "Light", "state": "Normal", "properties": {"Width": 100, "Height": 32}}
                        ],
                    },
                    {
                        "id": "prop-combobox-hdr",
                        "control": "ComboBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 80},
                        "properties": {"PlaceholderText": "Pick", "Header": "Choice", "Width": 140, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
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

                script_only = Path(tmp) / "script_only.json"
                script_only.write_text(json.dumps({"cases": [{
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
                }]}), encoding="utf-8")

                out1 = ref_run.run_reference(exe, Path(tmp) / "out1", cases_path=combo, work_dir=build_tmp)
                out2 = ref_run.run_reference(exe, Path(tmp) / "out2", cases_path=script_only, work_dir=build_tmp)

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
                # The 48x18 slot is centred in 160x64, so it starts at (56, 23). A TextBlock reports its text, not its slot:
                # "Sample" at 14 in Segoe UI is 45.562 wide (the font's own advance widths) and one line is 18.621.
                ind_txt = [("RealLabel", 56.0, 23.0, 45.562, 18.621)]
                txt_px = (out1 / "btn-text-transient-s150.bgra").read_bytes()
                mut_txt = bytearray(txt_px)
                px = round((56.0 + 23.0) * 1.5)
                py = round((23.0 + 9.0) * 1.5)
                mut_txt[(py * 240 + px) * 4:(py * 240 + px) * 4 + 3] = bytes([255, 0, 255])
                compare_frames_with_layout_mask(bytes(mut_txt), txt_px, 240, 96, 1.5, ind_txt, txt_rects, limit=0)
                with self.assertRaises(ValueError):
                    compare_frames_with_layout_mask(bytes(mut_txt), txt_px, 240, 96, 1.5, [("RealLabel", 58.0, 23.0, 46.0, 19.0)], txt_rects, limit=0)

                # Nested child visual state and child theme override must both take effect in rendered pixels.
                norm_child_px = (out1 / "child-state-normal.bgra").read_bytes()
                self.assertNotEqual(norm_child_px, (out1 / "child-state-pressed.bgra").read_bytes())
                self.assertNotEqual(norm_child_px, (out1 / "child-theme-light.bgra").read_bytes())

                # Overlapping Shift modifier: held Shift stays active across chorded A and plain B, then flips for C (Caps Lock independent).
                shift_text = by1["tb-overlap-shift"]["script_log"]["FinalState"]["text"]
                self.assertIn(shift_text, ("ABc", "abC"))
                self.assertEqual(shift_text[0].isupper(), shift_text[1].isupper())
                self.assertNotEqual(shift_text[1].isupper(), shift_text[2].isupper())
                # Live rendered_text captures post-script displayed characters and exact FontFamily, and feeds publish_store end to end.
                rt_entries = by1["tb-overlap-shift"].get("rendered_text", [])
                matched_rt = [rt for rt in rt_entries if rt.get("text") == shift_text]
                self.assertTrue(matched_rt, f"expected {shift_text!r} in live rendered_text={rt_entries!r}")
                self.assertEqual(matched_rt[0].get("font", "").strip().lower(), "inter")
                pub_live_fresh = Path(tmp) / "pub_live_fresh"
                pub_live_fresh.mkdir()
                shutil.copy2(out1 / by1["tb-overlap-shift"]["file"], pub_live_fresh / by1["tb-overlap-shift"]["file"])
                (pub_live_fresh / "cases.json").write_text(json.dumps({
                    "installed_fonts": ["inter"],
                    "installed_font_ranges": {"inter": [[0x30, 0x39]]},
                    "cases": [by1["tb-overlap-shift"]],
                }), encoding="utf-8")
                pub_live_store = Path(tmp) / "pub_live_store"
                with self.assertRaises(ValueError) as cm_live_pub:
                    ref_run.publish_store(
                        pub_live_fresh, pub_live_store,
                        {"tb-overlap-shift": {"id": "tb-overlap-shift", "control": "TextBox", "properties": {"FontFamily": "Inter"}}},
                    )
                self.assertIn("rendered_text", str(cm_live_pub.exception))
                self.assertIn("not covered by installed FontFamily", str(cm_live_pub.exception))
                # touch_tap on AllowFocusOnInteraction=False button succeeds without timing out.
                self.assertEqual(by1["btn-touch-nofocus"]["script_log"]["FinalState"]["clicks"], 1)
                # Live C# IsTextTrimmed collector appends U+2026 only when layout actually trims text.
                ov_rt = by1["tb-trim-overflow-live"].get("rendered_text", [])
                self.assertTrue(any("\u2026" in r.get("text", "") for r in ov_rt), f"expected ellipsis in {ov_rt!r}")
                fit_rt = by1["tb-trim-fit-live"].get("rendered_text", [])
                self.assertTrue(any(r.get("text") == "A" for r in fit_rt), f"expected 'A' in {fit_rt!r}")
                self.assertFalse(any("\u2026" in r.get("text", "") for r in fit_rt), f"unexpected ellipsis in {fit_rt!r}")
                with self.assertRaises(ValueError) as cm_ov_live:
                    ref_run.check_no_prohibited_assets(
                        {"id": "tb-trim-overflow-live", "control": "TextBlock", "properties": {
                            "Text": "A" * 80, "Width": 40, "TextTrimming": "CharacterEllipsis", "FontFamily": "Inter",
                        }},
                        require_installed=True, installed_fonts=["inter"],
                        installed_font_ranges={"inter": [[0x20, 0x7E]]},
                        captured_frame=by1["tb-trim-overflow-live"],
                    )
                self.assertIn("U+2026", str(cm_ov_live.exception))
                ref_run.check_no_prohibited_assets(
                    {"id": "tb-trim-fit-live", "control": "TextBlock", "properties": {
                        "Text": "A", "Width": 400, "TextTrimming": "CharacterEllipsis", "FontFamily": "Inter",
                    }},
                    require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x20, 0x7E]]},
                    captured_frame=by1["tb-trim-fit-live"],
                )
                # Live RichEditBox collector captures plain typed 'A'/'a' with Inter and feeds check_no_prohibited_assets.
                reb_rt = by1["reb-plain-live"].get("rendered_text", [])
                self.assertTrue(any(r.get("text") in ("A", "a") and r.get("font", "").strip().lower() == "inter" for r in reb_rt), f"unexpected reb_rt={reb_rt!r}")
                reb_spec = {"id": "reb-plain-live", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}}
                ref_run.check_no_prohibited_assets(
                    reb_spec, require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x41, 0x41], [0x61, 0x61]]},
                    captured_frame=by1["reb-plain-live"],
                )
                with self.assertRaises(ValueError) as cm_reb_live:
                    ref_run.check_no_prohibited_assets(
                        reb_spec, require_installed=True, installed_fonts=["inter"],
                        installed_font_ranges={"inter": [[0x30, 0x39]]},
                        captured_frame=by1["reb-plain-live"],
                    )
                self.assertIn("not covered by installed FontFamily", str(cm_reb_live.exception))

                # Attached layout properties (Grid.Row, Canvas.Left, VariableSizedWrapGrid.ColumnSpan) move/resize child rects.
                def _find_layout_rect(node, name):
                    if node.get("Name") == name:
                        return node["Rect"]
                    for ch in node.get("Children", []):
                        got = _find_layout_rect(ch, name)
                        if got is not None:
                            return got
                    return None
                r0 = _find_layout_rect(by1["grid-rows-live"]["layout"], "Row0")
                r1 = _find_layout_rect(by1["grid-rows-live"]["layout"], "Row1")
                self.assertAlmostEqual(r1["Y"] - r0["Y"], 40.0, delta=1.0)
                c_a = _find_layout_rect(by1["canvas-left-live"]["layout"], "Left10")
                c_b = _find_layout_rect(by1["canvas-left-live"]["layout"], "Left50")
                self.assertAlmostEqual(c_b["X"] - c_a["X"], 40.0, delta=1.0)
                vs1 = _find_layout_rect(by1["vswg-span-live"]["layout"], "Span1")
                vs2 = _find_layout_rect(by1["vswg-span-live"]["layout"], "Span2")
                self.assertAlmostEqual(vs1["Width"], 40.0, delta=1.0)
                self.assertAlmostEqual(vs2["Width"], 80.0, delta=1.0)

                # Deterministic same-script copy/paste and undo verify text and selection state.
                cp_state = by1["tb-copy-paste-live"]["script_log"]["FinalState"]
                self.assertIn(cp_state["text"], ("AA", "aa"))
                self.assertEqual(cp_state["selection_start"], 2)
                self.assertEqual(cp_state["selection_length"], 0)
                undo_state = by1["tb-copy-paste-undo-live"]["script_log"]["FinalState"]
                self.assertIn(undo_state["text"], ("A", "a"))
                self.assertEqual(undo_state["selection_start"], 1)
                self.assertEqual(undo_state["selection_length"], 0)

                # Live RichEditBox formatting: AllCaps -> 'A', used dotted TabLeader -> refused, unused TabLeader -> accepted.
                caps_rt = by1["reb-allcaps-live"].get("rendered_text", [])
                self.assertTrue(any(r.get("text") == "A" for r in caps_rt), f"expected uppercase 'A' in {caps_rt!r}")
                with self.assertRaises(ValueError) as cm_caps:
                    ref_run.check_no_prohibited_assets(
                        {"id": "reb-allcaps-live", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}},
                        require_installed=True, installed_fonts=["inter"],
                        installed_font_ranges={"inter": [[0x61, 0x61]]},
                        captured_frame=by1["reb-allcaps-live"],
                    )
                self.assertIn("U+0041", str(cm_caps.exception))
                ref_run.check_no_prohibited_assets(
                    {"id": "reb-allcaps-live", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}},
                    require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x41, 0x41]]},
                    captured_frame=by1["reb-allcaps-live"],
                )
                used_rt = by1["reb-used-leader-live"].get("rendered_text", [])
                self.assertTrue(any(r.get("font") == "__unverified_rich_decoration__" for r in used_rt), f"expected TabLeader sentinel in {used_rt!r}")
                with self.assertRaises(ValueError):
                    ref_run.check_no_prohibited_assets(
                        {"id": "reb-used-leader-live", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}},
                        require_installed=True, installed_fonts=["inter"],
                        installed_font_ranges={"inter": [[0x20, 0x7E]]},
                        captured_frame=by1["reb-used-leader-live"],
                    )
                unused_rt = by1["reb-unused-leader-live"].get("rendered_text", [])
                self.assertFalse(any(r.get("font") == "__unverified_rich_decoration__" for r in unused_rt), f"unexpected sentinel in {unused_rt!r}")
                ref_run.check_no_prohibited_assets(
                    {"id": "reb-unused-leader-live", "control": "RichEditBox", "properties": {"FontFamily": "Inter"}},
                    require_installed=True, installed_fonts=["inter"],
                    installed_font_ranges={"inter": [[0x20, 0x7E]]},
                    captured_frame=by1["reb-unused-leader-live"],
                )

                # Transition-only delayed storyboard settles to Blue (#FF0000FF -> BGRA 255,0,0) and fails on short timeout.
                self.assertTrue(by1["btn-trans-only-live"].get("storyboard_awaited"))
                tr_px = (out1 / "btn-trans-only-live.bgra").read_bytes()
                center_off = (30 * 120 + 60) * 4
                self.assertEqual(tuple(tr_px[center_off:center_off + 3]), (255, 0, 0))
                tr_to_case = Path(tmp) / "trans_timeout.json"
                tr_to_case.write_text(json.dumps({"cases": [{
                    **next(c for c in json.loads(combo.read_text(encoding="utf-8"))["cases"] if c["id"] == "btn-trans-only-live"),
                    "id": "btn-trans-timeout",
                    "storyboard_timeout_ms": 50,
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as cm_trto:
                    ref_run.run_reference(exe, Path(tmp) / "out_trto", cases_path=tr_to_case, work_dir=build_tmp)
                self.assertIn("btn-trans-timeout", cm_trto.exception.stderr)
                self.assertIn("Pressed", cm_trto.exception.stderr)
                # GeneratedDuration combined storyboard budget check (equality + overrun) and live GeneratedDuration timeout.
                budget_done = subprocess.run([str(exe), "--check-storyboard-budget"], capture_output=True, text=True, timeout=60)
                self.assertEqual(budget_done.returncode, 0, budget_done.stderr)
                self.assertIn("storyboard-budget-ok", budget_done.stdout)
                gen_over_case = Path(tmp) / "gen_over.json"
                gen_over_case.write_text(json.dumps({"cases": [{
                    "id": "btn-gen-dur-over",
                    "control": "Button",
                    "theme": "Dark",
                    "scale": 1.0,
                    "state": "Pressed",
                    "use_transitions": True,
                    "storyboard_timeout_ms": 100,
                    "frame": {"width": 120, "height": 60},
                    "properties": {"Width": 80, "Height": 40, "HorizontalAlignment": "Center", "VerticalAlignment": "Center"},
                    "template_xaml": (
                        "<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
                        "xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
                        "<Border x:Name='RootBorder' Background='#FFFF0000'>"
                        "<VisualStateManager.VisualStateGroups>"
                        "<VisualStateGroup x:Name='CommonStates'>"
                        "<VisualStateGroup.Transitions>"
                        "<VisualTransition To='Pressed' GeneratedDuration='0:0:0.12'/>"
                        "</VisualStateGroup.Transitions>"
                        "<VisualState x:Name='Normal'/>"
                        "<VisualState x:Name='Pressed'/>"
                        "</VisualStateGroup>"
                        "</VisualStateManager.VisualStateGroups>"
                        "</Border></ControlTemplate>"
                    ),
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as cm_gen:
                    ref_run.run_reference(exe, Path(tmp) / "out_gen", cases_path=gen_over_case, work_dir=build_tmp)
                self.assertIn("btn-gen-dur-over", cm_gen.exception.stderr)
                self.assertIn("exceeds timeout 100ms", cm_gen.exception.stderr)

                # OpenType name-table discovery on the capture host populates real registered installed family names.
                host_fonts = set(m1.get("installed_fonts", []))
                self.assertTrue(any(f in host_fonts for f in ("segoe ui", "arial", "tahoma")))
                self.assertTrue(set(m1.get("installed_font_ranges", {}).keys()).issubset(host_fonts))

                # Generic property dispatch verifies actual configured values on ToggleButton, ProgressBar, Slider, and ComboBox.
                self.assertIs(by1["prop-togglebtn"].get("is_checked"), True)
                self.assertEqual(by1["prop-progressbar"].get("range_value"), 50.0)
                self.assertEqual(by1["prop-slider-vert"].get("orientation"), "Vertical")
                self.assertEqual(by1["prop-slider-vert"].get("range_value"), 40.0)
                self.assertEqual(by1["prop-combobox-hdr"].get("placeholder_text"), "Pick")
                self.assertEqual(by1["prop-combobox-hdr"].get("header"), "Choice")

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

                # Clipboard paste (Control+V) must be refused live by Script.cs (ambient clipboard is non-deterministic).
                paste_case = Path(tmp) / "paste_refused.json"
                paste_case.write_text(json.dumps({"cases": [{
                    "id": "reb-paste-refused",
                    "control": "RichEditBox",
                    "theme": "Dark",
                    "scale": 1.0,
                    "frame": {"width": 180, "height": 64},
                    "properties": {"FontFamily": "Inter", "Width": 140, "Height": 32, "IsTabStop": True},
                    "script": [{"action": "focus"}, {"action": "key_press", "key": "V", "modifiers": ["Control"]}],
                }]}), encoding="utf-8")
                with self.assertRaises(subprocess.CalledProcessError) as cm_lp:
                    ref_run.run_reference(exe, Path(tmp) / "out_paste", cases_path=paste_case, work_dir=build_tmp)
                self.assertIn("clipboard paste", cm_lp.exception.stderr)

                # Verify C# ReadFontCmapRanges + IntersectRanges on real synthetic SFNT format-4 .ttf and 2-face .ttc:
                # face 1 maps A..Z with 'M' (0x4D) explicitly mapped to glyph ID 0; face 2 maps A..F.
                import struct
                def _make_sfnt_fmt4(start_c, end_c, zero_c=None):
                    # Format 4 cmap with idRangeOffset pointing to glyphIdArray so zero_c maps to gid=0.
                    seg_count = 2
                    gids = [0 if c == zero_c else (c - start_c + 1) for c in range(start_c, end_c + 1)]
                    end_codes = struct.pack(">HH", end_c, 0xFFFF)
                    start_codes = struct.pack(">HH", start_c, 0xFFFF)
                    id_deltas = struct.pack(">hh", 0, 1)
                    # idRangeOffset for seg 0 = 4 (skips seg 0 + seg 1 idRangeOffset words into glyphIdArray)
                    id_range_offs = struct.pack(">HH", 4, 0)
                    glyph_arr = b"".join(struct.pack(">H", g) for g in gids)
                    sub_len = 14 + len(end_codes) + 2 + len(start_codes) + len(id_deltas) + len(id_range_offs) + len(glyph_arr)
                    sub = struct.pack(">HHHHHHH", 4, sub_len, 0, seg_count * 2, 2, 0, 2) + end_codes + b"\x00\x00" + start_codes + id_deltas + id_range_offs + glyph_arr
                    cmap = struct.pack(">HH", 0, 1) + struct.pack(">HHI", 3, 1, 12) + sub
                    return cmap
                def _wrap_ttf(cmap_bytes, base_off=0):
                    # 12-byte offset table + 16-byte table record + cmap
                    cmap_off = base_off + 28
                    hdr = struct.pack(">IHHHH", 0x00010000, 1, 16, 0, 0)
                    rec = b"cmap" + struct.pack(">III", 0, cmap_off, len(cmap_bytes))
                    return hdr + rec + cmap_bytes
                c1 = _make_sfnt_fmt4(0x41, 0x5A, zero_c=0x4D)
                c2 = _make_sfnt_fmt4(0x41, 0x46, zero_c=None)
                f1 = Path(build_tmp) / "face1.ttf"
                f2 = Path(build_tmp) / "face2.ttf"
                f1.write_bytes(_wrap_ttf(c1))
                f2.write_bytes(_wrap_ttf(c2))
                # Build a 2-face TTC containing face1 (offset 20) and face2.
                ttc_hdr_len = 12 + 8
                ttc_f1 = _wrap_ttf(c1, base_off=ttc_hdr_len)
                ttc_f2_off = ttc_hdr_len + len(ttc_f1)
                ttc_f2 = _wrap_ttf(c2, base_off=ttc_f2_off)
                ttc = Path(build_tmp) / "both.ttc"
                ttc.write_bytes(b"ttcf" + struct.pack(">IIII", 0x00010000, 2, ttc_hdr_len, ttc_f2_off) + ttc_f1 + ttc_f2)

                r_f1 = ref_run.run_cmd(str(exe), "--dump-cmap", ref_run.to_win_path(f1), capture_output=True, text=True)
                self.assertEqual(json.loads(r_f1.stdout), [[0x41, 0x4C], [0x4E, 0x5A]])
                r_both = ref_run.run_cmd(str(exe), "--dump-cmap", ref_run.to_win_path(f1), ref_run.to_win_path(f2), capture_output=True, text=True)
                self.assertEqual(json.loads(r_both.stdout), [[0x41, 0x46]])
                r_ttc = ref_run.run_cmd(str(exe), "--dump-cmap", ref_run.to_win_path(ttc), capture_output=True, text=True)
                self.assertEqual(json.loads(r_ttc.stdout), [[0x41, 0x46]])
                # Build synthetic .ttf with an OpenType name table ("Inter") + cmap (A..Z excluding 'M') and verify
                # --dump-font-info resolves family+ranges directly from the font binary and feeds publish_store.
                def _make_name_table(family):
                    enc = family.encode("utf-16-be")
                    rec = struct.pack(">HHHHHH", 3, 1, 0x0409, 16, len(enc), 0)
                    return struct.pack(">HHH", 0, 1, 6 + 12) + rec + enc
                nm = _make_name_table("Inter")
                nm_off = 12 + 32
                cm_off = nm_off + len(nm)
                hdr2 = struct.pack(">IHHHH", 0x00010000, 2, 32, 1, 0)
                rec_c = b"cmap" + struct.pack(">III", 0, cm_off, len(c1))
                rec_n = b"name" + struct.pack(">III", 0, nm_off, len(nm))
                f_named = Path(build_tmp) / "inter_named.ttf"
                f_named.write_bytes(hdr2 + rec_c + rec_n + nm + c1)
                r_info = ref_run.run_cmd(str(exe), "--dump-font-info", ref_run.to_win_path(f_named), capture_output=True, text=True)
                finfo = json.loads(r_info.stdout)
                self.assertEqual(finfo[0]["families"], ["inter"])
                self.assertEqual(finfo[0]["ranges"], [[0x41, 0x4C], [0x4E, 0x5A]])
                # Run the REAL normal RunCases host-manifest pipeline (no extra-font injection) using a dynamically
                # selected registered permitted open-source font discovered on this host (m1["installed_font_ranges"]),
                # selecting a verified covered ASCII character and verified uncovered Unicode codepoint from its
                # actual cmap ranges, then feed that real manifest directly into publish_store for both outcomes.
                host_ranges = m1.get("installed_font_ranges") or {}
                avail_fams = sorted(k for k, v in host_ranges.items() if isinstance(v, list) and v)
                if not avail_fams:
                    self.fail(
                        "No permitted open-source font (Inter, Barlow, JetBrains Mono, IBM Plex Sans/Mono, "
                        "DejaVu Sans/Mono, Liberation Sans/Mono, Noto Sans/Mono) is registered in Windows Fonts; "
                        "install one (e.g. Inter or Barlow) to run the live host-to-publication font checkpoint."
                    )
                chosen_fam = avail_fams[0]
                chosen_ranges = host_ranges[chosen_fam]
                covered_cp = next(
                    (cp for cp in range(0x41, 0x5B) if ref_run._codepoint_in_ranges(cp, chosen_ranges)),
                    chosen_ranges[0][0],
                )
                uncovered_cp = next(
                    cp for cp in (0x4E2D, 0x2603, 0x05D0, 0x0627, 0x3042, 0xE000)
                    if not ref_run._codepoint_in_ranges(cp, chosen_ranges)
                )
                font_ok_case = {
                    "id": "font-ok", "control": "TextBlock", "theme": "Dark", "scale": 1.0,
                    "frame": {"width": 120, "height": 40},
                    "properties": {"Text": chr(covered_cp), "FontFamily": chosen_fam},
                }
                font_miss_case = {
                    "id": "font-miss", "control": "TextBlock", "theme": "Dark", "scale": 1.0,
                    "frame": {"width": 120, "height": 40},
                    "properties": {"Text": chr(uncovered_cp), "FontFamily": chosen_fam},
                }
                for case_obj, should_pass in ((font_ok_case, True), (font_miss_case, False)):
                    cf_path = Path(build_tmp) / f"{case_obj['id']}.json"
                    cf_path.write_text(json.dumps({"cases": [case_obj]}), encoding="utf-8")
                    out_f = Path(build_tmp) / f"out_{case_obj['id']}"
                    ref_run.run_cmd(
                        str(exe), ref_run.to_win_path(out_f),
                        "--cases", ref_run.to_win_path(cf_path),
                        check=True,
                    )
                    m_live_f = json.loads((out_f / "cases.json").read_text(encoding="utf-8"))
                    self.assertIn(chosen_fam, m_live_f["installed_fonts"])
                    self.assertTrue(m_live_f["installed_font_ranges"].get(chosen_fam))
                    store_f = Path(tmp) / f"store_{case_obj['id']}"
                    if should_pass:
                        ref_run.publish_store(out_f, store_f, {case_obj["id"]: case_obj})
                        self.assertTrue((store_f / "cases.json").is_file())
                    else:
                        with self.assertRaises(ValueError) as cm_gid0:
                            ref_run.publish_store(out_f, store_f, {case_obj["id"]: case_obj})
                        self.assertIn(f"U+{uncovered_cp:04X}", str(cm_gid0.exception))

                # Class-level clipboard isolation regressions (zero host clipboard access):
                # 1. Structural invariant: reference C# sources contain no Win32/WinRT desktop clipboard APIs.
                for cs_rel in ("reference/Script.cs", "reference/App.xaml.cs", "reference/Dump.cs"):
                    cs_src = (ref_run.ROOT / cs_rel).read_text(encoding="utf-8")
                    for forbidden_api in (
                        "OpenClipboard", "EmptyClipboard", "GetClipboardData", "SetClipboardData",
                        "DataTransfer.Clipboard", "Clipboard.GetContent", "Clipboard.SetContent",
                    ):
                        self.assertNotIn(forbidden_api, cs_src, f"{cs_rel} must not touch the shared desktop clipboard ({forbidden_api})")

                # 2. Panel-root focused child TextBox copy/paste, Shift+Delete cut/paste, and multiline RichEditBox
                # with mixed RTF formatting (AllCaps on line1 + normal Line2 must survive copy and paste intact).
                iso_cases = [
                    {
                        "id": "panel-child-copy-paste-live",
                        "control": "StackPanel",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 200, "height": 80},
                        "children": [
                            {
                                "id": "child-tb",
                                "control": "TextBox",
                                "properties": {"Name": "ChildTb", "Text": "PanHi", "FontFamily": chosen_fam, "Width": 160, "Height": 40},
                            }
                        ],
                        "script": [
                            {"action": "pointer_click", "x": 80, "y": 20},
                            {"action": "key_press", "key": "A", "modifiers": ["Control"], "use_focused": True},
                            {"action": "key_press", "key": "C", "modifiers": ["Control"], "use_focused": True},
                            {"action": "key_press", "key": "Right", "use_focused": True},
                            {"action": "key_press", "key": "V", "modifiers": ["Control"], "use_focused": True},
                        ],
                    },
                    {
                        "id": "tb-shift-delete-cut-paste-live",
                        "control": "TextBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 64},
                        "properties": {"Text": "CutMe", "FontFamily": chosen_fam},
                        "script": [
                            {"action": "focus"},
                            {"action": "key_press", "key": "A", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "Delete", "modifiers": ["Shift"]},
                            {"action": "key_press", "key": "V", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "V", "modifiers": ["Control"]},
                        ],
                    },
                    {
                        "id": "reb-multiline-copy-paste-live",
                        "control": "RichEditBox",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 220, "height": 100},
                        "rich_text_rtf": r"{\rtf1\ansi {\caps line1}\par Line2\par}",
                        "properties": {"FontFamily": chosen_fam, "AcceptsReturn": True},
                        "script": [
                            {"action": "focus"},
                            {"action": "key_press", "key": "A", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "C", "modifiers": ["Control"]},
                            {"action": "key_press", "key": "Right"},
                            {"action": "key_press", "key": "V", "modifiers": ["Control"]},
                        ],
                    },
                ]
                iso_cf = Path(build_tmp) / "iso_clip_cases.json"
                iso_cf.write_text(json.dumps({"cases": iso_cases}), encoding="utf-8")
                out_iso = ref_run.run_reference(exe, Path(tmp) / "out_iso_clip", cases_path=iso_cf, work_dir=build_tmp)
                m_iso = json.loads((out_iso / "cases.json").read_text(encoding="utf-8"))
                by_iso = {c["id"]: c for c in m_iso["cases"]}
                self.assertIn("Text=PanHiPanHi", by_iso["panel-child-copy-paste-live"]["script_log"]["FinalState"].get("focused_state", ""))
                self.assertEqual(by_iso["tb-shift-delete-cut-paste-live"]["script_log"]["FinalState"].get("text"), "CutMeCutMe")
                reb_texts = [rt["text"] for rt in by_iso["reb-multiline-copy-paste-live"]["rendered_text"]]
                self.assertGreaterEqual(reb_texts.count("LINE1"), 2, f"RichEditBox AllCaps RTF formatting did not survive copy/paste: {reb_texts!r}")
                self.assertGreaterEqual(reb_texts.count("Line2"), 2, f"multiline RichEditBox paste missing duplicated Line2: {reb_texts!r}")

                # 3. Read-only copy source refusal for BOTH TextBox and RichEditBox with a writable sibling destination
                # (proves refusal happens at the read-only source, not merely at a read-only paste destination).
                for src_ctrl, src_props in (
                    ("TextBox", {"Name": "RoSrc", "Text": "RO", "IsReadOnly": True, "FontFamily": chosen_fam, "Width": 140, "Height": 32}),
                    ("RichEditBox", {"Name": "RoSrc", "IsReadOnly": True, "FontFamily": chosen_fam, "Width": 140, "Height": 32}),
                ):
                    ro_src_case = {
                        "id": f"ro-src-{src_ctrl.lower()}",
                        "control": "StackPanel",
                        "theme": "Dark",
                        "scale": 1.0,
                        "frame": {"width": 180, "height": 90},
                        "children": [
                            {"id": "ro-child", "control": src_ctrl, "properties": src_props,
                             **({"rich_text_rtf": r"{\rtf1\ansi RO\par}"} if src_ctrl == "RichEditBox" else {})},
                            {"id": "rw-child", "control": "TextBox",
                             "properties": {"Name": "RwDst", "Text": "", "FontFamily": chosen_fam, "Width": 140, "Height": 32}},
                        ],
                        "script": [
                            {"action": "pointer_click", "x": 70, "y": 16},
                            {"action": "key_press", "key": "A", "modifiers": ["Control"], "use_focused": True},
                            {"action": "key_press", "key": "C", "modifiers": ["Control"], "use_focused": True},
                            {"action": "key_press", "key": "Tab", "use_focused": True},
                            {"action": "key_press", "key": "V", "modifiers": ["Control"], "use_focused": True},
                        ],
                    }
                    ro_cf = Path(build_tmp) / f"ro_src_{src_ctrl.lower()}.json"
                    ro_cf.write_text(json.dumps({"cases": [ro_src_case]}), encoding="utf-8")
                    out_ro = Path(build_tmp) / f"out_ro_src_{src_ctrl.lower()}"
                    ro_res = ref_run.run_cmd(
                        str(exe), ref_run.to_win_path(out_ro),
                        "--cases", ref_run.to_win_path(ro_cf),
                        capture_output=True, text=True, check=False,
                    )
                    self.assertNotEqual(ro_res.returncode, 0, f"copy from read-only {src_ctrl} must fail at source")
                    ro_err = (out_ro / "error.txt").read_text(encoding="utf-8")
                    self.assertIn("read-only", ro_err.lower(), f"expected read-only source error for {src_ctrl}, got: {ro_err}")

                # 4. Combined transition + destination storyboard duration exceeding one shared storyboard_timeout_ms must fail.
                comb_to_tpl = (
                    "<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' "
                    "xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' TargetType='Button'>"
                    "<Grid x:Name='RootGrid' Width='100' Height='32'>"
                    "<VisualStateManager.VisualStateGroups><VisualStateGroup x:Name='CommonStates'>"
                    "<VisualStateGroup.Transitions>"
                    "<VisualTransition From='Normal' To='Pressed'>"
                    "<Storyboard><DoubleAnimation Storyboard.TargetName='RootGrid' Storyboard.TargetProperty='Opacity' "
                    "To='0.5' Duration='0:0:0.25'/></Storyboard>"
                    "</VisualTransition>"
                    "</VisualStateGroup.Transitions>"
                    "<VisualState x:Name='Normal'/>"
                    "<VisualState x:Name='Pressed'><Storyboard>"
                    "<DoubleAnimation Storyboard.TargetName='RootGrid' Storyboard.TargetProperty='Opacity' "
                    "To='1.0' Duration='0:0:0.25'/>"
                    "</Storyboard></VisualState>"
                    "</VisualStateGroup></VisualStateManager.VisualStateGroups>"
                    "<Border Background='#FF0000FF'/></Grid></ControlTemplate>"
                )
                comb_cf = Path(build_tmp) / "comb_sb_timeout.json"
                comb_cf.write_text(json.dumps({"cases": [{
                    "id": "btn-combined-timeout", "control": "Button", "theme": "Dark", "scale": 1.0,
                    "state": "Pressed", "use_transitions": True, "storyboard_timeout_ms": 320,
                    "frame": {"width": 120, "height": 48}, "template_xaml": comb_to_tpl,
                }]}), encoding="utf-8")
                out_comb = Path(build_tmp) / "out_comb_timeout"
                comb_res = ref_run.run_cmd(
                    str(exe), ref_run.to_win_path(out_comb),
                    "--cases", ref_run.to_win_path(comb_cf),
                    capture_output=True, text=True, check=False,
                )
                self.assertNotEqual(comb_res.returncode, 0, "combined transition + state storyboard exceeding timeout must fail")
                comb_err = (out_comb / "error.txt").read_text(encoding="utf-8")
                self.assertIn("btn-combined-timeout", comb_err)
                self.assertIn("Pressed", comb_err)

                # 5. Clipboard chord on non-text Button must be refused before SendKey.
                btn_clip_cf = Path(build_tmp) / "btn_clip_refused.json"
                btn_clip_cf.write_text(json.dumps({"cases": [{
                    "id": "btn-copy-refused", "control": "Button", "theme": "Dark", "scale": 1.0,
                    "frame": {"width": 120, "height": 48},
                    "script": [{"action": "focus"}, {"action": "key_press", "key": "C", "modifiers": ["Control"]}],
                }]}), encoding="utf-8")
                out_btn_clip = Path(build_tmp) / "out_btn_clip_refused"
                btn_clip_res = ref_run.run_cmd(
                    str(exe), ref_run.to_win_path(out_btn_clip),
                    "--cases", ref_run.to_win_path(btn_clip_cf),
                    capture_output=True, text=True, check=False,
                )
                self.assertNotEqual(btn_clip_res.returncode, 0, "clipboard chord on non-text Button must be refused")
                self.assertIn("intercepted TextBox or RichEditBox", (out_btn_clip / "error.txt").read_text(encoding="utf-8"))

                # Scripted button gives identical event + focus log across two independent runs,
                # with touch_tap GotFocus settled before Click (deterministic ordering across refreshes).
                validate_script_log(by1["btn-script-live"]["script_log"])
                self.assertEqual(by1["btn-script-live"]["script_log"], by2["btn-script-live"]["script_log"])
                touch_evts = [e["Event"] for e in by1["btn-script-live"]["script_log"]["Events"] if e["Step"] == "3:touch_tap"]
                self.assertIn("GotFocus", touch_evts)
                self.assertIn("Click", touch_evts)
                self.assertLess(touch_evts.index("GotFocus"), touch_evts.index("Click"))

    def test_live_five_scale_panel_layouts_match_oracle(self):
        sys.path.insert(0, str(ROOT / "reference"))
        sys.path.insert(0, str(ROOT / "tests"))
        import run as ref_run
        import layout_cases

        oracle = json.loads((ROOT / "tests/fixtures/layout_reference.json").read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory(dir=ref_run.host_temp_root(), prefix="impossible-live-layout-") as build_tmp:
            exe = ref_run.build_reference(build_tmp)
            cases_file = Path(build_tmp) / "layout_cases.json"
            cases_file.write_text(json.dumps({"cases": layout_cases.build_winui_cases()}), encoding="utf-8")
            out_dir = Path(build_tmp) / "out"
            ref_run.run_reference(exe, out_dir, cases_path=cases_file, work_dir=build_tmp)
            manifest = json.loads((out_dir / "cases.json").read_text(encoding="utf-8"))
            by_id = {c["id"]: c for c in manifest["cases"]}

            all_diffs = []
            for scale in layout_cases.SCALES:
                tag = layout_cases.scale_tag(scale)
                expected_mode = "native-1x" if abs(scale - 1.0) < 1e-6 else "scaled-inputs"
                for base_name in layout_cases.REQUIRED_NODES:
                    cid = f"{base_name}-{tag}"
                    self.assertIn(cid, by_id, f"live WinUI missing {cid}")
                    entry = by_id[cid]
                    self.assertEqual(entry.get("layout_capture_mode"), expected_mode, cid)
                    self.assertAlmostEqual(float(entry.get("layout_scale", 0)), scale, places=3, msg=cid)
                    actual_nodes = layout_cases.index_named_nodes(entry["layout"])
                    diffs = layout_cases.compare_named_layouts(
                        cid, base_name, oracle[cid]["nodes"], actual_nodes, tol=0.05
                    )
                    all_diffs.extend(diffs)
            self.assertEqual(all_diffs, [], "WinUI vs ui/layout.c mismatches:\n" + "\n".join(all_diffs[:60]))

    def test_live_clip_and_opacity_matches_sdk(self):
        sys.path.insert(0, str(ROOT / "reference"))
        import run as ref_run

        # Render the exact same rounded-clip + 0.5 group-opacity overlapping red+blue scene in SDK and WinUI
        # at 100%, 150%, 200%, 300%, and compare EVERY pixel (straight/interior <= 2, rounded corners <= 40).
        shots_exe = ROOT / "build" / "linux" / "control-shots.exe"
        if not shots_exe.exists():
            shots_exe = ROOT / "build" / "control-shots.exe"
        self.assertTrue(shots_exe.exists(), f"missing control-shots.exe at {shots_exe}")
        sdk_raw = subprocess.check_output([str(shots_exe), "--clip-shots"])
        scale_defs = ((1.0, "s100", 64), (1.5, "s150", 96), (2.0, "s200", 128), (3.0, "s300", 192))
        off = 2 * sum(dim * dim * 4 for _, _, dim in scale_defs)  # skip probe + overlap group-opacity frames
        sdk_scenes = {}
        for sc, tag, dim in scale_defs:
            nbytes = dim * dim * 4
            sdk_scenes[tag] = sdk_raw[off:off + nbytes]
            off += nbytes
        self.assertEqual(off, len(sdk_raw))

        cases = []
        for sc, tag, _ in scale_defs:
            cases.append({
                "id": f"clip-opacity-{tag}",
                "case_group": "clip",
                "control": "Border",
                "theme": "Dark",
                "scale": sc,
                "layout_at_scale": True,
                "frame": {"width": 64, "height": 64, "background": "#FF202020"},
                "properties": {
                    "Width": 40,
                    "Height": 40,
                    "CornerRadius": 8,
                    "Background": "#FF183048",
                    "HorizontalAlignment": "Center",
                    "VerticalAlignment": "Center",
                },
                "children": [
                    {
                        "id": "op-group",
                        "control": "Grid",
                        "properties": {
                            "Width": 40, "Height": 40,
                            "Opacity": 0.5,
                        },
                        "children": [
                            {
                                "id": "red-half",
                                "control": "Border",
                                "properties": {
                                    "Width": 20, "Height": 40,
                                    "HorizontalAlignment": "Left", "VerticalAlignment": "Top",
                                    "Background": "#FF602020",
                                },
                            },
                            {
                                "id": "blue-half",
                                "control": "Border",
                                "properties": {
                                    "Width": 20, "Height": 40,
                                    "Margin": [20, 0, 0, 0],
                                    "HorizontalAlignment": "Left", "VerticalAlignment": "Top",
                                    "Background": "#FF202060",
                                },
                            },
                        ],
                    },
                ],
            })
        with tempfile.TemporaryDirectory(dir=ref_run.host_temp_root(), prefix="impossible-live-clip-") as build_tmp:
            exe = ref_run.build_reference(build_tmp)
            cf = Path(build_tmp) / "clip_cases.json"
            cf.write_text(json.dumps({"cases": cases}), encoding="utf-8")
            out_dir = Path(build_tmp) / "out"
            ref_run.run_reference(exe, out_dir, cases_path=cf, work_dir=build_tmp)
            for sc, tag, dim in scale_defs:
                winui = (out_dir / f"clip-opacity-{tag}.bgra").read_bytes()
                sdk = sdk_scenes[tag]
                self.assertEqual(len(winui), len(sdk), tag)
                # Rounded clip corner at (12*sc, 12*sc) must stay frame background #202020 in both renders.
                cx0, cy0 = int(round(12 * sc)), int(round(12 * sc))
                self.assertEqual(winui[(cy0 * dim + cx0) * 4:(cy0 * dim + cx0) * 4 + 3], bytes([0x20, 0x20, 0x20]), tag)
                self.assertEqual(sdk[(cy0 * dim + cx0) * 4:(cy0 * dim + cx0) * 4 + 3], bytes([0x20, 0x20, 0x20]), tag)
                b0, b1 = int(round(12 * sc)), int(round(52 * sc))
                cr = int(round(8 * sc)) + 1
                worst_straight = 0
                worst_corner = 0
                for y in range(dim):
                    for x in range(dim):
                        in_corner = (
                            (b0 - 1 <= x < b0 + cr or b1 - cr <= x <= b1)
                            and (b0 - 1 <= y < b0 + cr or b1 - cr <= y <= b1)
                        )
                        idx = (y * dim + x) * 4
                        d = max(abs(winui[idx + c] - sdk[idx + c]) for c in range(3))
                        if in_corner:
                            if d > worst_corner:
                                worst_corner = d
                        else:
                            if d > worst_straight:
                                worst_straight = d
                self.assertLessEqual(worst_straight, 2, f"{tag} straight/group-opacity diff={worst_straight}")
                self.assertLessEqual(worst_corner, 40, f"{tag} rounded-corner diff={worst_corner}")

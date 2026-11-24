#!/usr/bin/env python3
"""Compile WinUI control templates (MIT) at the pinned commit into ui/template_data.{h,c}.

    python3 tools/winui/templates.py <checkout>                   writes ui/template_data.{h,c}
    python3 tools/winui/templates.py <checkout> --check           fails when generated files differ
    python3 tools/winui/templates.py <checkout> --list            prints round-trip listing
    python3 tools/winui/templates.py --parse-file <fixture.xaml>  validates a standalone XAML fixture
"""
import argparse
import bisect
import io
import json
import re
import subprocess
import sys
import tarfile
import tempfile
import xml.parsers.expat as expat
from fractions import Fraction
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(HERE))
import resources as res_mod

PIN = res_mod.PIN

# Portable SDK icon map for WinUI SymbolThemeFontFamily private-use glyphs.
ICON_GLYPHS = {
    0xE711: "IM_ICON_DISMISS",
    0xE894: "IM_ICON_DISMISS",
    0xE73E: "IM_ICON_CHECKMARK",
}

# Element tags accepted by the portable engine.
ELEMENT_KINDS = {
    "Grid": "IM_ELEM_GRID",
    "Border": "IM_ELEM_BORDER",
    "ContentPresenter": "IM_ELEM_CONTENT_PRESENTER",
    "Rectangle": "IM_ELEM_RECTANGLE",
    "TextBlock": "IM_ELEM_TEXT_BLOCK",
    "Button": "IM_ELEM_BUTTON",
    "ScrollViewer": "IM_ELEM_SCROLL_VIEWER",
    "Thumb": "IM_ELEM_THUMB",
    "FontIcon": "IM_ELEM_FONT_ICON",
    "ListViewItemPresenter": "IM_ELEM_LIST_VIEW_ITEM_PRESENTER",
    "ItemsStackPanel": "IM_ELEM_ITEMS_STACK_PANEL",
    "ItemsPresenter": "IM_ELEM_ITEMS_PRESENTER",
    "RowDefinition": "IM_ELEM_ROW_DEFINITION",
    "ColumnDefinition": "IM_ELEM_COLUMN_DEFINITION",
    "TranslateTransform": "IM_ELEM_TRANSLATE_TRANSFORM",
    "ScaleTransform": "IM_ELEM_SCALE_TRANSFORM",
    "CompositeTransform": "IM_ELEM_COMPOSITE_TRANSFORM",
    "RectangleGeometry": "IM_ELEM_RECTANGLE_GEOMETRY",
    "BrushTransition": "IM_ELEM_BRUSH_TRANSITION",
    "AddDeleteThemeTransition": "IM_ELEM_ADD_DELETE_THEME_TRANSITION",
    "ContentThemeTransition": "IM_ELEM_CONTENT_THEME_TRANSITION",
    "ReorderThemeTransition": "IM_ELEM_REORDER_THEME_TRANSITION",
    "EntranceThemeTransition": "IM_ELEM_ENTRANCE_THEME_TRANSITION",
}

# Property-element wrapper tags accepted inside element subtrees.
PROP_ELEMENT_TAGS = {
    "Border.Clip",
    "Border.RenderTransform",
    "ContentPresenter.BackgroundTransition",
    "Grid.ColumnDefinitions",
    "Grid.RenderTransform",
    "Grid.Resources",
    "Grid.RowDefinitions",
    "Rectangle.RenderTransform",
    "RectangleGeometry.Transform",
    "Thumb.Template",
    "VisualStateManager.VisualStateGroups",
}

ANIMATION_KINDS = {
    "ObjectAnimationUsingKeyFrames": "IM_ANIM_OBJECT_KEYFRAMES",
    "DoubleAnimation": "IM_ANIM_DOUBLE",
    "DoubleAnimationUsingKeyFrames": "IM_ANIM_DOUBLE_KEYFRAMES",
    "ColorAnimationUsingKeyFrames": "IM_ANIM_COLOR_KEYFRAMES",
    "RepositionThemeAnimation": "IM_ANIM_REPOSITION_THEME",
    "FadeInThemeAnimation": "IM_ANIM_FADE_IN_THEME",
    "FadeOutThemeAnimation": "IM_ANIM_FADE_OUT_THEME",
    "DragItemThemeAnimation": "IM_ANIM_DRAG_ITEM_THEME",
    "DragOverThemeAnimation": "IM_ANIM_DRAG_OVER_THEME",
    "DropTargetItemThemeAnimation": "IM_ANIM_DROP_TARGET_THEME",
}

KEYFRAME_KINDS = {
    "DiscreteObjectKeyFrame": "IM_KF_DISCRETE",
    "LinearDoubleKeyFrame": "IM_KF_LINEAR_DOUBLE",
    "SplineDoubleKeyFrame": "IM_KF_SPLINE_DOUBLE",
    "EasingDoubleKeyFrame": "IM_KF_EASING_DOUBLE",
    "LinearColorKeyFrame": "IM_KF_LINEAR_COLOR",
}

ANIMATION_ATTRS = {
    "DoubleAnimation": {"Storyboard.TargetName", "Storyboard.TargetProperty", "Duration", "From", "To", "EnableDependentAnimation"},
    "DoubleAnimationUsingKeyFrames": {"Storyboard.TargetName", "Storyboard.TargetProperty", "Duration", "EnableDependentAnimation"},
    "ObjectAnimationUsingKeyFrames": {"Storyboard.TargetName", "Storyboard.TargetProperty", "Duration"},
    "ColorAnimationUsingKeyFrames": {"Storyboard.TargetName", "Storyboard.TargetProperty", "Duration", "EnableDependentAnimation"},
    "RepositionThemeAnimation": {"TargetName", "Duration", "FromHorizontalOffset", "FromVerticalOffset"},
    "FadeInThemeAnimation": {"TargetName", "Duration"},
    "FadeOutThemeAnimation": {"TargetName", "Duration"},
    "DragItemThemeAnimation": {"TargetName", "Duration"},
    "DragOverThemeAnimation": {"TargetName", "Duration", "Direction", "ToOffset"},
    "DropTargetItemThemeAnimation": {"TargetName", "Duration"},
}

ANIMATION_KEYFRAMES = {
    "ObjectAnimationUsingKeyFrames": {"DiscreteObjectKeyFrame"},
    "DoubleAnimationUsingKeyFrames": {"LinearDoubleKeyFrame", "SplineDoubleKeyFrame", "EasingDoubleKeyFrame"},
    "ColorAnimationUsingKeyFrames": {"LinearColorKeyFrame"},
}

KEYFRAME_ATTRS = {
    "DiscreteObjectKeyFrame": {"KeyTime", "Value"},
    "LinearDoubleKeyFrame": {"KeyTime", "Value"},
    "SplineDoubleKeyFrame": {"KeyTime", "Value", "KeySpline"},
    "EasingDoubleKeyFrame": {"KeyTime", "Value"},
    "LinearColorKeyFrame": {"KeyTime", "Value"},
}

BINDING_MODES = {"OneWay", "TwoWay", "OneTime"}

# Attached properties legal on UI elements and as parenthesized targets in VisualState.Setters.
ATTACHED_PROPS = {
    "AnimatedIcon.State", "AutomationProperties.AccessibilityView", "Canvas.ZIndex",
    "Control.IsTemplateFocusTarget", "Grid.Column", "Grid.ColumnSpan", "Grid.Row", "Grid.RowSpan",
    "ScrollViewer.BringIntoViewOnFocusChange", "ScrollViewer.HorizontalScrollBarVisibility",
    "ScrollViewer.HorizontalScrollMode", "ScrollViewer.IsDeferredScrollingEnabled",
    "ScrollViewer.IsHorizontalRailEnabled", "ScrollViewer.IsHorizontalScrollChainingEnabled",
    "ScrollViewer.IsVerticalRailEnabled", "ScrollViewer.IsVerticalScrollChainingEnabled",
    "ScrollViewer.VerticalScrollBarVisibility", "ScrollViewer.VerticalScrollMode",
    "ScrollViewer.ZoomMode",
}

# XAML directives legal only with the x: namespace (not assignable via VisualState.Setters).
XAML_DIRECTIVE_ATTRS = {"DeferLoadStrategy", "Key", "Load"}

# Properties declared on UIElement (from WinUI XamlOM).
UI_ELEMENT_PROPS = {
    "AccessKey", "AccessKeyScopeOwner", "AllowDrop", "CacheMode", "CanBeScrollAnchor",
    "CanDrag", "CenterPoint", "Clip", "CompositeMode", "ContextFlyout",
    "ExitDisplayModeOnAccessKeyInvoked", "HighContrastAdjustment", "IsAccessKeyScope",
    "IsDoubleTapEnabled", "IsHitTestVisible", "IsHoldingEnabled", "IsRightTapEnabled",
    "IsTabStop", "IsTapEnabled", "KeyTipHorizontalOffset", "KeyTipPlacementMode",
    "KeyTipTarget", "KeyTipVerticalOffset", "KeyboardAcceleratorPlacementMode",
    "KeyboardAcceleratorPlacementTarget", "ManipulationMode", "Opacity", "OpacityTransition",
    "Projection", "RenderTransform", "RenderTransformOrigin", "Rotation", "RotationAxis",
    "RotationTransition", "Scale", "ScaleTransition", "Shadow", "TabFocusNavigation",
    "TabIndex", "Transform3D", "TransformMatrix", "Transitions", "Translation",
    "TranslationTransition", "UseLayoutRounding", "UseSystemFocusVisuals", "Visibility",
    "XYFocusDown", "XYFocusDownNavigationStrategy", "XYFocusKeyboardNavigation",
    "XYFocusLeft", "XYFocusLeftNavigationStrategy", "XYFocusRight",
    "XYFocusRightNavigationStrategy", "XYFocusUp", "XYFocusUpNavigationStrategy",
}

# Properties declared on FrameworkElement (from WinUI XamlOM).
FRAMEWORK_ELEMENT_PROPS = {
    "AllowFocusOnInteraction", "AllowFocusWhenDisabled", "DataContext", "FlowDirection",
    "FocusVisualMargin", "FocusVisualPrimaryBrush", "FocusVisualPrimaryThickness",
    "FocusVisualSecondaryBrush", "FocusVisualSecondaryThickness", "Height",
    "HorizontalAlignment", "Language", "Margin", "MaxHeight", "MaxWidth",
    "MinHeight", "MinWidth", "RequestedTheme", "Style", "Tag", "VerticalAlignment", "Width",
}

# Properties inherited by all UIElement / FrameworkElement controls and panels.
COMMON_UI_PROPS = UI_ELEMENT_PROPS | FRAMEWORK_ELEMENT_PROPS

# Properties inherited by all Control subclasses (from WinUI XamlOM).
CONTROL_PROPS = {
    "Background", "BackgroundSizing", "BorderBrush", "BorderThickness", "CharacterSpacing",
    "CornerRadius", "DefaultStyleResourceUri", "ElementSoundMode", "FontFamily", "FontSize",
    "FontStretch", "FontStyle", "FontWeight", "Foreground", "HorizontalContentAlignment",
    "IsEnabled", "IsFocusEngaged", "IsFocusEngagementEnabled", "IsTabStop",
    "IsTextScaleFactorEnabled", "Padding", "RequiresPointer", "TabNavigation", "Template",
    "VerticalContentAlignment",
}

# Properties inherited by ContentControl subclasses (Button, ScrollViewer, ListViewItem).
CONTENT_CONTROL_PROPS = CONTROL_PROPS | {
    "Content", "ContentTemplate", "ContentTemplateSelector", "ContentTransitions",
}

# Properties declared on Panel (from WinUI XamlOM: Grid and ItemsStackPanel derive from Panel).
PANEL_PROPS = {
    "Background", "BackgroundTransition", "ChildrenTransitions",
}

# Properties declared on Border (from WinUI XamlOM).
BORDER_PROPS = {
    "Background", "BackgroundSizing", "BackgroundTransition", "BorderBrush",
    "BorderThickness", "Child", "ChildTransitions", "CornerRadius", "Padding",
}

# Properties declared on ContentPresenter (inherited by ListViewItemPresenter).
CONTENT_PRESENTER_PROPS = {
    "Background", "BackgroundSizing", "BackgroundTransition", "BorderBrush",
    "BorderThickness", "CharacterSpacing", "Content", "ContentTemplate",
    "ContentTemplateSelector", "ContentTransitions", "CornerRadius", "FontFamily",
    "FontSize", "FontStretch", "FontStyle", "FontWeight", "Foreground",
    "HorizontalContentAlignment", "IsTextScaleFactorEnabled", "LineHeight",
    "LineStackingStrategy", "MaxLines", "OpticalMarginAlignment", "Padding",
    "TextLineBounds", "TextWrapping", "VerticalContentAlignment",
}

# Element-specific properties (UI elements also inherit COMMON_UI_PROPS | ATTACHED_PROPS).
NON_UI_ELEMENTS = {
    "RowDefinition", "ColumnDefinition", "TranslateTransform", "ScaleTransform",
    "CompositeTransform", "RectangleGeometry", "BrushTransition",
    "AddDeleteThemeTransition", "ContentThemeTransition", "ReorderThemeTransition",
    "EntranceThemeTransition",
}

ELEMENT_OWN_PROPS = {
    "Grid": PANEL_PROPS | {
        "BackgroundSizing", "BorderBrush", "BorderThickness", "ColumnSpacing",
        "CornerRadius", "Padding", "RowSpacing",
    },
    "Border": BORDER_PROPS,
    "ContentPresenter": CONTENT_PRESENTER_PROPS,
    "Rectangle": {
        "Fill", "RadiusX", "RadiusY", "Stretch", "Stroke", "StrokeDashArray",
        "StrokeDashCap", "StrokeDashOffset", "StrokeEndLineCap", "StrokeLineJoin",
        "StrokeMiterLimit", "StrokeStartLineCap", "StrokeThickness",
    },
    "TextBlock": {
        "CharacterSpacing", "FontFamily", "FontSize", "FontStretch", "FontStyle", "FontWeight",
        "Foreground", "IsTextScaleFactorEnabled", "Padding", "Text", "TextAlignment", "TextWrapping",
    },
    "Button": CONTENT_CONTROL_PROPS | {"ClickMode", "Command", "CommandParameter", "Flyout"},
    "ScrollViewer": CONTENT_CONTROL_PROPS | {
        "BringIntoViewOnFocusChange", "CanContentRenderOutsideBounds",
        "HorizontalAnchorRatio", "HorizontalScrollBarVisibility", "HorizontalScrollMode",
        "HorizontalSnapPointsAlignment", "HorizontalSnapPointsType",
        "IsDeferredScrollingEnabled", "IsHorizontalRailEnabled", "IsHorizontalScrollChainingEnabled",
        "IsScrollInertiaEnabled", "IsVerticalRailEnabled", "IsVerticalScrollChainingEnabled",
        "IsZoomChainingEnabled", "IsZoomInertiaEnabled", "LeftHeader", "MaxZoomFactor",
        "MinZoomFactor", "TopHeader", "TopLeftHeader", "VerticalAnchorRatio",
        "VerticalScrollBarVisibility", "VerticalScrollMode", "VerticalSnapPointsAlignment",
        "VerticalSnapPointsType", "ZoomMode", "ZoomSnapPoints", "ZoomSnapPointsType",
    },
    "Thumb": CONTROL_PROPS,
    "ToggleSwitch": CONTROL_PROPS | {
        "Header", "HeaderPlacement", "HeaderTemplate", "IsOn",
        "OffContent", "OffContentTemplate", "OnContent", "OnContentTemplate",
    },
    "TextBox": CONTROL_PROPS | {
        "AcceptsReturn", "CharacterCasing", "Description", "DesiredCandidateWindowAlignment",
        "Header", "HeaderPlacement", "HeaderTemplate", "HorizontalTextAlignment", "InputScope",
        "IsColorFontEnabled", "IsReadOnly", "IsSpellCheckEnabled", "IsTextPredictionEnabled",
        "MaxLength", "PlaceholderForeground", "PlaceholderText",
        "PreventKeyboardDisplayOnProgrammaticFocus", "SelectedText",
        "SelectionFlyout", "SelectionHighlightColor", "SelectionHighlightColorWhenNotFocused",
        "SelectionLength", "SelectionStart", "Text", "TextAlignment", "TextReadingOrder",
        "TextWrapping",
    },
    "ListView": CONTROL_PROPS | {
        "CanDragItems", "CanReorderItems", "DataFetchSize", "DisplayMemberPath",
        "Footer", "FooterTemplate", "FooterTransitions", "GroupStyleSelector",
        "Header", "HeaderTemplate", "HeaderTransitions", "IncrementalLoadingThreshold",
        "IncrementalLoadingTrigger", "IsItemClickEnabled", "IsMultiSelectCheckBoxEnabled",
        "IsSwipeEnabled", "IsSynchronizedWithCurrentItem", "ItemContainerStyle",
        "ItemContainerStyleSelector", "ItemContainerTransitions", "ItemTemplate",
        "ItemTemplateSelector", "ItemsPanel", "ItemsSource", "ReorderMode",
        "SelectedIndex", "SelectedItem", "SelectedValue", "SelectedValuePath",
        "SelectionMode", "ShowsScrollingPlaceholders", "SingleSelectionFollowsFocus",
    },
    "ListViewItem": CONTENT_CONTROL_PROPS | {"IsSelected"},
    "FontIcon": {
        "FontFamily", "FontSize", "FontStyle", "FontWeight", "Foreground", "Glyph",
        "IsTextScaleFactorEnabled", "MirroredWhenRightToLeft",
    },
    "ListViewItemPresenter": CONTENT_PRESENTER_PROPS | {
        "CheckBoxBorderBrush", "CheckBoxBrush", "CheckBoxCornerRadius", "CheckBoxDisabledBorderBrush",
        "CheckBoxDisabledBrush", "CheckBoxPointerOverBorderBrush", "CheckBoxPointerOverBrush",
        "CheckBoxPressedBorderBrush", "CheckBoxPressedBrush", "CheckBoxSelectedBrush",
        "CheckBoxSelectedDisabledBrush", "CheckBoxSelectedPointerOverBrush",
        "CheckBoxSelectedPressedBrush", "CheckBrush", "CheckDisabledBrush", "CheckMode",
        "CheckPressedBrush", "ContentMargin", "ContentTransitions",
        "DisabledOpacity", "DragBackground", "DragForeground", "DragOpacity", "FocusBorderBrush",
        "FocusSecondaryBorderBrush", "HorizontalContentAlignment", "PlaceholderBackground",
        "PointerOverBackground", "PointerOverForeground", "PressedBackground", "ReorderHintOffset",
        "SelectedBackground", "SelectedDisabledBackground", "SelectedForeground",
        "SelectedPointerOverBackground", "SelectedPressedBackground",
        "SelectionCheckMarkVisualEnabled", "SelectionIndicatorBrush",
        "SelectionIndicatorCornerRadius", "SelectionIndicatorDisabledBrush",
        "SelectionIndicatorPointerOverBrush", "SelectionIndicatorPressedBrush",
        "SelectionIndicatorVisualEnabled", "VerticalContentAlignment",
    },
    "ItemsStackPanel": PANEL_PROPS | {
        "AreStickyGroupHeadersEnabled", "CacheLength", "GroupHeaderPlacement",
        "GroupPadding", "ItemsUpdatingScrollMode", "Orientation",
    },
    "ItemsPresenter": {"Footer", "FooterTemplate", "FooterTransitions", "Header", "HeaderTemplate", "HeaderTransitions", "Padding"},
    "RowDefinition": {"Height", "MinHeight", "MaxHeight"},
    "ColumnDefinition": {"Width", "MinWidth", "MaxWidth"},
    "TranslateTransform": {"X", "Y"},
    "ScaleTransform": {"ScaleX", "ScaleY", "CenterX", "CenterY"},
    "CompositeTransform": {"CenterX", "CenterY", "Rotation", "ScaleX", "ScaleY", "SkewX", "SkewY", "TranslateX", "TranslateY"},
    "RectangleGeometry": {"Rect", "Transform"},
    "BrushTransition": {"Duration"},
    "AddDeleteThemeTransition": set(),
    "ContentThemeTransition": set(),
    "ReorderThemeTransition": set(),
    "EntranceThemeTransition": {"FromHorizontalOffset", "FromVerticalOffset", "IsStaggeringEnabled"},
}

# XAML inheritance chain for validating owner-qualified Storyboard.TargetProperty paths.
ELEMENT_TYPES = {
    "Grid": {"Grid", "Panel", "FrameworkElement", "UIElement"},
    "Border": {"Border", "FrameworkElement", "UIElement"},
    "ContentPresenter": {"ContentPresenter", "FrameworkElement", "UIElement"},
    "Rectangle": {"Rectangle", "Shape", "FrameworkElement", "UIElement"},
    "TextBlock": {"TextBlock", "FrameworkElement", "UIElement"},
    "Button": {"Button", "ButtonBase", "ContentControl", "Control", "FrameworkElement", "UIElement"},
    "ScrollViewer": {"ScrollViewer", "ContentControl", "Control", "FrameworkElement", "UIElement"},
    "Thumb": {"Thumb", "Control", "FrameworkElement", "UIElement"},
    "ToggleSwitch": {"ToggleSwitch", "Control", "FrameworkElement", "UIElement"},
    "TextBox": {"TextBox", "Control", "FrameworkElement", "UIElement"},
    "ListView": {"ListView", "ListViewBase", "Selector", "ItemsControl", "Control", "FrameworkElement", "UIElement"},
    "ListViewItem": {"ListViewItem", "SelectorItem", "ContentControl", "Control", "FrameworkElement", "UIElement"},
    "FontIcon": {"FontIcon", "IconElement", "FrameworkElement", "UIElement"},
    "ListViewItemPresenter": {"ListViewItemPresenter", "ContentPresenter", "FrameworkElement", "UIElement"},
    "ItemsStackPanel": {"ItemsStackPanel", "Panel", "FrameworkElement", "UIElement"},
    "ItemsPresenter": {"ItemsPresenter", "FrameworkElement", "UIElement"},
    "RowDefinition": {"RowDefinition"},
    "ColumnDefinition": {"ColumnDefinition"},
    "TranslateTransform": {"TranslateTransform", "Transform"},
    "ScaleTransform": {"ScaleTransform", "Transform"},
    "CompositeTransform": {"CompositeTransform", "Transform"},
    "RectangleGeometry": {"RectangleGeometry", "Geometry"},
    "BrushTransition": {"BrushTransition"},
    "AddDeleteThemeTransition": {"AddDeleteThemeTransition", "Transition"},
    "ContentThemeTransition": {"ContentThemeTransition", "Transition"},
    "ReorderThemeTransition": {"ReorderThemeTransition", "Transition"},
    "EntranceThemeTransition": {"EntranceThemeTransition", "Transition"},
}

# Elements that declare an ordinary RuntimeNameProperty 'Name' in WinUI (all others require x:Name).
NAME_PROP_ELEMENTS = (set(ELEMENT_OWN_PROPS) - NON_UI_ELEMENTS) | {"VisualState", "VisualStateGroup"}


def allowed_props_for(tag):
    own = ELEMENT_OWN_PROPS.get(tag)
    if own is None:
        return set()
    if tag in NON_UI_ELEMENTS:
        return own
    return COMMON_UI_PROPS | ATTACHED_PROPS | own


# Known properties derived directly from the element schemas (plus brush sub-properties).
KNOWN_PROPS = (
    COMMON_UI_PROPS
    | ATTACHED_PROPS
    | {"Background.Color", "Fill.Color", "Stroke.Color", "Color"}
    | set().union(*ELEMENT_OWN_PROPS.values())
)

TARGET_STYLES = [
    ("controls/dev/CommonStyles/Button_themeresources.xaml", "DefaultButtonStyle", "Button", "IM_TEMPLATE_BUTTON"),
    ("controls/dev/CommonStyles/Button_themeresources.xaml", "AccentButtonStyle", "Button", "IM_TEMPLATE_BUTTON_ACCENT"),
    ("controls/dev/CommonStyles/Button_themeresources.xaml", "SubtleButtonStyle", "Button", "IM_TEMPLATE_BUTTON_SUBTLE"),
    ("controls/dev/CommonStyles/ToggleSwitch_themeresources.xaml", "DefaultToggleSwitchStyle", "ToggleSwitch", "IM_TEMPLATE_TOGGLE_SWITCH"),
    ("controls/dev/CommonStyles/TextBox_themeresources.xaml", "DefaultTextBoxStyle", "TextBox", "IM_TEMPLATE_TEXT_BOX"),
    ("dxaml/xcp/dxaml/themes/generic.xaml", None, "ListView", "IM_TEMPLATE_LIST_VIEW"),
    ("controls/dev/CommonStyles/ListViewItem_themeresources.xaml", "DefaultListViewItemStyle", "ListViewItem", "IM_TEMPLATE_LIST_VIEW_ITEM"),
    ("controls/dev/CommonStyles/ListViewItem_themeresources.xaml", "ListViewItemExpanded", "ListViewItem", "IM_TEMPLATE_LIST_VIEW_ITEM_EXPANDED"),
]


class TemplateError(Exception):
    """Raised when XAML contains an unknown element, property, animation, binding mode, converter, or unresolved ref."""


class Node:
    """XML node with exact source file, namespace, opening line, and consumption tracking."""

    def __init__(self, tag, ns, attrs, file, line, attr_lines, ns_map=None, attr_ns=None):
        self.tag = tag
        self.ns = ns
        self.attrs = attrs
        self.file = file
        self.line = line
        self.attr_lines = attr_lines
        self.ns_map = ns_map or {}
        self.attr_ns = attr_ns or {}
        self.children = []
        self.text = ""
        self.visited = False
        self.consumed_attrs = set()

    def attr_line(self, name):
        return self.attr_lines.get(name, self.line)

    def visit(self):
        self.visited = True

    def take(self, name, default=None):
        if name in self.attrs:
            self.consumed_attrs.add(name)
            return self.attrs[name]
        return default

    def take_one_of(self, *names):
        present = [n for n in names if n in self.attrs]
        if len(present) > 1:
            raise TemplateError(
                f"{self.file}:{self.attr_line(present[1])}: conflicting properties {present[0]!r} and {present[1]!r} on {self.tag}"
            )
        if present:
            self.consumed_attrs.add(present[0])
            return present[0], self.attrs[present[0]]
        return None, None


def verify_fully_consumed(node):
    """Fail if any parsed element or attribute in the compiled AST was silently ignored."""
    if not node.visited:
        raise TemplateError(f"{node.file}:{node.line}: unconsumed element {node.tag!r}")
    for k in node.attrs:
        if k not in node.consumed_attrs:
            raise TemplateError(f"{node.file}:{node.attr_line(k)}: unconsumed property {k!r} on {node.tag}")
    for child in node.children:
        verify_fully_consumed(child)


def local_name(name):
    return name.rsplit(" ", 1)[-1].rsplit("}", 1)[-1]


def parse_xaml(text, filename):
    """Parse XAML into Node tree preserving exact line numbers for elements and attributes."""
    raw = text.encode("utf-8")
    nl_offsets = [m.start() for m in re.finditer(rb"\n", raw)]
    parser = expat.ParserCreate(namespace_separator=" ")
    stack = []
    ns_scopes = [{"xml": "http://www.w3.org/XML/1998/namespace", "xmlns": "http://www.w3.org/2000/xmlns/"}]
    pending_ns = {}
    root = None

    def line_at(byte_index):
        return bisect.bisect_left(nl_offsets, byte_index) + 1

    def find_attr_lines(byte_index):
        # Quote-aware scan of the opening tag starting at byte_index to record exact attribute line numbers.
        i = byte_index
        n = len(raw)
        in_quote = 0
        end = -1
        while i < n:
            b = raw[i]
            if in_quote:
                if b == in_quote:
                    in_quote = 0
            else:
                if b in (34, 39):  # " or '
                    in_quote = b
                elif b == 62:  # >
                    end = i
                    break
            i += 1
        if end < 0:
            return {}
        tag_bytes = raw[byte_index:end + 1]
        # Replace quoted attribute values with spaces (preserving newlines) so regex only matches attribute names.
        masked = bytearray(tag_bytes)
        q = 0
        for idx, b in enumerate(tag_bytes):
            if q:
                if b == q:
                    q = 0
                elif b != 10:
                    masked[idx] = 32
            elif b in (34, 39):
                q = b
        base_line = line_at(byte_index)
        cur_ns = ns_scopes[-1]
        result = {}
        masked_str = bytes(masked).decode("utf-8", "replace")
        for match in re.finditer(r"""\s([^\s=/>"']+)\s*=""", masked_str):
            full = match.group(1)
            line_no = base_line + masked_str[:match.start(1)].count("\n")
            if ":" in full:
                prefix, short = full.split(":", 1)
                if prefix == "xmlns":
                    continue
                ns_uri = cur_ns.get(prefix)
                if ns_uri is not None:
                    result[(ns_uri, short)] = line_no
                result[full] = line_no
                result.setdefault(short, line_no)
            else:
                if full == "xmlns":
                    continue
                result[("", full)] = line_no
                result[full] = line_no
        return result

    def start_ns(prefix, uri):
        pending_ns[prefix or ""] = uri

    def start(tag, attrs):
        byte_idx = parser.CurrentByteIndex
        base_line = line_at(byte_idx)
        cur_ns = {**ns_scopes[-1], **pending_ns}
        pending_ns.clear()
        ns_scopes.append(cur_ns)
        attr_lines = find_attr_lines(byte_idx)
        ns_xaml = "http://schemas.microsoft.com/winfx/2006/xaml"
        ns_pres = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        ns_mc = "http://schemas.openxmlformats.org/markup-compatibility/2006"
        ns_ctrl = "using:Microsoft.UI.Xaml.Controls"
        if " " in tag:
            el_ns, el_local = tag.rsplit(" ", 1)
            if el_ns == ns_xaml:
                if el_local not in ("Boolean", "Double", "Int32", "String"):
                    raise TemplateError(f"{filename}:{base_line}: unsupported x: element {el_local!r}")
            elif el_ns == ns_pres:
                if el_local in ("Boolean", "Double", "Int32", "String"):
                    raise TemplateError(f"{filename}:{base_line}: XAML primitive {el_local!r} must use the x: namespace")
            else:
                raise TemplateError(f"{filename}:{base_line}: unsupported element namespace {el_ns!r} for {el_local!r}")
        else:
            el_ns = ""
            el_local = local_name(tag)
        xaml_allowed = {"Name", "Key", "DeferLoadStrategy", "Load"}
        clean_attrs = {}
        attr_ns = {}
        for k, v in attrs.items():
            if " " in k:
                ns, short = k.rsplit(" ", 1)
                line_no = attr_lines.get((ns, short), base_line)
                if ns == ns_mc:
                    if short != "Ignorable":
                        raise TemplateError(f"{filename}:{line_no}: unsupported markup-compatibility attribute {short!r}")
                elif ns == ns_xaml:
                    if short not in xaml_allowed:
                        raise TemplateError(f"{filename}:{line_no}: unsupported x: attribute {short!r}")
                elif ns == ns_ctrl:
                    if short != "AnimatedIcon.State":
                        raise TemplateError(f"{filename}:{line_no}: unsupported controls attribute {short!r}")
                else:
                    raise TemplateError(f"{filename}:{line_no}: unsupported attribute namespace {ns!r} for {short!r}")
            else:
                ns = ""
                short = local_name(k)
                line_no = attr_lines.get(("", short), base_line)
                if short in XAML_DIRECTIVE_ATTRS:
                    raise TemplateError(f"{filename}:{line_no}: XAML directive {short!r} must use the x: namespace")
                if short == "Ignorable":
                    raise TemplateError(f"{filename}:{line_no}: Ignorable must use the mc: namespace")
                if short == "Name" and el_local not in NAME_PROP_ELEMENTS:
                    raise TemplateError(f"{filename}:{line_no}: Name on {el_local} must use the x: namespace")
            if "." in el_local:
                raise TemplateError(f"{filename}:{line_no}: unknown property {short!r} on {el_local}")
            if short in clean_attrs:
                raise TemplateError(f"{filename}:{line_no}: duplicate attribute {short!r}")
            clean_attrs[short] = v
            attr_ns[short] = ns
            attr_lines[short] = line_no
        node = Node(el_local, el_ns, clean_attrs, filename, base_line, attr_lines, ns_map=cur_ns, attr_ns=attr_ns)
        if stack:
            stack[-1].children.append(node)
        stack.append(node)

    def end(tag):
        nonlocal root
        node = stack.pop()
        ns_scopes.pop()
        if not stack:
            root = node

    def char_data(data):
        if stack:
            stack[-1].text += data

    parser.StartNamespaceDeclHandler = start_ns
    parser.StartElementHandler = start
    parser.EndElementHandler = end
    parser.CharacterDataHandler = char_data
    try:
        parser.Parse(raw, True)
    except expat.ExpatError as err:
        raise TemplateError(f"{filename}:{err.lineno}: XML syntax error: {err}") from err
    return root


def parse_fixed(text, file, line):
    try:
        value = Fraction(text.strip()) * 65536
        whole = int(abs(value) + Fraction(1, 2))
        return -whole if value < 0 else whole
    except Exception as err:
        raise TemplateError(f"{file}:{line}: invalid fixed number {text!r}") from err


def parse_duration_ms(text, file, line):
    """Parse XAML Duration / KeyTime (e.g. '0', '0:0:0.083', '00:00:00.250') to milliseconds."""
    s = text.strip()
    if s == "0":
        return 0
    m = re.fullmatch(r"(?:(\d+):)?(\d+):(\d+(?:\.\d+)?)", s)
    if m:
        hours = int(m.group(1) or 0)
        mins = int(m.group(2))
        secs = Fraction(m.group(3))
        ms = (hours * 3600 + mins * 60 + secs) * 1000
        return int(ms + Fraction(1, 2))
    # Plain seconds
    if re.fullmatch(r"\d+(?:\.\d+)?", s):
        ms = Fraction(s) * 1000
        return int(ms + Fraction(1, 2))
    raise TemplateError(f"{file}:{line}: invalid duration/keytime {text!r}")


def parse_spline(text, file, line):
    parts = [p for p in re.split(r"[,\s]+", text.strip()) if p]
    if len(parts) != 4:
        raise TemplateError(f"{file}:{line}: invalid KeySpline {text!r}")
    return [parse_fixed(p, file, line) for p in parts]


def c_str(text):
    if text is None:
        return "NULL"
    escaped = text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return f'"{escaped}"'


ALL_OWNER_TYPES = set().union(*ELEMENT_TYPES.values())

# Properties valid on each XAML base or concrete owner type for (Owner.Prop) validation.
TYPE_PROPS = {
    "UIElement": UI_ELEMENT_PROPS,
    "FrameworkElement": COMMON_UI_PROPS,
    "Panel": COMMON_UI_PROPS | PANEL_PROPS,
    "Shape": COMMON_UI_PROPS | (ELEMENT_OWN_PROPS["Rectangle"] - {"RadiusX", "RadiusY"}),
    "Control": COMMON_UI_PROPS | CONTROL_PROPS,
    "ContentControl": COMMON_UI_PROPS | CONTENT_CONTROL_PROPS,
    "ButtonBase": COMMON_UI_PROPS | CONTENT_CONTROL_PROPS | {"ClickMode", "Command", "CommandParameter"},
    "ItemsControl": COMMON_UI_PROPS | CONTROL_PROPS | {
        "DisplayMemberPath", "GroupStyleSelector", "ItemContainerStyle",
        "ItemContainerStyleSelector", "ItemContainerTransitions", "ItemTemplate",
        "ItemTemplateSelector", "ItemsPanel", "ItemsSource",
    },
    "Selector": COMMON_UI_PROPS | CONTROL_PROPS | {
        "DisplayMemberPath", "GroupStyleSelector", "IsSynchronizedWithCurrentItem",
        "ItemContainerStyle", "ItemContainerStyleSelector", "ItemContainerTransitions",
        "ItemTemplate", "ItemTemplateSelector", "ItemsPanel", "ItemsSource",
        "SelectedIndex", "SelectedItem", "SelectedValue", "SelectedValuePath",
    },
    "SelectorItem": COMMON_UI_PROPS | CONTENT_CONTROL_PROPS | {"IsSelected"},
    "ListViewBase": COMMON_UI_PROPS | ELEMENT_OWN_PROPS["ListView"],
    "IconElement": COMMON_UI_PROPS | {"Foreground"},
    "Transform": set(),
    "Geometry": {"Transform"},
    "Transition": set(),
}
for _tag in ELEMENT_OWN_PROPS:
    TYPE_PROPS[_tag] = allowed_props_for(_tag)

BRUSH_PROPS = {
    "Background", "BorderBrush", "Foreground", "Fill", "Stroke",
    "FocusVisualPrimaryBrush", "FocusVisualSecondaryBrush",
    "PlaceholderForeground", "SelectionHighlightColor",
    "SelectionHighlightColorWhenNotFocused",
}


XAML_PRES_NS = "http://schemas.microsoft.com/winfx/2006/xaml/presentation"

OWNER_NAMESPACE = {
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


def is_valid_owner_ns(ns, owner):
    """Return True iff XML namespace ns validly names the WinUI owner type."""
    if not ns:
        return False
    expected = OWNER_NAMESPACE.get(owner)
    return expected is not None and ns in (expected, XAML_PRES_NS)


def is_valid_qualified_member(owner, member, target_tag=None):
    """Return True iff (owner.member) is a valid attached or owner-declared property (and valid on target_tag)."""
    if f"{owner}.{member}" in ATTACHED_PROPS:
        return target_tag is None or target_tag not in NON_UI_ELEMENTS
    if owner not in TYPE_PROPS or member not in TYPE_PROPS[owner]:
        return False
    if target_tag is not None and owner not in ELEMENT_TYPES.get(target_tag, {target_tag}):
        return False
    return True


def clean_target_property(prop, file, line):
    """Validate Storyboard.TargetProperty against known properties and structural compound paths."""
    raw = prop.strip()
    if not raw:
        raise TemplateError(f"{file}:{line}: empty TargetProperty {prop!r}")
    if ":" in raw:
        raise TemplateError(f"{file}:{line}: unsupported prefixed TargetProperty {prop!r}")
    if raw in KNOWN_PROPS:
        return raw
    cm = re.fullmatch(r"\((\w+)\.(\w+)\)(?:\.\((\w+)\.(\w+)\))?", raw)
    if cm:
        owner, member, sub_owner, sub_member = cm.groups()
        valid_head = is_valid_qualified_member(owner, member)
        valid_tail = (sub_owner is None) or (
            sub_owner == "SolidColorBrush" and sub_member == "Color" and member in BRUSH_PROPS
        )
        if valid_head and valid_tail:
            return raw
    raise TemplateError(f"{file}:{line}: unknown TargetProperty {prop!r}")


class Compiler:
    """Compiles WinUI styles and control templates into flat portable C tables."""

    def __init__(self, res_dict=None, brushes=None, values=None):
        self.res_dict = res_dict
        self.brushes = brushes or {}
        self.values = values or {}
        self.templates = []
        self.style_setters = []
        self.elements = []
        self.props = []
        self.groups = []
        self.states = []
        self.transitions = []
        self.anims = []
        self.keyframes = []
        self.local_resources = {}  # nested style keys inside Grid.Resources

    def resolve_string_resource(self, key, file, line):
        if self.res_dict is None:
            raise TemplateError(f"{file}:{line}: unresolved resource {key!r} (no resource dictionary)")
        try:
            item = self.res_dict.find("Default", key)
        except res_mod.Unresolved as err:
            raise TemplateError(f"{file}:{line}: unresolved resource {key!r}: {err}") from err
        return res_mod.local(item.tag), (item.text or "").strip(), item

    def resolve_color_theme(self, theme, key, file, line):
        seen = []
        cur = key
        while True:
            if cur in res_mod.SLOTS:
                return res_mod.SLOTS.index(cur), 0xFF000000
            if cur in seen:
                raise TemplateError(f"{file}:{line}: colour loop on {key!r}")
            seen.append(cur)
            item = self.res_dict.theme[theme].get(cur, self.res_dict.any.get(cur))
            if item is None and theme == "HighContrast":
                item = self.res_dict.theme["Default"].get(cur)
            if item is None:
                raise TemplateError(f"{file}:{line}: unresolved colour {cur!r} for {theme}")
            tag = res_mod.local(item.tag)
            if tag == "StaticResource":
                cur = item.get("ResourceKey")
                continue
            if tag == "Color":
                ref = res_mod.reference(item.text)
                if ref:
                    cur = ref
                    continue
                return -1, res_mod.argb(item.text)
            if tag == "SolidColorBrush":
                ref = res_mod.reference(item.get("Color"))
                if ref:
                    cur = ref
                    continue
                return -1, res_mod.argb(item.get("Color"))
            raise TemplateError(f"{file}:{line}: {cur!r} is {tag!r}, not a Color")

    def parse_value(self, raw_text, prop_name, file, line, element_context=None):
        text = (raw_text or "").strip()
        if text == "{x:Null}":
            return {"kind": "IM_TVAL_NULL", "a": 0, "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}

        # Check markup extensions
        if text.startswith("{") and text.endswith("}"):
            inner = text[1:-1].strip()
            head = inner.split(None, 1)[0]
            rest = inner[len(head):].strip()

            if head == "TemplateBinding":
                if rest not in KNOWN_PROPS:
                    raise TemplateError(f"{file}:{line}: unknown property {rest!r} in TemplateBinding")
                return {"kind": "IM_TVAL_TEMPLATE_BINDING", "a": 0, "b": 0, "c": 0, "d": 0, "text": rest, "fallback": None}

            if head == "Binding":
                # Split Binding arguments by top-level commas (respecting nested {...} braces).
                tokens = []
                cur = []
                depth = 0
                for ch in rest:
                    if ch == "{":
                        depth += 1
                    elif ch == "}":
                        depth -= 1
                    if ch == "," and depth == 0:
                        tok = "".join(cur).strip()
                        if tok:
                            tokens.append(tok)
                        cur = []
                    else:
                        cur.append(ch)
                if cur:
                    tok = "".join(cur).strip()
                    if tok:
                        tokens.append(tok)
                path = None
                source = "IM_BIND_DATA"
                mode_map = {"OneWay": "IM_BIND_ONE_WAY", "TwoWay": "IM_BIND_TWO_WAY", "OneTime": "IM_BIND_ONE_TIME"}
                mode = "IM_BIND_ONE_WAY"
                fallback = None
                for tok in tokens:
                    if "=" in tok:
                        k, v = [x.strip() for x in tok.split("=", 1)]
                        if k == "Converter":
                            raise TemplateError(f"{file}:{line}: unsupported converter {v!r} in Binding")
                        if k == "Mode":
                            if v not in mode_map:
                                raise TemplateError(f"{file}:{line}: unknown binding mode {v!r}")
                            mode = mode_map[v]
                        elif k == "Path":
                            path = v
                        elif k == "TargetNullValue":
                            fallback = v.strip("'\"")
                        elif k == "RelativeSource":
                            rs = v.strip()
                            if rs in ("{RelativeSource TemplatedParent}", "{RelativeSource Mode=TemplatedParent}", "TemplatedParent"):
                                source = "IM_BIND_TEMPLATED_PARENT"
                            else:
                                raise TemplateError(f"{file}:{line}: unsupported RelativeSource {v!r} in Binding")
                        else:
                            raise TemplateError(f"{file}:{line}: unknown Binding parameter {k!r}")
                    else:
                        if path is None:
                            path = tok
                        else:
                            raise TemplateError(f"{file}:{line}: unexpected Binding token {tok!r}")
                return {"kind": "IM_TVAL_BINDING", "a": 0, "b": 0, "c": 0, "d": 0, "source": source, "mode": mode, "text": path or "", "fallback": fallback}

            if head in ("ThemeResource", "StaticResource"):
                key = rest.removeprefix("ResourceKey=").strip()
                if key in self.local_resources:
                    return {"kind": "IM_TVAL_NESTED_TEMPLATE", "a": self.local_resources[key], "b": 0, "c": 0, "d": 0, "text": key, "fallback": None}
                if key in self.brushes:
                    return {"kind": "IM_TVAL_BRUSH", "a": 0, "b": 0, "c": 0, "d": 0, "text": f"IM_BRUSH_{key}", "fallback": None}
                if key in self.values:
                    return {"kind": "IM_TVAL_THEME_VALUE", "a": 0, "b": 0, "c": 0, "d": 0, "text": f"IM_VALUE_{key}", "fallback": None}
                tag, res_text, _item = self.resolve_string_resource(key, file, line)
                if prop_name in ("KeyTime", "Duration", "GeneratedDuration") or tag == "Duration":
                    ms = parse_duration_ms(res_text, file, line)
                    return {"kind": "IM_TVAL_DURATION_MS", "a": ms, "b": 0, "c": 0, "d": 0, "text": key, "fallback": None}
                if prop_name == "KeySpline" or tag == "KeySpline":
                    sp = parse_spline(res_text, file, line)
                    return {"kind": "IM_TVAL_SPLINE", "a": sp[0], "b": sp[1], "c": sp[2], "d": sp[3], "text": key, "fallback": None}
                if tag == "Boolean" or res_text in ("True", "False"):
                    return {"kind": "IM_TVAL_BOOL", "a": 1 if res_text == "True" else 0, "b": 0, "c": 0, "d": 0, "text": key, "fallback": None}
                if tag == "Color":
                    _slot_d, argb_d = self.resolve_color_theme("Default", key, file, line)
                    _slot_l, argb_l = self.resolve_color_theme("Light", key, file, line)
                    slot_c, argb_c = self.resolve_color_theme("HighContrast", key, file, line)
                    return {"kind": "IM_TVAL_COLOR", "a": argb_d, "b": argb_l, "c": slot_c if slot_c >= 0 else argb_c, "d": slot_c, "text": key, "fallback": None}
                if key == "SymbolThemeFontFamily" or (tag == "FontFamily" and "Segoe" in (res_text or "")):
                    return {"kind": "IM_TVAL_ICON_FONT", "a": 0, "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}
                if tag in ("FontFamily", "FontWeight", "String", "Style", "MenuFlyout", "TextCommandBarFlyout", "ListViewItemPresenterCheckMode"):
                    return {"kind": "IM_TVAL_ENUM_STR", "a": 0, "b": 0, "c": 0, "d": 0, "text": res_text or key, "fallback": None}
                raise TemplateError(f"{file}:{line}: unsupported resource {key!r} of type {tag!r}")

            raise TemplateError(f"{file}:{line}: unknown markup extension {head!r}")

        # Private-use Unicode glyph check (e.g. SymbolThemeFontFamily glyphs on TextBlock/FontIcon)
        if prop_name in ("Text", "Glyph") and len(text) == 1 and 0xE000 <= ord(text) <= 0xF8FF:
            cp = ord(text)
            icon_id = ICON_GLYPHS.get(cp)
            if icon_id is None:
                raise TemplateError(f"{file}:{line}: unmapped private-use icon glyph U+{cp:04X}")
            return {"kind": "IM_TVAL_ICON", "a": 0, "b": 0, "c": 0, "d": 0, "text": icon_id, "fallback": None}

        if prop_name in ("KeyTime", "Duration", "GeneratedDuration"):
            ms = parse_duration_ms(text, file, line)
            return {"kind": "IM_TVAL_DURATION_MS", "a": ms, "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}
        if prop_name == "KeySpline":
            sp = parse_spline(text, file, line)
            return {"kind": "IM_TVAL_SPLINE", "a": sp[0], "b": sp[1], "c": sp[2], "d": sp[3], "text": None, "fallback": None}
        if text in ("True", "False"):
            return {"kind": "IM_TVAL_BOOL", "a": 1 if text == "True" else 0, "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}

        # Numeric points (RenderTransformOrigin)
        if prop_name == "RenderTransformOrigin":
            parts = [p for p in re.split(r"[,\s]+", text) if p]
            if len(parts) != 2 or not all(re.fullmatch(r"-?\d+(?:\.\d+)?", p) for p in parts):
                raise TemplateError(f"{file}:{line}: invalid RenderTransformOrigin {text!r}")
            return {"kind": "IM_TVAL_POINT", "a": parse_fixed(parts[0], file, line), "b": parse_fixed(parts[1], file, line), "c": 0, "d": 0, "text": None, "fallback": None}

        # Numeric quads (Thickness, CornerRadius, Rect)
        if prop_name in ("Margin", "Padding", "BorderThickness", "CornerRadius", "Rect", "FocusVisualMargin", "FocusVisualPrimaryThickness", "FocusVisualSecondaryThickness"):
            parts = [p for p in re.split(r"[,\s]+", text) if p]
            if all(re.fullmatch(r"-?\d+(?:\.\d+)?", p) for p in parts):
                vals = [parse_fixed(p, file, line) for p in parts]
                if len(vals) == 1:
                    vals = vals * 4
                elif len(vals) == 2:
                    vals = [vals[0], vals[1], vals[0], vals[1]]
                if len(vals) == 4:
                    return {"kind": "IM_TVAL_QUAD", "a": vals[0], "b": vals[1], "c": vals[2], "d": vals[3], "text": None, "fallback": None}

        if re.fullmatch(r"-?\d+", text) and prop_name in ("Grid.Row", "Grid.Column", "Grid.RowSpan", "Grid.ColumnSpan", "Canvas.ZIndex"):
            return {"kind": "IM_TVAL_INT", "a": int(text), "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}

        if re.fullmatch(r"-?\d+(?:\.\d+)?", text):
            return {"kind": "IM_TVAL_FIXED", "a": parse_fixed(text, file, line), "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}

        return {"kind": "IM_TVAL_ENUM_STR", "a": 0, "b": 0, "c": 0, "d": 0, "text": text, "fallback": None}

    def compile_animation(self, node, named_elements=None):
        named_elements = named_elements or {}
        node.visit()
        if node.tag not in ANIMATION_KINDS:
            raise TemplateError(f"{node.file}:{node.line}: unknown animation {node.tag!r}")
        allowed_anim_attrs = ANIMATION_ATTRS[node.tag]
        for attr_name in node.attrs:
            if attr_name == "Name" and node.attr_ns.get("Name") == "http://schemas.microsoft.com/winfx/2006/xaml":
                continue
            if attr_name not in allowed_anim_attrs:
                raise TemplateError(
                    f"{node.file}:{node.attr_line(attr_name)}: property {attr_name!r} is not supported on {node.tag}"
                )
        node.take("Name")
        kind = ANIMATION_KINDS[node.tag]
        target_name_key, target_name = node.take_one_of("Storyboard.TargetName", "TargetName")
        if target_name is not None and target_name not in named_elements:
            raise TemplateError(
                f"{node.file}:{node.attr_line(target_name_key)}: unknown Storyboard.TargetName {target_name!r}"
            )
        target_prop_key, target_prop = node.take_one_of("Storyboard.TargetProperty", "TargetProperty")
        if target_prop is not None:
            target_prop = clean_target_property(target_prop, node.file, node.attr_line(target_prop_key))
            if target_name:
                target_tag = named_elements[target_name]
                if "(" not in target_prop and "." not in target_prop:
                    if target_prop not in allowed_props_for(target_tag):
                        raise TemplateError(
                            f"{node.file}:{node.attr_line(target_prop_key)}: property {target_prop!r} is not supported on {target_tag} ({target_name})"
                        )
                elif target_prop in ("Background.Color", "Fill.Color", "Stroke.Color"):
                    base_prop = target_prop.split(".", 1)[0]
                    if base_prop not in allowed_props_for(target_tag):
                        raise TemplateError(
                            f"{node.file}:{node.attr_line(target_prop_key)}: TargetProperty {target_prop!r} is not supported on {target_tag} ({target_name})"
                        )
                else:
                    cm = re.fullmatch(r"\((\w+)\.(\w+)\)(?:\.\((\w+)\.(\w+)\))?", target_prop)
                    if not cm:
                        raise TemplateError(
                            f"{node.file}:{node.attr_line(target_prop_key)}: unsupported TargetProperty {target_prop!r}"
                        )
                    owner, prop = cm.group(1), cm.group(2)
                    if not is_valid_qualified_member(owner, prop, target_tag):
                        raise TemplateError(
                            f"{node.file}:{node.attr_line(target_prop_key)}: TargetProperty {target_prop!r} is not supported on {target_tag} ({target_name})"
                        )
        duration_ms = 0
        dur_raw = node.take("Duration")
        if dur_raw is not None:
            dv = self.parse_value(dur_raw, "Duration", node.file, node.attr_line("Duration"))
            duration_ms = dv["a"]
        empty_v = {"kind": "IM_TVAL_NONE", "a": 0, "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}
        has_from_y = False
        from_y_v = empty_v
        if node.tag == "RepositionThemeAnimation":
            fh_raw = node.take("FromHorizontalOffset")
            fv_raw = node.take("FromVerticalOffset")
            if fh_raw is not None:
                has_from = True
                from_v = self.parse_value(fh_raw, "FromHorizontalOffset", node.file, node.attr_line("FromHorizontalOffset"))
            else:
                has_from = False
                from_v = empty_v
            if fv_raw is not None:
                has_from_y = True
                from_y_v = self.parse_value(fv_raw, "FromVerticalOffset", node.file, node.attr_line("FromVerticalOffset"))
        else:
            from_raw = node.take("From")
            if from_raw is not None:
                has_from = True
                from_v = self.parse_value(from_raw, "From", node.file, node.attr_line("From"))
            else:
                has_from = False
                from_v = empty_v
        to_key, to_raw = node.take_one_of("To", "ToOffset")
        if to_key is not None:
            has_to = True
            to_v = self.parse_value(to_raw, to_key, node.file, node.attr_line(to_key))
        else:
            has_to = False
            to_v = empty_v

        direction = "IM_DIR_NONE"
        raw_dir = node.take("Direction")
        if raw_dir is not None:
            dir_map = {"Left": "IM_DIR_LEFT", "Right": "IM_DIR_RIGHT", "Top": "IM_DIR_TOP", "Bottom": "IM_DIR_BOTTOM"}
            raw_dir = raw_dir.strip()
            if raw_dir not in dir_map:
                raise TemplateError(f"{node.file}:{node.attr_line('Direction')}: unknown Direction {raw_dir!r}")
            direction = dir_map[raw_dir]
        node.take("EnableDependentAnimation")

        kf_start = len(self.keyframes)
        allowed_kfs = ANIMATION_KEYFRAMES.get(node.tag, set())
        for child in node.children:
            child.visit()
            if child.tag not in KEYFRAME_KINDS or child.tag not in allowed_kfs:
                raise TemplateError(f"{child.file}:{child.line}: keyframe {child.tag!r} is not supported in {node.tag}")
            allowed_kf_attrs = KEYFRAME_ATTRS[child.tag]
            for attr_name in child.attrs:
                if attr_name not in allowed_kf_attrs:
                    raise TemplateError(
                        f"{child.file}:{child.attr_line(attr_name)}: property {attr_name!r} is not supported on {child.tag}"
                    )
            kf_kind = KEYFRAME_KINDS[child.tag]
            kt_ms = 0
            kt_raw = child.take("KeyTime")
            if kt_raw is not None:
                ktv = self.parse_value(kt_raw, "KeyTime", child.file, child.attr_line("KeyTime"))
                kt_ms = ktv["a"]
            spline = [0, 0, 0, 0]
            sp_raw = child.take("KeySpline")
            if sp_raw is not None:
                sv = self.parse_value(sp_raw, "KeySpline", child.file, child.attr_line("KeySpline"))
                spline = [sv["a"], sv["b"], sv["c"], sv["d"]]
            val = {"kind": "IM_TVAL_NONE", "a": 0, "b": 0, "c": 0, "d": 0, "text": None, "fallback": None}
            val_raw = child.take("Value")
            has_val = val_raw is not None
            if has_val:
                val = self.parse_value(val_raw, target_prop or "Value", child.file, child.attr_line("Value"))
            for sub in child.children:
                if sub.tag == f"{child.tag}.Value":
                    if has_val:
                        raise TemplateError(f"{sub.file}:{sub.line}: duplicate {sub.tag} on {child.tag}")
                    sub.visit()
                    if len(sub.children) == 0:
                        raise TemplateError(f"{sub.file}:{sub.line}: expected value element in {sub.tag}")
                    inner = sub.children[0]
                    inner.visit()
                    if inner.children:
                        raise TemplateError(f"{inner.children[0].file}:{inner.children[0].line}: unexpected element {inner.children[0].tag!r} in {inner.tag}")
                    if inner.tag == "Visibility" or (inner.tag == "Boolean" and inner.ns == "http://schemas.microsoft.com/winfx/2006/xaml"):
                        val = self.parse_value(inner.text, inner.tag, inner.file, inner.line)
                        has_val = True
                    else:
                        raise TemplateError(f"{inner.file}:{inner.line}: unknown keyframe value element {inner.tag!r}")
                    if len(sub.children) > 1:
                        extra = sub.children[1]
                        raise TemplateError(f"{extra.file}:{extra.line}: unexpected extra element {extra.tag!r} in {sub.tag}")
                else:
                    raise TemplateError(f"{sub.file}:{sub.line}: unknown element {sub.tag!r} in {child.tag}")
            self.keyframes.append({
                "kind": kf_kind,
                "key_time_ms": kt_ms,
                "value": val,
                "spline": spline,
            })
        kf_count = len(self.keyframes) - kf_start
        self.anims.append({
            "kind": kind,
            "target_name": target_name,
            "target_property": target_prop,
            "duration_ms": duration_ms,
            "direction": direction,
            "has_from": has_from,
            "has_from_y": has_from_y,
            "has_to": has_to,
            "from": from_v,
            "from_y": from_y_v,
            "to": to_v,
            "kf_start": kf_start,
            "kf_count": kf_count,
        })

    def compile_vsm_groups(self, groups_node, named_elements=None, target_type=""):
        named_elements = named_elements or {}
        groups_node.visit()
        group_start = len(self.groups)
        for gnode in groups_node.children:
            gnode.visit()
            if gnode.tag != "VisualStateGroup":
                raise TemplateError(f"{gnode.file}:{gnode.line}: expected VisualStateGroup, got {gnode.tag!r}")
            gname = gnode.take("Name", "")
            state_start = len(self.states)
            trans_start = len(self.transitions)
            trans_nodes = []
            for child in gnode.children:
                if child.tag == "VisualStateGroup.Transitions":
                    child.visit()
                    trans_nodes.extend(child.children)
                elif child.tag == "VisualState":
                    child.visit()
                    sname = child.take("Name", "")
                    setter_start = len(self.style_setters)
                    anim_start = len(self.anims)
                    for sc in child.children:
                        if sc.tag == "VisualState.Setters":
                            sc.visit()
                            for s in sc.children:
                                s.visit()
                                if s.tag != "Setter":
                                    raise TemplateError(f"{s.file}:{s.line}: expected Setter, got {s.tag!r}")
                                if s.children:
                                    raise TemplateError(f"{s.children[0].file}:{s.children[0].line}: unexpected child {s.children[0].tag!r} in VisualState Setter")
                                val_raw = s.take("Value")
                                if val_raw is None:
                                    raise TemplateError(f"{s.file}:{s.line}: VisualState Setter missing Value")
                                which, raw_tp = s.take_one_of("Target", "Property")
                                if which is None:
                                    raise TemplateError(f"{s.file}:{s.line}: VisualState Setter missing Target or Property")
                                if which == "Target":
                                    tgt = raw_tp.strip()
                                    if "." not in tgt:
                                        raise TemplateError(f"{s.file}:{s.attr_line('Target')}: invalid VisualState Setter Target {tgt!r}")
                                    part_name, prop_part = tgt.split(".", 1)
                                    prop_line = s.attr_line("Target")
                                    part_tag = named_elements.get(part_name)
                                    if part_tag is None:
                                        raise TemplateError(f"{s.file}:{prop_line}: unknown target element {part_name!r} in Setter Target {tgt!r}")
                                    if prop_part.startswith("(") and prop_part.endswith(")"):
                                        inner_prop = prop_part[1:-1]
                                        if ":" in inner_prop:
                                            pfx, rest_prop = inner_prop.split(":", 1)
                                            if "." not in rest_prop:
                                                raise TemplateError(f"{s.file}:{prop_line}: unsupported prefixed Setter Target {tgt!r}")
                                            owner, member = rest_prop.split(".", 1)
                                            if not is_valid_owner_ns(s.ns_map.get(pfx), owner) or not is_valid_qualified_member(owner, member, part_tag):
                                                raise TemplateError(f"{s.file}:{prop_line}: unsupported prefixed Setter Target {tgt!r}")
                                            prop = rest_prop if rest_prop in ATTACHED_PROPS else member
                                        elif inner_prop in ATTACHED_PROPS:
                                            prop = inner_prop
                                        elif "." in inner_prop:
                                            owner, member = inner_prop.split(".", 1)
                                            if not is_valid_qualified_member(owner, member, part_tag):
                                                raise TemplateError(f"{s.file}:{prop_line}: unsupported qualified Setter Target {tgt!r}")
                                            prop = member
                                        else:
                                            raise TemplateError(f"{s.file}:{prop_line}: unsupported attached Setter Target {tgt!r}")
                                    elif ":" in prop_part:
                                        raise TemplateError(f"{s.file}:{prop_line}: unsupported prefixed Setter Target {tgt!r}")
                                    else:
                                        prop = prop_part
                                    if prop not in allowed_props_for(part_tag):
                                        raise TemplateError(f"{s.file}:{prop_line}: property {prop!r} is not supported on {part_tag} ({part_name})")
                                else:
                                    tgt = ""
                                    prop = raw_tp.strip()
                                    prop_line = s.attr_line("Property")
                                    allowed_owner = allowed_props_for(target_type) if target_type in ELEMENT_OWN_PROPS else (COMMON_UI_PROPS | CONTROL_PROPS | ATTACHED_PROPS)
                                    if not prop or prop not in allowed_owner:
                                        raise TemplateError(f"{s.file}:{prop_line}: property {prop!r} is not supported on {target_type or 'target'}")
                                val = self.parse_value(val_raw, prop, s.file, s.attr_line("Value"))
                                self.style_setters.append({"target": tgt, "property": prop, "value": val})
                        elif sc.tag == "Storyboard":
                            sc.visit()
                            sc.take("Name")
                            for anim in sc.children:
                                self.compile_animation(anim, named_elements=named_elements)
                        else:
                            raise TemplateError(f"{sc.file}:{sc.line}: unexpected element {sc.tag!r} in VisualState")
                    self.states.append({
                        "name": sname,
                        "setter_start": setter_start,
                        "setter_count": len(self.style_setters) - setter_start,
                        "anim_start": anim_start,
                        "anim_count": len(self.anims) - anim_start,
                    })
                else:
                    raise TemplateError(f"{child.file}:{child.line}: unexpected element {child.tag!r} in VisualStateGroup")

            for tnode in trans_nodes:
                tnode.visit()
                if tnode.tag != "VisualTransition":
                    raise TemplateError(f"{tnode.file}:{tnode.line}: expected VisualTransition, got {tnode.tag!r}")
                tnode.take("Name")
                from_s = tnode.take("From")
                to_s = tnode.take("To")
                dur_ms = 0
                gd_raw = tnode.take("GeneratedDuration")
                if gd_raw is not None:
                    dv = self.parse_value(gd_raw, "GeneratedDuration", tnode.file, tnode.attr_line("GeneratedDuration"))
                    dur_ms = dv["a"]
                anim_start = len(self.anims)
                for tc in tnode.children:
                    if tc.tag == "Storyboard":
                        tc.visit()
                        tc.take("Name")
                        for anim in tc.children:
                            self.compile_animation(anim, named_elements=named_elements)
                    else:
                        raise TemplateError(f"{tc.file}:{tc.line}: unexpected element {tc.tag!r} in VisualTransition")
                self.transitions.append({
                    "from_state": from_s,
                    "to_state": to_s,
                    "duration_ms": dur_ms,
                    "anim_start": anim_start,
                    "anim_count": len(self.anims) - anim_start,
                })
            self.groups.append({
                "name": gname,
                "state_start": state_start,
                "state_count": len(self.states) - state_start,
                "trans_start": trans_start,
                "trans_count": len(self.transitions) - trans_start,
            })
        return group_start, len(self.groups) - group_start

    def compile_element_tree(self, node, parent_idx, vsm_groups_out, out_elems, out_props):
        node.visit()
        if node.tag not in ELEMENT_KINDS:
            raise TemplateError(f"{node.file}:{node.line}: unknown element {node.tag!r}")
        elem_idx = len(out_elems)
        prop_start = len(out_props)
        name = node.take("Name")

        saved_resources = dict(self.local_resources)
        try:
            # First scan Grid.Resources so nested styles (like DeleteButtonStyle) are scoped to this element's subtree.
            for child in node.children:
                if child.tag == "Grid.Resources":
                    child.visit()
                    for res_node in child.children:
                        if res_node.tag == "Style":
                            rk = res_node.attrs.get("Name") or res_node.attrs.get("Key")
                            nested_idx = self.compile_style(res_node, key_override=rk, enum_id=None)
                            if rk:
                                self.local_resources[rk] = nested_idx
                        else:
                            raise TemplateError(f"{res_node.file}:{res_node.line}: unknown resource element {res_node.tag!r}")

            # Collect attributes on this element (validated against this element's allowed property set)
            elem_directives = {"DeferLoadStrategy", "Load"}
            allowed_elem = allowed_props_for(node.tag) | (set() if node.tag in NON_UI_ELEMENTS else elem_directives)
            for k in list(node.attrs):
                if k == "Name":
                    continue
                if k not in allowed_elem:
                    raise TemplateError(f"{node.file}:{node.attr_line(k)}: unknown property {k!r} on {node.tag}")
                v = node.take(k)
                val = self.parse_value(v, k, node.file, node.attr_line(k))
                out_props.append({"name": k, "value": val})

            # Handle property-element children that attach directly to this element's props (e.g. Thumb.Template)
            child_elements = []
            for child in node.children:
                if child.tag == "Grid.Resources":
                    continue
                elif child.tag == "VisualStateManager.VisualStateGroups":
                    vsm_groups_out.append(child)
                elif child.tag == "Thumb.Template":
                    child.visit()
                    if len(child.children) == 0:
                        raise TemplateError(f"{child.file}:{child.line}: expected ControlTemplate in Thumb.Template")
                    ct = child.children[0]
                    if ct.tag != "ControlTemplate":
                        raise TemplateError(f"{ct.file}:{ct.line}: expected ControlTemplate in Thumb.Template, got {ct.tag!r}")
                    if len(child.children) > 1:
                        extra = child.children[1]
                        raise TemplateError(f"{extra.file}:{extra.line}: unexpected extra element {extra.tag!r} in Thumb.Template")
                    nested_idx = self.compile_control_template(ct, key=f"{name or 'Thumb'}.Template", target_type="Thumb", style_setters=(0, 0), enum_id=None)
                    out_props.append({"name": "Template", "value": {"kind": "IM_TVAL_NESTED_TEMPLATE", "a": nested_idx, "b": 0, "c": 0, "d": 0, "text": f"{name or 'Thumb'}.Template", "fallback": None}})
                elif child.tag in PROP_ELEMENT_TAGS:
                    child.visit()
                    for sub in child.children:
                        child_elements.append(sub)
                elif "." in child.tag:
                    raise TemplateError(f"{child.file}:{child.line}: unknown property element {child.tag!r}")
                else:
                    child_elements.append(child)

            prop_count = len(out_props) - prop_start
            out_elems.append({
                "kind": ELEMENT_KINDS[node.tag],
                "tag": node.tag,
                "name": name,
                "parent": parent_idx,
                "prop_start": prop_start,
                "prop_count": prop_count,
            })
            for sub in child_elements:
                self.compile_element_tree(sub, elem_idx, vsm_groups_out, out_elems, out_props)
            return elem_idx
        finally:
            self.local_resources = saved_resources

    def compile_control_template(self, ct_node, key, target_type, style_setters, enum_id):
        ct_node.visit()
        ct_node.take("TargetType")
        local_elems = []
        local_props = []
        vsm_nodes = []
        for child in ct_node.children:
            self.compile_element_tree(child, -1, vsm_nodes, local_elems, local_props)

        base_prop = len(self.props)
        self.props.extend(local_props)
        elem_start = len(self.elements)
        for e in local_elems:
            self.elements.append({
                "kind": e["kind"],
                "name": e["name"],
                "parent": e["parent"],
                "prop_start": base_prop + e["prop_start"],
                "prop_count": e["prop_count"],
            })
        elem_count = len(local_elems)

        named_elements = {e["name"]: e["tag"] for e in local_elems if e["name"]}
        group_start = len(self.groups)
        group_count = 0
        for vsm in vsm_nodes:
            gs, gc = self.compile_vsm_groups(vsm, named_elements=named_elements, target_type=target_type)
            if group_count == 0:
                group_start = gs
            group_count += gc

        tpl_idx = len(self.templates)
        self.templates.append({
            "enum_id": enum_id,
            "key": key,
            "target_type": target_type,
            "style_setter_start": style_setters[0],
            "style_setter_count": style_setters[1],
            "elem_start": elem_start,
            "elem_count": elem_count,
            "group_start": group_start,
            "group_count": group_count,
        })
        return tpl_idx

    def compile_style(self, style_node, key_override=None, enum_id=None):
        style_node.visit()
        style_key = style_node.take("Key")
        if "Name" in style_node.attrs and style_node.attr_ns.get("Name") != "http://schemas.microsoft.com/winfx/2006/xaml":
            raise TemplateError(f"{style_node.file}:{style_node.attr_line('Name')}: Style Name must use the x: namespace")
        style_name = style_node.take("Name")
        target_type = style_node.take("TargetType", "")
        style_node.take("BasedOn")
        key = key_override if key_override is not None else style_key or style_name or target_type
        local_setters = []
        ct_node = None

        for child in style_node.children:
            child.visit()
            if child.tag != "Setter":
                raise TemplateError(f"{child.file}:{child.line}: expected Setter in Style, got {child.tag!r}")
            prop = child.take("Property", "")
            style_allowed = allowed_props_for(target_type) if target_type in ELEMENT_OWN_PROPS else KNOWN_PROPS
            if prop not in style_allowed:
                raise TemplateError(
                    f"{child.file}:{child.attr_line('Property')}: unknown property {prop!r} in Style Setter for {target_type or 'Style'}"
                )
            if prop in ("Template", "ItemsPanel", "ItemContainerTransitions"):
                if len(child.children) == 0:
                    raise TemplateError(f"{child.file}:{child.line}: Setter for {prop!r} missing Setter.Value")
                if child.children[0].tag != "Setter.Value":
                    bad = child.children[0]
                    raise TemplateError(f"{bad.file}:{bad.line}: expected Setter.Value for {prop!r}, got {bad.tag!r}")
                if len(child.children) > 1:
                    extra = child.children[1]
                    raise TemplateError(f"{extra.file}:{extra.line}: unexpected extra element {extra.tag!r} in Setter")
                sv = child.children[0]
                sv.visit()
                if len(sv.children) == 0:
                    raise TemplateError(f"{sv.file}:{sv.line}: empty Setter.Value for {prop!r}")
                inner = sv.children[0]
                expected_tag = {"Template": "ControlTemplate", "ItemsPanel": "ItemsPanelTemplate", "ItemContainerTransitions": "TransitionCollection"}[prop]
                if inner.tag != expected_tag:
                    raise TemplateError(f"{inner.file}:{inner.line}: expected {expected_tag}, got {inner.tag!r}")
                if len(sv.children) > 1:
                    extra = sv.children[1]
                    raise TemplateError(f"{extra.file}:{extra.line}: unexpected extra element {extra.tag!r} in Setter.Value")
                if prop == "Template":
                    if ct_node is not None:
                        raise TemplateError(f"{child.file}:{child.line}: duplicate Template Setter in Style")
                    ct_node = inner
                elif prop == "ItemsPanel":
                    nested_idx = self.compile_control_template(inner, key=f"{key}.ItemsPanel", target_type=target_type, style_setters=(0, 0), enum_id=None)
                    local_setters.append({"target": None, "property": prop, "value": {"kind": "IM_TVAL_NESTED_TEMPLATE", "a": nested_idx, "b": 0, "c": 0, "d": 0, "text": f"{key}.ItemsPanel", "fallback": None}})
                else:
                    nested_idx = self.compile_control_template(inner, key=f"{key}.ItemContainerTransitions", target_type=target_type, style_setters=(0, 0), enum_id=None)
                    local_setters.append({"target": None, "property": prop, "value": {"kind": "IM_TVAL_NESTED_TEMPLATE", "a": nested_idx, "b": 0, "c": 0, "d": 0, "text": f"{key}.ItemContainerTransitions", "fallback": None}})
            elif "Value" in child.attrs:
                if child.children:
                    raise TemplateError(f"{child.children[0].file}:{child.children[0].line}: unexpected element {child.children[0].tag!r} in Setter")
                val = self.parse_value(child.take("Value"), prop, child.file, child.attr_line("Value"))
                local_setters.append({"target": None, "property": prop, "value": val})
            else:
                raise TemplateError(f"{child.file}:{child.line}: Setter for {prop!r} has no Value")

        setter_start = len(self.style_setters)
        self.style_setters.extend(local_setters)
        setter_count = len(local_setters)

        if ct_node is None:
            raise TemplateError(f"{style_node.file}:{style_node.line}: Style {key!r} has no ControlTemplate")
        return self.compile_control_template(ct_node, key=key, target_type=target_type, style_setters=(setter_start, setter_count), enum_id=enum_id)


def compile_checkout(checkout):
    head = subprocess.run(["git", "-C", str(checkout), "rev-parse", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
    if head != PIN["commit"]:
        raise SystemExit(f"error: the checkout is at {head}, the pin is {PIN['commit']}")
    with tempfile.TemporaryDirectory() as folder:
        archive = subprocess.run(
            ["git", "-C", str(checkout), "archive", "--format=tar", PIN["commit"], "--",
             PIN["base"], PIN["controls"].split("*")[0].rstrip("/")],
            capture_output=True, check=True,
        ).stdout
        with tarfile.open(fileobj=io.BytesIO(archive)) as bundle:
            bundle.extractall(folder, filter="data")
        root = Path(folder)
        res_dict = res_mod.load(root)
        brushes, values, _left = res_mod.collect(res_dict)
        compiler = Compiler(res_dict=res_dict, brushes=brushes, values=values)

        parsed_cache = {}
        for rel_path, style_key, target_type, enum_id in TARGET_STYLES:
            if rel_path not in parsed_cache:
                parsed_cache[rel_path] = parse_xaml((root / rel_path).read_text(encoding="utf-8"), rel_path)
            doc = parsed_cache[rel_path]
            found = None
            for node in doc.children:
                if node.tag == "Style" and node.attrs.get("TargetType") == target_type:
                    if style_key is None and "Key" not in node.attrs and "BasedOn" not in node.attrs:
                        found = node
                        break
                    if style_key is not None and node.attrs.get("Key") == style_key:
                        found = node
                        break
            if found is None:
                raise TemplateError(f"{rel_path}:1: Style key={style_key!r} TargetType={target_type!r} not found")
            compiler.compile_style(found, enum_id=enum_id)
            verify_fully_consumed(found)
        return compiler


def format_val_c(v):
    kind = v["kind"]
    if kind == "IM_TVAL_BRUSH":
        return f"{{{kind}, {v['text']}, 0, 0, 0, NULL, NULL}}"
    if kind == "IM_TVAL_THEME_VALUE":
        return f"{{{kind}, {v['text']}, 0, 0, 0, NULL, NULL}}"
    if kind == "IM_TVAL_ICON":
        return f"{{{kind}, {v['text']}, 0, 0, 0, NULL, NULL}}"
    if kind == "IM_TVAL_BINDING":
        return f"{{{kind}, {v['source']}, {v['mode']}, 0, 0, {c_str(v['text'])}, {c_str(v['fallback'])}}}"
    if kind == "IM_TVAL_COLOR":
        return f"{{{kind}, (int32_t)0x{v['a'] & 0xFFFFFFFF:08X}u, (int32_t)0x{v['b'] & 0xFFFFFFFF:08X}u, (int32_t)0x{v['c'] & 0xFFFFFFFF:08X}u, {v['d']}, {c_str(v['text'])}, NULL}}"
    return f"{{{kind}, {v['a']}, {v['b']}, {v['c']}, {v['d']}, {c_str(v['text'])}, {c_str(v['fallback'])}}}"


def emit(compiler):
    head = (
        f"/* Generated by tools/winui/templates.py from microsoft-ui-xaml {PIN['commit']}.\n"
        "   Copyright (c) Microsoft Corporation. Licensed under the MIT License (see licenses/WinUI.txt).\n"
        "   Do not edit. */\n"
    )
    h = [head, '#pragma once\n#include "template.h"\n\n/* Compiled control template identifiers. */\ntypedef enum im_template_id {\n']
    for idx, t in enumerate(compiler.templates):
        if t["enum_id"]:
            h.append(f"    {t['enum_id']} = {idx},\n")
    h.append(f"    IM_TEMPLATE_COUNT = {len(compiler.templates)}\n}} im_template_id;\n\n")
    h.append("/* Top-level compiled templates table. */\nextern const im_template im_templates[IM_TEMPLATE_COUNT];\n")
    h.append("/* Compiled style and visual-state setters table. */\nextern const im_template_setter im_template_setters[];\n")
    h.append("/* Compiled template element tree nodes table. */\nextern const im_template_element im_template_elements[];\n")
    h.append("/* Compiled element property table. */\nextern const im_template_prop im_template_props[];\n")
    h.append("/* Compiled visual state groups table. */\nextern const im_template_group im_template_groups[];\n")
    h.append("/* Compiled visual states table. */\nextern const im_template_state im_template_states[];\n")
    h.append("/* Compiled visual transitions table. */\nextern const im_template_transition im_template_transitions[];\n")
    h.append("/* Compiled storyboard animations table. */\nextern const im_template_anim im_template_anims[];\n")
    h.append("/* Compiled animation keyframes table. */\nextern const im_template_keyframe im_template_keyframes[];\n")

    c = [head, '#include "template.h"\n\n']
    c.append("const im_template im_templates[IM_TEMPLATE_COUNT] = {\n")
    for t in compiler.templates:
        c.append(f"    {{{c_str(t['key'])}, {c_str(t['target_type'])}, {t['style_setter_start']}, {t['style_setter_count']}, {t['elem_start']}, {t['elem_count']}, {t['group_start']}, {t['group_count']}}},\n")
    c.append("};\n\nconst im_template_setter im_template_setters[] = {\n")
    for s in compiler.style_setters:
        c.append(f"    {{{c_str(s['target'])}, {c_str(s['property'])}, {format_val_c(s['value'])}}},\n")
    c.append("};\n\nconst im_template_element im_template_elements[] = {\n")
    for e in compiler.elements:
        c.append(f"    {{{e['kind']}, {c_str(e['name'])}, {e['parent']}, {e['prop_start']}, {e['prop_count']}}},\n")
    c.append("};\n\nconst im_template_prop im_template_props[] = {\n")
    for p in compiler.props:
        c.append(f"    {{{c_str(p['name'])}, {format_val_c(p['value'])}}},\n")
    c.append("};\n\nconst im_template_group im_template_groups[] = {\n")
    for g in compiler.groups:
        c.append(f"    {{{c_str(g['name'])}, {g['state_start']}, {g['state_count']}, {g['trans_start']}, {g['trans_count']}}},\n")
    c.append("};\n\nconst im_template_state im_template_states[] = {\n")
    for st in compiler.states:
        c.append(f"    {{{c_str(st['name'])}, {st['setter_start']}, {st['setter_count']}, {st['anim_start']}, {st['anim_count']}}},\n")
    c.append("};\n\nconst im_template_transition im_template_transitions[] = {\n")
    for tr in compiler.transitions:
        c.append(f"    {{{c_str(tr['from_state'])}, {c_str(tr['to_state'])}, {tr['duration_ms']}, {tr['anim_start']}, {tr['anim_count']}}},\n")
    c.append("};\n\nconst im_template_anim im_template_anims[] = {\n")
    for a in compiler.anims:
        hf = "true" if a["has_from"] else "false"
        hfy = "true" if a["has_from_y"] else "false"
        ht = "true" if a["has_to"] else "false"
        c.append(f"    {{{a['kind']}, {c_str(a['target_name'])}, {c_str(a['target_property'])}, {a['duration_ms']}, {a['direction']}, {hf}, {hfy}, {ht}, {format_val_c(a['from'])}, {format_val_c(a['from_y'])}, {format_val_c(a['to'])}, {a['kf_start']}, {a['kf_count']}}},\n")
    c.append("};\n\nconst im_template_keyframe im_template_keyframes[] = {\n")
    for kf in compiler.keyframes:
        sp = ", ".join(str(x) for x in kf["spline"])
        c.append(f"    {{{kf['kind']}, {kf['key_time_ms']}, {format_val_c(kf['value'])}, {{{sp}}}}},\n")
    c.append("};\n")
    return {"template_data.h": "".join(h), "template_data.c": "".join(c)}


def format_listing(compiler):
    lines = []
    for idx, t in enumerate(compiler.templates):
        lines.append(f"TEMPLATE[{idx}] {t['key']} ({t['target_type']}): setters={t['style_setter_count']} elems={t['elem_count']} groups={t['group_count']}")
        for e_off in range(t["elem_count"]):
            e = compiler.elements[t["elem_start"] + e_off]
            lines.append(f"  ELEM[{e_off}] parent={e['parent']} {e['kind']} name={e['name'] or '-'}")
        for g_off in range(t["group_count"]):
            g = compiler.groups[t["group_start"] + g_off]
            lines.append(f"  GROUP {g['name']}: states={g['state_count']} transitions={g['trans_count']}")
            for s_off in range(g["state_count"]):
                st = compiler.states[g["state_start"] + s_off]
                lines.append(f"    STATE {st['name']}: setters={st['setter_count']} anims={st['anim_count']}")
            for tr_off in range(g["trans_count"]):
                tr = compiler.transitions[g["trans_start"] + tr_off]
                lines.append(f"    TRANSITION {tr['from_state'] or '*'} -> {tr['to_state'] or '*'} dur={tr['duration_ms']}ms anims={tr['anim_count']}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("checkout", nargs="?", type=Path, help="path to pinned WinUI git checkout")
    parser.add_argument("--check", action="store_true", help="fail if generated files differ")
    parser.add_argument("--list", action="store_true", help="print round-trip listing of compiled templates")
    parser.add_argument("--out-dir", type=Path, help="output/check directory (default ui/)")
    parser.add_argument("--parse-file", type=Path, help="parse a standalone XAML fixture file and exit")
    args = parser.parse_args()

    if args.parse_file:
        doc = parse_xaml(args.parse_file.read_text(encoding="utf-8"), str(args.parse_file))
        comp = Compiler()
        if doc.tag == "Style":
            comp.compile_style(doc)
        elif doc.tag == "ControlTemplate":
            comp.compile_control_template(doc, key="Fixture", target_type=doc.attrs.get("TargetType", ""), style_setters=(0, 0), enum_id=None)
        elif doc.tag == "ResourceDictionary":
            doc.visit()
            doc.take("Ignorable")
            for child in doc.children:
                if child.tag == "Style":
                    comp.compile_style(child)
                else:
                    raise TemplateError(f"{child.file}:{child.line}: unknown element {child.tag!r} in ResourceDictionary")
        else:
            raise TemplateError(f"{doc.file}:{doc.line}: unknown root element {doc.tag!r}")
        verify_fully_consumed(doc)
        print(f"parsed {len(comp.templates)} templates from {args.parse_file}")
        return 0

    if not args.checkout:
        raise SystemExit("error: checkout path is required unless --parse-file is given")

    compiler = compile_checkout(args.checkout)
    if args.list:
        sys.stdout.write(format_listing(compiler))
        return 0

    out_dir = args.out_dir or (ROOT / "ui")
    stale = []
    for name, text in emit(compiler).items():
        path = out_dir / name
        if args.check:
            if not path.is_file() or path.read_text(encoding="utf-8") != text:
                stale.append(name)
        else:
            path.write_text(text, encoding="utf-8", newline="\n")
    if stale:
        raise SystemExit(f"error: not what the pinned WinUI gives: {', '.join(stale)}")
    print(f"{len(compiler.templates)} templates, {len(compiler.elements)} elements, {len(compiler.states)} states, {len(compiler.anims)} animations")
    return 0


if __name__ == "__main__":
    sys.dont_write_bytecode = True
    try:
        raise SystemExit(main())
    except TemplateError as err:
        raise SystemExit(f"error: {err}")

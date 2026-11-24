"""Shared 5-scale WinUI panel layout oracle specification and comparator."""

SCALES = [1.0, 1.25, 1.5, 2.0, 3.0]

# Required named nodes per base case (checked independently of fixture contents).
REQUIRED_NODES = {
    "grid-mixed": [
        "GridRoot", "CellPix", "CellSpanCol", "CellSpanRow", "CellAuto", "CellStar",
    ],
    "grid-unbounded": [
        "OuterStack", "InnerGrid", "RowA", "RowB",
    ],
    "stack-vert": [
        "VStack", "VFirst", "VMidCollapsed", "VLast",
    ],
    "stack-horiz": [
        "HStack", "HTop", "HCenter", "HBottom",
    ],
    "border-round": [
        "RoundGrid", "RoundedBorder", "RoundedChild", "UnroundedBorder", "UnroundedChild",
    ],
    "presenter-rtl": [
        "RtlRoot", "RtlPresenter", "RtlChild",
    ],
    "button-child": [
        "BtnRoot", "BtnChild",
    ],
}


def scale_tag(scale):
    return f"s{int(round(scale * 100))}"


def build_winui_cases(scales=None):
    """Build WinUI Reference.exe CaseSpec entries for all 7 layout trees at every scale."""
    scales = SCALES if scales is None else scales
    cases = []
    for scale in scales:
        tag = scale_tag(scale)
        # 1. Grid: own Padding + BorderThickness, Row/ColumnSpacing, Pixel/Auto/Star + min/max, RowSpan/ColumnSpan.
        cases.append({
            "id": f"grid-mixed-{tag}",
            "case_group": "layout",
            "control": "Grid",
            "theme": "Dark",
            "scale": scale,
            "layout_at_scale": True,
            "frame": {"width": 160, "height": 120},
            "properties": {
                "Name": "GridRoot",
                "Padding": [3, 5, 7, 9],
                "BorderThickness": [1, 2, 3, 4],
                "RowSpacing": 5,
                "ColumnSpacing": 7,
                "RowDefinitions": [
                    {"length": "21"},
                    {"length": "Auto", "min": 15},
                    {"length": "1*", "min": 10, "max": 50},
                    {"length": "2*"},
                ],
                "ColumnDefinitions": [
                    {"length": "25"},
                    {"length": "Auto", "max": 40},
                    {"length": "1*"},
                ],
            },
            "children": [
                {
                    "id": "cell-pix",
                    "control": "Border",
                    "properties": {"Name": "CellPix", "Grid.Row": 0, "Grid.Column": 0, "Background": "#FF224466"},
                },
                {
                    "id": "cell-span-col",
                    "control": "Border",
                    "properties": {
                        "Name": "CellSpanCol",
                        "Grid.Row": 0,
                        "Grid.Column": 1,
                        "Grid.ColumnSpan": 2,
                        "Background": "#FF335577",
                    },
                },
                {
                    "id": "cell-span-row",
                    "control": "Border",
                    "properties": {
                        "Name": "CellSpanRow",
                        "Grid.Row": 1,
                        "Grid.Column": 0,
                        "Grid.RowSpan": 2,
                        "Background": "#FF446688",
                    },
                },
                {
                    "id": "cell-auto",
                    "control": "Border",
                    "properties": {
                        "Name": "CellAuto",
                        "Grid.Row": 1,
                        "Grid.Column": 1,
                        "Width": 28,
                        "Height": 19,
                        "HorizontalAlignment": "Left",
                        "VerticalAlignment": "Top",
                        "Background": "#FF557799",
                    },
                },
                {
                    "id": "cell-star",
                    "control": "Border",
                    "properties": {
                        "Name": "CellStar",
                        "Grid.Row": 3,
                        "Grid.Column": 2,
                        "Background": "#FF6688AA",
                    },
                },
            ],
        })

        # 2. Unbounded vertical StackPanel -> Grid with Star rows (verifies IM_FIXED_INF DesiredSize + Rect).
        cases.append({
            "id": f"grid-unbounded-{tag}",
            "case_group": "layout",
            "control": "StackPanel",
            "theme": "Dark",
            "scale": scale,
            "layout_at_scale": True,
            "frame": {"width": 160, "height": 120},
            "properties": {
                "Name": "OuterStack",
                "Orientation": "Vertical",
                "Spacing": 4,
            },
            "children": [
                {
                    "id": "inner-grid",
                    "control": "Grid",
                    "properties": {
                        "Name": "InnerGrid",
                        "RowSpacing": 3,
                        "RowDefinitions": ["1*", "2*"],
                    },
                    "children": [
                        {
                            "id": "row-a",
                            "control": "Border",
                            "properties": {
                                "Name": "RowA",
                                "Grid.Row": 0,
                                "Width": 50,
                                "Height": 17,
                                "HorizontalAlignment": "Left",
                                "Background": "#FF226644",
                            },
                        },
                        {
                            "id": "row-b",
                            "control": "Border",
                            "properties": {
                                "Name": "RowB",
                                "Grid.Row": 1,
                                "Width": 70,
                                "Height": 23,
                                "HorizontalAlignment": "Left",
                                "Background": "#FF337755",
                            },
                        },
                    ],
                },
            ],
        })

        # 3. Vertical StackPanel with Padding, Spacing, and a Collapsed middle child.
        cases.append({
            "id": f"stack-vert-{tag}",
            "case_group": "layout",
            "control": "StackPanel",
            "theme": "Dark",
            "scale": scale,
            "layout_at_scale": True,
            "frame": {"width": 140, "height": 100},
            "properties": {
                "Name": "VStack",
                "Orientation": "Vertical",
                "Padding": [4, 6, 8, 10],
                "Spacing": 7,
            },
            "children": [
                {
                    "id": "v-first",
                    "control": "Border",
                    "properties": {"Name": "VFirst", "Height": 15, "Background": "#FF224488"},
                },
                {
                    "id": "v-mid",
                    "control": "Border",
                    "properties": {
                        "Name": "VMidCollapsed",
                        "Height": 30,
                        "Visibility": "Collapsed",
                        "Background": "#FF884422",
                    },
                },
                {
                    "id": "v-last",
                    "control": "Border",
                    "properties": {"Name": "VLast", "Height": 19, "Background": "#FF228844"},
                },
            ],
        })

        # 4. Horizontal StackPanel with Spacing and cross-axis Top/Center/Bottom alignments.
        cases.append({
            "id": f"stack-horiz-{tag}",
            "case_group": "layout",
            "control": "StackPanel",
            "theme": "Dark",
            "scale": scale,
            "layout_at_scale": True,
            "frame": {"width": 160, "height": 80},
            "properties": {
                "Name": "HStack",
                "Orientation": "Horizontal",
                "Spacing": 5,
            },
            "children": [
                {
                    "id": "h-top",
                    "control": "Border",
                    "properties": {
                        "Name": "HTop",
                        "Width": 21,
                        "Height": 17,
                        "VerticalAlignment": "Top",
                        "Background": "#FF224488",
                    },
                },
                {
                    "id": "h-center",
                    "control": "Border",
                    "properties": {
                        "Name": "HCenter",
                        "Width": 25,
                        "Height": 19,
                        "VerticalAlignment": "Center",
                        "Background": "#FF448822",
                    },
                },
                {
                    "id": "h-bottom",
                    "control": "Border",
                    "properties": {
                        "Name": "HBottom",
                        "Width": 29,
                        "Height": 23,
                        "VerticalAlignment": "Bottom",
                        "Background": "#FF882244",
                    },
                },
            ],
        })

        # 5. Border with UseLayoutRounding=true vs UseLayoutRounding=false and fractional centered child (9.5 offset).
        cases.append({
            "id": f"border-round-{tag}",
            "case_group": "layout",
            "control": "Grid",
            "theme": "Dark",
            "scale": scale,
            "layout_at_scale": True,
            "frame": {"width": 120, "height": 60},
            "properties": {
                "Name": "RoundGrid",
                "ColumnDefinitions": ["60", "60"],
            },
            "children": [
                {
                    "id": "b-round",
                    "control": "Border",
                    "properties": {
                        "Name": "RoundedBorder",
                        "Grid.Column": 0,
                        "Width": 40,
                        "Height": 40,
                        "BorderThickness": [1, 2, 3, 1],
                        "Padding": [2, 3, 2, 3],
                        "Margin": [3, 5, 7, 9],
                        "HorizontalAlignment": "Left",
                        "VerticalAlignment": "Top",
                        "UseLayoutRounding": True,
                        "Background": "#FF223344",
                    },
                    "children": [
                        {
                            "id": "b-round-ch",
                            "control": "Border",
                            "properties": {
                                "Name": "RoundedChild",
                                "Width": 11,
                                "Height": 11,
                                "HorizontalAlignment": "Center",
                                "VerticalAlignment": "Center",
                                "UseLayoutRounding": True,
                                "Background": "#FF4488CC",
                            },
                        },
                    ],
                },
                {
                    "id": "b-unround",
                    "control": "Border",
                    "properties": {
                        "Name": "UnroundedBorder",
                        "Grid.Column": 1,
                        "Width": 40,
                        "Height": 40,
                        "BorderThickness": [1, 2, 3, 1],
                        "Padding": [2, 3, 2, 3],
                        "Margin": [3, 5, 7, 9],
                        "HorizontalAlignment": "Left",
                        "VerticalAlignment": "Top",
                        "UseLayoutRounding": False,
                        "Background": "#FF443322",
                    },
                    "children": [
                        {
                            "id": "b-unround-ch",
                            "control": "Border",
                            "properties": {
                                "Name": "UnroundedChild",
                                "Width": 11,
                                "Height": 11,
                                "HorizontalAlignment": "Center",
                                "VerticalAlignment": "Center",
                                "UseLayoutRounding": False,
                                "Background": "#FFCC8844",
                            },
                        },
                    ],
                },
            ],
        })

        # 6. ContentPresenter inside RTL Grid with asymmetric Margin, Padding, BorderThickness, and content alignment.
        cases.append({
            "id": f"presenter-rtl-{tag}",
            "case_group": "layout",
            "control": "Grid",
            "theme": "Dark",
            "scale": scale,
            "layout_at_scale": True,
            "frame": {"width": 160, "height": 80},
            "properties": {"Name": "RtlRoot", "FlowDirection": "RightToLeft"},
            "children": [
                {
                    "id": "rtl-pres",
                    "control": "ContentPresenter",
                    "properties": {
                        "Name": "RtlPresenter",
                        "Width": 100,
                        "Height": 50,
                        "Margin": [3, 4, 11, 6],
                        "Padding": [5, 3, 9, 7],
                        "BorderThickness": [1, 2, 3, 4],
                        "HorizontalAlignment": "Left",
                        "VerticalAlignment": "Top",
                        "HorizontalContentAlignment": "Left",
                        "VerticalContentAlignment": "Bottom",
                        "Background": "#FF224466",
                    },
                    "children": [
                        {
                            "id": "rtl-ch",
                            "control": "Border",
                            "properties": {
                                "Name": "RtlChild",
                                "Width": 23,
                                "Height": 13,
                                "Background": "#FF88CCFF",
                            },
                        },
                    ],
                },
            ],
        })

        # 7. Button with sized child, asymmetric local Padding, and Left/Bottom content alignment.
        cases.append({
            "id": f"button-child-{tag}",
            "case_group": "layout",
            "control": "Button",
            "theme": "Dark",
            "scale": scale,
            "layout_at_scale": True,
            "frame": {"width": 160, "height": 64},
            "properties": {
                "Name": "BtnRoot",
                "Width": 110,
                "Height": 36,
                "BorderThickness": [1, 1, 1, 1],
                "Padding": [5, 3, 13, 7],
                "HorizontalAlignment": "Center",
                "VerticalAlignment": "Center",
                "HorizontalContentAlignment": "Left",
                "VerticalContentAlignment": "Bottom",
                "IsTabStop": False,
            },
            "children": [
                {
                    "id": "btn-ch",
                    "control": "Border",
                    "properties": {
                        "Name": "BtnChild",
                        "Width": 31,
                        "Height": 13,
                        "Background": "#FF66AAEE",
                    },
                },
            ],
        })
    return cases


def index_named_nodes(layout_tree):
    """Collect {Name: {Rect, DesiredSize, Visibility}} for all non-empty named nodes in a LayoutNode tree."""
    out = {}

    def walk(node):
        name = node.get("Name") or ""
        if name and name not in out:
            out[name] = {
                "Rect": node["Rect"],
                "DesiredSize": node.get("DesiredSize"),
                "Visibility": node.get("Visibility", "Visible"),
            }
        for ch in node.get("Children", []):
            walk(ch)

    walk(layout_tree)
    return out


def compare_named_layouts(case_id, base_name, expected_nodes, actual_nodes, tol=0.05):
    """Compare required named nodes (Rect and DesiredSize) between expected and actual dicts."""
    diffs = []
    required = REQUIRED_NODES[base_name]
    for name in required:
        if name not in expected_nodes:
            diffs.append(f"{case_id}: expected missing required node {name!r}")
            continue
        if name not in actual_nodes:
            diffs.append(f"{case_id}: actual missing required node {name!r}")
            continue
        er = expected_nodes[name]["Rect"]
        ar = actual_nodes[name]["Rect"]
        for k in ("X", "Y", "Width", "Height"):
            if abs(float(er[k]) - float(ar[k])) > tol:
                diffs.append(f"{case_id}.{name}.Rect.{k}: expected {er[k]} vs actual {ar[k]}")
        ed = expected_nodes[name].get("DesiredSize")
        ad = actual_nodes[name].get("DesiredSize")
        if ed is not None and ad is not None:
            for k in ("Width", "Height"):
                if abs(float(ed[k]) - float(ad[k])) > tol:
                    diffs.append(f"{case_id}.{name}.DesiredSize.{k}: expected {ed[k]} vs actual {ad[k]}")
    return diffs

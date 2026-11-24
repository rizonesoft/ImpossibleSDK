"""Frame and script-log checks the stored and the live reference tests share."""


def extract_mask_rects(layout_node):
    """Collect frame-relative logical rectangles for all text and icon nodes in a Dump.cs layout tree."""
    rects = []
    if layout_node.get("IsText") or layout_node.get("IsIcon"):
        r = layout_node["Rect"]
        rects.append((layout_node.get("Name") or layout_node.get("Type"), r["X"], r["Y"], r["Width"], r["Height"]))
    for ch in layout_node.get("Children", []):
        rects.extend(extract_mask_rects(ch))
    return rects


def compare_frames_with_layout_mask(mine, real, width, height, scale, cand_rects, ref_rects, limit=0, rect_tol_px=0):
    """Compare candidate and reference rasterized text/icon pixel bounds FIRST, then compare unmasked pixels."""
    if len(cand_rects) != len(ref_rects):
        raise ValueError(f"masked element count mismatch: candidate={len(cand_rects)} vs reference={len(ref_rects)}")
    pixel_masks = []
    for (cname, cx, cy, cw, ch), (rname, rx, ry, rw, rh) in zip(cand_rects, ref_rects):
        if cname != rname:
            raise ValueError(f"masked element name mismatch: {cname!r} vs {rname!r}")
        cx0, cy0, cx1, cy1 = round(cx * scale), round(cy * scale), round((cx + cw) * scale), round((cy + ch) * scale)
        rx0, ry0, rx1, ry1 = round(rx * scale), round(ry * scale), round((rx + rw) * scale), round((ry + rh) * scale)
        if max(abs(cx0 - rx0), abs(cy0 - ry0), abs(cx1 - rx1), abs(cy1 - ry1)) > rect_tol_px:
            raise ValueError(
                f"masked element {rname!r} moved at scale={scale}: cand_px=({cx0},{cy0},{cx1},{cy1}) vs ref_px=({rx0},{ry0},{rx1},{ry1})"
            )
        # Convert logical DIPs to target pixels and clip to frame bounds.
        x0 = max(0, min(width, rx0))
        y0 = max(0, min(height, ry0))
        x1 = max(0, min(width, rx1))
        y1 = max(0, min(height, ry1))
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
    step_pressed = set()
    step_released = set()
    gesture_steps = set()
    for ev in events:
        step = ev.get("Step", "")
        name = ev.get("Event", "")
        ptype = ev.get("PointerType", "")
        if step.endswith(":pointer_click") or step.endswith(":touch_tap"):
            gesture_steps.add(step)
            if name == "PointerPressed":
                step_pressed.add(step)
            elif name == "PointerReleased":
                if step not in step_pressed:
                    raise ValueError(f"script log step {step!r} recorded PointerReleased before PointerPressed")
                step_released.add(step)
        if name == "PointerPressed" and ptype == "Mouse":
            seen_mods.add("pointer")
        elif name.startswith("KeyDown:"):
            seen_mods.add("keyboard")
        elif name == "PointerPressed" and ptype == "Touch":
            seen_mods.add("touch")
        if name == "Click":
            clicks += 1
    for gstep in sorted(gesture_steps):
        if gstep not in step_pressed or gstep not in step_released:
            raise ValueError(f"script log step {gstep!r} missing complete press/release pair")
    missing = [m for m in require_modalities if m not in seen_mods]
    if missing:
        raise ValueError(f"script log missing required input modalities: {missing}")
    if clicks < len(require_modalities):
        raise ValueError(f"expected at least {len(require_modalities)} Click events, got {clicks}")
    if final.get("clicks") != clicks:
        raise ValueError(f"FinalState.clicks ({final.get('clicks')}) != logged Click events ({clicks})")
    if not final.get("focused") or final.get("focused") == "none":
        raise ValueError(f"FinalState.focused not set: {final}")

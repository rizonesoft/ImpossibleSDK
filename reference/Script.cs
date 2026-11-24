using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text.Json.Serialization;
using System.Threading.Tasks;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.System;
using WinRT.Interop;

namespace Reference;

[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed class ScriptActionSpec
{
    [JsonPropertyName("action")] public string Action { get; set; } = "";
    [JsonPropertyName("key")] public string? Key { get; set; }
    [JsonPropertyName("modifiers")] public List<string>? Modifiers { get; set; }
    [JsonPropertyName("use_focused")] public bool UseFocused { get; set; }
    [JsonPropertyName("x")] public double? X { get; set; }
    [JsonPropertyName("y")] public double? Y { get; set; }
}

public sealed class ScriptEventDto
{
    public string Step { get; set; } = "";
    public string Event { get; set; } = "";
    public string PointerType { get; set; } = "";
    public string Target { get; set; } = "";
    public string Focused { get; set; } = "";
    public string State { get; set; } = "";
}

public sealed class ScriptResultDto
{
    public List<ScriptEventDto> Events { get; set; } = new();
    public Dictionary<string, object?> FinalState { get; set; } = new();
    public List<string> CleanupReleases { get; set; } = new();
}

public static class Script
{
    private const uint INPUT_MOUSE = 0;
    private const uint INPUT_KEYBOARD = 1;
    private const uint MOUSEEVENTF_MOVE = 0x0001;
    private const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    private const uint MOUSEEVENTF_LEFTUP = 0x0004;
    private const uint MOUSEEVENTF_ABSOLUTE = 0x8000;
    private const uint MOUSEEVENTF_VIRTUALDESK = 0x4000;
    private const uint KEYEVENTF_KEYUP = 0x0002;
    private const int SM_XVIRTUALSCREEN = 76;
    private const int SM_YVIRTUALSCREEN = 77;
    private const int SM_CXVIRTUALSCREEN = 78;
    private const int SM_CYVIRTUALSCREEN = 79;

    private const uint PT_TOUCH = 0x00000002;
    private const uint POINTER_FLAG_NONE = 0x00000000;
    private const uint POINTER_FLAG_INRANGE = 0x00000002;
    private const uint POINTER_FLAG_INCONTACT = 0x00000004;
    private const uint POINTER_FLAG_DOWN = 0x00010000;
    private const uint POINTER_FLAG_UPDATE = 0x00020000;
    private const uint POINTER_FLAG_UP = 0x00040000;
    private const uint TOUCH_MASK_CONTACTAREA = 0x00000001;
    private const uint TOUCH_MASK_PRESSURE = 0x00000004;
    private const uint TOUCH_FEEDBACK_NONE = 0x3;

    [StructLayout(LayoutKind.Sequential)]
    private struct POINT { public int X; public int Y; }

    [StructLayout(LayoutKind.Sequential)]
    private struct RECT { public int Left, Top, Right, Bottom; }

    [StructLayout(LayoutKind.Sequential)]
    private struct MOUSEINPUT
    {
        public int dx;
        public int dy;
        public uint mouseData;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KEYBDINPUT
    {
        public ushort wVk;
        public ushort wScan;
        public uint dwFlags;
        public uint time;
        public IntPtr dwExtraInfo;
    }

    [StructLayout(LayoutKind.Explicit)]
    private struct INPUTUNION
    {
        [FieldOffset(0)] public MOUSEINPUT mi;
        [FieldOffset(0)] public KEYBDINPUT ki;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct INPUT
    {
        public uint type;
        public INPUTUNION u;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINTER_INFO
    {
        public uint pointerType;
        public uint pointerId;
        public uint frameId;
        public uint pointerFlags;
        public IntPtr sourceDevice;
        public IntPtr hwndTarget;
        public POINT ptPixelLocation;
        public POINT ptHimetricLocation;
        public POINT ptPixelLocationRaw;
        public POINT ptHimetricLocationRaw;
        public uint dwTime;
        public uint historyCount;
        public int inputData;
        public uint dwKeyStates;
        public ulong PerformanceCount;
        public int ButtonChangeType;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct POINTER_TOUCH_INFO
    {
        public POINTER_INFO pointerInfo;
        public uint touchFlags;
        public uint touchMask;
        public RECT rcContact;
        public RECT rcContactRaw;
        public uint orientation;
        public uint pressure;
    }

    [DllImport("user32.dll")] private static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] private static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern int GetSystemMetrics(int nIndex);
    [DllImport("user32.dll", SetLastError = true)] private static extern uint SendInput(uint nInputs, INPUT[] pInputs, int cbSize);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool InitializeTouchInjection(uint maxCount, uint dwMode);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool InjectTouchInput(uint count, POINTER_TOUCH_INFO[] contacts);

    private static async Task WaitSettleAsync(Microsoft.UI.Dispatching.DispatcherQueue? dq, int minDelayMs)
    {
        await Task.Delay(minDelayMs);
        if (dq != null)
        {
            var tcs = new TaskCompletionSource<bool>();
            if (dq.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, () => tcs.TrySetResult(true)))
            {
                await tcs.Task;
            }
        }
    }

    private static bool touchInitialized;

    private static void EnsureTouch()
    {
        if (!touchInitialized)
        {
            if (!InitializeTouchInjection(1, TOUCH_FEEDBACK_NONE))
            {
                throw new InvalidOperationException($"InitializeTouchInjection failed (err={Marshal.GetLastWin32Error()})");
            }
            touchInitialized = true;
        }
    }

    private static void EnsureForeground(IntPtr hwnd, string step)
    {
        if (GetForegroundWindow() != hwnd)
        {
            SetForegroundWindow(hwnd);
            if (GetForegroundWindow() != hwnd)
            {
                throw new InvalidOperationException($"script step {step}: reference window lost foreground focus; refusing to inject desktop input");
            }
        }
    }

    private static POINT TargetScreenPoint(Window window, FrameworkElement target, double? relX, double? relY, bool requireInside, string step)
    {
        var hwnd = WindowNative.GetWindowHandle(window);
        double scale = target.XamlRoot?.RasterizationScale ?? 1.0;
        double w = target.ActualWidth;
        double h = target.ActualHeight;
        double lx = relX ?? (w * 0.5);
        double ly = relY ?? (h * 0.5);
        if (requireInside && (lx < 0 || lx > w || ly < 0 || ly > h))
        {
            throw new InvalidOperationException($"script step {step}: coordinate ({lx},{ly}) lies outside target bounds ({w}x{h})");
        }
        // Transform the exact local point through the full visual transform (handles Viewbox scale and RTL mirroring).
        var rootPt = target.TransformToVisual(null).TransformPoint(new Point(lx, ly));
        var pt = new POINT
        {
            X = (int)Math.Round(rootPt.X * scale),
            Y = (int)Math.Round(rootPt.Y * scale),
        };
        ClientToScreen(hwnd, ref pt);
        return pt;
    }

    private static void SendMouse(POINT pt, uint flags)
    {
        int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int vw = Math.Max(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
        int vh = Math.Max(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
        int nx = (int)Math.Round((pt.X - vx) * 65535.0 / Math.Max(1, vw - 1));
        int ny = (int)Math.Round((pt.Y - vy) * 65535.0 / Math.Max(1, vh - 1));
        var inp = new INPUT[]
        {
            new()
            {
                type = INPUT_MOUSE,
                u = new INPUTUNION
                {
                    mi = new MOUSEINPUT
                    {
                        dx = nx,
                        dy = ny,
                        dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK | flags,
                    }
                }
            }
        };
        if (SendInput(1, inp, Marshal.SizeOf<INPUT>()) != 1)
        {
            throw new InvalidOperationException($"SendInput(mouse) failed (err={Marshal.GetLastWin32Error()})");
        }
    }

    private static void SendKey(ushort vk, bool keyUp)
    {
        var inp = new INPUT[]
        {
            new()
            {
                type = INPUT_KEYBOARD,
                u = new INPUTUNION
                {
                    ki = new KEYBDINPUT
                    {
                        wVk = vk,
                        dwFlags = keyUp ? KEYEVENTF_KEYUP : 0,
                    }
                }
            }
        };
        if (SendInput(1, inp, Marshal.SizeOf<INPUT>()) != 1)
        {
            throw new InvalidOperationException($"SendInput(key) failed (err={Marshal.GetLastWin32Error()})");
        }
    }

    private static void SendTouch(POINT pt, uint flags)
    {
        EnsureTouch();
        var contact = new POINTER_TOUCH_INFO
        {
            pointerInfo = new POINTER_INFO
            {
                pointerType = PT_TOUCH,
                pointerId = 0,
                pointerFlags = flags,
                ptPixelLocation = pt,
            },
            touchFlags = 0,
            touchMask = 0,
        };
        if (!InjectTouchInput(1, new[] { contact }))
        {
            throw new InvalidOperationException($"InjectTouchInput failed (err={Marshal.GetLastWin32Error()}, pt={pt.X},{pt.Y}, flags=0x{flags:X})");
        }
    }

    private static string DescribeElement(object? obj)
    {
        if (obj is FrameworkElement fe)
        {
            return string.IsNullOrEmpty(fe.Name) ? fe.GetType().Name : $"{fe.GetType().Name}:{fe.Name}";
        }
        return obj?.GetType().Name ?? "none";
    }

    private static string DescribeFocused(XamlRoot? root) =>
        root == null ? "none" : DescribeElement(FocusManager.GetFocusedElement(root));

    private static string DescribeControlValue(FrameworkElement target, int clicks) =>
        target switch
        {
            ToggleSwitch ts => $"IsOn={ts.IsOn}",
            CheckBox cb => $"IsChecked={cb.IsChecked}",
            Button => $"Clicks={clicks}",
            TextBox tb => $"Text={tb.Text};Sel={tb.SelectionStart}:{tb.SelectionLength}",
            Slider sl => $"Value={sl.Value}",
            _ => "",
        };

    private static Dictionary<string, object?> SnapshotState(FrameworkElement target, int clicks)
    {
        var snap = new Dictionary<string, object?>
        {
            ["focused"] = DescribeFocused(target.XamlRoot),
        };
        switch (target)
        {
            case ToggleSwitch ts:
                snap["is_on"] = ts.IsOn;
                break;
            case CheckBox cb:
                snap["is_checked"] = cb.IsChecked;
                break;
            case Button:
                snap["clicks"] = clicks;
                break;
            case TextBox tb:
                snap["text"] = tb.Text;
                snap["selection_start"] = tb.SelectionStart;
                snap["selection_length"] = tb.SelectionLength;
                break;
            case Slider sl:
                snap["value"] = sl.Value;
                break;
        }
        var focusedEl = target.XamlRoot != null ? FocusManager.GetFocusedElement(target.XamlRoot) as FrameworkElement : null;
        if (focusedEl != null && !ReferenceEquals(focusedEl, target))
        {
            snap["focused_state"] = DescribeControlValue(focusedEl, clicks);
        }
        return snap;
    }

    private static readonly HashSet<VirtualKey> AllowedModifiers = new()
    {
        VirtualKey.Shift, VirtualKey.LeftShift, VirtualKey.RightShift,
        VirtualKey.Control, VirtualKey.LeftControl, VirtualKey.RightControl,
    };

    private static readonly HashSet<VirtualKey> ForbiddenSystemKeys = new()
    {
        VirtualKey.LeftWindows, VirtualKey.RightWindows,
        VirtualKey.Menu, VirtualKey.LeftMenu, VirtualKey.RightMenu,
        VirtualKey.F4, VirtualKey.Print, VirtualKey.Snapshot, VirtualKey.Sleep,
    };

    private static VirtualKey ParseNamedVirtualKey(string raw, int stepIndex, string field)
    {
        string trimmed = raw?.Trim() ?? "";
        bool allAlphaNum = trimmed.Length > 0;
        for (int idx = 0; idx < trimmed.Length; idx++)
        {
            if (!char.IsLetterOrDigit(trimmed[idx]))
            {
                allAlphaNum = false;
                break;
            }
        }
        if (!allAlphaNum || int.TryParse(trimmed, out _) || !Enum.TryParse<VirtualKey>(trimmed, ignoreCase: true, out var vk) || !Enum.IsDefined(typeof(VirtualKey), vk))
        {
            throw new InvalidOperationException($"script step {stepIndex}: invalid {field} '{raw}' (must be a single named VirtualKey)");
        }
        return vk;
    }

    public static async Task<ScriptResultDto> RunAsync(Window window, FrameworkElement target, IReadOnlyList<ScriptActionSpec> actions)
    {
        var hwnd = WindowNative.GetWindowHandle(window);
        if (!SetForegroundWindow(hwnd) && GetForegroundWindow() != hwnd)
        {
            throw new InvalidOperationException("Script.RunAsync: failed to activate reference window as foreground");
        }
        await Task.Delay(120);
        EnsureForeground(hwnd, "init");

        var result = new ScriptResultDto();
        int clicks = 0;
        string currentStep = "init";

        void Record(string evName, string pointerType = "", object? source = null)
        {
            object activeTarget = source ?? target;
            FrameworkElement stateTarget = (source as FrameworkElement) ?? target;
            result.Events.Add(new ScriptEventDto
            {
                Step = currentStep,
                Event = evName,
                PointerType = pointerType,
                Target = DescribeElement(activeTarget),
                Focused = DescribeFocused(target.XamlRoot),
                State = DescribeControlValue(stateTarget, clicks),
            });
        }

        TaskCompletionSource<bool>? pendingTouchPressed = null;
        RoutedEventHandler gotFocus = (_, e) => Record("GotFocus", "", e.OriginalSource);
        RoutedEventHandler lostFocus = (_, e) => Record("LostFocus", "", e.OriginalSource);
        PointerEventHandler ptrEntered = (_, e) => Record("PointerEntered", e.Pointer.PointerDeviceType.ToString(), e.OriginalSource);
        PointerEventHandler ptrPressed = (_, e) =>
        {
            Record("PointerPressed", e.Pointer.PointerDeviceType.ToString(), e.OriginalSource);
            pendingTouchPressed?.TrySetResult(true);
        };
        PointerEventHandler ptrReleased = (_, e) => Record("PointerReleased", e.Pointer.PointerDeviceType.ToString(), e.OriginalSource);
        PointerEventHandler ptrExited = (_, e) => Record("PointerExited", e.Pointer.PointerDeviceType.ToString(), e.OriginalSource);
        KeyEventHandler keyDown = (_, e) => Record($"KeyDown:{e.Key}", "", e.OriginalSource);
        KeyEventHandler keyUp = (_, e) => Record($"KeyUp:{e.Key}", "", e.OriginalSource);
        TappedEventHandler tapped = (_, e) => Record("Tapped", e.PointerDeviceType.ToString(), e.OriginalSource);
        RoutedEventHandler clickHandler = (_, e) => { clicks++; Record("Click", "", e.OriginalSource); };
        RoutedEventHandler toggledHandler = (_, e) => Record("Toggled", "", e.OriginalSource);
        RoutedEventHandler checkedHandler = (_, e) => Record("Checked", "", e.OriginalSource);
        RoutedEventHandler uncheckedHandler = (_, e) => Record("Unchecked", "", e.OriginalSource);

        target.GotFocus += gotFocus;
        target.LostFocus += lostFocus;
        target.AddHandler(UIElement.PointerEnteredEvent, ptrEntered, true);
        target.AddHandler(UIElement.PointerPressedEvent, ptrPressed, true);
        target.AddHandler(UIElement.PointerReleasedEvent, ptrReleased, true);
        target.AddHandler(UIElement.PointerExitedEvent, ptrExited, true);
        target.AddHandler(UIElement.KeyDownEvent, keyDown, true);
        target.AddHandler(UIElement.KeyUpEvent, keyUp, true);
        target.AddHandler(UIElement.TappedEvent, tapped, true);
        if (target is ButtonBase bb) bb.Click += clickHandler;
        if (target is ToggleSwitch ts) ts.Toggled += toggledHandler;
        if (target is ToggleButton tb) { tb.Checked += checkedHandler; tb.Unchecked += uncheckedHandler; }

        // Park pointer outside the control (still inside window client area) before running script so PointerEntered is deterministic.
        var outside = TargetScreenPoint(window, target, -15, -15, requireInside: false, step: "park");
        EnsureForeground(hwnd, "park");
        SendMouse(outside, MOUSEEVENTF_MOVE);
        await Task.Delay(80);

        bool leftMouseDown = false;
        POINT lastMousePt = outside;
        var heldKeys = new List<ushort>();
        POINT? activeTouchPt = null;

        Exception? runError = null;
        try
        {
            for (int i = 0; i < actions.Count; i++)
            {
                var step = actions[i];
                currentStep = $"{i}:{step.Action}";
                EnsureForeground(hwnd, currentStep);
                switch (step.Action)
                {
                    case "focus":
                        if (target is not Control ctrl)
                        {
                            throw new InvalidOperationException($"script step {currentStep}: target '{DescribeElement(target)}' is not a Control and cannot receive focus");
                        }
                        {
                            var fr = await FocusManager.TryFocusAsync(ctrl, FocusState.Keyboard);
                            if (!fr.Succeeded)
                            {
                                throw new InvalidOperationException($"script step {currentStep}: TryFocusAsync failed (IsTabStop={ctrl.IsTabStop}, IsEnabled={ctrl.IsEnabled})");
                            }
                            await Task.Delay(80);
                        }
                        break;

                    case "pointer_move":
                    {
                        var pt = TargetScreenPoint(window, target, step.X, step.Y, requireInside: false, step: currentStep);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_MOVE);
                        lastMousePt = pt;
                        await Task.Delay(80);
                        break;
                    }

                    case "pointer_down":
                    {
                        var pt = TargetScreenPoint(window, target, step.X, step.Y, requireInside: true, step: currentStep);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_MOVE);
                        lastMousePt = pt;
                        await Task.Delay(60);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_LEFTDOWN);
                        leftMouseDown = true;
                        await Task.Delay(80);
                        break;
                    }

                    case "pointer_up":
                    {
                        var pt = (step.X.HasValue || step.Y.HasValue)
                            ? TargetScreenPoint(window, target, step.X, step.Y, requireInside: false, step: currentStep)
                            : lastMousePt;
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_MOVE);
                        lastMousePt = pt;
                        await Task.Delay(40);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_LEFTUP);
                        leftMouseDown = false;
                        await Task.Delay(120);
                        break;
                    }

                    case "pointer_click":
                    {
                        var pt = TargetScreenPoint(window, target, step.X, step.Y, requireInside: true, step: currentStep);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_MOVE);
                        lastMousePt = pt;
                        await Task.Delay(80);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_LEFTDOWN);
                        leftMouseDown = true;
                        await Task.Delay(80);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(pt, MOUSEEVENTF_LEFTUP);
                        leftMouseDown = false;
                        await Task.Delay(120);
                        EnsureForeground(hwnd, currentStep);
                        SendMouse(outside, MOUSEEVENTF_MOVE);
                        lastMousePt = outside;
                        await Task.Delay(80);
                        break;
                    }

                    case "key_down":
                    case "key_up":
                    case "key_press":
                    {
                        string keyName = step.Key ?? "Space";
                        var vk = ParseNamedVirtualKey(keyName, i, "key");
                        if (ForbiddenSystemKeys.Contains(vk))
                        {
                            throw new InvalidOperationException($"script step {i}: system key '{keyName}' is disallowed in reference scripts");
                        }
                        var modVks = new List<ushort>();
                        if (step.Modifiers != null && step.Modifiers.Count > 0)
                        {
                            if (step.Action != "key_press")
                            {
                                throw new InvalidOperationException($"script step {i}: modifiers is only supported on key_press (not '{step.Action}')");
                            }
                            foreach (var m in step.Modifiers)
                            {
                                var mvk = ParseNamedVirtualKey(m, i, "modifier");
                                if (!AllowedModifiers.Contains(mvk))
                                {
                                    throw new InvalidOperationException($"script step {i}: modifier '{m}' is disallowed (only Shift/Control allowed)");
                                }
                                modVks.Add((ushort)mvk);
                            }
                        }
                        if (step.Action != "key_up")
                        {
                            bool ctrlActive = vk == VirtualKey.Control
                                || vk == VirtualKey.LeftControl
                                || vk == VirtualKey.RightControl
                                || heldKeys.Contains((ushort)VirtualKey.Control)
                                || heldKeys.Contains((ushort)VirtualKey.LeftControl)
                                || heldKeys.Contains((ushort)VirtualKey.RightControl)
                                || modVks.Contains((ushort)VirtualKey.Control)
                                || modVks.Contains((ushort)VirtualKey.LeftControl)
                                || modVks.Contains((ushort)VirtualKey.RightControl);
                            bool escActive = vk == VirtualKey.Escape || heldKeys.Contains((ushort)VirtualKey.Escape);
                            if (ctrlActive && escActive)
                            {
                                throw new InvalidOperationException($"script step {i}: Control+Escape is disallowed in reference scripts (including across sequenced steps)");
                            }
                        }
                        if (!step.UseFocused && target is Control kCtrl && !ReferenceEquals(FocusManager.GetFocusedElement(target.XamlRoot), kCtrl))
                        {
                            var fr = await FocusManager.TryFocusAsync(kCtrl, FocusState.Keyboard);
                            if (!fr.Succeeded)
                            {
                                throw new InvalidOperationException($"script step {currentStep}: TryFocusAsync failed before {step.Action} (IsTabStop={kCtrl.IsTabStop})");
                            }
                            await Task.Delay(80);
                        }
                        if (step.Action == "key_down")
                        {
                            EnsureForeground(hwnd, currentStep);
                            SendKey((ushort)vk, false);
                            heldKeys.Add((ushort)vk);
                            await Task.Delay(80);
                        }
                        else if (step.Action == "key_up")
                        {
                            EnsureForeground(hwnd, currentStep);
                            SendKey((ushort)vk, true);
                            heldKeys.Remove((ushort)vk);
                            await Task.Delay(80);
                        }
                        else
                        {
                            var addedMods = new List<ushort>();
                            foreach (var mvk in modVks)
                            {
                                if (!heldKeys.Contains(mvk))
                                {
                                    EnsureForeground(hwnd, currentStep);
                                    SendKey(mvk, false);
                                    heldKeys.Add(mvk);
                                    addedMods.Add(mvk);
                                    await Task.Delay(40);
                                }
                            }
                            EnsureForeground(hwnd, currentStep);
                            SendKey((ushort)vk, false);
                            heldKeys.Add((ushort)vk);
                            await Task.Delay(80);
                            EnsureForeground(hwnd, currentStep);
                            SendKey((ushort)vk, true);
                            heldKeys.Remove((ushort)vk);
                            await Task.Delay(60);
                            for (int mi = addedMods.Count - 1; mi >= 0; mi--)
                            {
                                EnsureForeground(hwnd, currentStep);
                                SendKey(addedMods[mi], true);
                                heldKeys.Remove(addedMods[mi]);
                                await Task.Delay(40);
                            }
                            await Task.Delay(80);
                        }
                        break;
                    }

                    case "touch_down":
                    {
                        var pt = TargetScreenPoint(window, target, step.X, step.Y, requireInside: true, step: currentStep);
                        EnsureForeground(hwnd, currentStep);
                        SendTouch(pt, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
                        activeTouchPt = pt;
                        await Task.Delay(80);
                        break;
                    }

                    case "touch_move":
                    {
                        var pt = TargetScreenPoint(window, target, step.X, step.Y, requireInside: false, step: currentStep);
                        EnsureForeground(hwnd, currentStep);
                        SendTouch(pt, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
                        activeTouchPt = pt;
                        await Task.Delay(80);
                        break;
                    }

                    case "touch_up":
                    {
                        var pt = (step.X.HasValue || step.Y.HasValue)
                            ? TargetScreenPoint(window, target, step.X, step.Y, requireInside: false, step: currentStep)
                            : (activeTouchPt ?? outside);
                        EnsureForeground(hwnd, currentStep);
                        SendTouch(pt, POINTER_FLAG_UP);
                        activeTouchPt = null;
                        await Task.Delay(120);
                        break;
                    }

                    case "touch_tap":
                    {
                        var pt = TargetScreenPoint(window, target, step.X, step.Y, requireInside: true, step: currentStep);
                        pendingTouchPressed = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                        EnsureForeground(hwnd, currentStep);
                        SendTouch(pt, POINTER_FLAG_DOWN | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
                        activeTouchPt = pt;
                        bool expectPointerFocus = target is Control fc && fc.IsTabStop && fc.IsEnabled && fc.AllowFocusOnInteraction;
                        bool settled = false;
                        bool pressed = false;
                        bool focused = false;
                        for (int tick = 0; tick < 25; tick++)
                        {
                            await Task.Delay(30);
                            pressed = pendingTouchPressed.Task.IsCompleted;
                            focused = !expectPointerFocus || (target is Control c2 && c2.FocusState == FocusState.Pointer);
                            if (pressed && focused)
                            {
                                settled = true;
                                break;
                            }
                            EnsureForeground(hwnd, currentStep);
                            SendTouch(pt, POINTER_FLAG_UPDATE | POINTER_FLAG_INRANGE | POINTER_FLAG_INCONTACT);
                        }
                        pendingTouchPressed = null;
                        if (!settled)
                        {
                            throw new InvalidOperationException(
                                $"script step {currentStep}: touch_tap did not settle press/pointer-focus before release (pressed={pressed}, focused={focused})"
                            );
                        }
                        await WaitSettleAsync(target.DispatcherQueue, 10);
                        EnsureForeground(hwnd, currentStep);
                        SendTouch(pt, POINTER_FLAG_UP);
                        activeTouchPt = null;
                        await WaitSettleAsync(target.DispatcherQueue, 140);
                        break;
                    }

                    default:
                        throw new InvalidOperationException($"script step {i}: unknown action '{step.Action}'");
                }
            }
        }
        catch (Exception ex)
        {
            runError = ex;
        }
        finally
        {
            // Balanced input cleanup: never leave desktop mouse, keyboard, or touch contacts held down on failure.
            var cleanupErrors = new List<string>();
            if (leftMouseDown)
            {
                try { SendMouse(lastMousePt, MOUSEEVENTF_LEFTUP); result.CleanupReleases.Add("mouse_left_up"); }
                catch (Exception cex) { cleanupErrors.Add($"mouse_left_up:{cex.Message}"); }
                leftMouseDown = false;
            }
            for (int hi = heldKeys.Count - 1; hi >= 0; hi--)
            {
                ushort hvk = heldKeys[hi];
                try { SendKey(hvk, true); result.CleanupReleases.Add($"key_up:{(VirtualKey)hvk}"); }
                catch (Exception cex) { cleanupErrors.Add($"key_up:{(VirtualKey)hvk}:{cex.Message}"); }
            }
            heldKeys.Clear();
            if (activeTouchPt.HasValue)
            {
                try { SendTouch(activeTouchPt.Value, POINTER_FLAG_UP); result.CleanupReleases.Add("touch_up"); }
                catch (Exception cex) { cleanupErrors.Add($"touch_up:{cex.Message}"); }
                activeTouchPt = null;
            }
            if (runError == null && GetForegroundWindow() == hwnd)
            {
                try { SendMouse(outside, MOUSEEVENTF_MOVE); } catch (Exception cex) { cleanupErrors.Add($"park_mouse:{cex.Message}"); }
            }
            target.GotFocus -= gotFocus;
            target.LostFocus -= lostFocus;
            target.RemoveHandler(UIElement.PointerEnteredEvent, ptrEntered);
            target.RemoveHandler(UIElement.PointerPressedEvent, ptrPressed);
            target.RemoveHandler(UIElement.PointerReleasedEvent, ptrReleased);
            target.RemoveHandler(UIElement.PointerExitedEvent, ptrExited);
            target.RemoveHandler(UIElement.KeyDownEvent, keyDown);
            target.RemoveHandler(UIElement.KeyUpEvent, keyUp);
            target.RemoveHandler(UIElement.TappedEvent, tapped);
            if (target is ButtonBase bb2) bb2.Click -= clickHandler;
            if (target is ToggleSwitch ts2) ts2.Toggled -= toggledHandler;
            if (target is ToggleButton tb2) { tb2.Checked -= checkedHandler; tb2.Unchecked -= uncheckedHandler; }
            if (runError != null)
            {
                string relInfo = $"cleanup_releases=[{string.Join(",", result.CleanupReleases)}]";
                string errInfo = cleanupErrors.Count > 0 ? $"; cleanup_errors=[{string.Join(",", cleanupErrors)}]" : "";
                throw new InvalidOperationException($"{runError.Message} ({relInfo}{errInfo})", runError);
            }
            if (cleanupErrors.Count > 0)
            {
                throw new InvalidOperationException($"script cleanup failed: {string.Join("; ", cleanupErrors)}");
            }
        }

        result.FinalState = SnapshotState(target, clicks);
        return result;
    }
}

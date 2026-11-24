using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Animation;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.UI;
using Windows.UI.ViewManagement;

namespace Reference;

[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed class CaseFileSpec
{
    [JsonPropertyName("cases")] public List<CaseSpec> Cases { get; set; } = new();
}

[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed class FrameSpec
{
    [JsonPropertyName("width")] public int Width { get; set; } = 160;
    [JsonPropertyName("height")] public int Height { get; set; } = 64;
    [JsonPropertyName("background")] public string? Background { get; set; }
}

[JsonUnmappedMemberHandling(JsonUnmappedMemberHandling.Disallow)]
public sealed class CaseSpec
{
    [JsonPropertyName("id")] public string Id { get; set; } = "";
    [JsonPropertyName("case_group")] public string? CaseGroup { get; set; }
    [JsonPropertyName("control")] public string Control { get; set; } = "";
    [JsonPropertyName("style")] public string? Style { get; set; }
    [JsonPropertyName("theme")] public string? Theme { get; set; }
    [JsonPropertyName("scale")] public double Scale { get; set; } = 1.0;
    [JsonPropertyName("flow_direction")] public string? FlowDirection { get; set; }
    [JsonPropertyName("state")] public string? State { get; set; }
    [JsonPropertyName("use_transitions")] public bool UseTransitions { get; set; }
    [JsonPropertyName("storyboard_timeout_ms")] public int StoryboardTimeoutMs { get; set; } = 3000;
    [JsonPropertyName("frame")] public FrameSpec Frame { get; set; } = new();
    [JsonPropertyName("properties")] public Dictionary<string, JsonElement>? Properties { get; set; }
    [JsonPropertyName("items")] public List<string>? Items { get; set; }
    [JsonPropertyName("children")] public List<CaseSpec>? Children { get; set; }
    [JsonPropertyName("script")] public List<ScriptActionSpec>? Script { get; set; }
    [JsonPropertyName("dump_tree")] public bool DumpTree { get; set; } = true;
    [JsonPropertyName("probe_box")] public int[]? ProbeBox { get; set; }
    [JsonPropertyName("probe_radii")] public double[]? ProbeRadii { get; set; }
}

public partial class App : Application
{
    private Window? window;

    public App() => InitializeComponent();

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        var cli = Environment.GetCommandLineArgs();
        string folder = cli.Length > 1 ? cli[1] : throw new InvalidOperationException("Usage: Reference.exe <out_dir> [--cases <path>]");
        string? casesPath = null;
        for (int i = 2; i < cli.Length; i++)
        {
            if (cli[i] == "--cases" && i + 1 < cli.Length)
            {
                casesPath = cli[++i];
            }
        }

        try
        {
            Directory.CreateDirectory(folder);
            var canvas = new Grid();
            window = new Window { Content = canvas, Title = "Impossible UI reference" };
            window.AppWindow.Resize(new Windows.Graphics.SizeInt32(900, 700));
            window.Activate();
            await Task.Delay(800);
            await RunCases(window, canvas, folder, casesPath);
            Environment.Exit(0);
        }
        catch (Exception error)
        {
            File.WriteAllText(Path.Combine(folder, "error.txt"), error.ToString());
            Environment.Exit(1);
        }
    }

    private static void CheckNoCaseCollidingKeys(JsonElement elem, string path)
    {
        if (elem.ValueKind == JsonValueKind.Object)
        {
            var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            foreach (var prop in elem.EnumerateObject())
            {
                if (!seen.Add(prop.Name))
                {
                    throw new InvalidOperationException($"{path}: duplicate case-colliding JSON key '{prop.Name}'");
                }
                CheckNoCaseCollidingKeys(prop.Value, $"{path}.{prop.Name}");
            }
        }
        else if (elem.ValueKind == JsonValueKind.Array)
        {
            int idx = 0;
            foreach (var item in elem.EnumerateArray())
            {
                CheckNoCaseCollidingKeys(item, $"{path}[{idx++}]");
            }
        }
    }

    private static List<CaseSpec> LoadCases(string? casesPath)
    {
        var opts = new JsonSerializerOptions { PropertyNameCaseInsensitive = true };
        var files = new List<string>();
        if (!string.IsNullOrEmpty(casesPath))
        {
            if (Directory.Exists(casesPath))
            {
                files.AddRange(Directory.GetFiles(casesPath, "*.json").OrderBy(p => p, StringComparer.Ordinal));
            }
            else if (File.Exists(casesPath))
            {
                files.Add(casesPath);
            }
            else
            {
                throw new InvalidOperationException($"cases path not found: {casesPath}");
            }
        }
        else
        {
            string defaultDir = Path.Combine(AppContext.BaseDirectory, "cases");
            if (!Directory.Exists(defaultDir))
            {
                defaultDir = Path.Combine(Directory.GetCurrentDirectory(), "reference", "cases");
            }
            if (!Directory.Exists(defaultDir))
            {
                throw new InvalidOperationException($"default cases directory not found: {defaultDir}");
            }
            files.AddRange(Directory.GetFiles(defaultDir, "*.json").OrderBy(p => p, StringComparer.Ordinal));
        }

        var all = new List<CaseSpec>();
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var validId = new Regex(@"^[A-Za-z0-9][A-Za-z0-9._-]*$", RegexOptions.CultureInvariant);
        foreach (var file in files)
        {
            string rawJson = File.ReadAllText(file);
            using (var doc = JsonDocument.Parse(rawJson))
            {
                CheckNoCaseCollidingKeys(doc.RootElement, file);
            }
            var spec = JsonSerializer.Deserialize<CaseFileSpec>(rawJson, opts)
                       ?? throw new InvalidOperationException($"{file}: invalid JSON");
            foreach (var c in spec.Cases)
            {
                if (string.IsNullOrWhiteSpace(c.Id) || !validId.IsMatch(c.Id) || c.Id.Contains("..", StringComparison.Ordinal))
                {
                    throw new InvalidOperationException($"{file}: invalid case id '{c.Id}' (must match [A-Za-z0-9][A-Za-z0-9._-]* without '..')");
                }
                if (!seen.Add(c.Id))
                {
                    throw new InvalidOperationException($"{file}: duplicate case id '{c.Id}' (case-insensitive)");
                }
                all.Add(c);
            }
        }
        return all;
    }

    private static Color ParseColor(string caseId, string field, string raw)
    {
        string s = raw.Trim();
        if (s.StartsWith("#")) s = s[1..];
        if (s.Length == 6) s = "FF" + s;
        if (s.Length != 8 || !uint.TryParse(s, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint v))
        {
            throw new InvalidOperationException($"{caseId}.{field}: invalid color '{raw}'");
        }
        return Color.FromArgb((byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8), (byte)v);
    }

    private static Thickness ParseThickness(string caseId, string field, JsonElement val)
    {
        if (val.ValueKind == JsonValueKind.Number)
        {
            double d = val.GetDouble();
            return new Thickness(d);
        }
        if (val.ValueKind == JsonValueKind.Array)
        {
            var arr = val.EnumerateArray().Select(e => e.GetDouble()).ToArray();
            return arr.Length switch
            {
                1 => new Thickness(arr[0]),
                2 => new Thickness(arr[0], arr[1], arr[0], arr[1]),
                4 => new Thickness(arr[0], arr[1], arr[2], arr[3]),
                _ => throw new InvalidOperationException($"{caseId}.{field}: expected 1, 2, or 4 numbers for Thickness"),
            };
        }
        throw new InvalidOperationException($"{caseId}.{field}: invalid Thickness value");
    }

    private static CornerRadius ParseCornerRadius(string caseId, string field, JsonElement val)
    {
        if (val.ValueKind == JsonValueKind.Number)
        {
            double d = val.GetDouble();
            return new CornerRadius(d);
        }
        if (val.ValueKind == JsonValueKind.Array)
        {
            var arr = val.EnumerateArray().Select(e => e.GetDouble()).ToArray();
            return arr.Length switch
            {
                1 => new CornerRadius(arr[0]),
                4 => new CornerRadius(arr[0], arr[1], arr[2], arr[3]),
                _ => throw new InvalidOperationException($"{caseId}.{field}: expected 1 or 4 numbers for CornerRadius"),
            };
        }
        throw new InvalidOperationException($"{caseId}.{field}: invalid CornerRadius value");
    }

    private static TEnum ParseEnum<TEnum>(string caseId, string field, string raw) where TEnum : struct
    {
        if (!Enum.TryParse<TEnum>(raw, ignoreCase: false, out var result))
        {
            throw new InvalidOperationException($"{caseId}.{field}: invalid {typeof(TEnum).Name} '{raw}'");
        }
        return result;
    }

    private static FrameworkElement InstantiateControl(string caseId, string controlName)
    {
        if (string.IsNullOrWhiteSpace(controlName))
        {
            throw new InvalidOperationException($"{caseId}.control: missing control type");
        }
        Type? type = typeof(Button).Assembly.GetType($"Microsoft.UI.Xaml.Controls.{controlName}", throwOnError: false, ignoreCase: false)
            ?? typeof(Button).Assembly.GetType($"Microsoft.UI.Xaml.Controls.Primitives.{controlName}", throwOnError: false, ignoreCase: false)
            ?? typeof(Button).Assembly.GetType($"Microsoft.UI.Xaml.Shapes.{controlName}", throwOnError: false, ignoreCase: false);
        if (type != null && typeof(FrameworkElement).IsAssignableFrom(type) && !type.IsAbstract)
        {
            var ctor = type.GetConstructor(Type.EmptyTypes);
            if (ctor != null)
            {
                return (FrameworkElement)ctor.Invoke(null);
            }
        }
        throw new InvalidOperationException($"{caseId}.control: unknown control type '{controlName}'");
    }

    private static FrameworkElement CreateElement(CaseSpec spec, double scaleFactor, List<(CaseSpec Spec, FrameworkElement Element)>? pendingStates = null, bool isChild = false)
    {
        string caseId = spec.Id;
        if (isChild)
        {
            if (spec.Script != null && spec.Script.Count > 0)
                throw new InvalidOperationException($"{caseId}: child element cannot specify 'script'");
            if (spec.ProbeBox != null || spec.ProbeRadii != null)
                throw new InvalidOperationException($"{caseId}: child element cannot specify probe_box/probe_radii");
            if (Math.Abs(spec.Scale - 1.0) > 1e-6)
                throw new InvalidOperationException($"{caseId}: child element cannot override 'scale'");
            if (!string.IsNullOrEmpty(spec.CaseGroup))
                throw new InvalidOperationException($"{caseId}: child element cannot specify 'case_group'");
        }
        FrameworkElement el = InstantiateControl(caseId, spec.Control);
        if (!string.IsNullOrEmpty(spec.State))
        {
            pendingStates?.Add((spec, el));
        }

        if (!string.IsNullOrEmpty(spec.Style))
        {
            if (!Current.Resources.TryGetValue(spec.Style, out object? resObj) || resObj is not Style style)
            {
                throw new InvalidOperationException($"{caseId}.style: unknown style resource '{spec.Style}'");
            }
            el.Style = style;
        }

        if (!string.IsNullOrEmpty(spec.FlowDirection))
        {
            el.FlowDirection = ParseEnum<FlowDirection>(caseId, "flow_direction", spec.FlowDirection);
        }

        if (isChild && !string.IsNullOrEmpty(spec.Theme))
        {
            el.RequestedTheme = ParseEnum<ElementTheme>(caseId, "theme", spec.Theme);
        }

        if (spec.Items != null)
        {
            if (el is ItemsControl ic)
            {
                foreach (var item in spec.Items)
                {
                    ic.Items.Add(item);
                }
            }
            else
            {
                throw new InvalidOperationException($"{caseId}.items: control '{spec.Control}' does not support items");
            }
        }

        if (spec.Properties != null)
        {
            foreach (var (prop, val) in spec.Properties)
            {
                ApplyProperty(caseId, el, prop, val, scaleFactor);
            }
        }

        if (spec.Children != null)
        {
            if (el is Panel panel)
            {
                foreach (var childSpec in spec.Children)
                {
                    if (string.IsNullOrEmpty(childSpec.Id)) childSpec.Id = $"{caseId}.child";
                    panel.Children.Add(CreateElement(childSpec, scaleFactor, pendingStates, isChild: true));
                }
            }
            else if (el is ContentControl cc && spec.Children.Count == 1)
            {
                var childSpec = spec.Children[0];
                if (string.IsNullOrEmpty(childSpec.Id)) childSpec.Id = $"{caseId}.content";
                cc.Content = CreateElement(childSpec, scaleFactor, pendingStates, isChild: true);
            }
            else if (el is Border border && spec.Children.Count == 1)
            {
                var childSpec = spec.Children[0];
                if (string.IsNullOrEmpty(childSpec.Id)) childSpec.Id = $"{caseId}.child";
                border.Child = CreateElement(childSpec, scaleFactor, pendingStates, isChild: true);
            }
            else
            {
                throw new InvalidOperationException($"{caseId}.children: control '{spec.Control}' cannot accept {spec.Children.Count} children");
            }
        }

        return el;
    }

    private static void ApplyProperty(string caseId, FrameworkElement el, string prop, JsonElement val, double scaleFactor)
    {
        switch (prop)
        {
            case "Name":
                el.Name = val.GetString() ?? "";
                break;
            case "Width":
                el.Width = val.GetDouble() * scaleFactor;
                break;
            case "Height":
                el.Height = val.GetDouble() * scaleFactor;
                break;
            case "MinWidth":
                el.MinWidth = val.GetDouble() * scaleFactor;
                break;
            case "MinHeight":
                el.MinHeight = val.GetDouble() * scaleFactor;
                break;
            case "HorizontalAlignment":
                el.HorizontalAlignment = ParseEnum<HorizontalAlignment>(caseId, prop, val.GetString() ?? "");
                break;
            case "VerticalAlignment":
                el.VerticalAlignment = ParseEnum<VerticalAlignment>(caseId, prop, val.GetString() ?? "");
                break;
            case "Margin":
                el.Margin = ParseThickness(caseId, prop, val);
                break;
            case "IsEnabled":
                if (el is Control cEn) cEn.IsEnabled = val.GetBoolean();
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            case "IsTabStop":
                if (el is Control cTab) cTab.IsTabStop = val.GetBoolean();
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            case "Content":
                if (el is ContentControl cc) cc.Content = val.GetString() ?? "";
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            case "Padding":
                if (el is Control cPad) cPad.Padding = ParseThickness(caseId, prop, val);
                else if (el is Border bPad) bPad.Padding = ParseThickness(caseId, prop, val);
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            case "CornerRadius":
            {
                var cr = ParseCornerRadius(caseId, prop, val);
                var scaled = new CornerRadius(
                    cr.TopLeft * scaleFactor,
                    cr.TopRight * scaleFactor,
                    cr.BottomRight * scaleFactor,
                    cr.BottomLeft * scaleFactor);
                if (el is Border bCr) bCr.CornerRadius = scaled;
                else if (el is Control cCr) cCr.CornerRadius = scaled;
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            }
            case "Background":
            {
                var brush = new SolidColorBrush(ParseColor(caseId, prop, val.GetString() ?? ""));
                if (el is Border bBg) bBg.Background = brush;
                else if (el is Control cBg) cBg.Background = brush;
                else if (el is Panel pBg) pBg.Background = brush;
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            }
            case "Foreground":
            {
                var brush = new SolidColorBrush(ParseColor(caseId, prop, val.GetString() ?? ""));
                if (el is Control cFg) cFg.Foreground = brush;
                else if (el is TextBlock tFg) tFg.Foreground = brush;
                else if (el is IconElement iFg) iFg.Foreground = brush;
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            }
            case "FontFamily":
            {
                var ff = new FontFamily(val.GetString() ?? "");
                if (el is Control cFf) cFf.FontFamily = ff;
                else if (el is TextBlock tFf) tFf.FontFamily = ff;
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            }
            case "FontSize":
            {
                double sz = val.GetDouble();
                if (el is Control cFs) cFs.FontSize = sz;
                else if (el is TextBlock tFs) tFs.FontSize = sz;
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            }
            case "Text":
                if (el is TextBlock tb) tb.Text = val.GetString() ?? "";
                else if (el is TextBox tbox) tbox.Text = val.GetString() ?? "";
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            case "PlaceholderText":
                if (el is TextBox tboxP) tboxP.PlaceholderText = val.GetString() ?? "";
                else goto default;
                break;
            case "Header":
                if (el is ToggleSwitch tsH) tsH.Header = val.GetString() ?? "";
                else if (el is TextBox tboxH) tboxH.Header = val.GetString() ?? "";
                else if (el is Slider slH) slH.Header = val.GetString() ?? "";
                else goto default;
                break;
            case "IsOn":
                if (el is ToggleSwitch tsOn) tsOn.IsOn = val.GetBoolean();
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            case "IsChecked":
                if (el is Microsoft.UI.Xaml.Controls.Primitives.ToggleButton tbChk) tbChk.IsChecked = val.ValueKind == JsonValueKind.Null ? null : val.GetBoolean();
                else goto default;
                break;
            case "Value":
                if (el is Microsoft.UI.Xaml.Controls.Primitives.RangeBase rbVal) rbVal.Value = val.GetDouble();
                else goto default;
                break;
            case "Minimum":
                if (el is Microsoft.UI.Xaml.Controls.Primitives.RangeBase rbMin) rbMin.Minimum = val.GetDouble();
                else goto default;
                break;
            case "Maximum":
                if (el is Microsoft.UI.Xaml.Controls.Primitives.RangeBase rbMax) rbMax.Maximum = val.GetDouble();
                else goto default;
                break;
            case "Orientation":
                if (el is StackPanel sp) sp.Orientation = ParseEnum<Orientation>(caseId, prop, val.GetString() ?? "");
                else goto default;
                break;
            case "Spacing":
                if (el is StackPanel spS) spS.Spacing = val.GetDouble();
                else goto default;
                break;
            case "Data":
                if (el is PathIcon pi)
                {
                    string pathData = val.GetString() ?? "";
                    var geom = (Geometry)XamlReader.Load(
                        $"<Geometry xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'>{System.Security.SecurityElement.Escape(pathData)}</Geometry>");
                    pi.Data = geom;
                }
                else throw new InvalidOperationException($"{caseId}.{prop}: not supported on {el.GetType().Name}");
                break;
            default:
            {
                var propInfo = el.GetType().GetProperty(prop);
                if (propInfo == null || !propInfo.CanWrite)
                {
                    throw new InvalidOperationException($"{caseId}.{prop}: unknown property '{prop}' on {el.GetType().Name}");
                }
                Type pt = Nullable.GetUnderlyingType(propInfo.PropertyType) ?? propInfo.PropertyType;
                object? converted;
                if (val.ValueKind == JsonValueKind.Null && (!propInfo.PropertyType.IsValueType || Nullable.GetUnderlyingType(propInfo.PropertyType) != null))
                {
                    converted = null;
                }
                else if (pt == typeof(int)) converted = val.GetInt32();
                else if (pt == typeof(double)) converted = val.GetDouble();
                else if (pt == typeof(bool)) converted = val.GetBoolean();
                else if (pt == typeof(string) || pt == typeof(object)) converted = val.ValueKind == JsonValueKind.String ? val.GetString() : val.ToString();
                else if (pt.IsEnum) converted = Enum.Parse(pt, val.GetString() ?? "", ignoreCase: false);
                else if (pt == typeof(Thickness)) converted = ParseThickness(caseId, prop, val);
                else if (pt == typeof(CornerRadius)) converted = ParseCornerRadius(caseId, prop, val);
                else if (pt == typeof(Brush)) converted = new SolidColorBrush(ParseColor(caseId, prop, val.GetString() ?? ""));
                else if (pt == typeof(FontFamily)) converted = new FontFamily(val.GetString() ?? "");
                else throw new InvalidOperationException($"{caseId}.{prop}: unsupported property type '{propInfo.PropertyType.Name}' on {el.GetType().Name}");
                propInfo.SetValue(el, converted);
                break;
            }
        }
    }

    private static async Task<bool> ApplyVisualStateAsync(string caseId, Control control, string stateName, bool useTransitions, int timeoutMs = 3000)
    {
        control.ApplyTemplate();
        VisualStateGroup? foundGroup = null;
        VisualState? foundState = null;

        int childCount = VisualTreeHelper.GetChildrenCount(control);
        for (int i = 0; i < childCount && foundState == null; i++)
        {
            if (VisualTreeHelper.GetChild(control, i) is FrameworkElement root)
            {
                var groups = VisualStateManager.GetVisualStateGroups(root);
                if (groups == null) continue;
                foreach (var g in groups)
                {
                    foreach (var s in g.States)
                    {
                        if (string.Equals(s.Name, stateName, StringComparison.Ordinal))
                        {
                            foundGroup = g;
                            foundState = s;
                            break;
                        }
                    }
                    if (foundState != null) break;
                }
            }
        }

        if (foundState == null)
        {
            throw new InvalidOperationException($"{caseId}: visual state '{stateName}' not found on {control.GetType().Name}");
        }

        bool alreadyActive = foundGroup != null && string.Equals(foundGroup.CurrentState?.Name, stateName, StringComparison.Ordinal);
        Storyboard? sb = foundState.Storyboard;
        TaskCompletionSource<bool>? completed = null;
        EventHandler<object>? onCompleted = null;
        bool storyboardCompleted = false;

        if (!alreadyActive && sb != null)
        {
            completed = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            onCompleted = (_, _) => completed.TrySetResult(true);
            sb.Completed += onCompleted;
        }

        try
        {
            if (!VisualStateManager.GoToState(control, stateName, useTransitions))
            {
                throw new InvalidOperationException($"{caseId}: GoToState('{stateName}') returned false on {control.GetType().Name}");
            }

            if (completed != null)
            {
                var finished = await Task.WhenAny(completed.Task, Task.Delay(timeoutMs));
                if (finished == completed.Task)
                {
                    storyboardCompleted = true;
                }
                else
                {
                    throw new InvalidOperationException($"{caseId}: storyboard for state '{stateName}' did not complete within {timeoutMs}ms (use_transitions={useTransitions})");
                }
            }
        }
        finally
        {
            if (sb != null && onCompleted != null)
            {
                sb.Completed -= onCompleted;
            }
        }

        await Task.Delay(120);
        return storyboardCompleted;
    }

    private static async Task<(byte[] Pixels, int Width, int Height)> CaptureSettledAsync(FrameworkElement target, string label)
    {
        var bitmap = new RenderTargetBitmap();
        byte[] pixels = [];
        for (int same = 0, tries = 0; same < 3; tries++)
        {
            if (tries == 100)
            {
                throw new InvalidOperationException($"{label}: the picture does not settle");
            }
            await bitmap.RenderAsync(target);
            var next = (await bitmap.GetPixelsAsync()).ToArray();
            same = next.AsSpan().SequenceEqual(pixels) ? same + 1 : 0;
            pixels = next;
            await Task.Delay(60);
        }
        return (pixels, bitmap.PixelWidth, bitmap.PixelHeight);
    }

    private static async Task RunCases(Window window, Grid canvas, string folder, string? casesPath)
    {
        double hostDpi = canvas.XamlRoot.RasterizationScale;
        var specs = LoadCases(casesPath);
        var results = new List<object>();

        foreach (var spec in specs)
        {
            string themeName = string.IsNullOrEmpty(spec.Theme) ? "Dark" : spec.Theme;
            var theme = ParseEnum<ElementTheme>(spec.Id, "theme", themeName);
            if (double.IsNaN(spec.Scale) || double.IsInfinity(spec.Scale) || spec.Scale <= 0)
            {
                throw new InvalidOperationException($"{spec.Id}.scale: scale must be positive (got {spec.Scale})");
            }
            double scale = spec.Scale;
            bool nativeScaleMatch = Math.Abs(hostDpi - scale) < 1e-6;
            if (!nativeScaleMatch && Math.Abs(hostDpi - 1.0) > 1e-6)
            {
                throw new InvalidOperationException(
                    $"{spec.Id}: host display scale {hostDpi} matches neither 1.0 nor requested case scale {scale}"
                );
            }
            string flowDirName = string.IsNullOrEmpty(spec.FlowDirection) ? "LeftToRight" : spec.FlowDirection;
            var flowDir = ParseEnum<FlowDirection>(spec.Id, "flow_direction", flowDirName);
            int logicalW = spec.Frame.Width;
            int logicalH = spec.Frame.Height;
            int pixelW = (int)Math.Round(logicalW * scale);
            int pixelH = (int)Math.Round(logicalH * scale);

            Color ground = !string.IsNullOrEmpty(spec.Frame.Background)
                ? ParseColor(spec.Id, "frame.background", spec.Frame.Background)
                : (theme == ElementTheme.Dark ? Color.FromArgb(255, 32, 32, 32) : Color.FromArgb(255, 243, 243, 243));

            bool isProbe = string.Equals(spec.CaseGroup, "probe", StringComparison.Ordinal);
            // When hostDpi matches spec.Scale (native-dpi), render innerFrame directly at logicalW x logicalH so
            // WinUI measures and rounds layout natively at that DPI. Otherwise (hostDpi == 1.0 and scale != 1.0),
            // wrap non-probe controls in a Viewbox sized to pixelW x pixelH (viewbox-vector) and record scale_mode.
            string scaleMode;
            FrameworkElement element;
            Grid innerFrame;
            FrameworkElement captureTarget;
            var pendingStates = new List<(CaseSpec Spec, FrameworkElement Element)>();
            if (isProbe)
            {
                scaleMode = nativeScaleMatch ? "native-dpi" : "probe-dip";
                double propScale = nativeScaleMatch ? 1.0 : scale;
                element = CreateElement(spec, propScale, pendingStates);
                innerFrame = new Grid
                {
                    Width = nativeScaleMatch ? logicalW : pixelW,
                    Height = nativeScaleMatch ? logicalH : pixelH,
                    RequestedTheme = theme,
                    FlowDirection = flowDir,
                    Background = new SolidColorBrush(ground),
                    Children = { element },
                };
                captureTarget = innerFrame;
            }
            else if (nativeScaleMatch)
            {
                scaleMode = Math.Abs(scale - 1.0) < 1e-6 ? "native-1x" : "native-dpi";
                element = CreateElement(spec, 1.0, pendingStates);
                innerFrame = new Grid
                {
                    Width = logicalW,
                    Height = logicalH,
                    RequestedTheme = theme,
                    FlowDirection = flowDir,
                    Background = new SolidColorBrush(ground),
                    Children = { element },
                };
                captureTarget = innerFrame;
            }
            else
            {
                scaleMode = "viewbox-vector";
                element = CreateElement(spec, 1.0, pendingStates);
                innerFrame = new Grid
                {
                    Width = logicalW,
                    Height = logicalH,
                    RequestedTheme = theme,
                    FlowDirection = flowDir,
                    Background = new SolidColorBrush(ground),
                    Children = { element },
                };
                captureTarget = new Viewbox
                {
                    Width = pixelW,
                    Height = pixelH,
                    Stretch = Stretch.Fill,
                    Child = innerFrame,
                };
            }

            canvas.Children.Clear();
            canvas.Children.Add(captureTarget);
            await Task.Delay(80);

            bool storyboardAwaited = false;
            foreach (var (stSpec, stEl) in pendingStates)
            {
                if (stEl is not Control ctrl)
                {
                    throw new InvalidOperationException($"{stSpec.Id}.state: '{stSpec.Control}' is not a Control and has no visual states");
                }
                bool awaited = await ApplyVisualStateAsync(stSpec.Id, ctrl, stSpec.State!, stSpec.UseTransitions, stSpec.StoryboardTimeoutMs);
                if (ReferenceEquals(stEl, element))
                {
                    storyboardAwaited = awaited;
                }
            }

            ScriptResultDto? scriptResult = null;
            if (spec.Script != null && spec.Script.Count > 0)
            {
                scriptResult = await Script.RunAsync(window, element, spec.Script);
            }

            var (pixels, width, height) = await CaptureSettledAsync(captureTarget, spec.Id);
            string file = $"{spec.Id}.bgra";
            File.WriteAllBytes(Path.Combine(folder, file), pixels);

            // Always dump element rectangles and automation tree per case (section contract).
            double dumpLayoutScale = (isProbe && !nativeScaleMatch) ? scale : 1.0;
            double dumpAutoLogicalScale = nativeScaleMatch ? 1.0 : scale;
            LayoutNode layoutDump = Dump.DumpLayout(innerFrame, dumpLayoutScale);
            AutomationNode automationDump = Dump.DumpAutomation(innerFrame, element, dumpAutoLogicalScale, dumpLayoutScale);

            var entry = new Dictionary<string, object?>
            {
                ["id"] = spec.Id,
                ["control"] = spec.CaseGroup ?? spec.Control,
                ["xaml_control"] = spec.Control,
                ["theme"] = themeName,
                ["scale"] = scale,
                ["host_rasterization_scale"] = hostDpi,
                ["effective_rasterization_scale"] = hostDpi,
                ["scale_mode"] = scaleMode,
                ["flow_direction"] = flowDirName,
                ["logical_width"] = logicalW,
                ["logical_height"] = logicalH,
                ["file"] = file,
                ["width"] = width,
                ["height"] = height,
            };
            if (!string.IsNullOrEmpty(spec.State))
            {
                entry["state"] = spec.State;
                entry["storyboard_awaited"] = storyboardAwaited;
            }
            if (element is Microsoft.UI.Xaml.Controls.Primitives.Selector sel && sel.SelectedIndex >= 0)
            {
                entry["selected_index"] = sel.SelectedIndex;
                entry["selected_item"] = sel.SelectedItem?.ToString();
            }
            if (element is Microsoft.UI.Xaml.Controls.Primitives.ToggleButton tbEl)
            {
                entry["is_checked"] = tbEl.IsChecked;
            }
            if (element is Microsoft.UI.Xaml.Controls.Primitives.RangeBase rbEl)
            {
                entry["range_value"] = rbEl.Value;
            }
            if (element is Slider slEl)
            {
                entry["orientation"] = slEl.Orientation.ToString();
            }
            if (element is ComboBox cbEl)
            {
                if (!string.IsNullOrEmpty(cbEl.PlaceholderText)) entry["placeholder_text"] = cbEl.PlaceholderText;
                if (cbEl.Header != null) entry["header"] = cbEl.Header.ToString();
            }
            if (isProbe)
            {
                entry["name"] = spec.Id;
                if (spec.ProbeBox != null && spec.ProbeBox.Length == 2)
                {
                    entry["box_width"] = spec.ProbeBox[0];
                    entry["box_height"] = spec.ProbeBox[1];
                }
                if (spec.ProbeRadii != null) entry["radii"] = spec.ProbeRadii;
            }
            if (layoutDump != null) entry["layout"] = layoutDump;
            if (automationDump != null) entry["automation"] = automationDump;
            if (scriptResult != null) entry["script_log"] = scriptResult;
            results.Add(entry);
        }

        var settings = new UISettings();
        var accents = new Dictionary<string, string>();
        foreach (var type in new[]
                 {
                     UIColorType.Accent, UIColorType.AccentLight1, UIColorType.AccentLight2, UIColorType.AccentLight3,
                     UIColorType.AccentDark1, UIColorType.AccentDark2, UIColorType.AccentDark3
                 })
        {
            var c = settings.GetColorValue(type);
            accents[type.ToString()] = $"{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}";
        }

        var installedFonts = new SortedSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var rootKey in new[] { Microsoft.Win32.Registry.LocalMachine, Microsoft.Win32.Registry.CurrentUser })
        {
            using var fk = rootKey.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts");
            if (fk == null) continue;
            foreach (var vname in fk.GetValueNames())
            {
                string clean = vname;
                int paren = clean.IndexOf('(');
                if (paren > 0) clean = clean.Substring(0, paren);
                clean = clean.Trim().ToLowerInvariant();
                if (!string.IsNullOrEmpty(clean)) installedFonts.Add(clean);
            }
        }

        var manifest = new
        {
            scale = canvas.XamlRoot.RasterizationScale,
            accents,
            installed_fonts = installedFonts.ToArray(),
            cases = results,
        };
        File.WriteAllText(
            Path.Combine(folder, "cases.json"),
            JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }));
    }
}

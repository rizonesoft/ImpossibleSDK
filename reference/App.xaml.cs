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
    [JsonPropertyName("template_xaml")] public string? TemplateXaml { get; set; }
    [JsonPropertyName("rich_text_rtf")] public string? RichTextRtf { get; set; }
}

public partial class App : Application
{
    private Window? window;

    public App() => InitializeComponent();

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        var cli = Environment.GetCommandLineArgs();
        if (cli.Length >= 3 && cli[1] == "--dump-cmap")
        {
            List<int[]>? acc = null;
            for (int i = 2; i < cli.Length; i++)
            {
                var r = ReadFontCmapRanges(cli[i]);
                acc = acc == null ? r : IntersectRanges(acc, r);
            }
            Console.WriteLine(JsonSerializer.Serialize(acc ?? new List<int[]>()));
            Environment.Exit(0);
            return;
        }
        if (cli.Length >= 3 && cli[1] == "--dump-font-info")
        {
            var faces = new List<object>();
            for (int i = 2; i < cli.Length; i++)
            {
                foreach (var (fams, rngs) in ReadFontFileFaces(cli[i]))
                {
                    faces.Add(new { families = fams, ranges = rngs });
                }
            }
            Console.WriteLine(JsonSerializer.Serialize(faces));
            Environment.Exit(0);
            return;
        }
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
            PlaceWindowOnTargetMonitor(window, 900, 700);
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

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct WinPoint { public int X; public int Y; }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern IntPtr MonitorFromPoint(WinPoint pt, uint dwFlags);

    [System.Runtime.InteropServices.DllImport("shcore.dll")]
    private static extern int GetDpiForMonitor(IntPtr hmonitor, int dpiType, out uint dpiX, out uint dpiY);

    private static uint GetDisplayAreaDpi(Microsoft.UI.Windowing.DisplayArea area)
    {
        var wa = area.WorkArea;
        var pt = new WinPoint { X = wa.X + Math.Max(1, wa.Width / 2), Y = wa.Y + Math.Max(1, wa.Height / 2) };
        IntPtr hmon = MonitorFromPoint(pt, 2 /* MONITOR_DEFAULTTONEAREST */);
        if (hmon != IntPtr.Zero && GetDpiForMonitor(hmon, 0 /* MDT_EFFECTIVE_DPI */, out uint dpiX, out _) == 0 && dpiX > 0)
        {
            return dpiX;
        }
        return 96;
    }

    private static void PlaceWindowOnTargetMonitor(Window win, int width, int height)
    {
        var areas = Microsoft.UI.Windowing.DisplayArea.FindAll();
        Microsoft.UI.Windowing.DisplayArea? chosen = null;
        string? envMon = Environment.GetEnvironmentVariable("IMPOSSIBLE_REFERENCE_MONITOR")?.Trim();
        if (!string.IsNullOrEmpty(envMon))
        {
            if (envMon.Equals("primary", StringComparison.OrdinalIgnoreCase))
            {
                chosen = Microsoft.UI.Windowing.DisplayArea.Primary;
            }
            else if (int.TryParse(envMon, NumberStyles.Integer, CultureInfo.InvariantCulture, out int idx) && idx >= 1 && idx <= areas.Count)
            {
                chosen = areas[idx - 1];
            }
            else
            {
                throw new InvalidOperationException($"IMPOSSIBLE_REFERENCE_MONITOR='{envMon}' is invalid (expected 'primary' or 1..{areas.Count})");
            }
        }
        else if (areas.Count > 1)
        {
            // Default to the first non-primary 100%-DPI (96 DPI) monitor so multi-scale captures stay valid and off the primary screen.
            for (int i = 0; i < areas.Count; i++)
            {
                if (!areas[i].IsPrimary && GetDisplayAreaDpi(areas[i]) == 96)
                {
                    chosen = areas[i];
                    break;
                }
            }
        }
        if (chosen != null)
        {
            var wa = chosen.WorkArea;
            int x = wa.X + Math.Max(0, (wa.Width - width) / 2);
            int y = wa.Y + Math.Max(0, (wa.Height - height) / 2);
            win.AppWindow.MoveAndResize(new Windows.Graphics.RectInt32(x, y, width, height));
        }
        else
        {
            win.AppWindow.Resize(new Windows.Graphics.SizeInt32(width, height));
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
        if (!string.IsNullOrEmpty(spec.TemplateXaml))
        {
            if (el is not Control tplCtrl)
            {
                throw new InvalidOperationException($"{caseId}.template_xaml: '{spec.Control}' is not a Control");
            }
            tplCtrl.Template = (ControlTemplate)XamlReader.Load(spec.TemplateXaml);
        }
        if (!string.IsNullOrEmpty(spec.RichTextRtf))
        {
            if (el is not RichEditBox rtfBox)
            {
                throw new InvalidOperationException($"{caseId}.rich_text_rtf: only supported on RichEditBox");
            }
            rtfBox.Document.SetText(Microsoft.UI.Text.TextSetOptions.FormatRtf, spec.RichTextRtf);
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
            case "RowDefinitions":
                if (el is Grid grRows && val.ValueKind == JsonValueKind.Array)
                {
                    grRows.RowDefinitions.Clear();
                    foreach (var item in val.EnumerateArray())
                    {
                        grRows.RowDefinitions.Add(new RowDefinition { Height = ParseGridLength(caseId, prop, item) });
                    }
                }
                else throw new InvalidOperationException($"{caseId}.{prop}: requires a Grid and JSON array");
                break;
            case "ColumnDefinitions":
                if (el is Grid grCols && val.ValueKind == JsonValueKind.Array)
                {
                    grCols.ColumnDefinitions.Clear();
                    foreach (var item in val.EnumerateArray())
                    {
                        grCols.ColumnDefinitions.Add(new ColumnDefinition { Width = ParseGridLength(caseId, prop, item) });
                    }
                }
                else throw new InvalidOperationException($"{caseId}.{prop}: requires a Grid and JSON array");
                break;
            case "Grid.Row":
                Grid.SetRow(el, val.GetInt32());
                break;
            case "Grid.Column":
                Grid.SetColumn(el, val.GetInt32());
                break;
            case "Grid.RowSpan":
                Grid.SetRowSpan(el, val.GetInt32());
                break;
            case "Grid.ColumnSpan":
                Grid.SetColumnSpan(el, val.GetInt32());
                break;
            case "Canvas.Left":
                Canvas.SetLeft(el, val.GetDouble() * scaleFactor);
                break;
            case "Canvas.Top":
                Canvas.SetTop(el, val.GetDouble() * scaleFactor);
                break;
            case "Canvas.ZIndex":
                Canvas.SetZIndex(el, val.GetInt32());
                break;
            case "VariableSizedWrapGrid.RowSpan":
                VariableSizedWrapGrid.SetRowSpan(el, val.GetInt32());
                break;
            case "VariableSizedWrapGrid.ColumnSpan":
                VariableSizedWrapGrid.SetColumnSpan(el, val.GetInt32());
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

    private static GridLength ParseGridLength(string caseId, string prop, JsonElement val)
    {
        if (val.ValueKind == JsonValueKind.Number)
        {
            return new GridLength(val.GetDouble(), GridUnitType.Pixel);
        }
        string s = (val.GetString() ?? "").Trim();
        if (string.Equals(s, "Auto", StringComparison.OrdinalIgnoreCase))
        {
            return GridLength.Auto;
        }
        if (s.EndsWith("*", StringComparison.Ordinal))
        {
            string prefix = s.Substring(0, s.Length - 1).Trim();
            double weight = string.IsNullOrEmpty(prefix) ? 1.0 : double.Parse(prefix, CultureInfo.InvariantCulture);
            return new GridLength(weight, GridUnitType.Star);
        }
        if (double.TryParse(s, NumberStyles.Float, CultureInfo.InvariantCulture, out double px))
        {
            return new GridLength(px, GridUnitType.Pixel);
        }
        throw new InvalidOperationException($"{caseId}.{prop}: invalid GridLength '{s}'");
    }

    private static VisualTransition? FindMatchingTransition(VisualStateGroup group, string? fromState, string toState)
    {
        VisualTransition? exact = null;
        VisualTransition? toOnly = null;
        VisualTransition? fromOnly = null;
        VisualTransition? fallback = null;
        foreach (var t in group.Transitions)
        {
            bool matchFrom = !string.IsNullOrEmpty(t.From) && string.Equals(t.From, fromState, StringComparison.Ordinal);
            bool emptyFrom = string.IsNullOrEmpty(t.From);
            bool matchTo = !string.IsNullOrEmpty(t.To) && string.Equals(t.To, toState, StringComparison.Ordinal);
            bool emptyTo = string.IsNullOrEmpty(t.To);
            if (matchFrom && matchTo) exact = t;
            else if (emptyFrom && matchTo) toOnly = t;
            else if (matchFrom && emptyTo) fromOnly = t;
            else if (emptyFrom && emptyTo) fallback = t;
        }
        return exact ?? toOnly ?? fromOnly ?? fallback;
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

        string? fromState = foundGroup?.CurrentState?.Name;
        bool alreadyActive = foundGroup != null && string.Equals(fromState, stateName, StringComparison.Ordinal);
        Storyboard? stateSb = foundState.Storyboard;
        VisualTransition? transition = (!alreadyActive && useTransitions && foundGroup != null)
            ? FindMatchingTransition(foundGroup, fromState, stateName)
            : null;
        Storyboard? transSb = transition?.Storyboard;

        TaskCompletionSource<bool>? stateCompleted = null;
        EventHandler<object>? onStateCompleted = null;
        TaskCompletionSource<bool>? transCompleted = null;
        EventHandler<object>? onTransCompleted = null;
        bool storyboardCompleted = false;

        if ((!alreadyActive && stateSb != null)
            || (alreadyActive && stateSb != null && stateSb.GetCurrentState() == ClockState.Active))
        {
            stateCompleted = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            onStateCompleted = (_, _) => stateCompleted.TrySetResult(true);
            stateSb.Completed += onStateCompleted;
        }
        if (!alreadyActive && transSb != null)
        {
            transCompleted = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            onTransCompleted = (_, _) => transCompleted.TrySetResult(true);
            transSb.Completed += onTransCompleted;
        }

        try
        {
            if (!VisualStateManager.GoToState(control, stateName, useTransitions))
            {
                throw new InvalidOperationException($"{caseId}: GoToState('{stateName}') returned false on {control.GetType().Name}");
            }

            var sw = System.Diagnostics.Stopwatch.StartNew();
            if (transCompleted != null)
            {
                var finished = await Task.WhenAny(transCompleted.Task, Task.Delay(timeoutMs));
                if (finished == transCompleted.Task)
                {
                    storyboardCompleted = true;
                }
                else
                {
                    throw new InvalidOperationException($"{caseId}: transition storyboard for state '{stateName}' did not complete within {timeoutMs}ms (use_transitions={useTransitions})");
                }
            }
            if (transition != null && transition.GeneratedDuration.HasTimeSpan && transition.GeneratedDuration.TimeSpan > TimeSpan.Zero)
            {
                int genMs = (int)Math.Ceiling(transition.GeneratedDuration.TimeSpan.TotalMilliseconds);
                if (genMs > timeoutMs)
                {
                    throw new InvalidOperationException($"{caseId}: transition duration ({genMs}ms) for state '{stateName}' exceeds timeout {timeoutMs}ms");
                }
                int remaining = genMs - (int)sw.ElapsedMilliseconds;
                if (remaining > 0)
                {
                    await Task.Delay(remaining + 40);
                }
                storyboardCompleted = true;
            }

            if (stateCompleted != null)
            {
                var finished = await Task.WhenAny(stateCompleted.Task, Task.Delay(timeoutMs));
                if (finished == stateCompleted.Task)
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
            if (stateSb != null && onStateCompleted != null)
            {
                stateSb.Completed -= onStateCompleted;
            }
            if (transSb != null && onTransCompleted != null)
            {
                transSb.Completed -= onTransCompleted;
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

    private static List<int[]> MergeRanges(List<int[]> raw)
    {
        if (raw.Count == 0) return raw;
        raw.Sort((a, b) => a[0] != b[0] ? a[0].CompareTo(b[0]) : a[1].CompareTo(b[1]));
        var merged = new List<int[]> { new[] { raw[0][0], raw[0][1] } };
        for (int i = 1; i < raw.Count; i++)
        {
            var last = merged[^1];
            if (raw[i][0] <= last[1] + 1)
            {
                if (raw[i][1] > last[1]) last[1] = raw[i][1];
            }
            else
            {
                merged.Add(new[] { raw[i][0], raw[i][1] });
            }
        }
        return merged;
    }

    private static List<int[]> IntersectRanges(List<int[]> a, List<int[]> b)
    {
        var outList = new List<int[]>();
        int i = 0, j = 0;
        while (i < a.Count && j < b.Count)
        {
            int lo = Math.Max(a[i][0], b[j][0]);
            int hi = Math.Min(a[i][1], b[j][1]);
            if (lo <= hi) outList.Add(new[] { lo, hi });
            if (a[i][1] < b[j][1]) i++; else j++;
        }
        return outList;
    }

    private static List<int[]> ReadSingleFaceCmapRanges(byte[] data, int sfntOffset)
    {
        var ranges = new List<int[]>();
        static ushort U16(byte[] b, int o) => (ushort)((b[o] << 8) | b[o + 1]);
        static short I16(byte[] b, int o) => (short)((b[o] << 8) | b[o + 1]);
        static uint U32(byte[] b, int o) => ((uint)b[o] << 24) | ((uint)b[o + 1] << 16) | ((uint)b[o + 2] << 8) | b[o + 3];
        if (sfntOffset < 0 || sfntOffset + 12 > data.Length) return ranges;
        int numTables = U16(data, sfntOffset + 4);
        int cmapOff = -1;
        for (int i = 0; i < numTables; i++)
        {
            int rec = sfntOffset + 12 + i * 16;
            if (rec + 16 > data.Length) break;
            if (data[rec] == (byte)'c' && data[rec + 1] == (byte)'m' && data[rec + 2] == (byte)'a' && data[rec + 3] == (byte)'p')
            {
                cmapOff = (int)U32(data, rec + 8);
                break;
            }
        }
        if (cmapOff < 0 || cmapOff + 4 > data.Length) return ranges;
        int numSub = U16(data, cmapOff + 2);
        int bestOff = -1;
        int bestFmt = -1;
        for (int i = 0; i < numSub; i++)
        {
            int sr = cmapOff + 4 + i * 8;
            if (sr + 8 > data.Length) break;
            ushort pid = U16(data, sr);
            int subOff = cmapOff + (int)U32(data, sr + 4);
            if ((pid == 3 || pid == 0) && subOff >= 0 && subOff + 4 <= data.Length)
            {
                ushort fmt = U16(data, subOff);
                if (fmt == 12) { bestOff = subOff; bestFmt = 12; break; }
                if (fmt == 4 && bestFmt != 4) { bestOff = subOff; bestFmt = 4; }
            }
        }
        if (bestFmt == 12 && bestOff + 16 <= data.Length)
        {
            uint nGroups = U32(data, bestOff + 12);
            for (uint g = 0; g < nGroups; g++)
            {
                int go = bestOff + 16 + (int)g * 12;
                if (go + 12 > data.Length) break;
                int st = (int)U32(data, go);
                int en = (int)U32(data, go + 4);
                uint startGid = U32(data, go + 8);
                if (startGid == 0)
                {
                    if (st < en) ranges.Add(new[] { st + 1, en });
                }
                else
                {
                    ranges.Add(new[] { st, en });
                }
            }
        }
        else if (bestFmt == 4 && bestOff + 14 <= data.Length)
        {
            int segCount = U16(data, bestOff + 6) / 2;
            int endOff = bestOff + 14;
            int startOff = endOff + segCount * 2 + 2;
            int deltaOff = startOff + segCount * 2;
            int rangeOff = deltaOff + segCount * 2;
            for (int s = 0; s < segCount; s++)
            {
                if (rangeOff + s * 2 + 2 > data.Length) break;
                int st = U16(data, startOff + s * 2);
                int en = U16(data, endOff + s * 2);
                if (st == 0xFFFF && en == 0xFFFF) break;
                int delta = I16(data, deltaOff + s * 2);
                int ro = U16(data, rangeOff + s * 2);
                int curStart = -1;
                for (int c = st; c <= en; c++)
                {
                    int gid;
                    if (ro == 0)
                    {
                        gid = (c + delta) & 0xFFFF;
                    }
                    else
                    {
                        int glyphIdxOff = rangeOff + s * 2 + ro + (c - st) * 2;
                        if (glyphIdxOff + 2 > data.Length) { gid = 0; }
                        else
                        {
                            int rawGid = U16(data, glyphIdxOff);
                            gid = rawGid == 0 ? 0 : ((rawGid + delta) & 0xFFFF);
                        }
                    }
                    if (gid != 0)
                    {
                        if (curStart < 0) curStart = c;
                    }
                    else if (curStart >= 0)
                    {
                        ranges.Add(new[] { curStart, c - 1 });
                        curStart = -1;
                    }
                }
                if (curStart >= 0) ranges.Add(new[] { curStart, en });
            }
        }
        return MergeRanges(ranges);
    }

    private static List<int[]> ReadFontCmapRanges(string fontPath)
    {
        if (!File.Exists(fontPath)) return new List<int[]>();
        byte[] data = File.ReadAllBytes(fontPath);
        if (data.Length < 12) return new List<int[]>();
        static uint U32(byte[] b, int o) => ((uint)b[o] << 24) | ((uint)b[o + 1] << 16) | ((uint)b[o + 2] << 8) | b[o + 3];
        if (data[0] == (byte)'t' && data[1] == (byte)'t' && data[2] == (byte)'c' && data[3] == (byte)'f')
        {
            if (data.Length < 16) return new List<int[]>();
            uint numFonts = U32(data, 8);
            if (numFonts == 0 || 12 + numFonts * 4 > (uint)data.Length) return new List<int[]>();
            List<int[]>? acc = null;
            for (uint fi = 0; fi < numFonts; fi++)
            {
                int off = (int)U32(data, 12 + (int)fi * 4);
                var faceRanges = ReadSingleFaceCmapRanges(data, off);
                if (faceRanges.Count == 0) return new List<int[]>();
                acc = acc == null ? faceRanges : IntersectRanges(acc, faceRanges);
                if (acc.Count == 0) return acc;
            }
            return acc ?? new List<int[]>();
        }
        return ReadSingleFaceCmapRanges(data, 0);
    }

    private static List<string> ReadSingleFaceFamilyNames(byte[] data, int sfntOffset)
    {
        var names16 = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var names1 = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        if (sfntOffset < 0 || sfntOffset + 12 > data.Length) return new List<string>();
        static ushort U16(byte[] b, int o) => (ushort)((b[o] << 8) | b[o + 1]);
        static uint U32(byte[] b, int o) => ((uint)b[o] << 24) | ((uint)b[o + 1] << 16) | ((uint)b[o + 2] << 8) | b[o + 3];
        int numTables = U16(data, sfntOffset + 4);
        int nameOff = -1;
        int nameLen = 0;
        for (int i = 0; i < numTables; i++)
        {
            int rec = sfntOffset + 12 + i * 16;
            if (rec + 16 > data.Length) break;
            if (data[rec] == (byte)'n' && data[rec + 1] == (byte)'a' && data[rec + 2] == (byte)'m' && data[rec + 3] == (byte)'e')
            {
                nameOff = (int)U32(data, rec + 8);
                nameLen = (int)U32(data, rec + 12);
                break;
            }
        }
        if (nameOff < 0 || nameOff + 6 > data.Length || nameOff + nameLen > data.Length) return new List<string>();
        int count = U16(data, nameOff + 2);
        int strBase = nameOff + U16(data, nameOff + 4);
        for (int i = 0; i < count; i++)
        {
            int r = nameOff + 6 + i * 12;
            if (r + 12 > nameOff + nameLen) break;
            ushort platformId = U16(data, r);
            ushort encodingId = U16(data, r + 2);
            ushort nameId = U16(data, r + 6);
            if (nameId != 1 && nameId != 16) continue;
            ushort len = U16(data, r + 8);
            ushort off = U16(data, r + 10);
            int start = strBase + off;
            if (start < 0 || start + len > nameOff + nameLen || len == 0) continue;
            string? decoded = null;
            if ((platformId == 3 && (encodingId == 1 || encodingId == 10)) || platformId == 0)
            {
                decoded = System.Text.Encoding.BigEndianUnicode.GetString(data, start, len);
            }
            else if (platformId == 1 && encodingId == 0)
            {
                bool pureAscii = true;
                for (int bi = 0; bi < len; bi++)
                {
                    byte b = data[start + bi];
                    if (b < 0x20 || b > 0x7E) { pureAscii = false; break; }
                }
                if (pureAscii) decoded = System.Text.Encoding.ASCII.GetString(data, start, len);
            }
            if (string.IsNullOrWhiteSpace(decoded)) continue;
            string norm = decoded.Trim().ToLowerInvariant();
            if (nameId == 16) names16.Add(norm);
            else names1.Add(norm);
        }
        var result = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var n in names16) result.Add(n);
        foreach (var n in names1) result.Add(n);
        return result.OrderBy(s => s, StringComparer.Ordinal).ToList();
    }

    private static List<(List<string> Families, List<int[]> Ranges)> ReadFontFileFaces(string fontPath, HashSet<string>? parseCmapOnlyFor = null)
    {
        var outFaces = new List<(List<string>, List<int[]>)>();
        if (!File.Exists(fontPath)) return outFaces;
        byte[] data;
        try { data = File.ReadAllBytes(fontPath); }
        catch { return outFaces; }
        if (data.Length < 12) return outFaces;
        static uint U32(byte[] b, int o) => ((uint)b[o] << 24) | ((uint)b[o + 1] << 16) | ((uint)b[o + 2] << 8) | b[o + 3];
        if (data[0] == (byte)'t' && data[1] == (byte)'t' && data[2] == (byte)'c' && data[3] == (byte)'f')
        {
            if (data.Length < 16) return outFaces;
            uint numFonts = U32(data, 8);
            if (numFonts == 0 || 12 + numFonts * 4 > (uint)data.Length) return outFaces;
            for (uint fi = 0; fi < numFonts; fi++)
            {
                int off = (int)U32(data, 12 + (int)fi * 4);
                var fams = ReadSingleFaceFamilyNames(data, off);
                if (fams.Count > 0)
                {
                    bool needCmap = parseCmapOnlyFor == null || fams.Any(f => parseCmapOnlyFor.Contains(f));
                    outFaces.Add((fams, needCmap ? ReadSingleFaceCmapRanges(data, off) : new List<int[]>()));
                }
            }
            return outFaces;
        }
        var singleFams = ReadSingleFaceFamilyNames(data, 0);
        if (singleFams.Count > 0)
        {
            bool needCmap = parseCmapOnlyFor == null || singleFams.Any(f => parseCmapOnlyFor.Contains(f));
            outFaces.Add((singleFams, needCmap ? ReadSingleFaceCmapRanges(data, 0) : new List<int[]>()));
        }
        return outFaces;
    }

    private static List<Dictionary<string, string>> CollectRenderedText(DependencyObject root)
    {
        var items = new List<Dictionary<string, string>>();
        void Walk(DependencyObject obj)
        {
            if (obj is UIElement uie && (uie.Visibility != Visibility.Visible || uie.Opacity <= 0.0))
            {
                return;
            }
            if (obj is TextBlock tb)
            {
                if (!string.IsNullOrEmpty(tb.Text))
                {
                    string tbText = tb.IsTextTrimmed ? (tb.Text + "\u2026") : tb.Text;
                    items.Add(new Dictionary<string, string>
                    {
                        ["element"] = string.IsNullOrEmpty(tb.Name) ? "TextBlock" : $"TextBlock:{tb.Name}",
                        ["font"] = tb.FontFamily?.Source ?? "",
                        ["text"] = tbText,
                    });
                }
            }
            else if (obj is RichTextBlock rtb)
            {
                string font = rtb.FontFamily?.Source ?? "";
                string elemName = string.IsNullOrEmpty(rtb.Name) ? "RichTextBlock" : $"RichTextBlock:{rtb.Name}";
                bool addedAny = false;
                foreach (var block in rtb.Blocks)
                {
                    if (block is Microsoft.UI.Xaml.Documents.Paragraph para)
                    {
                        foreach (var inline in para.Inlines)
                        {
                            if (inline is Microsoft.UI.Xaml.Documents.Run run && !string.IsNullOrEmpty(run.Text))
                            {
                                addedAny = true;
                                items.Add(new Dictionary<string, string>
                                {
                                    ["element"] = elemName,
                                    ["font"] = run.FontFamily?.Source ?? font,
                                    ["text"] = run.Text,
                                });
                            }
                        }
                    }
                }
                if (addedAny && rtb.IsTextTrimmed)
                {
                    items.Add(new Dictionary<string, string>
                    {
                        ["element"] = elemName,
                        ["font"] = font,
                        ["text"] = "\u2026",
                    });
                }
            }
            else if (obj is TextBox tbox)
            {
                if (!string.IsNullOrEmpty(tbox.Text))
                {
                    items.Add(new Dictionary<string, string>
                    {
                        ["element"] = string.IsNullOrEmpty(tbox.Name) ? "TextBox" : $"TextBox:{tbox.Name}",
                        ["font"] = tbox.FontFamily?.Source ?? "",
                        ["text"] = tbox.Text,
                    });
                }
            }
            else if (obj is RichEditBox reb)
            {
                string elemName = string.IsNullOrEmpty(reb.Name) ? "RichEditBox" : $"RichEditBox:{reb.Name}";
                string fallbackFont = reb.FontFamily?.Source ?? "";
                var range = reb.Document.GetRange(0, 0);
                int pos = 0;
                while (range.Expand(Microsoft.UI.Text.TextRangeUnit.CharacterFormat) > 0)
                {
                    int end = range.EndPosition;
                    range.GetText(Microsoft.UI.Text.TextGetOptions.None, out string runText);
                    runText = runText.TrimEnd('\r', '\n');
                    var paraRange = reb.Document.GetRange(range.StartPosition, range.StartPosition);
                    paraRange.Expand(Microsoft.UI.Text.TextRangeUnit.Paragraph);
                    var pf = paraRange.ParagraphFormat;
                    if (pf != null)
                    {
                        if (pf.ListType != Microsoft.UI.Text.MarkerType.None)
                        {
                            items.Add(new Dictionary<string, string>
                            {
                                ["element"] = elemName,
                                ["font"] = "__unverified_rich_decoration__",
                                ["text"] = "ListMarker",
                            });
                        }
                        if (runText.Contains('\t') && pf.TabCount > 0)
                        {
                            bool foundNonSpaceLeader = false;
                            for (int ci = 0; ci < runText.Length && !foundNonSpaceLeader; ci++)
                            {
                                if (runText[ci] != '\t') continue;
                                var tabRange = reb.Document.GetRange(range.StartPosition + ci, range.StartPosition + ci);
                                tabRange.GetPoint(
                                    Microsoft.UI.Text.HorizontalCharacterAlignment.Left,
                                    Microsoft.UI.Text.VerticalCharacterAlignment.Top,
                                    Microsoft.UI.Text.PointOptions.ClientCoordinates,
                                    out var tabPt);
                                var paraStartRange = reb.Document.GetRange(paraRange.StartPosition, paraRange.StartPosition);
                                paraStartRange.GetPoint(
                                    Microsoft.UI.Text.HorizontalCharacterAlignment.Left,
                                    Microsoft.UI.Text.VerticalCharacterAlignment.Top,
                                    Microsoft.UI.Text.PointOptions.ClientCoordinates,
                                    out var lineStartPt);
                                double tabXPt = Math.Abs(tabPt.X - lineStartPt.X) * 72.0 / 96.0;
                                float bestPos = float.MaxValue;
                                var bestLeader = Microsoft.UI.Text.TabLeader.Spaces;
                                for (int ti = 0; ti < pf.TabCount; ti++)
                                {
                                    pf.GetTab(ti, out float stopPos, out _, out var leader);
                                    if (stopPos > tabXPt && stopPos < bestPos)
                                    {
                                        bestPos = stopPos;
                                        bestLeader = leader;
                                    }
                                }
                                if (bestPos < float.MaxValue && bestLeader != Microsoft.UI.Text.TabLeader.Spaces)
                                {
                                    foundNonSpaceLeader = true;
                                }
                            }
                            if (foundNonSpaceLeader)
                            {
                                items.Add(new Dictionary<string, string>
                                {
                                    ["element"] = elemName,
                                    ["font"] = "__unverified_rich_decoration__",
                                    ["text"] = "TabLeader",
                                });
                            }
                        }
                    }
                    if (runText.Contains('\uFFFC'))
                    {
                        items.Add(new Dictionary<string, string>
                        {
                            ["element"] = elemName,
                            ["font"] = "__unverified_rich_decoration__",
                            ["text"] = "\uFFFC",
                        });
                    }
                    if (!string.IsNullOrEmpty(runText))
                    {
                        string runFont = range.CharacterFormat?.Name ?? "";
                        if (string.IsNullOrEmpty(runFont)) runFont = fallbackFont;
                        string emitText = runText;
                        if (range.CharacterFormat != null
                            && (range.CharacterFormat.AllCaps == Microsoft.UI.Text.FormatEffect.On
                                || range.CharacterFormat.SmallCaps == Microsoft.UI.Text.FormatEffect.On))
                        {
                            emitText = runText.ToUpperInvariant();
                        }
                        items.Add(new Dictionary<string, string>
                        {
                            ["element"] = elemName,
                            ["font"] = runFont,
                            ["text"] = emitText,
                        });
                    }
                    if (end <= pos) break;
                    pos = end;
                    range.SetRange(pos, pos);
                }
            }
            else if (obj is PasswordBox pbox)
            {
                if (!string.IsNullOrEmpty(pbox.Password))
                {
                    string shown = pbox.PasswordRevealMode == PasswordRevealMode.Visible
                        ? pbox.Password
                        : (string.IsNullOrEmpty(pbox.PasswordChar) ? "\u25CF" : pbox.PasswordChar);
                    items.Add(new Dictionary<string, string>
                    {
                        ["element"] = string.IsNullOrEmpty(pbox.Name) ? "PasswordBox" : $"PasswordBox:{pbox.Name}",
                        ["font"] = pbox.FontFamily?.Source ?? "",
                        ["text"] = shown,
                    });
                }
            }
            else if (obj is FontIcon fi && !string.IsNullOrEmpty(fi.Glyph))
            {
                items.Add(new Dictionary<string, string>
                {
                    ["element"] = string.IsNullOrEmpty(fi.Name) ? "FontIcon" : $"FontIcon:{fi.Name}",
                    ["font"] = fi.FontFamily?.Source ?? "",
                    ["text"] = fi.Glyph,
                });
            }
            else if (obj is Image img && img.Source != null)
            {
                items.Add(new Dictionary<string, string>
                {
                    ["element"] = string.IsNullOrEmpty(img.Name) ? "Image" : $"Image:{img.Name}",
                    ["font"] = "__unverified_image__",
                    ["text"] = "Image",
                });
            }
            else if (obj is BitmapIcon bi && bi.UriSource != null)
            {
                items.Add(new Dictionary<string, string>
                {
                    ["element"] = string.IsNullOrEmpty(bi.Name) ? "BitmapIcon" : $"BitmapIcon:{bi.Name}",
                    ["font"] = "__unverified_image__",
                    ["text"] = "BitmapIcon",
                });
            }
            int count = VisualTreeHelper.GetChildrenCount(obj);
            for (int i = 0; i < count; i++)
            {
                Walk(VisualTreeHelper.GetChild(obj, i));
            }
        }
        Walk(root);
        return items;
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
            var renderedText = CollectRenderedText(innerFrame);
            entry["rendered_text"] = renderedText;
            if (element is RichEditBox)
            {
                entry["rich_tabs_verified"] = !renderedText.Exists(r => r.TryGetValue("font", out var rf) && rf == "__unverified_rich_decoration__");
            }
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
        var installedFontRanges = new Dictionary<string, List<int[]>>(StringComparer.OrdinalIgnoreCase);
        var permittedFamilies = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
        {
            "inter", "jetbrains mono", "ibm plex sans", "ibm plex mono",
            "dejavu sans", "dejavu sans mono", "liberation sans", "liberation mono",
            "noto sans", "noto mono", "barlow",
        };
        var fontFiles = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        string winFontsDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), "Fonts");
        // Only scan font files registered in Windows Fonts registry keys (HKLM/HKCU); never trust unregistered stray files on disk.
        foreach (var rootKey in new[] { Microsoft.Win32.Registry.LocalMachine, Microsoft.Win32.Registry.CurrentUser })
        {
            using var fk = rootKey.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion\Fonts");
            if (fk == null) continue;
            foreach (var vname in fk.GetValueNames())
            {
                string rawVal = fk.GetValue(vname)?.ToString() ?? "";
                if (string.IsNullOrEmpty(rawVal)) continue;
                string full = Path.IsPathRooted(rawVal) ? rawVal : Path.Combine(winFontsDir, rawVal);
                if (File.Exists(full)) fontFiles.Add(full);
            }
        }
        foreach (var fp in fontFiles)
        {
            foreach (var (famNames, faceRanges) in ReadFontFileFaces(fp, permittedFamilies))
            {
                foreach (var fam in famNames)
                {
                    string clean = fam.Trim().ToLowerInvariant();
                    if (string.IsNullOrEmpty(clean)) continue;
                    installedFonts.Add(clean);
                    if (permittedFamilies.Contains(clean))
                    {
                        installedFontRanges[clean] = installedFontRanges.TryGetValue(clean, out var prev)
                            ? IntersectRanges(prev, faceRanges)
                            : faceRanges;
                    }
                }
            }
        }

        var manifest = new
        {
            scale = canvas.XamlRoot.RasterizationScale,
            accents,
            installed_fonts = installedFonts.ToArray(),
            installed_font_ranges = installedFontRanges,
            cases = results,
        };
        File.WriteAllText(
            Path.Combine(folder, "cases.json"),
            JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }));
    }
}

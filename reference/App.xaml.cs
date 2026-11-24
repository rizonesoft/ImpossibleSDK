using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices.WindowsRuntime;
using System.Text.Json;
using System.Threading.Tasks;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.UI;
using Windows.UI.ViewManagement;

namespace Reference;

// Draws each case with real WinUI and writes the raw pixels and a list of them
// to the folder named on the command line. No text is drawn.
public partial class App : Application
{
    private Window? window;

    public App() => InitializeComponent();

    protected override async void OnLaunched(LaunchActivatedEventArgs args)
    {
        string folder = Environment.GetCommandLineArgs()[1];
        try
        {
            Directory.CreateDirectory(folder);
            var canvas = new Grid();
            window = new Window { Content = canvas, Title = "Impossible UI reference" };
            window.AppWindow.Resize(new Windows.Graphics.SizeInt32(700, 500));
            window.Activate();
            await Task.Delay(1000);
            await Draw(canvas, folder);
            Environment.Exit(0);
        }
        catch (Exception error)
        {
            File.WriteAllText(Path.Combine(folder, "error.txt"), error.ToString());
            Environment.Exit(1);
        }
    }

    private static async Task Draw(Grid canvas, string folder)
    {
        if (Math.Abs(canvas.XamlRoot.RasterizationScale - 1.0) > 1e-6)
            throw new InvalidOperationException($"Button captures require a 100% display scale (got {canvas.XamlRoot.RasterizationScale})");
        var cases = new List<object>();
        var styles = new (string Name, Func<Control> Make, string[] States)[]
        {
            ("button", () => new Button(), ["Normal", "PointerOver", "Pressed", "Disabled"]),
            ("accent", () => new Button { Style = (Style)Current.Resources["AccentButtonStyle"] }, ["Normal", "PointerOver", "Pressed", "Disabled"]),
        };
        foreach (var theme in new[] { ElementTheme.Dark, ElementTheme.Light })
        {
            foreach (var (name, make, states) in styles)
            {
                foreach (var state in states)
                {
                    var control = make();
                    control.Width = 120;
                    control.Height = 32;
                    control.IsTabStop = false;
                    control.HorizontalAlignment = HorizontalAlignment.Center;
                    control.VerticalAlignment = VerticalAlignment.Center;
                    var ground = theme == ElementTheme.Dark ? Color.FromArgb(255, 32, 32, 32) : Color.FromArgb(255, 243, 243, 243);
                    var frame = new Grid
                    {
                        Width = 160, Height = 64, RequestedTheme = theme, Background = new SolidColorBrush(ground),
                        Children = { control }
                    };
                    canvas.Children.Clear();
                    canvas.Children.Add(frame);
                    await Task.Delay(80);
                    if (!VisualStateManager.GoToState(control, state, false)) throw new InvalidOperationException($"{name}: no state {state}");
                    await Task.Delay(300);
                    var bitmap = new RenderTargetBitmap();
                    byte[] pixels = [];
                    // A transition runs for as long as a busy machine lets it: wait until the picture stays the same.
                    for (int same = 0, tries = 0; same < 3; tries++)
                    {
                        if (tries == 100) throw new InvalidOperationException($"{name} {state} {theme}: the picture does not settle");
                        await bitmap.RenderAsync(frame);
                        var next = (await bitmap.GetPixelsAsync()).ToArray();
                        same = next.AsSpan().SequenceEqual(pixels) ? same + 1 : 0;
                        pixels = next;
                        await Task.Delay(60);
                    }
                    string file = $"{name}-{state}-{theme}.bgra";
                    File.WriteAllBytes(Path.Combine(folder, file), pixels);
                    cases.Add(new
                    {
                        control = name,
                        state,
                        theme = theme.ToString(),
                        scale = canvas.XamlRoot.RasterizationScale,
                        logical_width = 160,
                        logical_height = 64,
                        file,
                        width = bitmap.PixelWidth,
                        height = bitmap.PixelHeight
                    });
                }
            }
        }

        // Radius probes: white on black, no border, at scales 1, 1.5 and 2, plus an asymmetric corner-radii probe.
        var probes = new (string Name, int LogicalW, int LogicalH, int BoxW, int BoxH, double[] Radii, double Scale)[]
        {
            ("probe-r4-s1", 64, 64, 48, 48, [4, 4, 4, 4], 1.0),
            ("probe-r4-s15", 64, 64, 48, 48, [4, 4, 4, 4], 1.5),
            ("probe-r4-s2", 64, 64, 48, 48, [4, 4, 4, 4], 2.0),
            ("probe-r8-s1", 64, 64, 48, 48, [8, 8, 8, 8], 1.0),
            ("probe-r8-s15", 64, 64, 48, 48, [8, 8, 8, 8], 1.5),
            ("probe-r8-s2", 64, 64, 48, 48, [8, 8, 8, 8], 2.0),
            ("probe-r16-s1", 64, 64, 48, 48, [16, 16, 16, 16], 1.0),
            ("probe-r16-s15", 64, 64, 48, 48, [16, 16, 16, 16], 1.5),
            ("probe-r16-s2", 64, 64, 48, 48, [16, 16, 16, 16], 2.0),
            ("probe-asym-s1", 64, 64, 40, 20, [16, 0, 0, 0], 1.0),
        };
        foreach (var (name, logicalW, logicalH, boxW, boxH, radii, probeScale) in probes)
        {
            int pixelW = (int)Math.Round(logicalW * probeScale);
            int pixelH = (int)Math.Round(logicalH * probeScale);
            var shape = new Border
            {
                Width = boxW * probeScale,
                Height = boxH * probeScale,
                Background = new SolidColorBrush(Colors.White),
                CornerRadius = new CornerRadius(
                    radii[0] * probeScale,
                    radii[1] * probeScale,
                    radii[2] * probeScale,
                    radii[3] * probeScale),
                HorizontalAlignment = HorizontalAlignment.Center,
                VerticalAlignment = VerticalAlignment.Center
            };
            var frame = new Grid
            {
                Width = pixelW,
                Height = pixelH,
                RequestedTheme = ElementTheme.Dark,
                Background = new SolidColorBrush(Colors.Black),
                Children = { shape }
            };
            canvas.Children.Clear();
            canvas.Children.Add(frame);
            await Task.Delay(80);
            var bitmap = new RenderTargetBitmap();
            byte[] pixels = [];
            for (int same = 0, tries = 0; same < 3; tries++)
            {
                if (tries == 100) throw new InvalidOperationException($"{name}: the picture does not settle");
                await bitmap.RenderAsync(frame);
                var next = (await bitmap.GetPixelsAsync()).ToArray();
                same = next.AsSpan().SequenceEqual(pixels) ? same + 1 : 0;
                pixels = next;
                await Task.Delay(60);
            }
            string file = $"{name}.bgra";
            File.WriteAllBytes(Path.Combine(folder, file), pixels);
            cases.Add(new
            {
                control = "probe",
                name,
                scale = probeScale,
                logical_width = logicalW,
                logical_height = logicalH,
                box_width = boxW,
                box_height = boxH,
                radii,
                file,
                width = bitmap.PixelWidth,
                height = bitmap.PixelHeight
            });
        }
        var settings = new UISettings();
        var accents = new Dictionary<string, string>();
        foreach (var type in new[] { UIColorType.Accent, UIColorType.AccentLight1, UIColorType.AccentLight2, UIColorType.AccentLight3,
                                     UIColorType.AccentDark1, UIColorType.AccentDark2, UIColorType.AccentDark3 })
        {
            var c = settings.GetColorValue(type);
            accents[type.ToString()] = $"{c.A:X2}{c.R:X2}{c.G:X2}{c.B:X2}";
        }
        var scale = canvas.XamlRoot.RasterizationScale;
        File.WriteAllText(Path.Combine(folder, "cases.json"), JsonSerializer.Serialize(new { scale, accents, cases }, new JsonSerializerOptions { WriteIndented = true }));
    }
}

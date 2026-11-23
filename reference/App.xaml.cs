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
                    await bitmap.RenderAsync(frame);
                    string file = $"{name}-{state}-{theme}.bgra";
                    File.WriteAllBytes(Path.Combine(folder, file), (await bitmap.GetPixelsAsync()).ToArray());
                    cases.Add(new { control = name, state, theme = theme.ToString(), file, width = bitmap.PixelWidth, height = bitmap.PixelHeight });
                }
            }
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

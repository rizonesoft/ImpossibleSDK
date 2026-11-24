using System;
using System.Collections.Generic;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace Reference;

public sealed class LayoutNode
{
    public string Type { get; set; } = "";
    public string Name { get; set; } = "";
    public string Visibility { get; set; } = "Visible";
    public bool IsText { get; set; }
    public bool IsIcon { get; set; }
    public RectDto Rect { get; set; } = new();
    public List<LayoutNode> Children { get; set; } = new();
}

public sealed class AutomationNode
{
    public string ControlType { get; set; } = "";
    public string Name { get; set; } = "";
    public string AutomationId { get; set; } = "";
    public bool IsEnabled { get; set; }
    public bool HasKeyboardFocus { get; set; }
    public List<string> Patterns { get; set; } = new();
    public RectDto Rect { get; set; } = new();
    public List<AutomationNode> Children { get; set; } = new();
}

public sealed class RectDto
{
    public double X { get; set; }
    public double Y { get; set; }
    public double Width { get; set; }
    public double Height { get; set; }
}

public static class Dump
{
    private static readonly PatternInterface[] KnownPatterns =
    {
        PatternInterface.Invoke,
        PatternInterface.Toggle,
        PatternInterface.Value,
        PatternInterface.RangeValue,
        PatternInterface.Selection,
        PatternInterface.SelectionItem,
        PatternInterface.Scroll,
        PatternInterface.ExpandCollapse,
        PatternInterface.Text,
    };

    public static LayoutNode DumpLayout(FrameworkElement frameRoot, double logicalScale = 1.0)
    {
        return WalkVisual(frameRoot, frameRoot, logicalScale);
    }

    private static LayoutNode WalkVisual(DependencyObject current, FrameworkElement frameRoot, double logicalScale)
    {
        var node = new LayoutNode
        {
            Type = current.GetType().Name,
        };

        if (current is FrameworkElement fe)
        {
            node.Name = fe.Name ?? "";
            node.Visibility = fe.Visibility.ToString();
            node.IsText = fe is TextBlock or RichTextBlock;
            node.IsIcon = fe is FontIcon or SymbolIcon or PathIcon;
            try
            {
                var transform = fe.TransformToVisual(frameRoot);
                var bounds = transform.TransformBounds(new Rect(0, 0, fe.ActualWidth, fe.ActualHeight));
                double inv = logicalScale > 0 ? 1.0 / logicalScale : 1.0;
                node.Rect = new RectDto
                {
                    X = Math.Round(bounds.X * inv, 3),
                    Y = Math.Round(bounds.Y * inv, 3),
                    Width = Math.Round(bounds.Width * inv, 3),
                    Height = Math.Round(bounds.Height * inv, 3),
                };
            }
            catch
            {
                node.Rect = new RectDto();
            }
        }

        int count = VisualTreeHelper.GetChildrenCount(current);
        for (int i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(current, i);
            node.Children.Add(WalkVisual(child, frameRoot, logicalScale));
        }

        return node;
    }

    public static AutomationNode DumpAutomation(FrameworkElement frameRoot, FrameworkElement targetRoot, double logicalScale = 1.0)
    {
        double hostDpi = frameRoot.XamlRoot?.RasterizationScale ?? 1.0;
        double totalScale = hostDpi * (logicalScale > 0 ? logicalScale : 1.0);
        double inv = totalScale > 0 ? 1.0 / totalScale : 1.0;

        Point frameOrigin = new(0, 0);
        var framePeer = FrameworkElementAutomationPeer.CreatePeerForElement(frameRoot);
        if (framePeer != null)
        {
            var fb = framePeer.GetBoundingRectangle();
            if (!fb.IsEmpty && !double.IsInfinity(fb.X) && !double.IsInfinity(fb.Y))
            {
                frameOrigin = new Point(fb.X, fb.Y);
            }
        }
        else
        {
            var anchor = FindFirstPeerElement(targetRoot);
            if (anchor.HasValue)
            {
                var (anchorEl, anchorPeer) = anchor.Value;
                var tb = anchorPeer.GetBoundingRectangle();
                var rel = anchorEl.TransformToVisual(frameRoot).TransformBounds(new Rect(0, 0, anchorEl.ActualWidth, anchorEl.ActualHeight));
                if (!tb.IsEmpty && !double.IsInfinity(tb.X) && !double.IsInfinity(tb.Y))
                {
                    frameOrigin = new Point(tb.X - rel.X * totalScale, tb.Y - rel.Y * totalScale);
                }
            }
        }

        var peer = FrameworkElementAutomationPeer.CreatePeerForElement(targetRoot);
        if (peer == null)
        {
            var relBounds = targetRoot.TransformToVisual(frameRoot).TransformBounds(new Rect(0, 0, targetRoot.ActualWidth, targetRoot.ActualHeight));
            var synthetic = new AutomationNode
            {
                ControlType = targetRoot.GetType().Name,
                Name = targetRoot.Name ?? "",
                Rect = new RectDto
                {
                    X = Math.Round(relBounds.X, 3),
                    Y = Math.Round(relBounds.Y, 3),
                    Width = Math.Round(relBounds.Width, 3),
                    Height = Math.Round(relBounds.Height, 3),
                },
                IsEnabled = targetRoot is Control c ? c.IsEnabled : true,
            };
            CollectDescendantPeers(targetRoot, frameOrigin, inv, synthetic.Children);
            return synthetic;
        }

        return WalkPeer(peer, frameOrigin, inv);
    }

    private static (FrameworkElement Element, AutomationPeer Peer)? FindFirstPeerElement(DependencyObject root)
    {
        if (root is FrameworkElement fe)
        {
            var p = FrameworkElementAutomationPeer.CreatePeerForElement(fe);
            if (p != null)
            {
                return (fe, p);
            }
        }
        int count = VisualTreeHelper.GetChildrenCount(root);
        for (int i = 0; i < count; i++)
        {
            var found = FindFirstPeerElement(VisualTreeHelper.GetChild(root, i));
            if (found.HasValue)
            {
                return found;
            }
        }
        return null;
    }

    private static void CollectDescendantPeers(DependencyObject parent, Point frameOrigin, double invScale, List<AutomationNode> into)
    {
        int count = VisualTreeHelper.GetChildrenCount(parent);
        for (int i = 0; i < count; i++)
        {
            var child = VisualTreeHelper.GetChild(parent, i);
            if (child is FrameworkElement fe)
            {
                var p = FrameworkElementAutomationPeer.CreatePeerForElement(fe);
                if (p != null)
                {
                    into.Add(WalkPeer(p, frameOrigin, invScale));
                    continue;
                }
            }
            CollectDescendantPeers(child, frameOrigin, invScale, into);
        }
    }

    private static AutomationNode WalkPeer(AutomationPeer peer, Point frameOrigin, double invScale)
    {
        var rect = new RectDto();
        try
        {
            var b = peer.GetBoundingRectangle();
            if (!b.IsEmpty && !double.IsInfinity(b.X) && !double.IsInfinity(b.Y) && !double.IsInfinity(b.Width) && !double.IsInfinity(b.Height))
            {
                rect = new RectDto
                {
                    X = Math.Round((b.X - frameOrigin.X) * invScale, 3),
                    Y = Math.Round((b.Y - frameOrigin.Y) * invScale, 3),
                    Width = Math.Round(b.Width * invScale, 3),
                    Height = Math.Round(b.Height * invScale, 3),
                };
            }
        }
        catch
        {
            // Keep default zero rect if peer cannot report bounds.
        }

        var patterns = new List<string>();
        foreach (var pat in KnownPatterns)
        {
            try
            {
                if (peer.GetPattern(pat) != null)
                {
                    patterns.Add(pat.ToString());
                }
            }
            catch
            {
                // Ignore unsupported pattern queries.
            }
        }

        var node = new AutomationNode
        {
            ControlType = peer.GetAutomationControlType().ToString(),
            Name = peer.GetName() ?? "",
            AutomationId = peer.GetAutomationId() ?? "",
            IsEnabled = peer.IsEnabled(),
            HasKeyboardFocus = peer.HasKeyboardFocus(),
            Patterns = patterns,
            Rect = rect,
        };

        var children = peer.GetChildren();
        if (children != null)
        {
            foreach (var child in children)
            {
                if (child != null)
                {
                    node.Children.Add(WalkPeer(child, frameOrigin, invScale));
                }
            }
        }

        return node;
    }
}

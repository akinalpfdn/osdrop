using System.Globalization;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace Osdrop.App.Arrangement;

/// <summary>A computer dropped in the editor: its new origin, before snapping.</summary>
public sealed record ArrangementMove(string DeviceId, Point Origin);

/// <summary>
/// Every member's monitors to scale, like Windows' Display settings: drag a computer and drop it
/// beside another one; its monitors move together (the Mac's ArrangementEditor). Colors come from
/// the theme, so a light/dark switch redraws it.
/// </summary>
public sealed class ArrangementEditor : FrameworkElement
{
    public static readonly DependencyProperty GeometryProperty = DependencyProperty.Register(
        nameof(Geometry), typeof(ArrangementGeometry), typeof(ArrangementEditor),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    public static readonly DependencyProperty MoveCommandProperty = DependencyProperty.Register(
        nameof(MoveCommand), typeof(ICommand), typeof(ArrangementEditor));

    public static readonly DependencyProperty ThisPcBrushProperty = BrushProperty(nameof(ThisPcBrush));
    public static readonly DependencyProperty MemberBrushProperty = BrushProperty(nameof(MemberBrush));
    public static readonly DependencyProperty OutlineBrushProperty = BrushProperty(nameof(OutlineBrush));
    public static readonly DependencyProperty TextBrushProperty = BrushProperty(nameof(TextBrush));

    private string? _dragged;
    private Point _dragStart;
    private Vector _translation;

    public ArrangementEditor()
    {
        SetResourceReference(ThisPcBrushProperty, "AccentFillColorDefaultBrush");
        SetResourceReference(MemberBrushProperty, "ControlStrongFillColorDefaultBrush");
        SetResourceReference(OutlineBrushProperty, "ControlStrokeColorDefaultBrush");
        SetResourceReference(TextBrushProperty, "TextFillColorPrimaryBrush");
        SetResourceReference(HeightProperty, "Osdrop.Arrangement.Height");
        Focusable = false;
    }

    public ArrangementGeometry? Geometry
    {
        get => (ArrangementGeometry?)GetValue(GeometryProperty);
        set => SetValue(GeometryProperty, value);
    }

    /// <summary>Runs with an <see cref="ArrangementMove"/> when a computer is dropped.</summary>
    public ICommand? MoveCommand
    {
        get => (ICommand?)GetValue(MoveCommandProperty);
        set => SetValue(MoveCommandProperty, value);
    }

    public Brush? ThisPcBrush
    {
        get => (Brush?)GetValue(ThisPcBrushProperty);
        set => SetValue(ThisPcBrushProperty, value);
    }

    public Brush? MemberBrush
    {
        get => (Brush?)GetValue(MemberBrushProperty);
        set => SetValue(MemberBrushProperty, value);
    }

    public Brush? OutlineBrush
    {
        get => (Brush?)GetValue(OutlineBrushProperty);
        set => SetValue(OutlineBrushProperty, value);
    }

    public Brush? TextBrush
    {
        get => (Brush?)GetValue(TextBrushProperty);
        set => SetValue(TextBrushProperty, value);
    }

    protected override void OnRender(DrawingContext drawingContext)
    {
        // Hit testing needs something under the whole area, also where no computer is.
        drawingContext.DrawRectangle(Brushes.Transparent, null, new Rect(RenderSize));
        if (Geometry is not { } geometry || !TryLayout(geometry, out Rect bounds, out double scale))
        {
            return;
        }
        double radius = Token("Osdrop.Arrangement.CornerRadius");
        var outline = new Pen(OutlineBrush, Token("Osdrop.Arrangement.BorderWidth"));
        foreach (ArrangementComputer computer in geometry.Computers)
        {
            Vector shift = computer.DeviceId == _dragged ? _translation : default;
            drawingContext.PushOpacity(OpacityOf(computer));
            Rect[] monitors = [.. computer.PlacedMonitors.Select(monitor => Rect.Offset(ToView(monitor, bounds, scale), shift))];
            Brush? fill = computer.IsThisPc ? ThisPcBrush : MemberBrush;
            foreach (Rect monitor in monitors)
            {
                drawingContext.DrawRoundedRectangle(fill, outline, monitor, radius, radius);
            }
            drawingContext.Pop();
            if (monitors.Length > 0)
            {
                DrawName(drawingContext, computer.Name, monitors.MaxBy(monitor => monitor.Width * monitor.Height));
            }
        }
    }

    protected override void OnMouseLeftButtonDown(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonDown(e);
        Point at = e.GetPosition(this);
        if (Geometry is not { } geometry || !TryLayout(geometry, out Rect bounds, out double scale))
        {
            return;
        }
        ArrangementComputer? hit = geometry.Computers.LastOrDefault(computer =>
            computer.PlacedMonitors.Any(monitor => ToView(monitor, bounds, scale).Contains(at)));
        if (hit is null)
        {
            return;
        }
        _dragged = hit.DeviceId;
        _dragStart = at;
        _translation = default;
        _ = CaptureMouse();
        e.Handled = true;
    }

    protected override void OnMouseMove(MouseEventArgs e)
    {
        base.OnMouseMove(e);
        if (_dragged is null)
        {
            Cursor = Hit(e.GetPosition(this)) ? Cursors.Hand : null;
            return;
        }
        _translation = e.GetPosition(this) - _dragStart;
        InvalidateVisual();
    }

    protected override void OnMouseLeftButtonUp(MouseButtonEventArgs e)
    {
        base.OnMouseLeftButtonUp(e);
        if (_dragged is not string device)
        {
            return;
        }
        ReleaseMouseCapture();
        _dragged = null;
        if (Geometry is { } geometry && TryLayout(geometry, out _, out double scale) &&
            geometry.Computers.FirstOrDefault(computer => computer.DeviceId == device) is { } moved &&
            _translation.Length > 0)
        {
            var move = new ArrangementMove(device, moved.Offset + (_translation / scale));
            if (MoveCommand?.CanExecute(move) == true)
            {
                MoveCommand.Execute(move);
            }
        }
        _translation = default;
        InvalidateVisual();
        e.Handled = true;
    }

    protected override void OnLostMouseCapture(MouseEventArgs e)
    {
        base.OnLostMouseCapture(e);
        _dragged = null; // Escape or another window: nothing moves
        _translation = default;
        InvalidateVisual();
    }

    private static DependencyProperty BrushProperty(string name) => DependencyProperty.Register(
        name, typeof(Brush), typeof(ArrangementEditor),
        new FrameworkPropertyMetadata(null, FrameworkPropertyMetadataOptions.AffectsRender));

    private static Rect ToView(Rect monitor, Rect bounds, double scale) =>
        new((monitor.X - bounds.X) * scale, (monitor.Y - bounds.Y) * scale, monitor.Width * scale, monitor.Height * scale);

    private bool Hit(Point at) =>
        Geometry is { } geometry && TryLayout(geometry, out Rect bounds, out double scale) &&
        geometry.Computers.Any(computer => computer.PlacedMonitors.Any(monitor => ToView(monitor, bounds, scale).Contains(at)));

    private bool TryLayout(ArrangementGeometry geometry, out Rect bounds, out double scale)
    {
        bounds = geometry.Bounds(Token("Osdrop.Arrangement.MarginFraction"));
        scale = 0;
        if (bounds.IsEmpty || bounds.Width <= 0 || bounds.Height <= 0 || ActualWidth <= 0 || ActualHeight <= 0)
        {
            return false;
        }
        scale = Math.Min(ActualWidth / bounds.Width, ActualHeight / bounds.Height);
        // Centred in the editor.
        double width = ActualWidth / scale;
        double height = ActualHeight / scale;
        bounds = new Rect(
            bounds.X - ((width - bounds.Width) / 2), bounds.Y - ((height - bounds.Height) / 2), width, height);
        return true;
    }

    private double OpacityOf(ArrangementComputer computer) => Token(
        computer.IsThisPc ? "Osdrop.Arrangement.ThisPcOpacity"
        : computer.IsConnected ? "Osdrop.Arrangement.MemberOpacity"
        : "Osdrop.Arrangement.OfflineOpacity");

    private void DrawName(DrawingContext drawingContext, string name, Rect monitor)
    {
        double padding = Token("Osdrop.Arrangement.LabelPadding");
        var text = new FormattedText(
            name, CultureInfo.CurrentCulture, FlowDirection, new Typeface(SystemFonts.MessageFontFamily.Source),
            Token("Osdrop.FontSize.Caption"), TextBrush, VisualTreeHelper.GetDpi(this).PixelsPerDip)
        {
            MaxTextWidth = Math.Max(1, monitor.Width - (2 * padding)),
            MaxTextHeight = Math.Max(1, monitor.Height - (2 * padding)),
            TextAlignment = TextAlignment.Center,
            Trimming = TextTrimming.None,
        };
        drawingContext.DrawText(text, new Point(monitor.X + padding, monitor.Y + ((monitor.Height - text.Height) / 2)));
    }

    private double Token(string key) => TryFindResource(key) is double value ? value : 0;
}

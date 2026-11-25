using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Interop;
using System.Windows.Media;
using Osdrop.App.Transfers;

namespace Osdrop.App.Corners;

/// <summary>
/// A phone's box: a topmost window that never takes the focus. The engine places it in the phone's corner
/// (in physical pixels); while a drag hovers it lets the drag through to the corner's target. A received file
/// is dragged out of it as a copy, into Explorer or any app.
/// </summary>
public partial class CornerBoxWindow : Window
{
    private const int _exStyle = -20;
    private const long _transparent = 0x20;
    private const long _toolWindow = 0x80;
    private const long _noActivate = 0x0800_0000;

    private Point? _pressedAt;

    internal CornerBoxWindow(CornerBoxViewModel box)
    {
        InitializeComponent();
        DataContext = box;
        Handle = new WindowInteropHelper(this).EnsureHandle();
        SetStyle(_toolWindow | _noActivate, on: true);
    }

    /// <summary>The window's handle, for the engine to place it.</summary>
    internal nint Handle { get; }

    /// <summary>A file is about to be dragged out (true) or the drag ended (false): the corners must not take it.</summary>
    internal event Action<bool>? DraggingOut;

    /// <summary>While a drag hovers, the box shows without catching it: the corner's target under it takes it.</summary>
    internal void LetsDragsThrough(bool through) => SetStyle(_transparent, through);

    private void SetStyle(long bits, bool on)
    {
        long style = GetWindowLongPtr(Handle, _exStyle).ToInt64();
        long changed = on ? style | bits : style & ~bits;
        if (changed != style)
        {
            _ = SetWindowLongPtr(Handle, _exStyle, new nint(changed));
        }
    }

    private void OnRowPressed(object sender, MouseButtonEventArgs e)
    {
        if (sender is FrameworkElement { Tag: TransferRow { HasFile: true } } row && !OnButton(e.OriginalSource as DependencyObject))
        {
            _pressedAt = e.GetPosition(this);
            DraggingOut?.Invoke(true); // before the corners show for the held button
            _ = row.CaptureMouse();
        }
    }

    private void OnRowMoved(object sender, MouseEventArgs e)
    {
        if (_pressedAt is not { } start || e.LeftButton != MouseButtonState.Pressed
            || sender is not FrameworkElement { Tag: TransferRow { File: { } file } } row)
        {
            return;
        }
        Vector moved = e.GetPosition(this) - start;
        if (Math.Abs(moved.X) < SystemParameters.MinimumHorizontalDragDistance
            && Math.Abs(moved.Y) < SystemParameters.MinimumVerticalDragDistance)
        {
            return;
        }
        _pressedAt = null;
        row.ReleaseMouseCapture();
        try
        {
            _ = DragDrop.DoDragDrop(row, new DataObject(DataFormats.FileDrop, new[] { file }), DragDropEffects.Copy);
        }
        catch (COMException)
        {
            // The target failed: the file stays where it is.
        }
        finally
        {
            DraggingOut?.Invoke(false);
        }
    }

    private void OnRowReleased(object sender, MouseButtonEventArgs e)
    {
        if (_pressedAt is null)
        {
            return;
        }
        _pressedAt = null;
        (sender as UIElement)?.ReleaseMouseCapture();
        DraggingOut?.Invoke(false);
    }

    /// <summary>A press on the row's buttons is theirs: it starts no drag.</summary>
    private static bool OnButton(DependencyObject? element)
    {
        for (; element is not null; element = VisualTreeHelper.GetParent(element))
        {
            if (element is ButtonBase)
            {
                return true;
            }
        }
        return false;
    }

    [LibraryImport("user32.dll", EntryPoint = "GetWindowLongPtrW")]
    private static partial nint GetWindowLongPtr(nint window, int index);

    [LibraryImport("user32.dll", EntryPoint = "SetWindowLongPtrW")]
    private static partial nint SetWindowLongPtr(nint window, int index, nint value);
}

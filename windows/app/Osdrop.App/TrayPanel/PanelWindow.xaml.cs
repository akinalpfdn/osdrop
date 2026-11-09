using System.Windows;
using System.Windows.Controls.Primitives;
using System.Windows.Input;

namespace Osdrop.App.TrayPanel;

/// <summary>
/// The panel the tray icon opens, in the corner above the taskbar; it closes when it loses focus,
/// like the Mac's menu bar window.
/// </summary>
public partial class PanelWindow : Wpf.Ui.Controls.FluentWindow
{
    private DateTime _hiddenAt = DateTime.MinValue;

    public PanelWindow(PanelViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
        // No SystemThemeWatcher here: it makes the window background transparent for a backdrop,
        // which leaves this borderless panel white. The XAML's ApplicationBackgroundBrush follows the
        // theme the app applies on system changes.
        SizeChanged += (_, _) => PlaceAboveTaskbar();
    }

    /// <summary>Set when the app quits; until then closing only hides the panel.</summary>
    internal bool IsQuitting { get; set; }

    /// <summary>Shows the panel, or hides it when it is already open (a second tray click).</summary>
    public void Toggle()
    {
        if (IsVisible)
        {
            Hide();
            return;
        }
        if (DateTime.UtcNow - _hiddenAt < AppTunables.PanelReopenGuard)
        {
            return; // the tray click that took focus away already closed it
        }
        Show();
        PlaceAboveTaskbar();
        _ = Activate();
    }

    protected override void OnDeactivated(EventArgs e)
    {
        base.OnDeactivated(e);
        _hiddenAt = DateTime.UtcNow;
        Hide();
    }

    protected override void OnPreviewKeyDown(KeyEventArgs e)
    {
        base.OnPreviewKeyDown(e);
        if (e.Key == Key.Escape)
        {
            Hide();
            e.Handled = true;
        }
    }

    protected override void OnClosing(System.ComponentModel.CancelEventArgs e)
    {
        if (!IsQuitting)
        {
            // Alt+F4 only hides it: the panel lives as long as the tray icon.
            e.Cancel = true;
            Hide();
        }
        base.OnClosing(e);
    }

    /// <summary>A member's "…" button: its menu opens on a left click.</summary>
    private void OpenMenu(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { ContextMenu: { } menu } button)
        {
            menu.PlacementTarget = button;
            menu.Placement = PlacementMode.Bottom;
            menu.IsOpen = true;
        }
    }

    private void PlaceAboveTaskbar()
    {
        Rect area = SystemParameters.WorkArea; // the primary monitor without the taskbar
        double margin = (double)FindResource("Osdrop.Panel.EdgeMargin");
        MaxHeight = area.Height - (2 * margin); // a long group scrolls
        Left = area.Right - ActualWidth - margin;
        Top = area.Bottom - ActualHeight - margin;
    }
}

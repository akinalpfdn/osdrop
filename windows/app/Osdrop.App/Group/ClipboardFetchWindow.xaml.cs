using System.ComponentModel;
using System.Windows;

namespace Osdrop.App.Group;

/// <summary>Copied files on their way from another computer, in the corner above the taskbar.</summary>
public partial class ClipboardFetchWindow : Wpf.Ui.Controls.FluentWindow
{
    private readonly GroupViewModel _group;
    private bool _closingFromModel;

    public ClipboardFetchWindow(GroupViewModel group)
    {
        InitializeComponent();
        _group = group;
        Loaded += (_, _) => PlaceInCorner();
    }

    /// <summary>Closes when the files are no longer on their way, without cancelling them.</summary>
    internal void CloseFromModel()
    {
        _closingFromModel = true;
        Close();
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!_closingFromModel)
        {
            _group.CancelClipboardFetch(); // closed by the user
        }
        base.OnClosing(e);
    }

    private void PlaceInCorner()
    {
        Rect area = SystemParameters.WorkArea;
        double margin = (double)FindResource("Osdrop.Panel.EdgeMargin");
        Left = area.Right - ActualWidth - margin;
        Top = area.Bottom - ActualHeight - margin;
    }
}

/// <summary>Shows the window while copied files are on their way.</summary>
internal sealed class ClipboardFetchWindowPresenter
{
    private readonly GroupViewModel _group;
    private ClipboardFetchWindow? _window;

    public ClipboardFetchWindowPresenter(GroupViewModel group)
    {
        _group = group;
        _group.PropertyChanged += OnGroupChanged;
    }

    private void OnGroupChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(GroupViewModel.ClipboardFetch))
        {
            return;
        }
        if (_group.ClipboardFetch is null)
        {
            _window?.CloseFromModel();
            _window = null;
            return;
        }
        if (_window is null)
        {
            _window = new ClipboardFetchWindow(_group);
            _window.Closed += (_, _) => _window = null;
        }
        _window.DataContext = _group.ClipboardFetch;
        _window.Show();
    }
}

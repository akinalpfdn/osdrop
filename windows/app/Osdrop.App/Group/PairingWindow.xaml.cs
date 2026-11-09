using System.ComponentModel;
using Wpf.Ui.Appearance;

namespace Osdrop.App.Group;

/// <summary>The pairing code on this PC, whichever computer started the pairing.</summary>
public partial class PairingWindow : Wpf.Ui.Controls.FluentWindow
{
    private readonly GroupViewModel _group;
    private bool _closingFromModel;

    public PairingWindow(GroupViewModel group)
    {
        InitializeComponent();
        _group = group;
        SystemThemeWatcher.Watch(this);
    }

    /// <summary>Closes when the pairing ends, without counting as the user's answer.</summary>
    internal void CloseFromModel()
    {
        _closingFromModel = true;
        Close();
    }

    protected override void OnClosing(CancelEventArgs e)
    {
        if (!_closingFromModel)
        {
            _group.ClosePairing(); // a code still on screen is rejected
        }
        base.OnClosing(e);
    }
}

/// <summary>Shows the pairing window while the group has a pairing in progress.</summary>
internal sealed class PairingWindowPresenter
{
    private readonly GroupViewModel _group;
    private PairingWindow? _window;

    public PairingWindowPresenter(GroupViewModel group)
    {
        _group = group;
        _group.PropertyChanged += OnGroupChanged;
    }

    private void OnGroupChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName != nameof(GroupViewModel.Pairing))
        {
            return;
        }
        if (_group.Pairing is null)
        {
            _window?.CloseFromModel();
            _window = null;
            return;
        }
        if (_window is null)
        {
            _window = new PairingWindow(_group);
            _window.Closed += (_, _) => _window = null;
        }
        _window.DataContext = _group.Pairing;
        _window.Show();
        _ = _window.Activate();
    }
}

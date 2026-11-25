using Wpf.Ui.Appearance;

namespace Osdrop.App.Transfers;

/// <summary>Every transfer the PC remembers, newest first; the panel shows only the newest few.</summary>
public partial class TransfersWindow : Wpf.Ui.Controls.FluentWindow
{
    public TransfersWindow(TransfersViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
        SystemThemeWatcher.Watch(this);
    }
}

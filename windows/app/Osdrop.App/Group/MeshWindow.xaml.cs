using Wpf.Ui.Appearance;

namespace Osdrop.App.Group;

/// <summary>The mesh as it runs: this PC, its group and every member's session (debug builds).</summary>
public partial class MeshWindow : Wpf.Ui.Controls.FluentWindow
{
    public MeshWindow(GroupViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
        SystemThemeWatcher.Watch(this);
    }
}

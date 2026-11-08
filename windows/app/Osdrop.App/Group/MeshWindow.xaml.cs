using System.Windows;

namespace Osdrop.App.Group;

/// <summary>The mesh as it runs: this PC, its group and every member's session (debug builds).</summary>
public partial class MeshWindow : Window
{
    public MeshWindow(GroupViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }
}

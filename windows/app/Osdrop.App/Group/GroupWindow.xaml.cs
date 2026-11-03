using System.Windows;

namespace Osdrop.App.Group;

/// <summary>Members and nearby computers (temporary UI, final in Phase 17).</summary>
public partial class GroupWindow : Window
{
    public GroupWindow(GroupViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
    }
}

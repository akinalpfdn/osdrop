using Wpf.Ui.Appearance;

namespace Osdrop.App.Settings;

/// <summary>The Settings window; one instance, brought to the front when opened again.</summary>
public partial class SettingsWindow : Wpf.Ui.Controls.FluentWindow
{
    public SettingsWindow(SettingsViewModel viewModel)
    {
        InitializeComponent();
        DataContext = viewModel;
        SystemThemeWatcher.Watch(this);
        viewModel.Appeared();
    }
}

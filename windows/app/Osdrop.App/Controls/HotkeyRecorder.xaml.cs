using System.Windows;
using System.Windows.Input;
using Osdrop.App.Engine;
using Osdrop.App.Services;

namespace Osdrop.App.Controls;

/// <summary>
/// A chord field (the Mac's HotkeyRecorder): click, then press the new combination. Escape alone
/// cancels; with modifiers it records, so Ctrl+Alt+Win+Esc can be chosen again.
/// </summary>
public partial class HotkeyRecorder : System.Windows.Controls.UserControl
{
    public static readonly DependencyProperty HotkeyProperty = DependencyProperty.Register(
        nameof(Hotkey), typeof(Hotkey), typeof(HotkeyRecorder),
        new FrameworkPropertyMetadata(
            Hotkey.DefaultReturnHome, FrameworkPropertyMetadataOptions.BindsTwoWayByDefault,
            (d, _) => ((HotkeyRecorder)d).ShowHotkey()));

    private bool _recording;

    public HotkeyRecorder()
    {
        InitializeComponent();
        ShowHotkey();
    }

    public Hotkey Hotkey
    {
        get => (Hotkey)GetValue(HotkeyProperty);
        set => SetValue(HotkeyProperty, value);
    }

    private void OnClick(object sender, RoutedEventArgs e)
    {
        _recording = true;
        Recorder.Content = Localization.Strings.HotkeyRecording;
        Hint.Visibility = Visibility.Collapsed;
    }

    private void OnPreviewKeyDown(object sender, KeyEventArgs e)
    {
        if (!_recording)
        {
            return;
        }
        e.Handled = true; // the key press is for the recorder, not the window
        Key key = e.Key == Key.System ? e.SystemKey : e.Key;
        ModifierKeys modifiers = Keyboard.Modifiers;
        if (key == Key.Escape && modifiers == ModifierKeys.None)
        {
            StopRecording();
            return;
        }
        Hotkey? pressed = HotkeyText.From(key, modifiers);
        if (pressed is null)
        {
            return; // a modifier on its way to a chord
        }
        if (!pressed.HasModifier)
        {
            Hint.Visibility = Visibility.Visible;
            return;
        }
        Hotkey = pressed;
        StopRecording();
    }

    private void OnLostFocus(object sender, KeyboardFocusChangedEventArgs e) => StopRecording();

    private void StopRecording()
    {
        _recording = false;
        Hint.Visibility = Visibility.Collapsed;
        ShowHotkey();
    }

    private void ShowHotkey()
    {
        if (Recorder is not null && !_recording)
        {
            Recorder.Content = HotkeyText.Display(Hotkey);
            System.Windows.Automation.AutomationProperties.SetName(Recorder, HotkeyText.Display(Hotkey));
        }
    }
}

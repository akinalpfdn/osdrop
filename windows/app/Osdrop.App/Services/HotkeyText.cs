using System.Runtime.InteropServices;
using System.Windows.Input;
using Osdrop.App.Engine;

namespace Osdrop.App.Services;

/// <summary>Chords as Settings shows and records them.</summary>
internal static partial class HotkeyText
{
    private const uint _virtualKeyToScanCodeExtended = 4; // MAPVK_VK_TO_VSC_EX
    private const int _extendedPrefix = 0xE0;
    private const int _keyNameLength = 64;

    /// <summary>"Ctrl+Alt+Win+Esc", with the key named as this PC's keyboard layout names it.</summary>
    public static string Display(Hotkey hotkey)
    {
        var parts = new List<string>();
        if ((hotkey.Modifiers & Hotkey.Control) != 0)
        {
            parts.Add(Localization.Strings.KeyControl);
        }
        if ((hotkey.Modifiers & Hotkey.Alt) != 0)
        {
            parts.Add(Localization.Strings.KeyAlt);
        }
        if ((hotkey.Modifiers & Hotkey.Shift) != 0)
        {
            parts.Add(Localization.Strings.KeyShift);
        }
        if ((hotkey.Modifiers & Hotkey.Windows) != 0)
        {
            parts.Add(Localization.Strings.KeyWindows);
        }
        parts.Add(KeyName(hotkey.ScanCode, hotkey.Extended));
        return string.Join('+', parts);
    }

    /// <summary>
    /// The chord of a key press in a WPF window, or null for a modifier key alone. The key is kept
    /// by its scan code, as the hook sees it.
    /// </summary>
    public static Hotkey? From(Key key, ModifierKeys modifiers)
    {
        if (key is Key.LeftCtrl or Key.RightCtrl or Key.LeftAlt or Key.RightAlt or Key.LeftShift
            or Key.RightShift or Key.LWin or Key.RWin or Key.System or Key.None)
        {
            return null;
        }
        uint scan = MapVirtualKeyW((uint)KeyInterop.VirtualKeyFromKey(key), _virtualKeyToScanCodeExtended);
        if (scan == 0)
        {
            return null; // a key without a scan code
        }
        byte bits = 0;
        bits |= modifiers.HasFlag(ModifierKeys.Control) ? Hotkey.Control : (byte)0;
        bits |= modifiers.HasFlag(ModifierKeys.Alt) ? Hotkey.Alt : (byte)0;
        bits |= modifiers.HasFlag(ModifierKeys.Shift) ? Hotkey.Shift : (byte)0;
        bits |= modifiers.HasFlag(ModifierKeys.Windows) ? Hotkey.Windows : (byte)0;
        return new Hotkey(bits, (ushort)(scan & 0xFF), (scan >> 8) == _extendedPrefix);
    }

    private static unsafe string KeyName(ushort scanCode, bool extended)
    {
        int parameter = (scanCode << 16) | (extended ? 1 << 24 : 0);
        char* name = stackalloc char[_keyNameLength];
        int length = GetKeyNameTextW(parameter, name, _keyNameLength);
        return length > 0 ? new string(name, 0, length) : $"0x{scanCode:X2}";
    }

    [LibraryImport("user32.dll")]
    private static partial uint MapVirtualKeyW(uint code, uint mapType);

    [LibraryImport("user32.dll")]
    private static unsafe partial int GetKeyNameTextW(int parameter, char* name, int size);
}

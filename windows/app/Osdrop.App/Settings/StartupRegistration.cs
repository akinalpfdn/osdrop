using Microsoft.Win32;
using Osdrop.App.Diagnostics;
using Osdrop.App.Interop;

namespace Osdrop.App.Settings;

/// <summary>"Start with Windows": the current user's Run key (the installer removes it on uninstall).</summary>
internal static class StartupRegistration
{
    private const string _runKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string _valueName = "Osdrop";

    public static bool IsEnabled
    {
        get
        {
            using RegistryKey? run = Registry.CurrentUser.OpenSubKey(_runKey);
            return run?.GetValue(_valueName) is string;
        }
    }

    /// <summary>Returns false when the registry refused the change.</summary>
    public static bool SetEnabled(bool enabled)
    {
        try
        {
            using RegistryKey run = Registry.CurrentUser.CreateSubKey(_runKey);
            if (enabled)
            {
                run.SetValue(_valueName, $"\"{Environment.ProcessPath}\"");
            }
            else
            {
                run.DeleteValue(_valueName, throwOnMissingValue: false);
            }
            return true;
        }
        catch (Exception error) when (error is UnauthorizedAccessException or System.Security.SecurityException
            or System.IO.IOException)
        {
            Log.Write(LogLevel.Warning, $"Start with Windows could not be changed: {error.Message}");
            return false;
        }
    }
}

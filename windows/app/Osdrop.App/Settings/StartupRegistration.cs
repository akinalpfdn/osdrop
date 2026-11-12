using Microsoft.Win32;
using Osdrop.App.Diagnostics;
using Osdrop.App.Interop;

namespace Osdrop.App.Settings;

/// <summary>"Start with Windows": the current user's Run key (the installer removes it on uninstall).</summary>
internal static class StartupRegistration
{
    private const string _runKey = @"Software\Microsoft\Windows\CurrentVersion\Run";
    private const string _valueName = "Osdrop";

    public static bool IsEnabled => Stored() is not null;

    /// <summary>
    /// When it is on, points it at this copy: an Osdrop installed or built elsewhere since it was
    /// turned on would otherwise start the old copy at sign-in, or nothing.
    /// </summary>
    public static void FollowThisCopy()
    {
        string? stored = Stored();
        if (stored is null || string.Equals(stored, Command, StringComparison.OrdinalIgnoreCase))
        {
            return;
        }
        if (SetEnabled(true))
        {
            Log.Write(LogLevel.Info, "Start with Windows now starts this copy");
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
                run.SetValue(_valueName, Command);
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

    private static string Command => $"\"{Environment.ProcessPath}\"";

    private static string? Stored()
    {
        using RegistryKey? run = Registry.CurrentUser.OpenSubKey(_runKey);
        return run?.GetValue(_valueName) as string;
    }
}

using System.Diagnostics;
using System.IO;
using Osdrop.App.Diagnostics;
using Osdrop.App.Interop;

namespace Osdrop.App.Services;

/// <summary>Opens links and folders.</summary>
internal static class Shell
{
    public static void Open(Uri target) => Start(target.ToString());

    public static void OpenFolder(string folder)
    {
        _ = Directory.CreateDirectory(folder);
        Start(folder);
    }

    private static void Start(string target)
    {
        try
        {
            using var process = Process.Start(new ProcessStartInfo(target) { UseShellExecute = true });
        }
        catch (Exception error) when (error is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            Log.Write(LogLevel.Warning, $"Could not open {target}: {error.Message}");
        }
    }
}

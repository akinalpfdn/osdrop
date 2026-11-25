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

    /// <summary>An Explorer window on the file's folder, the file selected.</summary>
    public static void ShowInFolder(string file)
    {
        var start = new ProcessStartInfo("explorer.exe") { UseShellExecute = false };
        start.ArgumentList.Add($"/select,{file}");
        Start(start, file);
    }

    private static void Start(string target) =>
        Start(new ProcessStartInfo(target) { UseShellExecute = true }, target);

    private static void Start(ProcessStartInfo start, string target)
    {
        try
        {
            using var process = Process.Start(start);
        }
        catch (Exception error) when (error is System.ComponentModel.Win32Exception or InvalidOperationException)
        {
            Log.Write(LogLevel.Warning, $"Could not open {target}: {error.Message}");
        }
    }
}

using System.Globalization;
using System.IO;
using System.Text;
using System.Threading.Channels;
using Osdrop.App.Interop;

namespace Osdrop.App.Diagnostics;

/// <summary>
/// The shell's and the engine's log lines in %LOCALAPPDATA%\Osdrop\logs\osdrop.log, rolled over at
/// <see cref="AppTunables.LogFileLimitBytes"/>. Writing never blocks the caller (engine events
/// arrive on the UI thread); a background task appends. Lines never carry keys, keystrokes or file
/// contents: core and the engine never log them.
/// </summary>
internal static class Log
{
    public const string FileName = "osdrop.log";
    private const string _previousFileName = "osdrop.1.log";

    private static readonly Channel<string> _lines = Channel.CreateUnbounded<string>(
        new UnboundedChannelOptions { SingleReader = true });

    private static readonly Task _writer = Task.Run(WriteLinesAsync);

    public static string Folder { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Osdrop", "logs");

    /// <summary>OsdropService's and OsdropInput's logs (docs/BUILDING.md).</summary>
    public static string ServiceFolder { get; } = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.CommonApplicationData), "Osdrop", "logs");

    public static void Write(LogLevel level, string message)
    {
        string line = string.Create(
            CultureInfo.InvariantCulture, $"{DateTimeOffset.UtcNow:O} {level} {message}");
        _ = _lines.Writer.TryWrite(line);
    }

    /// <summary>Writes what is queued; called when the app exits.</summary>
    public static async Task FlushAsync()
    {
        _ = _lines.Writer.TryComplete();
        await _writer.ConfigureAwait(false);
    }

    private static async Task WriteLinesAsync()
    {
        await foreach (string line in _lines.Reader.ReadAllAsync().ConfigureAwait(false))
        {
            try
            {
                _ = Directory.CreateDirectory(Folder);
                string path = Path.Combine(Folder, FileName);
                var file = new FileInfo(path);
                if (file.Exists && file.Length > AppTunables.LogFileLimitBytes)
                {
                    File.Move(path, Path.Combine(Folder, _previousFileName), overwrite: true);
                }
                await File.AppendAllTextAsync(path, line + Environment.NewLine, Encoding.UTF8).ConfigureAwait(false);
            }
            catch (Exception error) when (error is IOException or UnauthorizedAccessException)
            {
                // Logging must never take the app down; a line that cannot be written is dropped.
                System.Diagnostics.Debug.WriteLine($"Osdrop log write failed: {error.Message}");
            }
        }
    }
}

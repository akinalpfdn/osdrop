using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using Osdrop.App.Diagnostics;

namespace Osdrop.App.Services;

/// <summary>
/// Text for bug reports ("Copy diagnostics"): versions, settings, state and the recent log, without
/// device names, keys or network addresses.
/// </summary>
internal sealed partial class DiagnosticsReport(
    string appVersion, string coreVersion, string system, IReadOnlyList<(string Name, string Value)> facts,
    IReadOnlyList<string> logLines, IReadOnlyList<string> privateNames)
{
    public string Text
    {
        get
        {
            var text = new StringBuilder();
            _ = text.AppendLine(CultureInfo.InvariantCulture, $"Osdrop {appVersion} (core {coreVersion})");
            _ = text.AppendLine(system).AppendLine();
            foreach ((string name, string value) in facts)
            {
                _ = text.AppendLine(CultureInfo.InvariantCulture, $"{name}: {value}");
            }
            _ = text.AppendLine().AppendLine(CultureInfo.InvariantCulture, $"Log (newest {logLines.Count} lines):");
            foreach (string line in logLines)
            {
                _ = text.AppendLine(Anonymized(line));
            }
            return text.ToString();
        }
    }

    /// <summary>Device names become placeholders and IP addresses "&lt;address&gt;".</summary>
    public string Anonymized(string line)
    {
        string result = line;
        for (int i = 0; i < privateNames.Count; i++)
        {
            if (privateNames[i].Length > 0)
            {
                result = result.Replace(privateNames[i], $"<device {i + 1}>", StringComparison.OrdinalIgnoreCase);
            }
        }
        result = Ipv4().Replace(result, "<address>");
        return Ipv6().Replace(result, "<address>");
    }

    /// <summary>The newest lines of the shell's log file (empty when there is none yet).</summary>
    public static IReadOnlyList<string> RecentLogLines(int count)
    {
        string path = Path.Combine(Log.Folder, Log.FileName);
        try
        {
            return File.Exists(path) ? [.. File.ReadLines(path).TakeLast(count)] : [];
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            return [$"(the log could not be read: {error.Message})"];
        }
    }

    [GeneratedRegex(@"\b\d{1,3}(\.\d{1,3}){3}\b")]
    private static partial Regex Ipv4();

    [GeneratedRegex(@"\b[0-9A-Fa-f]{1,4}(:[0-9A-Fa-f]{0,4}){2,7}\b")]
    private static partial Regex Ipv6();
}

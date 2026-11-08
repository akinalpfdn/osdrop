using System.Globalization;
using CommunityToolkit.Mvvm.Input;

namespace Osdrop.App.Group;

/// <summary>
/// Copied files on their way from another computer. Until they arrive, a paste gives what the
/// clipboard had before (DEVPLAN Phase 15), so the progress shows and the copy can be cancelled.
/// </summary>
public sealed partial class ClipboardFetchViewModel
{
    private readonly GroupViewModel _group;

    internal ClipboardFetchViewModel(GroupViewModel group, string computer, ulong bytesDone, ulong bytesTotal)
    {
        _group = group;
        Message = Format(Localization.Strings.ClipboardFetching, computer);
        Percent = bytesTotal == 0 ? 0 : Math.Min(100.0 * bytesDone / bytesTotal, 100);
        Progress = Format(Localization.Strings.ClipboardFetchBytes, Size(bytesDone), Size(bytesTotal));
    }

    public string Message { get; }
    public double Percent { get; }
    public string Progress { get; }

    [RelayCommand]
    private void Cancel() => _group.CancelClipboardFetch();

    /// <summary>A byte count as Explorer shows sizes: KB, MB and GB in steps of 1024.</summary>
    internal static string Size(ulong bytes)
    {
        string[] units = [Localization.Strings.UnitBytes, Localization.Strings.UnitKilobytes,
            Localization.Strings.UnitMegabytes, Localization.Strings.UnitGigabytes];
        double value = bytes;
        int unit = 0;
        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }
        string number = unit == 0 ? value.ToString("0", CultureInfo.CurrentCulture) : value.ToString("0.#", CultureInfo.CurrentCulture);
        return Format(units[unit], number);
    }

    private static string Format(string text, params string[] values) =>
        string.Format(CultureInfo.CurrentCulture, text, values);
}

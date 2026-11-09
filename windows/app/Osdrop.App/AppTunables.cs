namespace Osdrop.App;

/// <summary>Every tunable of the shell in one place (DEVPLAN: no buried constants).</summary>
internal static class AppTunables
{
    /// <summary>The log file is rolled over at this size; one older file is kept.</summary>
    public const long LogFileLimitBytes = 5 * 1024 * 1024;

    /// <summary>Copy diagnostics includes this many of the newest log lines.</summary>
    public const int DiagnosticsLogLines = 300;

    /// <summary>GitHub's latest release of the repository (no Osdrop server exists).</summary>
    public static readonly Uri LatestReleaseUri = new("https://api.github.com/repos/akinalpfdn/osdrop/releases/latest");

    /// <summary>Releases page opened when no release could be read.</summary>
    public static readonly Uri ReleasesPageUri = new("https://github.com/akinalpfdn/osdrop/releases");

    /// <summary>Automatic update checks happen at most this often, at launch.</summary>
    public static readonly TimeSpan UpdateCheckInterval = TimeSpan.FromDays(1);

    public static readonly TimeSpan UpdateCheckTimeout = TimeSpan.FromSeconds(15);

    /// <summary>A tray click right after the panel lost focus to it closes the panel instead of reopening it.</summary>
    public static readonly TimeSpan PanelReopenGuard = TimeSpan.FromMilliseconds(300);

    /// <summary>How long a push against an edge must last by default, and the range Settings offers (as on the Mac).</summary>
    public const uint DefaultPushMilliseconds = 1_000;

    public const double MinPushSeconds = 0.2;
    public const double MaxPushSeconds = 3.0;
    public const double PushSecondsStep = 0.1;

    /// <summary>Pointer speed on another computer, as a share of this PC's own; core takes 250 to 4000 thousandths.</summary>
    public const uint NormalPointerSpeed = 1_000;

    public const double MinPointerSpeed = 0.25;
    public const double MaxPointerSpeed = 4.0;

    /// <summary>Copied files are fetched up to this total by default (user decision, Phase 15), and the choices offered.</summary>
    public const ulong DefaultClipboardFilesLimit = 512UL * 1024 * 1024;

    public static readonly IReadOnlyList<ulong> ClipboardFilesLimits =
        [128UL * 1024 * 1024, 512UL * 1024 * 1024, 1024UL * 1024 * 1024, 4096UL * 1024 * 1024];

    /// <summary>The longest name other computers show, in UTF-8 bytes (core's limit).</summary>
    public const int MaxDeviceNameBytes = 64;

    /// <summary>The service that keeps OsdropInput running (windows/service).</summary>
    public const string InputServiceName = "OsdropService";

    public const string InputServiceExecutable = "OsdropService.exe";
}

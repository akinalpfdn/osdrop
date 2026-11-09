using System.Net.Http;
using System.Net.Http.Headers;
using System.Reflection;
using System.Text.Json;
using Osdrop.App.Diagnostics;
using Osdrop.App.Interop;

namespace Osdrop.App.Services;

/// <summary>A newer release on GitHub.</summary>
internal sealed record AvailableUpdate(Version Version, Uri Page);

/// <summary>The outcome of one check.</summary>
internal enum UpdateCheckOutcome
{
    UpToDate,
    UpdateAvailable,
    Failed,
}

/// <summary>Asks GitHub Releases for the latest version (Osdrop has no server of its own).</summary>
internal static class UpdateChecker
{
    /// <summary>Major.minor.patch, comparable with release tags ("v0.2.0").</summary>
    public static Version CurrentVersion { get; } = Normalize(Assembly.GetExecutingAssembly().GetName().Version);

    private static Version Normalize(Version? version) =>
        version is null ? new Version(0, 0, 0) : new Version(version.Major, version.Minor, Math.Max(0, version.Build));

    public static async Task<(UpdateCheckOutcome Outcome, AvailableUpdate? Update)> CheckAsync()
    {
        try
        {
            using var client = new HttpClient { Timeout = AppTunables.UpdateCheckTimeout };
            client.DefaultRequestHeaders.UserAgent.Add(new ProductInfoHeaderValue("Osdrop", CurrentVersion.ToString(3)));
            client.DefaultRequestHeaders.Accept.Add(new MediaTypeWithQualityHeaderValue("application/vnd.github+json"));
            using HttpResponseMessage response = await client.GetAsync(AppTunables.LatestReleaseUri).ConfigureAwait(true);
            if (response.StatusCode == System.Net.HttpStatusCode.NotFound)
            {
                return (UpdateCheckOutcome.UpToDate, null); // nothing released yet
            }
            _ = response.EnsureSuccessStatusCode();
            using var release = JsonDocument.Parse(
                await response.Content.ReadAsStringAsync().ConfigureAwait(true));
            string tag = release.RootElement.GetProperty("tag_name").GetString() ?? string.Empty;
            string page = release.RootElement.GetProperty("html_url").GetString() ?? string.Empty;
            if (!Version.TryParse(tag.TrimStart('v', 'V'), out Version? latest) ||
                !Uri.TryCreate(page, UriKind.Absolute, out Uri? pageUri))
            {
                return (UpdateCheckOutcome.Failed, null);
            }
            return latest > CurrentVersion
                ? (UpdateCheckOutcome.UpdateAvailable, new AvailableUpdate(latest, pageUri))
                : (UpdateCheckOutcome.UpToDate, null);
        }
        catch (Exception error) when (error is HttpRequestException or TaskCanceledException or JsonException
            or KeyNotFoundException or InvalidOperationException)
        {
            Log.Write(LogLevel.Warning, $"The update check failed: {error.Message}");
            return (UpdateCheckOutcome.Failed, null);
        }
    }
}

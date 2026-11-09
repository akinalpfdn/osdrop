using System.IO;
using System.Text.Json;
using CommunityToolkit.Mvvm.ComponentModel;
using Osdrop.App.Diagnostics;
using Osdrop.App.Engine;
using Osdrop.App.Interop;

namespace Osdrop.App.Settings;

/// <summary>
/// What the user chose, saved to %APPDATA%\Osdrop\settings.json on every change (the identity, the
/// group and the arrangement are the engine's, sealed with DPAPI next to it).
/// </summary>
public sealed partial class AppSettings : ObservableObject
{
    private static readonly JsonSerializerOptions _json = new() { WriteIndented = true };

    private readonly string? _path;
    private bool _loading;

    private AppSettings(string? path) => _path = path;

    /// <summary>Settings that are never saved, for tests.</summary>
    internal static AppSettings InMemory() => new(null);

    /// <summary>The name other computers see; null: this PC's own name.</summary>
    [ObservableProperty]
    public partial string? DeviceName { get; set; }

    [ObservableProperty]
    public partial bool ChecksForUpdates { get; set; } = true;

    [ObservableProperty]
    public partial DateTimeOffset? LastUpdateCheck { get; set; }

    /// <summary>Copies are shared with members whose clipboard permission is on.</summary>
    [ObservableProperty]
    public partial bool SharesClipboard { get; set; } = true;

    /// <summary>Files copied on another computer are fetched up to this total.</summary>
    [ObservableProperty]
    public partial ulong ClipboardFilesLimit { get; set; } = AppTunables.DefaultClipboardFilesLimit;

    [ObservableProperty]
    public partial ModifierMapping ModifierMapping { get; set; } = ModifierMapping.Shortcuts;

    /// <summary>Cross as soon as the cursor pushes past an edge.</summary>
    [ObservableProperty]
    public partial bool CrossesImmediately { get; set; }

    /// <summary>Enter modifier bits; the cursor crosses only while one is held. 0: always.</summary>
    [ObservableProperty]
    public partial byte CrossingModifiers { get; set; }

    [ObservableProperty]
    public partial uint PushMilliseconds { get; set; } = AppTunables.DefaultPushMilliseconds;

    /// <summary>Pointer speed on each member (hex device ID), in thousandths of this PC's own.</summary>
    [ObservableProperty]
    public partial IReadOnlyDictionary<string, uint> PointerSpeeds { get; set; } = new Dictionary<string, uint>();

    internal InputSettings Input => new(ModifierMapping, CrossesImmediately, CrossingModifiers, PushMilliseconds);

    internal EngineOptions EngineOptions => new(DeviceName, SharesClipboard, ClipboardFilesLimit, Input);

    public static AppSettings Load()
    {
        string folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "Osdrop");
        return Load(Path.Combine(folder, "settings.json"));
    }

    internal static AppSettings Load(string path)
    {
        var settings = new AppSettings(path);
        try
        {
            if (File.Exists(path))
            {
                Stored? stored = JsonSerializer.Deserialize<Stored>(File.ReadAllText(path), _json);
                if (stored is not null)
                {
                    settings.Apply(stored);
                }
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException or JsonException)
        {
            Log.Write(LogLevel.Warning, $"Settings could not be read, using defaults: {error.Message}");
        }
        return settings;
    }

    /// <summary>The pointer speed on a member, in thousandths of this PC's own.</summary>
    public uint PointerSpeed(string deviceId) =>
        PointerSpeeds.TryGetValue(deviceId, out uint permille) ? permille : AppTunables.NormalPointerSpeed;

    public void SetPointerSpeed(string deviceId, uint permille) =>
        PointerSpeeds = new Dictionary<string, uint>(PointerSpeeds) { [deviceId] = permille };

    protected override void OnPropertyChanged(System.ComponentModel.PropertyChangedEventArgs e)
    {
        base.OnPropertyChanged(e);
        if (!_loading)
        {
            Save();
        }
    }

    private void Apply(Stored stored)
    {
        _loading = true;
        try
        {
            DeviceName = string.IsNullOrWhiteSpace(stored.DeviceName) ? null : stored.DeviceName;
            ChecksForUpdates = stored.ChecksForUpdates;
            LastUpdateCheck = stored.LastUpdateCheck;
            SharesClipboard = stored.SharesClipboard;
            // A damaged or older value falls back to the default.
            ClipboardFilesLimit = AppTunables.ClipboardFilesLimits.Contains(stored.ClipboardFilesLimit)
                ? stored.ClipboardFilesLimit : AppTunables.DefaultClipboardFilesLimit;
            ModifierMapping = Enum.IsDefined(stored.ModifierMapping) ? stored.ModifierMapping : ModifierMapping.Shortcuts;
            CrossesImmediately = stored.CrossesImmediately;
            CrossingModifiers = stored.CrossingModifiers;
            PushMilliseconds = Math.Clamp(
                stored.PushMilliseconds, (uint)(AppTunables.MinPushSeconds * 1000), (uint)(AppTunables.MaxPushSeconds * 1000));
            PointerSpeeds = (stored.PointerSpeeds ?? [])
                .Where(entry => entry.Value is >= 250 and <= 4000 && entry.Key.Length == NativeSizes.DeviceId * 2)
                .ToDictionary(entry => entry.Key, entry => entry.Value);
        }
        finally
        {
            _loading = false;
        }
    }

    private void Save()
    {
        if (_path is null)
        {
            return;
        }
        var stored = new Stored(
            DeviceName, ChecksForUpdates, LastUpdateCheck, SharesClipboard, ClipboardFilesLimit, ModifierMapping,
            CrossesImmediately, CrossingModifiers, PushMilliseconds, new Dictionary<string, uint>(PointerSpeeds));
        try
        {
            _ = Directory.CreateDirectory(Path.GetDirectoryName(_path)!);
            File.WriteAllText(_path, JsonSerializer.Serialize(stored, _json));
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            Log.Write(LogLevel.Warning, $"Settings could not be saved: {error.Message}");
        }
    }

    private sealed record Stored(
        string? DeviceName = null,
        bool ChecksForUpdates = true,
        DateTimeOffset? LastUpdateCheck = null,
        bool SharesClipboard = true,
        ulong ClipboardFilesLimit = AppTunables.DefaultClipboardFilesLimit,
        ModifierMapping ModifierMapping = ModifierMapping.Shortcuts,
        bool CrossesImmediately = false,
        byte CrossingModifiers = 0,
        uint PushMilliseconds = AppTunables.DefaultPushMilliseconds,
        Dictionary<string, uint>? PointerSpeeds = null);
}

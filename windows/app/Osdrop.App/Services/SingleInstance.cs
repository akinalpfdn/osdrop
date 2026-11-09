using System.Runtime.InteropServices;

namespace Osdrop.App.Services;

/// <summary>
/// One Osdrop per user session: a second start (Start menu, the Run key, a double click) asks the
/// running one to open its panel and exits, instead of adding a second tray icon and engine.
/// </summary>
internal sealed partial class SingleInstance : IDisposable
{
    // Session-local names: other users signed in on the same PC run their own Osdrop.
    private const string _mutexName = "com.akinalpfdn.osdrop.app";
    private const string _showEventName = "com.akinalpfdn.osdrop.show-panel";
    private const int _anyProcess = -1; // ASFW_ANY

    private readonly Mutex _mutex;
    private readonly EventWaitHandle _showRequest;
    private RegisteredWaitHandle? _registration;

    private SingleInstance(Mutex mutex, EventWaitHandle showRequest)
    {
        _mutex = mutex;
        _showRequest = showRequest;
    }

    /// <summary>
    /// Returns the claim for this process, or null when Osdrop already runs in this session; the
    /// running one is then asked to show its panel.
    /// </summary>
    public static SingleInstance? Claim()
    {
        var mutex = new Mutex(initiallyOwned: true, _mutexName, out bool created);
        if (!created)
        {
            mutex.Dispose();
            if (EventWaitHandle.TryOpenExisting(_showEventName, out EventWaitHandle? showRequest))
            {
                using (showRequest)
                {
                    // This start came from the user, so it may hand the foreground to the running one.
                    _ = AllowSetForegroundWindow(_anyProcess);
                    _ = showRequest.Set();
                }
            }
            return null;
        }
        return new SingleInstance(mutex, new EventWaitHandle(false, EventResetMode.AutoReset, _showEventName));
    }

    /// <summary>Calls <paramref name="show"/> on a thread-pool thread whenever another start asks.</summary>
    public void OnShowRequested(Action show) =>
        _registration = ThreadPool.RegisterWaitForSingleObject(
            _showRequest, (_, _) => show(), null, Timeout.Infinite, executeOnlyOnce: false);

    public void Dispose()
    {
        _ = _registration?.Unregister(null);
        _showRequest.Dispose();
        // Closing the only handle removes the mutex; releasing it would have to happen on the thread
        // that created it.
        _mutex.Dispose();
    }

    [LibraryImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool AllowSetForegroundWindow(int processId);
}

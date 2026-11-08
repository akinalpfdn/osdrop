using Osdrop.App.Interop;

namespace Osdrop.App.Engine;

/// <summary>
/// Runs every call into the engine, one after another, on thread pool threads: the UI thread never
/// waits for native code (DEVPLAN), and calls into the engine never overlap.
/// </summary>
internal static class Native
{
    private static readonly Lock _lock = new();
    private static Task _tail = Task.CompletedTask;

    public static Task<T> Run<T>(Func<T> call)
    {
        lock (_lock)
        {
            Task<T> next = _tail.ContinueWith(
                _ => call(), CancellationToken.None, TaskContinuationOptions.None, TaskScheduler.Default);
            _tail = next;
            return next;
        }
    }

    public static Task Run(Action call) => Run(() =>
    {
        call();
        return true;
    });

    /// <summary>Whether the engine library loads; false when it is missing or damaged.</summary>
    public static Task<bool> IsAvailableAsync() => Run(() =>
    {
        try
        {
            _ = NativeMethods.CoreVersion();
            return true;
        }
        catch (Exception error) when (error is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException)
        {
            System.Diagnostics.Trace.TraceError($"The engine could not be loaded: {error.Message}");
            return false;
        }
    });
}

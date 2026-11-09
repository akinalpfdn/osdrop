using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using Osdrop.App.Diagnostics;
using Osdrop.App.Interop;

namespace Osdrop.App.Services;

/// <summary>OsdropService as Windows reports it.</summary>
public enum InputServiceState
{
    NotInstalled,
    Stopped,
    /// <summary>Starting, running, or stopping: it may come back by itself.</summary>
    Running,
    /// <summary>Windows would not say (the query failed).</summary>
    Unknown,
}

/// <summary>
/// The service that keeps OsdropInput running in this session (DEVPLAN Phase 3), which the engine
/// needs to watch this PC's input and to inject other computers'. Repairing runs the service's own
/// installer from Osdrop's folder, elevated: it installs the service, points it at this folder and
/// starts it.
/// </summary>
internal static partial class InputService
{
    private const uint _scManagerConnect = 0x0001;
    private const uint _serviceQueryStatus = 0x0004;
    private const int _serviceDoesNotExist = 1060;
    private const uint _serviceStopped = 1;
    private const int _cancelledByUser = 1223; // ERROR_CANCELLED: the user said no to the prompt

    public static Task<InputServiceState> QueryAsync() => Task.Run(Query);

    /// <summary>
    /// Asks for administrator rights and installs or restarts the service; false when it could not
    /// start (the user declined, or OsdropService.exe is missing).
    /// </summary>
    public static async Task<bool> RepairAsync()
    {
        string installer = Path.Combine(AppContext.BaseDirectory, AppTunables.InputServiceExecutable);
        if (!File.Exists(installer))
        {
            Log.Write(LogLevel.Error, $"{AppTunables.InputServiceExecutable} is missing next to Osdrop.exe");
            return false;
        }
        try
        {
            using var process = Process.Start(new ProcessStartInfo(installer, "--install")
            {
                UseShellExecute = true,
                Verb = "runas",
                WindowStyle = ProcessWindowStyle.Hidden,
            });
            if (process is null)
            {
                return false;
            }
            await process.WaitForExitAsync().ConfigureAwait(true);
            Log.Write(LogLevel.Info, $"The input service installer exited with {process.ExitCode}");
            return process.ExitCode == 0;
        }
        catch (Win32Exception error) when (error.NativeErrorCode == _cancelledByUser)
        {
            return false;
        }
        catch (Exception error) when (error is Win32Exception or InvalidOperationException)
        {
            Log.Write(LogLevel.Warning, $"The input service could not be repaired: {error.Message}");
            return false;
        }
    }

    private static InputServiceState Query()
    {
        nint manager = OpenSCManagerW(null, null, _scManagerConnect);
        if (manager == 0)
        {
            return InputServiceState.Unknown;
        }
        try
        {
            nint service = OpenServiceW(manager, AppTunables.InputServiceName, _serviceQueryStatus);
            if (service == 0)
            {
                return Marshal.GetLastPInvokeError() == _serviceDoesNotExist
                    ? InputServiceState.NotInstalled : InputServiceState.Unknown;
            }
            try
            {
                if (!QueryServiceStatus(service, out ServiceStatus status))
                {
                    return InputServiceState.Unknown;
                }
                return status.CurrentState == _serviceStopped ? InputServiceState.Stopped : InputServiceState.Running;
            }
            finally
            {
                _ = CloseServiceHandle(service);
            }
        }
        finally
        {
            _ = CloseServiceHandle(manager);
        }
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ServiceStatus
    {
        public uint ServiceType;
        public uint CurrentState;
        public uint ControlsAccepted;
        public uint Win32ExitCode;
        public uint ServiceSpecificExitCode;
        public uint CheckPoint;
        public uint WaitHint;
    }

    [LibraryImport("advapi32.dll", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint OpenSCManagerW(string? machineName, string? databaseName, uint desiredAccess);

    [LibraryImport("advapi32.dll", SetLastError = true, StringMarshalling = StringMarshalling.Utf16)]
    private static partial nint OpenServiceW(nint manager, string serviceName, uint desiredAccess);

    [LibraryImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool QueryServiceStatus(nint service, out ServiceStatus status);

    [LibraryImport("advapi32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static partial bool CloseServiceHandle(nint handle);
}

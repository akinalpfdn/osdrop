using System.ComponentModel;
using System.Windows;
using System.Windows.Threading;
using Osdrop.App.Engine;
using Osdrop.App.Group;
using Osdrop.App.Interop;
using Osdrop.App.Settings;
using Osdrop.App.Transfers;

namespace Osdrop.App.Corners;

/// <summary>
/// The phones' boxes in screen corners (DEVPLAN Phase 21; the Mac's CornerBoxController): each connected phone's
/// corner takes drags, and its box shows once a drag reaches the corner; files dropped there go to the phone.
/// Files a phone sends open its box by themselves, to be dragged out. The engine owns the corners' targets and
/// places the boxes in physical pixels; this tells it which phones, where, and which boxes show.
/// </summary>
internal sealed class CornerBoxController
{
    private readonly IGroupEngine _engine;
    private readonly GroupViewModel _group;
    private readonly AppSettings _settings;
    private readonly TransfersViewModel _transfers;
    private readonly Action<string, string> _notify;
    private readonly ushort _zone;
    private readonly ushort _inset;
    private readonly Dispatcher _dispatcher = Dispatcher.CurrentDispatcher;
    private readonly Dictionary<string, (CornerBoxViewModel Box, CornerBoxWindow Window)> _boxes = [];
    /// <summary>A file is being dragged out of a box: no corner takes it.</summary>
    private bool _draggingOut;
    private bool _pushQueued;

    /// <summary>
    /// <paramref name="zone"/>: the corner's side, and <paramref name="inset"/>: the box's distance from the screen's
    /// sides, in device-independent pixels.
    /// </summary>
    internal CornerBoxController(
        IGroupEngine engine, GroupViewModel group, AppSettings settings, TransfersViewModel transfers,
        Action<string, string> notify, ushort zone, ushort inset)
    {
        _engine = engine;
        _group = group;
        _settings = settings;
        _transfers = transfers;
        _notify = notify;
        _zone = zone;
        _inset = inset;
        // After the transfers list subscribed: a received item is listed before its box opens.
        _engine.EventReceived += OnEvent;
        // Also after the engine restarted with a new name: the group is listed again.
        _group.Members.CollectionChanged += (_, _) => MembersChanged();
        _settings.PropertyChanged += OnSettingsChanged;
    }

    private IEnumerable<MemberItem> ConnectedPhones => _group.Members.Where(member => member.IsPhone && member.IsConnected);

    private void OnEvent(EngineEvent received)
    {
        switch (received.Kind)
        {
            case EngineEventKind.CornerEntered when !_draggingOut:
                Box(received.DeviceId)?.Box.Entered();
                break;
            case EngineEventKind.CornerLeft:
                if (_boxes.TryGetValue(received.DeviceId, out var left))
                {
                    left.Box.Left();
                }
                break;
            case EngineEventKind.CornerDropped:
                Box(received.DeviceId)?.Box.Open();
                if (received.Reason != CoreResult.Ok)
                {
                    _notify(Localization.Strings.TransfersNotifyNotSent, _group.NameOf(received.DeviceId));
                }
                break;
            case EngineEventKind.TransferReceiving when IsPhone(received.DeviceId):
                Box(received.DeviceId)?.Box.Open(TransferKey.Receive(received.Connection));
                break;
            default:
                break;
        }
    }

    private bool IsPhone(string deviceId) => _group.Members.Any(member => member.DeviceId == deviceId && member.IsPhone);

    private void OnSettingsChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (e.PropertyName == nameof(AppSettings.PhoneCorners))
        {
            QueuePush();
        }
    }

    /// <summary>A phone that went away takes no drag; its open box stays with what it listed.</summary>
    private void MembersChanged()
    {
        HashSet<string> connected = [.. ConnectedPhones.Select(phone => phone.DeviceId)];
        foreach ((string deviceId, var entry) in _boxes)
        {
            if (!connected.Contains(deviceId))
            {
                entry.Box.Left();
            }
        }
        QueuePush();
    }

    private (CornerBoxViewModel Box, CornerBoxWindow Window)? Box(string deviceId)
    {
        if (_boxes.TryGetValue(deviceId, out var entry))
        {
            return entry;
        }
        if (!IsPhone(deviceId))
        {
            return null;
        }
        var box = new CornerBoxViewModel(deviceId, () => _group.NameOf(deviceId), _transfers);
        var window = new CornerBoxWindow(box);
        entry = (box, window);
        _boxes[deviceId] = entry;
        box.PropertyChanged += (_, e) =>
        {
            if (e.PropertyName == nameof(CornerBoxViewModel.Mode))
            {
                Apply(box, window);
            }
        };
        window.SizeChanged += (_, _) => QueuePush();
        window.LocationChanged += (_, _) => QueuePush();
        window.DraggingOut += dragging =>
        {
            _draggingOut = dragging;
            Push(); // at once: the corners would show for the held button
        };
        return entry;
    }

    private void Apply(CornerBoxViewModel box, CornerBoxWindow window)
    {
        if (box.Mode == CornerBoxMode.Hidden)
        {
            window.Hide();
        }
        else
        {
            window.LetsDragsThrough(box.Mode == CornerBoxMode.Hovering);
            if (!window.IsVisible)
            {
                Guess(window);
                window.Show();
            }
        }
        Push();
    }

    /// <summary>
    /// Where the box most likely goes, before the engine places it: the main screen's corner, as WPF measures it.
    /// </summary>
    private void Guess(CornerBoxWindow window)
    {
        Rect area = SystemParameters.WorkArea;
        ScreenCorner corner = _settings.Corner(_boxes.First(entry => entry.Value.Window == window).Key).Corner;
        bool left = corner is ScreenCorner.TopLeft or ScreenCorner.BottomLeft;
        bool top = corner is ScreenCorner.TopLeft or ScreenCorner.TopRight;
        window.Left = left ? area.Left + _inset : area.Right - _inset - window.Width;
        window.Top = top ? area.Top + _inset : area.Bottom - _inset - window.ActualHeight;
    }

    /// <summary>Once, after the changes in progress (a group refresh lists every member again).</summary>
    private void QueuePush()
    {
        if (_pushQueued)
        {
            return;
        }
        _pushQueued = true;
        _ = _dispatcher.BeginInvoke(DispatcherPriority.Background, () =>
        {
            _pushQueued = false;
            Push();
        });
    }

    private void Push()
    {
        List<CornerPlacement> corners = _draggingOut ? [] : [.. ConnectedPhones.Select(phone =>
        {
            PhoneCorner chosen = _settings.Corner(phone.DeviceId);
            nint box = _boxes.TryGetValue(phone.DeviceId, out var entry) && entry.Window.IsVisible ? entry.Window.Handle : 0;
            return new CornerPlacement(phone.DeviceId, chosen.Screen, chosen.Corner, _zone, _inset, box);
        })];
        _ = _engine.SetCornersAsync(corners);
    }
}

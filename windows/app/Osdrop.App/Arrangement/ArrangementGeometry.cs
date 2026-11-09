using System.Windows;
using Osdrop.App.Engine;

namespace Osdrop.App.Arrangement;

/// <summary>A computer in the editor: its monitors where its system put them, moved as a whole.</summary>
public sealed record ArrangementComputer(
    string DeviceId, string Name, bool IsThisPc, bool IsConnected, Point Offset, IReadOnlyList<Rect> Monitors)
{
    /// <summary>Its monitors where they are in the shared space.</summary>
    public IEnumerable<Rect> PlacedMonitors => Monitors.Select(monitor => Rect.Offset(monitor, (Vector)Offset));

    public Rect Bounds => PlacedMonitors.Aggregate(Rect.Empty, Rect.Union);
}

/// <summary>
/// Every member's monitors in the shared space, for the arrangement editor (the Mac's
/// ArrangementGeometry). Units are core's: 1/16 of a logical unit.
/// </summary>
public sealed class ArrangementGeometry
{
    /// <summary>The share of the shorter edge two touching computers keep in common.</summary>
    public const double SharedEdgeFraction = 0.5;

    public ArrangementGeometry(IReadOnlyList<ArrangementComputer> computers) => Computers = computers;

    /// <summary>
    /// The computers placed in the arrangement and their monitors; detached ones and computers whose
    /// screens are not known yet are left out.
    /// </summary>
    internal ArrangementGeometry(IReadOnlyList<LayoutComputer> layout, IReadOnlyList<Group.MemberItem> members)
        : this([.. layout
            .Where(computer => computer.Placement is { IsDetached: false } && computer.Monitors.Count > 0)
            .Select(computer =>
            {
                Group.MemberItem? member = members.FirstOrDefault(item => item.DeviceId == computer.DeviceId);
                return new ArrangementComputer(
                    computer.DeviceId, member?.Name ?? string.Empty, member?.IsSelf ?? false,
                    member is not null && (member.IsSelf || member.IsConnected),
                    new Point(computer.Placement!.X, computer.Placement.Y),
                    [.. computer.Monitors.Select(frame => new Rect(frame.X, frame.Y, frame.Width, frame.Height))]);
            })])
    {
    }

    public IReadOnlyList<ArrangementComputer> Computers { get; }

    /// <summary>Everything, plus room around it to drag into.</summary>
    public Rect Bounds(double marginFraction)
    {
        Rect all = Computers.Select(computer => computer.Bounds).Aggregate(Rect.Empty, Rect.Union);
        if (all.IsEmpty)
        {
            return Rect.Empty;
        }
        double margin = Math.Max(all.Width, all.Height) * marginFraction;
        all.Inflate(margin, margin);
        return all;
    }

    /// <summary>
    /// Where <paramref name="deviceId"/> lands when dropped with its origin at <paramref name="drop"/>:
    /// touching the nearest edge of another computer, with at least half of the shorter of the two
    /// edges in common, so the cursor has room to cross. Alone, it stays where dropped.
    /// </summary>
    public Point Snapped(string deviceId, Point drop)
    {
        ArrangementComputer? moving = Computers.FirstOrDefault(computer => computer.DeviceId == deviceId);
        if (moving is null)
        {
            return drop;
        }
        Rect bounds = moving.Bounds;
        Vector local = bounds.TopLeft - moving.Offset;
        var dropped = new Rect(drop + local, bounds.Size);
        Point? best = null;
        double bestDistance = double.PositiveInfinity;
        foreach (ArrangementComputer other in Computers.Where(computer => computer.DeviceId != deviceId))
        {
            Rect target = other.Bounds;
            double sharedHeight = Math.Min(dropped.Height, target.Height) * SharedEdgeFraction;
            double sharedWidth = Math.Min(dropped.Width, target.Width) * SharedEdgeFraction;
            double overlapY = Math.Clamp(dropped.Top, target.Top - dropped.Height + sharedHeight, target.Bottom - sharedHeight);
            double overlapX = Math.Clamp(dropped.Left, target.Left - dropped.Width + sharedWidth, target.Right - sharedWidth);
            Point[] candidates =
            [
                new(target.Right, overlapY), // to its right
                new(target.Left - dropped.Width, overlapY), // to its left
                new(overlapX, target.Bottom), // below it
                new(overlapX, target.Top - dropped.Height), // above it
            ];
            foreach (Point candidate in candidates)
            {
                double distance = (candidate - dropped.TopLeft).Length;
                if (distance < bestDistance)
                {
                    best = candidate;
                    bestDistance = distance;
                }
            }
        }
        return best is Point point ? point - local : drop;
    }

    /// <summary>Every placement in <paramref name="layout"/>, detached computers included, with one moved.</summary>
    internal static IReadOnlyList<Placement> Placements(IReadOnlyList<LayoutComputer> layout, string deviceId, Point offset) =>
        [.. layout
            .Where(computer => computer.Placement is not null)
            .Select(computer => computer.DeviceId == deviceId
                ? new Placement(deviceId, Round(offset.X), Round(offset.Y), IsDetached: false)
                : computer.Placement!)];

    private static int Round(double value) => (int)Math.Clamp(Math.Round(value), int.MinValue, int.MaxValue);
}

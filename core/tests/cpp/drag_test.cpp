// Drags of files across computers over the simulated network (docs/PROTOCOL.md, "Drag messages"):
// from the controller, from a computer it controls, along a chain, back home and back to the
// source; drops, cancellations, the hotkey, local input, refusals and lost computers.

#include "control_fixture.hpp"
#include "temp_folder.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace osdrop::test::control_fixture; // NOLINT(google-build-using-namespace)
using osdrop::test::create_folders;
using osdrop::test::read_file;
using osdrop::test::TempFolder;
using osdrop::test::write_file;
using protocol::DragOutcome;

constexpr std::string_view kReport = "a quarterly report";

class DragControl : public InputControl {
protected:
    void SetUp() override {
        InputControl::SetUp();
        write_file(files / "Report.pdf", kReport);
        write_file(files / "Photos" / "a.jpg", "jpeg");
        write_file(files / "Photos" / "b.jpg", "more jpeg");
    }

    /// A drag of these items begins on computer `on` and they are measured.
    void start_drag(std::size_t on, const std::vector<std::string>& names = {"Report.pdf"}) {
        std::vector<control::DraggedItem> items;
        for (const std::string& name : names) {
            items.push_back({.path = files / name,
                             .content_type = name == "Photos" ? "public.folder" : "com.adobe.pdf"});
        }
        command(on, [&](control::Control& c) { ASSERT_TRUE(c.drag_started(items).has_value()); });
    }
    /// The left button goes down on computer 0, wherever its cursor is.
    void press() { button(0, 0, ButtonAction::Down); }
    void release() { button(0, 0, ButtonAction::Up); }
    /// Computer 0's cursor, on another computer, pushes through that computer's right edge.
    void push_right() { move(0, kWidth - 1, 400, 2 * kWidth, 0); }
    void push_left() { move(0, kWidth - 1, 400, -2 * kWidth, 0); }
    void finish(std::size_t on, std::uint32_t drag_id, DragOutcome outcome) {
        command(on, [&](control::Control& c) { c.drop_finished(drag_id, outcome); });
    }
    /// The arrivals on a computer.
    const std::vector<control::DragArrival>& arrivals(std::size_t on) {
        return node(on).control_observer.arrivals;
    }
    const std::vector<std::pair<std::uint32_t, DragOutcome>>& ends(std::size_t on) {
        return node(on).control_observer.drag_ends;
    }
    /// Computer `on` fetches item `index` of drag `drag_id` from `source` into `into`; the error,
    /// if any.
    std::optional<osdrop::Error> fetch(std::size_t on, std::size_t source, std::uint32_t drag_id,
                                       std::uint32_t index, const TempFolder& into) {
        const auto started = node(on).transfers->receive(
            id(source),
            {.kind = protocol::ObjectKind::Drag, .object_id = drag_id, .item_index = index},
            into.path());
        if (!started) {
            return started.error();
        }
        node(on).mesh.flush(network.now);
        network.run_for(milliseconds(300));
        const auto& finished = node(on).transfer_observer.finished;
        if (finished.empty() || finished.back().id != started.value()) {
            return osdrop::Error::Internal;
        }
        return finished.back().error;
    }
    /// The last button event injected on a computer.
    std::optional<protocol::ButtonEvent> last_button(std::size_t on) {
        std::optional<protocol::ButtonEvent> last;
        for (const control::Injection& injection : node(on).control_observer.injected) {
            if (const auto* event = std::get_if<protocol::ButtonEvent>(&injection)) {
                last = *event;
            }
        }
        return last;
    }

    TempFolder files;
};

TEST_F(DragControl, ADragFromTheControllerIsDroppedOnTheNextComputer) {
    start_drag(0, {"Report.pdf", "Photos"});
    press();
    cross_from_0();
    EXPECT_TRUE(node(0).control_observer.began.empty()) << "the first push finds the drag";
    EXPECT_TRUE(node(0).control->carrying());
    cross_from_0();
    ASSERT_EQ(node(0).control_observer.began.size(), 1U);
    ASSERT_EQ(arrivals(1).size(), 1U);
    const control::DragArrival arrival = arrivals(1).back();
    EXPECT_EQ(arrival.source, id(0));
    EXPECT_EQ(arrival.position, at(0, 400));
    ASSERT_EQ(arrival.items.size(), 2U);
    EXPECT_EQ(arrival.items[0], (protocol::OfferedItem{.kind = protocol::ItemKind::File,
                                                       .byte_count = kReport.size(),
                                                       .content_type = "com.adobe.pdf",
                                                       .name = "Report.pdf"}));
    EXPECT_EQ(arrival.items[1], (protocol::OfferedItem{.kind = protocol::ItemKind::Folder,
                                                       .byte_count = 13,
                                                       .content_type = "public.folder",
                                                       .name = "Photos"}));

    move(0, kWidth - 1, 400, 200, 50);
    release();
    ASSERT_TRUE(last_button(1).has_value());
    EXPECT_EQ(last_button(1)->action, ButtonAction::Up) << "the release drops it where it is";
    EXPECT_EQ(last_button(1)->x, at(200, 450).x);
    EXPECT_FALSE(node(0).control->carrying());
    EXPECT_TRUE(ends(0).empty()) << "the source waits for the drop's outcome";

    finish(1, arrival.drag_id, DragOutcome::Dropped);
    ASSERT_EQ(ends(0).size(), 1U);
    EXPECT_EQ(ends(0).back(), std::pair(arrival.drag_id, DragOutcome::Dropped));
    command(0, [](control::Control& c) { c.drag_stopped(); });

    // The drop fetches the items, after the source's drag has ended.
    const TempFolder received;
    EXPECT_EQ(fetch(1, 0, arrival.drag_id, 0, received), std::nullopt);
    EXPECT_EQ(fetch(1, 0, arrival.drag_id, 1, received), std::nullopt);
    EXPECT_EQ(read_file(received / "Report.pdf").size(), kReport.size());
    EXPECT_EQ(read_file(received / "Photos" / "b.jpg").size(), 9U);
}

TEST_F(DragControl, ADropThatTakesNothingSlidesTheDragBackAndWithdrawsIt) {
    start_drag(0);
    press();
    cross_from_0();
    cross_from_0();
    ASSERT_EQ(arrivals(1).size(), 1U);
    const std::uint32_t drag = arrivals(1).back().drag_id;
    release();
    finish(1, drag, DragOutcome::Cancelled);
    ASSERT_EQ(ends(0).size(), 1U);
    EXPECT_EQ(ends(0).back().second, DragOutcome::Cancelled);
    command(0, [](control::Control& c) { c.drag_stopped(); });
    const TempFolder received;
    EXPECT_NE(fetch(1, 0, drag, 0, received), std::nullopt);
}

TEST_F(DragControl, AParkedDragStaysOfferedUntilItsOutcome) {
    start_drag(0);
    press();
    cross_from_0();
    cross_from_0();
    const std::uint32_t drag = arrivals(1).back().drag_id;
    // The Mac may see its button released when the release went to computer 1.
    command(0, [](control::Control& c) { c.drag_stopped(); });
    release();
    finish(1, drag, DragOutcome::Dropped);
    ASSERT_EQ(ends(0).size(), 1U) << "the edge still ends the drag";
    const TempFolder received;
    EXPECT_EQ(fetch(1, 0, drag, 0, received), std::nullopt);
}

TEST_F(DragControl, AParkedDragThatEndedIsWithdrawnAfterACancellation) {
    start_drag(0);
    press();
    cross_from_0();
    cross_from_0();
    const std::uint32_t drag = arrivals(1).back().drag_id;
    command(0, [](control::Control& c) { c.drag_stopped(); });
    release();
    finish(1, drag, DragOutcome::Cancelled);
    ASSERT_EQ(ends(0).size(), 1U);
    const TempFolder nothing;
    EXPECT_NE(fetch(1, 0, drag, 0, nothing), std::nullopt);
}

TEST_F(DragControl, ADragOnAControlledComputerGoesToAThirdOne) {
    cross_from_0();
    press(); // on computer 1
    start_drag(1);
    push_right();
    EXPECT_TRUE(node(0).control->carrying()) << "computer 1 answered the probe with its drag";
    EXPECT_TRUE(node(2).control_observer.controlled.empty());
    push_right();
    ASSERT_EQ(arrivals(2).size(), 1U);
    const control::DragArrival arrival = arrivals(2).back();
    EXPECT_EQ(arrival.source, id(1));
    EXPECT_EQ(node(1).control_observer.controlled.back(), std::nullopt);
    EXPECT_EQ(held_after(node(1).control_observer.injected).buttons, std::set<std::uint8_t>{0})
        << "the source keeps the button down: its drag waits at the edge";

    release();
    EXPECT_EQ(last_button(2)->action, ButtonAction::Up);
    finish(2, arrival.drag_id, DragOutcome::Dropped);
    ASSERT_EQ(ends(1).size(), 1U);
    EXPECT_EQ(ends(1).back(), std::pair(arrival.drag_id, DragOutcome::Dropped));
    EXPECT_TRUE(ends(0).empty());
    const TempFolder received;
    EXPECT_EQ(fetch(2, 1, arrival.drag_id, 0, received), std::nullopt);
    EXPECT_EQ(read_file(received / "Report.pdf").size(), kReport.size());
}

TEST_F(DragControl, ADragCrossesAChainAndOnlyTheLastComputerReports) {
    start_drag(0);
    press();
    cross_from_0();
    cross_from_0();
    ASSERT_EQ(arrivals(1).size(), 1U);
    const std::uint32_t drag = arrivals(1).back().drag_id;
    push_right();
    ASSERT_EQ(arrivals(2).size(), 1U);
    EXPECT_EQ(arrivals(2).back().source, id(0));
    EXPECT_EQ(node(1).control_observer.withdrawn, std::vector<std::uint32_t>{drag})
        << "the drag on computer 1 ends without a drop";
    EXPECT_TRUE(ends(0).empty());

    release();
    finish(2, drag, DragOutcome::Dropped);
    finish(1, drag, DragOutcome::Cancelled); // the platform's late report of the withdrawn drag
    ASSERT_EQ(ends(0).size(), 1U);
    EXPECT_EQ(ends(0).back().second, DragOutcome::Dropped);
}

TEST_F(DragControl, ADragBroughtHomeMakesTheControllerItsTarget) {
    cross_from_0();
    press();
    start_drag(1);
    push_left();
    EXPECT_TRUE(node(0).control->carrying());
    push_left();
    ASSERT_FALSE(node(0).control_observer.ended.empty());
    EXPECT_EQ(node(0).control_observer.ended.back().reason, CaptureEnd::Returned);
    ASSERT_EQ(arrivals(0).size(), 1U);
    const control::DragArrival arrival = arrivals(0).back();
    EXPECT_EQ(arrival.source, id(1));
    EXPECT_EQ(arrival.position, node(0).control_observer.ended.back().cursor);

    release();
    finish(0, arrival.drag_id, DragOutcome::Dropped);
    ASSERT_EQ(ends(1).size(), 1U);
    EXPECT_EQ(ends(1).back().second, DragOutcome::Dropped);
}

TEST_F(DragControl, ADragBackOnItsSourceGoesOnThere) {
    start_drag(0);
    press();
    cross_from_0();
    cross_from_0();
    const std::uint32_t drag = arrivals(1).back().drag_id;
    push_left();
    EXPECT_FALSE(node(0).control->capturing());
    EXPECT_EQ(arrivals(0).size(), 0U) << "the controller's own drag goes on";
    EXPECT_EQ(node(1).control_observer.withdrawn, std::vector<std::uint32_t>{drag});
    finish(1, drag, DragOutcome::Cancelled);
    EXPECT_TRUE(ends(0).empty()) << "a drag that came back has no outcome from elsewhere";

    // A controlled computer's drag, carried to a third one and back.
    command(0, [](control::Control& c) { c.drag_stopped(); });
    release();
    cross_from_0();
    press();
    start_drag(1);
    push_right();
    push_right();
    ASSERT_EQ(arrivals(2).size(), 1U);
    push_left();
    EXPECT_EQ(node(1).control_observer.controlled.back(), id(0));
    EXPECT_TRUE(arrivals(1).size() == 1U) << "no new drag on its source";
    release();
    EXPECT_EQ(last_button(1)->action, ButtonAction::Up) << "its own drag drops there";
}

TEST_F(DragControl, WithoutADragAHeldButtonIsAWall) {
    press();
    cross_from_0();
    cross_from_0();
    EXPECT_TRUE(node(0).control_observer.began.empty());
    release();

    cross_from_0();
    press();
    push_right();
    push_right();
    EXPECT_TRUE(node(2).control_observer.controlled.empty()) << "computer 1 has no drag";
    EXPECT_FALSE(node(0).control->carrying());
}

TEST_F(DragControl, TheHotkeyCancelsACarriedDrag) {
    cross_from_0();
    press();
    start_drag(1);
    push_right();
    push_right();
    ASSERT_EQ(arrivals(2).size(), 1U);
    const std::uint32_t drag = arrivals(2).back().drag_id;
    command(0, [](control::Control& c) { c.return_home(protocol::LeaveReason::ReleasedByHotkey); });
    EXPECT_EQ(node(2).control_observer.withdrawn, std::vector<std::uint32_t>{drag});
    ASSERT_EQ(ends(1).size(), 1U);
    EXPECT_EQ(ends(1).back(), std::pair(drag, DragOutcome::Cancelled));
    EXPECT_FALSE(node(0).control->carrying());
}

TEST_F(DragControl, LocalInputOnTheTargetCancelsTheDrag) {
    cross_from_0();
    press();
    start_drag(1);
    push_right();
    push_right();
    const std::uint32_t drag = arrivals(2).back().drag_id;
    key(2, kKeyA, KeyAction::Down);
    EXPECT_EQ(node(2).control_observer.withdrawn, std::vector<std::uint32_t>{drag});
    EXPECT_EQ(node(0).control_observer.ended.back().reason, CaptureEnd::Takeover);
    ASSERT_EQ(ends(1).size(), 1U) << "told twice, by the target and the controller";
    EXPECT_EQ(ends(1).back().second, DragOutcome::Cancelled);
}

TEST_F(DragControl, AProbeWaitsUntilTheItemsAreMeasured) {
    cross_from_0();
    press();
    node(1).executor.hold_files = true;
    start_drag(1);
    push_right();
    push_right();
    EXPECT_TRUE(node(2).control_observer.controlled.empty());
    node(1).executor.hold_files = false;
    network.run_for(milliseconds(50));
    EXPECT_TRUE(node(0).control->carrying());
    push_right();
    EXPECT_EQ(arrivals(2).size(), 1U);
}

TEST_F(DragControl, ATargetThatTakesNoFilesFromTheSourceRefusesTheDrag) {
    osdrop::group::Permissions no_files;
    no_files.files = false;
    ASSERT_TRUE(node(2).group->set_permissions(id(1), no_files, network.now).has_value());
    network.run_for(milliseconds(200));
    cross_from_0();
    press();
    start_drag(1);
    push_right();
    push_right();
    EXPECT_TRUE(arrivals(2).empty());
    ASSERT_FALSE(node(0).control_observer.ended.empty());
    EXPECT_EQ(node(0).control_observer.ended.back().refused, protocol::RefuseReason::NotPermitted);
    ASSERT_EQ(ends(1).size(), 1U);
    EXPECT_EQ(ends(1).back().second, DragOutcome::Cancelled);
}

TEST_F(DragControl, ALostSourceEndsTheDragOnTheTarget) {
    cross_from_0();
    press();
    start_drag(1);
    push_right();
    push_right();
    const std::uint32_t drag = arrivals(2).back().drag_id;
    node(1).frozen = true;
    network.run_for(milliseconds(10'000));
    EXPECT_EQ(node(2).control_observer.withdrawn, std::vector<std::uint32_t>{drag});
    EXPECT_FALSE(node(0).control->carrying());
}

TEST_F(DragControl, ALostControllerSlidesAParkedDragBack) {
    cross_from_0();
    press();
    start_drag(1);
    push_right();
    push_right();
    const std::uint32_t drag = arrivals(2).back().drag_id;
    node(0).frozen = true;
    network.run_for(milliseconds(10'000));
    ASSERT_FALSE(ends(1).empty());
    EXPECT_EQ(ends(1).front(), std::pair(drag, DragOutcome::Cancelled));
    EXPECT_EQ(node(2).control_observer.withdrawn, std::vector<std::uint32_t>{drag});
}

TEST_F(DragControl, ADragOfMoreItemsThanAnOfferHoldsIsRefused) {
    const std::vector<control::DraggedItem> items(
        protocol::kMaxDragItems + 1, {.path = files / "Report.pdf", .content_type = "public.data"});
    EXPECT_EQ(node(0).control->drag_started(items).error(), osdrop::Error::InvalidArgument);
    EXPECT_EQ(node(0)
                  .control->drag_started({{.path = "Report.pdf", .content_type = "public.data"}})
                  .error(),
              osdrop::Error::InvalidArgument);
}

} // namespace

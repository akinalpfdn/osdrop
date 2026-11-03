# Decisions

This file tracks all non-trivial technical decisions made during this project.
Entries are append-only. The stack itself is specified in `DEVPLAN.md`; only deviations and
choices the plan leaves open are logged here.

---

## 2026-11-02 — Repository visibility, branch flow and work tracking
**Chosen:** User decision: public GitHub repository `akinalpfdn/osdrop`, with the same care as Osplay. Every DEVPLAN phase is developed on its own branch (`feature/phase-XX-<slug>`) and lands through a pull request that the user merges once CI is green. Progress is tracked only in `.claude/phases/` and this file.
**Alternatives:** Private repository until MVP; committing to `main`; Lore work items.
**Why:** Matches the sibling project, whose flow worked; public repositories get unlimited Actions minutes; a PR per phase is a reviewable, revertable unit that matches the phase gates.
**Trade-offs:** Work is visible before it is usable; every phase needs a PR round-trip.
**Revisit if:** Outside contributors need a different integration model.

---

## 2026-11-02 — Production grade from the first phase
**Chosen:** User decision: no proof-of-concept or spike code. Platform risks (input capture and injection, drag capture and promise drags on both platforms) are retired by Phases 1–4, which build the production modules and end with a go/no-go gate.
**Alternatives:** Throwaway spikes in `spikes/` before implementation phases.
**Why:** Spike code tends to survive into the product untested; building the real modules behind interfaces gives the same answer and keeps the work.
**Trade-offs:** If a gate fails, more code is discarded than a spike would have produced.
**Revisit if:** A gate fails and the alternative approach is uncertain enough to need a cheap experiment first.

---

## 2026-11-02 — Code shared with Osplay is copied, not linked
**Chosen:** User decision: the reusable parts of Osplay's core (Noise handshake, cipher states, replay window, randomness injection, Asio transport base, control framing, utilities) are copied into Osdrop at one recorded Osplay commit in Phase 6, adapted (names, labels, prologue) and owned by Osdrop from then on. The source commit is recorded here when the copy happens; provenance is noted in `docs/THIRD_PARTY.md`.
**Alternatives:** A third repository with the shared library used by both projects as a submodule; Osplay as a submodule of Osdrop.
**Why:** The shared pieces are small and stable, and the protocols differ (video streams versus input, drags and files). A shared library would tie the two products' release cycles together and force a refactor of a shipped product; a whole-Osplay submodule would pull in an unrelated product and break on its protocol changes.
**Trade-offs:** A security fix has to be applied in both repositories; the copies can drift.
**Revisit if:** The copied code keeps needing the same changes in both projects.

---

## 2026-11-02 — Any number of computers in a free arrangement
**Chosen:** User decision: a group holds up to 8 computers placed freely in one shared arrangement (like a display arrangement), in every direction (Mac ↔ PC, Mac ↔ Mac, PC ↔ PC). Whichever computer's mouse and keyboard are in use drive the others.
**Alternatives:** Exactly two computers.
**Why:** Covers every desk the user has (two Macs, two PCs) and avoids a later protocol and UI change for a third computer.
**Trade-offs:** Needs group membership, a replicated arrangement and a full mesh of sessions from the start.
**Revisit if:** Real use shows groups larger than 8.

---

## 2026-11-02 — First-release scope
**Chosen:** User decision: the first release has cursor and keyboard crossing, file and folder drag and drop, a shared clipboard for text and images, and file copy-paste between computers. Dragging text selections and images, and control of the Windows lock screen and UAC prompts, are post-MVP.
**Alternatives:** Including text and image dragging and secure-desktop control in the first release.
**Why:** Copy-paste of text, images and files is what users expect next to dragging; the other two add platform work (rich drag formats, a secure-desktop input path) that the first release can do without.
**Trade-offs:** A locked PC cannot be unlocked from another computer's keyboard in the first release.
**Revisit if:** Users ask for the deferred items.

---

## 2026-11-02 — Windows input runs as SYSTEM in the user's session
**Chosen:** `OsdropService` (LocalSystem) launches `OsdropInput.exe` with a SYSTEM token into the active console session; only hooks, cursor parking and injection run there. Networking, protocol parsing, files, clipboard and drag and drop stay in the user's medium-integrity app, which talks to the input process over a per-session pipe (ACL: session user and SYSTEM; client image path verified; injection accepted only during active control).
**Alternatives:** Everything in the user's process (no control over elevated windows such as Task Manager, admin terminals and installers, and capture fails while one is focused); `uiAccess` (requires an Authenticode-signed binary, which the project does not have); the whole engine as SYSTEM.
**Why:** UIPI blocks both hooks and injection for elevated windows from a medium-integrity process; a production tool has to work over them. Keeping the network-facing code at medium integrity means a protocol bug never runs as SYSTEM, and OLE drag and drop only works between processes of the same integrity. The same service later carries secure-desktop support.
**Trade-offs:** A privileged component and an install-time service; malware already running as the user could try to drive elevated windows through the pipe (mitigated, documented in `docs/SECURITY_MODEL.md`).
**Revisit if:** A code-signing certificate allows `uiAccess` instead.

---

## 2026-11-02 — Crossing by pushing through the edge
**Chosen:** User decision (from the product description): the cursor crosses after being held at an edge and pushed for about one second; the push time is a setting, and immediate crossing is offered.
**Alternatives:** Immediate crossing as the default; crossing only with a modifier key.
**Why:** Prevents accidental crossings when aiming at an edge or a corner, as Universal Control does.
**Trade-offs:** Deliberate crossings take a moment; the default is tuned in Phase 12 on real hardware.
**Revisit if:** The default feels slow in daily use.

---

## 2026-11-02 — Branch and tag protection
**Chosen:** As in Osplay: ruleset "main protection" blocks deletion, force push and any direct update of `main`, and requires a pull request with green `macOS`, `macOS sanitizers` and `Windows` checks; ruleset "release tag protection" applies the same to creating, moving and deleting `v*` tags. The repository admin role (only `akinalpfdn`) bypasses both.
**Alternatives:** Legacy branch protection; no bypass; requiring only the `macOS` and `Windows` checks as Osplay does.
**Why:** Outside contributors work through forks and PRs; merging stays with the owner. The sanitizer job exists from the first phase here, so it is required too.
**Trade-offs:** The bypass also applies to tools acting with the owner's credentials, so PR-per-phase stays a working rule rather than something GitHub enforces against the owner.
**Revisit if:** Another person gets write access, or releases need technically immutable tags.

---

## 2026-11-02 — User decision: a guard process gives the cursor back when Osdrop hangs
**Chosen:** `OsdropCaptureGuard`, a Foundation-only tool embedded in `Contents/MacOS`, is started with the event tap and reads one-byte messages on its standard input: arm when capture starts, a heartbeat on every healthy watchdog check (every 100 ms), disarm when capture ends. Armed and silent for 2 seconds, it sends SIGKILL to Osdrop; closing Osdrop's window server connection makes macOS show and release the cursor. Capture refuses to start when the guard cannot run. The in-app watchdog also ends a capture when the event tap stays disabled for three checks.
**Alternatives:** Documenting Force Quit (⌥⌘⎋) as the way out; deferring the problem to Phase 18.
**Why:** Phase 1 measured it: with Osdrop stopped (`SIGSTOP`) during capture, the cursor stayed hidden and frozen for as long as the app was stopped, because the freeze and the hidden state belong to Osdrop's own connection. The in-app watchdog cannot help when every thread is stuck. Leaving the user without a mouse breaks the input safety invariants, and a Force Quit escape depends on the keyboard still working and on the user knowing it.
**Trade-offs:** One more process; a hung Osdrop is ended instead of waited for (it was not answering anyway). The guard's timeout must stay above the in-app watchdog's, so a hung main thread is still handled inside the app first.
**Revisit if:** macOS offers a way to freeze and hide the cursor that ends with the process's responsiveness rather than its connection.

---

## 2026-11-02 — App Nap is held off while the event tap runs
**Chosen:** `EventTapCapture` holds a `userInitiatedAllowingIdleSystemSleep` + `latencyCritical` activity from `start` to `stop`; the guard runs at user-interactive quality of service.
**Alternatives:** Relying on the app being frontmost during capture.
**Why:** A guard test in the windowless test host took 5 min 49 s instead of 2.8 s: App Nap delayed its timers by minutes. In Osdrop the same delay would make the watchdog late and could let the guard end a healthy app. Capture cannot rely on Osdrop being frontmost, because macOS 14 may refuse to activate it.
**Trade-offs:** Slightly higher energy use while the tap runs; system sleep is still allowed.
**Revisit if:** Input observation moves to a component that App Nap does not apply to.

---

## 2026-11-03 — User decision: core phases before the Windows gates
**Chosen:** With the Windows PC away, Phases 5–9 (protocol, crypto, sessions and mesh, group membership, layout and crossing) run before Phases 3 and 4 (Windows input process and drag bridge). Phases 3 and 4 run when the PC is back, and both must be done before Phase 10.
**Alternatives:** Writing Phase 3 as code only and leaving its hardware gate open; pausing until the PC is back.
**Why:** Phases 5–9 live in `core` and are verified completely on the Mac and in CI; none depends on how Windows captures input. Writing Windows input code without its gate would pile up work that a failed gate could invalidate.
**Trade-offs:** The Windows go/no-go gates are answered later, so a Windows design change would be found after the core is built (the core does not depend on it).
**Revisit if:** The PC comes back before Phase 9 ends; Phase 3 can then start at the next phase boundary.

---

## 2026-11-03 — Protocol code copied from Osplay
**Chosen:** The codec scaffolding is copied from Osplay commit `35046970f99c145d1b3e549d2362897c114af733` (its `main` on 2026-10-31) and adapted to the `osdrop` namespace: `util/expected.hpp`, `protocol/byte_io.hpp` (with `i64` added), the UTF-8 rules in `protocol/text.cpp`, the codec layout (one struct per message, shared validation for encoder and decoder), the fixture manifest format and generator layout, the fuzz replay runner and the C API's validate/reencode pair. Messages, fields and rules are Osdrop's own.
**Alternatives:** Writing the scaffolding anew; sharing it with Osplay as a library.
**Why:** The copied pieces are small, reviewed and fuzzed in Osplay; the user chose copying with provenance over linking (2026-11-02).
**Trade-offs:** A bug found in these files must be fixed in both projects.
**Revisit if:** Either project changes these files in a way the other should follow.

---

## 2026-11-03 — Coordinates are bounded to ±2^28
**Chosen:** Every `coord` on the wire lies in −2^28…2^28 (1/16 point units, ±16,777,216 points); decoders reject values outside it.
**Alternatives:** Accepting the whole `i32` range and widening every geometric computation to 64 bits.
**Why:** Layout code adds an arrangement offset, a monitor origin and a size. With the bound, any sum of three coordinates fits an `i32`, so crossing and layout code (Phase 9) cannot overflow on hostile or corrupted input, and the bound is far beyond any real desktop.
**Trade-offs:** None in practice; a desktop larger than 16 million points cannot be described.
**Revisit if:** Never expected.

---

## 2026-11-03 — Symbolic links travel as link entries
**Chosen:** A folder's manifest lists links as their own entry kind; the link's content is its target path (1–4096 bytes), fetched like a file's content.
**Alternatives:** Following links on the sender (copying what they point to); leaving links out of the protocol.
**Why:** Following links can copy files from outside the dragged folder or loop forever; dropping them silently loses data. Sending them as links leaves the choice to the receiver, which knows its platform (Windows needs a privilege to create links).
**Trade-offs:** Receivers must handle a target that points outside the tree; how they do it is decided at the start of Phase 13 (DEVPLAN).
**Revisit if:** Phase 13 decides receivers never create links; the entry kind then stays for reporting what was skipped.

---

## 2026-11-03 — One fuzz target per decoder
**Chosen:** Separate libFuzzer targets for control messages, input datagrams, transfer messages (manifests included) and the TXT record parser, each run for 30 seconds in CI with its own corpus seeded from the fixtures. Every target checks that whatever decodes re-encodes to the same bytes.
**Alternatives:** One target that feeds every input to every decoder (Osplay's layout).
**Why:** Each decoder keeps its own coverage feedback and iteration rate; a single target would spend most of its time on prefixes that only one decoder accepts.
**Trade-offs:** Two minutes of CI time instead of one.
**Revisit if:** CI time becomes a constraint.

---

## 2026-11-03 — Crypto code copied from Osplay
**Chosen:** `core/src/crypto/{primitives,noise}` and the replay window and datagram sealing (now `input_crypto`), the identity C API, the random-source adapter, the Noise vector file and its extraction script, the crypto fuzz target and the Swift `DeviceIdentity`/`SystemRandom` wrappers are copied from Osplay commit `35046970f99c145d1b3e549d2362897c114af733`. Changes: the `osdrop` namespace, the `Osdrop control v1` prologue (Osplay's is empty), Osdrop's labels, keys for input datagrams and transfer streams instead of media, keyed BLAKE2b for per-stream keys, and session keys wiped on destruction. Monocypher 4.0.3 is vendored unmodified, checked against the release archive's SHA-256.
**Alternatives:** A new implementation; libsodium or the platforms' crypto libraries.
**Why:** The Osplay code reproduces the published Noise vectors and has been fuzzed; one C library builds identically on both platforms. The user chose copying with provenance (2026-11-02).
**Trade-offs:** A fix in either project must be checked against the other.
**Revisit if:** Monocypher publishes a security release, or Osplay changes these files.

---

## 2026-11-03 — Transfer stream IDs are split by handshake role
**Chosen:** The session's initiator opens transfer streams with odd IDs and the responder with even IDs; an ID is never reused within a session. Each direction of a stream uses the key derived from its sender's transfer base key and the stream ID.
**Alternatives:** IDs unique per direction only (the first draft); a random 16-byte stream nonce in TransferHello.
**Why:** With IDs unique only per direction, both sides could open a stream 1; frames each side sends would then use the same key on two connections whose counters both start at 0, reusing ChaCha20-Poly1305 nonces. Parity makes (direction, stream ID) unique without extra bytes on the wire.
**Trade-offs:** The session layer must enforce the parity; the codec cannot, as it does not know the role.
**Revisit if:** Streams need to be opened by a third party (not planned).

---

## 2026-11-03 — One I/O thread for the whole mesh
**Chosen:** `AsioNode` runs every connection, input socket and timer of a computer on one I/O thread owned by `core`; mesh and session logic stay sans-I/O. The timer waits for the mesh's next deadline instead of ticking. The session engine, control framing, clock offset estimator and traffic marking follow Osplay commit `35046970f99c145d1b3e549d2362897c114af733`; Asio 1.38.2 is vendored unmodified, checked against the release archive's SHA-256.
**Alternatives:** One thread per session (Osplay's layout); a thread pool.
**Why:** Up to seven sessions plus transfer streams fit easily in one thread (258 MB/s on loopback in a release build, control latency under 1 ms while a stream is saturated), and one thread means no locks in the mesh. A deadline timer lets an idle laptop sleep between heartbeats.
**Trade-offs:** A slow operation on the I/O thread delays every peer, so file reading and pasteboard work must stay off it (Phase 13 reads files on platform threads).
**Revisit if:** Profiling shows the I/O thread saturated during transfers.

---

## 2026-11-03 — Peer loss after 750 ms, session end after 10 s
**Chosen:** Heartbeats go out after 250 ms without traffic. A peer silent for 750 ms is reported unresponsive and input to and from it stops; the session itself closes only after 10 s of silence.
**Alternatives:** One timeout for both (1 s would tear down sessions on every Wi-Fi stall, 10 s would leave keys held for 10 s).
**Why:** The DEVPLAN requires peer loss detected within one second for input safety, and Osplay showed Wi-Fi stalls of several hundred milliseconds from AWDL. Separating the two keeps input safe without reconnect churn.
**Trade-offs:** Four small frames per second per idle peer.
**Revisit if:** Real Wi-Fi measurements show stalls longer than 750 ms that are not losses.

---

## 2026-11-03 — Session pairs: lower device ID dials, the same rule resolves duplicates
**Chosen:** The member with the lower device ID dials; the higher one dials too after 3 s without a session. When two connections between a pair authenticate, both keep the newer one if the same side dialled both, otherwise the one the lower device ID dialled, and close the other with a new Bye reason 7.
**Alternatives:** Both always dial and keep the first established; a coordinator.
**Why:** The rule is deterministic on both sides from facts both know, so they never close different connections; the fallback covers a computer that cannot see the other's advertisement. "Newer wins" for the same dialler replaces a connection the other side has already given up.
**Trade-offs:** A connection established moments earlier can be replaced in a dial race at startup.
**Revisit if:** Field logs show repeated duplicate closes.

---

## 2026-11-03 — Protocol fixes found while building sessions
**Chosen:** Transfer message types move to 0x81–0x88, SessionSetup gains `listen_port`, Bye gains reason 7 (duplicate connection).
**Alternatives:** Telling transfer streams apart by length; a second listener for transfer streams; learning the port from discovery.
**Why:** The shared listener must classify a connection by its first frame, and Hello and TransferHello both had type 0x01. A computer that was dialled knows its peer's address but not its listener. Closing a duplicate needs a reason that is neither an error nor a reconnect trigger.
**Trade-offs:** None; the protocol is not frozen before the first release.
**Revisit if:** —

---

## 2026-11-03 — Traffic marking on macOS only
**Chosen:** Input sockets are marked `NET_SERVICE_TYPE_VO` and control connections `NET_SERVICE_TYPE_SIG` on macOS; Windows sockets stay unmarked.
**Alternatives:** DSCP through qWAVE or a Group Policy QoS rule on Windows.
**Why:** Osplay measured that marking halves Wi-Fi loss on a Mac. On Windows, qWAVE marks only for administrators or with a policy the installer would have to add.
**Trade-offs:** Windows input datagrams get no Wi-Fi priority.
**Revisit if:** Windows loss measurements in Phase 18 call for it; the installer could then add a QoS policy.

---

## 2026-11-03 — The mesh's C API covers lifecycle, discovery and pairing only
**Chosen:** `osdrop_mesh_*` creates and destroys the mesh, feeds discovery, pairs, disconnects and reports peer state, statistics and log lines. Application messages, motion and transfer streams stay inside `core`, where Phases 8–14 consume them.
**Alternatives:** Raw message and stream functions for the platforms.
**Why:** The roster, layout, crossing and transfer engines live in `core`; platforms will never build protocol messages themselves, so a raw channel would be dead API.
**Trade-offs:** Phases 8–14 extend this API with typed functions as they arrive.
**Revisit if:** A platform feature needs a message `core` does not interpret.

---

## 2026-11-03 — The roster keeps one pairing and one revocation per device
**Chosen:** For each device the roster keeps its newest pairing and its newest revoked epoch; the device is a member while the pairing's epoch is higher. A revocation therefore covers every pairing up to its epoch.
**Alternatives:** A plain two-phase set of (device ID, epoch) pairs, where each revocation names one pairing.
**Why:** Merging stays a per-device maximum, so it converges in any order (property-tested over random operations and merges), and the roster never grows with re-pairings. A plain set would let an older, never-revoked pairing of the same device keep it in the group after its newest pairing was revoked.
**Trade-offs:** Revocations of devices that never come back are kept forever (20 bytes each; 255 fit in one message, more are split).
**Revisit if:** Groups accumulate enough revocations for the roster sync to matter.

---

## 2026-11-03 — Core decides trust; platforms only store the group state
**Chosen:** The group layer in `core` answers every trust query from its roster and hands its state to the platform as versioned bytes for the Keychain or DPAPI. The C API's trust callback, `osdrop_mesh_set_group` and `osdrop_mesh_disconnect` are gone; `group_changed`, `left_group`, members, revoke, leave and permissions replace them.
**Alternatives:** Keeping the roster in each platform and asking it per connection, as Phase 7's callback did.
**Why:** The merge and revocation rules are security decisions that must be identical on both platforms and are tested once in `core`. The platforms already store the identity key; the state belongs next to it.
**Trade-offs:** The state format is versioned, and a state from a newer Osdrop is refused rather than read.
**Revisit if:** A platform needs to inspect the roster without starting the mesh.

---

## 2026-11-03 — Only members speak for the group
**Chosen:** A RosterState is merged only from a current member and only for this computer's group. A computer in no group adopts a roster only from the computer it has just paired with, and only if it lists both of them with this computer's own key. A peer that advertises this group but is missing from the roster is refused with NOT_TRUSTED until a member's roster names it.
**Alternatives:** Accepting rosters from anyone who advertises the group ID.
**Why:** The group ID is public in every Bonjour record, so it proves nothing; trust comes only from users confirming a code with some member.
**Trade-offs:** A new member is refused by members that have not yet heard of it, for as long as the introduction takes to arrive; the mesh then dials it again.
**Revisit if:** Introductions turn out to be slow in practice (Phase 18 measurements).

---

## 2026-11-03 — Permissions are announced and enforced
**Chosen:** A computer clears the Capabilities bits of features it denies a member and sends Capabilities again in the established session when they change; independently, it refuses Enter (Refuse 3), drops input, forwarded drag offers and clipboard announcements, and answers clipboard requests with NOT_PERMITTED.
**Alternatives:** Only announcing (trusting peers to comply), or only enforcing (peers learn by being refused).
**Why:** Announcing lets a well-behaved peer avoid offering what will be refused (no wall at an edge the user is not allowed through); enforcing covers peers that do not comply.
**Trade-offs:** Capabilities is no longer a one-time setup message; receivers must replace it.
**Revisit if:** Another feature needs per-member permissions.

---

## 2026-11-03 — At most 8 members, one pairing prompt at a time
**Chosen:** A group takes at most 8 members (GROUP_FULL beyond that), and a computer shows one pairing code at a time (PEER_BUSY for others meanwhile). Two members adding computers at once may take a group past 8; nobody is removed then.
**Alternatives:** No limit; a strict limit enforced by removing members after a merge.
**Why:** Every member keeps a session with every other, so the mesh grows quadratically; 8 covers a desk or a small studio. One prompt at a time keeps a stranger on the LAN from flooding the user with codes. Removing members after a merge would be surprising and order-dependent.
**Trade-offs:** The limit can be exceeded by concurrent pairings.
**Revisit if:** Users ask for larger groups.

---

## 2026-11-03 — Monitors carry native rectangles on the wire
**Chosen:** ScreenReport gives each monitor a logical frame and a native rectangle (macOS points, Windows physical pixels). The controller moves a remote cursor in native units and between that computer's monitors as its system does; positions on the wire are native.
**Alternatives:** Logical frames only, with remote cursors moving in logical units and each endpoint converting.
**Why:** A Windows desktop with mixed scales has no logical space in which its monitors line up the way Windows moves the cursor between them; moving in logical units would make the cursor jump along every boundary between a 150% and a 100% monitor. The acceptance criterion asks for the platforms' own behaviour.
**Trade-offs:** 16 more bytes per monitor; the protocol changed before its first release (fixtures regenerated).
**Revisit if:** A platform appears whose cursor space is neither points nor pixels.

---

## 2026-11-03 — Core derives Windows logical frames
**Chosen:** `osdrop_layout_derive_frames` builds logical frames from physical rectangles and scales: the primary at its scaled origin, each touching monitor attached along the shared edge, starting as far along it as in pixels, scaled by the monitor it attaches to.
**Alternatives:** Dividing every rectangle by its own scale (leaves gaps and overlaps with mixed scales); one scale for the whole desktop (wrong sizes).
**Why:** The arrangement needs one rigid logical desktop per computer, with monitors that touch in pixels touching in points. The rule lives in core so it is tested on macOS and is the same for every Windows build.
**Trade-offs:** Rings of four or more monitors with mixed scales can come out with small overlaps; crossings still use each monitor's own conversion.
**Revisit if:** Users report arrangements that look wrong in the editor.

---

## 2026-11-03 — One layout document, newest wins, members place themselves
**Chosen:** The layout document is replaced as a whole; the newer (revision, author) wins and, for a repeated pair, the placements that sort first. A member missing from the document adds itself to the right of everything. Joining or leaving a group starts from an empty document. Editors publish only valid arrangements (INVALID_LAYOUT, code 31); receivers accept any well-formed one.
**Alternatives:** Per-placement merges; a designated member that places newcomers; refusing invalid documents on receipt.
**Why:** Arrangements are edited rarely and as a whole, so last writer wins is what users expect, and it converges in every order (property-tested, and fuzzed with repeated revisions). Self-placement needs no coordinator. Screens change after an arrangement is made, so validity cannot be a condition for accepting one.
**Trade-offs:** Two simultaneous edits keep only one. Newcomers that place themselves at once may each publish once more before the document settles.
**Revisit if:** The arrangement editor needs collaborative editing.

---

## 2026-11-03 — Edge push defaults: 1 s, 32 units, 300 ms pauses
**Chosen:** The cursor crosses after pushing against an edge with a neighbour for 1 s and 32 logical units, with no pause longer than 300 ms; immediate mode crosses at once.
**Alternatives:** Distance only (as some KVM tools do); time only.
**Why:** The user described crossing as pushing for about one second. The distance keeps a hand resting on a mouse at the edge from crossing; the pause limit lets a push continue across a short lift of the mouse.
**Trade-offs:** Defaults until Phase 12 makes them settings; values may change after the Phase 11 hardware runs.
**Revisit if:** Testing on the reference hardware feels slow or accidental.

---

## 2026-11-03 — The crossing engine stays inside core until Phase 11
**Chosen:** The C API gains screens, the arrangement, checks and Windows frame derivation; the crossing engine has no C API yet.
**Alternatives:** Exposing motion in and crossings out now.
**Why:** Phase 11 builds the controller around capture, Enter and Leave in core; the engine's shape for the platforms comes from that work, not before it.
**Trade-offs:** None for the platforms until Phase 11.
**Revisit if:** Phase 10 needs a crossing preview in the UI.

---

## 2026-11-03 — The Windows group logic is portable C++
**Chosen:** `GroupEngine` (starting the mesh from stored state, wiring DNS-SD to it, nearby computers, storing and advertising the group) lives in `windows/shared` behind small `Discovery` and `StateStore` interfaces; the engine DLL only adds DNS-SD, DPAPI, monitors and the C API.
**Alternatives:** Writing it inside the engine against Win32 directly.
**Why:** The PC is verified by CI only. Portable logic builds, lints and runs on the Mac, including two real meshes pairing over loopback, so most mistakes are found before a CI round.
**Trade-offs:** Two thin interfaces that only the engine implements.
**Revisit if:** The engine grows logic that cannot be separated from Win32.

---

## 2026-11-03 — Platforms pass every discovery to core and re-register on group changes
**Chosen:** The apps hand every Bonjour result to core, which dials only computers advertising its own group. When the group changes, the app withdraws its registration and registers again with the new `grp` value.
**Alternatives:** Filtering in the apps; updating the TXT record in place.
**Why:** One rule for whom to dial, in core. A new registration makes every browser see a removal and an addition and resolve the record again on both platforms' DNS-SD APIs, so members learn the new group and dial; Windows' DNS-SD has no record update.
**Trade-offs:** A short gap in the advertisement after each group change.
**Revisit if:** Browsers miss the change in practice (Phase 18 measurements).

---

## 2026-11-03 — Temporary pairing UI on both platforms
**Chosen:** A pairing window with the code and Pair / Don't Pair on both computers; members and nearby computers in the Mac menu and in a Windows window opened from the tray; a Mesh window in debug builds.
**Alternatives:** Building the final UI now.
**Why:** DEVPLAN keeps the final UI for Phases 16 and 17; Phase 10 needs only enough to pair, revoke and see the mesh on real hardware.
**Trade-offs:** Replaced later; strings and view models carry over.
**Revisit if:** The final UI starts earlier.

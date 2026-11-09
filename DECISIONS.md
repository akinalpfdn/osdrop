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

---

## 2026-11-03 — Received files are not marked
**Chosen:** User decision: received files get no quarantine attribute on macOS and no Mark of the Web on Windows.
**Alternatives:** Marking them as AirDrop and browsers do (DEVPLAN's default).
**Why:** The files come from the user's own computers in their own group, paired with a code; the user does not want Gatekeeper and SmartScreen prompts on their own files.
**Trade-offs:** A file that was itself downloaded on the sending computer loses its mark on the way; malware moved between one's own computers opens without the system's warning.
**Revisit if:** Groups come to include computers of other people.

---

## 2026-11-03 — Links inside folders are recreated only within their item
**Chosen:** User decision: a link inside a sent folder is recreated when its target names another entry of the same item, with the target rewritten to the mapped names; other links are skipped and counted, as are links Windows does not let the user create. A path offered as an item is followed when it is a link.
**Alternatives:** Following links on the sending side; recreating every link as it was; skipping all links.
**Why:** Following links can pull in whole trees from elsewhere, or loop. Recreating any target lets a sender plant links to places outside the destination. Within the item, links keep working after the copy, as in a Finder or Explorer copy of the folder.
**Trade-offs:** Links to things outside the item do not arrive; the user is told how many.
**Revisit if:** Users need links to shared locations (a network drive both computers mount).

---

## 2026-11-03 — File work on its own thread; the receiver pauses its stream
**Chosen:** Reading, hashing, writing and scanning run on one file thread per computer; results come back to the I/O thread. A receiver whose disk falls behind stops reading the stream (a new `pause_reading` in the transport), so TCP holds the sender back.
**Alternatives:** File I/O on the I/O thread; ranged requests as a window; a pool of file threads.
**Why:** The I/O thread carries input, and disk writes can block for tens of milliseconds under writeback. Pausing the socket bounds the receiver's memory without protocol changes, whereas ranged windows would make the sender hash the whole file for every range (ContentEnd covers the whole entry).
**Trade-offs:** One thread serializes all transfers' file work; a slow scan of a huge folder delays other transfers' reads.
**Revisit if:** Several simultaneous transfers stay below the throughput target (Phase 18).

---

## 2026-11-03 — A failed transfer removes everything it created
**Chosen:** On any failure, cancellation from either side, lost peer or quit, the receiver removes the temporaries and every file, link and folder the transfer created, newest first, before reporting the end.
**Alternatives:** Removing only the file being written and keeping finished ones.
**Why:** The phase's acceptance asks that cancelling leaves nothing behind, and a half-copied folder looks complete in Finder and Explorer while it is not.
**Trade-offs:** A large folder cancelled near its end must be sent again in full; there is no resume.
**Revisit if:** Users ask to resume large transfers.

---

## 2026-11-03 — The sender checks that a file did not change since the scan
**Chosen:** Before ContentEnd the sender compares the file's size and modification time with the manifest and sends IO_ERROR when they differ; digests computed once are reused for later requests of the entry.
**Alternatives:** Trusting the digest alone; snapshotting files before sending.
**Why:** The digest is computed from what is read, so it cannot tell a file that changed while it was being read; size and time can, at no cost. Reusing digests keeps a ranged request from rereading the whole file.
**Trade-offs:** A change that keeps both size and time (rare; tools that restore times) is caught by the receiver only when a cached digest no longer matches.
**Revisit if:** Snapshots become available (APFS clones, VSS) for the drag source.

---

## 2026-11-04 — Copies from other computers are fetched at once and written as real data
**Chosen:** User decisions after a spike on the M3 Air: Finder does not paste a file promise from the general pasteboard ("nothing to paste"), and macOS asked for a lazily provided `public.file-url` 0.03 s after it was written, before any paste (Handoff on, no clipboard manager running). So a copy on another computer is fetched in the background as soon as it is announced, within limits (text 16 MiB, images 64 MiB, files 512 MiB; settings in Phase 16), and written to the pasteboard or clipboard as real data once everything arrived; files are staged in a temporary folder and placed as file URLs (Mac) or CF_HDROP (PC). The same on both platforms (user, for Windows as well).
**Alternatives:** Lazy data providers and delayed rendering with on-demand fetches (DEVPLAN's plan); file promises; a File Provider extension on the Mac; OLE virtual files on Windows; no file paste on the Mac.
**Why:** Lazily provided data is requested at once on such Macs anyway, and a provider would block the main thread for the whole download. Real data never makes a paste wait and never hangs when the source disappears. Plain Win32 can be written without the PC; OLE virtual files could not be tried.
**Trade-offs:** Every copy is transferred even when it is never pasted (bounded by the limits). Copies over the limits do not reach the clipboard; drag and drop moves them. Explorer and Finder show progress only for the local copy from the staging folder.
**Revisit if:** Users copy large files often without pasting them, or Phase 18 measurements show the traffic matters.

---

## 2026-11-04 — Clip IDs from the wall clock make the newest copy win everywhere
**Chosen:** A copy's clip ID is microseconds since 1970, but always above every clip ID the computer has seen; members keep the copy with the highest (clip ID, device ID).
**Alternatives:** The last announcement to arrive wins; a vector clock.
**Why:** With arrival order, two copies made within a round trip leave members disagreeing. The rule needs no new fields (clip IDs were already "a new value for every change") and lets a copy made after hearing another win even on a computer whose clock runs behind.
**Trade-offs:** Of two copies made at nearly the same moment on computers whose clocks differ, the one with the later clock wins, not necessarily the one made last.
**Revisit if:** Users see a copy lose to an earlier one in practice.

---

## 2026-11-04 — Concealed copies are never read, and copies are read only while a member is connected
**Chosen:** A copy marked concealed or transient (nspasteboard.org types and older password manager types on the Mac; the exclusion and history/cloud formats on Windows) is not read at all; any copy is read only while a member that shares clipboards is connected.
**Alternatives:** Reading everything and filtering in core; marking sensitive copies on the receiving side.
**Why:** The acceptance requires that passwords never leave the computer; not reading them at all is the simplest guarantee. Not reading copies while alone avoids needless work and the Files and Folders prompt for copied files.
**Trade-offs:** A copy made while no member was connected is not shared when one connects later; copy again.
**Revisit if:** Users expect the clipboard to sync on connect.

---

## 2026-11-04 — Input control logic lives in core, not in each app
**Chosen:** A `control` layer in core is controller and endpoint at once: it takes all physical input, drives the crossing engine, sends Enter, Leave, motion and events, injects through the platform, keeps the ledger of held keys and buttons and every safety rule (peer loss, local takeover, refusals). The apps capture, inject, hide the cursor, register hotkeys and show state. User decision: Phase 11 is done now for core and the Mac, with the PC side after Phase 3.
**Alternatives:** DEVPLAN's first layout, an `InputController` on each platform (Swift and C++) with core providing the crossing engine and message types.
**Why:** The rules are the same on both platforms and are the riskiest part of the phase (stuck keys, input that never comes back). One implementation runs in the simulated network, where 1000 crossings with keys and buttons held and every way out can be tested without hardware; the Windows engine gets the same behaviour by forwarding events.
**Trade-offs:** Every mouse event of a computer in a group goes through core's I/O thread, also while the cursor is home; a thread hop per event.
**Revisit if:** The hop shows up in the latency measurements.

---

## 2026-11-04 — No crossing while a mouse button is held
**Chosen:** Edges are walls while any button is physically held, at home or on another computer.
**Alternatives:** Crossing with the button and pressing it again on the other side.
**Why:** The press went to one computer; carrying it would leave the button down there (or a drag half done) until Phase 14's drag and drop moves drags across properly.
**Trade-offs:** A drag that reaches an edge stops there.
**Revisit if:** Phase 14 carries drags across edges.

---

## 2026-11-04 — One controller at a time; a busy computer refuses
**Chosen:** An endpoint controlled by one computer answers another's Enter with Refuse 4 (busy); so does a computer whose own input is on another computer. The same controller may enter again with a new control_id.
**Alternatives:** The newest controller wins.
**Why:** Two people would fight over one cursor; local input already ends control, so the person at the computer keeps the last word.
**Trade-offs:** Two controllers cannot hand a computer over without one leaving first.
**Revisit if:** Users ask for shared control.

---

## 2026-11-04 — Keys held while crossing are released on the computer the cursor leaves
**Chosen:** When the cursor leaves home, the keys held there are released on this computer (the platform posts their release) and the held modifiers travel in Enter, so the other computer presses them; their physical release then goes to the other computer. Coming back, nothing is pressed again at home.
**Alternatives:** Leaving local keys alone; refusing to cross with a key held.
**Why:** The capture swallows the physical release, so without it Shift or Command would stay down on the computer left behind.
**Trade-offs:** A modifier held while coming back home is not held at home until pressed again (Phase 12 input fidelity).
**Revisit if:** Phase 12 adds modifier state sync.

---

## 2026-11-04 — Default hotkeys: ⌃⌥⌘⎋ returns the cursor, ⌃⌥⌘L locks it
**Chosen:** Control, Option and Command with Escape bring the cursor home (the Phase 1 release chord, which also ends any capture); with L they lock the cursor to the computer it is on, or unlock it. Both are kept by Osdrop and never reach apps or other computers.
**Alternatives:** Registered global hotkeys (Carbon `RegisterEventHotKey`); configurable keys now.
**Why:** The event tap already sees every key in both modes, so no second mechanism is needed, and the chords are unlikely to clash with app shortcuts. Settings come with Phase 16.
**Trade-offs:** The chords are fixed until then.
**Revisit if:** Users report clashes.

---

## 2026-11-04 — Shortcuts keep their place between a Mac and a PC
**Chosen:** User decision at the start of Phase 12: by default the controller swaps Control and Command/Windows (HID 0xE0 ↔ 0xE3, 0xE4 ↔ 0xE7) when the cursor is on a computer of the other platform, so ⌘C copies on a PC and Ctrl+C on a PC keyboard copies on a Mac; ⌥ stays Alt. Mac ↔ Mac and PC ↔ PC pass keys unchanged. Each computer's setting decides how its own keyboard behaves elsewhere, so the two directions are set apart.
**Alternatives:** Keys by position (⌘ is the Windows key), kept as the other setting.
**Why:** People switch between the two with their habits, not with key positions; the swap is the same both ways, so one rule serves both directions.
**Trade-offs:** Shortcuts that use the Windows key (Win+E, Win+L) are typed with Control on a Mac keyboard.
**Revisit if:** Users ask for per-key remapping.

---

## 2026-11-04 — Caps Lock travels as a state
**Chosen:** Enter carries the controller's Caps Lock (a byte that was reserved); the endpoint sets it and turns it over on each Caps Lock press, which it never injects as a key; a controller that comes back sets its own Caps Lock as it left the endpoints. On the Mac it is read and set with `IOHIDGet/SetModifierLockState`.
**Alternatives:** Forwarding Caps Lock as key presses.
**Why:** macOS does not toggle Caps Lock for a posted key event, and a press that reaches a computer whose state differs would invert the user's intent.
**Trade-offs:** A protocol change (unreleased, fixtures updated).
**Revisit if:** Num Lock or Scroll Lock need the same.

---

## 2026-11-04 — A line is a notch everywhere; a trackpad point is 1/48 of a Windows notch
**Chosen:** Scrolling travels as content movement; lines map to wheel notches (WHEEL_DELTA 120) and points to 2.5 wheel units, with fractions carried between events. Mac ↔ Mac passes values unchanged.
**Alternatives:** Mapping lines to Windows' lines per notch; pixels from the target's line height.
**Why:** A notch then scrolls as the target's own settings say, and trackpad steps stay fine. The point ratio approximates three lines of a typical Windows app per notch.
**Trade-offs:** The point ratio is an estimate until the PC is back.
**Revisit if:** K7 on the PC feels too fast or slow.

---

## 2026-11-04 — Input settings live in preferences until the settings window
**Chosen:** The modifier mapping, push time, immediate crossing, crossing modifier and per-computer pointer speed are read from UserDefaults keys under `input.` at start and handed to core; Phase 16 adds the window that writes them.
**Alternatives:** Waiting for Phase 16 to make them settable.
**Why:** Core and the tests need the settings now, and the hardware rows can be run with `defaults write`.
**Trade-offs:** Changes take effect at the next start until the window exists.
**Revisit if:** Phase 16.

---

## 2026-11-05 — The Mac interface follows Osplay's
**Chosen:** Settings as tabs of grouped forms, an onboarding window for permissions, a menu bar panel with Settings and Quit as icons, launch at login, a daily update check against GitHub releases, copy diagnostics and log export, reused from Osplay's macOS app and adapted.
**Alternatives:** A design of its own.
**Why:** The two apps are siblings with the same users; the same patterns are proven and cost less.
**Trade-offs:** Osdrop inherits Osplay's limits (no in-app updater, the update check needs GitHub).
**Revisit if:** Either app gets a design of its own.

---

## 2026-11-05 — The arrangement editor snaps computers together
**Chosen:** A computer moves as a whole and, when dropped, snaps against the nearest edge of another computer, keeping at least a sliver of that edge in common; core checks the result and the editor goes back to the arrangement in force when it is refused.
**Alternatives:** Free placement with a warning; moving single monitors.
**Why:** Core refuses computers that touch no other one, and a computer's own monitors are arranged by its system.
**Trade-offs:** A gap between computers cannot be drawn on purpose.
**Revisit if:** Users want walls between computers that sit side by side.

---

## 2026-11-05 — Renaming this computer restarts the mesh
**Chosen:** The name others see is a setting; changing it starts core's mesh again, so every session reconnects with the new name in Hello.
**Alternatives:** A core command that sends the new name to connected members.
**Why:** The name travels only in Hello; a restart reuses the existing reconnect path and needs no protocol change.
**Trade-offs:** Control, transfers and clipboard fetches in progress end when the name changes.
**Revisit if:** Renaming becomes frequent.

---

## 2026-11-07 — A forwarded drag offer names its source
**Chosen:** DragOffer carries the device ID of the computer the drag started on (zero without a drag); the group drops a forwarded offer whose source may not send files to this computer.
**Alternatives:** The controller relays the contents itself; the target asks the controller where the drag came from.
**Why:** When a third computer controls the source, the target must fetch from the source, and only the controller knows which computer that is. The protocol is unreleased, so the field is added in place.
**Trade-offs:** 16 bytes more per offer; the largest offer holds slightly shorter names.
**Revisit if:** Contents ever need to travel through the controller (computers that cannot reach each other).

---

## 2026-11-07 — The source keeps its drag held at the edge until the outcome
**Chosen:** A drag waits at the edge it left with its button down (parked) until the computer it was dropped on reports the outcome: then the source releases the button over Osdrop's edge panel, which accepts the drop as a copy or refuses it so the drag slides back. A computer whose own drag was offered keeps the left button held when its controller leaves; the controller carries the drag on, and sends the cancellation itself when the cursor comes home another way than through an edge.
**Alternatives:** Ending the source's drag at the crossing (accepted at the edge), so the outcome no longer matters to the source.
**Why:** A drop that takes nothing must slide back (DEVPLAN Phase 2), and a drag carried back to its source must go on there as if it never left.
**Trade-offs:** The source's button stays down while the drag is away; local input on the source, or losing its controller, slides the drag back. If the outcome is lost, the source's drag waits until one of those happens.
**Revisit if:** Hardware tests show a source app that ends its drag on its own while the button is held elsewhere.

---

## 2026-11-07 — Drags from the controller come home as a drop target
**Chosen:** When the cursor comes back to the controller through an edge with a drag from another computer, the controller becomes the target (it starts a drag under the cursor while the user still holds the button); coming home any other way cancels the drag.
**Alternatives:** Cancel every drag that comes home.
**Why:** "Any member can be the target" includes the computer whose mouse is in use; Universal Control works the same way.
**Trade-offs:** The platform starts a drag while the physical button is already down; this is a hardware-test item on each platform.
**Revisit if:** A platform cannot start a drag from that state.

---

## 2026-11-07 — Items of a dropped drag stay offered for a while
**Chosen:** After a drop, the source keeps the items available until 16 newer drags have been dropped; a drag that ended any other way is withdrawn at once.
**Alternatives:** Withdrawing when the target reports completion (no such message); a time limit.
**Why:** The target fetches as long as the drop target asks for the files, and the source cannot tell when that ends; a count bounds what the sender keeps without cutting a slow copy short.
**Trade-offs:** A 17th drop during a still running copy of the first one cuts that copy.
**Revisit if:** The protocol gains a message for a finished fetch.

---

## 2026-11-07 — Promised files are taken at the edge when the drag is wanted
**Chosen:** User decision: a drag of files that exist only once it is dropped (Photos, Mail attachments) is reported to core as promised; when a push at an edge wants it, the source drops it on a target of Osdrop's own at the cursor, receives the files into staging and offers them, and the drag goes on as files. Such a drag cannot slide back on the source.
**Alternatives:** Not supporting such drags (drop on the desktop first); receiving every promised drag at its start.
**Why:** Photos and Mail are common sources, and their files cannot be read before a drop; receiving only at the push keeps drags that stay on the Mac untouched.
**Trade-offs:** The cursor waits at the edge while the files are received (Photos may download originals from iCloud first); a cancelled drop leaves the original where it was but the drag is already over.
**Revisit if:** macOS offers promised content without a drop.

---

## 2026-11-07 — Transfers in the menu, both directions
**Chosen:** User decision: the menu lists transfers in both directions with progress and a cancel button, and a notification tells when one ends or fails. Core's sender reports each member's fetch of an item (`SendProgress`: sending, done, stopped); cancelling a send withdraws the whole drag. Notifications go through Notification Center, which asks the user once, at the first transfer that ends.
**Alternatives:** Finder's own progress on the receiving side only; cancelling a send for one member.
**Why:** DEVPLAN asks for progress and cancel on both sides; the sender is the only place that sees what goes out, and a drag usually goes to one member, so withdrawing it is the cancel the user means.
**Trade-offs:** A send cannot tell a member that stopped asking from one that failed: both show as not finished. Cancelling a send also stops other members fetching the same drag.
**Revisit if:** Drags are commonly dropped on several computers at once.

---

## 2026-11-07 — Copied files on their way show themselves
**Chosen:** User decision after the Mac ↔ PC run: while another computer's copied files are fetched, both computers show a small window in the screen's corner with their progress and a cancel button, a notification tells when they can be pasted, and the clipboard's notices (too large, out of reach) are notifications too. A paste before then still gives the clipboard's previous content.
**Alternatives:** Pasting at paste time with Explorer's own progress (virtual files; needs Phase 4's machinery, and Finder takes no promises); clearing the old clipboard as soon as a copy is announced.
**Why:** The fetch-first design (2026-11-04) left the user pasting the old content with no sign that files were coming. Clearing the old clipboard would break CL7 (a copy whose source is gone keeps the previous clipboard).
**Trade-offs:** A paste during the fetch still gives the old content; the window only tells the user to wait.
**Revisit if:** Phase 4 brings virtual files on the PC: Explorer could then paste while the files arrive.

---

## 2026-11-07 — The same copy again is not offered again
**Chosen:** User decision: a computer does not announce a copy that is the same as the one it last announced (the same contents, and the same files by path, size and modification time) while no other computer's copy came in between; the others already have it, so nothing is fetched again.
**Alternatives:** A cache on the receiving side that reuses files whose list (names, sizes, times) did not change.
**Why:** Copying a file twice is common, and a large file was fetched again in full. Skipping the announcement needs no protocol or engine change.
**Trade-offs:** A file changed without its size or modification time changing is not sent again. When something else was copied in between, the file is fetched again.
**Revisit if:** Repeated fetches still show up in use; the receiving cache would cover them.

---

## 2026-11-08 — A removed computer pairs again with a code
**Chosen:** User decision: a revoked device that has left the group gets a pairing code like any other computer, from either side; its new pairing has an epoch above its revocation. A revoked device that still claims the group is refused as before (NOT_TRUSTED).
**Alternatives:** Only a member may start the new pairing; or a revoked device never comes back, and the group has to be formed again.
**Why:** G4 on hardware: after removing the PC, the Mac refused it before any code, so it could never rejoin, although DEVPLAN says a revoked device comes back by pairing again. Refusing it gained nothing: a new key pairs as an unknown computer anyway, and both users still confirm the code.
**Trade-offs:** Someone at a removed computer can show a pairing code on members again; it goes nowhere unless a member accepts it.
**Revisit if:** Removed computers prompt members unwanted; a member-side block list would then be the answer.

---

## 2026-11-08 — Blocks are carried in the roster
**Chosen:** User decision: a block is group-wide. The roster keeps each device's newest block entry (a version and blocked or unblocked, with its name for the list); a blocked device is no member and every member refuses it before any code. It is chosen at removal ("Remove and Block") and at an incoming code ("Block"), and undone from the blocked list on any member. A computer keeps its blocks, but one of itself, when it leaves the group, and brings them into the next group it joins.
**Alternatives:** A block only on the computer that sets it (no protocol change, but the blocked computer can still prompt the other members); no list, only a wait after a refused code.
**Why:** Letting removed computers pair again (same day) opened code spam; a block that every member honours stops it in one place. Versioned entries merge like the rest of the roster, so an unblock spreads and a member that missed it cannot undo it.
**Trade-offs:** A new identity (Osdrop reinstalled, its key store cleared) is a new device and is not blocked. RosterState and the stored group state change format (version 2; version 1 is still read). Blocks a computer brings into another group apply there too.
**Revisit if:** Users are blocked by new identities in practice; a pairing wait for refused codes would then help.

---

## 2026-11-08 — Hidden cursors come back three ways
**Chosen:** OsdropInput hides the cursor by replacing every system cursor with a transparent one and restores the copies it saved. A marker file records that they are hidden: after a crash the next OsdropInput reloads the cursors, and the pipe's client, which runs as the user, reloads them too when the pipe breaks.
**Alternatives:** Only `SPI_SETCURSORS` on release; a full-screen window with no cursor.
**Why:** As SYSTEM, `SPI_SETCURSORS` reads SYSTEM's registry, so it brings back the default scheme, not the user's; only the user's own process gets their scheme back. A full-screen window takes the foreground from the user's app.
**Trade-offs:** After a crash with no client running (the developer tool killed too), the user gets the default cursors until they sign in again or change the scheme.
**Revisit if:** Code signing allows `uiAccess`: the user's own process could then hide and restore the cursors.

---

## 2026-11-08 — The input pipe admits Osdrop's own executables only, one client at a time
**Chosen:** A client must run in OsdropInput's session as its user and be `Osdrop.exe` or `OsdropInputTool.exe` from OsdropInput's own folder; the pipe has one instance, created once per process. The shell's build copies the input process, the service and the tool next to `Osdrop.exe`, so development uses the same check as an installed copy.
**Alternatives:** Any process of the session's user (the ACL alone); a shared secret handed to the app.
**Why:** DEVPLAN's security model: malware running as the user must not reach a SYSTEM injector. The install folder needs an administrator to write to; a secret the user's processes can read protects nothing.
**Trade-offs:** The tool and Osdrop cannot use the input process at the same time; the hardware rows quit Osdrop first.
**Revisit if:** Osdrop needs a second client (a helper of the drag bridge), or code signing allows checking signatures instead of paths.

---

## 2026-11-08 — Phase 3 makes the PC controllable; the PC controlling others is Phase 11's Windows half
**Chosen:** The engine's `InputBridge` injects what a controlling computer sends and reports this PC's own input during control, and the PC announces OSDROP_FEATURE_TAKES_CONTROL. Watching all of the PC's input for crossings from its edges, capture and the hotkeys stay with the Windows half of the controller (Phase 11), on top of the same input process.
**Alternatives:** Both directions in this phase.
**Why:** The feature bit means "injects input", the phase's gate is capture and injection over elevated windows, and the bridge is what lets a Mac's cursor cross in. The controller needs an observing mode in OsdropInput and crossing tests of its own.
**Trade-offs:** Until then a PC's own mouse cannot cross to a Mac.
**Revisit if:** The user wants both directions in one step.
User decision (same day): after the hardware rows pass, the PC controlling others is added to the Phase 3 PR.

---

## 2026-11-09 — The PC watches its own input through OsdropInput, measured from where the cursor is
**Chosen:** While a member is connected, the engine asks OsdropInput to watch (Watch): the hooks report every movement as where Windows takes the cursor and how far it moved from where it is when the hook runs, and every button, wheel and key, without swallowing them. The engine keeps the position on a monitor and hands it all to core. While captured, movement is measured the same way, so the park point and apps that move the cursor do not matter; for 2 ms after Osdrop moves the cursor itself, movement is not reported.
**Alternatives:** Hooks in the engine (medium integrity); Raw Input for movement; deltas from one hook position to the next.
**Why:** A medium-integrity hook hears nothing while an elevated window has the keyboard, so crossings and held keys would fail over Task Manager or an administrator terminal. A low-level hook sees where the cursor would go before Windows keeps it on a monitor, so a push against an outer edge still moves, as core's edge push needs; position-to-position deltas would be zero there. Raw Input gives device counts without the pointer settings and arrives apart from the hooks.
**Trade-offs:** Osdrop's own process now receives keys typed into elevated windows while a member is connected (SECURITY_MODEL.md). Absolute pointing devices (pen tablets, remote sessions) cannot push past an edge, as on any system that reads positions.
**Revisit if:** A hook ever reports positions already kept on a monitor; then Raw Input would carry the push.

---

## 2026-11-09 — PC chords: Ctrl+Alt+Win with Esc returns, with K locks; OsdropInput ends a capture by itself
**Chosen:** The Mac's ⌃⌥⌘ chords on the same physical keys: Ctrl+Alt+Win+Esc brings the cursor home, Ctrl+Alt+Win+K locks it (L belongs to Windows: Win+L locks the PC and no hook can take it). OsdropInput's hook matches them while it watches or captures and swallows the completing key; at home it then presses an unassigned key (VK 0xE8) so releasing Win or Alt opens no menu. During capture the return chord, or a heartbeat missing for 1 s (the engine sends one every 250 ms), ends the capture inside OsdropInput, with the cursor where it left; the engine then brings the cursor home in core.
**Alternatives:** Chords in the engine; RegisterHotKey; ending a capture only when core says so.
**Why:** Only the hook sees keys while they are swallowed, and the way out must work when the engine or core hangs, as the Mac's tap and capture guard do.
**Trade-offs:** The chords are fixed until the Windows settings (Phase 17).
**Revisit if:** Users report clashes with Ctrl+Alt+Win+K or Esc.

---

## 2026-11-09 — The Windows interface follows Osplay's shell; the engine reports the input state
**Chosen:** The shell is Osplay's: WPF-UI following the system theme live, a tray icon whose click opens a panel above the taskbar, a tabbed Settings window, settings in `%APPDATA%\Osdrop\settings.json`, one instance per session. Its contents are the Mac's, tab for tab. The engine reports what the panel needs from its own state (`osdrop_engine_get_input_state` and INPUT_CHANGED) instead of passing core's control callbacks through; a new name restarts the engine, as the Mac restarts its mesh. While OsdropInput does not answer, the panel names the cause from the service manager and repairs it by running `OsdropService.exe --install` elevated. The tray icon is the Mac menu bar glyph, drawn into .ico files by a script, and filled while another computer controls the PC.
**Alternatives:** A main window instead of a tray panel; core's callbacks forwarded one by one to C#; a repair that starts the service through the service manager (needs the same elevation, cannot install a missing one).
**Why:** The two Osdrop apps and the two sibling products should look and behave alike. The shell must read the state again after it starts or restarts the engine, which events alone cannot give; one state call can.
**Trade-offs:** Renaming drops every session for a moment.
**Revisit if:** The panel grows past what a tray panel holds; then a main window.

---

## 2026-11-09 — The PC's chords are settings, as on the Mac
**Chosen:** User decision: the return and lock chords can be changed in Settings › Input. The shell records a key by its scan code with Ctrl, Alt, Shift and Windows; the engine refuses a pair the hook cannot match (no Ctrl, Alt or Windows, a modifier as the key, a key without a place in the key table, the same chord twice) and sends the pair to OsdropInput in a SetChords request, again after every reconnect. OsdropInput uses the defaults until a client sends chords and after it goes. With the same key, the chord with more modifiers is matched first, so both stay reachable. In the recorder Escape alone cancels; with modifiers it is recorded.
**Alternatives:** Fixed chords shown in Settings (the earlier entry's trade-off).
**Why:** Both apps should work the same way, and a chord can clash with an app the user relies on.
**Trade-offs:** A combination Windows takes for itself (Win+L, some Win+letter shortcuts) never reaches the recorder, so it cannot be chosen.
**Revisit if:** Users need a chord with a key outside the key table.

---

## 2026-11-09 — No crossing while a game holds the cursor
**Chosen:** User decision: while an application holds the cursor in a rectangle smaller than the desktop (ClipCursor, as full-screen games do to keep it on their monitor), the PC's cursor crosses to no other computer. The engine reads the rectangle every 100 ms while watching; the forwarder then keeps positions in it and reports only the movement inside it, so a push at its edge moves nothing and core sees no crossing. A setting in Settings › Input turns it off; it is on by default.
**Alternatives:** Detecting full-screen applications (QUNS states); a list of games; leaving it to the lock chord.
**Why:** The low-level hook sees where the cursor would go before Windows applies the rectangle, so a held cursor still pushed past the edge and crossed. Holding the cursor is exactly how a game says it should not leave; full-screen detection misses borderless games and catches videos.
**Trade-offs:** Games in a window that do not hold the cursor still let it cross (the lock chord covers them). Up to 100 ms after a game takes the cursor, a push can still cross.
**Revisit if:** A game holds the cursor in a way GetClipCursor does not show, or users report Mac games letting the cursor cross.

---

## 2026-11-09 — The PC's drags are virtual files; its drop targets show while the left button is down
**Chosen:** A drag Osdrop starts on the PC offers its files as virtual files (FILEGROUPDESCRIPTORW and one FILECONTENTS stream per file) with IDataObjectAsyncCapability, plus CF_HDROP to staged copies up to 64 MB. The drop targets along the edges are shown while the left button is down, polled every 30 ms, instead of only during an OLE drag.
**Alternatives:** CF_HDROP only, staging everything at the drop; detecting OLE drags (Windows has no drag pasteboard to watch); a shell extension.
**Why:** Virtual files let Explorer pull the content as it copies, with its own progress and without Osdrop holding the drop, which the 10 GB criterion needs; CF_HDROP alone would stage everything before Explorer can start. Windows gives no notice that an OLE drag began, but every drag holds the left button, and the targets take nothing but drops.
**Trade-offs:** A descriptor holds 259 characters of path, so deeper files are left out of a drag (counted). Folder trees from other computers need their manifest before the drop (Phase 14). While the left button is down, a 6-pixel strip at an outer edge belongs to Osdrop.
**Revisit if:** Users report the strips getting in the way of something done at a screen edge with the button held.

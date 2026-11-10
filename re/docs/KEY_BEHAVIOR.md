# Keys, hold repeat and slider

2026-11-10. Static investigation of `gen/deep/pvmi-mapped.elf`, SHA-256
`094dadb25b312573973d0806c9838e787a2ba6e76405691d16223d6ef0c65ecf`.
The artifact is an analysis wrapper over the captured firmware. No hardware
operation, physical-device trace or Windows runtime comparison was performed.
The owner describes the original switch as latched left Lock, centered Unlocked,
and spring-return right held for Power; she requested that desktop behavior.

## Event generator

Observed function code/assembly and file/evidence identity are retained in
`key-input-dossiers.json`; six literal/data reads are in `key-input-data.json`.

| Function | Observed role | Evidence ID |
| --- | --- | --- |
| 0x16c0b8 | Initializes per-key states and creates btn_timer with initial/reload 5/5 | `ev_a0611d2f8cd54a7ccd873ff59a964016031af98bf6792967005febe9bbff566d` |
| 0x173e8 | Decrements per-key countdowns and posts timer-expiry messages | `ev_6e1daa11a63c75494842cb2b77e82c3b00e96c104a4f7ece3800bf0d514725b6` |
| 0x14a722 | Receives key configuration, raw changes and timer expirations | `ev_c04c7a3d46b7a70aa3b2cd8c8258f448281ca98ae9c59b222607611ad3ade388` |
| 0x14a842 | Dispatches six key states and starts/restarts the shared timer | `ev_edbb5abba68a585fd64f6581d51da449661d75f420b9575cb4a2d3f092c5879d` |
| 0x14a90c..0x14aa6c | Implements raw down/up, short click, long start, repeated hold and release transitions | See each of the six retained dossiers |
| 0x14a8c8 | Updates countdown/state and emits selected event bits; initial-delay flag triples one countdown | `ev_5b67056e52799ef15c0b4d7c97bce8f3defb4e7f5ac64cf2874228b3c94151e0` |
| 0x14a676 | Masks enabled event bits, prefixes the key index, adds lock status and enforces repeat routing | `ev_b6a917b461b2d3cd4e074f7f735d2c31c616c58642fc7040b59c3b8ed5210a27` |
| 0x16c074 | Applies packed per-key masks/cadence and shared flag configuration | `ev_b2ff337aea92616cd1dd5be0afa46f0bb1e68c1a5e4188314d21dc668d55331e` |

**Derived event meanings:** `key << 8 | 01` is a normal short click, produced
on release before the long threshold. `04` is long-start, `10` is hold repeat,
and `08` is release after a recognized hold. The generator also supports `02`
and a `20` lock-status bit. Treating every `...01` as raw key-down would
incorrectly skip a track before seeking or perform Select twice.

In the ordinary default path, 0x14a92c emits long-start 04 and immediately
arms/emits the first repeat 10. State 5, handled by 0x14aa6c, emits subsequent
10 events, then emits 08 on release. A short release emits 01 instead.
Additional double-click/configuration states exist; they are not exposed as
new desktop actions by this port.

## Worthwhile consumer behavior

| Area | Observed firmware behavior | Desktop port |
| --- | --- | --- |
| Volume, 0x15a1c0 | Plus accepts 401/404/410; minus accepts 501/504/510; changes one original 0..31 level, clamps to limit and refreshes popup timeout | Short tap changes one level on release; long-start plus its immediate repeat changes two levels, followed by repeat |
| Home, 0x152982 | 601/610 advances, 701/710 reverses; wraps | Held home-category navigation and wrap |
| Static lists, 0x14edf6 | Same repeat families; consumes 608/708 without another move | Held list navigation and wrap; release does not move again |
| Back, 0x14edf6 and root 0x14c588 | Short 201 follows parent routing; long 204 posts Home state 6 | Hold Back returns to main menu in ordinary menus |
| Recording, 0x157532 | Play 101/104 toggles pause; Back 201/204 pauses and opens Save confirmation | One action per gesture; the old hold cannot cancel the newly opened Save prompt |
| Confirmation, 0x157c36 | Play 101/104 confirms; Back 201/204 cancels; navigation does not choose an answer | No auto-repeat through a modal boundary or repeated confirmation |
| Playback, 0x1532e6 | 601/701 skips; 604/704 starts seek; 610/710 repeats; 608/708 stops seek | Tap skips, hold seeks, release stops without skipping |
| Seek, 0x153296 | Parameters 4/5/10/20 at counts 1/10/20/40 | Preserved thresholds; interpreting those parameters as seconds remains a desktop adaptation |
| Custom EQ, 0x15423c | Navigation repeats adjust gain by two units, bounded ±10; Play101 traverses bands; 104 is not consumed as that action | Held gain adjustment, short Select band traversal, no extra traversal on a long Select |
| Volume-limit editor, 0x15a1c0 | Vol-/Vol+ changes the draft; Up/Down families are consumed without modifying it | Only Vol-/Vol+ adjusts the preview; Up/Down is ignored |

Earlier consumer code/evidence is also retained in `ui-function-dossiers.json`,
`ui-flow-dossiers.json`, `ui-refinements-dossiers.json`,
`playback-recording-dossiers.json` and `lock-dossiers.json`.

## Timer counts and power

Observed initial data at 0x1ed8 consists of event-mask/repeat-count pairs:
keys 4/5 (volume) and 3 (power) have count 2; keys 6/7 (navigation) have count 6.
Each countdown decrement is supplied by the period-5 btn_timer. Data at 0x1408
starts at 1; the state machine uses it to triple the initial count 5 to 15,
and rearms that initial-delay flag after release. Thus the ordinary default
long threshold is **75 kernel ticks**, with volume/power repeats every
**10 ticks** and navigation repeats every **30 ticks**.

Root 0x14c588 counts key-3 repeat events **310**. At count 3 it posts **40350**,
which enters the existing shutdown path; **308** release resets the count.
This gives **95 kernel ticks** for the ordinary default power hold
(75 + two further 10-tick intervals). Its code and assembly are retained under
`ev_2c7803bc96d462c8f56b83d881db357fc3bbd942dba30a88a5f5b9480ad3f945`.

Key 8 is the lock-switch family. The closed/open padlock paths use 804/828.
The root's **40313 / 08040000** message goes to the key manager and clears
its shared lock flag via 0x16c074; it is **not a shutdown request**. This
distinction follows the receiving queue and packed argument interpretation.

**Unknown:** the physical kernel tick duration has not been established by
this lookup. The desktop interprets one tick as 10 ms, yielding a 750-ms long
threshold, 100-ms volume repeat, 300-ms navigation/seek repeat and 950-ms power
hold. These are explicit desktop timings, not measured physical-device values.
Configuration changes and optional double-click states can affect timing;
the port models the ordinary default path.

## Desktop input and slider

`Interface::pressKey/releaseKey/cancelKeys` own physical gestures for both mouse
and keyboard; semantic `input` remains a one-shot API for wheel/menu clicks
and diagnostics. Windows auto-repeat messages are ignored. Keyboard aliases
and a matching mouse button retain ownership until all matching inputs release.
Releasing a different direction cannot finish an active seek. Late timer ticks
do not cause a burst of accumulated actions.

A context change, confirmation, lock or wake-only press consumes the old gesture
through release. The previous Windows Back auto-repeat could pause a recording,
open Save, and immediately discard it with the next repeat. A held Back now
opens that prompt once and must be released before another Back can cancel it.

Slider left latches Lock, center unlocks, and right is held momentarily. A short
right hold springs back without switching off. A completed hold requests the
normal Windows close path once, which saves settings, keeps an active recording,
stops playback and plays shutdown animation. P is the keyboard power hold; L
remains a desktop lock toggle. Focus/capture loss cancels unfinished holds while
preserving a latched left position. Boot/Updating suppress new slider actions.

The owner-specified rendering is a recessed rounded groove with enough height
for the handle, a rounded handle about two-thirds of the groove's width, and
lock/power icons on the left/right. These are authored desktop shell graphics;
the LCD lock and shutdown graphics remain extracted firmware assets.

The owner's later shell correction places volume on the left and the switch
assembly on the right. A dark mounting plate backs both icons and the groove.
Continuous rightward handle travel is separate from the logical detent: power
arms near the right stop, partial travel remains unlocked, and release eases
back to center. Left Lock remains a hard snap/latch. These pointer and animation
mechanics are desktop adaptations, tested by `sliderTravelTests`.

## Validation boundary

`make test-keys` exercises the actual portable controller with fake services:
hold thresholds and caps, repeats and wrap, EQ traversal, Back/Select modal
boundaries, all seek acceleration thresholds, release ownership, context/wake/
lock cancellation, slider early release/rearm/single-fire and preservation of
active/paused recordings until the shell handles shutdown. `make test-ui`
retains the existing resource-pixel and controller regression coverage.
Windows crosscompilation checks shell integration; actual hit testing, physical
timing comparison and microphone finalization still need a Windows/device run.
No hidden service key sequences or hardware FM scan actions were added.

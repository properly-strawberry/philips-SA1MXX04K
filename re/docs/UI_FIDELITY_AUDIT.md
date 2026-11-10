# Desktop UI fidelity audit

2026-11-10. Scope: current desktop controller and saved ARM/Thumb dossiers,
not a device runtime comparison. Addresses refer to the analysis-only
`gen/deep/pvmi-mapped.elf`, SHA-256
`094dadb25b312573973d0806c9838e787a2ba6e76405691d16223d6ef0c65ecf`.
Observed means present in the captured code/resources; derived means an
interpretation of that evidence. Desktop timings remain adaptations unless
explicitly proven. Native fake-backend tests verify our implementation, not
equivalence with the physical player.

## Source-backed parts

| Area | Observed evidence | Desktop result |
| --- | --- | --- |
| Home and lists | Constructors/painters 0x152db4/0x152934, menu builder 0x150264 | Original bubbles, icon coordinates, centered selection, folder/check icons, recovered submenu tables |
| Playback | 0x1537a6/0x153c2c, numeric fonts 30/32 | Original title/metadata/time/progress/status placements; counters padded to four digits at owner's request |
| Title | 0x159d3c/0x159ede/0x159e28 | Hold, one full pass, restore glyph-boundary prefix plus original ellipsis, stop; metadata waits for completion |
| Artist/album | Tick 0x1597cc and constructor 0x159ada | Clipped 14-pixel row; horizontal pass for long metadata before the two rows slide upward together; desktop 100 ms steps |
| Volume | Paint 0x15a078, event 0x15a1c0, constructor 0x15a5e8 | Original group-4 widget and positions, 0..31 scale, cap marker, fullscreen popup; limit preview can commit or cancel |
| Updating | Root split 0x14ce9e, progress dispatch 0x14d106, spinner 0x196978/0x15bd10 | Group-5 spinner at (57,8); label rectangle (5,29,118,14), percentage (0,44,128,14), original group-7 strip at (5,59); requested three-second synthetic scan |
| Custom EQ | Event 0x15423c, paint 0x15441a | Original backgrounds, selected column, unit bars and thumb; Up/Down gain, Select next band/exit, Back previous band/exit |
| Radio geometry | Constructor 0x154ec0 | Original vertical scale, numeric font 31 and label placements; streams are an intentional desktop adapter |
| Recording geometry | Constructor 0x156e66 | Original filename/time/progress/microphone placements; microphone and storage services are desktop code |
| Shutdown | 0x15876c/0x15863e | Bye, then group-8 frames 0..6 at (15,9); requested slower 1000/500 ms timing |
| Confirmations | 0x157de8/0x15802a, recording 0x157532, factory reset 0x158240, playlist 0x15b3e0/0x15af5e | Original centered rows and fixed Play/OK and Back/Cancel footer, no Yes/No selection; Restore caption for reset |
| Lock/unlock | Constructor 0x15802a, paint 0x157be0, timer/event 0x157c10/0x157c36 | Full-screen themed closed/open padlock at (52,11), timed return to previous page and retrigger on blocked input; 1250 ms is a desktop interpretation |
| USB connected | HVMI blitter 0x10565c, table 0x10c054 | Actual extracted PC/player, battery and prerendered FullyCharged/Charging/transfer strips |

Full code and evidence identities are retained in `ui-function-dossiers.json`,
`ui-drawing-dossiers.json`, `playback-recording-dossiers.json`,
`ui-refinements-dossiers.json`, `shutdown-dossiers.json` and
`host-ui-dossiers.json`, `lock-dossiers.json` and `ui-flow-dossiers.json`. See `FIRMWARE_UI.md` for precise claims and limits.

## Title, menu and confirmation follow-up

The earlier audit identified independent looping title scrolling, a combined
Options menu and invented Yes/No confirmations. These have now been addressed:

- Title initially shows the clipped text without ellipsis, holds ten updates,
  passes all the way off the left edge once, then returns to a prefix ending
  at a glyph boundary at or before 106 pixels, plus original group-3 ellipsis.
  Metadata animation waits for completion. Track/title changes restart it.
- Menu builder 0x150264 state 0x12 has exactly Playlist-on-the-Go, Add To
  Playlist, Remove From Playlist. The desktop now exposes that original route.
  Event 0x150904 requires a current playback queue for Add; removal enters a
  playlist browser. Dialog 0x15b3e0 and event 0x15af5e confirm the track action.
- The combined M menu has no recovered original counterpart. It remains an
  explicit desktop shortcut, available only for a current queued track on the
  playback page, with applicable Add/Remove and no orphaned Add bookmark action.
  Its delete-file command is also a desktop shortcut, not a claim about the
  original hardware key that opened a per-file action.
- Existing confirmation flows use source-backed prompt rows and fixed footer
  captions. Play confirms, Back cancels; Up/Down and volume cannot select an
  answer or accidentally perform the action. Recording confirmation includes
  the quoted filename; factory reset uses the Restore caption. Radio options
  now follow recovered Presets / Manual tune / Auto tune order, with the
  original three-line auto-tune question.

## Highest-priority remaining reconstruction

1. **Timer and key cadence.** Firmware timer arguments do not by themselves
   prove milliseconds. Title/metadata 100 ms steps, spinner 90 ms, volume popup
   1400 ms, lock feedback 1250 ms, seek hold 450 ms/repeat 250 ms and shutdown
   4.5 seconds are desktop choices. Hardware routing and all long-press cases
   remain incomplete.
2. **Regional dialogs and notices.** English prompt geometry and source
   language-dependent playlist/footer positions are ported. Thai shaping,
   language-specific multiline layout, long-caption scrolling and all success/
   error modal lifetimes have not been compared to a running device. Generic
   desktop notices now clear the whole screen and omit the invented checkmark,
   but specialized success layouts, bulk Delete All and error flows remain.
3. **Audiobook feature gates.** Books/Bookmarks controller pages remain legacy
   internal pages with no normal UI route. Bookmark creation was removed from
   the ordinary music shortcuts to avoid a dead-end flow. Recover the original
   audiobook/DRM feature gate before exposing these as original behavior.

## Other remaining differences

| Area | Evidence / present behavior | Remaining work or intentional adaptation |
| --- | --- | --- |
| Physical controls | `ref/tpv_3433752.avif` shows four face keys around a blank center | Back left, Play/Pause right, Up/Down navigation/transport and separate Vol-/Vol+ now match that arrangement. Top lock-only slider is the owner's requested adaptation; shell dimensions remain hand drawn |
| Folder browser | Desktop begins inside Music and queues directory songs | Recover original root/drive access, sorting, filtering and file-type rules |
| Playlists/bookmarks | M3U8 and bookmarks.tsv beside EXE | Desktop formats; original formats, limits and ordering are unknown |
| Recording | Windows waveIn, timestamped mono 22050-Hz WAV; estimated remaining time capped at four hours | Recover original codec/naming/free-space and complete pause/confirmation state machine |
| Radio | Hidden sixth item after Now/Last Played, five-dot rail with no radio dot | Owner-requested internet-radio addition to a model without FM; hardware scan/20-preset behavior is not emulated |
| Equalizer DSP | RBJ filters and approximate named presets | Original gains, frequency/Q and DSP implementation not recovered; gain units interpreted as dB by desktop |
| Settings | Checked choice is selected on entry | Unchecked-menu cursor persistence and defaults need verification; Backlight exists as a desktop setting but has no recovered Display-menu route |
| Languages/fonts | All 24 tables and default fonts 33/34 embedded | Original Thai composition through 0x19369a is not ported; direct glyph lookup is insufficient for correct shaping. Meaning of font configuration value 4 (groups 28/29) remains unknown |
| Battery/power | Full battery decoration and desktop sleep timers | Host battery states, firmware power management and complete charging transitions not connected |
| USB preview | F9 source-backed; F10 uses localized RSRC charge-choice images | HVMI also has its own choice images at (0,16); original context/selection transitions are not fully ported |
| Playback services | FFmpeg, waveOut, Windows filesystem | Intentional adapter rather than firmware decoding, DRM or database emulation |

## Validation boundary

`make test-ui` exercises the actual shared renderer/controller with fake
services, checks resource pixels and intermediate metadata frames in all three
themes, and tests navigation, settings, seek, radio JSON, recording decisions
and DSP. Crosscompilation checks the Windows code, but real body hit testing,
microphone capture and network streams still require Windows runtime checks.
Wine cannot start in this sandbox because its runtime directory is read-only.
The owner's earlier successful run predates these refinements.

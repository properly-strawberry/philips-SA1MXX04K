# Desktop UI reconstruction

2026-11-10. The desktop controller adapts recovered resources to FFmpeg, Windows
audio/microphone I/O and filesystem folders. It is not firmware emulation.

## Observed resource structure

Inspection of every type-2 RSRC payload found 322 mode-0 RGB565 UI images and
128,901 mode-1 monochrome glyph payloads; no other encoding values were present.
Counts identify payload offsets, not distinct designs. `gen/resources/inventory.tsv`
records all offsets. Thus all RGB565 images in this resource inventory have been
exported, not merely a visually selected subset.
All monochrome payloads were also checked: none exceeds 32 pixels in either
dimension. There is no overlooked large monochrome PC-screen bitmap in that set.

- Group 0: home/category animations, six categories with six stages each,
  plus theme variants and selection markers. Category bases: 9, 45, 81 for
  Dark, Blue and Pink; families are Music, FM Radio, Recordings, Folder View,
  Settings and Now Playing. Frames range from background to fully lit icons.
  Assets 0.0..0.2 contain five indicator dots; 0.3..0.5 contain six. The desktop
  uses five: the owner's model had no radio. Firmware RAM 0x75a8 gates FM.
- Group 1: battery/charging images, small status images and fullscreen warnings.
- Group 2: record, previous/stop/next/pause/play, EQ/repeat/shuffle and status icons.
- Group 3: arrows, checks, selection bars (125x21), locks, scrollbars and dialogs.
- Groups 4/5: small controls and waiting-animation elements.
- Group 6: EQ background and slider graphics.
- Group 7: startup animation, registration screen, hourglass and progress segments.
- Group 8: shutdown expanding-dot animation.
- Group 9: 24 pairs of localized charge/transfer versus charge/play selections.
- Group 10: 24 UI text tables; the English table has 235 indexed text entries.
- Groups 33/34: default 12/14-pixel fonts selected by recovered initialization.
  Embedded glyph counts: 32,251 and 32,094 respectively. Firmware substitutes
  groups 28/29 under a configuration flag; that flag's meaning is unresolved.

The desktop extracts labels from these tables at build time rather than
maintaining translated copies. Bitmap color 0x07e0 is treated as a transparent
key, supported by visual inspection of glyph-free icon backgrounds. Theme
backgrounds are sampled from the original background-frame pixels.

The independent 48-image HVMI table is also embedded (370 images total).
Special numeric fonts in groups 30/31/32 supply playback times, the radio dial
and the five-pixel track counter. Playback, recording and radio geometry now
follow their recovered constructors; details and limitations are in FIRMWARE_UI.md.

## Implemented adaptation

`src/interface.cpp` is the shared portable renderer/controller, compiled both
into Windows and into `tests/interface_test.cpp`. `src/main.cpp` supplies the
Win32 shell and filesystem service; audio remains FFmpeg/waveOut. New recording
support uses waveIn and creates WAVs in the sibling Recordings/Voice folder.
The radio adapter uses user-defined virtual MHz positions from Playlists/radio.json.
The distributed radio.json is empty; radio.example.json documents the schema.
Existing configuration/media are preserved by packaging. The blank/example
files are tracked in assets/Playlists and seeded into the portable output.

The original DEVICON.FIL was observed to be an ICO container. Inspection of the
built PE with `tools/extract_pe_resources.py` found 12 ICON resources, one
GROUP_ICON and one VERSION resource. The window loads that icon group.

Recovered constructors now establish the home and centered-list geometry,
folder icon selection, theme palettes, font spacing and actual submenu tables.
See FIRMWARE_UI.md for addresses, evidence and exact values. At the owner's
request, internet radio is the hidden sixth home item after Now/Last Played,
always reachable even with an empty table. The original five-dot rail stays;
no bubble is highlighted for Radio. Native input/services, some screen layouts, EQ coefficients,
capacity reporting and I/O remain desktop adaptations. The detailed current
audit is in UI_FIDELITY_AUDIT.md. Requested timing changes
are 170 ms boot frames and three seconds of Updating… with centered text,
0-100% beneath it and the original bottom progress-strip frames. The original
group-5 spinner is restored at (57,8); text glyphs start at y30/y45 and the
strip at y59. Track counters pad to four digits.
Shutdown uses original group-8 expanding dots after Bye, 4.5 seconds total at
the requested slower pace. The window waits for the sequence to finish.

## Tests and limits

`make test-ui` passes navigation, 18 home/theme renders, all 24 languages,
play/pause/seek/next, settings/EQ, sleep/wake, JSON validation, virtual frequency
tuning, recording save/discard, sibling recording paths, lock and a 5,000-song
library. Services are fake in this test: no microphone is accessed and no network
request is made. EQ Off is checked as an exact bypass, and boosted filters are
exercised for stability. Native screen evidence is emitted as PPMs in gen/ui-tests.
Windows crosscompilation and PE resource inspection also pass. Real Windows
microphone/stream operation needs runtime verification; the previous player
was reported working by the owner, but that report predates these additions.

Subsequent deep analysis mapped hvmi/pvmi at 0xea000 using byte-identical SB
resident anchors. Constructors, painters and menu tables are now recovered;
the earlier startup-only query is superseded. tools/map_vmi.py reproduces the
mapping. FIRMWARE_UI.md and the saved function dossiers preserve the findings.

## Connected-to-PC screen: found in HVMI

Owner observation: the physical player's connected screen contains text that
looks like FullyCharged (space uncertain), possibly prerendered, and a full small
battery icon in the upper right. This is a user observation, not a captured image.

Before the host-code discovery, local checks established:

- No matching complete text in ASCII, UTF-16LE or UTF-16BE was found in any of
  the six decrypted sections. Partial diagnostic words are unrelated matches.
- Visual review of all 322 UI images did not identify that separate PC graphic.
  Group 9 is the charge-mode selection; group 7's smaller late entries are
  progress segments rather than computer graphics.
- The system-partition tail from file offset 9,000,928 to 9,437,184 is entirely
  0xff (436,256 bytes). It contains no additional graphics.
- The captured 4 MiB hidden drive 0x0c contains 20 leading metadata bytes and
  then only 0xff. It contains no bitmap payload outside those metadata bytes.
- Hidden drive 0x0b and boot ROM were not captured. Neither can be excluded.
- hvmi contains USB connection/charging diagnostics. Those establish USB code
  presence but do not locate the screen, its pixels or its renderer.

The subsequent code investigation resolved this: host blitter 0x10565c uses
48 pointers at VA 0x10c054, entirely inside hvmi. They include the PC/player,
battery and connection frames, and the RGB565 text strips spelling FullyCharged
without a space. It is prerendered. tools/extract_host_ui.py exports all 48;
F9 now renders this screen using actual pixels and recovered coordinates.
F10 retains the charge-choice preview. See FIRMWARE_UI.md for the host functions
and evidence. Neither hidden 0x0b nor boot ROM is needed for this graphic.

## Subsequent refinements

`ui-refinements-dossiers.json` records volume, Updating/spinner and Custom EQ
code; `lock-dossiers.json` records full-screen closed/open feedback and timer
behavior. Metadata now slides in a clipped 14-pixel area with a horizontal pass
for long labels. The volume widget uses actual group-4 sprites and a 0..31 UI
scale. Limit edits support preview/commit/cancel. Custom EQ uses recovered
column/bar/thumb painting and Select/Back band traversal.

Lock feedback clears the full screen and uses group 3.15..17 (closed) or
3.18..20 (open) at (52,11). It times out to the previous page, retaining the
control lock; a blocked keypress shows it again. Its 1250-ms desktop duration
is not a proven firmware wall-clock duration. The photo establishes the four
face-button arrangement; the requested separate top slider only locks/unlocks.
The full list of remaining approximations is in UI_FIDELITY_AUDIT.md.

## Title and modal-flow follow-up

Title scrolling now follows 0x159d3c/0x159ede/0x159e28: initial clipped text,
ten-update hold, one full pass, then a stopped glyph-boundary prefix with the
original ellipsis. Metadata waits for title completion. Cadence is interpreted
as 100 ms per update, not established hardware timing.

Music > Playlists has the actual three entries from menu state 0x12, including
Add of the current queued track and a removal browser. The M menu is retained
as a desktop shortcut for the current track, with the applicable playlist action;
there is no claim of an original combined Options menu. Its orphaned Add bookmark
entry is removed. Radio options follow the actual Presets/Manual/Auto order.

Confirmations now use centered text rows and fixed Play/OK and Back/Cancel
captions. They have no Yes/No cursor. Recording includes its quoted filename;
reset has the original Restore caption. Cancel preserves files/settings, or
discards only the pending recording as requested by that recording dialog.
Native tests exercise actual actions and pixel placements, including a disposable
file's delete/cancel sequence. Code/evidence: ui-flow-dossiers.json.

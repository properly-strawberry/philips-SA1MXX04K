# Recovered player and USB-host UI

2026-11-10. Evidence comes from the supplied SA1MXX04K/37 SB image, not from
another model's manual. Original SB SHA-256:
`f0565f2861cb5aaec78821ce2017c77ca222422a28e0a9ea72aa4db579a5d7ad`.
Addresses below belong to analysis-only mapped ELF images.

## Virtual code mapping

Observed: two large resident SB LOADs occur byte-for-byte at the start of each
VMI. For play mode, LOAD 0xea000/61336 bytes matches pvmi offset 0, and LOAD
0xf8f98/2668 bytes matches pvmi offset 0xef98. A subsequent 96-byte LOAD at
0xf9a50 differs. Host mode has the same base, with matching 54584- and 2758-byte
anchors and another differing 96-byte block.

Derived mapping: VMI offset zero maps to VA 0xea000. Preserve final-stage low
SB LOADs, map the VMI there, then overlay actual SB resident bytes. This makes
literal addresses, callback pointers and UI constructors resolve together.
Do not combine the earlier play preinitialization stage with this final stage.

`make map-vmi` reproduces both wrappers using tools/map_vmi.py and the shared
SB extractor's ELF writer. The Thumb analysis seed 0x100000 is not the SB entry;
the actual SB entry is 0xeaea8 (ARM-to-Thumb trampoline). The wrappers are not
flashable firmware. Their SHA-256 identities are:

- pvmi-mapped.elf: `094dadb25b312573973d0806c9838e787a2ba6e76405691d16223d6ef0c65ecf`.
- hvmi-mapped.elf: `2f86fbb3f643e6a45c9c11cee35cc61d4fe1ede6020b1753d5d26432b301c0d8`.

Ghidra discovered 4320 play functions and 1497 host functions with these seeds.
Those counts describe auto-analysis coverage, not a claim that every function
boundary or indirect call has been recovered.

## Main menu: five items without FM

Observed home constructor 0x152db4 builds one-based label IDs:

| Feature flag | IDs | English labels |
| --- | --- | --- |
| RAM 0x75a8 == 0 | 8,10,11,12,13 | Music, Recordings, Folder View, Settings, Now Playing |
| RAM 0x75a8 != 0 | 8,9,10,11,12,13 | Music, FM Radio, Recordings, Folder View, Settings, Now Playing |

Playback state can replace the final label with ID 14, Last Played. Subtract
one when indexing the extracted text tables. Presence of FM assets/text does
not mean this physical model has a radio. The owner confirms hers did not.
The flag's persistence/source and hardware detection are not yet established.

Desktop preference (owner correction after the first reconstruction): preserve
the five-dot rail and append internet Radio as a hidden sixth home stop after
Now/Last Played. That stop has no highlighted dot and is reachable with an empty
station table. This is an intentional desktop extension, not a recovered sixth
item for the no-radio hardware variant.

| Element | Rectangle / placement |
| --- | --- |
| Five-dot rail | resource 0.0 + theme, at (9,8) |
| Six-dot rail | resource 0.3 + theme, at (9,8) |
| Selection circle | resource 0.6 + theme, at (5,4 + index * spacing), 12x12 |
| Spacing | 11 pixels without FM; 9 with FM |
| Category icon | (48,0), 48x48 |
| Title | (28,48), 90x14 |
| Battery | (110,2), 16x11 |

Icon base is 0x40000009 + theme*36 + family*6; no-FM skips family 1. Resource
code 0x40000000 | group<<8 | index addresses two-level bitmap resources.
There are no adjacent carousel icons. Home paint is 0x152934, event handling
0x152982, and animation tick 0x152d3e. Next events 0x601/0x610 and previous
0x701/0x710 wrap through the enabled list. Icon frames advance through 1..5.
The timer field is five ticks; its wall-clock duration is not yet established.

Evidence: `ev_676690351d08b52f57c14d7a51c6571c7ce39cace6f280d4b1bd6875f2582e29`,
`ev_fc89580fd7a69df573d941061b18da006c5ccfd1c74d3d991dd51a4ae3973ec0`,
`ev_fe333bd5295ee49ec25760c7c9b30a0804bca720fcd2d2beb7aacd5092de1020`,
`ev_6dfe4671ed7d36bd9259511a2b0c031419f317b46c5321b3c9be7057e48d4b9a`.

## Lists and folder icons

Observed static menu builder 0x1505dc and browser painter 0x15d35c keep the
selected item in the center row. Neighbor rows are selection-1 and selection+1;
out-of-range rows remain empty. Text starts at y=8,29,50, and the selection bar
is resource 3.12 + theme at (0,25), 125x21. Text x=9 normally; folder text x=26.
The scrollbar is resource 3.25 + theme at x=126, y=selection*39/(count-1), 2x25.
Overflow rows use the original 12x14 ellipsis assets 3.21 + theme.

Folder browser mode 7 checks item flag bit 1 (0x02):

- Normal folder: resource 3.6 + theme at x=9, y=8 or 50.
- Selected folder: resource 3.9 + theme at (9,29).

The plus sign previously used by the desktop was an invented placeholder.
Folder paint entry 0x152344 supplies the actual folder, highlight and ellipsis
IDs to the browser painter. Static menus use checkmarks 3.2 + theme for the
selected row and 3.5 for an unselected row at x=107.

Evidence: `ev_0ae550f7928497cefa3f92e2f651ff6a0fb79519143f56654ff517a5041cf9ee`,
`ev_90c56cf681b7b9664746e00647ee738edadc0cd18525d7e0c25f125498fe1e08`,
`ev_0dfad8c8a76b02e3a4c2e305d57e07770da1c0c8c6f3233d71a93b1635fc34be`,
`ev_ebd6a55d39418303e2d2305b285da1becafaedf6c6c86242bbd4fd51f85da3c4`.

## Actual submenu tables

Observed 0x150264 constructs the following default tables. IDs here are
zero-based text indices, unlike the original builder's one-based IDs.

| Menu | Text indices / entries |
| --- | --- |
| Music | 14,15,16,17: All Songs, Artists, Albums, Playlists |
| Recordings | 80,81: Start voice recording, Recordings library |
| Settings | 108,119,134,147,172,6,178: Play mode, Sound, Display, Language, Information, Legal, Factory settings |
| Play mode | 109,110: Repeat, Shuffle |
| Sound | 120,129: Equalizer, Volume limit |
| Display | 104,136,137: Theme, Screen off, Rotate screen |
| Repeat | 111,112,113: Off, Repeat 1, Repeat All |
| Shuffle | 114,115: Off, All |
| Equalizer | 198,121..127: Off, Rock, Funk, Hip Hop, Jazz, Classical, Techno, Custom |
| Theme | 105..107: Dark, Blue, Pink |
| Screen off | 143..146: 40,50,60 seconds, Always |
| Rotation | 138,139: 0 degrees, 180 degrees |

Backlight choices exist as a separate state (10/20/30 seconds/Always), but the
recovered Display table does not link it. Audiobook labels also exist but are
not part of this default Music table. Language defaults to 23 entries with
Thai behind an additional flag; the desktop exposes all 24 as an adaptation.
Legal uses the original line table at resource 11.0.

Evidence: `ev_d9d47f699c3c577779927aa5cb7d424a8fc90ca2896c70be4ac1ce3bd6866e27`.

## Fonts and color interpretation

Observed font initializer 0x14c400 selects resource group 33 for the default
12-pixel font and 34 for the 14-pixel label font. A configuration byte at RAM
0x7598 equal to 4 substitutes groups 28/29; its regional meaning remains
unresolved. Bitmap color is not an instruction to invent a font.

Text renderer 0x148b4c advances x by the glyph header's width, with no added
pixel. The desktop previously added an extra pixel; this has been corrected.
The RGB565 driver 0x194c0e converts an ABGR color with:
`((c & 0xf8)<<8) | ((c & 0xfc00)>>5) | ((c & 0xf80000)>>19)`.
Theme setup 0x14c3a0 writes primary/unselected/selected/background colors:

| Theme | ABGR values |
| --- | --- |
| Dark | ffcec2bc, ffffffff, ff000000, ff000000 |
| Blue | fff8d6c2, ffffffff, ff9c613e, ff9c613e |
| Pink | ffead3fc, ffead3fc, ff9583d8, ff927be4 |

Evidence: `ev_155f94bca7ff3865e3aca32bbf4b0921789f03dd64051baeda4a0852a0fb5cc1`,
`ev_90aecbceaa74bee7e003127d5a5e6f7cd04adffebcd356c594b0365ab7ed3939`,
`ev_bf19bac49c41ff5e297c8603eef09ed780fecfe6aa47beee6c97879da6d5f633`,
`ev_318af31999d1cad9c5cffc909d534d6d28374932f1f8e7b76d75e96491cc89bf`.

## Found: the separate connected-to-PC screen

Observed host bitmap blitter 0x10565c indexes a pointer table at **0x10c054**.
It contains **48 bitmaps inside hvmi itself**, separate from all 322 shared
RSRC images. They were present in the supplied firmware.sb all along.
`tools/extract_host_ui.py` exports them to gen/host-ui with addresses and hashes.

| Host IDs | Contents |
| --- | --- |
| 0..5 | Six 16x11 battery states |
| 6 | 24x25 computer graphic |
| 7 | 27x10 player/USB graphic |
| 8,9 | Two strips spelling Do not disconnect |
| 10,11 | Two strips spelling Charging |
| 12,13 | Two strips spelling **FullyCharged**, without a space |
| 14..23 | Ten 26x2 connection-animation frames |
| 24..47 | 24 localized monochrome 128x36 charge-choice screens |

The status text pairs are RGB565, 98x6 plus 98x5: the words are prerendered,
which explains why ASCII/UTF-16 searches failed. Painter 0x1052f2 places computer
6 at (81,17), player 7 at (19,26), and connection frame at (50,30).
Status painter 0x1054c0 places the two strips at **(13,50)** and **(13,56)**.
Battery painter 0x10541e uses **(110,2)**. Above 94 percent it chooses host image
5 (the lightning battery), even for FullyCharged; the owner remembered a full
small indicator. The implementation follows the recovered branch.

Status selection uses Charging below 95 percent while charging, otherwise
FullyCharged; transfer events use Do not disconnect. Connection animation and
battery updates have separate callbacks. Exact event cadence is not recovered;
the desktop preview uses its own timing.

Evidence: `ev_e2e9d7c61e0f80c0239e9b15a1c18e709529563f4eeee9c7ff7b7f31fc4d4103`,
`ev_17ea151deb4475ec7e2b06a317dfa939e5df13c202072c5bbd55bc5b6ea4b262`,
`ev_caafa8330cb0ae7c2a24804dd8c77681c97dd8e5140ec449ab987958db5d71d7`,
`ev_9697bea3031ff719699105104b50e0dffdc1ad28a90ced6c344bac5dd3ead99b`.

## Playback, seek, recording and optional FM

Observed playback constructor 0x1537a6, metadata constructor 0x153c2c and
children 0x159ede/0x159ada establish the following layout:

| Element | Rectangle / resource |
| --- | --- |
| Song title | (5,3), 118x14, font group 34, theme primary color |
| Metadata row | (5,20), 118x14; artist/album cycle vertically in the original |
| Metadata icon | (5,22); artist resource 2.21+theme, album 2.18+theme |
| Metadata text | (17,20), 106 pixels, default 12-pixel font |
| Play/pause/seek icon | (5,35), resource 2.5/2.4/2.1/2.3 |
| Elapsed time | (18,37), 40x10, numeric font group 30 |
| Duration | (83,37), 40x10, right aligned |
| Progress bar | (5,48), 118x2, white fill |
| Current track / total | (5,55), (25,55), (31,55), five-pixel font group 32, zero-pad each number to four digits |
| Repeat, shuffle, EQ | (55,52), (70,52), (85,52), shown only when enabled |
| Battery | (107,51), 16x11 |

Progress backgrounds are ABGR ff2d2d2d/ffa87455/ffa390e9. Time/counter text is
white even on Pink. Clock formatter 0x19707c omits hours when zero and otherwise
includes them. Firmware title scrolling is a separate state machine, and the
artist/album row scrolls between child text widgets. The desktop approximates
that cadence with clipped artist/album sliding and horizontal metadata passes;
title coordination now follows its completion flag (see the follow-up below).

Observed seek handler 0x153296 increases its parameter to 4,5,10,20 at repeat
counts 1,10,20,40. Event handler 0x1532e6 distinguishes tap, hold/repeat and
release events (0x601/604/610, 0x701/704/710, 0x608/708, 0x804/808/810).
Original physical repeat timing and parameter-to-time conversion are not yet
fully traced. Desktop adaptation: release a short Up/Down or upper/lower shell press to
change track; after the subsequent key-generator pass, seek starts after 750 ms
and repeats every 300 ms using those step sizes in seconds. These milliseconds
are the desktop's 10-ms-per-kernel-tick interpretation, not a measured clock.
Shift+Up/Down remains a five-second seek, and clicking the progress bar maps
to the track duration. Seeking clamps to the file and works while paused.

`KEY_BEHAVIOR.md` records the later shared key-generator trace, including short
click on release, long-start/repeat/held-release families, volume/menu/EQ repeat,
long Back to Home, safe modal boundaries, and key-3 hold-to-shutdown versus
key-8 lock configuration. `key-input-dossiers.json` and `key-input-data.json`
preserve the code, assembly, bytes and Evidence IDs. The physical tick duration
is still unknown; the desktop now matches the owner's three-position slider.

Recording constructor 0x156e66 uses a filename at (5,13), 118x14; elapsed time
at (18,37), remaining time at (73,37), progress at (5,48), red record/pause icon
at (5,35), microphone resource 3.28 at (5,52), and battery (107,51).
Handler 0x157532 toggles recording with Select, pauses before confirmation,
and tracks saved-file changes. The desktop now follows those placements with
waveIn WAV recording and Save/No, but estimates remaining time from free disk
space with a four-hour display cap. Its file naming, codec and capacity display
are desktop adaptations. Raw firmware recording/DSP logic is not lifted.

Optional FM constructor 0x154ec0 uses **vertical** scale resource 2.24+theme
at (5,0), 16x63; arrow 2.27+theme at x=10, y=55-floor(percent*55/100).
The 24-pixel frequency font is group 31, centered in (26,20), 75x24. Literal
FM appears at (106,25), not MHz. Preset text occupies (26,47), 75x14, and the
battery is (107,51). Scale maximum is 20500 kHz above 87500 kHz.
The desktop uses these resources/placements for the explicitly requested
internet-radio adapter; the station's editable name replaces preset text.

Evidence: `ev_8e9c8d7235f41f9af2e121615d72fe7127a69ab68b31686ae2c1507e8c77ee28`,
`ev_0e1187b386340d508a60c28a7bc9b4efbc321f3601990b576cd581a0585ece3d`,
`ev_99bbda92dd3ce31518880fa1f5ec55330414842783831ab72a6cca8ef2825b81`,
`ev_a8d7c778ac527db1d15ce8066f66f13bffc7747e8455917b1b7784a4e3a4ff35`,
`ev_9c93b37f12db8a217dd6c0aa228f779e90ed62da460b93cdbebc6b37386af1a7`,
`ev_013c2d7dd03b344eecbe2f8c0917a9045dd38b3e2046333b019faf7e94bff036`,
`ev_3d6280046ed9f636d08b3e7a8c7d355d0f45f05532adbdb474b81d6eccc96740`.

## Shutdown animation

Observed constructor 0x15876c creates the one-based label 0xe6 (Bye) in a
centered text widget at (2,25), 124x14, white on black. It initializes its timer
count to 50. On its first tick, handler 0x15863e destroys that text widget and
sets the timer count to 25. It then reads group-8 bitmap frames 0..6, writing
98x46 pixels at (15,9), and posts a completion event after the last frame.
The bitmaps show a ring of dots expanding toward the screen edges; these are
the original shutdown animation, not a generic loading spinner. Group 8 frame
7 is blank; the handler's seven-frame loop does not draw it before completion.

Desktop timing is 1000 ms for Bye and 500 ms per bitmap, preserving the
observed 2:1 hold/frame ratio at the requested slower pace. The window closes
only after all seven frames (4500 ms total). Firmware tick duration is still
unknown, so those wall-clock timings are an adaptation. Native tests check each
original frame at the recovered coordinates and the completion boundary.

Evidence: `ev_5de526df939487756eb8a2b7f050b2cc28d765151d78d33dfcd6c475280f2725`,
`ev_6aef8fb6d24255da5b404333997c9add4ee7b606cce53e4ced3f6ed60aee7e0f`.
Code and identity are retained in shutdown-dossiers.json.

## Metadata, volume, Updating and Custom EQ refinements

Observed metadata constructor 0x159ada and tick 0x1597cc create a 14-pixel
viewport and two text/icon rows. The outgoing and incoming rows move upward
one pixel per timer update; long metadata makes a horizontal pass before the
vertical change. Original tick processing waits for title-scroll completion.
Desktop now reproduces the clipped two-row motion and metadata sequencing,
with 100-ms steps. The later title/modal-flow pass below replaces the previous
independent looping title marquee and restores title-completion gating.
Evidence: `ev_99bbda92dd3ce31518880fa1f5ec55330414842783831ab72a6cca8ef2825b81`,
`ev_d6d4384906a9a01420fbe0796793f9dbf76c790b42b77e2494b1860f78ba40b8`.

Volume paint 0x15a078 uses group 4: active bar 0, inactive bar 1+theme at
(17+3*i,26); thumb 4 at (14+3*position,21); cap 8 at (17+3*limit,21);
minus 6 at (1,26), plus/blocked-plus 7/5 at (115,26). The UI range is 0..31.
Desktop retains percentage settings for compatibility and maps them to these
32 positions. Limit editing previews audio changes, Select commits, Back
cancels. Popup expiry remains a 1400-ms desktop choice.
Evidence: paint `ev_1584f6ed759d08f3be393792e24cbaf1f496d79fa35aa837abb92b357c8e32e1`,
event `ev_1cf87bc7b0ae051c40ab90f0e2e8299b0979fd7ead47d584b96af565ffa647f9`,
constructor `ev_46d2ffcc162c61ca0519c340144f8bc411c884e16e21aebe4615db386f1ad6e8`.

Recheck of root dispatcher split 0x14ce9e supersedes the earlier guessed
Updating layout. Assembly resolves stack arguments that the split-function
pseudocode leaves unaffiliated: spinner rectangle (57,8,16,16), original label
0xe5 in (5,29,118,14), percent in (0,44,128,14), and strip (5,59,118,2).
With default 12-pixel font 33, glyphs are centered at y30/y45. Spinner
constructor 0x196978 and tick 0x15bd10 use group 5 frames 0..7 with period
argument 0x5a. Desktop interprets this as 90 ms; the actual tick unit is not
proven. The requested synthetic scan lasts three seconds. Dispatch 0x14d106
uses whole group-7 strip frames 24..34, initially white with gray advancing
left to right; labels stay zero for phases 0/1, become 20..80 for 2..8, remain
80 at phase 9, and reach 100 at phase 10. Library scanning also uses group 5,
without a fabricated percentage. Group 8 is the shutdown animation.
Evidence: `ev_d9bbfc07dfa6d73e1cee562ea19acc227d2ac1baa6dcb0e87b96ebcf07c24abc`,
`ev_64334e77f0530c447eddd8b333211b250b5e644608767499e54dac24638bdd9f`,
`ev_980b7a28ea7f50830fbb68670e1894bd57ea9bbbc060d96c3651ebecdd672d9e`,
`ev_655a43170f2c5f314915aa1531df0d505c9b38535931c0ea66926f9f61d4f574`.

Custom EQ event 0x15423c changes gains in units of two, bounded to +/-10;
Select advances through five bands and exits, Back goes to the previous band
or exits from band zero. Paint 0x15441a uses background 6.theme, selected
column 6.(4+theme) at (12+22*band,0), unit bars 6.3 at
(15+22*i,27-sign*k*4), and thumb 6.7 at
(12+22*band,25-(gain/2)*4). Desktop now follows those graphics/controls.
Original DSP units/coefficients remain unknown; desktop interprets gains as dB.
Evidence: `ev_795f71deaae77c82692f723a7eb6a5bfe2475231b0e3dd4f6fea580238885cb7`,
`ev_3645b9079663c7f038bf8df78aa66b575e73ec9773650c9b14ed6d23210d9722`.
Code and identity: ui-refinements-dossiers.json; metadata code is also retained
in playback-recording-dossiers.json.

## Lock/unlock feedback

Observed root event handler 0x14c588 creates dialog class 0x1020 over
(0,0,128,64), with timer period 25 and kind 2. Constructor 0x15802a reads
lock-state bit 4 and creates a 24x37 image at (52,11), using closed resources
3.15..17 or open 3.18..20 depending on theme. Painter 0x157be0 fills the
entire dialog bounds with the palette background. There is no text label and
no separate moving padlock frame sequence in these callbacks. The existing
narrow overlay and Hold MENU caption were desktop inventions and are removed.

Timer 0x157c10 starts from count 4, decrements on four callbacks and posts
its dismissal event on the fifth. Event handler 0x157c36 dismisses on event
0x367, updates the closed/open bitmap on 0x804/0x828 and routes blocked keys.
Root dispatch clears/sets the lock flag and can re-create the dialog. Derived
behavior: temporarily replace the whole display, then restore the underlying
page without unlocking controls. A blocked key can show the closed padlock
again. The desktop follows that flow, uses the open bitmap on unlock, cancels
pending transport holds and clears stale volume/notices. Its 1250-ms duration
interprets 125 firmware timer units as 10 ms each; that unit is unproven.

Native lock tests compare all 8192 pixels with a clean background plus the
original sprite in all three themes, check both states, expiry restoration,
blocked volume/click retrigger, transport cancellation, next-input dismissal
and rotated output. Windows shell execution remains unverified here.

Evidence: constructor
`ev_eda39f490b78e6ffe4d4ecc444a51291b4dca1ed54f31aaaaa396cc8ade64eba`,
paint `ev_5f04a62e4f07545a96bc989092bc97c4a83a0fab16bc5a173a660868cb9cc019`,
timer `ev_629b94dea149e94115add7a491465d2fdffd622231a1553a01fa1c27a4a1c06d`,
event `ev_63befebf663af1dcb1cd7ffa2e71f6b8a47a959c4dc40ecadaa91c818db0233a`.
Code/assembly and binary identity are retained in lock-dossiers.json; root
code is in ui-function-dossiers.json. REA session closed normally.

## Title, playlist and confirmation follow-up

Observed title constructor 0x159ede creates a 118x14 text child and a hidden
12x14 theme ellipsis image. Reset 0x159e28 restores the initial width/x and
hides that image. For text wider than 118 pixels, it computes a glyph-boundary
prefix of at most 106 pixels and clears the metadata-enable global. Tick
0x159d3c holds ten callbacks, then decrements the text x position one pixel
per callback until x+text width is negative. It resets x to zero, changes text
width to the saved prefix, unhides the ellipsis and sets the metadata-enable
global. Thus it starts with clipped text, makes one complete pass, then stops
on prefix+ellipsis. The desktop now follows this state order and gates metadata
until completion; new titles and fresh playback-page entry restart it.
Its 100-ms update interval is still a desktop interpretation.

Evidence: reset `ev_d39cc3bba82295844514d9f8e5b1a6ec89dad326a5aeb94d4c6031b677ddfcfb`;
constructor/tick and metadata code remain in playback-recording-dossiers.json.
The reset dossier is also preserved in ui-flow-dossiers.json.

Observed menu builder 0x150264 state 0x12 contains one-based IDs
0x1f/0x20/0x21: Playlist-on-the-Go, Add To Playlist, Remove From Playlist.
Preselection handler 0x150904 requires the current playback queue for Add.
The state-0x12 branch at 0x14f8c8, outside Ghidra's recovered function body,
sets playlist mode and sends state 0x1f for Add or 0x23 for its browser.
Bytes and explicit Thumb decoding are preserved in menu-branch-static-check.json
(`philips-menu-branch-static-check`); this branch check is not a runtime trace.
Root 0x14d4b8 creates class 0x1029 for add/remove. Constructor 0x15b3e0
builds its actual confirmation; event 0x15af5e performs the change on
Select and returns to the playlist menu/browser. The desktop now has this
original three-entry menu and remove-song route. Add uses the current queued
track, not the row selected in an unrelated browser.

The combined M/Options screen is a desktop shortcut: no matching combined
menu was found in the recovered static tables. It is now restricted to a
current queued track on the playback page, exposes Play Mode, Sound, the
applicable Add or Remove action and Delete, and omits orphaned bookmark
creation. That eligibility/order is a desktop choice, not a recovered physical
Options sequence. The original audiobook feature gate remains unresolved.
Radio options use observed state-1 table order Presets/Manual tune/Auto tune;
the optional recording entry is omitted for the internet adapter.

Generic dialog layout 0x157de8 puts one/two/three prompt rows at y17,
y6/y28, or y1/y17/y33 for confirmation kind 1. Its footer is y49, height 14:
left (2,49,83,14), right (86,49,40,14), or 60-pixel widgets starting at x2/x66
for language IDs 18/19/20/23. Text IDs are 201/202 (Play OK / Back Cancel),
with left/right alignment and white text. There is no selectable Yes/No list.
Factory handler 0x158240 sets the two original reset strings and substitutes
text 203 (Restore) in the left footer. Recording handler 0x157532 explicitly
uses Save recording? at y6 and a quoted filename at y28. Its cancellation
sets flag +0x45; cleanup 0x157288 then calls 0x163276 on the pending object.
The desktop maps that to discarding only the pending WAV, while Play saves it.

Playlist constructor 0x15b3e0 uses Add/Remove, quoted track name and
"to/from playlist?". English rows are y1/y17/y33. Language branches move
those rows as recorded in its code; the desktop ports those branches. Long
rows currently use the desktop generic marquee; their native cadence and all
regional multiline behavior are not verified. Success/error notice lifetimes
and specialized layouts also remain incomplete. Generic notices now clear the
whole display and omit the invented checkmark.

Evidence: menu eligibility `ev_799cbd860b1f8db86e54a8116d2cd7b9ba4afef14953a3fd6a3db607fa849b7c`,
playlist constructor `ev_904742674a8b277c84405413733db1487ce1ecb75caf0c21df50acbf42c2cc83`,
playlist event `ev_7e943f399b4de6d496c6086177b628b56e8042a2b856b579091db3050eb9dc63`,
layout `ev_f0ee4bc0fe0095e1bf9995ad9592d11e82d8c4c2752ffe4bcca7ee6e493db98b`,
factory reset `ev_1bf99b548c2992ae4edcd81cb64cfd4b0b322183592653d826204f224f0897b8`,
recording cleanup `ev_f93a69f8e47a2ed66bfe9021d59fed15b813204e7ebbe5fa38b7edaf097ad077`.
Saved code: ui-flow-dossiers.json, lock-dossiers.json and earlier playback dossiers.

Validation: native tests verify title hold/pass/ellipsis/stop/reset and delayed
metadata in all three themes, prompt/footer pixels, no action on navigation or
prompt clicks, playlist commit/cancel and original routes, recording save/
discard, factory reset/cancel and a disposable file guarded by confirmation.
They do not compare a physical runtime or verify Windows hardware services.
REA session closed normally after preserving the compact dossiers.

## Saved evidence and remaining limits

ui-function-dossiers.json, ui-drawing-dossiers.json and host-ui-dossiers.json
and playback-recording-dossiers.json, ui-refinements-dossiers.json and
lock-dossiers.json and ui-flow-dossiers.json retain Ghidra pseudocode/assembly and
evidence identity. These are decompiler
outputs, not original source or guaranteed compilable C. ui-deep-analysis.rea.json
retains the earlier play-mode query bundle. REA's atomic export hit a staging
identity failure on this host mount; the complete staged JSON was validated and
copied to that filename, then its owned staging directory removed. Later queries
are preserved in the compact dossiers. Both REA sessions were closed normally.

Still unknown: exact hardware key repeat/timer cadence, persistent feature flag
sources, service-menu entry sequence, remaining playback/recording state logic,
original DSP coefficients and all power-management behavior. These
findings substantially constrain the reconstruction; they do not establish
firmware emulation or complete behavioral/pixel lockstep.

## Browser cursor on return from playback

A focused recheck of A -> B -> Back supports keeping A highlighted. Save
callback 0x15228e and playback-start helper 0x15cd92 preserve the browser row
in a per-depth array at RAM 0x10b4. Playback Back (0x1532e6) requests browser
page 0x23 when its prior context remains valid. Constructor 0x15cf9c restores
that saved row; it does not substitute the current playback index. Queue
Next/Previous requests go through 0x15e66e/0x15e6a8 to a separate worker.

This is a static, derived conclusion for the saved-browser path, not a device
runtime observation. No desktop cursor behavior was changed. Full evidence
and limitations: BROWSER_CURSOR.md and browser-cursor-dossiers.json.

## Full-display error/message recheck

Error creator 0x14e318 constructs class 0x1020 over (0,0,128,64); dispatcher
0x14dae0 case 0x12 fills File format / not supported! through original labels
0x66/0x67. Generic kind-0 row rectangles start at y12/y40, or y6/y25/y44
for the language-21 three-row case. Text property 0x109=1 at 0x14e86c computes
vertical centering; with default 12-pixel font in 14 pixels, glyph y is row y+1.
This also corrects the previously one-pixel-high confirmation/footer drawing.

The desktop partial error repaint and in-row radio failure are replaced by a
shared full-screen dialog renderer. The complete current-page audit, exact
evidence IDs, pixel tests, input/layer behavior and remaining error-category
limitations are in DIALOG_SCREEN_AUDIT.md. Code is retained in
dialog-error-dossiers.json; REA closed normally after saving it.

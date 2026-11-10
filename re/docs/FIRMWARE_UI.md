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
| Current track / total | (5,55), (25,55), (31,55), five-pixel font group 32 |
| Repeat, shuffle, EQ | (55,52), (70,52), (85,52), shown only when enabled |
| Battery | (107,51), 16x11 |

Progress backgrounds are ABGR ff2d2d2d/ffa87455/ffa390e9. Time/counter text is
white even on Pink. Clock formatter 0x19707c omits hours when zero and otherwise
includes them. Firmware title scrolling is a separate state machine, and the
artist/album row scrolls between child text widgets. The desktop approximates
that cadence with a three-second artist/album switch and horizontal marquees.

Observed seek handler 0x153296 increases its parameter to 4,5,10,20 at repeat
counts 1,10,20,40. Event handler 0x1532e6 distinguishes tap, hold/repeat and
release events (0x601/604/610, 0x701/704/710, 0x608/708, 0x804/808/810).
Original physical repeat timing and parameter-to-time conversion are not yet
fully traced. Desktop adaptation: release a short Left/Right or shell press to
change track; after 450 ms, seek every 250 ms using those step sizes in seconds.
Shift+Left/Right remains a five-second seek, and clicking the progress bar maps
to the track duration. Seeking clamps to the file and works while paused.

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

## Saved evidence and remaining limits

ui-function-dossiers.json, ui-drawing-dossiers.json and host-ui-dossiers.json
and playback-recording-dossiers.json retain Ghidra pseudocode/assembly and
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

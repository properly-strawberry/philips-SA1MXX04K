# Philips GoGear Mix desktop player

Portable Windows x64 player. Unzip the entire package, put music in `Music`
beside `philips.exe`, then launch `philips.exe`. Nested folders are supported.
Keep the four FFmpeg DLLs beside the executable. No installation is needed.

The layout follows the physical player's folders:

```text
philips.exe
Music/
Recordings/Voice/
Playlists/Playlist-on-the-Go.m3u8
Playlists/radio.json
Playlists/radio.example.json
```

This is a new desktop implementation with original extracted firmware graphics
and bitmap font. It does not emulate the firmware or claim original UI lockstep.
The shell is drawn from the product photo in `ref/` and arranged horizontally.
The EXE/window icon is the original `ref/DEVICON.FIL` (12 icon variants).
The five home categories animate in Dark, Blue and Pink themes, with bubbles
on the left: Music, Recordings, Folder View, Settings, Now/Last Played.
Radio is a sixth, hidden home item after Now/Last Played. Keep pressing Down,
or press Up once from Music. It has no highlighted bubble on the five-dot rail.
Home geometry, centered list selection, folder icons and submenu tables now
follow recovered firmware code. Menus use original graphics, both bitmap font
sizes and all 24 language tables. Other desktop behavior remains an adaptation;
this is not a claim of complete pixel or behavioral lockstep. Boot runs at half
the first build's speed (170 ms per frame), followed by Updating… for three
seconds. Updating shows the original dot spinner at the top, Updating… in its
centered text row, the percentage underneath, and the original progress-strip
frames along the bottom edge.
Shutdown shows Bye, then the original expanding ring of dots. The slower
desktop sequence holds Bye for one second and each animation frame for 500 ms
(4.5 seconds total).

The playback screen follows recovered title/metadata/time/progress/counter
positions and includes the original 10-pixel time and 5-pixel counter fonts.
The track counter pads both numbers to four digits, for example 0001/0032.
Artist and album slide vertically between rows, clipped to the metadata area;
long metadata scrolls horizontally before the next row enters. A long title
holds, makes one full horizontal pass, then stops on its prefix plus the original
ellipsis. Artist/album motion waits until that title pass finishes. Timer
milliseconds remain desktop interpretations. Radio uses the original
vertical scale and 24-pixel digits; recording uses the original filename,
time, microphone and battery placements. Animation cadence and desktop audio
services remain adaptations.

## Controls

- Click the shell buttons or the screen's menu rows.
- Up / Down: navigate; while playing, tap for previous / next track or hold
  to seek with increasing speed. The upper/lower face buttons do the same.
- Left: Back. Right, Enter or Space: select / open / play or pause.
  The left face button is Back; the right face button is Play/Pause/Select.
- Shift + Up / Down: seek five seconds; click the progress bar to seek.
- Backspace or Escape: Back. Home: main menu.
- Plus / Minus: volume. Mouse wheel: selection or playback volume.
- S: shuffle. R: repeat off / all / one. F5: rescan Music.
- M: current-track shortcuts while on the playback screen, or radio options.
  The track shortcut menu is a desktop addition: Play Mode, Sound Settings,
  Add To Playlist or Remove From Playlist as appropriate, and Delete.
- L or the separate top-edge slider: lock/unlock. It has no power-off action.
  Lock/unlock briefly replaces the whole screen with the original closed/open
  padlock, then restores the current page. Locked buttons show the padlock again.
  Feedback lasts 1.25 seconds, a desktop interpretation of firmware timer ticks.
- Separate Vol- / Vol+ shell keys remain available; keyboard Plus / Minus too.
- F9: original Connected-to-PC screen. Up/Down cycles FullyCharged, Charging
  and Do not disconnect. F10: localized Charge & transfer / Charge & play preview.
- Settings includes play modes, sound/EQ, volume limit, theme, display timers,
  screen rotation, language, information and a settings-only factory reset.

All songs scans Music recursively. Artists and Albums use FFmpeg metadata.
Folder view browses individual directories, with folders first and both folders
and files sorted alphabetically without regard to case. Artist and album lists
also sort without regard to case. Playlists retain their saved order.
Back from playback restores the list's remembered selection. If A was selected
when playback opened, advancing to B does not move that saved cursor to B.
The traced firmware return path behaves this way too; details are in
`re/docs/BROWSER_CURSOR.md`.
Selecting a song in Folder view queues that directory's songs. All songs queues
the scanned library. Playback continues while browsing. Music > Playlists contains the recovered Playlist-on-the-Go, Add To Playlist
and Remove From Playlist entries. Add uses the current queued track; Remove
opens the playlist song list. Both ask for confirmation. The M shortcut also
provides the applicable playlist action for the current track. The M3U8 file is editable
and stores paths relative to Playlists. Unsupported or broken
files show a full-screen error; use Next to continue or Back to return. Visible
messages cover the title, track counter and battery too. Clicking a message
does not seek or activate controls behind it. Folder symlinks/reparse points are
skipped. Formats depend on FFmpeg; the browser includes MP3, FLAC, WAV, Ogg,
Opus, M4A/AAC, WMA, AIFF, APE, WavPack, ALAC and M4B.

Settings and the last playback position persist to `philips.ini` beside the executable.
Media is decoded in a worker with a bounded buffer and resampled to stereo
48 kHz signed 16-bit PCM, played through Windows waveOut. Playback reads files;
Delete in the M shortcuts removes the current file only after Play/OK confirmation.
Confirmation screens use original centered prompt rows and Play/OK plus Back/Cancel
captions, not a selectable Yes/No list. Up/Down does not change the answer.
Play/Right/Enter confirms; Back/Left/Escape cancels. The footer captions are also
clickable; clicking the prompt itself does nothing.
Five-band EQ applies real peaking filters with headroom; presets approximate
the named styles and are not recovered firmware DSP coefficients. Custom EQ
uses the original graphic editor: Up/Down changes gain, Select advances to the
next band (exits after the fifth), Back returns to the previous band (exits
from the first). The volume popup uses the original full-screen 0..31 bar widget
and cap marker. The volume-limit editor previews changes; Select saves and
Back cancels.
DRM playback, real FM hardware and the firmware service menu remain outside
this desktop player's current scope.

## Internet radio through virtual FM frequencies

This physical model had no FM radio. The requested internet-radio adapter is
always available as the hidden sixth home item, including with an empty table.
It maps virtual 87.5-108.0 MHz positions to HTTP(S) audio streams. Edit the file:

```json
{
  "stations": [
    {"mhz": 99.5, "name": "My station", "url": "https://my-station.example/live.mp3"}
  ]
}
```

The distribution always contains an empty radio.json and a radio.example.json.
Copy entries from the example or supply your own; example stream availability
is not guaranteed. Up/Down tunes in 0.1 MHz steps; Shift + Up/Down jumps
between configured stations. A mapped frequency starts after a 350 ms tuning
pause. Unmapped frequencies remain silent. Presets and Auto tune list the
configured table. Radio options follow the recovered Presets / Manual tune /
Auto tune order; Auto tune asks for confirmation before showing the configured
list. F5 reloads JSON as well as the media library. Duplicate or
out-of-range frequencies and non-HTTP(S) URLs are rejected.

Radio uses the same FFmpeg worker with cancellable network reads and bounded
timeouts. Network streams are opened only when the user tunes a configured
station. The package and application create missing templates without replacing
existing radio.json, personal playlists, music or recordings. Portable ZIPs
contain clean templates, never the developer's personal station/playlist data.

## Voice recordings

Recordings > Start voice recording opens the default Windows microphone at
22.05 kHz mono PCM. Play pauses/resumes; Back stops into Save recording? with
the quoted filename and original Play/OK and Back/Cancel footer. Play saves a
timestamped WAV under Recordings/Voice; Back/Cancel discards only that pending
recording. Closing the program keeps an active
recording. A microphone or storage error preserves a recoverable partial WAV
where possible and reports the error. Windows microphone privacy settings apply.
Saved recordings appear in Recordings Library after rescanning. Playback options
can delete a selected file only after the on-screen Delete? confirmation.

The separate original PC-connected graphic was found inside host-mode code,
in a 48-image table outside RSRC. F9 renders its actual computer/player images,
battery and prerendered FullyCharged text at the recovered coordinates.

## Build from Linux

Requires MinGW-w64 x64 POSIX C++, Python 3 and OpenSSL for initial SB extraction.
No system installation is performed by the build. Obtain the FFmpeg LGPL shared
Windows SDK described in `third_party/FFMPEG.md`, unpack its include/lib/bin into
`third_party/ffmpeg/`, and regenerate the firmware outputs if absent (see
`AGENTS.md`). Then run:

```sh
make -C philips package
```

The app uses libavformat, libavcodec, libswresample and libavutil directly.
It does not spawn ffmpeg.exe. Assets are embedded into philips.exe.
Generated files, the dependency SDK and dist outputs are ignored by git.

Diagnostic command lines (paths may contain Unicode):

```text
philips.exe --decode-test INPUT OUTPUT.s16le
philips.exe --decode-seek-test INPUT OUTPUT.s16le SECONDS
philips.exe --music TEST_MUSIC --screenshot OUTPUT.png
philips.exe --music TEST_MUSIC --smoke-test REPORT.txt --screenshot OUTPUT.png
```

The decode test writes raw stereo 48 kHz PCM using the same decoder as playback.
The UI smoke test expects at least two tracks. Diagnostic
screenshots and reports are explicit outputs, not normal application behavior.

From the repository root on Windows, run `powershell -ExecutionPolicy Bypass
-File tools/test_windows.ps1` for generated fixtures, sample-exact PCM comparisons
against the SDK's FFmpeg executable (WAV, FLAC, MP3, AAC, Opus and mono 22.05 kHz),
invalid-file rejection, sample-exact one-second seeks in WAV/FLAC, and the UI
smoke test. Results go to `gen/player-tests/`.

`make test-ui` runs the actual portable interface implementation natively on
Linux with fake audio/microphone services. It covers navigation, all 24 language
tables, 18 icon/theme screens, original folder/USB pixels at recovered coordinates,
Updating layout/percentages/timing, finite title scrolling and metadata gating,
modal confirmation captions/actions, original playlist routes, file-delete
cancellation, fullscreen lock/open pixels/timing, zero-padded counters, hidden radio navigation,
whole-display failure/notice pixels and pointer click guards in all three themes,
play controls, settings, display timers, JSON/MHz
tuning, save/discard recording flow, lock, a 5,000-song library and EQ DSP checks.
It emits rendered PPM screen evidence under gen/ui-tests/fixture. These tests do
not establish real microphone capture or live-stream connectivity on Windows.

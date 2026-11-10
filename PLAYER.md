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
Home geometry, centered list selection, folder icons and submenu tables now
follow recovered firmware code. Menus use original graphics, both bitmap font
sizes and all 24 language tables. Other desktop behavior remains an adaptation;
this is not a claim of complete pixel or behavioral lockstep. Boot runs at half
the first build's speed (170 ms per frame), followed by Updating… for three
seconds. Shutdown shows Bye for 1.2 seconds.

The playback screen follows recovered title/metadata/time/progress/counter
positions and includes the original 10-pixel time and 5-pixel counter fonts.
Artist and album alternate on the metadata row. Radio uses the original
vertical scale and 24-pixel digits; recording uses the original filename,
time, microphone and battery placements. Animation cadence and desktop audio
services remain adaptations.

## Controls

- Click the shell buttons or the screen's menu rows.
- Up / Down: select items; while playing, change volume.
- Enter: select / open / play or pause.
- Space: play or pause.
- Left / Right while playing: tap for previous / next track; hold to seek,
  with increasing speed. The shell's previous/next buttons behave the same way.
- Shift + Left / Right: seek five seconds; click the progress bar to seek.
- Backspace or Escape: parent folder / home. Home: main menu.
- Plus / Minus: volume. Mouse wheel: selection or playback volume.
- S: shuffle. R: repeat off / all / one. F5: rescan Music.
- M or the lower shell button: playback/radio options. Hold the lower button
  for 0.6 seconds, or press L, to lock/unlock controls.
- F9: original Connected-to-PC screen. Up/Down cycles FullyCharged, Charging
  and Do not disconnect. F10: localized Charge & transfer / Charge & play preview.
- Settings includes play modes, sound/EQ, volume limit, theme, display timers,
  screen rotation, language, information and a settings-only factory reset.

All songs scans Music recursively. Artists and Albums use FFmpeg metadata.
Folder view browses individual directories.
Selecting a song in Folder view queues that directory's songs. All songs queues
the scanned library. Playback continues while browsing. Playlist-on-the-Go can
be added to and removed from through playback options; the M3U8 file is editable
and stores paths relative to Playlists. Unsupported or broken
files show an error; use Next to continue. Folder symlinks/reparse points are
skipped. Formats depend on FFmpeg; the browser includes MP3, FLAC, WAV, Ogg,
Opus, M4A/AAC, WMA, AIFF, APE, WavPack, ALAC and M4B.

Settings and the last playback position persist to `philips.ini` beside the executable.
Media is decoded in a worker with a bounded buffer and resampled to stereo
48 kHz signed 16-bit PCM, played through Windows waveOut. Playback reads files;
Delete in playback options removes a selected file after confirmation.
Five-band EQ applies real peaking filters with headroom; presets approximate
the named styles and are not recovered firmware DSP coefficients. Custom EQ
allows editing all five bands with Left/Right (band) and Up/Down (gain).
DRM playback, real FM hardware and the firmware service menu remain outside
this desktop player's current scope.

## Internet radio through virtual FM frequencies

This physical model had no FM radio. Internet Radio appears as a desktop extra
at the end of Settings only when Playlists/radio.json contains stations.
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
is not guaranteed. Left/Right tunes in 0.1 MHz steps; Shift + Left/Right jumps
between configured stations. A mapped frequency starts after a 350 ms tuning
pause. Unmapped frequencies remain silent. Presets and Auto tune list the
configured table. F5 reloads JSON as well as the media library. Duplicate or
out-of-range frequencies and non-HTTP(S) URLs are rejected.

Radio uses the same FFmpeg worker with cancellable network reads and bounded
timeouts. Network streams are opened only when the user tunes a configured
station. The package and application create missing templates without replacing
existing radio.json, personal playlists, music or recordings. Portable ZIPs
contain clean templates, never the developer's personal station/playlist data.

## Voice recordings

Recordings > Start voice recording opens the default Windows microphone at
22.05 kHz mono PCM. Play pauses/resumes; Back stops into the original Save
recording? confirmation. Yes saves a timestamped WAV under Recordings/Voice;
No discards only that pending recording. Closing the program keeps an active
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
tables, 15 icon/theme screens, original folder/USB pixels at recovered coordinates,
Updating timing, play controls, settings, display timers, JSON/MHz
tuning, save/discard recording flow, lock, a 5,000-song library and EQ DSP checks.
It emits rendered PPM screen evidence under gen/ui-tests/fixture. These tests do
not establish real microphone capture or live-stream connectivity on Windows.

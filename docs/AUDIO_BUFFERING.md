# Playback buffering audit

2026-11-10. Desktop implementation analysis; no firmware behavior claim.
The owner reports occasional fractional-second hiccups during steady playback,
without navigating. The specific cause on her Windows machine is unmeasured.

## Source evidence and change

The previous `Audio::run` submitted only the current decoded frame to a free
waveOut slot, then polled for another slot. Each slot allocated 4096 stereo
frames, but AAC/MP3 frames commonly supplied only 1024/1152 source samples.
With six slots, that could leave just 128-157 ms queued at common sample rates,
rather than the 512 ms implied by allocated capacity. The same thread performed
potentially blocking FFmpeg reads, resampling and device refills. There was no
explicit startup prefill. This is a plausible starvation mechanism; it does
not establish that every reported hiccup was an application underrun.

The updated path is:

```text
FFmpeg decoder/resampler worker
    -> bounded raw stereo PCM ring: 96,000 frames / two seconds / 384,000 bytes
    -> independent output worker: EQ and software volume
    -> six prepared waveOut buffers: 2048 frames each / 256 ms total
    -> Windows audio device
```

- Output requests full 2048-frame blocks across arbitrary codec packet
  boundaries. Only EOF/error tails produce a shorter block.
- Startup and seek wait for 24,000 frames (500 ms), or the complete remaining
  audio at EOF. All available device slots are filled while paused before
  restarting the device. Local-file decoding can build this reserve faster
  than real time; a live stream may need roughly that much source time.
- A slow read cannot block the output thread. When the device queue genuinely
  empties before the source is exhausted, an underrun is counted and output
  pauses to rebuild the reserve rather than repeatedly restarting tiny blocks.
- A Windows event wakes the output worker when device buffer state changes.
  A short timeout also services commands and newly decoded PCM. Prepared
  headers are reused; they are no longer prepared/unprepared for every packet.
- The output thread requests above-normal priority. Metadata scans run below
  normal priority. There is no global timer-resolution or process-priority change.
- Pause retains queued PCM. Seek clears the device and software queues, resets
  EQ state, interrupts an old read and rejects samples from older seek revisions.
  Track change and shutdown interrupt and join the reader. Its state remains
  alive during FFmpeg cleanup, including interrupt callbacks.
- EQ and volume are applied just before device submission, so the two-second
  read-ahead does not itself delay those controls. Up to the device's 256 ms
  already queued may retain earlier settings.

The startup prefill and event handling follow Microsoft's documented
[pause/write/restart sequence](https://learn.microsoft.com/en-us/windows/win32/multimedia/stopping-pausing-and-restarting-playback)
and [event callback procedure](https://learn.microsoft.com/en-us/windows/win32/multimedia/using-an-callback-to-process-driver-messages).
Header reuse permits buffer lengths no larger than the original prepared
allocation; see [waveOutPrepareHeader](https://learn.microsoft.com/en-us/previous-versions/dd743868%28v%3Dvs.85%29).
[waveOutReset](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-waveoutreset)
returns queued buffers and resets the device position on seek.

## Verification and limits

`make test-audio` executes the actual portable ring with synthetic sources:
packet sizes 1/17/1024/1152/4096/20000 frames, wraparound, exact stereo sample
ordering, EOF tails and bounded memory. A test holds the producer blocked and
consumes over a second of buffered PCM without requiring new source data.
Other cases cover an interrupted read, multiple seeks, seek after EOF, seek
while the producer is blocked on a full ring, stop while reading/full, and
draining a short tail before reporting a decoder error.

The native audio and interface tests pass. The audio tests also pass AddressSanitizer
and UndefinedBehaviorSanitizer. LeakSanitizer cannot run under this environment's
tracing setup; leak detection was disabled for that sanitizer run.

The native interface/controller regression suite remains separate. Native
tests do not exercise FFmpeg or Windows output. Windows validation in
`tools/test_windows.ps1` now adds silent real-device playback of generated WAV,
FLAC, MP3, AAC, Opus and mono 22.05-kHz fixtures to the existing decoder PCM,
seek and UI tests. Those runtime tests have not been run in this environment;
Wine remains blocked before application startup.

`philips.exe --playback-test INPUT REPORT.txt` reports `finished`, `underruns`,
`peak_buffer_seconds` and `error`. Its output excludes paths and metadata.
Buffer depth includes the full currently playing device block, so it is an
approximation. An application counter cannot detect every driver/device/DPC
glitch, and stalls longer than the available reserve can still interrupt audio.
No gapless next-track decoder or whole-file preload is implemented by this change.

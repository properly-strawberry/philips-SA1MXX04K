# FFmpeg dependency

The user downloaded the BtbN FFmpeg n9.0 LGPL shared Windows x64 SDK on
2026-11-10 from:

https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-n9.0-latest-win64-lgpl-shared-9.0.zip

Archive SHA-256:
`2561c6f8216e510b236bca914e5efb5445e61167c72213251e814dd6ffbbe221`.
The archive's root is `ffmpeg-n9.0-latest-win64-lgpl-shared-9.0/`.
The app links its import libraries and ships the corresponding shared DLLs.
This is an ignored local dependency; no SDK binaries are committed.

Upstream build configuration and source acquisition scripts:
https://github.com/BtbN/FFmpeg-Builds

FFmpeg source and licensing:
https://ffmpeg.org/download.html
https://ffmpeg.org/legal.html
https://www.gnu.org/licenses/old-licenses/lgpl-2.1.html

The desktop source is separate from FFmpeg. Shared linking allows replacing
the libraries with ABI-compatible versions. The original firmware and extracted
graphics are retained privately; this package has not been published.

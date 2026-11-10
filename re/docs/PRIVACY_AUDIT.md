# Working-tree privacy audit

2026-11-10. Local inspection only; no data was sent to an external service.

The initial byte scan examined 1,284 files totaling 4,051,019,608 bytes,
including source, documentation, retained evidence, raw captures, extracted
firmware sections, generated outputs and local player media/configuration.
The requested personal-name search found no matches, case-insensitively,
in UTF-8/ASCII, UTF-16LE/BE or UTF-32LE/BE. Filenames were checked too.
A separate check of the copied Python bytecode found no matches.

Source, scripts, capture metadata and evidence documents were also checked
for personal home/account paths, email addresses and credential assignments.
No actionable matches were found. A home-path pattern matched ordinary prose
containing `/home/USB`, not an account directory. Firmware SDK source paths
and generic analysis-machine paths are not evidence of the project owner's
identity.

Unit serials were found in original capture metadata, a capture script and
local duplicate capture/log files. Their retention is explicitly approved.
Model identifiers, firmware versions, timestamps, USB IDs and artifact hashes
are retained. Original capture bytes were not changed.

## Ignore rules

Generated output, the dependency SDK, upstream Rockbox checkout, logs and
Python caches were already ignored. The missing `dump_tools/dumps/` exclusion
is now present, matching the existing project instructions. Root-level Music,
Recordings and Playlists runtime folders are excluded too; clean templates
remain under `assets/Playlists/`.

Ignore rules do not remove files already committed or staged. Index/history
were not inspected: this workspace prohibits Git commands and direct access to
Git internals. Any check of historical revisions or removal of previously
tracked runtime files must be performed on the host.

## Scope limits

The audit is a search and manual review of the current working tree, not a
proof that every byte is free of identifying information. Upstream Rockbox and
the downloaded FFmpeg SDK were excluded from the project scan, as were Git
internals and symlinks. Archives were not recursively unpacked, and photographs,
audio and bitmap glyphs were not subjected to OCR or transcription. The
project's retained text/JSON evidence and decoded firmware outputs were included.

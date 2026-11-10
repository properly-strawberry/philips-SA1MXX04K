# Philips GoGear Mix: first firmware inspection

2026-11-10. Static inspection only; no device commands or target code execution.

## Evidence and identity

The supplied backup README identifies the owner's player as SA1MXX04K/37.
This is capture metadata, not an independent hardware identification.

`20261110T141045Z/firmware.sb` is 9,000,928 bytes, SHA-256
`f0565f2861cb5aaec78821ce2017c77ca222422a28e0a9ea72aa4db579a5d7ad`.
The extraction run recorded in `../../gen/sb/manifest.json` verifies:

- SB version 1.1, header SHA-1 and full image SHA-1.
- The all-zero AES wrapping key matches the key dictionary CBC-MAC.
- AES-128-CBC decrypts all three bootable sections.
- All command checksums and all 47 LOAD CRCs match.
- Header timestamp: 2009-08-26 10:18:00, interpreted using the SB epoch.
- Product/component version fields are BCD 0001.0021.0000 (backup metadata).

Observed section layout (offsets are in the original SB file):

| Name | Offset | Bytes | Encrypted | Flags |
|---|---:|---:|---|---:|
| boot (ID 0) | 240 | 158880 | yes | 1 |
| host | 159136 | 99808 | yes | 1 |
| play | 258960 | 142624 | yes | 1 |
| hvmi | 405504 | 185072 | no | 2 |
| pvmi | 593920 | 798224 | no | 2 |
| rsrc | 1392640 | 7608256 | no | 2 |

## Platform and code

Observed in `gen/sb/02_play.strings.tsv`, section offset `0x9c54`:
`ThreadX ARM9/Green Hills Version G4.0b.4.0c` appears in an Express Logic
copyright string. The boot/host sections contain the same identification.
`00_boot.strings.tsv:0x5d6c` contains a source path ending in
`5005_P0_Mix\components\update\src\EncoreBootImageReader.cpp`.
Other paths name RijndaelCBCMAC.cpp and NAND mapper source files.

Conclusion (derived): native ARM firmware built from a SigmaTel/STMP SDK,
using ThreadX and Green Hills tooling. An exact SoC number has not been
established. ARM vector instructions and Ghidra's successful ARM decompilation
support the architecture identification independently of the string.

The extractor creates six ELF32 ARM files, splitting sequential LOAD/FILL
groups at CALL/JUMP boundaries. This preserves overlapping boot stages.
These are analysis wrappers, not original ELF files or flashable firmware.

REA/Ghidra successfully decompiled `02_play_stage_00.elf` entry `0x191c`
as a call to `0x15cc` with zero in the first argument register. Its caller
context is SB CALL, not the final playback entry. The larger initialization
routine includes hardware accesses, delays and repeated sampling; its precise
purpose is unknown. Evidence:

- Entry dossier: `ev_9e6e4a89a70886abd68c74448a03ebdad12ccb65f29e0cdcd8a51582d5732dc2`
  (inline tool output from the pre-restart session).
- Initialization dossier: `ev_bcfad7cc08f56641a953c35491a33de934525a0522403394005d71500a419f46`,
  preserved in `play-preinit.evidence.json` and `play-preinit.rea-evidence.json`.

## Graphics, text and possible service menu

`gen/resources/manifest.json` records 1,164 unique physical resource tables,
129,223 unique image payload offsets (including glyph resources), and 5,825
data payload offsets. These are payload counts, not unique rendered designs.
Value entries and alternate alias paths are not exported.

Observed: 322 UI images from root groups 0..9 decode with a four-byte packed
dimension header and little-endian RGB565 pixels. The payload length matches
`4 + 2*width*height`. Width occupies bits 8..19, height bits 20..31, with
zero in the low byte for exported images. Visual inspection confirms:

- GoGear to Mix boot-animation frames, group 7 (128x64).
- Battery images, group 1.
- Equalizer images, group 6.
- Waiting animation, group 8.
- Localized Charge & transfer / Charge & play screens, group 9.

Individual images: `gen/resources/images/`. Contact sheet:
`gen/resource-preview.png` (88 selected large images, generated with Pillow).

Observed: 5,823 UTF-16LE text payloads decoded from root groups 10 and 11.
Group 10 has 24 second-level groups; group 10.2 contains English UI text.
These groups are consistent with localization variants, but their full
language mapping has not been verified. Texts and resource paths are in
`gen/resources/texts.tsv`.

Particularly useful English resource entries:

| Resource path | RSRC payload offset | Text |
|---|---|---|
| 10.2.182 | 0x2fd6b4 | Virgin mode |
| 10.2.183 | 0x2fd6d0 | Region settings |
| 10.2.184 | 0x2fd6f4 | Diversity check |
| 10.2.185 | 0x2fd718 | Power Ups |
| 10.2.186 | 0x2fd730 | Connections |
| 10.2.187 | 0x2fd74c | Reset all counts |
| 10.2.189..193 | 0x2fd798..0x2fd7c8 | /02, /37, /55, /93, /97 |
| 10.2.194 | 0x2fd7d4 | 100DB Limit |
| 10.2.195 | 0x2fd804 | FM Record Supported |
| 10.2.196 | 0x2fd830 | Thai available: |
| 10.2.197 | 0x2fd854 | Battery info |

Conclusion (derived): a factory/service configuration UI is a strong candidate.
Unknown: its reachability, entry key combination, whether all entries are
enabled, and the actual effect of its settings. Text presence alone does not
establish those behaviors.

## Audio and diagnostics

Observed in `04_pvmi.strings.tsv` (offsets in the decrypted pvmi section):

- `0x538a8`: `ddi_fm_osa_event_rdsNotify`.
- `0x53a97`: `FOpen FM stream %d`.
- `0x8dd6c`: `Test   Description`, followed by register read/write diagnostics.
- `0x933f4`: DRM MsgQ; nearby DRM Task string.
- `0xbd5a0`: `A:/recordings/FM/FMREC999.WAV`.
- `0xbd5c0`: `A:/recordings/Voice/VOICE999.WAV`.
- `0xbf06c`: RIFF/WAVE identifiers and `vorbis`.

The play section also contains `a:/playqueue.dat`, `a:/persist.bin`, page-fault
zone diagnostics, OCRAM/SDRAM memory pool names, and USB connection watcher
timer/mutex names. Conclusion (derived): substantial SDK diagnostics and
separate host/playback support code are retained. Hardware support and code
reachability for each optional audio feature remain unknown.

The RSRC data also contains an autorun configuration at offset `0x346c10`,
with a Philips support URL referring to SA1MXX04WS/37. This is an embedded
support reference, not proof that the owner's K/37 identification is wrong.
The referenced URL was not fetched or executed.

## Reproduce and continue

From the workspace root, use new output directory names if outputs exist:

```sh
python3 -I tools/sb_extract.py philips/20261110T141045Z/firmware.sb philips/gen/sb
python3 -I tools/stmp_rsrc.py philips/gen/sb/05_rsrc.bin philips/gen/resources
```

`sb_extract.py` uses stdlib and installed OpenSSL; `stmp_rsrc.py` uses stdlib.
Both refuse an existing destination. `gen/` holds reproducible extracted data.
Read-only ELF verification used `readelf -h`; REA imported the preinit ELF
and decompiled its entry and callee. PNG decoding was visually checked.

Final verification run (`philips-static-extraction-check`, 2026-11-10):
322 exported PNGs passed Pillow integrity checks; all six ELF program-header
file ranges were valid; all 47 LOAD CRCs passed; source SHA-256 matched.
Flipping a byte in a scratch copy of the encrypted payload caused the extractor
to reject the image with `Full image SHA-1 mismatch` before creating output.
Scratch was removed and the owned REA target was closed.

Next useful work: map pvmi/hvmi virtual code paging, find code references to
service-menu resource IDs, trace the key-event handler and optional-feature
gates, and reconstruct the UI without assuming every embedded menu is enabled.
No firmware patches or device writes were attempted.

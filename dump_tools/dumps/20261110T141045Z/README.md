# Philips GoGear Mix firmware backup

Player identified by the owner as SA1MXX04K/37. USB storage reported Philips
GoGear Mix, VID:PID 0471:2070, serial 020300014E144D3A0002D9B15F7BCD3A.
Captured November 10, 2026, through the original firmware's SigmaTel SCSI
read commands. No flash-write, erase, or reset commands were used for this dump.

`firmware.sb` is the exact SB2 boot-image container extracted from logical
drive 3 (system tag 0x50), using the image length in its header:

- Size: 9,000,928 bytes.
- SHA-256: `f0565f2861cb5aaec78821ce2017c77ca222422a28e0a9ea72aa4db579a5d7ad`.
- Container version: 1.1; header SHA-1 verified.
- Product and component version fields: BCD 0001.0021.0000.

The three complete 9 MiB logical system partitions (tags 0x50, 0x60, 0x70)
are byte-identical. Each was captured and then verified by a second complete
device read. Their SHA-256 is
`d4dfd50d23fab120e31896673401b92ebbb9caf686f386b0072532e2c20aa223`.
Their full allocations, including bytes beyond the boot-image container, are
preserved in the three `drive_*_type_01_*.bin` files.

The additional 4 MiB hidden logical drive with tag 0x0c was also captured and
verified. `manifest.json` records geometry, filenames, and SHA-256 checksums.
`firmware_verification.json` records the container extraction checks.
The scope is logical firmware partitions and that additional hidden drive;
NAND spare/OOB data, boot ROM, the user music volume, and logical drive 1
(hidden tag 0x0b) were outside this capture.

The copied root files remain separately in `../../files`.

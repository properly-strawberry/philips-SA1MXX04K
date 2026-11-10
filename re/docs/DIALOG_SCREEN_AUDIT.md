# Full-screen dialog and error audit

2026-11-10. Scope: every current LCD page, popup and failure path in
`src/interface.cpp`, its Windows input adapter, and the saved firmware evidence.
This is static firmware analysis plus tests of the shared desktop renderer,
not a physical-device or Windows runtime comparison.

## Firmware evidence

Target: `gen/deep/pvmi-mapped.elf`, SHA-256
`094dadb25b312573973d0806c9838e787a2ba6e76405691d16223d6ef0c65ecf`.

- **0x14e318** creates error dialog class **0x1020**, kind 0, with bounds
  **(0,0,128,64)** and timer argument 0x19. It populates that dialog through
  0x14dae0, attaches it, runs it modally, then destroys it. Evidence:
  `ev_73800e6f8b3bf42a4ef5f07b7d7c12867388be7f3e04259dd749ccc8ebc1763e`.
- **0x14dae0**, case **0x12**, selects two original text resources, one-based
  **0x66/0x67**: File format / not supported! It calls the generic two-row
  layout. Language index 21 gets a third, blank resource 0x68 and the three-row
  layout. The dispatcher also populates database, media-engine, recording,
  delete and empty-library notices using the same dialog object. Evidence:
  `ev_eead9012576abe95c55163c9747bde907e03870480b8544f39528959d9d4d198`.
- **0x157be0** fills the complete dialog bounds with the theme background.
  **0x157de8** places kind-0 one/two/three text widgets at y25,
  y12/y40, or y6/y25/y44. These are widget origins, not glyph origins.
  Code/evidence is retained in `lock-dossiers.json`.
- **0x14e86c**, property **0x109=1**, computes vertical text offset as
  `(widget height - font height) >> 1`. Thus a 12-pixel font in a 14-pixel
  dialog row starts one pixel below its widget origin. The common dialog
  constructor applies that property; the painter 0x14e62c uses the computed
  offset. Evidence:
  `ev_14fea4b3cd91ebefea53c9d6147b61341504f826f7bc9a27708c2bbf61f41bc6`,
  `ev_efa3825268a2688a6ff89841a8145fbbfddcc685251407d84a6fe85307782b7f`.

New code/assembly and binary identity are retained in
`dialog-error-dossiers.json`. The original error message is therefore a
full-display dialog; leaving title/counter/battery pixels visible is a desktop
reconstruction bug.

## Complete current rendering checklist

| Path | Finding and resulting behavior |
| --- | --- |
| File/playback failure | **Fixed.** Previously cleared only (0,16,128,36), leaving the title and lower status visible. Now uses the shared full-screen text renderer and source-backed row positions |
| Internet-radio failure | **Fixed adapter presentation.** Previously replaced only the station-name row with Unavailable. Now shows Unavailable on a clean full screen; the network error itself has no physical-FM equivalent |
| Recording failure | Already cleared the full screen. Now shares the same renderer and common two-row layout; Microphone/unavailable remains desktop wording |
| Generic notices | Already full-screen. Playlist, delete, reset, volume-limit, configuration and desktop USB notices now share the same text renderer |
| Confirmations | Already full-screen. Now explicitly use the shared dialog clear/text path before the fixed Play/OK and Back/Cancel footer |
| Lock/unlock | Already fixed in the preceding pass; now shares the common full-screen clear operation |
| Volume popup / volume-limit editor | Already full-screen; now shares the same clear operation |
| Empty lists / scanning | Already safe because the page starts clean and no rows are drawn. Now explicitly clears through the common dialog operation before empty/loading content |
| Updating / boot / shutdown / blank | Full-display paths already; original images or explicit black fills are retained |
| USB-connected / charge-choice previews | Start from a clean display; no inherited page pixels. Other host-choice layout differences remain in the fidelity audit |
| Other ordinary pages | Start with a complete theme-background fill; no additional partial-message repaint found |
| Windows startup error | An OS MessageBox for runtime-folder creation failure, outside the emulated LCD |
| Windows decoder error detail | Text below the device body, outside the LCD; not an LCD coverage defect |

`beginDialog()` resets the clip rectangle and clears all 8192 pixels.
`drawMessage()` uses it before drawing bounded original-font rows. File/radio/
recording errors, confirmations and ordinary notices all use that path, so
their clearing behavior is no longer implemented separately.

## Input and layer checks

Screen clicks on timed notices, volume popups and unlocked padlock feedback
dismiss only that visible popup. They do not seek, pause or activate a hidden
menu row. Screen clicks on persistent failure messages do nothing; Back or
Next/Previous controls provide recovery. Seek/pause commands are ignored for
failed playback; a transport press changes tracks without starting a seek hold.
These mouse rules are desktop adaptations, not original touch-screen behavior.

Display priority remains lock feedback over notices, then volume feedback,
then page/failure content; screen-off blanks everything. Key handling and
exact native modal lifetimes are not fully reconstructed. In particular,
ordinary desktop notice keys can still dismiss and perform their explicit
action; this pass prevents pointer clicks from reaching hidden controls.

## Regression coverage and remaining gaps

`messageTests` injects file, stream, recorder and configuration failures, plus
a volume-limit notice, through the actual portable interface in all three
themes. `requireMessage` compares every pixel with a theme background and the
declared original glyphs, including empty areas where a title, counter,
progress bar or battery would otherwise remain. Tests also cover the special
language-21 error layout, rotation, blank/wake, volume/lock priority, popup
click consumption, rejected failed-track seek/pause and successful Next recovery.
Existing confirmation and lock tests remain active. Native tests do not open
an audio device, microphone or network stream.

Remaining **classification** issues are distinct from display coverage: the
current audio error string has no typed category, so output-device failures
also get the unsupported-file caption. Recorder storage failures likewise get
the generic microphone caption. Proper error categories, specialized success/
error layouts, regional shaping and exact modal timers remain future work.
There were no further partial-screen message paths in the current renderer
within this audit's scope.

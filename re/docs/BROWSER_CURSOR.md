# Browser cursor after playback

2026-11-10. Question: select song A in a list, advance playback to B, then
press Back. Should the list select A or B?

**Derived conclusion: the traced music-browser return path restores A, the
remembered browser selection. It does not follow the playing track to B.**
The desktop already restores its saved browser frame, so its behavior is
unchanged. This finding comes from static code, not a physical-device trace.

## Evidence

Addresses refer to `gen/deep/pvmi-mapped.elf`, SHA-256
`094dadb25b312573973d0806c9838e787a2ba6e76405691d16223d6ef0c65ecf`.

1. Browser save callback **0x15228e** writes the current list selection,
   browser-state offset `+8`, into the per-depth cursor array at RAM `0x10b4`.
   Depth is the byte at `0x6700`. Root dispatch invokes this when leaving the
   browser page. Evidence:
   `ev_721f1495ad99fa7b551f6114781858dd0fc894de259e406c74f1ee93055e20b1`.
2. Playback-start helper **0x15cd92** also saves that browser selection to
   the same cursor slot before computing the playback index through 0x15cbf8
   and selecting it through 0x160f00. It keeps browser position and playback
   position as separate state. Evidence:
   `ev_1aea2dfcb159b9f95aad7cdd3f40eb66d26c0f39abd94a9b7c4391b1ffc1c53e`.
3. Music playback handler **0x1532e6** handles Back (`0x201`). When the previous
   page was the browser (`0x23`) and its saved context is valid, it requests
   page `0x23` again. Other entry paths can return Home. It does not calculate
   a replacement browser row from the current playback index. This function
   is retained in `playback-recording-dossiers.json`.
4. Root switch **0x14d4b8** recreates browser class `0x1007`. Its constructor
   **0x1522f8** calls **0x15cf9c**. When the saved context is valid, that helper
   restores browser-state `+8` from `0x10b4[depth]`; otherwise it initializes
   the browser at zero. It then populates the visible rows from this restored
   position. Evidence:
   `ev_1095dbf88ee517ed37df07886cad57728840bd204ce4d4251dff9f5ab81b5e98`,
   `ev_6aeec8380fa613ebea83a779fce31b1a27ae34bca8b5fea823afa2fab7c4e168`.
5. The Next/Previous requests from the playback handler go through
   **0x15e66e / 0x15e6a8** to the playback worker. These wrappers enqueue
   direction requests; they do not update the browser cursor array.
   Evidence:
   `ev_1858164c17ed50e9f380139eadff59ac3622f536890c061b98b6fde03f21b5e5`,
   `ev_df51d6cb60652fc548472a7d97afa3fe44e83637304e2f4f5894bebfc0c0bab9`.

The pointer constants and relevant Thumb instructions are retained with the
function dossiers in `browser-cursor-dossiers.json`. Additional mappings:
`DAT_0015d154`, `DAT_0015cdc8`, `DAT_0015cf20` all point to cursor RAM `0x10b4`;
`DAT_0015d150`, `DAT_0015cdc4`, `DAT_0015cf1c` point to depth byte `0x6700`.
The saved context handles are a separate array at RAM `0x10d4`.

## Limits and desktop behavior

This establishes the normal saved-browser path represented by these callbacks.
It is not an exhaustive runtime proof for every database change, invalidated
context, audiobook page, or indirect firmware write. A physical A -> B -> Back
trace would independently confirm the conclusion.

The desktop's `Interface::back()` restores the saved Frame, including its
selection. Manual/automatic queue changes do not overwrite that browser cursor.
Following B instead would be a deliberate desktop behavior change rather than
a correction supported by the return path examined here.

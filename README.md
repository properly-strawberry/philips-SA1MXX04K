# Philips GoGear Mix investigation

- Original device capture: `20261110T141045Z/` (keep immutable).
- Findings and evidence: [re/docs/INITIAL_FINDINGS.md](re/docs/INITIAL_FINDINGS.md).
- Reproducible extracted sections/ELFs: `gen/sb/`.
- UI PNGs, text and inventory: `gen/resources/`.
- Selected graphics: [gen/resource-preview.png](gen/resource-preview.png).
- Extractors: `../tools/sb_extract.py` and `../tools/stmp_rsrc.py`.
- Device capture/recovery scripts: `dump_tools/`; its local Rockbox checkout,
  duplicate dumps and runtime logs are ignored.

The workspace prohibits all git commands and direct access to git internals.
The user initialized this directory as a repository on 2026-11-10 and manages
commits on the host. Continuation instructions are in [AGENTS.md](AGENTS.md).

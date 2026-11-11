# Milestone 5 acceptance pieces — change indicators (owner notation rulings 2026-08-16)

Every piece was produced through the converter pipeline from an existing M2 acceptance piece's `enriched.musicxml` (Kernel-enriched MusicXML, `urn:jims:musicxml:2`), with the change carried by the existing `StaffTypeChange` mechanism. New converter flags (this milestone): `JIMS_REFERENCE=<key_number>` binds Re0 to a MIDI-style key number in the base state (the reference-pitch interop anchor); `JIMS_KEY_CHANGE=<measure>:<nPer>:<nGen>` shifts the reference by minus the interval Do0→XxN (the SHIFT–Do0–XxN gesture's lattice delta; 12 key steps per period, 7 per generator); `JIMS_SCALE_CHANGE=<measure>:<collection_rotation>`; the pre-existing `JIMS_MODE_CHANGE=<measure>:<mode_rotation>`. All flags at one measure compose into a single `StaffTypeChange`. Tonic-extent tokens were hand-set (`JIMS_TONIC_TOKEN=tonic-bounded`; the second span reuses it) because the M2 `enrich-runner` scratch binary is not part of either repository — recorded here as fixture provenance.

| Piece | Source | Flags | Expected indicator |
| --- | --- | --- | --- |
| `m5-mode` | collision (3 bars, one system) | `JIMS_MODE_CHANGE=2:5` | mode: tonic indicators Do, La; arrow down (fewest degrees); no dots |
| `m5-key-up` | collision | `JIMS_REFERENCE=62 JIMS_KEY_CHANGE=2:0:1` | key Do0→So0: dots Do (lower Do-line), So; arrow up; no tonic indicator |
| `m5-key-down` | collision | `JIMS_REFERENCE=62 JIMS_KEY_CHANGE=2:-1:1` | key Do0→So−1: dots Do (upper Do-line), So; arrow down |
| `m5-scale` | collision | `JIMS_SCALE_CHANGE=2:-3` | scale (Parallel Minor Notes): new stack Do Re Me Fa So Le Te with tonic indicator on Do; no arrows |
| `m5-key-mode` | collision | `JIMS_REFERENCE=62 JIMS_MODE_CHANGE=2:5 JIMS_KEY_CHANGE=2:-1:3` | owner's worked example: dots Do, La each wrapped by a tonic indicator; ONE arrow up (key trumps mode) |
| `m5-syshead` | grym (16 bars, 3 systems) | `JIMS_MODE_CHANGE=6:0` | change at a system head: full header, NO enclosure |

## Command shape (per piece)

```
mscore enriched.musicxml -o stock.mscx
JIMS_TONIC_TOKEN=tonic-bounded [flags] python3 tools/jims/enriched_to_jims_mscx.py enriched.musicxml stock.mscx <piece>.mscx 13
mscore <piece>.mscx -o initial.png        # from a clean directory (no adjacent style/container files)
```

`semantics.json` is the Kernel change-indicator model as transported by the fork (`JiMStaffTests.changeIndicatorSemanticsPerFixtureMatchTheOwnerRules`), never pixel-derived. Renders were produced twice in clean directories and are byte-identical.

## MusicXML V3 change events (2026-08-16, owner decisions 1a/2a/3a/4a)

The six pieces are now expressed as JiMS MusicXML V3 (`urn:jims:musicxml:3`) and re-derived from it with NO converter override flags. Each `enriched.musicxml` was produced by the Kernel (`cargo run -p jims-api --example musicxml_v3 -- enrich <source> <piece>/timeline.json` in repository `jims` at merge commit `1f21503`, PR 196) from the stock source piece and the piece's `timeline.json` (a per-measure list of the two complete `JiMStaffStateV2` states, tonic-extent tokens declared explicitly as before); `v3-manifest.json` drives the Kernel's pinned change-event vectors for the extension repository (`musicxml-jims` `tests/vectors/jims_change_events_v3.json`). Every state carries an explicit `jims:reference`; the second state is followed immediately by the Kernel-written `jims:change` summary, which the converter never reads — the `StaffTypeChange` carrier is transcribed from the full second state alone. Command shape now:

```
mscore enriched.musicxml -o stock.mscx
python3 tools/jims/enriched_to_jims_mscx.py enriched.musicxml stock.mscx <piece>.mscx 13
mscore <piece>.mscx -o initial.png        # from a clean directory
```

Round-trip evidence: the regenerated `.mscx` files carry state JSON identical to the previously accepted `.mscx` (only MuseScore's random `<eid>` values differ), and their clean-directory renders are byte-identical, run to run and to a re-render of the previously accepted `.mscx` on the accepted fork build (`jims/main` 8a45729b72). Finding recorded honestly: the `initial-1.png` files committed at 2373bb1c7c predated the last two owner-gate render commits (26e0791d91 labels left, 8a45729b72 courtesy indicator) and no longer matched the accepted tip; they are refreshed here from the accepted build, and both the accepted-`.mscx` and the V3-round-trip renders hash to these files. The M2 12-TET pieces (`../m2-acceptance/*-12tet`) render deterministically on this build but their committed PNGs are M2-era history and are not comparable.

| Piece | V3 timeline | Kernel `jims:change` |
| --- | --- | --- |
| `m5-mode` | measure 2: mode-rotation 0 to 5 | `kind` mode; `mode-change` Do (nGen -2) to La (nGen 1) |
| `m5-key-up` | measure 2: reference key-number 62 to 55 | `kind` key; `key-change` n-per 0, n-gen 1, up |
| `m5-key-down` | measure 2: 62 to 67 | `kind` key; `key-change` n-per -1, n-gen 1, down |
| `m5-scale` | measure 2: collection-rotation 0 to -3 | `kind` scale; `scale-change` 0 to -3 |
| `m5-key-mode` | measure 2: mode-rotation 0 to 5 and 62 to 53 | `kind` key, mode; `key-change` n-per -1, n-gen 3, up; `mode-change` Do to La |
| `m5-syshead` | measure 6: mode-rotation 5 to 0 | `kind` mode; `mode-change` La (nGen 1) to Do (nGen -2) |

## Native import (2026-08-16, owner decision 1a)

The fork's own MusicXML importer now reads the JiMS profile directly (`src/importexport/musicxml/internal/import/importmusicxmljims.{h,cpp}`, wired into pass 1 for namespace resolution and pass 2 for `jims:staff-state` / `jims:pitch`): opening any of the six `enriched.musicxml` files in MuseScore builds the JiMStaff — the same StaffType / StaffTypeChange / note identities the converter produces — and renders byte-identical to `initial-1.png`. The converter `tools/jims/enriched_to_jims_mscx.py` remains as a fixture tool and cross-check (its unit tests still pass); `jims:change` is never read by either path.

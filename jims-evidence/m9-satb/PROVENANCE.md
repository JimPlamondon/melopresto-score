# JiMStaff Milestone 9 — SATB open score — sensory evidence provenance

Automated sensory evidence for M9 (owner-accepted plan `Plans/Add_JiMS_to_MuseScore/M9_SATB_OpenScore_FINAL.md`, 2026-08-22): the two fixtures of record rendered by the installed MuseScore bundle, twice each from clean scratch directories, and checked by a committed pixel verifier. Jim's by-eye acceptance in the app is the final gate; this folder is the machine evidence that precedes it.

Visual rendering is the whole sensory dimension of M9 — there is no audio dimension in this milestone.

## What is here

- `render_evidence.sh` — the committed render driver. Renders each fixture twice, from a fresh scratch directory per render, into `renders/` (`<fixture>-p<k>.png`, second render `<fixture>-2-p<k>.png`), then writes `renders/SHA256SUMS`. sha256 `a44a58113844e6a9f21800cfa732af230adc4fe74fa23e5ab579103bf990d314`.
- `verify_satb_layout.py` — the committed pixel verifier (Pillow + numpy from `/Users/jim/Developer/JiMS/.venv`, deterministic RGB decode). It fails unless: every render matches `SHA256SUMS`; every repeated render is byte-identical to its first; every rendered system draws four JiMStaff frames; every system has at least one measure barline running through all four frames; and **no measure barline carries any ink in any of the three inter-staff gaps**.
- `verify-summary.json` — the verifier's machine-readable verdict for the committed renders (`ok: true`).
- `renders/` — 6 page PNGs (2 fixtures × 2 renders, the template spilling to 2 pages) plus `SHA256SUMS`.

## Fixtures of record

Both fixtures are in **C, Do-mode**: diatonic scale, collection rotation 0, `mode_rotation` 0 so Do is the tonic, and a reference pitch of D4 which puts Do on C. Each file states its own key and mode, so nothing about which absolute pitch a JiMS note sounds is left to a fallback guess.

- **`template`** — the shipped empty template `share/templates/02-Choral/12-SATB_(JiMStaff)/12-SATB_(JiMStaff).mscx` (sha256 `41ec94f6e3872e7899b3def22febeb33cfe92401d208b2e8280b15d8eafd7729`): four JiMStaff staves in open score, 18 bars of measure rests, constellation extents `lower_do_register` 4/4/3/3 with `period_count` 1.
- **`hymn`** — `src/engraving/tests/jimstaff_data/m9-satb-hymn.mscx` (sha256 `b7e7762b0b1b43e33a0dc5844d86e26f5549ffb9b1042e6a12700e7019ef7a7c`): the first phrase of the **Old Hundredth**, four bars of 4/4, entered in all four parts, with lyrics on the Soprano and one dynamic.

### Hymn provenance, stated exactly

**The tune is public domain.** OLD HUNDREDTH (also OLD 100th, GENEVAN 134) is a Long Metre hymn tune from the second edition of the Genevan Psalter, *Pseaumes Octante Trois de David* (1551), attributed to Louis Bourgeois (c. 1510 – c. 1560). A 1551 melody by a composer who died c. 1560 is unambiguously out of copyright everywhere.

**The melody transcribed here is sourced, not remembered.** Hymnary.org's tune entry for OLD HUNDREDTH gives the melodic incipit `11765 12333 32143` in Hymnary's movable-do numeric notation, metre 8.8.8.8 (<https://hymnary.org/tune/old_hundredth_bourgeois>). The first line of a Long Metre tune is its first eight notes, so the phrase entered here is scale degrees **1 1 7 6 5 1 2 3**, written with Do = C in the octave above Do0: C5 C5 B4 A4 G4 C5 D5 E5. Background on the tune's date, attribution and metre: <https://en.wikipedia.org/wiki/Old_100th>.

**The lyric is public domain.** "All people that on earth do dwell" is William Kethe's metrical psalm paraphrase, first printed 1561.

**The three lower parts are NOT a transcription.** Soprano is the Bourgeois melody as sourced above. Alto, Tenor and Bass are a plain functional harmonisation written for this fixture by the M9 executor — chosen only to be singable, in each stock vocal part's amateur range, and free of voice crossing. They are not taken from any edition, are not claimed to be any historical harmonisation, and carry no third-party rights. Anyone comparing this render with a published Old Hundredth setting should expect the inner parts to differ.

## Renderer and commands

- Bundle: `build.install/mscore.app` (MuseScore4Development 4.7.4), built from `build.release` and installed with `cmake --install build.release`. Fork branch `claude/m9-satb`, base `ce964a7d4a21aaa61b6f461cf4c44963a91f6716`; the exact head SHA the evidence was regenerated at is recorded in the M9 final report. Kernel `GitHub/jims` `main` at `a9ad21108c3acd531598d6d7bf4fbc0f3014d9d5`. Qt 6.11.1 (Homebrew). macOS 26.5.2. `QT_QPA_PLATFORM=offscreen`.
- **Signature: none was applied, and none was needed.** `codesign` is not invoked anywhere in this milestone (owner constraint). The bundle is *linker-signed ad hoc* by the build itself — `codesign -dv` reports `flags=0x20002(adhoc,linker-signed)`, `Signature=adhoc` — and runs unsigned offscreen. This differs from the M8 bundle, which was explicitly ad-hoc signed; M9 needed no such step.
- Qt deviation from the M8 precedent: M8 rendered with the Qt 6.10.2 bundle from `build.qt610`. That build directory can no longer be configured — its VST3 SDK cache under `Temp/jimsynth-vst3-sdk-cache/` was removed during repository housekeeping, so CMake fails at `src/framework/vst/sdk/CMakeLists.txt`. M9 therefore renders with `build.release` (Qt 6.11.1). The known Qt 6.11.1 problem is a crash in MuseScore's *dialogs*; headless page rendering opens no dialog, and every render here is reproducible and byte-identical on repeat.
- Command per render, inside a fresh scratch directory: `QT_QPA_PLATFORM=offscreen mscore -r 120 -o page.png score.mscx`; page k becomes `renders/<fixture>[-2]-p<k>.png`. Image size 1020 × 1320 px (A4 at 120 dpi).
- Verify: `/Users/jim/Developer/JiMS/.venv/bin/python jims-evidence/m9-satb/verify_satb_layout.py > jims-evidence/m9-satb/verify-summary.json`, run from the fork root (exit 0).

## The broken-barline assertion, and what it excludes

MuseScore's octavo convention — and `ScoreOrder::setBracketsAndBarlines` re-forcing `barLineSpan=false` for the `voices` section — is that a barline stroke is drawn inside each vocal staff and never between them. That claim is not visible to code inspection, so the verifier makes it from the rendered page: for each system it locates every column whose ink fills at least 80% of all four staff frames, and requires that every such column right of the first measure division carries **zero** ink in all three inter-staff gaps.

Two things are deliberately excluded, both on principle rather than convenience.

- **The system's leading verticals.** MuseScore draws a bracketed system's leading vertical (and the initial barline beside it) full height, through the gaps. This is system furniture, not a measure division. The **stock** `02-Choral/01-SATB` template renders identically — its system edge at x≈142/149 is one continuous ink run spanning all four staves, while its measure barline at x≈948 is twelve separate short runs (four staves × three systems). The verifier therefore classifies every full-height vertical left of the first measure division as the system edge and reports it, rather than asserting on it.
- **Ink that is not a barline.** Lyrics, note heads, stems and dynamics legitimately occupy the space between staves — the hymn's lyric line sits in the first gap. The gap assertion is therefore made per barline column, never over the whole gap area.

## Results (from `verify-summary.json`)

- `ok: true`, verifier exit code 0.
- SHA-256: 6 of 6 renders match `renders/SHA256SUMS`.
- Reproducibility: 3 of 3 repeat pages byte-identical to their first render.
- Systems checked: 8 (template page 1 = 1 system, template page 2 = 2 systems, hymn = 1 system, each fixture twice).
- Every system drew exactly 4 JiMStaff frames.
- Measure barlines per system: 7–11 columns; `gap_ink_in_barline_columns` is `[0]` for every system — no measure barline puts a single pixel between vocal staves.
- Stray spanning barlines: none, on any system, on any page.
- `broken_barlines_confirmed: true`.

## Live-render observations (by eye, from the committed renders)

- Four visible JiMStaff frames per system, labelled Soprano, Alto, Tenor, Bass.
- One bracket spanning all four staves.
- No clef drawn on any staff, and no key signature.
- Barlines visibly broken between every adjacent pair of vocal staves.
- On the empty template, the Kernel's tonic row labels read `C4:` on Soprano and Alto and `C3:` on Tenor and Bass — the constellation, realised in the v1 extent encoding as registers 4/4/3/3.
- On the hymn, each written staff shows its own melody-derived frame, so the four frames differ — correct behaviour, not a defect.
- Lyrics sit below the Soprano staff; the dynamic sits above it (the vocal-above rule).
- **Every note head on the hymn is a plain oval.** This is the check that matters most here and it is easy to read at a glance: a JiMS note head's SHAPE carries its accidental class, so a piece with no accidentals must show no shaped heads. Triangles or diamonds in a diatonic hymn mean the written notes and their lattice identities disagree.
- The lyric line clears the whole-period frame rather than hugging the note heads. This is the accepted cosmetic consequence of owner decision 3b and is recorded, not fixed, under this milestone.
- No obvious collisions.

**Range colouring is absent from these renders by design, not by omission.** MuseScore colours out-of-range note heads only when `!isPrinting`, so it is a screen-only affordance that can never appear in a printed page render. The seam's inputs are asserted instead by `Engraving_JiMStaffM9SATBTests.m9SweepRangeColouringInputsAreCorrectOnJimsVocalStaves`.

## Correction, 2026-08-22

The first version of the hymn fixture was wrong and its renders were committed before anyone noticed. Its notes' lattice identities had been written against the wrong anchor, so every note's written identity sat a whole tone away from the pitch it claimed to sound. Eight of its thirty-two notes came out as accidentals — four F sharps in the Alto alone — in a hymn that has none, and those wrong shapes were plainly visible in the committed pictures. Jim spotted them; the automated checks did not, because nothing compared a note's identity against its sounding pitch.

Three things changed as a result. The hymn was rewritten with correct identities and now shows no accidentals. Both the hymn and the shipped template now state their key as well as their mode, instead of leaving the key to a fallback guess. And a new test, `m9EveryNotesPitchIsTheKernelsProjectionOfItsIdentity`, asks the Kernel what pitch each note's identity sounds at under that staff's own state and requires the note to agree — it asks the Kernel rather than applying a formula, because a formula would fix the anchor for every score and JiMS is movable-Do. Re-introducing the original mistake makes that test fail with the offending note named.

The renders in this folder are the corrected ones. Any earlier copy should be discarded.

## Update, 2026-09-14: the empty template's frames changed by owner decision; the verifier follows

The 2026-08-24 verifier stopped passing on the empty template without any barline having changed. The cause is the staff-edge fix the owner accepted on 2026-09-08 (workstream `ws_staff_edges_20260908`; Kernel commit `0dac1fb`, "pin empty tonic-bounded frames to tonic ratios"; fork PR 44, merged 2026-09-09): an empty tonic-bounded staff's frame now starts on its tonic ratio row, so each of the template's four empty staves is a Do-to-So frame (700 cents, 57 px at 120 dpi) with the red Do line as its lower edge, instead of the 2026-08-24 range-centred octave (99 px). Two systems now fit on page 1 and one on page 2. Fork PRs 51, 52, 53 and 55 do not touch the one-section whole-piece frame path and are not involved.

Two of the verifier's assumptions were tied to octave-tall frames and are replaced. Systems were told apart by an outsized between-system gap, which no longer exists when frames are shorter than the space between staves; a system is now what its leading edge spans (`system_spans`), the one vertical run on the page that contains two or more staff frames, with the gap heuristic kept only as a fallback. And the inter-staff gap now starts `EDGE_SLACK` (4) rows below one frame and ends 4 rows above the next, because a barline stroke's anti-aliased end overruns the consensus frame edge by a pixel and the Do line now lies on that edge; the gaps are over sixty pixels tall, so the broken-barline assertion loses nothing.

Negative checks run at the change: a barline painted through the three inter-staff gaps at x=455 of `template-p2.png` is rejected as a spanning barline column. Renderer, results and the regenerated renders are recorded in the M9 verify summary and in the final report of workstream `ws_satb_template_verifiers_20260914`.

**Manual acceptance: pending Jim.**

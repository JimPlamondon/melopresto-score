# Generated chord evidence

MeloPresto Harmony objects retain the immutable `melo.chord-evidence.v1` derivation with a generated or manual origin. Native persistence, MusicXML, and Music Encoding Initiative (MEI) import/export preserve the record. The public record schema and musical validation belong to the Kernel.

`Harmony::meloEvidenceError` gathers the actual supporting notes across the recorded interval and obtains their sounding frequencies through the Kernel. The Kernel checks membership, register, bass, duration coverage, and annotation position. A stale generated name uses the existing invalid-name display and accessibility feedback and prevents verified interchange export. Undo restores validity when the score evidence is restored. A manual edit retains generation history but is identified as manual.

Chord-evidence offsets and durations use quarter notes. Melo-Ready uses whole notes, so the common converter changes units at the chord-evidence boundary. A held note can cross a reference change without another onset. The Kernel expresses the held note's structural identity relative to the harmony's reference before Score compares live observations with the saved evidence. The operation preserves an unbounded lattice displacement even when distinct lattice points share a sounding frequency.

The MusicXML tests exercise native/interchange persistence, supporting-note edits, undo/redo, manual history, and refused exports. The MEI tests exercise evidence round trips and stale-export refusal, including a name anchored to a note inside a chord. Private corpus checks supply an optional score path to the dedicated local test; its absence is an explicit skip, not private-corpus coverage in hosted builds.

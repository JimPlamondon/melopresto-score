/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * JiMStaff Milestone 1 — the single translatable string table for every
 * user-visible MeloPresto name (owner Q22 answer, 2026-08-13: JiMS-based names
 * are likely to change for trademark reasons within months, so a rename
 * must be one-file work). No user-visible MeloPresto name may appear as a
 * literal anywhere else in this fork; tools/melo/check_melo_strings.py
 * enforces that.
 */
#ifndef MU_ENGRAVING_MELOSTRINGS_H
#define MU_ENGRAVING_MELOSTRINGS_H

#include "translation.h"
#include "types/translatablestring.h"

namespace mu::engraving::melo {
inline muse::String canonicalStaffUnavailable()
{
    return muse::mtrc("engraving", "The score has no canonical MeloPresto staff to revise.");
}

inline muse::String createMeasuresBeforeReference()
{
    return muse::mtrc("engraving", "Create the score's measures before initializing its MeloPresto reference.");
}

inline muse::String selectMeloPosition() { return muse::mtrc("engraving", "Select a musical position on a MeloPresto staff."); }
inline muse::String precedingCanonicalStateUnavailable()
{
    return muse::mtrc("engraving", "The selected change has no preceding MeloPresto state.");
}

inline muse::String canonicalReferenceRequired()
{
    return muse::mtrc("engraving",
                      "MeloPresto notation requires a spelled initial reference and authored relative history. Absolute numeric snapshots do not contain these identities.");
}

inline muse::String canonicalChangeReferenceRequired()
{
    return muse::mtrc("engraving", "A MeloPresto staff change requires its composition's spelled initial reference and relative history.");
}

inline muse::String canonicalExportReferenceRequired()
{
    return muse::mtrc("engraving", "MeloPresto staff export requires a spelled initial reference and authored relative history.");
}

inline constexpr char synthResourceId[] = "MeloPresto Synth";
inline constexpr char analysisVocabularyCitation[] = "MeloPresto analysis controlled vocabulary v1";
inline constexpr char accidentalEditFailed[] = "MeloPresto accidental edit: ";
inline constexpr char sectionFrameAlignmentFailed[] = "MeloPresto section frame alignment failed for staff ";
inline constexpr char sectionBandAlignmentFailed[] = "MeloPresto section band alignment failed for staff ";
inline constexpr char systemFrameUnionFailed[] = "MeloPresto per-system frame union failed for staff ";
inline muse::TranslatableString changeActionName() { return muse::TranslatableString("action", "MeloPresto change…"); }
inline muse::String featureName() { return muse::mtrc("engraving", "MeloPresto Staff"); }
inline muse::String staffUserName() { return muse::mtrc("engraving", "MeloPresto Staff"); }
inline muse::String presetName() { return muse::mtrc("engraving", "MeloPresto Staff 12-TET"); }
inline muse::String crescentClefName() { return muse::mtrc("engraving", "crescent clef"); }
inline muse::String scaleDotsName() { return muse::mtrc("engraving", "scale dots"); }
inline muse::String tonicIndicatorName() { return muse::mtrc("engraving", "tonic indicator"); }
inline muse::String linkedNoteMissingState()
{
    return muse::mtrc("engraving", "a linked MeloPresto note has no effective MeloPresto state");
}

inline muse::String partialTieCrossesState()
{
    return muse::mtrc("engraving", "a path-dependent partial tie crosses the MeloPresto state span; the edit was not applied");
}

inline muse::String linkedNoteIdentityMismatch()
{
    return muse::mtrc("engraving", "a linked note disagrees about MeloPresto identity; the edit was not applied");
}

inline muse::String conflictingLinkedProjections()
{
    return muse::mtrc("engraving", "linked notes require conflicting MeloPresto projections; the edit was not applied");
}

inline muse::String notSystemStaff() { return muse::mtrc("engraving", "not a MeloPresto Staff"); }
inline muse::String positionHasOtherStaffChange()
{
    return muse::mtrc("engraving", "this position already carries a non-MeloPresto Staff type change on this staff");
}

inline muse::String emptyStaffCentreUnavailable()
{
    return muse::mtrc("engraving", "the MeloPresto Kernel could not derive the empty staff centre");
}

inline muse::TranslatableString bindReferenceAction() { return muse::TranslatableString("undoableAction", "Bind MeloPresto reference"); }
inline muse::TranslatableString insertChangeAction() { return muse::TranslatableString("undoableAction", "Insert MeloPresto change"); }
inline muse::String measureHasOtherStaffChange()
{
    return muse::mtrc("engraving", "this measure already carries a non-MeloPresto Staff type change on this staff");
}

inline muse::String staffStateUnavailable() { return muse::mtrc("engraving", "staff %1: no MeloPresto state in force at this position"); }
inline muse::String numberedStaffCentreUnavailable()
{
    return muse::mtrc("engraving", "staff %1: the MeloPresto Kernel could not derive the empty staff centre");
}

inline muse::String changeUnavailable() { return muse::mtrc("engraving", "no MeloPresto change at this position"); }
inline muse::String precedingStateUnavailable() { return muse::mtrc("engraving", "no preceding MeloPresto state can replace this change"); }
inline muse::TranslatableString removeChangeAction() { return muse::TranslatableString("undoableAction", "Remove MeloPresto change"); }
inline muse::TranslatableString normalizeStoredPitchesAction()
{
    return muse::TranslatableString("undoableAction", "Normalize MeloPresto stored pitches");
}

inline muse::TranslatableString changeTuningAction() { return muse::TranslatableString("undoableAction", "Change MeloPresto tuning"); }
inline muse::TranslatableString changeMelodyPartAction()
{
    return muse::TranslatableString("undoableAction", "Change MeloPresto melody part");
}

inline muse::TranslatableString chordNameAction() { return muse::TranslatableString("action", "MeloPresto chord &name"); }
inline muse::TranslatableString chordNameActionDescription()
{
    return muse::TranslatableString("action", "Add text: MeloPresto chord name (one name, without spaces or ~; text only)");
}

inline muse::TranslatableString chordName() { return muse::TranslatableString("engraving", "MeloPresto chord name"); }
inline muse::String exportChordFretDiagram()
{
    return muse::mtrc("iex_musicxml", "MeloPresto export: a MeloPresto chord name cannot be attached to a conventional fret diagram");
}

inline muse::String exportChordNameInvalid()
{
    return muse::mtrc("iex_musicxml",
                      "MeloPresto export: every MeloPresto harmony must carry exactly one nonempty whitespace-free canonical chord name and must not contain the superseded '~' marker");
}

inline muse::String exportBaseStateRefused()
{
    return muse::mtrc("iex_musicxml", "MeloPresto export: Kernel refused the base state of staff %1: %2");
}

inline muse::String exportMissingBaseState()
{
    return muse::mtrc("iex_musicxml",
                      "MeloPresto export: staff %1 carries a MeloPresto section at measure %2 without a MeloPresto base state at tick 0");
}

inline muse::String exportStateRefused()
{
    return muse::mtrc("iex_musicxml", "MeloPresto export: Kernel refused the state at tick %1, staff %2: %3");
}

inline muse::String exportChangeUnclassified()
{
    return muse::mtrc("iex_musicxml", "MeloPresto export: Kernel could not classify the change at tick %1, staff %2: %3");
}

inline muse::String exportTimelinesDiffer()
{
    return muse::mtrc("iex_musicxml",
                      "MeloPresto export: parts %1 and %2 carry different MeloPresto state timelines; every MeloPresto part of a document must share one state timeline");
}

inline muse::String exportMissingLatticeIdentity()
{
    return muse::mtrc("iex_musicxml", "MeloPresto export: a note on staff %1 at tick %2 has no lattice identity");
}

inline muse::String exportNoteRefused()
{
    return muse::mtrc("iex_musicxml", "MeloPresto export: Kernel refused the note at tick %1 on staff %2: %3");
}

inline muse::String warnWhenSavingNotation() { return muse::mtrc("project/save", "Warn when saving MeloPresto Notation"); }
/// Stock-MuseScore loss warning (silent unknown-tag destruction on resave).
inline muse::String stockLossWarning()
{
    return muse::mtrc("engraving",
                      "This MeloPresto-enabled version of MuseScore can save and open files that include MeloPresto Notation, "
                      "like the file you're saving now. However, if you open this file with the standard version of "
                      "MuseScore, the MeloPresto-specific information will be lost. Until MuseScore's standard version "
                      "embraces MeloPresto Notation, use *only* this MeloPresto-enabled version of MuseScore to avoid losing "
                      "MeloPresto-specific information.");
}

namespace diagnostic {
inline constexpr char periodCoordinate[] = QT_TRANSLATE_NOOP("engraving/propertyName", "MeloPresto period coordinate");
inline constexpr char generatorCoordinate[] = QT_TRANSLATE_NOOP("engraving/propertyName", "MeloPresto generator coordinate");
inline constexpr char staffFrameMissingAmbit[] = "MeloPresto Staff: no declared tonic-ambit token; frame unavailable for staff ";
inline constexpr char staffFrameDerivationFailed[] = "MeloPresto Staff: Kernel frame derivation failed for staff ";
inline constexpr char staffBandsMissingAmbit[] = "MeloPresto Staff: no declared tonic-ambit token; banded frame unavailable for staff ";
inline constexpr char staffBandsDerivationFailed[] = "MeloPresto Staff: Kernel banded frame derivation failed for staff ";
inline constexpr char vstProfilePreparationFailed[] = "MeloPresto VST3 profile preparation failed: ";
inline constexpr char soundingPitchFailed[] = "MeloPresto note_sounding_pitch failed for identity (";
inline constexpr char unreadableScore[] = QT_TRANSLATE_NOOP("engraving",
                                                            "This score contains MeloPresto data that this version cannot read. The original file has not been changed. Open it in the MeloPresto version that saved it, and keep a native copy. Details: %1");
inline constexpr char storedProjectionsNormalized[]
    = " contradictory MeloPresto stored pitch projection(s); the document is marked modified";
inline constexpr char incompleteMeiState[] = QT_TRANSLATE_NOOP("iex_mei",
                                                               "MeloPresto data is incomplete: its required state record is missing. Import was stopped to avoid silently losing the notation. Use the original MeloPresto file.");
inline constexpr char16_t meiExportInvalidChordName[]
    = u"MeloPresto MEI export: every MeloPresto chord name must be nonempty, whitespace-free, and tilde-free";
inline constexpr char16_t meiExportBridgeUnavailable[] = u"MeloPresto MEI export: the MeloPresto Kernel bridge is unavailable";
inline constexpr char16_t meiExportExtentProjectionFailed[] = u"MeloPresto MEI export: extent projection failed: %1";
inline constexpr char16_t meiExportExtentUnreadable[] = u"MeloPresto MEI export: cannot read the extent of a staff state";
inline constexpr char16_t meiExportStateOutsideMeasure[] = u"MeloPresto MEI export: a staff state does not sit in an exported measure";
inline constexpr char16_t meiExportStateSerializationFailed[] = u"MeloPresto MEI export: the Kernel refused to serialize a staff state: %1";
inline constexpr char16_t meiExportStateFragmentInvalid[] = u"MeloPresto MEI export: unparsable Kernel staff-state fragment";
inline constexpr char16_t meiExportDurationInvalid[]
    = u"MeloPresto MEI export: a trajectory duration does not fit the canonical 960-division basis";
inline constexpr char meiExportStaleAdjudication[] = "MeloPresto MEI export: adjudication ";
inline constexpr char16_t meiImportBridgeUnavailable[] = u"MeloPresto MEI import: the MeloPresto Kernel bridge is unavailable";
inline constexpr char16_t meiImportMissingMusicXml[] = u"MeloPresto MEI import: jm:record carries no jm:musicxml section";
inline constexpr char16_t meiImportMeasureUnknown[] = u"MeloPresto MEI import: a record names a measure outside the score";
inline constexpr char16_t meiImportOffsetInvalid[] = u"MeloPresto MEI import: a record offset is not a rational number";
inline constexpr char16_t meiImportStaffDefinitionUnknown[] = u"MeloPresto MEI import: a jm:part names no staffDef of this file";
inline constexpr char16_t meiImportStaffUnknown[] = u"MeloPresto MEI import: a jm:part resolves to no score staff";
inline constexpr char16_t meiImportStateMissing[] = u"MeloPresto MEI import: jm:state carries no staff-state";
inline constexpr char16_t meiImportStateRejected[] = u"MeloPresto MEI import: the Kernel rejected a staff state: %1";
inline constexpr char16_t meiImportFirstStateNotAtStart[]
    = u"MeloPresto MEI import: the first staff state must sit at the start of the score";
inline constexpr char16_t meiImportStatesOutOfOrder[] = u"MeloPresto MEI import: staff states must be in strictly increasing order";
inline constexpr char16_t meiImportStateOutsideMeasure[] = u"MeloPresto MEI import: a staff state does not sit inside a measure";
inline constexpr char16_t meiImportStatePlacementFailed[]
    = u"MeloPresto MEI import: cannot place a staff type change at the exact state tick";
inline constexpr char16_t meiImportNoteIdentityUnresolved[] = u"MeloPresto MEI import: a note-identity record does not resolve";
inline constexpr char16_t meiImportAdjudicationUnresolved[]
    = u"MeloPresto MEI import: an adjudication record does not resolve to its annotation";
inline constexpr char exportProjectionFailed[] = "MeloPresto export projection failed after preflight: ";
inline constexpr char musicXmlImport[] = "MeloPresto MusicXML import: ";
inline constexpr char16_t unsupportedMusicXmlNamespace[]
    =
        u"unsupported MeloPresto MusicXML namespace '%1' (this MuseScore understands urn:melopresto:musicxml:%2..%3 and the retired urn:jims:musicxml:%2..%3); import refused so the document is not silently shown as a plain staff";
inline constexpr char16_t defaultNamespaceUnsupported[]
    = u"the MeloPresto namespace '%1' must be bound to a prefix, not used as the default namespace";
inline constexpr char16_t conflictingMusicXmlProfiles[]
    = u"two distinct MeloPresto profiles declared in one document (%1 and %2)";
inline constexpr char16_t namespaceBoundTwice[] = u"MeloPresto namespace bound twice; keeping prefix '%1'";
inline constexpr char16_t importBridgeUnavailable[] = u"MeloPresto Kernel bridge unavailable; cannot import a MeloPresto staff";
inline constexpr char16_t importStateRejected[] = u"the MeloPresto Kernel rejected a melo:staff-state: %1";
inline constexpr char16_t importSharedStateFailed[] = u"MeloPresto import: the Kernel could not derive the shared state form: %1";
inline constexpr char16_t importNormalizationFailed[] = u"the MeloPresto Kernel could not normalize imported note projections: %1";
inline constexpr char16_t importedProjectionsNormalized[] = u"normalized %1 contradictory MeloPresto compatibility pitch projection(s)";
inline constexpr char scoreUsesNotation[] = QT_TRANSLATE_NOOP("project/save", "This score uses MeloPresto notation");
inline constexpr char16_t importTimelinesDiffer[]
    =
        u"MeloPresto parts %1 and %2 carry different melo:staff-state timelines; every MeloPresto part of a document must share one state timeline";
}
}

#endif

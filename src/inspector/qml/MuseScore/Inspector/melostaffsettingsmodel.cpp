// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#include "melostaffsettingsmodel.h"
#include "inspector/internal/ielementrepositoryservice.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/editing/editstaff.h"
#include "engraving/melo/melochange.h"
#include "engraving/melo/melostrings.h"
#include "engraving/dom/masterscore.h"
using namespace mu::inspector;
using namespace mu::engraving;

MeloStaffSettingsModel::MeloStaffSettingsModel(QObject* parent, const muse::modularity::ContextPtr& ctx,
                                               IElementRepositoryService* repository)
    : AbstractInspectorModel(parent, ctx, repository)
{
    setSectionType(InspectorSectionType::SECTION_MELO_STAFF);
    setTitle(melo::staffUserName().toQString());
}

void MeloStaffSettingsModel::requestElements()
{
    m_elementList = m_repository->takeAllElements();
}

bool MeloStaffSettingsModel::target(Score*& score, Measure*& measure, Fraction& tick, staff_idx_t& staff) const
{
    for (EngravingItem* item : m_repository->takeAllElements()) {
        if (!item || !item->staff() || !item->staff()->staffType(item->tick())->isMelo()) {
            continue;
        }
        score = item->score();
        staff = item->staffIdx();
        const Selection& selection = score->selection();
        if (selection.isRange() && selection.startSegment()) {
            measure = selection.startSegment()->measure();
            tick = selection.startSegment()->tick();
        } else {
            measure = item->findMeasure();
            tick = item->tick();
        }
        if (measure && tick == measure->endTick() && measure->nextMeasure()) {
            measure = measure->nextMeasure();
            tick = measure->tick();
        }
        return measure != nullptr;
    }
    return false;
}

void MeloStaffSettingsModel::loadProperties()
{
    m_settings.clear();
    m_options = {};
    m_scaleSteps.clear();
    Score* score = nullptr;
    Measure* measure = nullptr;
    Fraction tick;
    staff_idx_t staff = 0;
    bool valid = target(score, measure, tick, staff) && melo::changeOptions(score, staff, measure, tick, m_options);
    const QString identity = valid ? QString::number(quintptr(score)) + ":" + QString::number(staff) + ":"
                             + QString::number(tick.ticks()) : QString();
    if (identity != m_targetIdentity) {
        m_targetIdentity = identity;
        m_status.clear();
        m_hasError = false;
        emit statusChanged();
    }
    m_settings["available"] = valid;
    if (valid) {
        m_settings["target"] = muse::qtrc("inspector",
                                          "Measure %1 · position %2 ticks · staff %3").arg(measure->no()
                                                                                           + 1).arg((tick
                                                                                                     - measure->tick()).ticks()).arg(staff
                                                                                                                                     + 1);

        m_settings["hasChange"] = melo::changeCarrierAt(measure, staff, tick) != nullptr;
        if (m_settings["hasChange"].toBool()) {
            QString description = muse::qtrc("inspector", "This position carries a change.");
            if (!tick.isZero()) {
                const StaffType* before = score->staff(staff)->staffType(Fraction::fromTicks(tick.ticks() - 1));
                const StaffType* here = score->staff(staff)->staffType(tick);
                melo::ChangeIndicator indicator;
                muse::String why;
                if (before && here && melo::changeIndicator(before->meloStateJson(), here->meloStateJson(), indicator, &why)) {
                    description = indicator.empty() ? muse::qtrc("inspector",
                                                                 "This position carries a metadata-only change, so no indicator is drawn.")
                                  : muse::qtrc("inspector", "A tonal change indicator is drawn at this position.");
                } else {
                    description = muse::qtrc("inspector", "No change indicator can be drawn: %1").arg(why.toQString());
                }
            }
            m_settings["indicator"] = description;
        }

        const StaffType* base = score->staff(staff)->staffType(Fraction(0, 1));
        m_settings["elision"] = int(base->meloElideOctaves());
        m_settings["labels"] = int(base->meloScaleDotLabelMode());
        auto choices = [this](const char* key, const std::vector<melo::StateChangeOption>& options) {
            QVariantList list;
            int current = -1;
            for (const auto& option : options) {
                if (option.current) {
                    current = int(list.size());
                }
                QString label = option.label.toQString();
                list << QVariantMap { { "text", label }, { "value", int(list.size()) } };
            }
            m_settings[key] = list;
            m_settings[QString::fromLatin1(key) + "Index"] = current;
        };
        choices("tonics", m_options.tonics);

        const melo::StateChangeOption* diatonic = nullptr;
        const melo::StateChangeOption* harmonic = nullptr;
        const melo::StateChangeOption* zero = nullptr;
        const melo::StateChangeOption* minor = nullptr;
        for (const auto& option : m_options.cycles) {
            if (option.id == u"scale:cycle:diatonic") {
                diatonic = &option;
            }
            if (option.id == u"scale:cycle:double-harmonic-minor") {
                harmonic = &option;
            }
        }
        for (const auto& option : m_options.rotations) {
            if (option.id == u"scale:rotation:0") {
                zero = &option;
            }
            if (option.id == u"scale:rotation:-3") {
                minor = &option;
            }
        }
        QVariantList scales;
        int currentScale = -1;
        auto addScale = [&](QString name, const melo::StateChangeOption* cycle, const melo::StateChangeOption* rotation) {
            if (!cycle || !rotation) {
                return;
            }
            if (cycle->current && rotation->current) {
                currentScale = int(scales.size());
            }
            scales << QVariantMap { { "text", name }, { "value", int(scales.size()) } };
            std::vector<muse::String> steps;
            if (cycle != rotation) {
                steps.push_back(cycle->id);
            }
            steps.push_back(rotation->id);
            m_scaleSteps.push_back(steps);
        };
        addScale(muse::qtrc("inspector", "Diatonic (White notes)"), diatonic, zero);
        addScale(muse::qtrc("inspector", "Parallel Minor (Grey notes)"), diatonic, minor);
        addScale(muse::qtrc("inspector", "Double Harmonic Minor"), harmonic, harmonic);
        m_settings["scales"] = scales;
        m_settings["scalesIndex"] = currentScale;
        muse::String why;
        m_settings["canChange"] = melo::canInsertChange(score, staff, measure, tick, why);
        m_settings["reason"] = why.toQString();
        m_settings["canKeyChange"] = m_settings["canChange"].toBool() && tick > Fraction(0, 1)
                                     && !score->masterScore()->metaTag(melo::REFERENCE_TIMELINE_TAG).isEmpty();
    }
    emit settingsChanged();
}

void MeloStaffSettingsModel::finish(bool ok, const muse::String& error, const QString& success)
{
    m_hasError = !ok;
    m_status = ok ? success : error.toQString();
    if (m_status.isEmpty() && !ok) {
        m_status = muse::qtrc("inspector", "This change could not be applied. The score is unchanged.");
    }
    emit statusChanged();
    if (!m_status.isEmpty() && accessibilityController()) {
        accessibilityController()->announce(m_status);
    }
    if (ok) {
        if (currentNotation() && currentNotation()->undoStack()) {
            currentNotation()->undoStack()->stackChanged().notify();
        }
        updateNotation();
    }
    loadProperties();
}

void MeloStaffSettingsModel::applyOption(const QString& group, int index)
{
    // Re-read immediately: an open dropdown may outlive a selection or undo change.
    loadProperties();
    Score* score = nullptr;
    Measure* measure = nullptr;
    Fraction tick;
    staff_idx_t staff = 0;
    if (!target(score, measure, tick, staff)) {
        return;
    }
    std::vector<muse::String> steps;
    const auto& options = m_options.tonics;
    if (group == "scales" && index >= 0 && size_t(index) < m_scaleSteps.size()) {
        steps = m_scaleSteps[index];
    } else if (group == "tonics" && index >= 0 && size_t(index) < options.size()) {
        steps = { options[index].id };
    }
    if (steps.empty()) {
        return;
    }
    auto states = [&]() {
        std::vector<muse::String> result;
        for (staff_idx_t index = 0; index < score->nstaves(); ++index) {
            muse::String state;
            if (melo::effectiveState(score, index, measure, tick, state)) {
                result.push_back(state);
            }
        }
        return result;
    };
    const auto before = states();
    muse::String error;
    bool ok = melo::applyChangeToAllMeloParts(score, measure, tick, steps, error);
    const auto after = states();
    finish(ok, error,
           before == after ? muse::qtrc("inspector", "Already selected; no change was needed.") : muse::qtrc("inspector",
                                                                                                             "Applied to all compatible parts at this position."));
}

void MeloStaffSettingsModel::editKeyChange()
{
    Score* score = nullptr;
    Measure* measure = nullptr;
    Fraction tick;
    staff_idx_t staff = 0;
    if (!target(score, measure, tick, staff) || tick <= Fraction(0, 1)) {
        return;
    }
    dispatcher()->dispatch("melo-edit-key-change",
                           muse::actions::ActionData::make_arg3<int, int, int>(int(staff), tick.numerator(), tick.denominator()));
}

void MeloStaffSettingsModel::removeChange()
{
    Score* score = nullptr;
    Measure* measure = nullptr;
    Fraction tick;
    staff_idx_t staff = 0;
    if (!target(score, measure, tick, staff)) {
        return;
    }
    muse::String error;
    bool ok = melo::removeChange(score, staff, measure, tick, error);
    finish(ok, error, muse::qtrc("inspector", "Change removed from this staff."));
}

void MeloStaffSettingsModel::setStaffOption(const QString& name, int value)
{
    if (value < 0 || (name == "elision" ? value > 2 : name != "labels" || value > 3)) {
        return;
    }
    Score* score = nullptr;
    Measure* measure = nullptr;
    Fraction tick;
    staff_idx_t staffIndex = 0;
    if (!target(score, measure, tick, staffIndex)) {
        return;
    }
    Staff* staff = score->staff(staffIndex);
    StaffType edited = *staff->staffType(Fraction(0, 1));
    if (name == "elision") {
        if (int(edited.meloElideOctaves()) == value) {
            return;
        }
        edited.setMeloElideOctaves(MeloElideOctaves(value));
    } else {
        if (int(edited.meloScaleDotLabelMode()) == value) {
            return;
        }
        edited.setMeloScaleDotLabelMode(MeloScaleDotLabelMode(value));
    }
    score->startCmd(name == "elision" ? muse::TranslatableString("undoableAction",
                                                                 "Octave-band elision override")
                    : muse::TranslatableString("undoableAction", "Change staff type"));
    score->undo(new ChangeStaffType(staff, edited));
    score->endCmd();
    finish(true, {}, muse::qtrc("inspector", "Staff presentation updated."));
}

QColor MeloStaffSettingsModel::criticalColor() const
{
    return engravingConfiguration()->criticalColor().toQColor();
}

// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#include "meloinitialpitchmodel.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/dom/masterscore.h"
#include "translation.h"

using namespace mu::notation;
using namespace mu::engraving;

MeloInitialPitchModel::MeloInitialPitchModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)) {}

void MeloInitialPitchModel::load(int staff, int numerator, int denominator, int periodIndex,
                                 const QString& expectedState, const QString& expectedTimeline)
{
    async_disconnectAll();
    m_active = false;
    m_error.clear();
    m_pitch.clear();
    m_notation = context() ? context()->currentNotation() : nullptr;
    Score* score = m_notation && m_notation->elements() ? m_notation->elements()->msScore() : nullptr;
    if (!score || staff < 0 || size_t(staff) >= score->nstaves() || numerator < 0 || denominator <= 0) {
        m_error = muse::qtrc("notation", "This staff-header pitch is no longer available.");
        emit changed();
        return;
    }
    m_staff = staff;
    m_tick = Fraction(numerator, denominator);
    m_periodIndex = periodIndex;
    m_expectedState = muse::String::fromQString(expectedState);
    m_expectedTimeline = muse::String::fromQString(expectedTimeline);
    muse::String current;
    melo::TonicPitchLabel label;
    if (!melo::effectiveState(score, staff, score->tick2measure(m_tick), m_tick, current)
        || current != m_expectedState || score->masterScore()->metaTag(melo::REFERENCE_TIMELINE_TAG) != m_expectedTimeline
        || !melo::tonicPitchLabelInPeriod(current, periodIndex, label)) {
        m_error = muse::qtrc("notation", "The score changed. Reopen the staff-header pitch editor.");
        emit changed();
        return;
    }
    m_pitch = label.label.toQString();
    m_active = true;
    context()->currentNotationChanged().onNotify(this, [this]() { cancel(); });
    emit changed();
}

bool MeloInitialPitchModel::commit(const QString& pitch)
{
    if (!m_active || !context() || m_notation != context()->currentNotation()) {
        m_error = muse::qtrc("notation", "This pitch editor is no longer attached to the current score.");
        emit changed();
        return false;
    }
    m_pitch = pitch;
    muse::String error;
    Score* score = m_notation->elements()->msScore();
    if (!melo::changeInitialTonicPitch(score, m_staff, m_tick, m_periodIndex, muse::String::fromQString(pitch),
                                       m_expectedState, m_expectedTimeline, error)) {
        m_error = error.toQString();
        emit changed();
        return false;
    }
    m_active = false;
    if (score->masterScore()->metaTag(melo::REFERENCE_TIMELINE_TAG) != m_expectedTimeline) {
        m_notation->notationChanged().notify();
    }
    emit changed();
    return true;
}

void MeloInitialPitchModel::cancel()
{
    m_active = false;
    emit changed();
    emit cancelled();
}

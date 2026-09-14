// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#pragma once

#include "engraving/melo/melobridge.h"
#include "engraving/melo/melochangecontroller.h"
#include "engraving/dom/score.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"

namespace mu::engraving::test {
// Author test inputs explicitly. These helpers do not read retired snapshots.
inline muse::String canonicalRequest(const muse::String& configuration,
                                     const muse::String& timeline = {}, const muse::String& at = u"{\"numerator\":0,\"denominator\":1}")
{
    muse::String root = timeline, result, error;
    if (root.empty() && !melo::defaultReferenceTimeline(root, error)) {
        return {};
    }
    const muse::String history = muse::String(u"[{\"at\":{\"numerator\":0,\"denominator\":1},\"configuration\":%1}]").arg(configuration);
    return melo::staffRequest(history, root, at, result, error) ? result : muse::String();
}

inline muse::String configuration(const muse::String& request)
{
    muse::String result, error;
    return melo::staffConfiguration(request, result, error) ? result : muse::String();
}

inline double referenceNumber(const muse::String& request)
{
    melo::SoundingPitch pitch;
    return melo::noteSoundingPitch(request, 0, 0, pitch) ? pitch.referenceKeyNumber : -9999.0;
}

inline bool relativeKey(Score* score, staff_idx_t staff, const Fraction& tick, int period, int generator, muse::String& error)
{
    const muse::String interval = muse::String(u"{\"nPer\":%1,\"nGen\":%2}").arg(period).arg(generator);
    return melo::changeRelativeKey(score, staff, tick, interval, score->metaTag(melo::REFERENCE_TIMELINE_TAG), error);
}

inline bool initialPitch(Score* score, staff_idx_t staff, const muse::String& pitch, muse::String& error)
{
    const Fraction tick(0, 1);
    return melo::changeInitialTonicPitch(score, staff, tick, 0, pitch,
                                         score->staff(staff)->staffType(tick)->meloStateJson(),
                                         score->metaTag(melo::REFERENCE_TIMELINE_TAG), error);
}
}

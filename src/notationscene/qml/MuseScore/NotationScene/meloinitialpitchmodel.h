// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#pragma once

#include <QObject>
#include <qqmlintegration.h>
#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "notation/inotation.h"
#include "engraving/types/fraction.h"

namespace mu::notation {
class MeloInitialPitchModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString pitch READ pitch NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
public:
    muse::ContextInject<context::IGlobalContext> context = { this };
    explicit MeloInitialPitchModel(QObject* parent = nullptr);
    QString pitch() const { return m_pitch; }
    QString error() const { return m_error; }
    bool active() const { return m_active; }
    Q_INVOKABLE void load(int staff, int numerator, int denominator, int periodIndex, const QString& expectedState,
                          const QString& expectedTimeline);
    Q_INVOKABLE bool commit(const QString& pitch);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void cancelled();
private:
    INotationPtr m_notation;
    engraving::Fraction m_tick;
    int m_staff = 0;
    int m_periodIndex = 0;
    muse::String m_expectedState;
    muse::String m_expectedTimeline;
    QString m_pitch;
    QString m_error;
    bool m_active = false;
};
}

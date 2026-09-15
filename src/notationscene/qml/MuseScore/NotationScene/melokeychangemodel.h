// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#pragma once
#include <QObject>
#include <QQuickPaintedItem>
#include <QPointer>
#include <qqmlintegration.h>
#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "notation/inotation.h"
#include "engraving/melo/melochangecontroller.h"

namespace mu::engraving {
class MasterScore;
}
namespace mu::notation {
class MeloKeyChangeModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString expression READ expression NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool existing READ existing NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
public:
    muse::ContextInject<context::IGlobalContext> context = { this };
    explicit MeloKeyChangeModel(QObject* parent = nullptr);
    ~MeloKeyChangeModel() override;
    QString expression() const { return m_expression; }
    QString description() const { return m_description; }
    QString error() const { return m_error; }
    bool active() const { return m_active; }
    bool existing() const { return m_editor.exists; }
    bool valid() const { return m_active && m_error.isEmpty() && bool(m_preview); }
    Q_INVOKABLE void load(int staff, int numerator, int denominator, const QString& expectedState, const QString& expectedTimeline);
    Q_INVOKABLE void preview(const QString& expression);
    Q_INVOKABLE bool commit(const QString& expression);
    Q_INVOKABLE void cancel();
    void paintPreview(QPainter* painter, qreal width, qreal height) const;
signals:
    void changed();
    void cancelled();
private:
    bool current(engraving::Score*& score);
    void prepare(const muse::String* expression);
    INotationPtr m_notation;
    engraving::Fraction m_tick;
    int m_staff = 0;
    int m_previewStaff = 0;
    muse::String m_expectedState;
    muse::String m_expectedTimeline;
    QString m_expression;
    QString m_description;
    QString m_error;
    bool m_active = false;
    engraving::melo::RelativeKeyEditor m_editor;
    std::unique_ptr<engraving::MasterScore> m_preview;
};

class MeloKeyChangePreview : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(MeloKeyChangeModel* model READ model WRITE setModel NOTIFY modelChanged)
public:
    explicit MeloKeyChangePreview(QQuickItem* parent = nullptr)
        : QQuickPaintedItem(parent) { setAntialiasing(true); }
    MeloKeyChangeModel* model() const { return m_model; }
    void setModel(MeloKeyChangeModel* model);
    void paint(QPainter* painter) override;
signals:
    void modelChanged();
private:
    QPointer<MeloKeyChangeModel> m_model;
};
}

// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#pragma once
#include "abstractinspectormodel.h"
#include "engraving/melo/melochangecontroller.h"
#include "accessibility/iaccessibilitycontroller.h"
#include "engraving/iengravingconfiguration.h"
#include <QColor>

namespace mu::inspector {
class MeloStaffSettingsModel : public AbstractInspectorModel
{
    Q_OBJECT
    QML_ELEMENT;
    QML_UNCREATABLE("Created by the inspector")
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY settingsChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY statusChanged)
    Q_PROPERTY(QColor criticalColor READ criticalColor NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    muse::GlobalInject<engraving::IEngravingConfiguration> engravingConfiguration;
    muse::ContextInject<muse::accessibility::IAccessibilityController> accessibilityController = { this };
    MeloStaffSettingsModel(QObject* parent, const muse::modularity::ContextPtr& ctx, IElementRepositoryService* repository);
    QVariantMap settings() const { return m_settings; }
    bool hasError() const { return m_hasError; }
    QString status() const { return m_status; }
    void createProperties() override {}
    void resetProperties() override {}
    void requestElements() override;
    void loadProperties() override;
    Q_INVOKABLE void applyOption(const QString& group, int index);
    Q_INVOKABLE void editKeyChange();
    Q_INVOKABLE void removeChange();
    Q_INVOKABLE void setStaffOption(const QString& name, int value);
    QColor criticalColor() const;
signals:
    void settingsChanged();
    void statusChanged();
protected:
    bool shouldUpdateOnEmptyPropertyAndStyleIdSets() const override { return true; }
    void onNotationChanged(const engraving::PropertyIdSet&, const engraving::StyleIdSet&) override { loadProperties(); }
private:
    bool target(engraving::Score*& score, engraving::Measure*& measure, engraving::Fraction& tick, engraving::staff_idx_t& staff) const;
    void finish(bool ok, const muse::String& error, const QString& success);
    QVariantMap m_settings;
    QString m_status;
    bool m_hasError = false;
    QString m_targetIdentity;
    engraving::melo::StateChangeOptions m_options;
    std::vector<std::vector<muse::String> > m_scaleSteps;
};
}

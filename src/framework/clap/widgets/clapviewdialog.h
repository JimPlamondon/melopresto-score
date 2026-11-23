/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#pragma once

#include "uicomponents/view/topleveldialog.h"

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "../iclapinstancesregister.h"

namespace muse::clap {
//! The window a CLAP plug-in's editor is shown in: the counterpart of
//! VstViewDialog. The plug-in draws inside this window's native view.
class ClapViewDialog : public uicomponents::TopLevelDialog, public async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(int instanceId READ instanceId WRITE setInstanceId NOTIFY instanceIdChanged)

    muse::ContextInject<IClapInstancesRegister> instancesRegister { this };

public:
    ClapViewDialog(QWidget* parent = nullptr);
    ~ClapViewDialog() override;

    int instanceId() const;
    void setInstanceId(int newInstanceId);

signals:
    void instanceIdChanged();

private:
    void wrapPluginView();
    void attachView();
    void applyEditorSize(uint32_t width, uint32_t height);
    void moveViewToMainWindowCenter();
    void deinit();

    void showEvent(QShowEvent* ev) override;
    void closeEvent(QCloseEvent* ev) override;

    ClapPluginInstanceId m_instanceId = -1;
    IClapPluginInstancePtr m_instance = nullptr;
    bool m_attached = false;
};
}

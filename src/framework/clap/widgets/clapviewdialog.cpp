/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapviewdialog.h"

#include <QScreen>
#include <QTimer>
#include <QWindow>

#include "../internal/claphostedplugin.h"

#include "log.h"

using namespace muse::clap;

ClapViewDialog::ClapViewDialog(QWidget* parent)
    : TopLevelDialog(parent)
{
    setAttribute(Qt::WA_NativeWindow);
}

ClapViewDialog::~ClapViewDialog()
{
    deinit();
}

void ClapViewDialog::deinit()
{
    if (!m_instance) {
        return;
    }
    m_instance->loadingCompleted().disconnect(this);
    if (m_attached) {
        m_attached = false;
        m_instance->hosted().closeEditor();
        // What the musician set in the editor is the plug-in's state now: save it.
        m_instance->refreshConfig();
    }
    m_instance = nullptr;
}

void ClapViewDialog::wrapPluginView()
{
    m_instance = instancesRegister()->instanceById(m_instanceId);
    if (!m_instance) {
        return;
    }

    setWindowTitle(QString::fromStdString(m_instance->name()));

    if (m_instance->isLoaded()) {
        attachView();
    } else {
        m_instance->loadingCompleted().onNotify(this, [this]() {
            attachView();
        });
    }
}

void ClapViewDialog::attachView()
{
    if (!m_instance || !m_instance->isLoaded() || m_attached) {
        return;
    }

    setWindowTitle(QString::fromStdString(m_instance->name()));

    ClapEditorCallbacks callbacks;
    callbacks.resizeRequested = [this](uint32_t width, uint32_t height) {
        applyEditorSize(width, height);
        return true;
    };
    callbacks.closed = [this]() {
        // Not from inside the plug-in's own call: close on the next turn of the event loop.
        QTimer::singleShot(0, this, [this]() {
            close();
        });
    };

    uint32_t width = 0;
    uint32_t height = 0;
    // The window's native handle: an NSView on macOS, an HWND on Windows, an X11 window elsewhere.
    void* nativeParent = reinterpret_cast<void*>(windowHandle()->winId());
    if (!m_instance->hosted().openEditor(ClapEditorKind::Embedded, nativeParent, m_instance->name(), callbacks, width, height)) {
        LOGE() << "Unable to open the editor of CLAP plug-in " << m_instance->resourceId();
        return;
    }
    m_attached = true;

    applyEditorSize(width, height);
    QTimer::singleShot(0, this, [this]() {
        moveViewToMainWindowCenter();
    });
}

void ClapViewDialog::applyEditorSize(uint32_t width, uint32_t height)
{
    const QSize available = screen()->availableSize();
    int newWidth = static_cast<int>(width);
    int newHeight = static_cast<int>(height);

//! NOTE: as for a VST editor, on Windows the size includes the UI scaling.
#ifdef Q_OS_WIN
    const qreal scaling = screen()->devicePixelRatio();
    newWidth = static_cast<int>(newWidth / scaling);
    newHeight = static_cast<int>(newHeight / scaling);
#endif

    newWidth = std::min(newWidth, available.width());
    newHeight = std::min(newHeight, available.height());
    if (newWidth > 0 && newHeight > 0) {
        setFixedSize(newWidth, newHeight);
    }
}

void ClapViewDialog::moveViewToMainWindowCenter()
{
    const QRectF mainWindowGeo = mainWindow()->qWindow()->geometry();
    const int x = static_cast<int>(mainWindowGeo.x() + (mainWindowGeo.width() - width()) / 2);
    const int y = static_cast<int>(mainWindowGeo.y() + (mainWindowGeo.height() - height()) / 2);
    move(x, y);
}

void ClapViewDialog::showEvent(QShowEvent* ev)
{
    moveViewToMainWindowCenter();
    TopLevelDialog::showEvent(ev);
}

void ClapViewDialog::closeEvent(QCloseEvent* ev)
{
    deinit();
    TopLevelDialog::closeEvent(ev);
}

int ClapViewDialog::instanceId() const
{
    return m_instanceId;
}

void ClapViewDialog::setInstanceId(int newInstanceId)
{
    if (m_instanceId == newInstanceId) {
        return;
    }

    m_instanceId = newInstanceId;
    emit instanceIdChanged();

    wrapPluginView();
}

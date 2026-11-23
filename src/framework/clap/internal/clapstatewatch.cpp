/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapstatewatch.h"

#include <QByteArray>

#include "claphostedplugin.h"

using namespace muse::clap;

ClapStateWatch::ClapStateWatch(ClapHostedPlugin& plugin)
    : m_plugin(plugin)
{
}

void ClapStateWatch::setBaseline()
{
    m_hasKnown = m_plugin.hasState() && m_plugin.saveState(m_known);
    m_settleCountdown = 0;
    m_editorVisits = 0;
    m_plugin.takeStateChanged();
}

bool ClapStateWatch::restore(const std::string& base64)
{
    if (base64.empty()) {
        return true;
    }
    if (!m_plugin.hasState()) {
        m_rejected = true;
        return false;
    }
    // The plug-in's opaque bytes are kept as base64 text.
    const QByteArray bytes = QByteArray::fromBase64(QByteArray::fromStdString(base64), QByteArray::AbortOnBase64DecodingErrors);
    const std::vector<uint8_t> raw(bytes.begin(), bytes.end());
    m_rejected = bytes.isEmpty() || !m_plugin.loadState(raw);
    if (!m_rejected) {
        // What the plug-in gives back for that state is what the score holds.
        setBaseline();
    }
    return !m_rejected;
}

bool ClapStateWatch::isRejected() const
{
    return m_rejected;
}

bool ClapStateWatch::read(std::string& base64)
{
    std::vector<uint8_t> raw;
    if (!m_plugin.hasState() || !m_plugin.saveState(raw)) {
        return false;
    }
    // The same bytes as last known: nothing changed, nothing is reported.
    if (m_hasKnown && raw == m_known) {
        return false;
    }
    m_known = raw;
    m_hasKnown = true;
    const QByteArray bytes(reinterpret_cast<const char*>(raw.data()), static_cast<qsizetype>(raw.size()));
    base64 = bytes.toBase64().toStdString();
    return true;
}

bool ClapStateWatch::visit(std::string& base64)
{
    const bool editorOpen = m_plugin.isEditorOpen();
    m_editorVisits = editorOpen ? m_editorVisits + 1 : 0;

    if (m_plugin.takeStateChanged()) {
        // More may follow (a control being dragged): read when it has come to rest.
        m_settleCountdown = SETTLE_VISITS;
        return false;
    }
    if (m_rejected) {
        // The saved state the plug-in refused is kept until the editor has been closed.
        m_settleCountdown = 0;
        return false;
    }
    if (m_settleCountdown > 0) {
        return --m_settleCountdown == 0 && read(base64);
    }
    // A plug-in may change a sound in its editor and tell the host nothing.
    if (editorOpen && m_editorVisits >= EDITOR_POLL_VISITS) {
        m_editorVisits = 0;
        return read(base64);
    }
    return false;
}

bool ClapStateWatch::editorClosed(std::string& base64)
{
    // The musician has had the editor open: what the plug-in holds now is their choice.
    if (m_rejected) {
        m_rejected = false;
        m_hasKnown = false;
    }
    m_settleCountdown = 0;
    m_editorVisits = 0;
    m_plugin.takeStateChanged();
    return read(base64);
}

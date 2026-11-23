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

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <QString>

#include "audio/common/audiotypes.h"

//! CLAP (CLever Audio Plug-in) instruments in the mixer's sound menu: a
//! submenu of their own, "CLAP", beside "VST3", grouped by vendor. A
//! synthesizer installed in both formats appears in both submenus.
namespace mu::playback {
inline const QString CLAP_MENU_ITEM_ID("CLAP");

struct ClapMenuEntry {
    QString itemId;
    QString title;
    bool current = false;
};

struct ClapMenuVendor {
    QString vendor;
    bool current = false;
    std::vector<ClapMenuEntry> entries;
};

//! The menu item of one CLAP plug-in: its type, then its descriptor id.
inline QString clapMenuItemId(const muse::audio::AudioResourceId& resourceId)
{
    return CLAP_MENU_ITEM_ID + "\\" + QString::fromStdString(resourceId);
}

inline bool clapResourceIdOfMenuItem(const QString& menuItemId, muse::audio::AudioResourceId& resourceId)
{
    const QString prefix = CLAP_MENU_ITEM_ID + "\\";
    if (!menuItemId.startsWith(prefix) || menuItemId.size() == prefix.size()) {
        return false;
    }
    resourceId = menuItemId.mid(prefix.size()).toStdString();
    return true;
}

//! The vendors in order, each with its plug-ins sorted by the name shown.
inline std::vector<ClapMenuVendor> clapMenuModel(
    const std::map<muse::audio::AudioResourceVendor, muse::audio::AudioResourceMetaList>& resourcesByVendor,
    const muse::audio::AudioResourceMeta& current)
{
    const bool currentIsClap = current.type == muse::audio::AudioResourceType::ClapPlugin;

    std::vector<ClapMenuVendor> result;
    for (const auto& pair : resourcesByVendor) {
        ClapMenuVendor vendor;
        vendor.vendor = QString::fromStdString(pair.first);
        for (const muse::audio::AudioResourceMeta& meta : pair.second) {
            ClapMenuEntry entry;
            entry.itemId = clapMenuItemId(meta.id);
            // The id is the descriptor id; the plug-in's own name is what a musician reads.
            const muse::String& name = meta.attributeVal(u"clapName");
            entry.title = name.empty() ? QString::fromStdString(meta.id) : name.toQString();
            entry.current = currentIsClap && current.id == meta.id;
            vendor.current = vendor.current || entry.current;
            vendor.entries.push_back(entry);
        }
        std::sort(vendor.entries.begin(), vendor.entries.end(), [](const ClapMenuEntry& first, const ClapMenuEntry& second) {
            return QString::compare(first.title, second.title, Qt::CaseInsensitive) < 0;
        });
        result.push_back(vendor);
    }
    return result;
}

//! The action that opens the editor of a CLAP instrument; empty for any other source.
inline std::string clapInstrumentEditorAction(muse::audio::AudioSourceType sourceType)
{
    return sourceType == muse::audio::AudioSourceType::Clap ? std::string("action://clap/instrument_editor") : std::string();
}
}

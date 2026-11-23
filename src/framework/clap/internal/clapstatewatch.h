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

#include <cstdint>
#include <string>
#include <vector>

namespace muse::clap {
class ClapHostedPlugin;

//! Decides when a plug-in's state is read and reported toward the score.
//! Main thread only.
//!
//! The state may have changed when the plug-in says so in any of the ways a
//! CLAP plug-in can: it marks its state dirty, it reports a parameter value
//! or a gesture on the output events of a process or flush call, or it asks
//! for a rescan of its parameters. The state is then read once the changes
//! have come to rest. While the plug-in's editor is open the state is also
//! read now and then, because a plug-in may change a sound and tell the host
//! nothing. It is read when the editor closes.
//!
//! A state is reported only when its bytes differ from the last ones known
//! (those restored from the score, or those last reported), so a state that
//! did not change never marks the score as modified.
class ClapStateWatch
{
public:
    //! Visits between the last sign of a change and reading the state.
    static constexpr int SETTLE_VISITS = 25;
    //! Visits between two readings while the editor is open.
    static constexpr int EDITOR_POLL_VISITS = 100;

    explicit ClapStateWatch(ClapHostedPlugin& plugin);

    //! What the plug-in holds now is what the score holds.
    void setBaseline();

    //! Applies a state saved as base64 text. True if there was none or the
    //! plug-in took it. After a refusal nothing is reported, so that the
    //! saved state is kept, until the musician has changed the sound.
    bool restore(const std::string& base64);
    bool isRejected() const;

    //! One visit of the main thread. True, with the state as base64 text,
    //! when a new state is to be reported.
    bool visit(std::string& base64);

    //! The editor was closed: the state is read now.
    bool editorClosed(std::string& base64);

private:
    bool read(std::string& base64);

    ClapHostedPlugin& m_plugin;
    std::vector<uint8_t> m_known;
    bool m_hasKnown = false;
    bool m_rejected = false;
    int m_settleCountdown = 0;
    int m_editorVisits = 0;
};
}

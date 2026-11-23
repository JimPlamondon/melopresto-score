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

#include <atomic>
#include <cstring>
#include <vector>

#include "../claptypes.h"

namespace muse::clap {
//! The events handed to a plug-in in one process call, in time order. The
//! storage is allocated once, off the audio thread; push never allocates and
//! refuses an event when the list is full.
class ClapInputEvents
{
public:
    explicit ClapInputEvents(uint32_t capacity = CLAP_EVENT_CAPACITY)
        : m_events(capacity)
    {
        m_list.ctx = this;
        m_list.size = &ClapInputEvents::listSize;
        m_list.get = &ClapInputEvents::listGet;
    }

    ClapInputEvents(const ClapInputEvents&) = delete;
    ClapInputEvents& operator=(const ClapInputEvents&) = delete;

    //! Keeps the list in time order: an event goes after every event already
    //! there whose time is not later than its own.
    bool push(const ClapEventStorage& event)
    {
        if (m_size == m_events.size()) {
            return false;
        }
        uint32_t at = m_size;
        while (at > 0 && m_events[at - 1].header.time > event.header.time) {
            m_events[at] = m_events[at - 1];
            --at;
        }
        m_events[at] = event;
        ++m_size;
        return true;
    }

    void clear() { m_size = 0; }
    uint32_t size() const { return m_size; }
    uint32_t capacity() const { return static_cast<uint32_t>(m_events.size()); }
    const ClapEventStorage& at(uint32_t index) const { return m_events[index]; }

    void replace(uint32_t index, const ClapEventStorage& event) { m_events[index] = event; }
    void truncate(uint32_t size) { m_size = size < m_size ? size : m_size; }

    //! Every event now waits at the first frame of the next block.
    void moveAllToBlockStart()
    {
        for (uint32_t i = 0; i < m_size; ++i) {
            m_events[i].header.time = 0;
        }
    }

    const clap_input_events_t* list() const { return &m_list; }

private:
    static uint32_t CLAP_ABI listSize(const clap_input_events_t* list)
    {
        return static_cast<const ClapInputEvents*>(list->ctx)->m_size;
    }

    static const clap_event_header_t* CLAP_ABI listGet(const clap_input_events_t* list, uint32_t index)
    {
        const ClapInputEvents* self = static_cast<const ClapInputEvents*>(list->ctx);
        return index < self->m_size ? &self->m_events[index].header : nullptr;
    }

    std::vector<ClapEventStorage> m_events;
    uint32_t m_size = 0;
    clap_input_events_t m_list {};
};

//! What a plug-in reports back from a process or flush call. This host keeps
//! none of it; a reported parameter value marks the plug-in's state as changed.
class ClapOutputEvents
{
public:
    ClapOutputEvents()
    {
        m_list.ctx = this;
        m_list.try_push = &ClapOutputEvents::listTryPush;
    }

    ClapOutputEvents(const ClapOutputEvents&) = delete;
    ClapOutputEvents& operator=(const ClapOutputEvents&) = delete;

    const clap_output_events_t* list() const { return &m_list; }

    //! True once, after a plug-in reported a parameter value.
    bool takeParameterChanged() { return m_parameterChanged.exchange(false); }

private:
    static bool CLAP_ABI listTryPush(const clap_output_events_t* list, const clap_event_header_t* event)
    {
        ClapOutputEvents* self = static_cast<ClapOutputEvents*>(list->ctx);
        // A value, or a gesture around one: a control was moved in the plug-in's editor.
        if (event && event->space_id == CLAP_CORE_EVENT_SPACE_ID
            && (event->type == CLAP_EVENT_PARAM_VALUE || event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN
                || event->type == CLAP_EVENT_PARAM_GESTURE_END)) {
            self->m_parameterChanged.store(true);
        }
        return true;
    }

    std::atomic<bool> m_parameterChanged { false };
    clap_output_events_t m_list {};
};
}

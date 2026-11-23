/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
 */

#include "clapactionscontroller.h"

#include "../widgets/clapviewdialog.h"
#include "claphostedplugin.h"

#include "log.h"

using namespace muse;
using namespace muse::clap;

static const char16_t* CLAP_EDITOR_URI = u"muse://clap/editor?instanceId=%1&modal=false&floating=true";

void ClapActionsController::init()
{
    interactiveUriRegister()->registerWidgetUri<ClapViewDialog>(Uri("muse://clap/editor"));
    dispatcher()->reg(this, actions::ActionQuery("action://clap/instrument_editor"), this, &ClapActionsController::instEditor);
}

void ClapActionsController::instEditor(const actions::ActionQuery& actionQuery)
{
    LOGD() << actionQuery.toString();

    const std::string resourceId = actionQuery.param("resourceId").toString();
    IF_ASSERT_FAILED(!resourceId.empty()) {
        LOGE() << "not set resourceId";
        return;
    }

    const int trackId = actionQuery.param("trackId", Val(-1)).toInt();
    const std::string operation = actionQuery.param("operation", Val("open")).toString();
    const bool sync = actionQuery.param("sync", Val(false)).toBool();

    const IClapPluginInstancePtr instance = instancesRegister()->instrumentPlugin(resourceId, trackId);
    if (!instance) {
        if (operation != "close") {
            LOGE() << "not found instance, resourceId: " << resourceId << ", trackId: " << trackId;
        }
        return;
    }

    editorOperation(operation, instance, sync);
}

void ClapActionsController::editorOperation(const std::string& operation, const IClapPluginInstancePtr& instance, bool sync)
{
    const bool close = operation == "close";

    // An editor that lives in a window of the plug-in's own making is shown
    // and hidden by the plug-in; this host supplies no window for it.
    if (instance->isLoaded() && instance->hosted().editorKind() == ClapEditorKind::Floating) {
        ClapHostedPlugin& hosted = instance->hosted();
        if (close) {
            if (hosted.isEditorOpen()) {
                hosted.closeEditor();
                instance->refreshConfig();
            }
        } else if (!hosted.isEditorOpen()) {
            uint32_t width = 0;
            uint32_t height = 0;
            if (!hosted.openEditor(ClapEditorKind::Floating, nullptr, instance->name(), ClapEditorCallbacks(), width, height)) {
                LOGE() << "CLAP plug-in " << instance->resourceId() << " did not open its editor";
            }
        }
        return;
    }

    if (!close && instance->isLoaded() && instance->hosted().editorKind() == ClapEditorKind::None) {
        LOGW() << "CLAP plug-in " << instance->resourceId() << " has no editor for this platform";
        return;
    }

    const UriQuery editorUri = UriQuery(String(CLAP_EDITOR_URI).arg(instance->id()));

    if (close) {
        if (sync) {
            interactive()->closeSync(editorUri);
        } else {
            interactive()->close(editorUri);
        }
        return;
    }

    if (interactive()->isOpened(editorUri).val) {
        interactive()->raise(editorUri);
    } else if (sync) {
        interactive()->openSync(editorUri);
    } else {
        interactive()->open(editorUri);
    }
}

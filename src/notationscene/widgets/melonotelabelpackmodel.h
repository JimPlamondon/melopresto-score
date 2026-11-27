// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#pragma once

class QStandardItemModel;

namespace mu::notation {
/// Fills the model of the Style dialog's "Note-class names:" box: one row per
/// Kernel note-label pack, in the Kernel's order, shown by the display name
/// the pack gives itself and holding the pack's id under Qt::UserRole, the
/// value the box stores in the style value `meloNoteLabelPack` (owner answer
/// 1a, 2026-11-27).
void fillMeloNoteLabelPackModel(QStandardItemModel* model);
}

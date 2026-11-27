// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#include "melonotelabelpackmodel.h"

#include <QStandardItemModel>

#include "engraving/melo/melobridge.h"

namespace mu::notation {
void fillMeloNoteLabelPackModel(QStandardItemModel* model)
{
    std::vector<engraving::melo::NoteLabelPack> packs;
    if (!model || !engraving::melo::noteLabelPacks(packs)) {
        return;
    }
    for (const engraving::melo::NoteLabelPack& pack : packs) {
        auto* item = new QStandardItem(pack.displayName.toQString());
        item->setData(pack.id.toQString(), Qt::UserRole);
        model->appendRow(item);
    }
}
}

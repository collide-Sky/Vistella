// SPDX-License-Identifier: MIT
//
// DialogFactory implementation - F-K (2026-09-09) / P0-3.2 v2 (2026-09-18)
//
#include "DialogFactory.h"
#include "AdjustDialogBase.h"
#include "HslAdjustDialog.h"
#include "CurvesAdjustDialog.h"
#include "LevelsAdjustDialog.h"
#include "BlackWhiteAdjustDialog.h"
#include "ChannelMixerAdjustDialog.h"
#include "logger.h"

namespace dialogs {

QHash<QString, AdjustDialogBase*(*)(const QVariantMap&, QWidget*)>& DialogFactory::creators()
{
    static QHash<QString, AdjustDialogBase*(*)(const QVariantMap&, QWidget*)> map = {
        {"HSL",          &HslAdjustDialog::create},
        {"Curves",       &CurvesAdjustDialog::create},
        {"Levels",       &LevelsAdjustDialog::create},
        {"B&W",          &BlackWhiteAdjustDialog::create},
        {"ChannelMixer", &ChannelMixerAdjustDialog::create},
    };
    return map;
}

AdjustDialogBase* DialogFactory::create(const QString& dialogId, const QVariantMap& args, QWidget* parent)
{
    auto& map = creators();
    auto it = map.find(dialogId);
    if (it == map.end()) {
        LOG_WARN("[DialogFactory] unknown dialogId: {}", dialogId.toStdString());
        return nullptr;
    }
    AdjustDialogBase* dlg = it.value()(args, parent);
    if (dlg) dlg->setInitialArgs(args);
    return dlg;
}

QStringList DialogFactory::knownIds()
{
    auto& map = creators();
    QStringList ids;
    ids.reserve(map.size());
    for (auto it = map.begin(); it != map.end(); ++it) {
        ids << it.key();
    }
    return ids;
}

} // namespace dialogs

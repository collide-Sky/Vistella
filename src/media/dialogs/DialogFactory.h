// SPDX-License-Identifier: MIT
//
// DialogFactory - F-K (2026-09-09)
//
// 5 个色彩调整 dialog 工厂. dialogId:
//   - "HSL"          (F-K 实装)
//   - "Curves"       (F-K.3 stub)
//   - "Levels"       (F-K.3 stub)
//   - "B&W"          (F-K.3 stub)
//   - "ChannelMixer" (F-K.3 stub)
//
// 用法:
//   m_dialogMed->showDialog("HSL", {{"hueShift", 0}, {"satShift", 0}});
//   → DialogFactory::create("HSL", args) -> HslAdjustDialog*
//   → emit dialogShowRequested("HSL", args) -> DialogFactory 接 -> 创建实例
//
#pragma once

#include <QHash>
#include <QString>
#include <QVariantMap>

class QWidget;

namespace dialogs {

class AdjustDialogBase;

class DialogFactory
{
public:
    // 创建 dialog (不 parent 化, 由 caller 负责)
    //   返回 nullptr = dialogId 未知
    static AdjustDialogBase* create(const QString& dialogId, const QVariantMap& args, QWidget* parent = nullptr);

    // 已知 dialogId 列表 (供 test 验证)
    static QStringList knownIds();

private:
    static QHash<QString, AdjustDialogBase*(*)(const QVariantMap&, QWidget*)>& creators();
};

} // namespace dialogs

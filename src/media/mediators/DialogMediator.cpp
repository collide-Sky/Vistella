// SPDX-License-Identifier: MIT
//
// DialogMediator 实现 - F-B3 (2026-09-08)
//
#include "DialogMediator.h"
#include "logger.h"

#include <QDialog>

namespace mediators {

DialogMediator::DialogMediator(QObject* parent) : QObject(parent)
{
    // F-B3 阶段: 框架, 不主动创建 dialog 实例
    //   F-K 阶段 DialogFactory 接 dialogShowRequested 后会创建
    //   这里只做查询 / hide / 状态维护
}

bool DialogMediator::isDialogOpen(const QString& dialogId) const
{
    auto it = m_dialogs.find(dialogId);
    return it != m_dialogs.end() && !it.value().isNull();
}

int DialogMediator::openDialogCount() const
{
    int n = 0;
    for (const auto& p : m_dialogs) {
        if (!p.isNull()) ++n;
    }
    return n;
}

void DialogMediator::showDialog(const QString& dialogId, const QVariantMap& args)
{
    auto it = m_dialogs.find(dialogId);
    if (it != m_dialogs.end() && !it.value().isNull()) {
        // 已有实例: 复用 + 提到前面
        QDialog* dlg = it.value().data();
        dlg->raise();
        dlg->activateWindow();
        if (dlg->isHidden()) dlg->show();
        LOG_DEBUG("[DialogMed] showDialog reuse: id={}", dialogId.toStdString());
        return;
    }
    // 没实例: 转发给 DialogFactory (F-K 阶段连)
    LOG_INFO("[DialogMed] showDialog request: id={} args={}", dialogId.toStdString(), args.size());
    emit dialogShowRequested(dialogId, args);
}

void DialogMediator::hideDialog(const QString& dialogId)
{
    auto it = m_dialogs.find(dialogId);
    if (it == m_dialogs.end()) return;
    QDialog* dlg = it.value().data();
    if (!dlg) return;
    if (dlg->isVisible()) dlg->hide();
    LOG_DEBUG("[DialogMed] hideDialog: id={}", dialogId.toStdString());
    emit dialogHidden(dialogId);
}

void DialogMediator::hideAllDialogs()
{
    for (auto& p : m_dialogs) {
        if (!p.isNull() && p.data()->isVisible()) p.data()->hide();
    }
    // 不 emit dialogHidden (避免多次信号)
    LOG_DEBUG("[DialogMed] hideAllDialogs: count={}", m_dialogs.size());
}

void DialogMediator::onDialogDestroyed(QObject* obj)
{
    // F-K DialogFactory 创建 dialog 后会 connect destroyed 到这个 slot
    //   dialog 销毁时, 自动从 m_dialogs 移除
    for (auto it = m_dialogs.begin(); it != m_dialogs.end(); ++it) {
        if (it.value().data() == obj) {
            LOG_DEBUG("[DialogMed] onDialogDestroyed: id={}", it.key().toStdString());
            m_dialogs.erase(it);
            return;
        }
    }
}

} // namespace mediators

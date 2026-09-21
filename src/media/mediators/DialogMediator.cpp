// SPDX-License-Identifier: MIT
//
// DialogMediator 实现 - F-B3 (2026-09-08) / P0 leftover 4 (2026-09-21)
//
#include "DialogMediator.h"
#include "../dialogs/DialogFactory.h"
#include "../dialogs/AdjustDialogBase.h"
#include "logger.h"

#include <QDialog>
#include <QApplication>

namespace mediators {

DialogMediator::DialogMediator(QObject* parent) : QObject(parent)
{
    // P0 leftover 4 (2026-09-21): self-wire to DialogFactory. The mediator
    //   owns dialog lifecycle now: showDialog emits dialogShowRequested,
    //   which we listen to internally via onShowDialogRequested, which
    //   calls DialogFactory::create. This removes the F-B3 stub state
    //   where the signal had no consumer and showDialog was effectively
    //   dead code.
    connect(this, &DialogMediator::dialogShowRequested,
            this, &DialogMediator::onShowDialogRequested);
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
    // 没实例: emit dialogShowRequested, 由 onShowDialogRequested 创建
    LOG_INFO("[DialogMed] showDialog request: id={} args={}", dialogId.toStdString(), args.size());
    emit dialogShowRequested(dialogId, args);
}

void DialogMediator::onShowDialogRequested(const QString& dialogId,
                                            const QVariantMap& args)
{
    // P0 leftover 4 (2026-09-21): DialogFactory creates the dialog.
    //   Parent it to the QApplication's active window so dialogs center
    //   on the user's current ImageWindow. Store the pointer (QPointer
    //   handles dialog destruction automatically).
    QWidget* parent = QApplication::activeWindow();
    dialogs::AdjustDialogBase* dlg = dialogs::DialogFactory::create(dialogId, args, parent);
    if (!dlg) {
        LOG_WARN("[DialogMed] create returned null: id={}", dialogId.toStdString());
        return;
    }
    connect(dlg, &QObject::destroyed, this, &DialogMediator::onDialogDestroyed);
    m_dialogs.insert(dialogId, dlg);
    // P0 leftover 5 (2026-09-21): notify subscribers (ImageWindow) so they
    //   can wire per-dialog signals (applied -> AdjustmentPanel). The
    //   signal fires before show() so subscribers can connect before the
    //   user interacts.
    emit dialogCreated(dialogId, dlg);
    dlg->show();
    LOG_DEBUG("[DialogMed] onShowDialogRequested created: id={}", dialogId.toStdString());
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
    // P0 leftover 4 (2026-09-21): dialog 销毁时, 自动从 m_dialogs 移除
    for (auto it = m_dialogs.begin(); it != m_dialogs.end(); ++it) {
        if (it.value().data() == obj) {
            LOG_DEBUG("[DialogMed] onDialogDestroyed: id={}", it.key().toStdString());
            m_dialogs.erase(it);
            return;
        }
    }
}

} // namespace mediators

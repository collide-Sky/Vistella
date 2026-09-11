// SPDX-License-Identifier: MIT
//
// DialogMediator - F-B3 (2026-09-08)
//
// Mediator 模式, 1 个类只负责"非模态 dialog 调度"一个方向
//   转发: 菜单/工具栏 → DialogMediator → 实际 dialog 实例 (showDialog / hideDialog)
//
// 设计原则 (跟 ToolMediator / WorkspaceMediator 一致, 一次性到位):
//   1) 单例 per ImageWindow
//   2) 14 个 dialog id (F-K 5 dialog + F-J 6 主菜单触发 + 后续 F-N~F-P)
//   3) close=hide (PS 同款), 不销毁, 状态保留
//   4) QPointer<QDialog> 存实例, dialog 关闭时自动 null
//   5) 强约束: 不动 setMode/toggleMode/m_normalSize (主窗口状态机)
//
// 实际 dialog 加载 (F-K 阶段):
//   showDialog("HSL", args) 调 DialogFactory 创建 HSLDialog 实例
//   dialog 关闭 → closeEvent 触发 hide(), 不 delete
//   再次 showDialog("HSL", args) → 复用已有实例 (避免重初始化开销)
//
// 用法:
//   m_dialogMed->showDialog("HSL", {{"hueShift", 0}, {"satShift", 0}});
//   m_dialogMed->hideDialog("HSL");
//   m_dialogMed->isDialogOpen("HSL");
//
#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QVariantMap>
#include <QString>

class QDialog;

namespace mediators {

class DialogMediator : public QObject
{
    Q_OBJECT
public:
    explicit DialogMediator(QObject* parent = nullptr);
    ~DialogMediator() override = default;

    // 查询
    bool isDialogOpen(const QString& dialogId) const;
    int  openDialogCount() const;

public slots:
    // 打开/激活 dialog
    //   第一次: 创建实例 + 显
    //   后续: 复用已有实例 + raise() + activateWindow()
    void showDialog(const QString& dialogId, const QVariantMap& args = {});
    // 隐藏 dialog (close=hide, 不销毁)
    void hideDialog(const QString& dialogId);
    // 隐藏全部 dialog (切换工作区/文档时调)
    void hideAllDialogs();

signals:
    // 订阅方 listen:
    //   DialogFactory (F-K 阶段实装): 收到 showDialog 信号后创建 dialog 实例
    //   其他 Mediator (切换工作区时关掉无关 dialog)
    void dialogShowRequested(const QString& dialogId, const QVariantMap& args);
    // dialog 关闭/隐藏通知 (供订阅方清理 UI 状态)
    void dialogHidden(const QString& dialogId);

private slots:
    // 监听 dialog destroyed 信号, 自动从 m_dialogs 移除
    void onDialogDestroyed(QObject* obj);

private:
    // 实际 dialog 实例 (QPointer 自动处理 dialog 销毁)
    //   F-B3 阶段只建框架, 实例创建由 DialogFactory (F-K) 接 dialogShowRequested 后做
    QHash<QString, QPointer<QDialog>> m_dialogs;
};

} // namespace mediators

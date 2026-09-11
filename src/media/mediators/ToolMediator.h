// SPDX-License-Identifier: MIT
//
// ToolMediator - F-B1 (2026-09-08)
//
// Mediator 模式, 1 个类只负责"工具切换 + 工具配置变更"一个方向
//   转发: 调用方 → ToolMediator → 订阅方 (LeftToolBar / ImageOptionBar / ImageCanvas / DialogMediator)
//
// 设计原则 (一次性到位):
//   1) 单例 per ImageWindow (不是全局) — 多 ImageWindow 互不干扰
//   2) QObject 父对象 = ImageWindow, 跟随 ImageWindow 析构
//   3) 调用方只跟 Mediator 通信, 订阅方只 listen Mediator 信号
//   4) 不持 ImageWindow 内部 state (m_currentTool / m_inUndoRedo 等不在这里)
//   5) 强约束: 不动 setMode/toggleMode/m_normalSize/m_btnMax (主窗口状态机)
//
// 用法:
//   // 调用方
//   m_toolMed->switchTool(ToolId::Move);
//   m_toolMed->applyToolConfig(ToolId::RectSelect, {{"feather", 5}});
//
//   // 订阅方
//   connect(m_toolMed, &ToolMediator::toolSwitched, this, [this](ToolId id) {
//       m_stackedWidget->setCurrentIndex(static_cast<int>(id));
//   });
//
#pragma once

#include <QObject>
#include <QVariantMap>
#include <QString>

namespace mediators {

// 工具 ID 枚举 (F-C 阶段实装各 ToolState 时用)
//   数字顺序 = LeftToolBar 2 列图标显示顺序
enum class ToolId {
    None        = 0,   // 不选工具 (默认)
    Move        = 1,   // 移动 (V)
    RectSelect  = 2,   // 矩形选框 (M)
    Lasso       = 3,   // 套索 (L)
    MagicWand   = 4,   // 魔棒 (W)
    Crop        = 5,   // 裁剪 (C)
    Text        = 6,   // 文字 (T)
    Brush       = 7,   // 画笔 (B)
    Eyedropper  = 8,   // 吸管 (I)
};

class ToolMediator : public QObject
{
    Q_OBJECT
public:
    explicit ToolMediator(QObject* parent = nullptr);
    ~ToolMediator() override = default;

    // 当前激活工具 (F-C 实装 ToolState 后由 ToolContext 同步, 这里先存)
    ToolId currentTool() const { return m_current; }
    // 当前工具的配置 (tool panel 改值时存, 切换时清)
    QVariantMap currentConfig(ToolId id) const;

public slots:
    // 调用方 API:
    //   切工具 (LeftToolBar 按钮 / 菜单 / 快捷键)
    void switchTool(ToolId id);
    // 改工具配置 (二级工具栏 ImageOptionBar 调值时)
    void applyToolConfig(ToolId id, const QVariantMap& cfg);
    // 重置某个工具到默认配置
    void resetToolConfig(ToolId id);

signals:
    // 订阅方 listen:
    //   工具切换 (LeftToolBar 高亮 / ImageOptionBar 切 page / ImageCanvas 装 eventFilter)
    void toolSwitched(ToolId id);
    //   工具配置变更 (ImageCanvas cursor / ToolState 内部参数)
    void toolConfigApplied(ToolId id, const QVariantMap& cfg);
    //   工具重置 (二级工具栏控件复位)
    void toolConfigReset(ToolId id);

private:
    ToolId m_current = ToolId::None;
    // 8 工具各存一份 config (Map<ToolId, QVariantMap>)
    //   切换工具时, ImageOptionBar 读对应 id 的 config 来初始化 UI
    QHash<ToolId, QVariantMap> m_configs;
};

} // namespace mediators

Q_DECLARE_METATYPE(mediators::ToolId)

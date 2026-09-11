// SPDX-License-Identifier: MIT
//
// AdjustDialogBase - F-K (2026-09-09)
//
// PS 风格色彩调整 dialog 基类. 子类 override:
//   - setupUi(QVBoxLayout*): 装子类专属控件
//   - applyAdjust(): 应用调整到 image (OK/Apply 触发)
//   - preview(): 实时预览 (子控件 valueChanged 触发)
//
// 统一定义:
//   - OK / Cancel / Apply 3 按钮 (PS 同款, Apply 实时预览, OK 提交, Cancel 关闭不保存)
//   - 状态: m_modified (子控件改过, Apply 启用)
//   - 信号: applied() / cancelled() / closed()
//
#pragma once

#include <QDialog>
#include <QVariantMap>

class QVBoxLayout;
class QDialogButtonBox;
class QPushButton;

namespace dialogs {

class AdjustDialogBase : public QDialog
{
    Q_OBJECT
public:
    explicit AdjustDialogBase(const QString& title, QWidget* parent = nullptr);
    ~AdjustDialogBase() override;

    // 设置初始参数 (子控件回显用)
    void setInitialArgs(const QVariantMap& args);
    QVariantMap currentArgs() const { return m_args; }

signals:
    // 实时预览 (子控件 valueChanged 调 emit preview(args))
    void preview(const QVariantMap& args);
    // OK / Apply 提交 (applyAdjust 完成, 参数已应用)
    void applied(const QVariantMap& args);

protected:
    // 子类必须 override
    virtual void setupUi(QVBoxLayout* body) = 0;
    // 子类默认实现: 直接 emit preview(args), OK/Apply 触发后 emit applied
    virtual void applyAdjust();
    // F-K (2026-09-09): derived ctor 调 base ctor 后调 init() 触发 setupUi
    //   避免 base ctor 调 pure virtual 链接错
    void init();

    // 子类调 (改任何参数后): 1) 更新 m_args 2) 触发实时预览
    void updateParam(const QString& key, const QVariant& value);
    void triggerPreview();

    QVariantMap m_args;

private slots:
    void onApplyClicked();
    void onAccepted();
    void onRejected();

private:
    QVBoxLayout*     m_body       = nullptr;
    QDialogButtonBox* m_btnBox    = nullptr;
    QPushButton*     m_applyBtn   = nullptr;
    bool             m_modified   = false;
};

} // namespace dialogs

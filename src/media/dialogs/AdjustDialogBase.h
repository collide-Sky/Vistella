// SPDX-License-Identifier: MIT
//
// AdjustDialogBase
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
    // The lifecycle of the preview session (M2-6a).
    //
    // When the host (ImageWindow) receives previewSessionStarted it takes the
    // baseline snapshot in the AdjustmentPanel; when it receives
    // previewSessionEnded(true) it freezes the previewed state into EXACTLY ONE
    // undo command; when it receives previewSessionEnded(false) it restores the canvas to the state before the dialog opened.
    //
    // committed=false means Cancel / reject -- the host has to roll back.
    void previewSessionStarted();
    void previewSessionEnded(bool committed);

protected:
    // 子类必须 override
    virtual void setupUi(QVBoxLayout* body) = 0;
    // 子类默认实现: 直接 emit preview(args), OK/Apply 触发后 emit applied
    virtual void applyAdjust();
    // derived ctor 调 base ctor 后调 init 触发 setupUi
    //   避免 base ctor 调 pure virtual 链接错
    void init();

    // 子类调 (改任何参数后): 1) 更新 m_args 2) 触发实时预览
    void updateParam(const QString& key, const QVariant& value);
    void triggerPreview();

    // Arm the preview in showEvent (only after the first show may triggerPreview really emit)
    void showEvent(QShowEvent* event) override;

public:
    // The single entry point for closing the dialog.
    //
    // QDialog::reject() is the entry point of the Esc key, of the window close button
    // and of any programmatic call; QDialogButtonBox::rejected is only one of those paths.
    // So the end of the preview session has to hang on reject() / accept() and not only on the button slots -- otherwise
    // pressing Esc or calling reject() programmatically would not roll back.
    void reject() override;
    void accept() override;

protected:

    QVariantMap m_args;

private slots:
    void onApplyClicked();
    void onAccepted();
    void onRejected();

private:
    // The state of the preview session (M2-6a)
    //
    // m_previewArmed: set to true after the first showEvent. setInitialArgs in the constructor
    //   calls setValue(), which fires valueChanged -> onSliderChanged -> triggerPreview.
    //   At that moment the dialog is not visible yet, so it must not preview (otherwise the canvas would change as soon as the dialog opens).
    // m_sessionStarted: previewSessionStarted has been emitted.
    // m_sessionCommitted: the last session ended with a commit (after Apply the
    //   user can keep previewing, and this flag decides whether previewSessionStarted is emitted again).
    void startPreviewSession();
    void endPreviewSession(bool committed);

    QVBoxLayout*     m_body       = nullptr;
    QDialogButtonBox* m_btnBox    = nullptr;
    QPushButton*     m_applyBtn   = nullptr;
    bool             m_modified   = false;
    bool             m_previewArmed   = false;
    bool             m_sessionStarted = false;
    bool             m_sessionCommitted = false;
};

} // namespace dialogs

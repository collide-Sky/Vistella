// SPDX-License-Identifier: MIT
//
// AdjustDialogBase implementation
//
#include "AdjustDialogBase.h"
#include "logger.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QShowEvent>
#include <QVBoxLayout>

namespace dialogs {

AdjustDialogBase::AdjustDialogBase(const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    resize(420, 320);

    QVBoxLayout* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(8);

    m_body = new QVBoxLayout;
    m_body->setContentsMargins(0, 0, 0, 0);
    m_body->setSpacing(6);
    outer->addLayout(m_body, 1);

    // 不在 ctor 调 setupUi: ctor 调 pure virtual 编译生成直接 call 到 base::setupUi
    //   (vtable 还没指向 derived), 链接会找不到 body
    // 改: derived ctor 调 base ctor 后调 init() 触发 setupUi

    // 按钮 (OK / Cancel / Apply)
    m_btnBox = new QDialogButtonBox(this);
    m_btnBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    m_applyBtn = m_btnBox->button(QDialogButtonBox::Apply);
    m_applyBtn->setEnabled(false);  // 默认禁用, 改参数后启用
    outer->addWidget(m_btnBox);

    connect(m_applyBtn, &QPushButton::clicked, this, &AdjustDialogBase::onApplyClicked);
    connect(m_btnBox, &QDialogButtonBox::accepted, this, &AdjustDialogBase::onAccepted);
    connect(m_btnBox, &QDialogButtonBox::rejected, this, &AdjustDialogBase::onRejected);
}

void AdjustDialogBase::init()
{
    // derived ctor 调 base ctor 后调 init 触发 setupUi
    //   避免 base ctor 调 virtual function 编译直接 call base::setupUi 链接错
    if (m_body) setupUi(m_body);
}

AdjustDialogBase::~AdjustDialogBase() = default;

void AdjustDialogBase::setInitialArgs(const QVariantMap& args)
{
    m_args = args;
    // 子类在 setupUi 完成后调, 用来回显
}

void AdjustDialogBase::updateParam(const QString& key, const QVariant& value)
{
    m_args.insert(key, value);
    m_modified = true;
    if (m_applyBtn) m_applyBtn->setEnabled(true);
}

void AdjustDialogBase::triggerPreview()
{
    // No preview before the dialog was shown.
    //
    // setInitialArgs() in the constructor calls setValue() on the sliders, which fires
    // valueChanged -> onSliderChanged -> triggerPreview(). At that moment the dialog
    // is not visible yet and the host has no baseline snapshot -- a preview could not be rolled back.
    // So nothing is emitted before the first showEvent.
    if (!m_previewArmed) {
        LOG_DEBUG("[AdjustDialog] preview suppressed (not armed yet)");
        return;
    }
    LOG_DEBUG("[AdjustDialog] preview: {}", m_args.size());
    // Start the session before the first real preview, so the host can take the baseline.
    startPreviewSession();
    emit preview(m_args);
}

void AdjustDialogBase::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    // Arm the preview after the first show.
    if (!m_previewArmed) {
        m_previewArmed = true;
        LOG_DEBUG("[AdjustDialog] preview armed");
    }
}

void AdjustDialogBase::startPreviewSession()
{
    if (m_sessionStarted && !m_sessionCommitted) return;
    m_sessionStarted   = true;
    m_sessionCommitted = false;
    emit previewSessionStarted();
}

void AdjustDialogBase::endPreviewSession(bool committed)
{
    if (!m_sessionStarted) return;
    m_sessionStarted   = false;
    m_sessionCommitted = committed;
    emit previewSessionEnded(committed);
}

void AdjustDialogBase::applyAdjust()
{
    // 子类 override, 默认 no-op
    LOG_INFO("[AdjustDialog] applyAdjust: {}", m_args.size());
    // Make sure the session is running before the commit (the user may not have touched any control,
    // and may just press Apply / OK).
    startPreviewSession();
    emit applied(m_args);
}

void AdjustDialogBase::onApplyClicked()
{
    m_modified = false;
    if (m_applyBtn) m_applyBtn->setEnabled(false);
    applyAdjust();
    // Apply: freeze the current state into ONE undo command, but do NOT end the session --
    // the dialog stays open and the user can keep adjusting and previewing.
    // The baseline becomes "the state that was just committed", so the next Cancel returns here.
    endPreviewSession(/*committed=*/true);
}

void AdjustDialogBase::onAccepted()
{
    accept();
}

void AdjustDialogBase::onRejected()
{
    reject();
}

void AdjustDialogBase::accept()
{
    if (m_modified) {
        applyAdjust();
    }
    // OK: commit and end the session. The host freezes the previewed state into one undo command.
    endPreviewSession(/*committed=*/true);
    QDialog::accept();
}

void AdjustDialogBase::reject()
{
    LOG_DEBUG("[AdjustDialog] cancelled");
    // Cancel: first let the host restore the canvas to the state before the dialog opened (without leaving an undo entry),
    // and only then close the dialog.
    endPreviewSession(/*committed=*/false);
    QDialog::reject();
}

} // namespace dialogs

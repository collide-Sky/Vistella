// SPDX-License-Identifier: MIT
//
// AdjustDialogBase implementation - F-K (2026-09-09)
//
#include "AdjustDialogBase.h"
#include "logger.h"

#include <QDialogButtonBox>
#include <QPushButton>
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
    // F-K (2026-09-09): derived ctor 调 base ctor 后调 init() 触发 setupUi
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
    LOG_DEBUG("[AdjustDialog] preview: {}", m_args.size());
    emit preview(m_args);
}

void AdjustDialogBase::applyAdjust()
{
    // 子类 override, 默认 no-op
    LOG_INFO("[AdjustDialog] applyAdjust: {}", m_args.size());
    emit applied(m_args);
}

void AdjustDialogBase::onApplyClicked()
{
    m_modified = false;
    if (m_applyBtn) m_applyBtn->setEnabled(false);
    applyAdjust();
}

void AdjustDialogBase::onAccepted()
{
    if (m_modified) {
        applyAdjust();
    }
    accept();
}

void AdjustDialogBase::onRejected()
{
    LOG_DEBUG("[AdjustDialog] cancelled");
    reject();
}

} // namespace dialogs
